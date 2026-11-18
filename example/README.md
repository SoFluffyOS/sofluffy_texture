# sofluffy_texture example

A window showing a picture that native code draws, 60 times a second, with no copy on the way to Flutter.

`packages/texture_demo` is the part to read. It is a small plugin that depends on sofluffy_texture the way yours would: it owns three GPU buffers, draws into a free one, presents it, and gets it back once Flutter is done.

```bash
flutter run -d macos   # or windows, linux
```

| Platform | Buffers | File |
|---|---|---|
| macOS | IOSurfaces, or one CVPixelBuffer per frame | `packages/texture_demo/macos/texture_demo/Sources/texture_demo/TextureDemoPlugin.swift` |
| Windows | Shared D3D11 textures | `packages/texture_demo/windows/texture_demo_plugin.cpp` |
| Linux | dmabufs from GBM | `packages/texture_demo/linux/texture_demo_plugin.cc` |
