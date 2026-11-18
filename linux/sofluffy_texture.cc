// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// dmabufs as a Flutter texture. The buffers are imported into Flutter's own
// GL context the first time it is asked to populate the texture with a
// frame of their set, and from then on it populates with the buffer of the
// latest frame.
//
// Each frame may come with a fence: where EGL has native fences, Flutter's
// context waits on it before sampling. A buffer goes back to the caller
// once the next one is bound, with a fence for the drawing that sampled it
// last.
#include "include/sofluffy_texture/sofluffy_texture.h"
#include "include/sofluffy_texture/sofluffy_texture_plugin.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>

namespace {

// More buffers than a set will ever have.
constexpr size_t kMaxBuffers = 64;

// One set of buffers as the caller described it, each descriptor a
// duplicate owned here until imported.
struct BufferSet {
  uint32_t generation = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t fourcc = 0;
  uint64_t modifier = 0;
  std::vector<uint32_t> strides;
  std::vector<uint32_t> offsets;
  std::vector<int> fds;

  ~BufferSet() {
    for (int fd : fds) {
      if (fd >= 0) close(fd);
    }
  }
};

struct Imported {
  EGLDisplay display = EGL_NO_DISPLAY;
  // The GL context the textures were made in: another engine's window may
  // not share it.
  EGLContext context = EGL_NO_CONTEXT;
  // The engine, by its texture registrar, whose texture imported it.
  uint64_t owner = 0;
  uint32_t generation = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<EGLImageKHR> images;
  std::vector<GLuint> textures;
};

// Deletes |imported|'s GL textures and EGL images. Raster thread, Flutter's
// context current.
void destroy_set(Imported& imported) {
  const auto destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  for (GLuint texture : imported.textures) {
    if (texture != 0) glDeleteTextures(1, &texture);
  }
  for (EGLImageKHR image : imported.images) {
    if (image != EGL_NO_IMAGE_KHR && destroy_image != nullptr) {
      destroy_image(imported.display, image);
    }
  }
  imported.textures.clear();
  imported.images.clear();
}

// Sets of disposed textures: their GL objects belong to Flutter's context,
// current only on the raster thread, so the next populate in that context
// deletes them. Each holds a set's buffers until then.
std::mutex graveyard_mutex;
std::vector<std::unique_ptr<Imported>> graveyard;

// Each engine's owner id, by its texture registrar, and the ids of engines
// gone, with their GL contexts. Graveyard mutex.
std::map<FlTextureRegistrar*, uint64_t> owners;
std::set<uint64_t> gone_owners;
uint64_t next_owner = 1;

// Lets go of |imported|'s EGL images only: its context is gone, and its
// texture names with it; deleting them in another context would delete
// that one's.
void release_images(Imported& imported) {
  const auto destroy_image = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      eglGetProcAddress("eglDestroyImageKHR"));
  for (EGLImageKHR image : imported.images) {
    if (image != EGL_NO_IMAGE_KHR && destroy_image != nullptr) {
      destroy_image(imported.display, image);
    }
  }
  imported.images.clear();
  imported.textures.clear();
}

void bury(std::unique_ptr<Imported> imported) {
  if (!imported) return;
  std::lock_guard<std::mutex> lock(graveyard_mutex);
  if (gone_owners.count(imported->owner) != 0) {
    release_images(*imported);
    return;
  }
  graveyard.push_back(std::move(imported));
}

// |registrar|'s engine is gone: its sets leave the graveyard, so a new
// context at the same address never takes them for its own.
void registrar_gone(gpointer, GObject* registrar) {
  std::lock_guard<std::mutex> lock(graveyard_mutex);
  const auto found = owners.find(reinterpret_cast<FlTextureRegistrar*>(registrar));
  if (found == owners.end()) return;
  const uint64_t owner = found->second;
  owners.erase(found);
  gone_owners.insert(owner);
  for (auto it = graveyard.begin(); it != graveyard.end();) {
    if ((*it)->owner == owner) {
      release_images(**it);
      it = graveyard.erase(it);
    } else {
      ++it;
    }
  }
}

