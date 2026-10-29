# Tests of the index module (brulion_core_index), one executable per area. fixture.h holds the
# shared sample vault.
foreach(_t internals resolve relocate vault search query consistency properties perf)
    brulion_add_test(tst_index_${_t} SOURCES tst_index_${_t}.cpp fixture.h LIBS brulion_core_index)
endforeach()
# The index cache's codec: round trips of the frozen corpus and of the guide's live notes.
brulion_add_test(tst_index_cache SOURCES tst_index_cache.cpp LIBS brulion_core_index)
target_compile_definitions(tst_index_cache PRIVATE BRULION_GUIDE_DIR="${PROJECT_SOURCE_DIR}/src/app/shell/guide")
# 3000 synthetic notes: slower than the rest (seconds on a Raspberry Pi).
set_tests_properties(tst_index_perf PROPERTIES LABELS slow TIMEOUT 300)
