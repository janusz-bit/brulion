# Tests of the index module (brulion_core_index), one executable per area. fixture.h holds the
# shared sample vault.
foreach(_t internals resolve relocate vault search query consistency perf)
    brulion_add_test(tst_index_${_t} SOURCES tst_index_${_t}.cpp fixture.h LIBS brulion_core_index)
endforeach()
# 3000 synthetic notes: slower than the rest (seconds on a Raspberry Pi).
set_tests_properties(tst_index_perf PROPERTIES LABELS slow TIMEOUT 300)
