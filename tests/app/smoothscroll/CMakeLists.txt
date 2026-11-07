# SmoothScroll (src/app/ui/smoothscroll.h, library brulion_ui_smoothscroll): its plan of a smooth
# scroll (pure), and views scrolled by wheel events sent to an offscreen window the way the
# platform delivers them — a notch's distance however fast the wheel turns, no overshoot, the
# events it leaves to Qt (touchpad, Ctrl, a view at its end goes on to the one around it), Shift
# sideways and the scroll bar's paging (scrollBy).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_ui_smoothscroll AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_smooth_scroll
        SOURCES tst_smooth_scroll.cpp
        LIBS brulion_ui_smoothscroll Qt6::Qml)
    set_tests_properties(tst_smooth_scroll PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()
