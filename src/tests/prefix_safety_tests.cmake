# Managed-prefix ownership and deletion-boundary tests.
add_executable(test_fluorineconfigownership EXCLUDE_FROM_ALL
    test_fluorineconfigownership.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorineconfig.cpp
    ${CMAKE_SOURCE_DIR}/src/src/fluorinepaths.cpp
)
set_target_properties(test_fluorineconfigownership PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_fluorineconfigownership PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_link_libraries(test_fluorineconfigownership PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_fluorineconfigownership COMMAND test_fluorineconfigownership)

# Skyrim's AppData/Local directory migration must not share the runtime-
# sensitive ContentCatalog.txt with Steam. Documents/My Games remains linked
# for the existing save flow, and Plugins.txt deployment must remain writable.
add_executable(test_prefixsymlinks EXCLUDE_FROM_ALL
    test_prefixsymlinks.cpp
    ${CMAKE_SOURCE_DIR}/src/src/wineprefix.cpp
)
set_target_properties(test_prefixsymlinks PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_prefixsymlinks PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
    ${CMAKE_SOURCE_DIR}/libs/uibase/include
)
target_compile_options(test_prefixsymlinks PRIVATE
    "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)
target_link_libraries(test_prefixsymlinks PRIVATE
    Qt6::Core
    mo2::uibase
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_prefixsymlinks COMMAND test_prefixsymlinks)

add_custom_target(prefix-safety-tests DEPENDS
    test_fluorineconfigownership
    test_prefixsymlinks)
