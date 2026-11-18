# sofluffy_texture

Hi folks 👋

Your native code already has the picture on the GPU: a decoded video frame, a renderer's output, a captured screen. Getting it into a Flutter `Texture` means writing the same glue for every platform, and getting the hard parts wrong at least once: who owns a buffer and when, which thread calls what, how to not sample a frame that is still being drawn.

sofluffy_texture is that glue, written once. Your plugin's native code calls a small C API, in the same process, and Flutter shows the buffer. **No copy**, no channel in between.

> **TL;DR**
>
> 1. Add `sofluffy_texture` to your plugin's `pubspec.yaml`.
> 2. Link it from your plugin's native build (one line per platform).
> 3. `sft_texture_create`, describe your buffers, then `sft_texture_present` for each frame.
> 4. Show it with `GpuTextureView(textureId: …)`.

| Platform | What you hand over | How Flutter gets it |
|---|---|---|
| 🍎 macOS | `IOSurfaceRef`, `CVPixelBufferRef` | A `FlutterTexture` backed by the same surface |
| 🪟 Windows | `ID3D11Texture2D`, or its shared `HANDLE` | A DXGI shared handle, opened on Flutter's device |
| 🐧 Linux | dmabuf fds, with an optional fence fd | An `EGLImage` in Flutter's own GL context |

## 📦 Add it to your plugin

```yaml
# your_plugin/pubspec.yaml
dependencies:
  sofluffy_texture:
    git: https://github.com/SoFluffyOS/sofluffy_texture.git
```

Then tell your native build about it.

### 🍎 macOS

Swift Package Manager, in your plugin's `Package.swift`. Flutter puts every plugin's package side by side, so the path is the plugin's name:

```swift
dependencies: [
    .package(name: "FlutterFramework", path: "../FlutterFramework"),
    .package(name: "sofluffy_texture", path: "../sofluffy_texture"),
],
targets: [
    .target(
        name: "your_plugin",
        dependencies: [
            .product(name: "FlutterFramework", package: "FlutterFramework"),
            .product(name: "sofluffy-texture", package: "sofluffy_texture"),
        ]
    )
]
```

CocoaPods, in your podspec:

```ruby
s.dependency 'sofluffy_texture'
```

Then `import sofluffy_texture` in Swift, or `#import <sofluffy_texture/sofluffy_texture.h>` in Objective-C++.

One Swift detail: a C array of surfaces is `[Unmanaged<IOSurfaceRef>]` there, so pass `surfaces.map { Unmanaged.passUnretained($0) }` to `sft_texture_set_surfaces`.

### 🪟 Windows and 🐧 Linux

One line in your plugin's `CMakeLists.txt`. The headers come with the target:

```cmake
target_link_libraries(${PLUGIN_NAME} PRIVATE sofluffy_texture_plugin)
```

```cpp
#include <sofluffy_texture/sofluffy_texture.h>
```

That's it.

## 🚀 Show a set of buffers

This is the path for a renderer: you own a few buffers, draw into a free one, show it, and draw into it again once Flutter is done with it.

The calls are the same on every platform. Only the buffers differ.

```cpp
// 1. One texture. `registry` comes from your plugin's registrar.
SftTexture* texture = sft_texture_create(registry, OnReleased, this);
int64_t id = sft_texture_id(texture);  // for the Texture widget

// 2. Describe your buffers once. A set has a generation number.
sft_texture_set_surfaces(texture, /*generation*/ 1, surfaces, 3);        // macOS
sft_texture_set_d3d11_textures(texture, 1, textures, 3);                 // Windows
sft_texture_set_dmabufs(texture, 1, width, height, fourcc, modifier,
                        dmabufs, 3);                                     // Linux

// 3. For each frame: draw into a free buffer, then name it.
sft_texture_present(texture, 1, index);            // macOS, Windows
sft_texture_present(texture, 1, index, fence_fd);  // Linux, -1 for no fence

// 4. When you are done.
sft_texture_dispose(texture);
```

And Flutter tells you when a buffer is yours again:

```cpp
void OnReleased(void* user_data, uint32_t generation, int32_t index) {
  // Buffer `index` is free. Draw into it again.
}
```