// Tells |registrar|'s sets from another engine's.
uint64_t owner_of(FlTextureRegistrar* registrar) {
  std::lock_guard<std::mutex> lock(graveyard_mutex);
  const auto found = owners.find(registrar);
  if (found != owners.end()) return found->second;
  const uint64_t owner = next_owner++;
  owners[registrar] = owner;
  g_object_weak_ref(G_OBJECT(registrar), registrar_gone, nullptr);
  return owner;
}

// Raster thread, context current: deletes the sets made in it.
void empty_graveyard() {
  const EGLContext context = eglGetCurrentContext();
  std::vector<std::unique_ptr<Imported>> sets;
  {
    std::lock_guard<std::mutex> lock(graveyard_mutex);
    for (auto it = graveyard.begin(); it != graveyard.end();) {
      if ((*it)->context == context) {
        sets.push_back(std::move(*it));
        it = graveyard.erase(it);
      } else {
        ++it;
      }
    }
  }
  for (auto& set : sets) destroy_set(*set);
}

// What a texture keeps, apart from the GObject it is.
struct State {
  std::mutex mutex;
  // Marks a frame available; null once disposed or once the engine is gone.
  FlTextureRegistrar* registrar = nullptr;
  uint64_t owner = 0;
  SftBufferReleased released = nullptr;
  gpointer user_data = nullptr;
  // The set waiting to be imported, and the one imported, in Flutter's GL
  // context, which only populate sees.
  std::unique_ptr<BufferSet> pending;
  std::unique_ptr<Imported> imported;
  // The frame named last, -1 for none, and its fence, -1 for none:
  // signalled when its drawing is done.
  uint32_t shown_generation = 0;
  int32_t shown_index = -1;
  int shown_fence = -1;
  // The buffer bound through the texture last, -1 for none: given back once
  // another is bound.
  uint32_t bound_generation = 0;
  int32_t bound_index = -1;
  // Disposed: nothing is imported or bound and no callback runs. Under the
  // mutex.
  bool disposed = false;
  EGLDisplay display = EGL_NO_DISPLAY;
  // Native fences on Flutter's display: -1 not looked up yet, else 0 or 1.
  int fences = -1;
  PFNEGLCREATESYNCKHRPROC create_sync = nullptr;
  PFNEGLDESTROYSYNCKHRPROC destroy_sync = nullptr;
  PFNEGLWAITSYNCKHRPROC wait_sync = nullptr;
  PFNEGLCLIENTWAITSYNCKHRPROC client_wait_sync = nullptr;
  PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fence = nullptr;
};

// Gives buffer |index| of |generation| back to the caller, with |fence_fd|
// (owned from here, -1 for none). Mutex held.
void give_back(State* state, uint32_t generation, int32_t index, int fence_fd) {
  if (state->released == nullptr || state->disposed) {
    if (fence_fd >= 0) close(fence_fd);
    return;
  }
  state->released(state->user_data, generation, index, fence_fd);
}

// ---- Fences --------------------------------------------------------------

// Whether Flutter's display has native fences, looked up once it is known.
// Raster thread.
bool fences_available(State* state) {
  if (state->fences >= 0) return state->fences == 1;
  if (state->display == EGL_NO_DISPLAY) return false;
  const char* extensions = eglQueryString(state->display, EGL_EXTENSIONS);
  const auto has = [extensions](const char* name) {
    return extensions != nullptr && std::strstr(extensions, name) != nullptr;
  };
  state->create_sync =
      reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(eglGetProcAddress("eglCreateSyncKHR"));
  state->destroy_sync =
      reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(eglGetProcAddress("eglDestroySyncKHR"));
  state->client_wait_sync = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
      eglGetProcAddress("eglClientWaitSyncKHR"));
  state->dup_fence = reinterpret_cast<PFNEGLDUPNATIVEFENCEFDANDROIDPROC>(
      eglGetProcAddress("eglDupNativeFenceFDANDROID"));
  state->wait_sync = has("EGL_KHR_wait_sync")
                         ? reinterpret_cast<PFNEGLWAITSYNCKHRPROC>(
                               eglGetProcAddress("eglWaitSyncKHR"))
                         : nullptr;
  state->fences = has("EGL_KHR_fence_sync") && has("EGL_ANDROID_native_fence_sync") &&
                          state->create_sync != nullptr && state->destroy_sync != nullptr &&
                          state->client_wait_sync != nullptr && state->dup_fence != nullptr
                      ? 1
                      : 0;
  return state->fences == 1;
}

