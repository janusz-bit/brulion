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
    # Helpers shared by tests (tests/common: centraleuropeantime.h, filesystemtraits.h).
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests/common)
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

# brulion_test_profile(<var> <name>) — the app's own runs in a test (the QML self-tests, the
# journey) keep out of the user's settings and notebooks with a private home of XDG folders. On
# Windows these do not move QStandardPaths: there such a run uses a separate profile instead
# (BRULION_PROFILE=test-<name>, src/ipc/profile.h). Sets <var> to the environment entries to append
# to the test's ENVIRONMENT (";BRULION_PROFILE=…" on Windows, "" elsewhere) and <var>_DIRS to the
# profile's folders, for the fixture that removes them before the run.
function(brulion_test_profile var name)
    if(WIN32)
        file(TO_CMAKE_PATH "$ENV{APPDATA}" _roaming)
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _local)
        set(${var} ";BRULION_PROFILE=test-${name}" PARENT_SCOPE)
        set(${var}_DIRS "${_roaming}/Brulion-test-${name}" "${_local}/Brulion-test-${name}" PARENT_SCOPE)
    else()
        set(${var} "" PARENT_SCOPE)
        set(${var}_DIRS "" PARENT_SCOPE)
    endif()
endfunction()

add_subdirectory(core)
# The app's areas (src/app): design system helpers, the editor, the shell, the views, the QML.
if(BRULION_BUILD_APP)
    add_subdirectory(app)
endif()
# OS integration (src/platform): the pure parts of autostart and the Windows app identity.
if(TARGET brulion_platform)
    add_subdirectory(platform)
endif()
# The desktop's single-instance socket (src/ipc) and the command line brulion-cli (src/cli).
if(TARGET brulion_ipc)
    add_subdirectory(ipc)
endif()
if(TARGET brulion-cli)
    add_subdirectory(cli)
endif()
# Shell scripts from scripts/ (release.sh, appimage-update-check.sh); bash scripts for Linux, not
# run on Windows (tests/scripts/CMakeLists.txt).
add_subdirectory(scripts)

# The longest tests start first: ctest orders a parallel run by the timings of the earlier runs
# in Testing/Temporary/CTestCostData.txt, which a fresh build (CI) does not have, so it starts
# with ctest-costs.txt (seconds in the sanitizer build on a CI runner). Without it the tests run
# in the order they were added, and a long one that starts late runs alone at the end.
if(NOT EXISTS "${CMAKE_BINARY_DIR}/Testing/Temporary/CTestCostData.txt")
    configure_file(ctest-costs.txt "${CMAKE_BINARY_DIR}/Testing/Temporary/CTestCostData.txt" COPYONLY)
endif()
# QBENCHMARK suites (tests/bench, run with scripts/bench.sh); off by default so CI and Nix stay fast.
option(BRULION_BUILD_BENCHMARKS "Build the benchmark suites in tests/bench (scripts/bench.sh)" OFF)
if(BRULION_BUILD_BENCHMARKS AND BRULION_BUILD_APP)
    add_subdirectory(bench)
endif()
