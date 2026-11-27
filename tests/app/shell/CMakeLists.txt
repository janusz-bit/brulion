# Tests of the app shell's C++ (library brulion_shell, src/app/shell/area.cmake): navigation
# history, settings persistence, the Workspace's EditorHost implementation against a real vault
# folder, the sample vault, commands and the folder tree (single-instance messages: tests/ipc).
brulion_add_test(tst_shell_navigation SOURCES tst_navigation.cpp LIBS brulion_shell brulion_platform)
# The tabs (tabs.h): opening, closing, moving, the split pane, tabs in windows of their own, each
# tab's history and view state, saving them per notebook (also malformed JSON).
brulion_add_test(tst_shell_tabs SOURCES tst_tabs.cpp LIBS brulion_shell)
# b.app over the app (appscripting.h): the settings' registry, an extension page's changes of the app,
# its theme and UI items, the "route" hook, live code and evaluations for a client, revert().
brulion_add_test(tst_shell_scripting SOURCES tst_scripting.cpp LIBS brulion_shell brulion_platform brulion_ui_helpers)
# ViewTraits (viewtraits.h): what ShellRouteView reads of its view — typed, following changes, the
# optional functions called (typed and untyped QML views); folderChoices (folderchoices.h): the
# rows of "Move to…".
brulion_add_test(tst_shell_viewtraits SOURCES tst_viewtraits.cpp LIBS brulion_shell Qt6::Qml)
# ViewKeeper (viewkeeper.h): which hidden views a pane holds on to — the most recently shown up to
# a limit, none after a while without a switch or when asked to let go (docs/PERFORMANCE.md "Memory:
# only what is in use").
brulion_add_test(tst_shell_viewkeeper SOURCES tst_viewkeeper.cpp LIBS brulion_shell)
# "Share → Brulion" on AppController (appshare.cpp; the --share option): the share dialog's
# data, saving to the journal or to new pages, waiting for the notebook, the copies removed; files
# opened with Brulion (--open, a second launch's `share` message: copies, the files untouched, a
# page of the notebook opened), the browser's share target (--share-query).
brulion_add_test(tst_shell_share SOURCES tst_share.cpp LIBS brulion_shell brulion_platform)
# brulion:// addresses on AppController (appautomation.cpp): opening, writing after the
# confirmation or with the notebook's key, the policies, waiting for a notebook, another notebook,
# the documentation, a second launch's `uri` message, links in notes, building addresses.
brulion_add_test(tst_shell_automation SOURCES tst_automation.cpp LIBS brulion_shell brulion_platform)
# The tray calendar's placement next to the tray icon and its reopen guard (traypopup.h; offscreen
# QGuiApplication for the screen lookup).
brulion_add_test(tst_shell_traypopup SOURCES tst_traypopup.cpp LIBS brulion_shell Qt6::Gui)
brulion_add_test(tst_shell_settings SOURCES tst_settings.cpp LIBS brulion_shell brulion_core_text)
# The start screen's recent notebooks (recentnotebooks.h): rows, "~" paths, last-opened texts in
# both languages, the folders checked off the GUI thread (missing, a share that hangs), temporary
# folders dropped, "Remove from list", malformed stored entries.
brulion_add_test(tst_shell_recentnotebooks SOURCES tst_recentnotebooks.cpp LIBS brulion_shell)
# Settings' "Browser extension (Web Clipper)" (webclippersetup.h): the host manifests in a private home.
brulion_add_test(tst_shell_webclippersetup SOURCES tst_webclippersetup.cpp LIBS brulion_shell brulion_core_clipper)
# The C++ behind the views (qml/views): route parameters read from a QVariantMap, SearchView's
# recent searches, CleanupView's selection (CleanupSelection).
brulion_add_test(tst_shell_views SOURCES tst_views.cpp LIBS brulion_shell)
# Custom keyboard shortcuts (shortcutmap.h): key chords, what cannot be a shortcut, conflicts,
# taking over, reset, persistence and malformed stored values.
brulion_add_test(tst_shell_shortcutmap SOURCES tst_shortcutmap.cpp LIBS brulion_shell)
# The message about a newer version (appupdates.h; a local HTTP server stands in for GitHub).
# BRULION_UPDATE_CHECK_EXPECTED: the CMake option, which the library's AppUpdates::checkBuiltIn()
# must follow (an OFF build runs the tests of a build without the check).
brulion_add_test(tst_shell_appupdates SOURCES tst_appupdates.cpp ../minihttpserver.h LIBS brulion_shell Qt6::Network)
target_compile_definitions(tst_shell_appupdates PRIVATE BRULION_UPDATE_CHECK_EXPECTED=$<BOOL:${BRULION_UPDATE_CHECK}>)
# The model on the device as the app keeps it (localai.h): the chosen model, its download from a
# local HTTP server and its removal, requests answered on the worker thread by a fake runner.
brulion_add_test(tst_shell_localai SOURCES tst_localai.cpp LIBS brulion_shell brulion_core_ai Qt6::Network)
brulion_add_test(tst_shell_workspace SOURCES tst_workspace.cpp LIBS brulion_shell brulion_core_vault brulion_core_markdown)
# Workspace.calendars against an in-memory CalDAV server (tests/core/caldav/fakecaldav.h) and a feed.
brulion_add_test(tst_shell_workspace_calendars SOURCES tst_workspace_calendars.cpp
    LIBS brulion_shell brulion_caldav_testsupport)
