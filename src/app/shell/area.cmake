# The app shell (docs/UI.md: window layout, navigation, onboarding, settings, command palette,
# quick switcher, toasts, shortcuts, tray). Included by src/app/CMakeLists.txt in its scope, so
# relative paths below are relative to src/app.
#
# C++: the static library brulion_shell (QtCore, QtGui, QtNetwork + core modules; unit-tested by
# tests/app/shell without the app): AppController (appshare.cpp: shares from other apps), AppSettings, Workspace (implements the
# editor's EditorHost and QueryHost; workspace*.cpp: index queries, history and trash, folders and
# moves, quick capture — the last three through the core module notebook, shared with brulion-cli —,
# the clipboard's HTML of copied blocks and pasted HTML (core io), PDFs (workspacepdf.cpp: thumbnails,
# "Open in another app", a drawing on a PDF page), reminders — WorkspaceReminders
# over the notifiers of src/app/notify —, sync —
# WorkspaceSync: Nextcloud, Syncthing, conflicts —, the notebook kept in a GitHub repository —
# WorkspaceGitHub: pull, commit, propose (core github) — and their wiring to the app,
# workspaceintegration), CommandModel, ShortcutMap (the commands' custom shortcuts), FolderTreeModel, the sample vault, the tray calendar's
# placement (traypopup.h, pure). SingleInstance is in
# src/ipc (brulion_ipc, desktop). The app registers them as QML types through the QML_FOREIGN wrappers in
# shell/shellqml.h (the src/app/ui pattern; the library's extracted metatypes give qmllint and
# qmlcachegen their full API).
# The desktop (Linux, Windows; BRULION_DESKTOP) only: the system tray (QSystemTrayIcon needs Qt
# Widgets, so main.cpp creates a QApplication there and BRULION_DESKTOP_INTEGRATION is defined for
# it), the QML self-test
# (--selftest-shell, run by ctest as tst_shell_qml), the user journey with screenshots
# (--journey <dir>, shelljourney.h; ctest tst_shell_journey, tests/app/shell/journey.sh) and the
# remote control of the running app for AI agents (--remote-control, remotecontrol.h; ctest
# tst_remote_control_qml), all three on the UI driver (uidriver.h, library brulion_uidriver).
# QML: qml/Main.qml (listed by src/app/CMakeLists.txt), qml/shell/*, and the views
# (shell/views.cmake).

set(_brulion_shell_dir ${CMAKE_CURRENT_LIST_DIR})

# EditorHost (editor/editorhost.h) is a header-only QObject interface: its moc output must be
# compiled exactly once. The shell's Workspace implements it, so this tiny library provides it
# unless an area included earlier (the editor) already defined the target. Areas that need the
# interface link brulion_editorhost instead of moc-ing the header themselves.
if(NOT TARGET brulion_editorhost)
    qt_add_library(brulion_editorhost STATIC ${CMAKE_CURRENT_SOURCE_DIR}/editor/editorhost.h
        ${CMAKE_CURRENT_SOURCE_DIR}/editor/picturesize.h ${CMAKE_CURRENT_SOURCE_DIR}/editor/picturesize.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/editor/taskhelpers.h ${CMAKE_CURRENT_SOURCE_DIR}/editor/taskhelpers.cpp)
    target_include_directories(brulion_editorhost PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/editor)
    target_link_libraries(brulion_editorhost PUBLIC Qt6::Core PRIVATE Qt6::Gui brulion_core_drawing
        brulion_core_markdown brulion_core_notebook)
    qt_extract_metatypes(brulion_editorhost)
    brulion_set_warnings(brulion_editorhost)
endif()

