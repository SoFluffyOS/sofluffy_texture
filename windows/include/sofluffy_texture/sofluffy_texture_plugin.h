// Copyright 2026 SoFluffy. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_
#define FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_

#include <flutter_plugin_registrar.h>

#ifdef SOFLUFFY_TEXTURE_IMPL
#define SOFLUFFY_TEXTURE_PLUGIN_EXPORT __declspec(dllexport)
#else
#define SOFLUFFY_TEXTURE_PLUGIN_EXPORT __declspec(dllimport)
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Called by Flutter so the library is linked into the app. It has no
// channel: the API is in sofluffy_texture.h.
SOFLUFFY_TEXTURE_PLUGIN_EXPORT void SofluffyTexturePluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_SOFLUFFY_TEXTURE_PLUGIN_H_
