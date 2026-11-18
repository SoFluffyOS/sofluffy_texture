// Draws a moving bar into a ring of three shared D3D11 textures and
// presents them through sofluffy_texture.
#include "include/texture_demo/texture_demo_plugin.h"

#include <windows.h>

#include <d3d11.h>
#include <wrl/client.h>

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <sofluffy_texture/sofluffy_texture.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

using flutter::EncodableMap;
using flutter::EncodableValue;
using Microsoft::WRL::ComPtr;

constexpr size_t kRingSize = 3;
constexpr uint32_t kGeneration = 1;

class DemoSource {
 public:
  // Null when no device or texture could be made.
  static std::unique_ptr<DemoSource> Create(FlutterDesktopTextureRegistrarRef registrar,
                                            uint32_t width,
                                            uint32_t height) {
    auto source = std::unique_ptr<DemoSource>(new DemoSource(width, height));
    if (!source->Start(registrar)) return nullptr;
    return source;
  }

  // No callback runs once the texture is disposed.
  ~DemoSource() { sft_texture_dispose(texture_); }

  int64_t texture_id() const { return sft_texture_id(texture_); }

  // Draws frame |frame| and presents it. False when every buffer of the
  // ring is still with Flutter.
  bool Draw(int64_t frame) {
    const int32_t index = TakeFreeBuffer();
    if (index < 0) return false;
    Paint(frame);
    context_->UpdateSubresource(textures_[static_cast<size_t>(index)].Get(), 0, nullptr,
                                pixels_.data(), width_ * 4, 0);
    // Sent to the GPU before Flutter's device reads the texture. A renderer
    // whose frames take long waits for an event query here.
    context_->Flush();
    sft_texture_present(texture_, kGeneration, index);
    return true;
  }

  EncodableMap Stats() {
    std::lock_guard<std::mutex> lock(mutex_);
    return EncodableMap{
        {EncodableValue("presented"), EncodableValue(presented_)},
        {EncodableValue("released"), EncodableValue(released_)},
    };
  }

 private:
  DemoSource(uint32_t width, uint32_t height)
      : width_(width), height_(height), pixels_(static_cast<size_t>(width) * height) {}

  bool Start(FlutterDesktopTextureRegistrarRef registrar) {
    const D3D_DRIVER_TYPE drivers[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};
    for (const D3D_DRIVER_TYPE driver : drivers) {
      if (SUCCEEDED(D3D11CreateDevice(nullptr, driver, nullptr,
                                      D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                      D3D11_SDK_VERSION, &device_, nullptr, &context_))) {
        break;
      }
    }
    if (!device_) return false;
    D3D11_TEXTURE2D_DESC description = {};
    description.Width = width_;
    description.Height = height_;
    description.MipLevels = 1;
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // A legacy shared handle: the kind Flutter can open.
    description.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    ID3D11Texture2D* ring[kRingSize] = {};
    for (size_t i = 0; i < kRingSize; ++i) {
      if (FAILED(device_->CreateTexture2D(&description, nullptr, &textures_[i]))) return false;
      ring[i] = textures_[i].Get();
    }
    texture_ = sft_texture_create(registrar, &DemoSource::OnReleased, this);
    if (texture_ == nullptr) return false;
    return sft_texture_set_d3d11_textures(texture_, kGeneration, ring, kRingSize);
  }

  // On Flutter's raster thread, or inside sft_texture_present.
  static void OnReleased(void* self, uint32_t generation, int32_t index) {
    auto* source = static_cast<DemoSource*>(self);
    std::lock_guard<std::mutex> lock(source->mutex_);
    source->free_[static_cast<size_t>(index)] = true;
    ++source->released_;
  }

