cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

file(GLOB SOURCES *.cpp *.h)

add_library(rootbuilder_native SHARED ${SOURCES})

set_target_properties(rootbuilder_native PROPERTIES
    AUTOMOC ON
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    PREFIX "lib"
)

target_link_libraries(rootbuilder_native PRIVATE mo2::uibase Qt6::Widgets)
install(TARGETS rootbuilder_native LIBRARY DESTINATION bin/plugins)
