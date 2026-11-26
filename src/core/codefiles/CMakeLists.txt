# codefiles — a project's files for the "Code" mode (docs/CODE-FILES.md), GUI-free:
#   fuzzy.h      the file picker's fuzzy matcher over relative paths (base-name hits first,
#                consecutive runs, word and path-separator starts, case; Polish letters folded)
#   fileindex.h  FileIndex: the files and folders of a folder — from `git ls-files -z` or a bounded
#                walk of the folder —, bounded, sorted, queried by fuzzy match
#   search.h     the project search: rg --json and git grep argument lists and parsers, Brulion's
#                own search of a text, the matched ranges of a line
#   viewer.h     the file viewer's reading (bounded, binary files and pictures told apart), file
#                links of agents' answers, @-mentions of a thread's composer, review comments
#   highlight.h  syntax colours of code: a table-driven lexer per language (Luau: core syntax)
brulion_add_core_module(codefiles
    SOURCES
        fuzzy.h fuzzy.cpp
        fileindex.h fileindex.cpp
        search.h search.cpp
        viewer.h viewer.cpp
        highlight.h highlight.cpp highlight_languages.cpp highlight_p.h
    DEPS text syntax)
