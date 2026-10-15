# miniz 3.1.2 (https://github.com/richgel999/miniz, MIT, see LICENSE) — deflate/inflate and ZIP
# reading/writing used by src/core/io. Vendored unmodified (only miniz.c, miniz.h, LICENSE).
#
# The project enables only CXX, so miniz.c is compiled as C++ (it is C++-clean). Warnings are
# disabled for this third-party code; Brulion's -Werror flags never apply to it.
add_library(brulion_miniz STATIC miniz.c miniz.h)
set_source_files_properties(miniz.c PROPERTIES LANGUAGE CXX)
set_target_properties(brulion_miniz PROPERTIES LINKER_LANGUAGE CXX POSITION_INDEPENDENT_CODE ON)
target_include_directories(brulion_miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Keep zlib-compatible macro names (deflate, crc32, ...) out of our translation units.
target_compile_definitions(brulion_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
target_compile_options(brulion_miniz PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-w> $<$<CXX_COMPILER_ID:MSVC>:/w>)
