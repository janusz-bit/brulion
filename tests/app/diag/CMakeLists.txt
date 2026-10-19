# Tests of the diagnostics area (library brulion_diag, src/app/diag): the event log page's model
# (filters, diacritic-insensitive search, following new entries, repeats, clearing) and the
# export (header, file name, clipboard cap, redaction through the whole path).
brulion_add_test(tst_diag_logmodel SOURCES tst_diag_logmodel.cpp LIBS brulion_diag brulion_core_log)
brulion_add_test(tst_diag_logexport SOURCES tst_diag_logexport.cpp LIBS brulion_diag brulion_core_log)
# The Polish catalog as the app build compiles it (the header test skips without it).
target_compile_definitions(tst_diag_logexport PRIVATE BRULION_TEST_PL_QM="${CMAKE_BINARY_DIR}/src/app/brulion_pl.qm")
