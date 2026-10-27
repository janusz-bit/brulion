# Unit tests of the design system's C++ helpers (library brulion_ui_helpers, src/app/ui):
# fuzzy matching, line diffs and conflict hunks, the QML-facing dates API. GUI-free
# (QTEST_GUILESS_MAIN); the app executable is not needed.
brulion_add_test(tst_ui_helpers
    SOURCES tst_uihelpers.cpp
    LIBS brulion_ui_helpers)
# KeyboardInsets (brulion_ui_helpers): the on-screen keyboard's overlap of the window and the
# choice between a simulated keyboard, the platform's report and Qt's input method.
brulion_add_test(tst_keyboardinsets
    SOURCES tst_keyboardinsets.cpp
    LIBS brulion_ui_helpers)
# GestureInsets (brulion_ui_helpers): the system's gesture zones at the window's edges, the
# platform's report and a simulation over it.
brulion_add_test(tst_gestureinsets
    SOURCES tst_gestureinsets.cpp
    LIBS brulion_ui_helpers)
# ThemePalette (brulion_ui_helpers): the appearance settings' colours — the default look unchanged,
# contrast of every accent on every surface, malformed colours — and the font pickers' filter.
brulion_add_test(tst_ui_themepalette
    SOURCES tst_themepalette.cpp
    LIBS brulion_ui_helpers)

# App areas add their tests in tests/app/<area>/CMakeLists.txt (picked up automatically).
file(GLOB _brulion_app_test_dirs LIST_DIRECTORIES true RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*)
list(SORT _brulion_app_test_dirs)
foreach(_d IN LISTS _brulion_app_test_dirs)
    if(IS_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${_d} AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_d}/CMakeLists.txt)
        add_subdirectory(${_d})
    endif()
endforeach()
