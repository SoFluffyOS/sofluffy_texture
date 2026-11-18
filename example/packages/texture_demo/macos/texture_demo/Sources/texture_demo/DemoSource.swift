import CoreVideo
import FlutterMacOS
import Foundation
import IOSurface
import sofluffy_texture

/// Draws a moving bar into GPU buffers and presents them through
/// sofluffy_texture: either a ring of three IOSurfaces that Flutter gives
/// back, or a new pixel buffer for every frame, as a video decoder makes
/// them.
final class DemoSource {
  private static let ringSize = 3
  private static let generation: UInt32 = 1

  private let width: Int
  private let height: Int
  private let usesRing: Bool
  private let texture: OpaquePointer
  private var surfaces: [IOSurfaceRef] = []
  private let lock = NSLock()
  private var free: [Bool]
  private var presented: Int64 = 0
  private var released: Int64 = 0

  var textureId: Int64 { sft_texture_id(texture) }

  var stats: [String: Int64] {
    lock.lock()
    defer { lock.unlock() }
    return ["presented": presented, "released": released]
  }

  init?(registry: FlutterTextureRegistry, width: Int, height: Int, usesRing: Bool) {
    self.width = width
    self.height = height
    self.usesRing = usesRing
    free = Array(repeating: true, count: Self.ringSize)
    let box = CallbackBox()
    let created = sft_texture_create(
      registry,
      { userData, _, index in
        guard let userData else {
          return
        }

        Unmanaged<CallbackBox>.fromOpaque(userData).takeUnretainedValue().source?.onReleased(
          Int(index))
      },
      Unmanaged.passUnretained(box).toOpaque())
    guard let created else {
      return nil
    }

    texture = created
    callbackBox = box
    box.source = self
    guard usesRing else {
      return
    }

    let properties: [CFString: Any] = [
      kIOSurfaceWidth: width,
      kIOSurfaceHeight: height,
      kIOSurfaceBytesPerElement: 4,
      kIOSurfacePixelFormat: kCVPixelFormatType_32BGRA,
    ]
    surfaces = (0..<Self.ringSize).compactMap { _ in IOSurfaceCreate(properties as CFDictionary) }
    // Swift sees a C array of surfaces as unmanaged references.
    let borrowed = surfaces.map { Unmanaged.passUnretained($0) }
    guard surfaces.count == Self.ringSize,
      sft_texture_set_surfaces(texture, Self.generation, borrowed, borrowed.count)
    else {
      sft_texture_dispose(texture)
      return nil
    }
  }

  private let callbackBox: CallbackBox

  /// Draws frame `frame` and presents it. False when every buffer of the
  /// ring is still with Flutter.
  func draw(frame: Int) -> Bool {
    guard usesRing else {
      return drawPixelBuffer(frame: frame)
    }

    guard let index = takeFreeBuffer() else {
      return false
    }

    let surface = surfaces[index]
    IOSurfaceLock(surface, [], nil)
    paint(
      IOSurfaceGetBaseAddress(surface), bytesPerRow: IOSurfaceGetBytesPerRow(surface), frame: frame)
    IOSurfaceUnlock(surface, [], nil)
    sft_texture_present(texture, Self.generation, Int32(index))
    return true
  }

  /// No callback runs once this returns.
  func dispose() {
    sft_texture_dispose(texture)
    callbackBox.source = nil
  }

  /// On Flutter's raster thread, or inside `sft_texture_present`.
  private func onReleased(_ index: Int) {
    lock.lock()
    free[index] = true
    released += 1
    lock.unlock()
  }

  private func takeFreeBuffer() -> Int? {
    lock.lock()
    defer { lock.unlock() }
    guard let index = free.firstIndex(of: true) else {
      return nil
    }

    free[index] = false
    presented += 1
    return index
  }

  private func drawPixelBuffer(frame: Int) -> Bool {
    let attributes: [CFString: Any] = [
      kCVPixelBufferMetalCompatibilityKey: true,
      kCVPixelBufferIOSurfacePropertiesKey: [String: Any](),
    ]
    var created: CVPixelBuffer?
    CVPixelBufferCreate(
      kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA, attributes as CFDictionary,
      &created)
    guard let created else {
      return false
    }

    CVPixelBufferLockBaseAddress(created, [])
    paint(
      CVPixelBufferGetBaseAddress(created), bytesPerRow: CVPixelBufferGetBytesPerRow(created),
      frame: frame)
    CVPixelBufferUnlockBaseAddress(created, [])
    sft_texture_present_pixel_buffer(texture, created)
    lock.lock()
    presented += 1
    lock.unlock()
    return true
  }

  /// A background that changes colour slowly and a white bar that crosses
  /// it, BGRA.
  private func paint(_ base: UnsafeMutableRawPointer?, bytesPerRow: Int, frame: Int) {
    guard let base else {
      return
    }

    let shade = UInt32(64 + (frame / 2) % 128)
    var background: UInt32 = 0xFF00_0000 | (shade << 16) | (0x40 << 8) | (192 - shade)
    var white: UInt32 = 0xFFFF_FFFF
    let barWidth = 48
    let barStart = (frame * 4) % max(width - barWidth, 1)
    for row in 0..<height {
      let line = base + row * bytesPerRow
      memset_pattern4(line, &background, width * 4)
      memset_pattern4(line + barStart * 4, &white, barWidth * 4)
    }
  }
}

/// What the C callback gets as its user data: the source it belongs to,
/// until that one is disposed.
private final class CallbackBox {
  weak var source: DemoSource?
}
