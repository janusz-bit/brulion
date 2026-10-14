# brulion_add_test(<name> SOURCES ... [LIBS ...]) — one QtTest executable per test file.
# Link only the module(s) under test (e.g. LIBS brulion_core_dates) so a broken module never
# blocks other modules' tests.
function(brulion_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE BRULION_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data")
    brulion_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

add_subdirectory(core)
