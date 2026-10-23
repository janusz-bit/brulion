# Benchmarks of the core modules (QtTest QBENCHMARK, one executable per area). Built only with
# -DBRULION_BUILD_BENCHMARKS=ON (tests/CMakeLists.txt); scripts/bench.sh configures a Release
# build dir, builds the target `brulion_benchmarks`, runs every suite N times with `-csv` and
# summarises / compares the results. benchdata.h holds the shared generators.
# Registered with ctest under the label "bench" (`ctest -L bench`; `ctest -LE bench` skips them).
add_custom_target(brulion_benchmarks)

# The hash of the sources above, recomputed on every build (sourcehash.cmake): a content change
# rebuilds the suites that use the file even when its mtime is older than the build, and each
# binary carries the hash, which scripts/bench.sh checks against the source tree.
set(_bench_gen ${CMAKE_CURRENT_BINARY_DIR}/sourcehash)
file(GLOB _bench_sources RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_SOURCE_DIR}/*.cmake
     ${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt)
set(_bench_stamps "")
foreach(_f IN LISTS _bench_sources)
    list(APPEND _bench_stamps ${_bench_gen}/stamps/${_f}.sha256)
endforeach()
set(_bench_hash_args -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR} -DOUTPUT_DIR=${_bench_gen}
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/sourcehash.cmake)
# Once at configure time, so the generated files exist before the first build.
execute_process(COMMAND ${CMAKE_COMMAND} ${_bench_hash_args} COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(brulion_bench_source_hash
    COMMAND ${CMAKE_COMMAND} ${_bench_hash_args}
    BYPRODUCTS ${_bench_gen}/benchsourcehash.cpp ${_bench_stamps}
    COMMENT "Hashing the benchmark sources"
    VERBATIM)

function(brulion_add_bench name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBS" ${ARGN})
    qt_add_executable(${name} ${ARG_SOURCES} benchdata.h ${_bench_gen}/benchsourcehash.cpp)
    add_dependencies(${name} brulion_bench_source_hash)
    foreach(_src IN LISTS ARG_SOURCES)
        set_property(SOURCE ${_src} APPEND PROPERTY OBJECT_DEPENDS
                     ${_bench_gen}/stamps/${_src}.sha256 ${_bench_gen}/stamps/benchdata.h.sha256)
    endforeach()
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBS})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(${name} PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    brulion_set_warnings(${name})
    add_dependencies(brulion_benchmarks ${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES LABELS bench ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

brulion_add_bench(bench_text SOURCES bench_text.cpp LIBS brulion_core_text)
brulion_add_bench(bench_dates SOURCES bench_dates.cpp LIBS brulion_core_dates)
brulion_add_bench(bench_calc SOURCES bench_calc.cpp LIBS brulion_core_calc)
brulion_add_bench(bench_markdown SOURCES bench_markdown.cpp LIBS brulion_core_markdown)
brulion_add_bench(bench_index SOURCES bench_index.cpp LIBS brulion_core_index brulion_core_vault brulion_core_notebook)
brulion_add_bench(bench_vault SOURCES bench_vault.cpp LIBS brulion_core_vault)
# The in-memory WebDAV server of the sync tests (tests/core/sync) drives SyncEngine.
brulion_add_bench(bench_sync SOURCES bench_sync.cpp LIBS brulion_sync_testsupport)
brulion_add_bench(bench_reminders SOURCES bench_reminders.cpp LIBS brulion_core_reminders)
brulion_add_bench(bench_io SOURCES bench_io.cpp LIBS brulion_core_io)
