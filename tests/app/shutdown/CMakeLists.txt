# The app's QML gone before the engine and the application object at quit (src/app/ui/qmlshutdown.h,
# library brulion_ui_shutdown): windows whose views are still being built ahead (incubated by hand,
# step by step) quit at every step, in child processes of the test — with the release no step
# crashes and no QML item is left for the application object's destructor; without it Qt crashes
# at some step (QTBUG-87228's delegates, the crash at quit of 2026-11-20).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_shutdown AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_ui_qmlshutdown
        SOURCES tst_qmlshutdown.cpp
        LIBS brulion_ui_shutdown Qt6::Gui)
    set_tests_properties(tst_ui_qmlshutdown PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 300)
endif()
