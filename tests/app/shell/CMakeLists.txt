# Tests of the app shell's C++ (library brulion_shell, src/app/shell/area.cmake): navigation
# history, settings persistence, the Workspace's EditorHost implementation against a real vault
# folder, the sample vault, commands and the folder tree (single-instance messages: tests/ipc).
brulion_add_test(tst_shell_navigation SOURCES tst_navigation.cpp LIBS brulion_shell)
brulion_add_test(tst_shell_settings SOURCES tst_settings.cpp LIBS brulion_shell brulion_core_text)
brulion_add_test(tst_shell_workspace SOURCES tst_workspace.cpp LIBS brulion_shell brulion_core_vault brulion_core_markdown)
brulion_add_test(tst_shell_demovault SOURCES tst_demovault.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_calc
         brulion_core_dates)
# The block editor's compare-and-swap saves on the real Workspace (an external append during a
# debounced save is merged, never overwritten).
brulion_add_test(tst_shell_workspace_editor SOURCES tst_workspace_editor.cpp LIBS brulion_shell brulion_editor)
# Pictures from the internet on the real Workspace with the editor (a local HTTP server): fetched
# only when allowed, cached on the device, stored in the vault on "Pobierz do notatnika".
brulion_add_test(tst_shell_workspace_pictures SOURCES tst_workspace_pictures.cpp LIBS brulion_shell brulion_editor Qt6::Network)

# The real shell in its QML, headless (`brulion --selftest-shell`, src/app/shell/shellselftest.h):
# key routing and focus (Escape/back with the drawer and the palette open, back through the
# history, back at home left to the system), rows with context menus with real input (the phone
# run: touch taps and long presses in the drawer opened from the bottom bar and the top bar, its
# edge swipe (also beyond a simulated gesture-navigation zone), All
# pages, tasks, editor bullets; the desktop run: left and right clicks), folders (the open page
# revealed in the tree, "Przenieś do…" from a row's "⋯" / right-click menu, a root folder from the
# "Strony" header, the desktop run's mouse drag and drop with Escape, All pages' folder rows and
# breadcrumb), the block editor inside the shell (real keys, source mode, a link to a missing
# page) and closing the notebook from the UI. Exit code = failures.
# A private XDG home keeps the sample notebook and settings out of the user's own.
# Qt's QML modules are found as for tst_editor_qml (tests/app/editor/CMakeLists.txt).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    # Each test gets its own home with a fresh sample notebook (the self-test edits pages):
    # the fixture removes both before every run.
    set(_shell_selftest_homes ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-phone ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-desktop)
    foreach(_kind phone desktop)
        brulion_test_profile(_profile shell-${_kind})
        list(APPEND _shell_selftest_homes ${_profile_DIRS})
    endforeach()
    add_test(NAME tst_shell_qml_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_shell_selftest_homes})
    set_tests_properties(tst_shell_qml_homes PROPERTIES FIXTURES_SETUP shell_qml_homes)
    add_test(NAME tst_shell_qml
        COMMAND brulion --demo --route journals --selftest-shell --no-single-instance
            --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
    # Started from the "Quick capture" launcher action: a popup open from the first moment.
    add_test(NAME tst_shell_qml_capture
        COMMAND brulion --demo --route journals --capture --selftest-shell --no-single-instance
            --size 1280x800 --now "2026-10-14 10:00" --locale en)
    # Next to other tests (two cores each): every wait of the self-test follows the state it waits
    # for (shellselftest.cpp), so a loaded machine only makes it slower.
    math(EXPR _shell_qml_timeout "150 + 10 * ${BRULION_TEST_SLOWDOWN}")
    foreach(_pair "tst_shell_qml;phone" "tst_shell_qml_capture;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-${_kind})
        brulion_test_profile(_profile shell-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state${_profile}"
            FIXTURES_REQUIRED shell_qml_homes TIMEOUT ${_shell_qml_timeout} PROCESSORS 2
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
    foreach(_kind phone desktop)
        brulion_test_profile(_profile journey-${_kind})
        list(APPEND _journey_homes ${_profile_DIRS})
    endforeach()
    add_test(NAME tst_shell_journey_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_journey_homes})
    set_tests_properties(tst_shell_journey_homes PROPERTIES FIXTURES_SETUP shell_journey_homes)
    add_test(NAME tst_shell_journey
        COMMAND brulion --journey ${CMAKE_CURRENT_BINARY_DIR}/journey-phone --journey-theme light
            --no-single-instance --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
    add_test(NAME tst_shell_journey_desktop
        COMMAND brulion --journey ${CMAKE_CURRENT_BINARY_DIR}/journey-desktop --journey-theme dark
            --no-single-instance --size 1280x800 --now "2026-10-14 10:00" --locale pl)
    math(EXPR _journey_timeout "240 + 10 * ${BRULION_TEST_SLOWDOWN}")
    foreach(_pair "tst_shell_journey;phone" "tst_shell_journey_desktop;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/journey-home-${_kind})
        brulion_test_profile(_profile journey-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};HOME=${_home};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state${_profile}"
            FIXTURES_REQUIRED shell_journey_homes TIMEOUT ${_journey_timeout} PROCESSORS 2 LABELS slow
            FAIL_REGULAR_EXPRESSION "Binding loop detected")
    endforeach()

    # Remote control of the running app (src/app/shell/remotecontrol.h), end to end: the app
    # offscreen on a small notebook in a private home, driven through `brulion-cli app …` and the
    # control socket (off by default, 0600, state, navigate, open, find and click, Polish text typed
    # into a block and saved, Ctrl+K, commands, capture, waitFor, screenshots, gestures, the MCP app
    # tools, quit). The test sets the app's environment itself — a private home of XDG folders, its
    # sockets in its runtime folder, Unix file modes —, which does not apply on Windows: not built
    # there.
    if(TARGET brulion-cli AND NOT WIN32)
        brulion_add_test(tst_remote_control_qml SOURCES tst_remote_control.cpp LIBS brulion_ipc)
        target_compile_definitions(tst_remote_control_qml PRIVATE BRULION_APP="$<TARGET_FILE:brulion>"
            BRULION_CLI="$<TARGET_FILE:brulion-cli>" BRULION_QML_ROOT="${_brulion_qt_qml_root}")
        add_dependencies(tst_remote_control_qml brulion brulion-cli)
        math(EXPR _remote_timeout "240 + 30 * ${BRULION_TEST_SLOWDOWN}")
        set_tests_properties(tst_remote_control_qml PROPERTIES TIMEOUT ${_remote_timeout} PROCESSORS 2)
    endif()
endif()

# The UI driver (src/app/shell/uidriver.h, shared by the self-test, the journey and the remote
# control) on a small Qt Quick scene, offscreen: key sequences, typing Polish text, shortcuts, mouse
# and touch gestures, scrolling, gestures played from timers, finding items.
if(TARGET brulion_uidriver AND EXISTS "${_brulion_qt_qml_root}/QtQuick")
    brulion_add_test(tst_uidriver SOURCES tst_uidriver.cpp LIBS brulion_uidriver Qt6::Qml Qt6::Quick)
    set_tests_properties(tst_uidriver PROPERTIES
        ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 120)
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
# measurements ("perf: …"); BRULION_PERF_NOTES=<n> changes the size. Alone (RUN_SERIAL): it has a
# wall-clock limit on writing the notebook.
brulion_add_test(tst_shell_workspace_perf SOURCES tst_workspace_perf.cpp
    LIBS brulion_shell brulion_core_reminders)
set_tests_properties(tst_shell_workspace_perf PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
