# TouchFlick (src/app/ui/touchflick.h, library brulion_ui_touch): its velocity estimate, and lists
# flicked with touch input delivered the way the Android QPA does — QWindowSystemInterface (a
# QPA header: Qt6::GuiPrivate, for this test only), a touch screen without velocities, explicit
# timestamps — offscreen: Qt's fling at 0.7x the finger and the jump of a drag started during a
# fling, with and without TouchFlick. Deterministic (flings are measured by where they end).
# Touch compression off: Qt Quick would otherwise deliver each move with the next frame, merged
# with the others that came since the last one. tst_touch_flick_compressed runs the drag that
# catches a fling with compression (Qt Quick's default, so the app's): 1 to 3 moves per frame.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 QUIET COMPONENTS GuiPrivate)
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET Qt6::GuiPrivate AND TARGET brulion_ui_touch AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_touch_flick
        SOURCES tst_touch_flick.cpp
        LIBS brulion_ui_touch Qt6::Qml Qt6::GuiPrivate)
    set_tests_properties(tst_touch_flick PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_NO_TOUCH_COMPRESSION=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
    add_test(NAME tst_touch_flick_compressed COMMAND tst_touch_flick chainedDragCompressed)
    set_tests_properties(tst_touch_flick_compressed PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()
