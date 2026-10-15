# Notifier adapters (included by src/app/CMakeLists.txt): the static library brulion_notify —
# reminders::NotificationSink implementations for each platform (notifier.h). The shell's
# reminders integration (shell/workspacereminders.h, shell/workspaceintegration.h) creates the
# platform's notifier with createPlatformNotifier() and hands it to the Workspace.
#
#   all builds      notifier (base + factory), desktopnotifier (pure message builder, the
#                   NotificationBus seam, the tray fallback), androidpayload (JSON for
#                   AndroidBridge — built everywhere so the desktop tests cover it)
#   Linux desktop   dbusnotificationbus (org.freedesktop.Notifications over QtDBus)
#   Android         androidnotifier (AndroidBridge::scheduleReminders)
#   WebAssembly     wasmnotifiersink (platform::WasmNotifier)
# Tests: tests/app/notify.

set(_brulion_notify_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_notify STATIC
    ${_brulion_notify_dir}/notifier.h ${_brulion_notify_dir}/notifier.cpp
    ${_brulion_notify_dir}/desktopnotifier.h ${_brulion_notify_dir}/desktopnotifier.cpp
    ${_brulion_notify_dir}/androidpayload.h ${_brulion_notify_dir}/androidpayload.cpp
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
