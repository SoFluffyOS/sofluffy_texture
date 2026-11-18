cmake_minimum_required(VERSION 3.10)

set(PROJECT_NAME "texture_demo")
project(${PROJECT_NAME} LANGUAGES CXX)

# Needs GBM for its buffers (libgbm-dev on Debian).
set(PLUGIN_NAME "texture_demo_plugin")

add_library(${PLUGIN_NAME} SHARED
  "texture_demo_plugin.cc"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::GTK)
# sofluffy_texture's C API: its headers come with the target.
target_link_libraries(${PLUGIN_NAME} PRIVATE sofluffy_texture_plugin)
pkg_check_modules(TEXTURE_DEMO_GBM REQUIRED IMPORTED_TARGET gbm)
target_link_libraries(${PLUGIN_NAME} PRIVATE PkgConfig::TEXTURE_DEMO_GBM)

set(texture_demo_bundled_libraries
  ""
  PARENT_SCOPE
)
