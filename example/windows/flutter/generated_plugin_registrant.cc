//
//  Generated file. Do not edit.
//

// clang-format off

#include "generated_plugin_registrant.h"

#include <sofluffy_texture/sofluffy_texture_plugin.h>
#include <texture_demo/texture_demo_plugin.h>

void RegisterPlugins(flutter::PluginRegistry* registry) {
  SofluffyTexturePluginRegisterWithRegistrar(
      registry->GetRegistrarForPlugin("SofluffyTexturePlugin"));
  TextureDemoPluginRegisterWithRegistrar(
      registry->GetRegistrarForPlugin("TextureDemoPlugin"));
}