// Makes Flutter's context wait for |fd| (owned from here) before what it
// draws next. Raster thread, context current.
void wait_fence(State* state, int fd) {
  if (!fences_available(state)) {
    close(fd);
    return;
  }
  const EGLint attributes[] = {EGL_SYNC_NATIVE_FENCE_FD_ANDROID, fd, EGL_NONE};
  // The sync owns the descriptor from here.
  const EGLSyncKHR sync =
      state->create_sync(state->display, EGL_SYNC_NATIVE_FENCE_ANDROID, attributes);
  if (sync == EGL_NO_SYNC_KHR) {
    close(fd);
    return;
  }
  if (state->wait_sync != nullptr) {
    state->wait_sync(state->display, sync, 0);
  } else {
    // On the CPU, so bounded: a GPU that hangs on the caller's drawing
    // must not hold the raster thread for good. Past it the frame shows
    // as is.
    constexpr EGLTimeKHR kFenceWaitNs = 100 * 1000 * 1000;
    state->client_wait_sync(state->display, sync, 0, kFenceWaitNs);
  }
  state->destroy_sync(state->display, sync);
}

// A fence for what Flutter's context has drawn so far, -1 without fences.
// Raster thread, context current.
int export_fence(State* state) {
  if (!fences_available(state)) return -1;
  const EGLSyncKHR sync =
      state->create_sync(state->display, EGL_SYNC_NATIVE_FENCE_ANDROID, nullptr);
  if (sync == EGL_NO_SYNC_KHR) return -1;
  // The fence is only submitted with the commands before it.
  glFlush();
  const int fd = state->dup_fence(state->display, sync);
  state->destroy_sync(state->display, sync);
  return fd;
}

}  // namespace

// ---- The texture ---------------------------------------------------------

struct SftTexture {
  FlTextureGL parent_instance;
  State* state;
};

typedef struct {
  FlTextureGLClass parent_class;
} SftTextureClass;

G_DEFINE_TYPE(SftTexture, sft_texture, fl_texture_gl_get_type())

#define SFT_TEXTURE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), sft_texture_get_type(), SftTexture))

