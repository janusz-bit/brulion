# brulion_drawing — the drawing editor's paper (docs/UI.md "Drawing editor"): DrawingCanvas, a
# QQuickPaintedItem that shows a drawing of the core module `drawing` and draws (stylus pressure,
# mouse, fingers), erases, undoes and moves around it; the painting it shares with the previews of
# drawings (drawingrender.h: the paper, the strokes in the light or the dark theme, Excalidraw's
# dark filter) and DrawingImageProvider, the QML image provider "drawing" of those previews (Qt
# SVG for files Brulion does not draw itself; the file's own size by the editor's picture size
# helper, brulion_editorhost's picturesize.h, as the embeds have it). A static library so
# tests/app/drawing links it without the app; the app's QML module registers the canvas through
# drawingqml.h and main.cpp the image provider.
qt_add_library(brulion_drawing STATIC
    drawingcanvas.h drawingcanvas.cpp
    drawingrender.h drawingrender.cpp
    drawingimageprovider.h drawingimageprovider.cpp
)
target_include_directories(brulion_drawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_drawing
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_core_drawing
    PRIVATE Qt6::Svg brulion_editorhost)
qt_extract_metatypes(brulion_drawing)
brulion_set_warnings(brulion_drawing)
