# The UI benchmark (docs/UI.md "UI benchmark (Brulion vs Obsidian)"): the metric library and tool
# brulion-uimetrics (built with the tests, not installed), the layout dump's scene test, the bench's
# dry run and the Brulion-only lint guard (contrast and target sizes on 8 real shots).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
qt_add_library(brulion_uibench STATIC uimetrics.h uimetrics.cpp)
target_include_directories(brulion_uibench PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(brulion_uibench PUBLIC Qt6::Core Qt6::Gui PRIVATE brulion_ui_helpers)
brulion_set_warnings(brulion_uibench)
qt_add_executable(brulion-uimetrics uimetrics_main.cpp)
target_link_libraries(brulion-uimetrics PRIVATE brulion_uibench)
brulion_set_warnings(brulion-uimetrics)

# The metrics on synthetic shots drawn with QPainter, the dump parser, screens.json.
brulion_add_test(tst_uibench_metrics SOURCES tst_uibench_metrics.cpp LIBS brulion_uibench brulion_ui_helpers Qt6::Gui)
target_compile_definitions(tst_uibench_metrics PRIVATE
    BRULION_UIBENCH_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    BRULION_UIMETRICS_BIN="$<TARGET_FILE:brulion-uimetrics>")
add_dependencies(tst_uibench_metrics brulion-uimetrics)

# The layout dump (src/app/ui/layoutdump.h) on a QML scene: texts, rich-text fragments, targets,
# boxes, clipping, opacity, occlusion, a popup's dim, dpr 2.
if(TARGET brulion_ui_layoutdump AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Templates")
    brulion_add_test(tst_uibench_layoutdump SOURCES tst_uibench_layoutdump.cpp
        LIBS brulion_ui_layoutdump Qt6::Qml Qt6::Quick)
    set_tests_properties(tst_uibench_layoutdump PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=2;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
endif()

if(NOT WIN32)
    # The bench's commands for screens.json without running anything (no nix, no binaries; node
    # reads screens.json — skipped without it), and the Node scripts parse (dryrun-test.sh).
    add_test(NAME tst_uibench_bench_dryrun COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/dryrun-test.sh
        ${CMAKE_CURRENT_BINARY_DIR}/dryrun)
    set_tests_properties(tst_uibench_bench_dryrun PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
    if(TARGET brulion)
        # Brulion-only regression guard: 8 shots of the real app (offscreen, software), every
        # visible text readable (WCAG contrast) and every target large enough, unless
        # lint-allow.json excuses it.
        add_test(NAME tst_uibench_lint_qml COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/lint.sh
            $<TARGET_FILE:brulion> $<TARGET_FILE:brulion-uimetrics> ${CMAKE_CURRENT_BINARY_DIR}/lint)
        math(EXPR _uibench_lint_timeout "240 + 10 * ${BRULION_TEST_SLOWDOWN}")
        set_tests_properties(tst_uibench_lint_qml PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
            TIMEOUT ${_uibench_lint_timeout} PROCESSORS 2 LABELS slow
            FAIL_REGULAR_EXPRESSION "Binding loop detected")
    endif()
endif()
