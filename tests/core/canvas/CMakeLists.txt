# The JSON Canvas file (canvas/canvas.h): Obsidian's, the spec's and other tools' files read and
# written back byte for byte, changed nodes with their keys in order, malformed and huge input.
brulion_add_test(tst_canvas SOURCES tst_canvas.cpp fixtures.h LIBS brulion_core_canvas)
# Edits and geometry (canvas/edit.h, canvas/geometry.h).
brulion_add_test(tst_canvas_edit SOURCES tst_canvas_edit.cpp fixtures.h LIBS brulion_core_canvas)
# Two versions merged (canvas/merge.h).
brulion_add_test(tst_canvas_merge SOURCES tst_canvas_merge.cpp fixtures.h LIBS brulion_core_canvas)
# What the index reads, rewriting on a rename, the SVG picture (canvas/indextext.h, canvas/svg.h).
brulion_add_test(tst_canvas_index SOURCES tst_canvas_index.cpp fixtures.h LIBS brulion_core_canvas)
