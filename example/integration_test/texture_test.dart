import 'dart:io';
import 'dart:typed_data';
import 'dart:ui' as ui;

import 'package:flutter/rendering.dart';
import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:integration_test/integration_test.dart';
import 'package:sofluffy_texture/sofluffy_texture.dart';
import 'package:texture_demo/texture_demo.dart';

const _width = 320;
const _height = 180;

/// Shows the texture [id], on the real GPU.
Future<void> _show(WidgetTester tester, GlobalKey boundary, int id) =>
    tester.pumpWidget(
      Directionality(
        textDirection: TextDirection.ltr,
        child: Center(
          child: RepaintBoundary(
            key: boundary,
            child: SizedBox(
              width: _width.toDouble(),
              height: _height.toDouble(),
              child: GpuTextureView(
                textureId: id,
                width: _width,
                height: _height,
              ),
            ),
          ),
        ),
      ),
    );

/// Draws [count] frames, each followed by a frame of Flutter's.
Future<int> _draw(WidgetTester tester, TextureDemo demo, int count) async {
  var drawn = 0;
  for (var frame = 0; frame < count; frame++) {
    if (await demo.draw(frame)) {
      drawn++;
    }

    await tester.pump(const Duration(milliseconds: 16));
    // The raster thread asks for the buffer after the frame is built.
    await tester.runAsync(
      () => Future<void>.delayed(const Duration(milliseconds: 20)),
    );
  }

  return drawn;
}

/// What is on screen inside [boundary], as RGBA.
Future<ByteData> _pixels(WidgetTester tester, GlobalKey boundary) async {
  final render =
      boundary.currentContext?.findRenderObject() as RenderRepaintBoundary;
  final data = await tester.runAsync(() async {
    final image = await render.toImage(pixelRatio: 1);
    return image.toByteData(format: ui.ImageByteFormat.rawRgba);
  });
  return data ?? ByteData(0);
}

/// How many pixels of the middle row are the bar's white.
int _whiteInMiddleRow(ByteData pixels) {
  final row = _height ~/ 2;
  var white = 0;
  for (var x = 0; x < _width; x++) {
    final at = (row * _width + x) * 4;
    final isWhite =
        pixels.getUint8(at) > 240 &&
        pixels.getUint8(at + 1) > 240 &&
        pixels.getUint8(at + 2) > 240;
    if (isWhite) {
      white++;
    }
  }

  return white;
}

void main() {
  final binding = IntegrationTestWidgetsFlutterBinding.ensureInitialized();
  binding.framePolicy = LiveTestWidgetsFlutterBindingFramePolicy.fullyLive;

  testWidgets('a ring of buffers shows, and each comes back', (tester) async {
    final demo = TextureDemo();
    final id = await demo.create(width: _width, height: _height);
    final boundary = GlobalKey();
    await _show(tester, boundary, id);

    final drawn = await _draw(tester, demo, 30);
    final stats = await demo.stats();
    final pixels = await _pixels(tester, boundary);

    expect(drawn, 30, reason: 'a ring of three never runs out');
    expect(stats.presented, 30);
    // All but the buffer on screen, and the one named after it at most.
    expect(stats.released, greaterThanOrEqualTo(28));
    expect(pixels.lengthInBytes, _width * _height * 4);
    expect(_whiteInMiddleRow(pixels), inInclusiveRange(40, 56));

    await demo.dispose();
  });

  testWidgets('a texture nobody draws gives every buffer back', (tester) async {
    final demo = TextureDemo();
    await demo.create(width: _width, height: _height);
    await tester.pumpWidget(const SizedBox.shrink());

    final drawn = await _draw(tester, demo, 20);
    final stats = await demo.stats();

    expect(drawn, 20, reason: 'a texture off screen must not hold the ring');
    expect(stats.released, 19);

    await demo.dispose();
  });

  testWidgets('one pixel buffer per frame shows', (tester) async {
    final demo = TextureDemo();
    final id = await demo.create(
      width: _width,
      height: _height,
      mode: DemoMode.pixelBuffer,
    );
    final boundary = GlobalKey();
    await _show(tester, boundary, id);

    final drawn = await _draw(tester, demo, 10);
    final pixels = await _pixels(tester, boundary);

    expect(drawn, 10);
    expect(pixels.lengthInBytes, _width * _height * 4);
    expect(_whiteInMiddleRow(pixels), inInclusiveRange(40, 56));

    await demo.dispose();
  }, skip: !Platform.isMacOS);
}
