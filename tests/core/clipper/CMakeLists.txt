# The clipper module (src/core/clipper, docs/CLIPPER.md): native messaging frames (pieces, limits,
# malformed input), clip requests (hostile values refused, paths), templates (YAML-safe values, left-out
# lines, the built-ins), articles as Markdown (fixtures of real pages: figures, code, tables, relative
# addresses, malicious HTML, Polish text), the pictures' download against a server on 127.0.0.1 (limits,
# private hosts, data: pictures) and the host manifests and their places for every browser and system.
brulion_add_test(tst_clipper SOURCES tst_clipper.cpp LIBS brulion_core_clipper Qt6::Network)
target_compile_definitions(tst_clipper PRIVATE BRULION_CLIPPER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The browser extension (clipper/): its scripts parse and its unit tests pass under Node (skipped
# without node). The extension in a real Chromium: tests/web/clipper.spec.mjs (scripts/web-test.sh --clipper).
if(NOT WIN32)
    add_test(NAME tst_clipper_extension
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/extension-test.sh ${PROJECT_SOURCE_DIR}/clipper)
    set_tests_properties(tst_clipper_extension PROPERTIES TIMEOUT 120 SKIP_RETURN_CODE 77)
endif()
