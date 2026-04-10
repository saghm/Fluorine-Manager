cmake_minimum_required(VERSION 3.16)

file(GLOB bethinipie_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/*.h
  ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(tool_bethinipie SHARED ${bethinipie_SOURCES})
mo2_configure_plugin(tool_bethinipie NO_SOURCES WARNINGS 4)
target_link_libraries(tool_bethinipie PRIVATE mo2::uibase)
mo2_install_plugin(tool_bethinipie)
