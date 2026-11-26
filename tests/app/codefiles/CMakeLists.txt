# The "Code" mode's project files (library brulion_codefiles, src/app/codefiles; docs/CODE-FILES.md):
# FileIndexCache and CodeFileIndex over a real git repository (skipped without git) and a plain
# folder — built off the GUI thread, shared, refreshed, the rows and their marks, recent files, the
# queries of a big index on a pool thread —; CodeFileDocument (lines, colours as escaped StyledText,
# tabs, pictures, binary and missing files, paths leaving the folder); CodeSearch with git grep,
# ripgrep (where it is on PATH) and Brulion's own search, cancelled, bounded; CodeFiles' links of
# answers, @-mentions and review comments.
brulion_add_test(tst_codefiles_app SOURCES tst_codefiles_app.cpp LIBS brulion_codefiles Qt6::Gui)
set_tests_properties(tst_codefiles_app PROPERTIES TIMEOUT 300)
