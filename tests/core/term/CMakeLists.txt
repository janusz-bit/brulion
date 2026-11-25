# The terminal emulator (src/core/term): the parser, the screen and the sequences' meaning
# (tst_term), the input encodings of keys, pastes, the mouse and focus (tst_term_keys), and 10 MB
# of random and of sequence-shaped bytes with resizes in between, which must neither crash nor hang
# nor break the screen's invariants (tst_term_fuzz, a fixed seed).
brulion_add_test(tst_term SOURCES tst_term.cpp LIBS brulion_core_term)
brulion_add_test(tst_term_keys SOURCES tst_term_keys.cpp LIBS brulion_core_term)
brulion_add_test(tst_term_fuzz SOURCES tst_term_fuzz.cpp LIBS brulion_core_term)
set_tests_properties(tst_term_fuzz PROPERTIES TIMEOUT 600)
