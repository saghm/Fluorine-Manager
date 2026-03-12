cmake_minimum_required(VERSION 3.16)

file(GLOB installer_omod_native_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(installer_omod_native SHARED ${installer_omod_native_SOURCES})
mo2_configure_plugin(installer_omod_native NO_SOURCES WARNINGS OFF)
target_link_libraries(installer_omod_native PRIVATE mo2::uibase Qt6::Widgets z lzma)
mo2_install_plugin(installer_omod_native)
