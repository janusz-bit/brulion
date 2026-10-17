# Block editor (src/app/editor, library brulion_editor): pure helpers, BlockListModel +
# PageController behaviour, suggestions, the Markdown highlighter. Run offscreen (QGuiApplication).
brulion_add_test(tst_editor_blocktext
    SOURCES tst_editor_blocktext.cpp
    LIBS brulion_editor brulion_core_markdown brulion_core_calc brulion_core_text)
brulion_add_test(tst_editor_controller
    SOURCES tst_editor_controller.cpp
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
# an unwrapped binary does not know). Skipped when that directory cannot be found.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    add_test(NAME tst_editor_qml
        COMMAND brulion --editor --editor-script selftest --no-single-instance
            --size 1280x800 --now "2026-10-15 10:00" --locale pl)
    # A TypeError means QML code ran on something already destroyed (deferred work of a gone
    # editor): a failure even when every step passed; so is a binding loop (the touch toolbar's
    # Loader had one).
    set_tests_properties(tst_editor_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 90 FAIL_REGULAR_EXPRESSION "TypeError;ReferenceError;Binding loop detected")
endif()
