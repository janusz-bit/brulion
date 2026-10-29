# Drawing area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): the drawing editor of route "drawing" (docs/UI.md "Drawing editor") — the
# brulion_drawing library (drawing/CMakeLists.txt: DrawingCanvas), its QML registration
# (drawing/drawingqml.h: DrawingCanvas, DrawingFonts) and qml/drawing (DrawingView, the view of the
# route; its tool bar, the selection's bar, the text editor).
add_subdirectory(drawing)
target_link_libraries(brulion PRIVATE brulion_drawing)
# The generated QML type registration includes the drawing headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/drawing)
target_sources(brulion PRIVATE drawing/drawingqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/drawing/DrawingView.qml
        qml/drawing/DrawingToolbar.qml
        qml/drawing/DrawingSelectionBar.qml
        qml/drawing/DrawingTextEditor.qml
)