# The extensions as the app runs them (workspaceextensions.h), on the sample extensions and their
# notebook (tests/core/extensions/samples): approval per device, commands with the questions the
# app shows, views and their actions, hooks as the Workspace delivers them (what an extension
# writes is no "change", what the user changes always is), code an extension writes, the editor's
# saves of an approved page; a view as the app draws it (ExtensionViewModel on the gym's "today"):
# numbers typed into its steppers, drawn again in place at once after an action.
brulion_add_test(tst_shell_extensions SOURCES tst_extensions.cpp
    LIBS brulion_shell brulion_core_extensions brulion_core_index brulion_core_vault)
target_compile_definitions(tst_shell_extensions PRIVATE BRULION_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# The Habits view as the app draws it (docs/UI.md "Habits"): ExtensionViewModel over the built-in
# extension `nawyki` — cards, problems, months, checks and days ticked, Undo, "Add habit"'s form.
brulion_add_test(tst_shell_habits SOURCES tst_habits.cpp
    LIBS brulion_shell brulion_core_extensions brulion_core_index brulion_core_vault)
# The Flashcards view as the app draws it (docs/UI.md "Flashcards"): ExtensionViewModel over the
# built-in extension `fiszki` — decks, a review session by its keys with Undo, Markdown as rich text,
# the app's settings and the sidebar's due count.
brulion_add_test(tst_shell_flashcards SOURCES tst_flashcards.cpp
    LIBS brulion_shell brulion_core_extensions brulion_core_index brulion_core_vault)
# Macros, template variables, view blocks and panels as the app runs them (workspacemacros.cpp,
# docs/EXTENSIONS.md "Macros", "View blocks and panels"): placeholders, values computed after the
# frame and again when what they read changes, the new controls of extension views.
brulion_add_test(tst_shell_macros SOURCES tst_macros.cpp
    LIBS brulion_shell brulion_core_extensions brulion_core_index brulion_core_vault)
brulion_add_test(tst_shell_demovault SOURCES tst_demovault.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_calc
         brulion_core_dates brulion_core_diagram brulion_core_chart brulion_core_dataview)
# The block editor's compare-and-swap saves on the real Workspace (an external append during a
# debounced save is merged, never overwritten).
brulion_add_test(tst_shell_workspace_editor SOURCES tst_workspace_editor.cpp LIBS brulion_shell brulion_editor)
# Encrypted notes on the real Workspace (workspaceencryption.h, encryptedpagehost.h) with the editor:
# a Meld Encrypt note opened, edited and saved encrypted, locks, page conversions, fragments, and no
# plaintext anywhere on disk (the notebook, History, the index cache, the device state).
brulion_add_test(tst_shell_workspace_encryption SOURCES tst_workspace_encryption.cpp LIBS brulion_shell brulion_editor)
# Pictures from the internet on the real Workspace with the editor (a local HTTP server): fetched
# only when allowed, cached on the device, stored in the vault on "Pobierz do notatnika"; drawings
# saved again and shown again (their versioned URL).
brulion_add_test(tst_shell_workspace_pictures SOURCES tst_workspace_pictures.cpp ../minihttpserver.h
    LIBS brulion_shell brulion_editor brulion_core_drawing Qt6::Network)
# The editor's AI assistant on the real Workspace (WritingSession), an OpenAI-compatible server
# played (../fakeaiserver.h): a rewrite streamed and put into the block as one undo step, a preset
# waited for, a summary and a continuation of blocks, source mode's paragraph, Stop, failures; Code
# mode's texts by the same AI: AiAsk, a new worktree's branch name, "Regenerate title".
brulion_add_test(tst_shell_writingsession SOURCES tst_writingsession.cpp ../fakeaiserver.h
    LIBS brulion_shell brulion_editor Qt6::Network)
# PDFs on the real Workspace (workspacepdf.cpp): "Open in another app" through a recording hook
# (what is refused and why), "Draw on this page" (the drawing embedded after the PDF's block, or
# in today's journal), the drawings of a page, the thumbnails with this build's renderer.
brulion_add_test(tst_shell_workspace_pdf SOURCES tst_workspace_pdf.cpp
    LIBS brulion_shell brulion_platform brulion_pdfview brulion_core_drawing)

# The real shell in its QML, headless (`brulion --selftest-shell`, src/app/shell/shellselftest.h):
# key routing and focus (Escape/back with the drawer and the palette open, back through the
# history, back at home left to the system), rows with context menus with real input (the phone
# run: touch taps and long presses in the drawer opened from the bottom bar and the top bar, its
# edge swipe (also beyond a simulated gesture-navigation zone), All
# pages, tasks, editor bullets; the desktop run: left and right clicks, and the keys of a tree row's
# menu and of the block menu), folders (the open page
# revealed in the tree — wholly in the list's visible part, also when the window got shorter while
# the drawer was closed; the new folder and "Show in tree" too —; a click on a row in view
# leaves the tree where it was; touchpad, wheel and arrow-key scrolling stays across a resize;
# the desktop sidebar shown again after its header grew starts at the top), "Move to…" from
# a row's "⋯" / right-click menu, a root folder from the
# "Pages" header, the desktop run's mouse drag and drop with Escape, All pages' folder rows and
# breadcrumb), the block editor inside the shell (real keys, source mode, a link to a missing
# page), PDFs (the thumbnail, the viewer's keys, zoom, search, page jump, a drawing on a page, a
# locked PDF from tests/data/pdf: BRULION_SELFTEST_PDF_DIR), tabs (shortcuts, middle and Ctrl+clicks,
# the bar's drag, the split view, a page window; the phone's tab switcher) and closing the notebook
# from the UI.
# Exit code = failures.
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
    # Started from the "Quick capture" launcher action: a popup open from the first moment. The
    # English UI with the Polish sample notes the self-test is written against (--sample-language).
    add_test(NAME tst_shell_qml_capture
        COMMAND brulion --demo --route journals --capture --selftest-shell --no-single-instance
            --size 1280x800 --now "2026-10-14 10:00" --locale en --sample-language pl)
    # Next to other tests (two cores each): every wait of the self-test follows the state it waits
    # for (shellselftest.cpp), so a loaded machine only makes it slower — and this limit, which
    # only has to end a run that hangs, is three times the longest run seen. (153 s alone on a busy
    # 16-core machine, 211 s in a full parallel run; on CI runners 160-215 s in November 2026, so
    # close to the 240 s it had that a busy runner went past it.)
    math(EXPR _shell_qml_timeout "660 + 10 * ${BRULION_TEST_SLOWDOWN}")
    foreach(_pair "tst_shell_qml;phone" "tst_shell_qml_capture;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/selftest-home-${_kind})
        brulion_test_profile(_profile shell-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state;BRULION_SELFTEST_PDF_DIR=${PROJECT_SOURCE_DIR}/tests/data/pdf${_profile}"
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
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};HOME=${_home};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state${_profile}"
            FIXTURES_REQUIRED shell_journey_homes TIMEOUT ${_journey_timeout} PROCESSORS 2 LABELS slow
            FAIL_REGULAR_EXPRESSION "Binding loop detected")
    endforeach()

    # The start screen (`brulion --onboarding --selftest-onboarding`, src/app/shell/onboardingselftest.h)
    # with real clicks, keys and taps: the recent notebooks (a missing one marked and removed from
    # the list, a vanished temporary one dropped, "Show all" on phones), the two-column layout that
    # fits 1280×800, the one primary action creating and opening a notebook, the keyboard on the
    # list. Desktop and phone, each in its own private home.
    set(_onboarding_homes ${CMAKE_CURRENT_BINARY_DIR}/onboarding-home-phone ${CMAKE_CURRENT_BINARY_DIR}/onboarding-home-desktop)
    foreach(_kind phone desktop)
        brulion_test_profile(_profile onboarding-${_kind})
        list(APPEND _onboarding_homes ${_profile_DIRS})
    endforeach()
    add_test(NAME tst_shell_onboarding_qml_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_onboarding_homes})
    set_tests_properties(tst_shell_onboarding_qml_homes PROPERTIES FIXTURES_SETUP shell_onboarding_homes)
    add_test(NAME tst_shell_onboarding_qml
        COMMAND brulion --onboarding --selftest-onboarding --no-single-instance
            --size 412x915 --touch --now "2026-11-20 18:30" --locale pl)
    add_test(NAME tst_shell_onboarding_qml_desktop
        COMMAND brulion --onboarding --selftest-onboarding --no-single-instance
            --size 1280x800 --now "2026-11-20 18:30" --locale en)
    math(EXPR _onboarding_timeout "120 + 10 * ${BRULION_TEST_SLOWDOWN}")
    foreach(_pair "tst_shell_onboarding_qml;phone" "tst_shell_onboarding_qml_desktop;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${CMAKE_CURRENT_BINARY_DIR}/onboarding-home-${_kind})
        brulion_test_profile(_profile onboarding-${_kind})
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state${_profile}"
            FIXTURES_REQUIRED shell_onboarding_homes TIMEOUT ${_onboarding_timeout} PROCESSORS 2
            FAIL_REGULAR_EXPRESSION "Binding loop detected;TypeError;ReferenceError")
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
            BRULION_CLI="$<TARGET_FILE:brulion-cli>" BRULION_QML_ROOT="${BRULION_TEST_QML_IMPORT_PATH}"
            FAKE_ACP_AGENT="$<TARGET_FILE:fake_acp_agent>")
        add_dependencies(tst_remote_control_qml brulion brulion-cli fake_acp_agent)
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
        ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${BRULION_TEST_QML_IMPORT_PATH}"
        TIMEOUT 120)
