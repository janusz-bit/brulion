# The editor's AI assistant (brulion_core_writing): the requests for each action and preset, the
# model's answers cleaned (fences, preambles, quotes, list markers, a repeated end), where the
# result goes in the text (a pure edit, whitespace and line breaks kept) and the word diff.
brulion_add_test(tst_writing SOURCES tst_writing.cpp LIBS brulion_core_writing)
