# Canvas area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): the canvas view of route "canvas" (docs/UI.md "Canvas view": JSON Canvas
# files, Obsidian's `.canvas`) — the brulion_canvasview library (canvas/CMakeLists.txt: CanvasEditor,
# the lists of cards and edges in view, CanvasCards, the image provider "canvas"), its QML
# registration (canvas/canvasqml.h) and qml/canvas (CanvasView, its cards, edges, tool bar,
# selection bar, text editor and dialog).
add_subdirectory(canvas)
target_link_libraries(brulion PRIVATE brulion_canvasview)
# The generated QML type registration includes the canvas headers by file name.
target_include_directories(brulion PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/canvas)
target_sources(brulion PRIVATE canvas/canvasqml.h)
qt_target_qml_sources(brulion
    QML_FILES
        qml/canvas/CanvasView.qml
        qml/canvas/CanvasCard.qml
        qml/canvas/CanvasCardRows.qml
        qml/canvas/CanvasEdge.qml
        qml/canvas/CanvasEdgeLabel.qml
        qml/canvas/CanvasToolbar.qml
        qml/canvas/CanvasSelectionBar.qml
        qml/canvas/CanvasTextEditor.qml
        qml/canvas/CanvasTextDialog.qml
)
