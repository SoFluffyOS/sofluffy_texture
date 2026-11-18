import FlutterMacOS
import Foundation

public class TextureDemoPlugin: NSObject, FlutterPlugin {
  private let registry: FlutterTextureRegistry
  private var source: DemoSource?

  init(registry: FlutterTextureRegistry) {
    self.registry = registry
  }

  public static func register(with registrar: FlutterPluginRegistrar) {
    let channel = FlutterMethodChannel(name: "texture_demo", binaryMessenger: registrar.messenger)
    let instance = TextureDemoPlugin(registry: registrar.textures)
    registrar.addMethodCallDelegate(instance, channel: channel)
  }

  public func handle(_ call: FlutterMethodCall, result: @escaping FlutterResult) {
    let arguments = call.arguments as? [String: Any] ?? [:]
    switch call.method {
    case "create":
      source?.dispose()
      source = DemoSource(
        registry: registry,
        width: arguments["width"] as? Int ?? 640,
        height: arguments["height"] as? Int ?? 360,
        usesRing: arguments["mode"] as? String != "pixelBuffer")
      guard let source else {
        result(FlutterError(code: "create", message: "Could not make the buffers", details: nil))
        return
      }

      result(source.textureId)
    case "draw":
      result(source?.draw(frame: arguments["frame"] as? Int ?? 0) ?? false)
    case "stats":
      result(source?.stats ?? [:])
    case "dispose":
      source?.dispose()
      source = nil
      result(nil)
    default:
      result(FlutterMethodNotImplemented)
    }
  }
}
