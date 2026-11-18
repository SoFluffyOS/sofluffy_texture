// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Hands GPU buffers that native code already holds to Flutter as a texture.
// Windows: shared D3D11 textures, by their DXGI shared handles, which
// Flutter opens on its own device.
//
// Describe a set of textures once (sft_texture_set_d3d11_textures or
// sft_texture_set_shared_handles), then name the one to show for each frame
// (sft_texture_present). The texture tells you when Flutter is done with a
// buffer (SftBufferReleased), so you can draw into it again.
//
// The textures must be BGRA (DXGI_FORMAT_B8G8R8A8_UNORM) and created with
// D3D11_RESOURCE_MISC_SHARED: Flutter cannot open an NT handle
// (D3D11_RESOURCE_MISC_SHARED_NTHANDLE).
//
// Threads: create and dispose on the platform thread. Everything else may
// be called from any thread. SftBufferReleased runs on Flutter's raster
// thread or on the thread that called present, with the texture's lock
// held: it must not block and must not call back into the same texture.
#ifndef SOFLUFFY_TEXTURE_H_
#define SOFLUFFY_TEXTURE_H_

#include <windows.h>

#include <d3d11.h>
#include <flutter_texture_registrar.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SOFLUFFY_TEXTURE_IMPL
#define SFT_EXPORT __declspec(dllexport)
#else
#define SFT_EXPORT __declspec(dllimport)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// A Flutter texture fed with native GPU buffers.
typedef struct SftTexture SftTexture;

// Flutter is done with buffer |index| of set |generation|: it is yours to
// draw into again. Called once for every buffer presented, when the next
// one is on screen, or at once when a buffer presented was replaced before
// Flutter drew it.
typedef void (*SftBufferReleased)(void* user_data,
                                  uint32_t generation,
                                  int32_t index);

// Registers a texture with |registrar|: from a plugin's registrar,
// FlutterDesktopRegistrarGetTextureRegistrar(registrar). |released| may be
// NULL. Returns NULL when Flutter refused the texture.
SFT_EXPORT SftTexture* sft_texture_create(
    FlutterDesktopTextureRegistrarRef registrar,
    SftBufferReleased released,
    void* user_data);

// The id for a `Texture` widget.
SFT_EXPORT int64_t sft_texture_id(const SftTexture* texture);

// Describes set |generation|: |count| textures of one size, in index order.
// The texture holds a reference to each until another set has replaced
// this one or the texture is disposed. A set replaces the one before it
// once a frame of it is presented; give each a new generation. Returns
// false when a texture has no shared handle.
SFT_EXPORT bool sft_texture_set_d3d11_textures(SftTexture* texture,
                                               uint32_t generation,
                                               ID3D11Texture2D* const* textures,
                                               size_t count);

// As sft_texture_set_d3d11_textures, for textures known by their shared
// handles (IDXGIResource::GetSharedHandle), such as another process's. The
// handles must stay valid until another set has replaced this one or the
// texture is disposed.
SFT_EXPORT bool sft_texture_set_shared_handles(SftTexture* texture,
                                               uint32_t generation,
                                               uint32_t width,
                                               uint32_t height,
                                               const HANDLE* handles,
                                               size_t count);

// Shows buffer |index| of set |generation| as the next frame. Call it once
// the buffer's drawing is done (flushed on its device): nothing waits for
// the GPU here.
SFT_EXPORT void sft_texture_present(SftTexture* texture,
                                    uint32_t generation,
                                    int32_t index);

// Unregisters the texture. No callback runs once this returns; every buffer
// is yours again. |texture| is gone.
SFT_EXPORT void sft_texture_dispose(SftTexture* texture);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // SOFLUFFY_TEXTURE_H_
