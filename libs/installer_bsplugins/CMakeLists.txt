# modorganizer-bsplugins — vendored from github.com/Exit-9B/modorganizer-bsplugins
# Branch: update-mob
# Commit: d52d5d73a591c747962a3a235d4851deb421b009 (2026-04-07)
#
# Record-level conflict view for Bethesda plugins (ESP/ESM/ESL).
# LOOT integration gutted for Fluorine port (sort button + masterlist import
# + dirty tooltips). Keeps the big feature: form record conflict view.

cmake_minimum_required(VERSION 3.22)

# Compat shim: upstream calls mo2_install_target, fluorine defines mo2_install_plugin.
if (NOT COMMAND mo2_install_target AND COMMAND mo2_install_plugin)
    function(mo2_install_target target)
        mo2_install_plugin(${target})
    endfunction()
endif ()

# bsplugins uses unprefixed includes (`<iplugin.h>`, `<gameplugins.h>`, ...).
# We can't put uibase/include/uibase directly on -I because it contains
# `strings.h` which shadows libc's POSIX <strings.h> (libc's string.h does
# `#include <strings.h>`, and gcc picks our -I copy first → pulls in C++ <string>
# inside an extern "C" block → "template with C linkage" errors everywhere).
#
# Fix: build a passthrough include dir with symlinks of every uibase header
# EXCEPT strings.h. Use the passthrough dir on -I.
set(_UIBASE_INCLUDE_ROOT "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
set(BSP_UIBASE_FWD "${CMAKE_CURRENT_BINARY_DIR}/uibase_fwd")
file(MAKE_DIRECTORY ${BSP_UIBASE_FWD})
file(MAKE_DIRECTORY ${BSP_UIBASE_FWD}/game_features)
file(MAKE_DIRECTORY ${BSP_UIBASE_FWD}/formatters)
file(GLOB _uibase_headers "${_UIBASE_INCLUDE_ROOT}/*.h")
foreach(h ${_uibase_headers})
    get_filename_component(_hn ${h} NAME)
    if(NOT _hn STREQUAL "strings.h")
        file(CREATE_LINK "${h}" "${BSP_UIBASE_FWD}/${_hn}" SYMBOLIC)
    endif()
endforeach()
file(GLOB _gf_headers "${_UIBASE_INCLUDE_ROOT}/game_features/*.h")
foreach(h ${_gf_headers})
    get_filename_component(_hn ${h} NAME)
    file(CREATE_LINK "${h}" "${BSP_UIBASE_FWD}/game_features/${_hn}" SYMBOLIC)
    # Also flat at top level — bsplugins mixes `<game_features/foo.h>` and `<foo.h>`.
    file(CREATE_LINK "${h}" "${BSP_UIBASE_FWD}/${_hn}" SYMBOLIC)
endforeach()
# formatters/ subdir (formatters.h uses `#include "./formatters/enums.h"`).
file(GLOB _fmt_headers "${_UIBASE_INCLUDE_ROOT}/formatters/*.h")
foreach(h ${_fmt_headers})
    get_filename_component(_hn ${h} NAME)
    file(CREATE_LINK "${h}" "${BSP_UIBASE_FWD}/formatters/${_hn}" SYMBOLIC)
endforeach()
set(MO2_UIBASE_INCLUDE_DIRS ${BSP_UIBASE_FWD})

add_subdirectory(src)
