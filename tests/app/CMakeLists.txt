# Unit tests of the design system's C++ helpers (library brulion_ui_helpers, src/app/ui):
# fuzzy matching, line diffs and conflict hunks, the QML-facing dates API. GUI-free
# (QTEST_GUILESS_MAIN); the app executable is not needed.
brulion_add_test(tst_ui_helpers
    SOURCES tst_uihelpers.cpp
    LIBS brulion_ui_helpers)
