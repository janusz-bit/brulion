# The terminal panel's C++ (library brulion_terminal, src/app/terminal): TerminalSession end to end
# on a real pseudo-terminal (input echoed by cat, the shell started in its folder printing pwd, the
# size the program sees, the title it sets, stop and restart, a command that fails, a job in the
# foreground, marks while not shown, the current directory), a thread's tabs and splits and the
# registry of every thread's terminals (keys, rekeying, renames, deletion, archiving, the limits),
# and TerminalView in a window — keys into the program (Ctrl+C is ^C, copy and paste chords stay in
# the view, the app's shortcuts are left to it), mouse selection and copying, the scrollback, mouse
# reporting, links under Ctrl, the find bar's matches, Clear, the context menu, the block for the
# chat, painting at odd sizes (0×0, 1×1) and in both palettes. Where the build has no pty only `available` and the
# view without a program are checked. Offscreen with the software scene graph.
if(TARGET brulion_terminal)
    brulion_add_test(tst_terminal
        SOURCES tst_terminal.cpp
        LIBS brulion_terminal brulion_core_term Qt6::Quick)
    target_compile_definitions(tst_terminal PRIVATE BRULION_TEST_PTY_BACKEND="${BRULION_PTY_BACKEND_USED}")
    math(EXPR _terminal_function_timeout "300000 * ${BRULION_TEST_SLOWDOWN}")
    set_tests_properties(tst_terminal PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QTEST_FUNCTION_TIMEOUT=${_terminal_function_timeout}"
        TIMEOUT 180)
endif()
