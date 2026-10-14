# Tests of the calc module (brulion_core_calc).
foreach(_t numbers engine blocks pagescope table tableeval)
    brulion_add_test(tst_calc_${_t} SOURCES tst_calc_${_t}.cpp calctest.h LIBS brulion_core_calc)
endforeach()
