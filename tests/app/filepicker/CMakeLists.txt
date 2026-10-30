# FilePicker (src/app/ui/filepicker.h, library brulion_ui_files): the system's file picker for QML
# instead of QtQuick.Dialogs (docs/PERFORMANCE.md L9) — its helpers, its life with fake backends,
# the parent window, the QML API, the fallback's palette from ThemePalette's colours
# (brulion_ui_helpers) and, in desktop builds (a QApplication), Qt Widgets' QFileDialog fallback
# driven offscreen. Its own main(): no platform theme of the developer's desktop.
if(TARGET brulion_ui_files)
    brulion_add_test(tst_filepicker
        SOURCES tst_filepicker.cpp
        LIBS brulion_ui_files brulion_ui_helpers Qt6::Qml Qt6::Quick)
    if(BRULION_DESKTOP)
        find_package(Qt6 REQUIRED COMPONENTS Widgets)
        target_link_libraries(tst_filepicker PRIVATE Qt6::Widgets)
    endif()
    set_tests_properties(tst_filepicker PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
        TIMEOUT 120)
endif()
