# The drawing editor's canvas (library brulion_drawing, src/app/drawing): stylus input through
# QWindowSystemInterface (a QPA header: Qt6::GuiPrivate, for this test only), a mouse, fingers,
# the wheel, undo, the SVG checked with Qt's SVG renderer, Excalidraw files, merging. Offscreen
# with the software scene graph.
set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 QUIET COMPONENTS GuiPrivate Svg)
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_drawing AND TARGET Qt6::GuiPrivate AND TARGET Qt6::Svg AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_drawingcanvas
        SOURCES tst_drawingcanvas.cpp
        LIBS brulion_drawing Qt6::Qml Qt6::Svg Qt6::GuiPrivate)
    set_tests_properties(tst_drawingcanvas PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_NO_TOUCH_COMPRESSION=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()
