brulion_add_test(tst_drawing SOURCES tst_drawing.cpp LIBS brulion_core_drawing)
# Shapes, lines, arrows, text and pictures: Excalidraw's elements, geometry, hit tests, edits, undo
# diffs, the SVG of them.
brulion_add_test(tst_drawing_elements SOURCES tst_drawing_elements.cpp LIBS brulion_core_drawing)
# Two versions of a drawing merged (drawing/merge.h).
brulion_add_test(tst_drawing_merge SOURCES tst_drawing_merge.cpp LIBS brulion_core_drawing)
