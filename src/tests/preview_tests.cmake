# Regression tests for setup extraction and preview context lifetimes.
add_executable(test_xrandrinstaller EXCLUDE_FROM_ALL
    test_xrandrinstaller.cpp ${CMAKE_SOURCE_DIR}/src/src/xrandrinstaller.cpp)
set_target_properties(test_xrandrinstaller PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
target_include_directories(test_xrandrinstaller PRIVATE ${CMAKE_SOURCE_DIR}/src/src)
target_link_libraries(test_xrandrinstaller PRIVATE Qt6::Core GTest::gtest_main)
add_test(NAME test_xrandrinstaller COMMAND test_xrandrinstaller)
add_dependencies(fluorine-tests test_xrandrinstaller)

if(TARGET preview_nif)
    find_package(Qt6 REQUIRED COMPONENTS OpenGL OpenGLWidgets Test)
    add_executable(test_nif_preview EXCLUDE_FROM_ALL test_nif_preview.cpp)
    set_target_properties(test_nif_preview PROPERTIES AUTOMOC OFF CXX_STANDARD 23)
    target_include_directories(test_nif_preview PRIVATE ${CMAKE_SOURCE_DIR}/libs/preview_nif/src)
    target_compile_options(test_nif_preview PRIVATE
        "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase")
    target_compile_definitions(test_nif_preview PRIVATE FLUORINE_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_link_libraries(test_nif_preview PRIVATE preview_nif nifly gli mo2::uibase
        Qt6::Widgets Qt6::OpenGL Qt6::OpenGLWidgets Qt6::Test GTest::gtest)
    add_test(NAME test_nif_preview COMMAND test_nif_preview)
    # Headless runs still exercise early input. Set QT_QPA_PLATFORM=xcb (or
    # wayland) to also exercise real contexts, shader failures and reparenting.
    set_tests_properties(test_nif_preview PROPERTIES TIMEOUT 60)
    add_dependencies(fluorine-tests test_nif_preview)
endif()

add_test(NAME test_dds_preview
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_dds_preview.py)
set_tests_properties(test_dds_preview PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
