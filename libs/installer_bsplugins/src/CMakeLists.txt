cmake_minimum_required(VERSION 3.22)

# Fluorine port: simplified from upstream vcpkg-based build.
# Removed deps: WebEngineWidgets (LOOT dialog gutted), ryml (LootGroups gutted),
# lootcli (stubbed), lz4 (transitive via bsatk), interprocess/uuid/accumulators
# boost libs (unused), libbsarch/dds-header (unused). See README / commit for rationale.

find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(ZLIB REQUIRED)
find_package(Boost CONFIG REQUIRED COMPONENTS thread)
find_package(Python 3 COMPONENTS Interpreter REQUIRED)

# ── TES form parser codegen (from ParseTES.cmake, simplified: no esp.json fetch,
# we vendor or skip the generated header on the fly) ──
include(FetchContent)
FetchContent_Declare(
    esp_json
    GIT_REPOSITORY https://github.com/matortheeternal/esp.json.git
    GIT_TAG master
)
FetchContent_MakeAvailable(esp_json)

set(CODEGEN_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/MakeFormParser.py)
set(TES_INCLUDE_FILES "")
foreach(GAME SSE)
    set(INPUT_FILE ${esp_json_SOURCE_DIR}/data/${GAME}.json)
    set(OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/include/FormParser.${GAME}.inl)
    add_custom_command(
        OUTPUT ${OUTPUT_FILE}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/include
        COMMAND ${Python_EXECUTABLE} ${CODEGEN_SCRIPT} ${INPUT_FILE} ${OUTPUT_FILE}
        DEPENDS ${CODEGEN_SCRIPT} ${INPUT_FILE}
        VERBATIM
    )
    list(APPEND TES_INCLUDE_FILES ${OUTPUT_FILE})
endforeach()

# ── Source collection ──
file(GLOB_RECURSE BSPLUGINS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginInfo/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginList/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GUI/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MOPlugin/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MOTools/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TESData/*.cpp
)
file(GLOB_RECURSE BSPLUGINS_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginInfo/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginList/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/GUI/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MOPlugin/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/MOTools/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TESData/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/TESFile/*.h
)
file(GLOB BSPLUGINS_UI
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginInfo/*.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginList/*.ui
    ${CMAKE_CURRENT_SOURCE_DIR}/GUI/*.ui
)
file(GLOB BSPLUGINS_QRC
    ${CMAKE_CURRENT_SOURCE_DIR}/*.qrc
)

add_library(bsplugins SHARED
    ${BSPLUGINS_SOURCES}
    ${BSPLUGINS_HEADERS}
    ${BSPLUGINS_UI}
    ${BSPLUGINS_QRC}
    ${TES_INCLUDE_FILES}
)

set_target_properties(bsplugins PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(bsplugins
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/include
        # Bare uibase includes (<iplugin.h>, <log.h>, <gameplugins.h>, etc.)
        ${MO2_UIBASE_INCLUDE_DIRS}
        # moc's preprocessor needs these on -I so quote-includes like
        # `#include "IPluginPanel.h"` inside MOPlugin/BSPlugins.h resolve.
        ${CMAKE_CURRENT_SOURCE_DIR}/MOPlugin
        ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginInfo
        ${CMAKE_CURRENT_SOURCE_DIR}/BSPluginList
        ${CMAKE_CURRENT_SOURCE_DIR}/GUI
        ${CMAKE_CURRENT_SOURCE_DIR}/MOTools
        ${CMAKE_CURRENT_SOURCE_DIR}/TESData
        ${CMAKE_CURRENT_SOURCE_DIR}/TESFile
)

target_link_libraries(bsplugins PRIVATE
    mo2::uibase
    mo2::bsatk
    Qt6::Widgets
    Boost::thread
    ZLIB::ZLIB
)

# Install into fluorine plugins dir (uses fomod_plus-style shim).
if(COMMAND mo2_install_plugin)
    mo2_install_plugin(bsplugins)
elseif(COMMAND mo2_install_target)
    mo2_install_target(bsplugins)
endif()
