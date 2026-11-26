# HarfBuzz (https://github.com/harfbuzz/harfbuzz, "Old MIT", see COPYING) — shapes Atrament's text
# and gives its glyphs' outlines (src/atrament/text, docs/ATRAMENT.md D7). Vendored, one release
# (pin.json): src/ holds the release's files that harfbuzz.cc needs, unchanged; harfbuzz.cc is
# upstream's amalgamation without the integrations (FreeType, glib, ICU, Graphite, CoreText,
# DirectWrite, GDI, Uniscribe, WebAssembly) — the core, OpenType shaping, hb-draw and hb-paint with
# HarfBuzz's own Unicode tables. Update: scripts/harfbuzz-vendor.sh <tag>.
#
# The same sources on every platform, the system's HarfBuzz never: text is shaped and drawn the
# same everywhere (the golden images), and nothing has to be found or downloaded. One static
# library, its symbols hidden: a program whose Qt has a HarfBuzz of its own (bundled into QtGui,
# or the system's) gets two copies that never meet. Warnings are disabled for this third-party
# code (Brulion's -Werror never applies to it).
add_library(brulion_harfbuzz STATIC harfbuzz.cc)
target_include_directories(brulion_harfbuzz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# HarfBuzz is C++11; upstream builds it without exceptions, RTTI and guarded statics.
set_target_properties(brulion_harfbuzz PROPERTIES
    CXX_STANDARD 17
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
if(MSVC)
    target_compile_options(brulion_harfbuzz PRIVATE /w)
else()
    target_compile_options(brulion_harfbuzz PRIVATE -w -fno-exceptions -fno-rtti -fno-threadsafe-statics)
    # A debug build still shapes at full speed (every frame shapes its new lines); its asserts stay.
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(brulion_harfbuzz PRIVATE -O2)
    endif()
endif()
