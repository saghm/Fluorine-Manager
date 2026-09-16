set(LOCALE_OUTPUT "${CMAKE_BINARY_DIR}/src/src/locale")
foreach(arch IN ITEMS x86_64 i386)
  add_library(fluorine_locale_${arch} SHARED locale_guard.c)
  set_target_properties(fluorine_locale_${arch} PROPERTIES
      OUTPUT_NAME fluorine_locale
      LIBRARY_OUTPUT_DIRECTORY "${LOCALE_OUTPUT}/${arch}"
      AUTOMOC OFF C_STANDARD 11)
  if(arch STREQUAL "i386")
    target_compile_options(fluorine_locale_${arch} PRIVATE -m32)
    target_link_options(fluorine_locale_${arch} PRIVATE -m32)
  endif()
  target_compile_options(fluorine_locale_${arch} PRIVATE -Wall -Wextra -fno-builtin -fno-stack-protector)
  target_link_options(fluorine_locale_${arch} PRIVATE -nostdlib)
endforeach()
