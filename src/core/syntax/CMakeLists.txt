# syntax — syntax colours of code in notes: a lexer of Luau (the extensions' language, docs/EXTENSIONS.md)
# that splits a line into coloured spans with the state a line leaves open (long strings and comments);
# the editor's MarkdownHighlighter paints them in ```luau blocks and in the source mode.
brulion_add_core_module(syntax SOURCES luau.h luau.cpp)
