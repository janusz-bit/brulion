# Scrolling without jumps (scroll-jumps.sh): the journal stream (the sample notebook plus generated
# days), the guide page, Tasks and the calendar agenda scrolled down and up in the real app with
# touchpad deltas (desktop size) and touch drags (phone size), mostly with the list's cache off (every row created at the edge of
# the view, so a row that grows after it was placed fails every run, not only on a loaded machine);
# the scroll probe (`--probe-check`, src/app/ui/scrollprobe.h) fails a run when the content moves
# more than the input explains or shifts inside the list, or the thumb of the list's row scroll bar
# jumps (or the list has none, --probe-row-bar). Geometry only, no timing thresholds: the
# sanitizer build runs two cases with more time and only scrolls less far (BRULION_TEST_SLOWDOWN).
# Alone (RUN_SERIAL): other GUI tests next to it would only make it slower (the other GUI tests run
# side by side, two cores each). Every app run has its
# own time limit inside the script, the test a limit for all of them.
# The measurement scripts next to it (scroll-probe.sh, perf-record.sh, android-scroll.sh) are run
# by hand (docs/UI.md "Scrolling performance"). Not on Windows: scroll-jumps.sh is a bash script.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND BRULION_SCROLL_PROBE AND NOT ANDROID AND NOT EMSCRIPTEN AND NOT WIN32
   AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    add_test(NAME tst_scroll_jumps_qml
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scroll-jumps.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/scroll-jumps)
    # ~3 min on a laptop; a slow machine scrolls less far in the same bounded runs.
    if(BRULION_TEST_SLOWDOWN GREATER 1)
        set(_scroll_timeout 900)
    else()
        set(_scroll_timeout 600)
    endif()
    set_tests_properties(tst_scroll_jumps_qml PROPERTIES
        ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT ${_scroll_timeout} RUN_SERIAL TRUE LABELS slow)
endif()
