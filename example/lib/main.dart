import 'dart:io';

import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';
import 'package:sofluffy_texture/sofluffy_texture.dart';
import 'package:texture_demo/texture_demo.dart';

const _width = 640;
const _height = 360;
const _ink = Color(0xFFE8E8EC);
const _paper = Color(0xFF16161A);

void main() => runApp(const DemoApp());

class DemoApp extends StatelessWidget {
  const DemoApp({super.key});

  @override
  Widget build(BuildContext context) => WidgetsApp(
    color: _paper,
    debugShowCheckedModeBanner: false,
    builder: (context, _) => const ColoredBox(
      color: _paper,
      child: DefaultTextStyle(
        style: TextStyle(color: _ink, fontSize: 14),
        child: DemoPage(),
      ),
    ),
  );
}

class DemoPage extends StatefulWidget {
  const DemoPage({super.key});

  @override
  State<DemoPage> createState() => _DemoPageState();
}

class _DemoPageState extends State<DemoPage>
    with SingleTickerProviderStateMixin {
  final _demo = TextureDemo();
  late final Ticker _ticker = createTicker(_onTick);

  DemoMode _mode = DemoMode.ring;
  int? _textureId;
  int _frame = 0;
  int _skipped = 0;
  bool _drawing = false;
  DemoStats _stats = const DemoStats(presented: 0, released: 0);
  String? _error;

  @override
  void initState() {
    super.initState();
    _start();
  }

  @override
  void dispose() {
    _ticker.dispose();
    _demo.dispose();
    super.dispose();
  }

  Future<void> _start() async {
    try {
      final id = await _demo.create(
        width: _width,
        height: _height,
        mode: _mode,
      );
      if (!mounted) {
        return;
      }

      setState(() {
        _textureId = id;
        _error = null;
      });
      _ticker.start();
    } on Exception catch (error) {
      setState(() => _error = '$error');
    }
  }

  Future<void> _switchMode() async {
    _ticker.stop();
    await _demo.dispose();
    setState(() {
      _textureId = null;
      _frame = 0;
      _skipped = 0;
      _mode = switch (_mode) {
        DemoMode.ring => DemoMode.pixelBuffer,
        DemoMode.pixelBuffer => DemoMode.ring,
      };
    });
    await _start();
  }

  Future<void> _onTick(Duration _) async {
    if (_drawing) {
      return;
    }

    _drawing = true;
    final drawn = await _demo.draw(_frame);
    final stats = _frame % 30 == 0 ? await _demo.stats() : _stats;
    _drawing = false;
    if (!mounted) {
      return;
    }

    setState(() {
      _frame++;
      _stats = stats;
      if (!drawn) {
        _skipped++;
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    final textureId = _textureId;
    return Padding(
      padding: const EdgeInsets.all(24),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Expanded(
            child: Center(
              child: switch (textureId) {
                null => Text(_error ?? 'Starting…'),
                final id => GpuTextureView(
                  textureId: id,
                  width: _width,
                  height: _height,
                ),
              },
            ),
          ),
          const SizedBox(height: 16),
          Text(
            'Mode: ${_mode.name}   '
            'presented ${_stats.presented}   '
            'given back ${_stats.released}   '
            'skipped $_skipped',
          ),
          if (Platform.isMacOS)
            Padding(
              padding: const EdgeInsets.only(top: 12),
              child: GestureDetector(
                onTap: _switchMode,
                child: const Text(
                  'Switch between a ring and one pixel buffer per frame',
                  style: TextStyle(decoration: TextDecoration.underline),
                ),
              ),
            ),
        ],
      ),
    );
  }
}
