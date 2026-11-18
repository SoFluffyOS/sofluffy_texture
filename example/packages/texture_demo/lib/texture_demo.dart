import 'package:flutter/services.dart';

/// How the native side hands its buffers over.
enum DemoMode {
  /// A set of three buffers, drawn into in turn and given back by Flutter.
  ring,

  /// A new pixel buffer for every frame, as a video decoder gives them.
  /// macOS only.
  pixelBuffer,
}

/// How many frames went to Flutter and how many buffers came back.
class DemoStats {
  const DemoStats({required this.presented, required this.released});

  final int presented;
  final int released;
}

/// The native renderer: draws a moving bar into GPU buffers and presents
/// them through sofluffy_texture.
class TextureDemo {
  static const _channel = MethodChannel('texture_demo');

  /// Makes the buffers and the texture; returns the texture's id.
  Future<int> create({
    required int width,
    required int height,
    DemoMode mode = DemoMode.ring,
  }) async {
    final id = await _channel.invokeMethod<int>('create', {
      'width': width,
      'height': height,
      'mode': mode.name,
    });
    return id ?? -1;
  }

  /// Draws frame [frame] and presents it. False when every buffer is still
  /// with Flutter, and the frame was skipped.
  Future<bool> draw(int frame) async =>
      await _channel.invokeMethod<bool>('draw', {'frame': frame}) ?? false;

  Future<DemoStats> stats() async {
    final stats = await _channel.invokeMapMethod<String, int>('stats');
    return DemoStats(
      presented: stats?['presented'] ?? 0,
      released: stats?['released'] ?? 0,
    );
  }

  Future<void> dispose() => _channel.invokeMethod<void>('dispose');
}