Where the registry comes from:

| Platform | Pass this |
|---|---|
| macOS | `registrar.textures` |
| Windows | `FlutterDesktopRegistrarGetTextureRegistrar(registrar)` |
| Linux | `fl_plugin_registrar_get_texture_registrar(registrar)` |

Resized? Make new buffers, describe them with the next generation number, and present from it. The old picture stays on screen until the first frame of the new set arrives.

## 🎬 Show one pixel buffer per frame (macOS)

A VideoToolbox decoder hands you a fresh `CVPixelBuffer` for every frame. There is no set to describe. Just hand each one over:

```swift
import sofluffy_texture

let texture = sft_texture_create(registrar.textures, nil, nil)

// In your decoder's output callback, on any thread:
sft_texture_present_pixel_buffer(texture, pixelBuffer)
```

The texture retains the buffer until the next one is on screen, then lets it go, so the decoder's pool gets it back by itself. Easy, right?

Ask VideoToolbox for buffers Metal can draw: `kCVPixelFormatType_32BGRA`, `kCVPixelBufferMetalCompatibilityKey` and `kCVPixelBufferIOSurfacePropertiesKey`.

## 💎 The Dart side

It is thin on purpose. Your plugin sends the texture id to Dart however it likes, and shows it:

```dart
import 'package:sofluffy_texture/sofluffy_texture.dart';

GpuTextureView(textureId: id, width: 1920, height: 1080)
```

`GpuTextureView` keeps the buffers' aspect ratio, where a bare `Texture` stretches. Pass `bottomUp: true` if your buffers were drawn through a GL framebuffer and come out upside down.

## 🔑 Rules worth knowing

- **A buffer is with Flutter from `present` until `SftBufferReleased`.** Don't draw into it in between.
- **Use at least 3 buffers in a set.** The texture holds 2 at most: the one on screen and the one named next. With 3 you ALWAYS have one to draw into.
- **A texture nobody draws holds nothing back.** Off screen or in a hidden tab, each `present` gives the previous buffer back at once. Your renderer never stalls on a view the user can't see.
- **Present only what is drawn.** On macOS and Windows nothing waits for your GPU, so finish (or flush) the drawing first. On Linux, pass a fence fd and Flutter waits for it before sampling.
- **On Linux you get a fence back too.** `SftBufferReleased` has a fourth argument, `fence_fd`: wait for it before you draw into the buffer again, and close it. It is `-1` when there is none.
- **The release callback runs on Flutter's raster thread** (or inside your own `present` call). Keep it short, and don't call back into the same texture from it.
- **Create and dispose on the platform thread.** Everything else works from any thread.
- **Dispose before the engine goes.** After `sft_texture_dispose` returns, no callback runs and every buffer is yours.

Each platform's header has the details:

- [`macos/…/sofluffy_texture.h`](macos/sofluffy_texture/Sources/sofluffy_texture/include/sofluffy_texture/sofluffy_texture.h)
- [`windows/…/sofluffy_texture.h`](windows/include/sofluffy_texture/sofluffy_texture.h)
- [`linux/…/sofluffy_texture.h`](linux/include/sofluffy_texture/sofluffy_texture.h)

## 🧱 Limits

- **Windows**: BGRA textures created with `D3D11_RESOURCE_MISC_SHARED`. Flutter can't open an NT handle (`D3D11_RESOURCE_MISC_SHARED_NTHANDLE`).
- **Linux**: one plane, in a format GL samples as RGB (`DRM_FORMAT_ARGB8888`, `DRM_FORMAT_XRGB8888`). No NV12. Flutter needs EGL with `EGL_EXT_image_dma_buf_import`. Build needs `libegl1-mesa-dev` and `libgles2-mesa-dev`.
- **macOS**: 10.15 or later, Flutter 3.44 or later.

## 🤖 Example

[`example/`](example) is a window with a bar moving across it, drawn by native code at 60 frames a second. [`example/packages/texture_demo`](example/packages/texture_demo) is a small plugin that uses sofluffy_texture the way yours would, on all three platforms.

```bash
cd example
flutter run -d macos   # or windows, linux
```

## Happy Coding 🦊
