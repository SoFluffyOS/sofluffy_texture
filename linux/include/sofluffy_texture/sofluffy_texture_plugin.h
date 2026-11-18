// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_

#include <flutter_linux/flutter_linux.h>

G_BEGIN_DECLS

#ifdef SOFLUFFY_TEXTURE_IMPL
#define SOFLUFFY_TEXTURE_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define SOFLUFFY_TEXTURE_PLUGIN_EXPORT
#endif

// Called by Flutter so the library is linked into the app. It has no
// channel: the API is in sofluffy_texture.h.
SOFLUFFY_TEXTURE_PLUGIN_EXPORT void sofluffy_texture_plugin_register_with_registrar(
    FlPluginRegistrar* registrar);

G_END_DECLS

#endif  // FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_