// On the raster thread, Flutter's context current. Returns whether a set is
// imported afterwards.
static gboolean import_pending(State* state, GError** error) {
  std::unique_ptr<BufferSet> set;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->disposed) return FALSE;
    // A set waits until a frame of it is named, the first one too: until
    // then the frame shown is of the set at hand, if any, and the new
    // set's buffers may be being drawn or never drawn.
    if (state->pending && state->shown_index >= 0 &&
        state->pending->generation == state->shown_generation) {
      set = std::move(state->pending);
    }
  }
  if (!set) return state->imported != nullptr;
  const auto create_image =
      reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
  const auto target_texture = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
      eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  state->display = eglGetCurrentDisplay();
  if (create_image == nullptr || target_texture == nullptr ||
      state->display == EGL_NO_DISPLAY) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                "no EGL image import in this GL context");
    return state->imported != nullptr;
  }
  auto imported = std::make_unique<Imported>();
  imported->display = state->display;
  imported->context = eglGetCurrentContext();
  imported->owner = state->owner;
  imported->generation = set->generation;
  imported->width = set->width;
  imported->height = set->height;
  for (size_t i = 0; i < set->fds.size(); ++i) {
    std::vector<EGLint> attributes = {
        EGL_WIDTH, static_cast<EGLint>(set->width),
        EGL_HEIGHT, static_cast<EGLint>(set->height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(set->fourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT, set->fds[i],
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, static_cast<EGLint>(set->offsets[i]),
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(set->strides[i]),
    };
    if (set->modifier != SFT_MODIFIER_NONE) {
      attributes.insert(attributes.end(), {
          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
          static_cast<EGLint>(set->modifier & 0xffffffffULL),
          EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
          static_cast<EGLint>(set->modifier >> 32),
      });
    }
    attributes.push_back(EGL_NONE);
    const EGLImageKHR image = create_image(state->display, EGL_NO_CONTEXT,
                                           EGL_LINUX_DMA_BUF_EXT, nullptr, attributes.data());
    GLuint texture = 0;
    if (image != EGL_NO_IMAGE_KHR) {
      glGenTextures(1, &texture);
      glBindTexture(GL_TEXTURE_2D, texture);
      target_texture(GL_TEXTURE_2D, image);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    imported->images.push_back(image);
    imported->textures.push_back(texture);
  }
  // The images keep the buffers: the set's descriptors close with it.
  set.reset();
  if (state->imported) destroy_set(*state->imported);
  state->imported = std::move(imported);
  return TRUE;
}

static gboolean sft_texture_populate(FlTextureGL* texture,
                                     uint32_t* target,
                                     uint32_t* name,
                                     uint32_t* width,
                                     uint32_t* height,
                                     GError** error) {
  State* state = SFT_TEXTURE(texture)->state;
  empty_graveyard();
  if (!import_pending(state, error)) {
    // Flutter logs the error's message, and a missing one crashes it.
    if (error != nullptr && *error == nullptr) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_PENDING, "no frame of a set named yet");
    }
    return FALSE;
  }
  const Imported& imported = *state->imported;
  if (imported.textures.empty() || imported.textures[0] == 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "no GPU buffer imported");
    return FALSE;
  }
  // The frame named is not of this set: the buffer bound last stays, and
  // with nothing of this set bound, nothing shows: any other buffer of it
  // may be the one being drawn.
  int32_t index = -1;
  int fence_fd = -1;
  bool rebound = false;
  uint32_t before_generation = 0;
  int32_t before_index = -1;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    const uint32_t generation = state->shown_generation;
    const int32_t shown = state->shown_index;
    if (state->bound_generation == imported.generation && state->bound_index >= 0 &&
        static_cast<size_t>(state->bound_index) < imported.textures.size() &&
        imported.textures[state->bound_index] != 0) {
      index = state->bound_index;
    }
    if (!state->disposed && generation == imported.generation && shown >= 0 &&
        static_cast<size_t>(shown) < imported.textures.size() &&
        imported.textures[shown] != 0) {
      index = shown;
      fence_fd = state->shown_fence;
      state->shown_fence = -1;
      rebound = state->bound_index != shown || state->bound_generation != generation;
      before_generation = state->bound_generation;
      before_index = state->bound_index;
      state->bound_generation = generation;
      state->bound_index = shown;
    }
  }
  if (index < 0) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_PENDING, "no frame of this set named yet");
    return FALSE;
  }
  // The caller's drawing may still be running: sample after it.
  if (fence_fd >= 0) wait_fence(state, fence_fd);
  // The buffer bound before was last read by the frame drawn before this
  // one; once that reading is done, it is the caller's again.
  if (rebound && before_index >= 0) {
    const int release_fence = export_fence(state);
    std::lock_guard<std::mutex> lock(state->mutex);
    give_back(state, before_generation, before_index, release_fence);
  }
  *target = GL_TEXTURE_2D;
  *name = imported.textures[index];
  *width = imported.width;
  *height = imported.height;
  return TRUE;
}

static void sft_texture_gobject_dispose(GObject* object) {
  SftTexture* self = SFT_TEXTURE(object);
  // GObject may dispose twice.
  if (self->state != nullptr) {
    State* state = self->state;
    self->state = nullptr;
    if (state->registrar != nullptr) {
      g_object_remove_weak_pointer(G_OBJECT(state->registrar),
                                   reinterpret_cast<gpointer*>(&state->registrar));
    }
    if (state->shown_fence >= 0) close(state->shown_fence);
    bury(std::move(state->imported));
    delete state;
  }
  G_OBJECT_CLASS(sft_texture_parent_class)->dispose(object);
}

static void sft_texture_class_init(SftTextureClass* klass) {
  FL_TEXTURE_GL_CLASS(klass)->populate = sft_texture_populate;
  G_OBJECT_CLASS(klass)->dispose = sft_texture_gobject_dispose;
}

static void sft_texture_init(SftTexture* self) {
  self->state = new State();
}

// ---- The C API -----------------------------------------------------------

SftTexture* sft_texture_create(FlTextureRegistrar* registrar,
                               SftBufferReleased released,
                               gpointer user_data) {
  if (registrar == nullptr) return nullptr;
  SftTexture* texture = SFT_TEXTURE(g_object_new(sft_texture_get_type(), nullptr));
  if (!fl_texture_registrar_register_texture(registrar, FL_TEXTURE(texture))) {
    g_object_unref(texture);
    return nullptr;
  }
  State* state = texture->state;
  state->owner = owner_of(registrar);
  state->released = released;
  state->user_data = user_data;
  state->registrar = registrar;
  g_object_add_weak_pointer(G_OBJECT(registrar),
                            reinterpret_cast<gpointer*>(&state->registrar));
  return texture;
}

