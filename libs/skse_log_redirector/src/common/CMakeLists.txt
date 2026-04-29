cmake_minimum_required(VERSION 3.16)

add_library(skse_log_redirector_common STATIC
  skseLogRedirectorBase.cpp
  skseLogRedirectorBase.h
)

set_target_properties(skse_log_redirector_common PROPERTIES
  AUTOMOC ON
  CXX_STANDARD 23
  CXX_STANDARD_REQUIRED ON
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(skse_log_redirector_common
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_options(skse_log_redirector_common PRIVATE
  "-idirafter" "${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)
set_property(TARGET skse_log_redirector_common APPEND PROPERTY AUTOMOC_MOC_OPTIONS
  "-I${CMAKE_SOURCE_DIR}/libs/uibase/include/uibase"
)

target_link_libraries(skse_log_redirector_common PUBLIC mo2::uibase)
