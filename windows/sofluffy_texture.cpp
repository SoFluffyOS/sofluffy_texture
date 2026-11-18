// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Shared D3D11 textures as a Flutter texture: a GPU surface texture of type
// DXGI shared handle, which Flutter opens on its own device. Flutter is
// handed the handle of the latest frame whenever it asks.
//
// No fence is waited on: the caller presents a buffer only once its GPU has
// drawn it, and a buffer goes back to the caller once the next one is
// bound, by when the drawing that read it last was submitted a frame
// earlier.
#include "include/sofluffy_texture/sofluffy_texture.h"
#include "include/sofluffy_texture/sofluffy_texture_plugin.h"

#include <memory>
#include <mutex>
#include <vector>

namespace {

// More buffers than a set will ever have.
constexpr size_t kMaxBuffers = 64;

// One set of buffers as the caller described it: each texture's shared
// handle, in index order, and the textures themselves when the caller
// gave them, each with a reference held.
struct BufferSet {
  uint32_t generation = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<HANDLE> handles;
  std::vector<ID3D11Texture2D*> textures;

  ~BufferSet() {
    for (ID3D11Texture2D* texture : textures) texture->Release();
  }
};

}  // namespace

struct SftTexture {
  FlutterDesktopTextureRegistrarRef registrar = nullptr;
  int64_t id = -1;
  SftBufferReleased released = nullptr;
  void* user_data = nullptr;
  FlutterDesktopGpuSurfaceDescriptor descriptor{};
  std::mutex mutex;
  // The set waiting to be shown, and the one shown; the raster thread takes
  // pending over when Flutter asks for a surface.
  std::unique_ptr<BufferSet> pending;
  std::unique_ptr<BufferSet> current;
  // The frame named last, -1 for none.
  uint32_t shown_generation = 0;
  int32_t shown_index = -1;
  // The buffer handed to Flutter last, -1 for none: given back once another
  // is.
  uint32_t bound_generation = 0;
  int32_t bound_index = -1;
  // Disposed: nothing is handed out and no callback runs.
  bool disposed = false;

  // Gives buffer |index| of |generation| back to the caller. Mutex held.
  void GiveBack(uint32_t generation, int32_t index) {
    if (released != nullptr && !disposed) released(user_data, generation, index);
  }

  // On the raster thread: the latest frame's texture.
  const FlutterDesktopGpuSurfaceDescriptor* Surface() {
    std::lock_guard<std::mutex> lock(mutex);
    if (disposed) return nullptr;
    // A set waits until a frame of it is named, the first one too: until
    // then the frame shown is of the set at hand, if any, and the new
    // set's buffers may be being drawn or never drawn.
    if (pending && shown_index >= 0 && pending->generation == shown_generation) {
      current = std::move(pending);
    }
    if (!current || current->handles.empty()) return nullptr;
    const BufferSet& set = *current;
    // The frame named is not of this set: the buffer bound last stays, and
    // with nothing of this set bound, nothing shows: any other buffer of it
    // may be the one being drawn.
    int32_t index = -1;
    if (bound_generation == set.generation && bound_index >= 0 &&
        static_cast<size_t>(bound_index) < set.handles.size()) {
      index = bound_index;
    }
    if (shown_generation == set.generation && shown_index >= 0 &&
        static_cast<size_t>(shown_index) < set.handles.size()) {
      index = shown_index;
      const bool rebound =
          bound_index != shown_index || bound_generation != set.generation;
      // The buffer bound before was last read by the frame drawn before
      // this one: the caller's again.
      if (rebound && bound_index >= 0) GiveBack(bound_generation, bound_index);
      bound_generation = set.generation;
      bound_index = shown_index;
    }
    if (index < 0) return nullptr;
    descriptor = {};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.handle = set.handles[static_cast<size_t>(index)];
    descriptor.width = descriptor.visible_width = set.width;
    descriptor.height = descriptor.visible_height = set.height;
    descriptor.format = kFlutterDesktopPixelFormatBGRA8888;
    return &descriptor;
  }

  void Take(std::unique_ptr<BufferSet> set) {
    bool named;
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (disposed) return;
      named = shown_index >= 0 && set->generation == shown_generation;
      pending = std::move(set);
    }
    // The frame was named before its set came: Flutter asked for it then
    // and keeps what it got, so it must ask again, or a still picture stays
    // as it was.
    if (named) {
      FlutterDesktopTextureRegistrarMarkExternalTextureFrameAvailable(registrar, id);
    }
  }
};

