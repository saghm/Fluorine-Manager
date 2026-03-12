cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS OpenGL OpenGLWidgets)

file(GLOB preview_dds_native_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(preview_dds_native SHARED ${preview_dds_native_SOURCES})
mo2_configure_plugin(preview_dds_native NO_SOURCES WARNINGS OFF)
target_link_libraries(preview_dds_native PRIVATE
  mo2::uibase
  Qt6::OpenGL
  Qt6::OpenGLWidgets
)
mo2_install_plugin(preview_dds_native)
