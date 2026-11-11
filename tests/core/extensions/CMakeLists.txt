# Tests of the extensions module (brulion_core_extensions): the Luau engine, block edits, the
# notebook host and the built-in gym extension.
foreach(_t engine blockedit gym)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tst_extensions_${_t}.cpp)
        brulion_add_test(tst_extensions_${_t} SOURCES tst_extensions_${_t}.cpp LIBS brulion_core_extensions)
    endif()
endforeach()
