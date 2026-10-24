# Block editor (src/app/editor, library brulion_editor): pure helpers, BlockListModel +
# PageController behaviour, suggestions, the Markdown highlighter. Run offscreen (QGuiApplication).
brulion_add_test(tst_editor_blocktext
    SOURCES tst_editor_blocktext.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
# (brulion_core_notebook: the links `brulion-cli block link` gives, compared with the copied ones.)
brulion_add_test(tst_editor_controller
    SOURCES tst_editor_controller.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text brulion_core_notebook)
# Its 3000- and 5000-block cases take minutes under the sanitizers on CI runners (279 s of
# ctest's default 300 s before 0.2.0, then over it): scale the limit like the tests' own ones.
# (QtTest's own per-function limit scales in brulion_add_test: the 3000-block undo case alone
# once ran past its default 300 s.)
math(EXPR _controller_timeout "300 + 30 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_editor_controller PROPERTIES TIMEOUT ${_controller_timeout})
# Calculations across edits: definitions above, sum(above), calc fences, frontmatter, and random
# edits checked against a model computing everything from scratch (BlockListModel keeps results
# across calc-neutral edits).
brulion_add_test(tst_editor_calc
    SOURCES tst_editor_calc.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
# The block selection: ranges, Shift+arrows, copy / cut / paste with the real clipboard, range
# operations and their undo steps, ids on paste, HTML from other apps.
brulion_add_test(tst_editor_selection
    SOURCES tst_editor_selection.cpp
    LIBS brulion_editor brulion_core_markdown)
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
    # The playground's waits for something asynchronous last BRULION_TEST_SLOWDOWN times longer
    # (DevTools.testSlowdown; 25 in sanitizer builds), and so may a run whose waits fail.
    set(_playground_env "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}")
    math(EXPR _playground_timeout "90 + 10 * ${BRULION_TEST_SLOWDOWN}")
    math(EXPR _playground_timeout_long "120 + 10 * ${BRULION_TEST_SLOWDOWN}")
    add_test(NAME tst_editor_qml
        COMMAND brulion --editor --editor-script selftest --no-single-instance
            --size 1280x800 --now "2026-10-15 10:00" --locale pl)
    # A TypeError means QML code ran on something already destroyed (deferred work of a gone
    # editor): a failure even when every step passed; so is a binding loop (the touch toolbar's
    # Loader had one).
    set_tests_properties(tst_editor_qml PROPERTIES
        ENVIRONMENT "${_playground_env}"
        TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected")
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
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected")
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
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
        # The menu of date chips (`--editor-script datechips`): right-clicks with the mouse on a
        # desktop, long presses with a finger on a phone on 📅 and ⏰ chips, its items tapped.
        foreach(_chips "tst_editor_qml_datechips;1280x800;" "tst_editor_qml_datechips_touch;412x915;--touch")
            list(GET _chips 0 _chips_test)
            list(GET _chips 1 _chips_size)
            list(GET _chips 2 _chips_touch)
            add_test(NAME ${_chips_test}
                COMMAND brulion --editor --editor-script datechips --no-single-instance
                    --size ${_chips_size} ${_chips_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_chips_test} PROPERTIES
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
        # Links to blocks and headings (`--editor-script blocklinks`): the block menu's "Kopiuj
        # odnośnik" from right-clicks on text and frames (desktop; also a title drawn above the
        # page's properties), a bullet's right-click / long press and the touch toolbar's "Więcej"
        # (a block's — on a phone's keyboard, below a status bar —, a table cell's), the clipboard
        # read back, Ctrl+Shift+C (also in a cell),
        # following the copied links (scroll, select, flash, again after scrolling away), a missing
        # heading.
        foreach(_links "tst_editor_qml_blocklinks;1280x800;" "tst_editor_qml_blocklinks_touch;412x915;--touch")
            list(GET _links 0 _links_test)
            list(GET _links 1 _links_size)
            list(GET _links 2 _links_touch)
            add_test(NAME ${_links_test}
                COMMAND brulion --editor --editor-script blocklinks --no-single-instance
                    --size ${_links_size} ${_links_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_links_test} PROPERTIES
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
        # Selecting and copying blocks (`--editor-script selectiontest`, qml/dev/SelectionSelfTest.qml):
        # keys (Esc, Shift+arrows also from an edited block, Ctrl+A twice, Ctrl+C / X / V, Delete,
        # Ctrl+Z, Alt+arrows, Tab), Shift+click, mouse drags (a document's paragraphs, the view's
        # edge scrolling), the block and text menus, the floating tools, in-block copies keeping
        # no-break spaces; with a finger (--touch): a long press, taps, the grips, a long press
        # dragged to the edge, Back, the toolbar's and the bullet menu's "Zaznacz". The clipboard is
        # read back.
        foreach(_sel "tst_editor_qml_selection;1280x800;" "tst_editor_qml_selection_touch;412x915;--touch")
            list(GET _sel 0 _sel_test)
            list(GET _sel 1 _sel_size)
            list(GET _sel 2 _sel_touch)
            add_test(NAME ${_sel_test}
                COMMAND brulion --editor --editor-script selectiontest --no-single-instance
                    --size ${_sel_size} ${_sel_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_sel_test} PROPERTIES
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout_long} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
        # Views following what they depend on (`--editor-script refreshtest`,
        # qml/dev/RefreshSelfTest.qml): a query's results replaced by as many rows of another
        # height, an embedded section changed in its own note, a date picker's today marker after
        # midnight, the reminder dialog after a language change, a calculation's results after the
        # window narrowed (also in a phone's touch layout). Each binding once read what it follows
        # with a bare `void x`, which qmlcachegen compiles away with the dependency.
        foreach(_refresh "tst_editor_qml_refresh;1280x800;" "tst_editor_qml_refresh_touch;412x915;--touch")
            list(GET _refresh 0 _refresh_test)
            list(GET _refresh 1 _refresh_size)
            list(GET _refresh 2 _refresh_touch)
            add_test(NAME ${_refresh_test}
                COMMAND brulion --editor --editor-script refreshtest --no-single-instance
                    --size ${_refresh_size} ${_refresh_touch} --now "2026-10-15 10:00" --locale pl)
            set_tests_properties(${_refresh_test} PROPERTIES
                ENVIRONMENT "${_playground_env}"
                TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
        endforeach()
        # The touch toolbar docked on a simulated on-screen keyboard (`--editor-script keyboard`,
        # KeyboardInsets): a block in an outline and a document, a keyboard sliding in frame by
        # frame, the system hiding it while editing goes on, the caret, autocomplete, source mode
        # and a table cell above the toolbar, its buttons tapped with a finger.
        add_test(NAME tst_editor_qml_keyboard_touch
            COMMAND brulion --editor --editor-script keyboard --no-single-instance
                --size 412x915 --touch --keyboard 0 --now "2026-10-15 10:00" --locale pl)
        set_tests_properties(tst_editor_qml_keyboard_touch PROPERTIES
            ENVIRONMENT "${_playground_env}"
            TIMEOUT ${_playground_timeout} FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected;SELFTEST FAIL")
    endif()
endif()
