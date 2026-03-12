cmake_minimum_required(VERSION 3.16)

file(GLOB script_extender_checker_native_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(script_extender_checker_native SHARED ${script_extender_checker_native_SOURCES})
mo2_configure_plugin(script_extender_checker_native NO_SOURCES WARNINGS OFF)
target_link_libraries(script_extender_checker_native PRIVATE mo2::uibase)
mo2_install_plugin(script_extender_checker_native)
