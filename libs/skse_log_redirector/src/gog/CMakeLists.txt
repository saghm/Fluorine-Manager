cmake_minimum_required(VERSION 3.16)

add_library(skse_log_redirector_gog SHARED
  skseLogRedirectorGOG.cpp
  skseLogRedirectorGOG.h
)
mo2_configure_plugin(skse_log_redirector_gog NO_SOURCES WARNINGS OFF)
target_link_libraries(skse_log_redirector_gog PRIVATE
  mo2::uibase
  skse_log_redirector_common
)

mo2_install_plugin(skse_log_redirector_gog)
