# Notifier adapters (included by src/app/CMakeLists.txt): the static library brulion_notify —
# reminders::NotificationSink implementations for each platform (notifier.h). The shell's
# reminders integration (shell/workspacereminders.h, shell/workspaceintegration.h) creates the
# platform's notifier with createPlatformNotifier() and hands it to the Workspace.
#
#   all builds      notifier (base + factory), desktopnotifier (pure message builder, the
#                   NotificationBus seam, the tray fallback), androidpayload (JSON for
#                   AndroidBridge — built everywhere so the desktop tests cover it),
#                   soundchoice (the sound of reminders and alarms, freedesktop sound hints),
#                   remindersounds (ReminderSounds: the sound settings, QML singleton through
#                   notifyqml.h; QML: qml/notify/ReminderSoundSetting.qml), toastxml (the Windows
#                   toast XML and its activation arguments — built everywhere for the tests)
#   Linux desktop   dbusnotificationbus (org.freedesktop.Notifications over QtDBus)
#   Windows         windowstoastbus (toast notifications through C++/WinRT, WindowsApp.lib)
#   Android         androidnotifier (AndroidBridge::scheduleReminders)
#   WebAssembly     wasmnotifiersink (platform::WasmNotifier)
# Desktop builds also get the settings self-test of the real app (`--selftest-sounds`,
# notifyselftest.h). Tests: tests/app/notify.

set(_brulion_notify_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_notify STATIC
    ${_brulion_notify_dir}/notifier.h ${_brulion_notify_dir}/notifier.cpp
    ${_brulion_notify_dir}/desktopnotifier.h ${_brulion_notify_dir}/desktopnotifier.cpp
    ${_brulion_notify_dir}/androidpayload.h ${_brulion_notify_dir}/androidpayload.cpp
    ${_brulion_notify_dir}/soundchoice.h ${_brulion_notify_dir}/soundchoice.cpp
    ${_brulion_notify_dir}/remindersounds.h ${_brulion_notify_dir}/remindersounds.cpp
    ${_brulion_notify_dir}/toastxml.h ${_brulion_notify_dir}/toastxml.cpp
)
target_include_directories(brulion_notify PUBLIC ${_brulion_notify_dir})
target_link_libraries(brulion_notify PUBLIC Qt6::Core brulion_core_reminders)

if(ANDROID)
    target_sources(brulion_notify PRIVATE
        ${_brulion_notify_dir}/androidnotifier.h ${_brulion_notify_dir}/androidnotifier.cpp)
    target_link_libraries(brulion_notify PRIVATE brulion_platform)
elseif(EMSCRIPTEN)
    target_sources(brulion_notify PRIVATE
        ${_brulion_notify_dir}/wasmnotifiersink.h ${_brulion_notify_dir}/wasmnotifiersink.cpp)
    target_link_libraries(brulion_notify PRIVATE brulion_platform)
elseif(WIN32)
    # C++/WinRT: the headers of the Windows SDK (cppwinrt), WindowsApp.lib; the AppUserModelID
    # from brulion_platform (appidentity.h).
    target_sources(brulion_notify PRIVATE
        ${_brulion_notify_dir}/windowstoastbus.h ${_brulion_notify_dir}/windowstoastbus.cpp)
    target_link_libraries(brulion_notify PRIVATE brulion_platform brulion_core_log WindowsApp)
    target_compile_definitions(brulion_notify PRIVATE BRULION_NOTIFY_HAVE_WINTOAST=1)
elseif(UNIX AND NOT APPLE)
    # GLOBAL: the tests (tests/app/...) link brulion_notify from another directory tree and must
    # see the Qt6::DBus target too.
    find_package(Qt6 QUIET COMPONENTS DBus GLOBAL)
    if(TARGET Qt6::DBus)
        target_sources(brulion_notify PRIVATE
            ${_brulion_notify_dir}/dbusnotificationbus.h ${_brulion_notify_dir}/dbusnotificationbus.cpp)
        target_link_libraries(brulion_notify PRIVATE Qt6::DBus)
        target_compile_definitions(brulion_notify PRIVATE BRULION_NOTIFY_HAVE_DBUS=1)
    else()
        message(STATUS "Qt DBus not found: desktop reminders use the system tray only")
    endif()
endif()

brulion_set_warnings(brulion_notify)
qt_extract_metatypes(brulion_notify)

# --- The app: QML registration (ReminderSounds), the settings component, the self-test --------
target_sources(brulion PRIVATE notify/notifyqml.h)
# The generated QML type registration includes the notify headers by file name.
target_include_directories(brulion PRIVATE ${_brulion_notify_dir})
target_link_libraries(brulion PRIVATE brulion_notify)
qt_target_qml_sources(brulion QML_FILES qml/notify/ReminderSoundSetting.qml)
if(BRULION_DESKTOP)
    target_sources(brulion PRIVATE ${_brulion_notify_dir}/notifyselftest.h ${_brulion_notify_dir}/notifyselftest.cpp)
endif()
