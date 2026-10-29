brulion_add_test(tst_drawing SOURCES tst_drawing.cpp LIBS brulion_core_drawing)
# Shapes, lines, arrows, text and pictures: Excalidraw's elements, geometry, hit tests, edits, undo
# diffs, the SVG of them.
brulion_add_test(tst_drawing_elements SOURCES tst_drawing_elements.cpp LIBS brulion_core_drawing)
# Two versions of a drawing merged (drawing/merge.h).
brulion_add_test(tst_drawing_merge SOURCES tst_drawing_merge.cpp LIBS brulion_core_drawing)
# The Obsidian Excalidraw plugin's files (drawing/excalidrawmd.h), LZ-String against lz-string's
# own vectors (tests/data/drawing/lzstring), the one API of drawing files (drawing/drawingfile.h).
brulion_add_test(tst_drawing_exmd SOURCES tst_drawing_exmd.cpp LIBS brulion_core_drawing)
