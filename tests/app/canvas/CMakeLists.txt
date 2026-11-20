# The canvas view's board (library brulion_canvasview, src/app/canvas): a JSON Canvas file loaded,
# written back byte for byte where nothing changed, every edit and its undo, the view (fit, zoom at
# a point, the cards in view), mouse gestures in a window (select, rubber band, move, resize,
# connect from a side, onto empty canvas), the wheel, external versions merged, pastes, the
# pictures of canvases (CanvasImageProvider). Offscreen with the software scene graph.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion_canvasview AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_canvas_editor
        SOURCES tst_canvaseditor.cpp ../../core/canvas/fixtures.h
        LIBS brulion_canvasview Qt6::Quick)
    target_include_directories(tst_canvas_editor PRIVATE ${PROJECT_SOURCE_DIR}/tests/core/canvas)
    set_tests_properties(tst_canvas_editor PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 120)
endif()
