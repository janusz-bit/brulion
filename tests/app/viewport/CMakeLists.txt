# Empty texts that stop observing the viewport (library brulion_ui_viewport, src/app/ui;
# docs/PERFORMANCE.md "Scrolling: empty texts no longer walked"): the texts of an offscreen window
# after a frame and after a look, rows made while a list scrolls, what a scroll step still walks
# down to, and the benchmark `scrollStep` (`-perf -perfcounter instructions:u`).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_viewport AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_viewport_observers
        SOURCES tst_viewport_observers.cpp
        LIBS brulion_ui_viewport Qt6::Qml Qt6::Quick)
    set_tests_properties(tst_viewport_observers PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 120)
endif()
