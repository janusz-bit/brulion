# drawing — drawings in notes (docs/SYNTAX.md §2 "Drawings"): the model of Excalidraw's elements
# (freehand lines, shapes, lines, arrows, text, pictures), the outline of a freehand line (a port of
# perfect-freehand, MIT), the geometry and hit tests of elements, their edits and undo diffs, the
# merge of two versions, the Excalidraw scene and its payload embedded in an SVG, the colours of
# the light and the dark theme (Excalidraw's dark mode), the SVG file Brulion writes, the Obsidian
# Excalidraw plugin's `*.excalidraw.md` files (LZ-String) and one API for every drawing file format.
# QtCore only; zlib through the vendored miniz.
if(NOT TARGET brulion_miniz)
    add_subdirectory(${PROJECT_SOURCE_DIR}/3rdparty/miniz ${CMAKE_BINARY_DIR}/3rdparty/miniz)
endif()

brulion_add_core_module(drawing
    SOURCES
        drawing.h drawing.cpp
        stroke.h stroke.cpp
        excalidraw.h excalidraw.cpp
        palette.h palette.cpp
        geometry.h geometry.cpp
        edit.h edit.cpp
        merge.h merge.cpp
        svgwrite.h svgwrite.cpp
        lzstring.h lzstring.cpp
        excalidrawmd.h excalidrawmd.cpp
        drawingfile.h drawingfile.cpp)
target_link_libraries(brulion_core_drawing PRIVATE brulion_miniz)
