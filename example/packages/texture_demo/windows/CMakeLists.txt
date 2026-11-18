cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "texture_demo")
project(${PROJECT_NAME} LANGUAGES CXX)

cmake_policy(VERSION 3.14...3.25)

set(PLUGIN_NAME "texture_demo_plugin")

add_library(${PLUGIN_NAME} SHARED
  "include/texture_demo/texture_demo_plugin.h"
  "texture_demo_plugin.cpp"
)

apply_standard_settings(${PLUGIN_NAME})

set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)
# sofluffy_texture's C API: its headers come with the target.
target_link_libraries(${PLUGIN_NAME} PRIVATE sofluffy_texture_plugin)
target_link_libraries(${PLUGIN_NAME} PRIVATE d3d11)

set(texture_demo_bundled_libraries
  ""
  PARENT_SCOPE
)
