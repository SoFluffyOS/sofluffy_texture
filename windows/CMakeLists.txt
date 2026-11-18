cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "sofluffy_texture")
project(${PROJECT_NAME} LANGUAGES CXX)

cmake_policy(VERSION 3.14...3.25)

# Shared D3D11 textures as Flutter textures. Other plugins link this target
# and include <sofluffy_texture/sofluffy_texture.h>:
#
#   target_link_libraries(${PLUGIN_NAME} PRIVATE sofluffy_texture_plugin)
set(PLUGIN_NAME "sofluffy_texture_plugin")

add_library(${PLUGIN_NAME} SHARED
  "include/sofluffy_texture/sofluffy_texture.h"
  "include/sofluffy_texture/sofluffy_texture_plugin.h"
  "sofluffy_texture.cpp"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE SOFLUFFY_TEXTURE_IMPL)

# INTERFACE: whoever links the target finds the headers, and Flutter's too,
# which sofluffy_texture.h includes.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)

set(sofluffy_texture_bundled_libraries
  ""
  PARENT_SCOPE
)