int64_t sft_texture_id(SftTexture* texture) {
  return texture == nullptr ? -1 : fl_texture_get_id(FL_TEXTURE(texture));
}

// Tells Flutter to populate the texture again.
static void mark_frame_available(SftTexture* texture) {
  FlTextureRegistrar* registrar = texture->state->registrar;
  if (registrar == nullptr) return;
  fl_texture_registrar_mark_texture_frame_available(registrar, FL_TEXTURE(texture));
}

bool sft_texture_set_dmabufs(SftTexture* texture,
                             uint32_t generation,
                             uint32_t width,
                             uint32_t height,
                             uint32_t fourcc,
                             uint64_t modifier,
                             const SftDmabuf* buffers,
                             size_t count) {
  if (texture == nullptr || buffers == nullptr || count == 0 || count > kMaxBuffers) {
    return false;
  }
  auto set = std::make_unique<BufferSet>();
  set->generation = generation;
  set->width = width;
  set->height = height;
  set->fourcc = fourcc;
  set->modifier = modifier;
  for (size_t i = 0; i < count; ++i) {
    const int fd = fcntl(buffers[i].fd, F_DUPFD_CLOEXEC, 0);
    if (fd < 0) return false;
    set->fds.push_back(fd);
    set->strides.push_back(buffers[i].stride);
    set->offsets.push_back(buffers[i].offset);
  }
  State* state = texture->state;
  bool named;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->disposed) return false;
    named = state->shown_index >= 0 && set->generation == state->shown_generation;
    state->pending = std::move(set);
  }
  // The frame was named before its set came: Flutter asked for it then and
  // keeps what it got, so it must ask again, or a still picture stays as
  // it was.
  if (named) mark_frame_available(texture);
  return true;
}

void sft_texture_present(SftTexture* texture,
                         uint32_t generation,
                         int32_t index,
                         int fence_fd) {
  if (texture == nullptr || index < 0) {
    if (fence_fd >= 0) close(fence_fd);
    return;
  }
  State* state = texture->state;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    if (state->disposed) {
      if (fence_fd >= 0) close(fence_fd);
      return;
    }
    // The fence of the frame named before: its buffer is bound by now, or
    // never will be.
    if (state->shown_fence >= 0) close(state->shown_fence);
    state->shown_fence = fence_fd;
    // A frame named but never bound (the raster thread did not get to it)
    // was never read: the caller's again at once. Without this a texture
    // nobody draws (off screen, a hidden tab) would keep every buffer it
    // is given, and whoever fills the set would run out.
    const bool superseded =
        state->shown_index >= 0 &&
        (state->shown_index != index || state->shown_generation != generation);
    const bool unbound = state->shown_index != state->bound_index ||
                         state->shown_generation != state->bound_generation;
    if (superseded && unbound) {
      give_back(state, state->shown_generation, state->shown_index, -1);
    }
    state->shown_generation = generation;
    state->shown_index = index;
  }
  mark_frame_available(texture);
}

void sft_texture_dispose(SftTexture* texture) {
  if (texture == nullptr) return;
  State* state = texture->state;
  FlTextureRegistrar* registrar;
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->disposed = true;
    state->pending.reset();
    if (state->shown_fence >= 0) close(state->shown_fence);
    state->shown_fence = -1;
    registrar = state->registrar;
    if (registrar != nullptr) {
      g_object_remove_weak_pointer(G_OBJECT(registrar),
                                   reinterpret_cast<gpointer*>(&state->registrar));
      state->registrar = nullptr;
    }
  }
  if (registrar != nullptr) {
    fl_texture_registrar_unregister_texture(registrar, FL_TEXTURE(texture));
  }
  // The texture itself goes a second later: the registrar hands it to the
  // raster thread without a reference, and a populate may be under way.
  g_timeout_add_seconds(
      1,
      [](gpointer data) -> gboolean {
        g_object_unref(data);
        return G_SOURCE_REMOVE;
      },
      texture);
}

void sofluffy_texture_plugin_register_with_registrar(FlPluginRegistrar* registrar) {}
