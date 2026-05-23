cmake_minimum_required(VERSION 3.16)

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(GTest REQUIRED)

# ---------------------------------------------------------------------------
# meta.ini case-normalization helper tests
# ---------------------------------------------------------------------------
add_executable(test_metainiutils EXCLUDE_FROM_ALL
    test_metainiutils.cpp
    ${CMAKE_SOURCE_DIR}/src/src/metainiutils.cpp
)
set_target_properties(test_metainiutils PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_metainiutils PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_metainiutils PRIVATE
    Qt6::Core
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_metainiutils COMMAND test_metainiutils)

# ---------------------------------------------------------------------------
# External directory mapping (SKSE Log Redirector style) tests
#
# Exercises the deploy/cleanup logic for `isDirectory && createTarget`
# mappings — directory-symlink path plus the per-file fallback when the
# destination already has real content. The behavior is mirrored from
# FuseConnector::deployExternalMappings so the test can run without
# bringing in libfuse or the rest of the organizer dependency tree.
# ---------------------------------------------------------------------------
add_executable(test_external_dir_mapping EXCLUDE_FROM_ALL
    test_external_dir_mapping.cpp
)
set_target_properties(test_external_dir_mapping PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_link_libraries(test_external_dir_mapping PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_external_dir_mapping COMMAND test_external_dir_mapping)

# ---------------------------------------------------------------------------
# Overwrite duplicate scan tests
# ---------------------------------------------------------------------------
add_executable(test_overwrite_duplicates EXCLUDE_FROM_ALL
    test_overwrite_duplicates.cpp
    ${CMAKE_SOURCE_DIR}/src/src/vfs/trackedwrites.cpp
)
set_target_properties(test_overwrite_duplicates PROPERTIES
    AUTOMOC OFF
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
)
target_include_directories(test_overwrite_duplicates PRIVATE
    ${CMAKE_SOURCE_DIR}/src/src
)
target_link_libraries(test_overwrite_duplicates PRIVATE
    GTest::gtest
    GTest::gtest_main
)
add_test(NAME test_overwrite_duplicates COMMAND test_overwrite_duplicates)

# Register a "fluorine-tests" umbrella target that builds every test exe.
add_custom_target(fluorine-tests
    DEPENDS test_metainiutils test_external_dir_mapping test_overwrite_duplicates)
