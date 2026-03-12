cmake_minimum_required(VERSION 3.16)

file(GLOB form43_checker_native_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(form43_checker_native SHARED ${form43_checker_native_SOURCES})
mo2_configure_plugin(form43_checker_native NO_SOURCES WARNINGS OFF)
target_link_libraries(form43_checker_native PRIVATE mo2::uibase)
mo2_install_plugin(form43_checker_native)
