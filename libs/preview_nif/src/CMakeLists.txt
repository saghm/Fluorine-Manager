cmake_minimum_required(VERSION 3.22)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Widgets OpenGL OpenGLWidgets)
find_package(fmt CONFIG REQUIRED)

file(GLOB preview_nif_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/*.h
)

add_library(preview_nif SHARED ${preview_nif_SOURCES})

mo2_configure_plugin(preview_nif NO_SOURCES WARNINGS OFF)

target_include_directories(preview_nif PRIVATE
    ${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase/game_features
    ${CMAKE_SOURCE_DIR}/libs/libbsarch/include/libbsarch
)

target_link_libraries(preview_nif PRIVATE
    mo2::uibase
    libbsarch
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    fmt::fmt
)

mo2_install_plugin(preview_nif)

# Stage shader files next to the plugin under plugins/data/shaders/.
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../data/shaders
    DESTINATION plugins/data
    FILES_MATCHING PATTERN "*.vert" PATTERN "*.frag"
)
