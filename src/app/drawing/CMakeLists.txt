# brulion_drawing — the drawing editor's paper (docs/UI.md "Drawing editor"): DrawingCanvas, a
# QQuickPaintedItem that shows a drawing of the core module `drawing` and draws (stylus pressure,
# mouse, fingers), erases, undoes and moves around it. A static library so tests/app/drawing links
# it without the app; the app's QML module registers it through drawingqml.h.
qt_add_library(brulion_drawing STATIC
    drawingcanvas.h drawingcanvas.cpp
)
target_include_directories(brulion_drawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_drawing
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_core_drawing)
qt_extract_metatypes(brulion_drawing)
brulion_set_warnings(brulion_drawing)
