# Tests of the extensions module (brulion_core_extensions): the Luau engine, block edits, the
# notebook host, the built-in gym and AI assistant extensions and extensions written as pages of
# the notebook.
foreach(_t engine blockedit gym assistant pages)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tst_extensions_${_t}.cpp)
        brulion_add_test(tst_extensions_${_t} SOURCES tst_extensions_${_t}.cpp LIBS brulion_core_extensions)
    endif()
endforeach()
# The assistant's requests are checked as the app makes them (ai::requestFrom, ai::httpRequest).
if(TARGET tst_extensions_assistant AND TARGET brulion_core_ai)
    target_link_libraries(tst_extensions_assistant PRIVATE brulion_core_ai)
endif()
