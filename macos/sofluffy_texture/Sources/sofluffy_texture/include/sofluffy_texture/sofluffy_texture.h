// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Hands GPU buffers that native code already holds to Flutter as a texture,
// without a copy. macOS: IOSurfaces and CVPixelBuffers.
//
// A texture shows buffers in one of two ways:
//
// - A set: describe a set of buffers once (sft_texture_set_surfaces or
//   sft_texture_set_pixel_buffers), then name the buffer to show for each
//   frame (sft_texture_present). The texture tells you when Flutter is done
//   with a buffer (SftBufferReleased), so you can draw into it again.
// - One at a time: hand each frame's pixel buffer over
//   (sft_texture_present_pixel_buffer), as a VideoToolbox decoder gives
//   them. The texture retains it until the next one is on screen.
//
// Threads: create on the main thread. Everything else may be called from
// any thread. SftBufferReleased runs on Flutter's raster thread or on the
// thread that called present, with the texture's lock held: it must not
// block and must not call back into the same texture.
#ifndef SOFLUFFY_TEXTURE_H_
#define SOFLUFFY_TEXTURE_H_

#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __OBJC__
#import <FlutterMacOS/FlutterMacOS.h>
#endif

#define SFT_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

/// A Flutter texture fed with native GPU buffers.
typedef struct SftTexture SftTexture;

/// Flutter is done with buffer |index| of set |generation|: it is yours to
/// draw into again. Called once for every buffer presented, when the next
/// one is on screen, or at once when a buffer presented was replaced before
/// Flutter drew it.
typedef void (*SftBufferReleased)(void* _Nullable user_data,
                                  uint32_t generation,
                                  int32_t index);

#ifdef __OBJC__
/// Registers a texture with |registry| (a plugin's `registrar.textures`).
/// |released| may be NULL. Main thread.
SFT_EXPORT SftTexture* _Nullable sft_texture_create(
    id<FlutterTextureRegistry> _Nonnull registry,
    SftBufferReleased _Nullable released,
    void* _Nullable user_data);
#endif

/// The id for a `Texture` widget.
SFT_EXPORT int64_t sft_texture_id(const SftTexture* _Nonnull texture);

/// Describes set |generation|: |count| surfaces, in index order. The
/// texture keeps each surface until another set replaces this one or the
/// texture is disposed. A set replaces the one before it; give each a new
/// generation. Returns false when no surface could be wrapped.
SFT_EXPORT bool sft_texture_set_surfaces(SftTexture* _Nonnull texture,
                                         uint32_t generation,
                                         IOSurfaceRef _Nonnull const* _Nonnull surfaces,
                                         size_t count);

/// As sft_texture_set_surfaces, for buffers that are already pixel buffers
/// (IOSurface-backed and Metal compatible, such as a pixel buffer pool's).
SFT_EXPORT bool sft_texture_set_pixel_buffers(
    SftTexture* _Nonnull texture,
    uint32_t generation,
    CVPixelBufferRef _Nonnull const* _Nonnull buffers,
    size_t count);

/// Shows buffer |index| of set |generation| as the next frame. Call it once
/// the buffer's drawing is done: nothing waits for the GPU here.
SFT_EXPORT void sft_texture_present(SftTexture* _Nonnull texture,
                                    uint32_t generation,
                                    int32_t index);

/// Shows |buffer| as the next frame, outside any set. The texture retains
/// it until another frame is on screen, so a decoder's pool gets it back
/// by itself; SftBufferReleased is not called for it.
SFT_EXPORT void sft_texture_present_pixel_buffer(SftTexture* _Nonnull texture,
                                                 CVPixelBufferRef _Nonnull buffer);

/// Unregisters the texture and lets go of every buffer. No callback runs
/// once this returns; every buffer is yours again. |texture| is gone.
SFT_EXPORT void sft_texture_dispose(SftTexture* _Nullable texture);

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef __OBJC__
/// Registered by Flutter so the library is linked into the app. It has no
/// channel: the API is the C functions above.
@interface SofluffyTexturePlugin : NSObject <FlutterPlugin>
@end
#endif

#endif  // SOFLUFFY_TEXTURE_H_
