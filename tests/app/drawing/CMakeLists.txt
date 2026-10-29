# The drawing editor's canvas (library brulion_drawing, src/app/drawing): stylus input through
# QWindowSystemInterface (a QPA header: Qt6::GuiPrivate, for this test only), a mouse, fingers,
# the wheel, undo, the SVG checked with Qt's SVG renderer, Excalidraw files, merging, painting in
# the light and the dark theme, the previews' image provider (also through a QML Image). Offscreen
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

# The drawing editor in the real app (qml-smoke.sh): the route on a drawing, a read-only Excalidraw
# drawing, a picture, a missing and a new file, the background menu, the embeds and the picture
# view, desktop and phone, light and dark; QML warnings from those files fail it, and so does any
# file written by only looking.
# Not on Windows: qml-smoke.sh is a bash script.
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT WIN32 AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    math(EXPR _drawing_qml_timeout "360 + 20 * ${BRULION_TEST_SLOWDOWN}")
    add_test(NAME tst_drawing_qml
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke
                ${PROJECT_SOURCE_DIR}/tests/data/drawing)
    set_tests_properties(tst_drawing_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT ${_drawing_qml_timeout})
endif()
