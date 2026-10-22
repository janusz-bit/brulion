# Tests of the app shell's C++ (library brulion_shell, src/app/shell/area.cmake): navigation
# history, settings persistence, the Workspace's EditorHost implementation against a real vault
# folder, the sample vault, commands, the folder tree and (desktop) single-instance messages.
brulion_add_test(tst_shell_navigation SOURCES tst_navigation.cpp LIBS brulion_shell)
brulion_add_test(tst_shell_settings SOURCES tst_settings.cpp LIBS brulion_shell brulion_core_text)
brulion_add_test(tst_shell_workspace SOURCES tst_workspace.cpp LIBS brulion_shell brulion_core_vault brulion_core_markdown)
brulion_add_test(tst_shell_demovault SOURCES tst_demovault.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_calc
         brulion_core_dates)
# Tests are desktop-only builds, where SingleInstance exists.
brulion_add_test(tst_shell_singleinstance SOURCES tst_singleinstance.cpp LIBS brulion_shell Qt6::Network)

# The real shell in its QML, headless (`brulion --selftest-shell`, src/app/shell/shellselftest.h):
# key routing and focus (Escape/back with the drawer and the palette open, back through the
# history, back at home left to the system), rows with context menus with real input (the phone
# run: touch taps and long presses in the drawer opened from the bottom bar and the top bar, All
# pages, tasks, editor bullets; the desktop run: left and right clicks), the block editor inside
# the shell (real keys, source mode, a link to a missing page) and closing the notebook from the
# UI. Exit code = failures.
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
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state"
            FIXTURES_REQUIRED shell_qml_homes TIMEOUT 90 RUN_SERIAL TRUE
            # QML that ran on something already gone fails too, as in tst_editor_qml (a side
            # panel created from the context of the view the shell had just left did).
            FAIL_REGULAR_EXPRESSION "Binding loop detected;TypeError;ReferenceError;Cannot create a component in an invalid context")
    endforeach()

    # A first-time user's journey through the real app with real clicks, taps and keys
    # (`brulion --journey <dir>`, src/app/shell/shelljourney.h): onboarding, the sample notebook,
    # writing in today's journal (outline, [[link]], a task with a reminder from the slash menu),
    # calendar, tasks, a page through a link, search "zolw", graph, sync, import/export, history,
    # settings. Exit code = failed steps. The phone run taps (touch events) through the drawer and
    # the bottom bar; the desktop run clicks through the sidebar and menus. Screenshots land in
    # the build dir (tests/app/shell/journey.sh makes the reviewed set).
    set(_journey_homes ${CMAKE_CURRENT_BINARY_DIR}/journey-home-phone ${CMAKE_CURRENT_BINARY_DIR}/journey-home-desktop)
    add_test(NAME tst_shell_journey_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_journey_homes})
    set_tests_properties(tst_shell_journey_homes PROPERTIES FIXTURES_SETUP shell_journey_homes)
    add_test(NAME tst_shell_journey
        COMMAND brulion --journey ${CMAKE_CURRENT_BINARY_DIR}/journey-phone --journey-theme light
            --no-single-instance --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
    add_test(NAME tst_shell_journey_desktop
        COMMAND brulion --journey ${CMAKE_CURRENT_BINARY_DIR}/journey-desktop --journey-theme dark
            --no-single-instance --size 1280x800 --now "2026-10-14 10:00" --locale pl)
    foreach(_pair "tst_shell_journey;phone" "tst_shell_journey_desktop;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/journey-home-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};HOME=${_home};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state"
            FIXTURES_REQUIRED shell_journey_homes TIMEOUT 240 RUN_SERIAL TRUE LABELS slow
            FAIL_REGULAR_EXPRESSION "Binding loop detected")
    endforeach()
endif()

# The Workspace wired to the real core (workspace*.cpp): indexing progress, quick capture,
# index queries and the QueryHost, trash, history, fold state, reminders through a fake sink,
# conflict copies and the AppController integration; and Workspace.sync against the core's
# in-memory Nextcloud server and a fake Syncthing REST API.
brulion_add_test(tst_shell_workspace_wiring SOURCES tst_workspace_wiring.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_reminders
         brulion_core_dates)
if(TARGET brulion_sync_testsupport)
    brulion_add_test(tst_shell_workspace_sync SOURCES tst_workspace_sync.cpp
        LIBS brulion_shell brulion_sync_testsupport)
    # Nextcloud per notebook through AppController and the in-memory server: the settings of
    # 0.3.0 and older going to the notebook that was synced (never mixing two notebooks in one
    # server folder), the same notebook through a symlink or a relative path, the start screen.
    brulion_add_test(tst_shell_nextcloud SOURCES tst_nextcloud.cpp
        LIBS brulion_shell brulion_sync_testsupport brulion_core_vault)
endif()

# Start-up of a 3000-note notebook (Workspace::open, chunked indexing on the event loop, the
# first view queries): open() time, the longest UI-thread stall, time until `ready`. Prints the
# measurements ("perf: …"); BRULION_PERF_NOTES=<n> changes the size.
brulion_add_test(tst_shell_workspace_perf SOURCES tst_workspace_perf.cpp
    LIBS brulion_shell brulion_core_reminders)
set_tests_properties(tst_shell_workspace_perf PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
