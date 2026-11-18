// Draws a moving bar into a ring of three dmabufs from GBM and presents
// them through sofluffy_texture.
#include "include/texture_demo/texture_demo_plugin.h"

#include <fcntl.h>
#include <flutter_linux/flutter_linux.h>
#include <gbm.h>
#include <sofluffy_texture/sofluffy_texture.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {

constexpr int kRingSize = 3;
constexpr uint32_t kGeneration = 1;

struct DemoSource {
  uint32_t width = 0;
  uint32_t height = 0;
  int device_fd = -1;
  gbm_device* device = nullptr;
  gbm_bo* buffers[kRingSize] = {nullptr, nullptr, nullptr};
  SftTexture* texture = nullptr;
  std::mutex mutex;
  bool free[kRingSize] = {true, true, true};
  int64_t presented = 0;
  int64_t released = 0;
};

// On Flutter's raster thread, or inside sft_texture_present.
void on_released(gpointer user_data, uint32_t generation, int32_t index, int fence_fd) {
  // Drawn on the CPU after mapping, which waits for the GPU by itself.
  if (fence_fd >= 0) close(fence_fd);
  auto* source = static_cast<DemoSource*>(user_data);
  std::lock_guard<std::mutex> lock(source->mutex);
  source->free[index] = true;
  ++source->released;
}

void destroy_source(DemoSource* source) {
  if (source == nullptr) return;
  // No callback runs once the texture is disposed.
  sft_texture_dispose(source->texture);
  for (gbm_bo* buffer : source->buffers) {
    if (buffer != nullptr) gbm_bo_destroy(buffer);
  }
  if (source->device != nullptr) gbm_device_destroy(source->device);
  if (source->device_fd >= 0) close(source->device_fd);
  delete source;
}

// Null when there is no GPU to allocate from, or Flutter refused.
DemoSource* create_source(FlTextureRegistrar* registrar, uint32_t width, uint32_t height) {
  auto* source = new DemoSource();
  source->width = width;
  source->height = height;
  for (int node = 128; node < 136 && source->device_fd < 0; ++node) {
    char path[32];
    std::snprintf(path, sizeof(path), "/dev/dri/renderD%d", node);
    source->device_fd = open(path, O_RDWR | O_CLOEXEC);
  }
  if (source->device_fd >= 0) source->device = gbm_create_device(source->device_fd);
  if (source->device == nullptr) {
    destroy_source(source);
    return nullptr;
  }
  SftDmabuf described[kRingSize] = {};
  int fds[kRingSize] = {-1, -1, -1};
  bool made = true;
  for (int i = 0; i < kRingSize && made; ++i) {
    source->buffers[i] = gbm_bo_create(source->device, width, height, GBM_FORMAT_ARGB8888,
                                       GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    made = source->buffers[i] != nullptr;
    if (!made) break;
    fds[i] = gbm_bo_get_fd(source->buffers[i]);
    made = fds[i] >= 0;
    described[i].fd = fds[i];
    described[i].stride = gbm_bo_get_stride(source->buffers[i]);
    described[i].offset = gbm_bo_get_offset(source->buffers[i], 0);
  }
  if (made) {
    source->texture = sft_texture_create(registrar, on_released, source);
    made = source->texture != nullptr &&
           sft_texture_set_dmabufs(source->texture, kGeneration, width, height,
                                   GBM_FORMAT_ARGB8888,
                                   gbm_bo_get_modifier(source->buffers[0]), described,
                                   kRingSize);
  }
  // The texture duplicated them.
  for (int fd : fds) {
    if (fd >= 0) close(fd);
  }
  if (!made) {
    destroy_source(source);
    return nullptr;
  }
  return source;
}

// A background that changes colour slowly and a white bar that crosses it,
// BGRA.
void paint(DemoSource* source, uint8_t* pixels, uint32_t stride, int64_t frame) {
  const uint32_t shade = 64 + static_cast<uint32_t>((frame / 2) % 128);
  const uint32_t background = 0xFF000000u | (shade << 16) | (0x40u << 8) | (192 - shade);
  const uint32_t bar_width = 48;
  const uint32_t travel = source->width > bar_width ? source->width - bar_width : 1;
  const uint32_t bar_start = static_cast<uint32_t>((frame * 4) % travel);
  for (uint32_t y = 0; y < source->height; ++y) {
    uint8_t* line = pixels + static_cast<size_t>(y) * stride;
    for (uint32_t x = 0; x < source->width; ++x) {
      const bool in_bar = x >= bar_start && x < bar_start + bar_width;
      const uint32_t colour = in_bar ? 0xFFFFFFFFu : background;
      std::memcpy(line + static_cast<size_t>(x) * 4, &colour, 4);
    }
  }
}

// Draws frame |frame| and presents it. False when every buffer of the ring
// is still with Flutter.
bool draw(DemoSource* source, int64_t frame) {
  int index = -1;
  {
    std::lock_guard<std::mutex> lock(source->mutex);
    for (int i = 0; i < kRingSize && index < 0; ++i) {
      if (source->free[i]) index = i;
    }
    if (index < 0) return false;
    source->free[index] = false;
    ++source->presented;
  }
  uint32_t stride = 0;
  void* mapping = nullptr;
  void* pixels = gbm_bo_map(source->buffers[index], 0, 0, source->width, source->height,
                            GBM_BO_TRANSFER_WRITE, &stride, &mapping);
  if (pixels != nullptr) {
    paint(source, static_cast<uint8_t*>(pixels), stride, frame);
    gbm_bo_unmap(source->buffers[index], mapping);
  }
  // No fence: the pixels were written on the CPU.
  sft_texture_present(source->texture, kGeneration, index, -1);
  return true;
}

}  // namespace

