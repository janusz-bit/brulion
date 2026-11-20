# Tests of the dataview module (brulion_core_dataview): parsing, expressions, functions, sources,
# data commands and results over a small Polish vault.
brulion_add_test(tst_dataview SOURCES tst_dataview.cpp LIBS brulion_core_dataview)
# Standalone expressions: view formulas (Dataview syntax) and Obsidian Bases' expressions.
brulion_add_test(tst_dataview_expression SOURCES tst_dataview_expression.cpp LIBS brulion_core_dataview)
