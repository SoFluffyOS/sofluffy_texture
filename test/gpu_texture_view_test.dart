import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:sofluffy_texture/sofluffy_texture.dart';

Future<void> pump(WidgetTester tester, Widget view) => tester.pumpWidget(
  Directionality(
    textDirection: TextDirection.ltr,
    child: Center(child: SizedBox(width: 400, height: 400, child: view)),
  ),
);

void main() {
  testWidgets('shows the texture at the buffers\' aspect ratio', (
    tester,
  ) async {
    await pump(
      tester,
      const GpuTextureView(textureId: 7, width: 1920, height: 1080),
    );

    final texture = tester.widget<Texture>(find.byType(Texture));
    expect(texture.textureId, 7);
    final size = tester.getSize(find.byType(Texture));
    expect(size, const Size(1920, 1080));
    final box = tester.renderObject<RenderBox>(find.byType(Texture));
    final shown =
        box.localToGlobal(Offset(size.width, size.height)) -
        box.localToGlobal(Offset.zero);
    expect(shown.dx, moreOrLessEquals(400));
    expect(shown.dy, moreOrLessEquals(225));
  });

  testWidgets('a texture that starts at the top is drawn as it is', (
    tester,
  ) async {
    await pump(
      tester,
      const GpuTextureView(textureId: 7, width: 64, height: 64),
    );

    final box = tester.renderObject<RenderBox>(find.byType(Texture));
    final top = box.localToGlobal(Offset.zero).dy;
    final bottom = box.localToGlobal(const Offset(0, 64)).dy;
    expect(top, lessThan(bottom));
  });

  testWidgets('a bottom-up texture is drawn the other way up', (tester) async {
    await pump(
      tester,
      const GpuTextureView(textureId: 7, width: 64, height: 64, bottomUp: true),
    );

    final box = tester.renderObject<RenderBox>(find.byType(Texture));
    final top = box.localToGlobal(Offset.zero).dy;
    final bottom = box.localToGlobal(const Offset(0, 64)).dy;
    expect(top, greaterThan(bottom));
  });

  testWidgets('buffers without a size show nothing', (tester) async {
    await pump(
      tester,
      const GpuTextureView(textureId: 7, width: 0, height: 1080),
    );

    expect(find.byType(Texture), findsNothing);
  });
}