#define TEXTURE_DEMO_PLUGIN(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), texture_demo_plugin_get_type(), TextureDemoPlugin))

struct _TextureDemoPlugin {
  GObject parent_instance;
  FlTextureRegistrar* textures;
  DemoSource* source;
};

typedef struct _TextureDemoPlugin TextureDemoPlugin;
typedef struct {
  GObjectClass parent_class;
} TextureDemoPluginClass;

G_DEFINE_TYPE(TextureDemoPlugin, texture_demo_plugin, g_object_get_type())

static int64_t int_argument(FlValue* args, const char* key, int64_t fallback) {
  if (args == nullptr || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) return fallback;
  FlValue* value = fl_value_lookup_string(args, key);
  if (value == nullptr || fl_value_get_type(value) != FL_VALUE_TYPE_INT) return fallback;
  return fl_value_get_int(value);
}

static void handle_method_call(TextureDemoPlugin* self, FlMethodCall* method_call) {
  g_autoptr(FlMethodResponse) response = nullptr;
  const gchar* method = fl_method_call_get_name(method_call);
  FlValue* args = fl_method_call_get_args(method_call);
  if (strcmp(method, "create") == 0) {
    destroy_source(self->source);
    self->source = create_source(self->textures,
                                 static_cast<uint32_t>(int_argument(args, "width", 640)),
                                 static_cast<uint32_t>(int_argument(args, "height", 360)));
    if (self->source == nullptr) {
      response = FL_METHOD_RESPONSE(
          fl_method_error_response_new("create", "Could not make the buffers", nullptr));
    } else {
      g_autoptr(FlValue) id = fl_value_new_int(sft_texture_id(self->source->texture));
      response = FL_METHOD_RESPONSE(fl_method_success_response_new(id));
    }
  } else if (strcmp(method, "draw") == 0) {
    const bool drawn =
        self->source != nullptr && draw(self->source, int_argument(args, "frame", 0));
    g_autoptr(FlValue) result = fl_value_new_bool(drawn);
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(result));
  } else if (strcmp(method, "stats") == 0) {
    g_autoptr(FlValue) stats = fl_value_new_map();
    if (self->source != nullptr) {
      std::lock_guard<std::mutex> lock(self->source->mutex);
      fl_value_set_string_take(stats, "presented", fl_value_new_int(self->source->presented));
      fl_value_set_string_take(stats, "released", fl_value_new_int(self->source->released));
    }
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(stats));
  } else if (strcmp(method, "dispose") == 0) {
    destroy_source(self->source);
    self->source = nullptr;
    response = FL_METHOD_RESPONSE(fl_method_success_response_new(nullptr));
  } else {
    response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
  }
  fl_method_call_respond(method_call, response, nullptr);
}

static void texture_demo_plugin_dispose(GObject* object) {
  TextureDemoPlugin* self = TEXTURE_DEMO_PLUGIN(object);
  destroy_source(self->source);
  self->source = nullptr;
  G_OBJECT_CLASS(texture_demo_plugin_parent_class)->dispose(object);
}

static void texture_demo_plugin_class_init(TextureDemoPluginClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = texture_demo_plugin_dispose;
}

static void texture_demo_plugin_init(TextureDemoPlugin* self) {
  self->source = nullptr;
}

static void method_call_cb(FlMethodChannel* channel, FlMethodCall* method_call,
                           gpointer user_data) {
  handle_method_call(TEXTURE_DEMO_PLUGIN(user_data), method_call);
}

void texture_demo_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  TextureDemoPlugin* plugin =
      TEXTURE_DEMO_PLUGIN(g_object_new(texture_demo_plugin_get_type(), nullptr));
  plugin->textures = fl_plugin_registrar_get_texture_registrar(registrar);
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  g_autoptr(FlMethodChannel) channel = fl_method_channel_new(
      fl_plugin_registrar_get_messenger(registrar), "texture_demo", FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(channel, method_call_cb, g_object_ref(plugin),
                                            g_object_unref);
  g_object_unref(plugin);
}
