# Terminal area of the app (included by src/app/CMakeLists.txt in its scope, so relative paths are
# relative to src/app): the Code mode's terminal panel (docs/CODE-TERMINAL.md) — the static library
# brulion_terminal and its QML.
#
#   TerminalSession       a program on a pseudo-terminal (platform::Pty) and its emulator
#                         (core term::Terminal): start, stop, write, paste, resize, title, exit code,
#                         busy (a job in the foreground), activity and bell marks
#   TerminalGroup         a tab: up to four sessions side by side or stacked, one focused
#   TerminalThread        a thread's tabs, the one shown, the folder new terminals start in
#   TerminalRegistry      every thread's terminals, kept while another thread is shown; limits
#   TerminalView          the screen painted (dirty rows only, once a frame), keys, mouse, selection,
#                         the clipboard, the input method, links under Ctrl, the find bar's matches
# The emulator is the core module term (src/core/term), the pty src/platform/pty.h (POSIX; none on
# Windows, Android and in the browser: `available` false and the panel says so). Tests:
# tests/app/terminal. QML: qml/terminal/TerminalPanel.qml (a thread's tabs, splits, the find bar,
# the context menu, run(command, label) for a project's scripts), TerminalTab, TerminalPane (one
# terminal: the view, its scroll bar, "Jump to latest", "Process exited"), TerminalFindBar.
# terminalwiring.cpp (in the app) ties the registry to the notebook: renames, deletions, archiving.

set(_brulion_terminal_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_terminal STATIC
    ${_brulion_terminal_dir}/terminalsession.h ${_brulion_terminal_dir}/terminalsession.cpp
    ${_brulion_terminal_dir}/terminalgroup.h ${_brulion_terminal_dir}/terminalgroup.cpp
    ${_brulion_terminal_dir}/terminalthread.h ${_brulion_terminal_dir}/terminalthread.cpp
    ${_brulion_terminal_dir}/terminalregistry.h ${_brulion_terminal_dir}/terminalregistry.cpp
    ${_brulion_terminal_dir}/terminalview.h ${_brulion_terminal_dir}/terminalview.cpp
)
target_include_directories(brulion_terminal PUBLIC ${_brulion_terminal_dir})
target_link_libraries(brulion_terminal
    PUBLIC Qt6::Core Qt6::Gui Qt6::Quick brulion_core_term
    PRIVATE brulion_platform)
qt_extract_metatypes(brulion_terminal)
brulion_set_warnings(brulion_terminal)

# --- The app: QML registration and QML files ---------------------------------------------------
target_sources(brulion PRIVATE terminal/terminalqml.h terminal/terminalwiring.cpp)
# The generated QML type registration includes the area's headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_terminal_dir})
target_link_libraries(brulion PRIVATE brulion_terminal)

qt_target_qml_sources(brulion QML_FILES
    qml/terminal/TerminalPanel.qml
    qml/terminal/TerminalTab.qml
    qml/terminal/TerminalPane.qml
    qml/terminal/TerminalFindBar.qml
)
