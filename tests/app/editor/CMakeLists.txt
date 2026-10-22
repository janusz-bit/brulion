# Block editor (src/app/editor, library brulion_editor): pure helpers, BlockListModel +
# PageController behaviour, suggestions, the Markdown highlighter. Run offscreen (QGuiApplication).
brulion_add_test(tst_editor_blocktext
    SOURCES tst_editor_blocktext.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
brulion_add_test(tst_editor_controller
    SOURCES tst_editor_controller.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
# Its 3000- and 5000-block cases take minutes under the sanitizers on CI runners (279 s of
# ctest's default 300 s before 0.2.0, then over it): scale the limit like the tests' own ones.
math(EXPR _controller_timeout "300 + 30 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_editor_controller PROPERTIES TIMEOUT ${_controller_timeout})
# Calculations across edits: definitions above, sum(above), calc fences, frontmatter, and random
# edits checked against a model computing everything from scratch (BlockListModel keeps results
# across calc-neutral edits).
brulion_add_test(tst_editor_calc
    SOURCES tst_editor_calc.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
brulion_add_test(tst_editor_suggestions
    SOURCES tst_editor_suggestions.cpp
    LIBS brulion_editor)
brulion_add_test(tst_editor_highlighter
    SOURCES tst_editor_highlighter.cpp
    LIBS brulion_editor)
brulion_add_test(tst_editor_queryresults
    SOURCES tst_editor_queryresults.cpp
    LIBS brulion_editor)
brulion_add_test(tst_editor_notemode
    SOURCES tst_editor_notemode.cpp
    LIBS brulion_editor brulion_core_markdown)
# The size of embedded pictures read from their headers / an SVG's root (picturesize.h).
brulion_add_test(tst_editor_picturesize
    SOURCES tst_editor_picturesize.cpp
    LIBS brulion_editorhost Qt6::Gui)

# The block editor in its real QML (the playground, `brulion --editor --editor-script selftest`):
# keyboard flows (Enter, Backspace, Tab, Up/Down, Alt+Up/Down, Ctrl+Z, Ctrl+Enter, Esc,
# completions, folding, source mode, paste) through the actual delegates, with real key and
# input-method events (EditorInput: preedit compositions, no-break spaces, typing into a block
# scrolled out of view, table cells, a dialog while the page changes); exit code = failures.
# The uninstalled app finds Qt's QML modules (QtQuick.Controls…) through QML_IMPORT_PATH: Qt's
# QML directory next to the Qt6Qml package (in Nix builds QtQml lives in its own prefix, which
# an unwrapped binary does not know). Skipped when that directory cannot be found, and without
# the developer QML (BRULION_DEV_QML, src/app/CMakeLists.txt), which has the playground.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
# tst_editor_suggestions' ListView test imports QtQuick: give it Qt's QML directory, as below, so
# that it does not depend on the caller's environment (it skips when QtQuick cannot be imported).
if(EXISTS "${_brulion_qt_qml_root}/QtQuick")
    set_tests_properties(tst_editor_suggestions PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QML_IMPORT_PATH=${_brulion_qt_qml_root}")
endif()
if(TARGET brulion AND BRULION_DEV_QML_ENABLED AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    add_test(NAME tst_editor_qml
        COMMAND brulion --editor --editor-script selftest --no-single-instance
            --size 1280x800 --now "2026-10-15 10:00" --locale pl)
    # A TypeError means QML code ran on something already destroyed (deferred work of a gone
    # editor): a failure even when every step passed; so is a binding loop (the touch toolbar's
    # Loader had one).
    set_tests_properties(tst_editor_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 90 FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected")
    # The round clock (BTimePicker, BTimePickerDialog) and the clock buttons of the reminder's date
    # picker and the calendar's add dialog (`--editor-script clock`), with real mouse or touch input
    # (QtTest's TestEvent): on a desktop, a phone, and a phone held sideways (the dial has to fit).
    if(EXISTS "${_brulion_qt_qml_root}/QtTest")
        foreach(_clock "tst_editor_qml_clock;1280x800;" "tst_editor_qml_clock_phone;412x915;--touch"
                       "tst_editor_qml_clock_landscape;915x412;--touch")
            list(GET _clock 0 _clock_test)
            list(GET _clock 1 _clock_size)
            list(GET _clock 2 _clock_touch)
            add_test(NAME ${_clock_test}
                COMMAND brulion --editor --editor-script clock --no-single-instance
                    --size ${_clock_size} ${_clock_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_clock_test} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
                TIMEOUT 90 FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected")
        endforeach()
    endif()
    # The task status menu and commands (`--editor-script taskstatus`): right-clicks with the mouse
    # on a desktop, long presses with a finger on a phone (QtTest's TestEvent), menu keys, the "/"
    # commands typed with real keys, the touch toolbar's task button held down.
    if(EXISTS "${_brulion_qt_qml_root}/QtTest")
        foreach(_status "tst_editor_qml_taskstatus;1280x800;" "tst_editor_qml_taskstatus_touch;412x915;--touch")
            list(GET _status 0 _status_test)
            list(GET _status 1 _status_size)
            list(GET _status 2 _status_touch)
            add_test(NAME ${_status_test}
                COMMAND brulion --editor --editor-script taskstatus --no-single-instance
                    --size ${_status_size} ${_status_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_status_test} PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
                TIMEOUT 90 FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
    endif()
endif()
