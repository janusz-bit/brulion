# brulion_ui_helpers — the GUI-free C++ helpers of the design system (docs/UI.md): natural-language
# dates and recurrence for QML (DatesHelper), fuzzy matching (TextHelper), line diffs and
# conflict hunks (DiffHelper). A static library (QtCore + core modules only) so that
# tests/app links it without the app; the app's QML module "Brulion" registers the classes as
# QML singletons through QML_FOREIGN wrappers (src/app/ui/qmlforeign.h), which needs the
# metatypes extracted below. DevTools and DeviceInfo stay in the app (they need QtQuick/QtGui).
qt_add_library(brulion_ui_helpers STATIC
    dateshelper.h dateshelper.cpp
    diffhelper.h diffhelper.cpp
    texthelper.h texthelper.cpp
)
target_include_directories(brulion_ui_helpers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_ui_helpers PUBLIC Qt6::Core PRIVATE brulion_core_dates brulion_core_text)
qt_extract_metatypes(brulion_ui_helpers)
brulion_set_warnings(brulion_ui_helpers)
