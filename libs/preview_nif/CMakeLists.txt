cmake_minimum_required(VERSION 3.22)

project(preview_nif CXX)

include(FetchContent)

# gli (texture loader — header-only, needs glm)
FetchContent_Declare(
    gli
    GIT_REPOSITORY https://github.com/g-truc/gli.git
    GIT_TAG        master
)
set(GLI_TEST_ENABLE OFF CACHE BOOL "Build gli unit tests")

# nifly (NIF parser)
FetchContent_Declare(
    nifly
    GIT_REPOSITORY https://github.com/ousnius/nifly.git
    GIT_TAG        main
)

FetchContent_MakeAvailable(gli nifly)

# mo2_install_target compatibility shim (upstream uses mo2_install_target;
# Fluorine defines mo2_install_plugin).
if(NOT COMMAND mo2_install_target AND COMMAND mo2_install_plugin)
    function(mo2_install_target target)
        mo2_install_plugin(${target})
    endfunction()
endif()

add_subdirectory(src)

target_link_libraries(preview_nif PRIVATE nifly gli)
