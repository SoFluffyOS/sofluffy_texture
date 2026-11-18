// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Hands GPU buffers that native code already holds to Flutter as a texture,
// without a copy. Linux: dmabufs, imported as EGL images into Flutter's own
// GL context, with native fences where EGL has them.
//
// Describe a set of buffers once (sft_texture_set_dmabufs), then name the
// one to show for each frame (sft_texture_present), with a fence for its
// drawing if you have one. The texture tells you when Flutter is done with
// a buffer (SftBufferReleased), with a fence for the drawing that sampled
// it last, so you can draw into it again.
//
// The buffers have one plane and a format GL samples as RGB (such as
// DRM_FORMAT_ARGB8888 or DRM_FORMAT_XRGB8888). Flutter needs EGL with
// EGL_EXT_image_dma_buf_import.
//
// Threads: create and dispose on the platform (GTK main) thread. Everything
// else may be called from any thread. SftBufferReleased runs on Flutter's
// raster thread or on the thread that called present, with the texture's
// lock held: it must not block and must not call back into the same
// texture.
#ifndef SOFLUFFY_TEXTURE_H_
#define SOFLUFFY_TEXTURE_H_

#include <flutter_linux/flutter_linux.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef SOFLUFFY_TEXTURE_IMPL
#define SFT_EXPORT __attribute__((visibility("default")))
#else
#define SFT_EXPORT
#endif

G_BEGIN_DECLS

// A Flutter texture fed with native GPU buffers.
typedef struct SftTexture SftTexture;

// DRM_FORMAT_MOD_INVALID: the buffers have no explicit modifier.
#define SFT_MODIFIER_NONE 0x00ffffffffffffffULL

// One buffer of a set: its dmabuf and where its only plane lies in it.
typedef struct {
  // Borrowed: the texture duplicates it.
  int fd;
  uint32_t stride;
  uint32_t offset;
} SftDmabuf;

// Flutter is done with buffer |index| of set |generation|: it is yours to
// draw into again once |fence_fd| has signalled. |fence_fd| is a sync file
// for the drawing that sampled the buffer last, yours to close, or -1 when
// there is none (EGL has no native fences, or Flutter never drew the
// buffer). Called once for every buffer presented, when the next one is on
// screen, or at once when a buffer presented was replaced before Flutter
// drew it.
typedef void (*SftBufferReleased)(gpointer user_data,
                                  uint32_t generation,
                                  int32_t index,
                                  int fence_fd);

// Registers a texture with |registrar|: from a plugin's registrar,
// fl_plugin_registrar_get_texture_registrar(registrar). |released| may be
// NULL. Returns NULL when Flutter refused the texture.
SFT_EXPORT SftTexture* sft_texture_create(FlTextureRegistrar* registrar,
                                          SftBufferReleased released,
                                          gpointer user_data);

// The id for a `Texture` widget.
SFT_EXPORT int64_t sft_texture_id(SftTexture* texture);

// Describes set |generation|: |count| buffers of one size and format, in
// index order. |fourcc| is a DRM format, |modifier| its modifier or
// SFT_MODIFIER_NONE. The texture keeps each buffer until another set has
// replaced this one or the texture is disposed. A set replaces the one
// before it once a frame of it is presented; give each a new generation.
SFT_EXPORT bool sft_texture_set_dmabufs(SftTexture* texture,
                                        uint32_t generation,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t fourcc,
                                        uint64_t modifier,
                                        const SftDmabuf* buffers,
                                        size_t count);

// Shows buffer |index| of set |generation| as the next frame. |fence_fd| is
// a sync file that signals when the buffer's drawing is done, which
// Flutter waits for before sampling; the texture closes it. Pass -1 when
// the drawing is already done.
SFT_EXPORT void sft_texture_present(SftTexture* texture,
                                    uint32_t generation,
                                    int32_t index,
                                    int fence_fd);

// Unregisters the texture and lets go of every buffer. No callback runs
// once this returns; every buffer is yours again. |texture| is gone.
SFT_EXPORT void sft_texture_dispose(SftTexture* texture);

G_END_DECLS

#endif  // SOFLUFFY_TEXTURE_H_
