# A project's files for the "Code" mode (brulion_core_codefiles, docs/CODE-FILES.md).
#   tst_codefiles            the fuzzy matcher (data-driven: base-name hits first, runs, word and
#                            separator starts, case, Polish letters), FileIndex (git ls-files -z
#                            output, a walk with skipped folders, bounds, a generated tree of 50 000
#                            files timed), the viewer's reading, file links, @-mentions, review comments
#   tst_codefiles_search     rg --json and git grep argument lists and output (garbage, base64
#                            bytes, Polish letters, long lines), the matched ranges, Brulion's own
#                            search; with real rg and git where they are on PATH (skipped otherwise)
#   tst_codefiles_highlight  the syntax colours of every lexer, states carried over lines, garbage
brulion_add_test(tst_codefiles SOURCES tst_codefiles.cpp LIBS brulion_core_codefiles)
brulion_add_test(tst_codefiles_search SOURCES tst_codefiles_search.cpp LIBS brulion_core_codefiles)
brulion_add_test(tst_codefiles_highlight SOURCES tst_codefiles_highlight.cpp LIBS brulion_core_codefiles)
