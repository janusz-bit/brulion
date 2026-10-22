# brulion_add_test(<name> SOURCES ... [LIBS ...]) — one QtTest executable per test file.
# Link only the module(s) under test (e.g. LIBS brulion_core_dates) so a broken module never
# blocks other modules' tests.
# Sanitizer builds (CI: ASan + UBSan) run many times slower, with larger stack frames: tests
# multiply their wall-clock limits and worker-thread stack sizes by BRULION_TEST_SLOWDOWN.
if(CMAKE_CXX_FLAGS MATCHES "-fsanitize=")
    set(BRULION_TEST_SLOWDOWN 25)
else()
    set(BRULION_TEST_SLOWDOWN 1)
endif()

function(brulion_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_compile_definitions(${name} PRIVATE BRULION_TEST_DATA="${PROJECT_SOURCE_DIR}/tests/data"
                                                BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN})
    brulion_set_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # QtTest stops a single test function after QTEST_FUNCTION_TIMEOUT (300 s by default) on its
    # own, before ctest's TIMEOUT: scale it too, so only ctest's limit applies.
    math(EXPR _function_timeout_ms "300000 * ${BRULION_TEST_SLOWDOWN}")
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QTEST_FUNCTION_TIMEOUT=${_function_timeout_ms}")
endfunction()

add_subdirectory(core)
# Design system helpers of the app (src/app/ui): fuzzy matching, diffs, dates for QML.
add_subdirectory(app)
# Shell scripts from scripts/ (release.sh, appimage-update-check.sh).
add_subdirectory(scripts)
# QBENCHMARK suites (tests/bench, run with scripts/bench.sh); off by default so CI and Nix stay fast.
option(BRULION_BUILD_BENCHMARKS "Build the benchmark suites in tests/bench (scripts/bench.sh)" OFF)
if(BRULION_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()
