cmake_minimum_required(VERSION 3.16)

file(GLOB basic_games_native_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(basic_games_native SHARED ${basic_games_native_SOURCES})
mo2_configure_plugin(basic_games_native NO_SOURCES WARNINGS OFF)
target_link_libraries(basic_games_native PRIVATE mo2::uibase)
mo2_install_plugin(basic_games_native)