endif()

# The Workspace wired to the real core (workspace*.cpp): indexing progress, quick capture,
# index queries and the QueryHost, trash, history, fold state, reminders through a fake sink,
# conflict copies and the AppController integration; and Workspace.sync against the core's
# in-memory Nextcloud server and a fake Syncthing REST API.
brulion_add_test(tst_shell_workspace_wiring SOURCES tst_workspace_wiring.cpp
    LIBS brulion_shell brulion_core_vault brulion_core_markdown brulion_core_index brulion_core_reminders
         brulion_core_dates)
# Automatic backups on the real Workspace (workspacebackups.h): the schedule, pruning, a folder that
# went away, restoring, the browser's rules; also in slices on the GUI thread (the browser's way).
brulion_add_test(tst_shell_workspace_backups SOURCES tst_workspace_backups.cpp LIBS brulion_shell)
add_test(NAME tst_shell_workspace_backups_inline COMMAND tst_shell_workspace_backups)
set_tests_properties(tst_shell_workspace_backups_inline PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;BRULION_INDEX_THREADS=0")
# The same with the index loaded in slices on the GUI thread (no worker threads: WebAssembly's way);
# the default run loads it on worker threads (docs/PERFORMANCE.md, L1).
add_test(NAME tst_shell_workspace_wiring_inline COMMAND tst_shell_workspace_wiring)
math(EXPR _wiring_timeout_ms "300000 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_shell_workspace_wiring_inline PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;BRULION_INDEX_THREADS=0;QTEST_FUNCTION_TIMEOUT=${_wiring_timeout_ms}")
# Workspace.github against the core's in-memory GitHub (tests/core/github/fakegithub.h): the token,
# linking a notebook, pulls, commits, proposals, a repository opened as a new notebook.
if(TARGET brulion_github_testsupport)
    brulion_add_test(tst_shell_workspace_github SOURCES tst_workspace_github.cpp
        LIBS brulion_shell brulion_github_testsupport brulion_core_vault)
endif()
if(TARGET brulion_sync_testsupport)
    brulion_add_test(tst_shell_workspace_sync SOURCES tst_workspace_sync.cpp
        LIBS brulion_shell brulion_sync_testsupport brulion_pdfview)
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
# The remote MCP server the desktop app runs (mcpserversupervisor.h) with the real brulion-cli:
# its arguments and delays, off and refused, running and answering, its log, refused by the
# server (no https, a port in use), started again after a kill and a setting changed, stopped.
if(TARGET brulion-cli)
    brulion_add_test(tst_shell_mcpserver SOURCES tst_mcpserver.cpp LIBS brulion_shell Qt6::Network)
    target_compile_definitions(tst_shell_mcpserver PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>")
    add_dependencies(tst_shell_mcpserver brulion-cli)
    math(EXPR _mcpserver_timeout "180 * ${BRULION_TEST_SLOWDOWN}")
    set_tests_properties(tst_shell_mcpserver PROPERTIES TIMEOUT ${_mcpserver_timeout})
endif()

# AI agents of the user's own in the AI chat (workspaceagents.h) on a real Workspace, the agent a
# real program that speaks the Agent Client Protocol (fakeacpagent.cpp): a conversation answered,
# the same session for the next message, the agent's question allowed, rejected and stopped, Stop,
# an agent that dies and starts again with the conversation so far, signing in, an agent not set
# up or not starting, the chat's partners, the settings' agents (malformed entries left out); Code
# mode's threads, also of a project of another computer through the real brulion-cli acp serve.
qt_add_executable(fake_acp_agent fakeacpagent.cpp)
target_link_libraries(fake_acp_agent PRIVATE Qt6::Core)
brulion_set_warnings(fake_acp_agent)
brulion_add_test(tst_shell_agents SOURCES tst_agents.cpp LIBS brulion_shell)
target_compile_definitions(tst_shell_agents PRIVATE FAKE_ACP_AGENT="$<TARGET_FILE:fake_acp_agent>")
add_dependencies(tst_shell_agents fake_acp_agent)
# The real agents' wake (realAgentWakesParent): a control socket of the test's own (src/ipc, desktop).
if(TARGET brulion_ipc)
    target_link_libraries(tst_shell_agents PRIVATE brulion_ipc)
    target_compile_definitions(tst_shell_agents PRIVATE BRULION_TEST_IPC)
endif()
# An agent on another computer: the real `brulion-cli acp serve` (desktop builds have it).
if(TARGET brulion-cli)
    target_compile_definitions(tst_shell_agents PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>")
    add_dependencies(tst_shell_agents brulion-cli)
endif()
# Claude Code's and Codex's subscription limits as the chat shows them (agentlimits.h): which agents
# have them, their texts in local time, read when shown or refreshed over a fake transport.
# What the app told the user (notificationcenter.h): kept per notebook, read or unread, pruned;
# agents', extensions' and scripts' posts limited, titled with who sends them, cleaned.
brulion_add_test(tst_shell_notificationcenter SOURCES tst_notificationcenter.cpp LIBS brulion_shell)
brulion_add_test(tst_shell_agentlimits SOURCES tst_agentlimits.cpp LIBS brulion_shell brulion_core_agentlimits)
# What the agents used, from their own session logs, as Settings and the chat show it
# (agentusage.h): which agents, where their logs are, the texts in English and in Polish (the app's
# translations, compiled for tst_shell_chattools below), the dialog's bars, models and notebook
# filter, reading synthetic logs when shown or refreshed, off the GUI thread.
brulion_add_test(tst_shell_agentusage SOURCES tst_agentusage.cpp LIBS brulion_shell brulion_core_agentusage)
# An agent's tool calls in words (chattools.h): a part per kind in the order done, at most three
# named, the failed ones apart, the answer of a turn without words — in English and in Polish with
# the app's own translations (i18n/*.ts, compiled here as for brulion-cli), their plural forms.
brulion_add_test(tst_shell_chattools SOURCES tst_chattools.cpp LIBS brulion_shell)
if(COMMAND qt_add_lrelease)
    qt_add_lrelease(
        TS_FILES ${PROJECT_SOURCE_DIR}/i18n/brulion_pl.ts ${PROJECT_SOURCE_DIR}/i18n/brulion_en.ts
        LRELEASE_TARGET tst_shell_chattools_lrelease)
    target_compile_definitions(tst_shell_chattools PRIVATE BRULION_QM_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(tst_shell_chattools tst_shell_chattools_lrelease)
    target_compile_definitions(tst_shell_agentusage PRIVATE BRULION_QM_DIR="${CMAKE_CURRENT_BINARY_DIR}")
    add_dependencies(tst_shell_agentusage tst_shell_chattools_lrelease)
endif()
# A brulion-cli server run while the app runs (serverprocess.h) with a server of the test's own
# (fakeserver.cpp): Running on its first line, stopped by the end of its input (terminated when it
# ignores it), started again after a crash with growing waits, a refusal final, new arguments.
qt_add_executable(fake_server fakeserver.cpp)
target_link_libraries(fake_server PRIVATE Qt6::Core)
brulion_set_warnings(fake_server)
brulion_add_test(tst_shell_serverprocess SOURCES tst_serverprocess.cpp LIBS brulion_shell)
target_compile_definitions(tst_shell_serverprocess PRIVATE FAKE_SERVER="$<TARGET_FILE:fake_server>")
add_dependencies(tst_shell_serverprocess fake_server)
# "Share agents with the phone" (agentshare.h): brulion-cli's arguments for each address, the
# network's address, the notebook's offers; and the real `brulion-cli acp serve` started by the
# switch against a fake tailscaled (tests/common/faketailscaled.h): its tailnet name, the offer in
# config.json, a pairing without a code from the app's client, a code and its link, unpairing,
# the code asked for without the trust, turned off.
brulion_add_test(tst_shell_agentshare SOURCES tst_agentshare.cpp ${PROJECT_SOURCE_DIR}/tests/common/faketailscaled.h
    LIBS brulion_shell brulion_core_tailscale brulion_core_vault Qt6::Network)
if(TARGET brulion-cli)
    target_compile_definitions(tst_shell_agentshare PRIVATE BRULION_CLI="$<TARGET_FILE:brulion-cli>")
    add_dependencies(tst_shell_agentshare brulion-cli)
endif()
math(EXPR _agentshare_timeout "180 * ${BRULION_TEST_SLOWDOWN}")
set_tests_properties(tst_shell_agentshare PROPERTIES TIMEOUT ${_agentshare_timeout})
# Code mode's projects beyond the list (codeprojects.h, codeconfig.cpp, codeworktrees.cpp,
# codeeditors.cpp, codeclones.cpp): t3.json, merged scripts, pins, icons, defaults, "Open in…" with
# fake editors on a PATH of their own, pull request references, the clone picker's rows and default
# folders, and with git: worktrees from a base branch, branches, pull requests checked out without gh
# and with a fake one, worktrees checked, removed and made again, clones added as projects, retried,
# dismissed, stopped and tried again over https.
brulion_add_test(tst_shell_codeprojects SOURCES tst_codeprojects.cpp LIBS brulion_shell)
