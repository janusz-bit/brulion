# Tests of the app shell's C++ (library brulion_shell, src/app/shell/area.cmake): navigation
# history, settings persistence, the Workspace's EditorHost implementation against a real vault
# folder, the sample vault, commands, the folder tree and (desktop) single-instance messages.
brulion_add_test(tst_shell_navigation SOURCES tst_navigation.cpp LIBS brulion_shell)
brulion_add_test(tst_shell_settings SOURCES tst_settings.cpp LIBS brulion_shell)
brulion_add_test(tst_shell_workspace SOURCES tst_workspace.cpp LIBS brulion_shell brulion_core_vault brulion_core_markdown)
brulion_add_test(tst_shell_demovault SOURCES tst_demovault.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_calc
         brulion_core_dates)
# Tests are desktop-only builds, where SingleInstance exists.
brulion_add_test(tst_shell_singleinstance SOURCES tst_singleinstance.cpp LIBS brulion_shell Qt6::Network)

# The real shell in its QML, headless (`brulion --selftest-shell`, src/app/shell/shellselftest.h):
# key routing and focus (Escape/back with the drawer and the palette open, back through the
# history, back at home left to the system), the block editor inside the shell (real keys, source
# mode, a link to a missing page) and closing the notebook from the UI. Exit code = failures.
# A private XDG home keeps the sample notebook and settings out of the user's own.
# Qt's QML modules are found as for tst_editor_qml (tests/app/editor/CMakeLists.txt).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    # Each test gets its own home with a fresh sample notebook (the self-test edits pages):
    # the fixture removes both before every run.
    set(_shell_selftest_homes ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-phone ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-desktop)
    add_test(NAME tst_shell_qml_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_shell_selftest_homes})
    set_tests_properties(tst_shell_qml_homes PROPERTIES FIXTURES_SETUP shell_qml_homes)
    add_test(NAME tst_shell_qml
        COMMAND brulion --demo --route journals --selftest-shell --no-single-instance
            --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
    # Started from the "Quick capture" launcher action: a popup open from the first moment.
    add_test(NAME tst_shell_qml_capture
        COMMAND brulion --demo --route journals --capture --selftest-shell --no-single-instance
            --size 1280x800 --now "2026-10-14 10:00" --locale en)
    foreach(_pair "tst_shell_qml;phone" "tst_shell_qml_capture;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache"
            FIXTURES_REQUIRED shell_qml_homes TIMEOUT 90 RUN_SERIAL TRUE)
    endforeach()
endif()