qt_add_library(brulion_shell STATIC
    ${_brulion_shell_dir}/appcontroller.h ${_brulion_shell_dir}/appcontroller.cpp
    ${_brulion_shell_dir}/appshare.cpp
    ${_brulion_shell_dir}/appsettings.h ${_brulion_shell_dir}/appsettings.cpp
    ${_brulion_shell_dir}/appupdates.h ${_brulion_shell_dir}/appupdates.cpp
    ${_brulion_shell_dir}/commandmodel.h ${_brulion_shell_dir}/commandmodel.cpp
    ${_brulion_shell_dir}/demovault.h ${_brulion_shell_dir}/demovault.cpp
    ${_brulion_shell_dir}/folderchoices.h ${_brulion_shell_dir}/folderchoices.cpp
    ${_brulion_shell_dir}/foldertreemodel.h ${_brulion_shell_dir}/foldertreemodel.cpp
    ${_brulion_shell_dir}/indexworkers.h
    ${_brulion_shell_dir}/navigation.h ${_brulion_shell_dir}/navigation.cpp
    ${_brulion_shell_dir}/tabs.h ${_brulion_shell_dir}/tabs.cpp
    ${_brulion_shell_dir}/replacesession.h ${_brulion_shell_dir}/replacesession.cpp
    ${_brulion_shell_dir}/shortcutmap.h ${_brulion_shell_dir}/shortcutmap.cpp
    ${_brulion_shell_dir}/traypopup.h ${_brulion_shell_dir}/traypopup.cpp
    ${_brulion_shell_dir}/viewtraits.h ${_brulion_shell_dir}/viewtraits.cpp
    ${_brulion_shell_dir}/workspace.h ${_brulion_shell_dir}/workspace.cpp
    ${_brulion_shell_dir}/workspacequery.cpp
    ${_brulion_shell_dir}/workspacehistory.cpp
    ${_brulion_shell_dir}/workspacefolders.cpp
    ${_brulion_shell_dir}/workspaceclipboard.cpp
    ${_brulion_shell_dir}/workspacepictures.cpp
    ${_brulion_shell_dir}/workspacepdf.cpp
    ${_brulion_shell_dir}/workspacereminders.h ${_brulion_shell_dir}/workspacereminders.cpp
    ${_brulion_shell_dir}/workspacebackups.h ${_brulion_shell_dir}/workspacebackups.cpp
    ${_brulion_shell_dir}/workspaceextensions.h ${_brulion_shell_dir}/workspaceextensions.cpp
    ${_brulion_shell_dir}/extensionviewmodel.h ${_brulion_shell_dir}/extensionviewmodel.cpp
    ${_brulion_shell_dir}/extensionmodels.h ${_brulion_shell_dir}/extensionmodels.cpp
    ${_brulion_shell_dir}/workspacesync.h ${_brulion_shell_dir}/workspacesync.cpp
    ${_brulion_shell_dir}/workspacesyncruns.cpp
    ${_brulion_shell_dir}/workspacegithub.h ${_brulion_shell_dir}/workspacegithub.cpp
    ${_brulion_shell_dir}/workspaceintegration.h ${_brulion_shell_dir}/workspaceintegration.cpp
    # The views' C++ (qml/views/*.qml)
    ${_brulion_shell_dir}/cleanupselection.h ${_brulion_shell_dir}/cleanupselection.cpp
)
# The documentation (guide/<language>/*.md, Brulion notes about Brulion: en English, pl Polish) as
# :/guide/<language>/<name>.md; demovault.cpp (updateDocsVault) writes one language's pages at the
# root of the separate notebook "Brulion Documentation" (Polish: "Dokumentacja Brulionu") and keeps
# them up to date.
file(GLOB_RECURSE _brulion_guide_files CONFIGURE_DEPENDS ${_brulion_shell_dir}/guide/*.md)
qt_add_resources(brulion_shell brulion_guide
    PREFIX /guide
    BASE ${_brulion_shell_dir}/guide
    FILES ${_brulion_guide_files})
# Resources become sources of the target: lupdate (update_translations) would parse the pages as
# C++ and warn about their brackets.
set_property(TARGET brulion_shell APPEND PROPERTY QT_EXCLUDE_SOURCES_FROM_TRANSLATION ${_brulion_guide_files})
target_include_directories(brulion_shell PUBLIC ${_brulion_shell_dir})
target_link_libraries(brulion_shell
    PUBLIC Qt6::Core Qt6::Gui Qt6::Qml brulion_editorhost brulion_core_vault brulion_core_index brulion_core_markdown
           brulion_core_reminders brulion_core_dates brulion_core_log brulion_core_notebook brulion_core_backup
           brulion_core_extensions
    PRIVATE Qt6::Network brulion_core_appupdate brulion_core_util brulion_core_text brulion_core_sync brulion_core_io brulion_core_webpicture
            brulion_core_github
            brulion_core_drawing brulion_notify brulion_background
            brulion_ui_helpers) # folderchoices.cpp: TextHelper's fuzzy ranking
# Autostart ("Start when you sign in", Windows); AndroidBridge (permissions, test notification);
# WasmStorage + WasmNotifier; PDFs (workspacepdf.cpp: the thumbnails' cache of brulion_pdfview,
# src/app/pdf — its area is included before this one — and openexternal.h).
target_link_libraries(brulion_shell PRIVATE brulion_platform brulion_pdfview)
target_compile_definitions(brulion_shell PRIVATE BRULION_VERSION="${PROJECT_VERSION}"
    BRULION_UPDATE_CHECK=$<BOOL:${BRULION_UPDATE_CHECK}>)
qt_extract_metatypes(brulion_shell)
brulion_set_warnings(brulion_shell)

# --- The app: QML registration, desktop integration, QML files ------------------------------
target_sources(brulion PRIVATE shell/shellqml.h)
# The generated QML type registration includes the shell headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_shell_dir})
target_link_libraries(brulion PRIVATE brulion_shell)

if(BRULION_DESKTOP)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    # The UI driver (uidriver.h): real input and item finding for the self-test, the journey and
    # the remote control (tests/app/shell: tst_uidriver).
    qt_add_library(brulion_uidriver STATIC ${_brulion_shell_dir}/uidriver.h ${_brulion_shell_dir}/uidriver.cpp)
    target_include_directories(brulion_uidriver PUBLIC ${_brulion_shell_dir})
    target_link_libraries(brulion_uidriver PUBLIC Qt6::Core Qt6::Gui Qt6::Quick)
    brulion_set_warnings(brulion_uidriver)
    target_sources(brulion PRIVATE shell/desktop/desktoptray.h shell/desktop/desktoptray.cpp
        shell/shellselftest.h shell/shellselftest.cpp shell/shelljourney.h shell/shelljourney.cpp
        shell/remotecontrol.h shell/remotecontrol.cpp)
    # One Brulion per session, the messages of brulion-cli and the remote control's socket
    # (src/ipc); `brulion cli …` runs the command line (src/cli) inside the app's binary (the
    # AppImage's one entry point).
    target_link_libraries(brulion PRIVATE Qt6::Widgets brulion_ipc brulion_cli brulion_uidriver)
    target_compile_definitions(brulion PRIVATE BRULION_DESKTOP_INTEGRATION=1)
endif()

qt_target_qml_sources(brulion QML_FILES
    qml/shell/AppShell.qml
    qml/shell/ShellAboutDialog.qml
    qml/shell/ShellBackupSettings.qml
    qml/shell/ShellBottomNav.qml
    qml/shell/ShellContextDrawer.qml
    qml/shell/ShellExtensionQuestion.qml
    qml/shell/ShellExtensionView.qml
    qml/shell/ShellMoveDialog.qml
    qml/shell/ShellMoveWords.qml
    qml/shell/ShellNewPageDialog.qml
    qml/shell/ShellPageWindow.qml
    qml/shell/ShellQuickCapture.qml
    qml/shell/ShellRail.qml
    qml/shell/ShellRenameDialog.qml
    qml/shell/ShellRouteTitles.qml
    qml/shell/ShellRouteView.qml
    qml/shell/ShellSettingDividers.qml
    qml/shell/ShellSettingRow.qml
    qml/shell/ShellShareDialog.qml
    qml/shell/ShellShortcutSettings.qml
    qml/shell/ShellSidePanel.qml
    qml/shell/ShellSidebar.qml
    qml/shell/ShellSplash.qml
    qml/shell/ShellStatusBar.qml
    qml/shell/ShellSyncFooter.qml
    qml/shell/ShellTabBar.qml
    qml/shell/ShellTabPane.qml
    qml/shell/ShellTopBar.qml
    qml/shell/ShellTrayCalendar.qml
    qml/shell/ShellUpdateBanner.qml
)

include(${_brulion_shell_dir}/views.cmake)
