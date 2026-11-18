//
//  Generated file. Do not edit.
//

// clang-format off

#include "generated_plugin_registrant.h"

#include <sofluffy_texture/sofluffy_texture_plugin.h>
#include <texture_demo/texture_demo_plugin.h>

void fl_register_plugins(FlPluginRegistry* registry) {
  g_autoptr(FlPluginRegistrar) sofluffy_texture_registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry, "SofluffyTexturePlugin");
  sofluffy_texture_plugin_register_with_registrar(sofluffy_texture_registrar);
  g_autoptr(FlPluginRegistrar) texture_demo_registrar =
      fl_plugin_registry_get_registrar_for_plugin(registry, "TextureDemoPlugin");
  texture_demo_plugin_register_with_registrar(texture_demo_registrar);
}
