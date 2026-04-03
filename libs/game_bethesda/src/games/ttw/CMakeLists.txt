cmake_minimum_required(VERSION 3.16)

add_library(game_ttw SHARED
	falloutttwbsainvalidation.cpp
	falloutttwbsainvalidation.h
	falloutttwdataarchives.cpp
	falloutttwdataarchives.h
	falloutttwmoddatachecker.h
	falloutttwmoddatacontent.h
	falloutttwsavegame.cpp
	falloutttwsavegame.h
	falloutttwscriptextender.cpp
	falloutttwscriptextender.h
	gamefalloutttw.cpp
	gamefalloutttw.h
)
mo2_configure_plugin(game_ttw NO_SOURCES WARNINGS 4)
mo2_default_source_group()
target_link_libraries(game_ttw PRIVATE game_gamebryo)

# NaK FFI for TTW game detection (TTW uses FalloutNV as base game)
if(TARGET mo2::nak_ffi)
    target_link_libraries(game_ttw PRIVATE mo2::nak_ffi)
    target_compile_definitions(game_ttw PRIVATE HAS_NAK_FFI)
endif()

mo2_install_plugin(game_ttw)
