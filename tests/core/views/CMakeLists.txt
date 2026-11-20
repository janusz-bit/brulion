# Tests of the views module (brulion_core_views): views of queries (columns, cells, groups, boards,
# calendars, summaries, formulas, new rows), their edits, and Obsidian Bases (fixtures/*.base).
brulion_add_test(tst_views SOURCES tst_views.cpp LIBS brulion_core_views)
brulion_add_test(tst_views_edits SOURCES tst_views_edits.cpp LIBS brulion_core_views)
brulion_add_test(tst_views_bases SOURCES tst_views_bases.cpp LIBS brulion_core_views)
target_compile_definitions(tst_views_bases PRIVATE BRULION_BASES_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
