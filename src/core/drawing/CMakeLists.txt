# drawing — drawings in notes (docs/SYNTAX.md §2 "Drawings"): the model of freehand strokes, the
# outline of a stroke (a port of perfect-freehand, MIT), the Excalidraw scene and its payload
# embedded in an SVG, the colours of the light and the dark theme (Excalidraw's dark mode) and the
# SVG file Brulion writes. QtCore only; zlib through the vendored miniz.
if(NOT TARGET brulion_miniz)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/miniz ${CMAKE_BINARY_DIR}/3rdparty/miniz)
endif()

brulion_add_core_module(drawing
    SOURCES
        drawing.h drawing.cpp
        stroke.h stroke.cpp
        excalidraw.h excalidraw.cpp
        palette.h palette.cpp
        svgwrite.h svgwrite.cpp)
target_link_libraries(brulion_core_drawing PRIVATE brulion_miniz)
