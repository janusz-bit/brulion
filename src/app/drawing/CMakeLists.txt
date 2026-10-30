# brulion_drawing — the drawing editor's paper (docs/UI.md "Drawing editor"): DrawingCanvas, a
# QQuickPaintedItem that shows a drawing of the core module `drawing` and draws (stylus pressure,
# mouse, fingers), erases, selects with a lasso, moves, makes shapes and text (the tools:
# drawingcanvas_tools.cpp), undoes and moves around it; the painting it shares with the previews of
# drawings (drawingrender.h: the paper, the strokes in the light or the dark theme, Excalidraw's
# dark filter) and DrawingImageProvider, the QML image provider "drawing" of those previews (Qt
# SVG for files Brulion does not draw itself; the file's own size by the editor's picture size
# helper, brulion_editorhost's picturesize.h, as the embeds have it), and a drawing's backdrop (a
# picture or a PDF page under the ink: backdropimage.h prepares its raster, DrawingBackdrops is
# QML's way to it, PdfPageImageProvider the image provider "pdfpage" of the page picker's
# thumbnails; PDF pages through brulion_platform's pdfpages.h). A static library so
# tests/app/drawing links it without the app; the app's QML module registers the canvas and
# DrawingBackdrops through drawingqml.h and main.cpp the image providers.
qt_add_library(brulion_drawing STATIC
    drawingcanvas.h drawingcanvas.cpp drawingcanvas_tools.cpp
    drawingrender.h drawingrender.cpp
    drawingimageprovider.h drawingimageprovider.cpp
    backdropimage.h backdropimage.cpp
    backdrops.h backdrops.cpp
    pdfpageimageprovider.h pdfpageimageprovider.cpp
)
target_include_directories(brulion_drawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_drawing
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_core_drawing
    PRIVATE Qt6::Svg brulion_editorhost brulion_platform)
qt_extract_metatypes(brulion_drawing)
brulion_set_warnings(brulion_drawing)