SftTexture* sft_texture_create(FlutterDesktopTextureRegistrarRef registrar,
                               SftBufferReleased released,
                               void* user_data) {
  if (registrar == nullptr) return nullptr;
  auto texture = std::make_unique<SftTexture>();
  texture->registrar = registrar;
  texture->released = released;
  texture->user_data = user_data;
  FlutterDesktopTextureInfo info = {};
  info.type = kFlutterDesktopGpuSurfaceTexture;
  info.gpu_surface_config.struct_size = sizeof(FlutterDesktopGpuSurfaceTextureConfig);
  info.gpu_surface_config.type = kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle;
  info.gpu_surface_config.callback =
      [](size_t, size_t, void* self) -> const FlutterDesktopGpuSurfaceDescriptor* {
    return static_cast<SftTexture*>(self)->Surface();
  };
  info.gpu_surface_config.user_data = texture.get();
  texture->id = FlutterDesktopTextureRegistrarRegisterExternalTexture(registrar, &info);
  if (texture->id < 0) return nullptr;
  return texture.release();
}

int64_t sft_texture_id(const SftTexture* texture) {
  return texture == nullptr ? -1 : texture->id;
}

bool sft_texture_set_d3d11_textures(SftTexture* texture,
                                    uint32_t generation,
                                    ID3D11Texture2D* const* textures,
                                    size_t count) {
  if (texture == nullptr || textures == nullptr || count == 0 || count > kMaxBuffers) {
    return false;
  }
  auto set = std::make_unique<BufferSet>();
  set->generation = generation;
  for (size_t i = 0; i < count; ++i) {
    ID3D11Texture2D* each = textures[i];
    if (each == nullptr) return false;
    IDXGIResource* resource = nullptr;
    if (FAILED(each->QueryInterface(__uuidof(IDXGIResource),
                                    reinterpret_cast<void**>(&resource)))) {
      return false;
    }
    HANDLE handle = nullptr;
    const HRESULT got = resource->GetSharedHandle(&handle);
    resource->Release();
    if (FAILED(got) || handle == nullptr) return false;
    if (i == 0) {
      D3D11_TEXTURE2D_DESC description;
      each->GetDesc(&description);
      set->width = description.Width;
      set->height = description.Height;
    }
    each->AddRef();
    set->textures.push_back(each);
    set->handles.push_back(handle);
  }
  texture->Take(std::move(set));
  return true;
}

bool sft_texture_set_shared_handles(SftTexture* texture,
                                    uint32_t generation,
                                    uint32_t width,
                                    uint32_t height,
                                    const HANDLE* handles,
                                    size_t count) {
  if (texture == nullptr || handles == nullptr || count == 0 || count > kMaxBuffers) {
    return false;
  }
  auto set = std::make_unique<BufferSet>();
  set->generation = generation;
  set->width = width;
  set->height = height;
  set->handles.assign(handles, handles + count);
  texture->Take(std::move(set));
  return true;
}

void sft_texture_present(SftTexture* texture, uint32_t generation, int32_t index) {
  if (texture == nullptr || index < 0) return;
  {
    std::lock_guard<std::mutex> lock(texture->mutex);
    if (texture->disposed) return;
    // A frame named but never bound (Flutter did not get to it) was never
    // read: the caller's again at once. Without this a texture nobody
    // draws (off screen, a hidden tab) would keep every buffer it is
    // given, and whoever fills the set would run out.
    const bool superseded =
        texture->shown_index >= 0 &&
        (texture->shown_index != index || texture->shown_generation != generation);
    const bool unbound = texture->shown_index != texture->bound_index ||
                         texture->shown_generation != texture->bound_generation;
    if (superseded && unbound) {
      texture->GiveBack(texture->shown_generation, texture->shown_index);
    }
    texture->shown_generation = generation;
    texture->shown_index = index;
  }
  FlutterDesktopTextureRegistrarMarkExternalTextureFrameAvailable(texture->registrar,
                                                                  texture->id);
}

void sft_texture_dispose(SftTexture* texture) {
  if (texture == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(texture->mutex);
    texture->disposed = true;
  }
  // Flutter lets go of the texture on its own time; it lives until then,
  // its buffers with it: a frame under way may still open one.
  FlutterDesktopTextureRegistrarUnregisterExternalTexture(
      texture->registrar, texture->id,
      [](void* self) { delete static_cast<SftTexture*>(self); }, texture);
}

void SofluffyTexturePluginRegisterWithRegistrar(FlutterDesktopPluginRegistrarRef registrar) {}