  int32_t TakeFreeBuffer() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < kRingSize; ++i) {
      if (!free_[i]) continue;
      free_[i] = false;
      ++presented_;
      return static_cast<int32_t>(i);
    }
    return -1;
  }

  // A background that changes colour slowly and a white bar that crosses
  // it, BGRA.
  void Paint(int64_t frame) {
    const uint32_t shade = 64 + static_cast<uint32_t>((frame / 2) % 128);
    const uint32_t background = 0xFF000000u | (shade << 16) | (0x40u << 8) | (192 - shade);
    const uint32_t bar_width = 48;
    const uint32_t travel = width_ > bar_width ? width_ - bar_width : 1;
    const uint32_t bar_start = static_cast<uint32_t>((frame * 4) % travel);
    for (uint32_t y = 0; y < height_; ++y) {
      uint32_t* line = pixels_.data() + static_cast<size_t>(y) * width_;
      for (uint32_t x = 0; x < width_; ++x) {
        const bool in_bar = x >= bar_start && x < bar_start + bar_width;
        line[x] = in_bar ? 0xFFFFFFFFu : background;
      }
    }
  }

  uint32_t width_;
  uint32_t height_;
  std::vector<uint32_t> pixels_;
  ComPtr<ID3D11Device> device_;
  ComPtr<ID3D11DeviceContext> context_;
  ComPtr<ID3D11Texture2D> textures_[kRingSize];
  SftTexture* texture_ = nullptr;
  std::mutex mutex_;
  bool free_[kRingSize] = {true, true, true};
  int64_t presented_ = 0;
  int64_t released_ = 0;
};

class TextureDemoPlugin : public flutter::Plugin {
 public:
  explicit TextureDemoPlugin(FlutterDesktopTextureRegistrarRef textures)
      : textures_(textures) {}

  void Handle(const flutter::MethodCall<EncodableValue>& call,
              std::unique_ptr<flutter::MethodResult<EncodableValue>> result) {
    const auto* arguments = std::get_if<EncodableMap>(call.arguments());
    if (call.method_name() == "create") {
      source_.reset();
      source_ = DemoSource::Create(textures_,
                                   static_cast<uint32_t>(Int(arguments, "width", 640)),
                                   static_cast<uint32_t>(Int(arguments, "height", 360)));
      if (!source_) {
        result->Error("create", "Could not make the buffers");
        return;
      }
      result->Success(EncodableValue(source_->texture_id()));
      return;
    }
    if (call.method_name() == "draw") {
      result->Success(EncodableValue(source_ && source_->Draw(Int(arguments, "frame", 0))));
      return;
    }
    if (call.method_name() == "stats") {
      result->Success(EncodableValue(source_ ? source_->Stats() : EncodableMap{}));
      return;
    }
    if (call.method_name() == "dispose") {
      source_.reset();
      result->Success();
      return;
    }
    result->NotImplemented();
  }

  std::unique_ptr<flutter::MethodChannel<EncodableValue>> channel_;

 private:
  // An integer argument, whichever width the codec gave it.
  static int64_t Int(const EncodableMap* arguments, const char* key, int64_t fallback) {
    if (arguments == nullptr) return fallback;
    const auto found = arguments->find(EncodableValue(key));
    if (found == arguments->end()) return fallback;
    if (const auto* narrow = std::get_if<int32_t>(&found->second)) return *narrow;
    if (const auto* wide = std::get_if<int64_t>(&found->second)) return *wide;
    return fallback;
  }

  FlutterDesktopTextureRegistrarRef textures_;
  std::unique_ptr<DemoSource> source_;
};

}  // namespace

void TextureDemoPluginRegisterWithRegistrar(FlutterDesktopPluginRegistrarRef registrar) {
  auto* windows = flutter::PluginRegistrarManager::GetInstance()
                      ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar);
  // sofluffy_texture takes Flutter's C handle, not the C++ wrapper.
  auto plugin = std::make_unique<TextureDemoPlugin>(
      FlutterDesktopRegistrarGetTextureRegistrar(registrar));
  plugin->channel_ = std::make_unique<flutter::MethodChannel<EncodableValue>>(
      windows->messenger(), "texture_demo", &flutter::StandardMethodCodec::GetInstance());
  plugin->channel_->SetMethodCallHandler(
      [handler = plugin.get()](const auto& call, auto result) {
        handler->Handle(call, std::move(result));
      });
  windows->AddPlugin(std::move(plugin));
}
