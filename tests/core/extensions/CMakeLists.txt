# Tests of the extensions module (brulion_core_extensions): the Luau engine, block edits, the
# notebook host (and its dry run), the built-in gym and AI assistant extensions, extensions written as pages of
# the notebook, and the documentation's Luau examples and API references.
foreach(_t engine blockedit gym assistant pages host docs)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tst_extensions_${_t}.cpp)
        brulion_add_test(tst_extensions_${_t} SOURCES tst_extensions_${_t}.cpp LIBS brulion_core_extensions)
    endif()
endforeach()
# The documentation is read where it is written: docs/EXTENSIONS.md, docs/SYNTAX.md and the
# documentation notebook's pages (src/app/shell/guide).
if(TARGET tst_extensions_docs)
    target_compile_definitions(tst_extensions_docs PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
endif()
# The assistant's requests are checked as the app makes them (ai::requestFrom, ai::httpRequest).
if(TARGET tst_extensions_assistant AND TARGET brulion_core_ai)
    target_link_libraries(tst_extensions_assistant PRIVATE brulion_core_ai)
endif()
