# The fallback fonts (src/app/ui/fallbackfonts.h, library brulion_ui_fonts): the bundled subsets
# cover every emoji and symbol of src/ (scanned, so a new emoji without a regenerated font fails
# here), missingCharacters() finds what the fonts draw as boxes, installing the fonts makes the
# boxes go away while ordinary text keeps its own font, and no text gets one of them as its font.
# Fonts are installed once per process: tst_fallback_fonts installs them unconditionally (the
# browser's policy), the …_when_missing entries run only the Linux policy.
if(NOT TARGET brulion_ui_fonts OR ANDROID)
    return()
endif()
brulion_add_test(tst_fallback_fonts
    SOURCES tst_fallback_fonts.cpp
    LIBS brulion_ui_fonts)
target_compile_definitions(tst_fallback_fonts PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME tst_fallback_fonts_when_missing COMMAND tst_fallback_fonts installWhenMissing)
set_tests_properties(tst_fallback_fonts_when_missing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The browser's fonts on the desktop: the same tests with a fontconfig that knows only DejaVu Sans
# and DejaVu Sans Mono — all Qt for WebAssembly has — and no alias rules, so "serif" and Qt's
# default "Sans Serif" are no fonts, as in the browser. There the emoji were boxes. Needs
# fc-match and DejaVu on the build machine (else skipped).
find_program(BRULION_FC_MATCH fc-match)
set(_brulion_browser_fonts "")
if(BRULION_FC_MATCH)
    foreach(_family "DejaVu Sans" "DejaVu Sans Mono")
        execute_process(COMMAND ${BRULION_FC_MATCH} -f "%{file}" "${_family}:style=Book"
            OUTPUT_VARIABLE _file RESULT_VARIABLE _rc ERROR_QUIET)
        string(REPLACE " " "" _expected "${_family}.ttf")
        get_filename_component(_name "${_file}" NAME)
        if(_rc EQUAL 0 AND _name STREQUAL _expected)
            list(APPEND _brulion_browser_fonts "${_file}")
        endif()
    endforeach()
endif()
list(LENGTH _brulion_browser_fonts _count)
if(_count EQUAL 2)
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/browser-fonts)
    foreach(_file IN LISTS _brulion_browser_fonts)
        get_filename_component(_name "${_file}" NAME)
        configure_file("${_file}" "${_dir}/fonts/${_name}" COPYONLY)
    endforeach()
    file(WRITE ${_dir}/fonts.conf "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n  <dir>${_dir}/fonts</dir>\n  <cachedir>${_dir}/cache</cachedir>\n</fontconfig>\n")
    add_test(NAME tst_fallback_fonts_browser_fonts COMMAND tst_fallback_fonts)
    add_test(NAME tst_fallback_fonts_browser_fonts_when_missing COMMAND tst_fallback_fonts installWhenMissing)
    set_tests_properties(tst_fallback_fonts_browser_fonts tst_fallback_fonts_browser_fonts_when_missing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FONTCONFIG_FILE=${_dir}/fonts.conf;BRULION_TEST_BROWSER_FONTS=1")

    # A Linux system with an emoji font but nothing for ⧉ and the math symbols (DejaVu and Noto
    # Color Emoji, say): the Linux policy leaves the emoji to the system and adds Brulion Math as
    # an application font only. The bundled Brulion Emoji stands in for the system's emoji font.
    set(_dir ${CMAKE_CURRENT_BINARY_DIR}/system-emoji-font)
    foreach(_file IN LISTS _brulion_browser_fonts ITEMS ${PROJECT_SOURCE_DIR}/assets/fonts/BrulionEmoji.ttf)
        get_filename_component(_name "${_file}" NAME)
        configure_file("${_file}" "${_dir}/fonts/${_name}" COPYONLY)
    endforeach()
    file(WRITE ${_dir}/fonts.conf "<?xml version=\"1.0\"?>\n<!DOCTYPE fontconfig SYSTEM \"fonts.dtd\">\n"
        "<fontconfig>\n  <dir>${_dir}/fonts</dir>\n  <cachedir>${_dir}/cache</cachedir>\n</fontconfig>\n")
    add_test(NAME tst_fallback_fonts_system_emoji_when_missing COMMAND tst_fallback_fonts installWhenMissing)
    set_tests_properties(tst_fallback_fonts_system_emoji_when_missing PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;FONTCONFIG_FILE=${_dir}/fonts.conf;BRULION_TEST_SYSTEM_EMOJI=1")
else()
    message(STATUS "tst_fallback_fonts_browser_fonts, …_system_emoji_when_missing skipped: fc-match does not find "
        "DejaVu Sans and DejaVu Sans Mono")
endif()
