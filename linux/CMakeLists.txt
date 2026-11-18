cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME "sofluffy_texture")
project(${PROJECT_NAME} LANGUAGES CXX)

# dmabufs as Flutter textures. Needs EGL and GLES 2 (libegl1-mesa-dev and
# libgles2-mesa-dev on Debian). Other plugins link this target and include
# <sofluffy_texture/sofluffy_texture.h>:
#
#   target_link_libraries(${PLUGIN_NAME} PRIVATE sofluffy_texture_plugin)
set(PLUGIN_NAME "sofluffy_texture_plugin")

add_library(${PLUGIN_NAME} SHARED
  "sofluffy_texture.cc"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE SOFLUFFY_TEXTURE_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)
pkg_check_modules(SOFLUFFY_TEXTURE_GL REQUIRED IMPORTED_TARGET egl glesv2)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::SOFLUFFY_TEXTURE_GL)

set(sofluffy_texture_bundled_libraries
  ""
  PARENT_SCOPE
)
