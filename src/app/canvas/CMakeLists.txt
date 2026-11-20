# brulion_canvasview — the canvas view (docs/UI.md "Canvas view"): CanvasEditor, the board of a
# JSON Canvas file of the core module `canvas` (its undo, the view onto it, the selection, every
# pointer gesture: canvaseditor_input.cpp), the lists of the cards and edges in view
# (canvasmodels.h), CanvasCards — what a card shows, rendered by the editor's BlockListModel — and
# CanvasImageProvider, the QML image provider "canvas" of canvases' pictures (Qt SVG). A static
# library so tests/app/canvas links it without the app; the app's QML module registers it through
# canvasqml.h and main.cpp the image provider.
qt_add_library(brulion_canvasview STATIC
    canvaseditor.h canvaseditor.cpp canvaseditor_input.cpp
    canvasmodels.h canvasmodels.cpp
    canvascards.h canvascards.cpp
    canvasimageprovider.h canvasimageprovider.cpp
)
target_include_directories(brulion_canvasview PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_canvasview
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_core_canvas brulion_editorhost
    PRIVATE Qt6::Svg brulion_editor)
qt_extract_metatypes(brulion_canvasview)
brulion_set_warnings(brulion_canvasview)
