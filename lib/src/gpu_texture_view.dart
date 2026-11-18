import 'package:flutter/widgets.dart';

/// Shows a texture made with `sft_texture_create`, at the buffers' own
/// aspect ratio.
///
/// A [Texture] alone stretches to whatever box it is given. This sizes it
/// to the buffers ([width] by [height], in pixels) and fits that into the
/// space at hand.
class GpuTextureView extends StatelessWidget {
  const GpuTextureView({
    super.key,
    required this.textureId,
    required this.width,
    required this.height,
    this.fit = BoxFit.contain,
    this.alignment = Alignment.center,
    this.bottomUp = false,
    this.filterQuality = FilterQuality.low,
  });

  /// What `sft_texture_id` returned.
  final int textureId;

  /// The buffers' width in pixels.
  final int width;

  /// The buffers' height in pixels.
  final int height;

  /// How the picture fits into the space it gets.
  final BoxFit fit;

  /// Where the picture sits when it doesn't fill the space.
  final AlignmentGeometry alignment;

  /// Whether the buffers' first row is the picture's last, as when they
  /// were drawn through a GL framebuffer: the picture is then drawn the
  /// other way up.
  final bool bottomUp;

  /// How the texture is sampled when scaled.
  final FilterQuality filterQuality;

  @override
  Widget build(BuildContext context) {
    if (width <= 0 || height <= 0) {
      return const SizedBox.shrink();
    }

    Widget picture = Texture(
      textureId: textureId,
      filterQuality: filterQuality,
    );
    if (bottomUp) {
      picture = Transform.flip(flipY: true, child: picture);
    }

    return FittedBox(
      fit: fit,
      alignment: alignment,
      clipBehavior: Clip.hardEdge,
      child: SizedBox(
        width: width.toDouble(),
        height: height.toDouble(),
        child: picture,
      ),
    );
  }
}
