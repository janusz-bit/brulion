# The background check of the notes (Android, docs/ANDROID.md "Sprawdzanie notatek w tle"; included
# by src/app/CMakeLists.txt in its scope, so relative paths are relative to src/app):
#
#   all builds   the static library brulion_background — Target (what to check, its JSON for the
#                Kotlin side), upcomingFor() (the reminders exactly as the app hands them to
#                Android) and Check (open the notebook, an optional Nextcloud sync, the reminders);
#                built everywhere so the desktop tests cover it (tests/app/background)
#   Android      runBackgroundCheck(), the main() of the check's own process (backgroundentry.h),
#                compiled into the app's library: BackgroundCheckService.kt starts it with
#                --background-check
#
# The Kotlin side: android/src/io/github/januszbit/brulion/android/BackgroundCheck*.kt.

set(_brulion_background_dir ${CMAKE_CURRENT_LIST_DIR})

qt_add_library(brulion_background STATIC
    ${_brulion_background_dir}/backgroundcheck.h ${_brulion_background_dir}/backgroundcheck.cpp
)
target_include_directories(brulion_background PUBLIC ${_brulion_background_dir})
target_link_libraries(brulion_background
    PUBLIC Qt6::Core brulion_core_reminders
    PRIVATE brulion_notify brulion_core_notebook brulion_core_index brulion_core_vault brulion_core_sync
            brulion_core_log)
brulion_set_warnings(brulion_background)

if(ANDROID)
    # QAndroidService (qandroidextras_p.h): the application object of a Qt service.
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    target_sources(brulion PRIVATE ${_brulion_background_dir}/backgroundentry.h
        ${_brulion_background_dir}/backgroundentry_android.cpp)
    target_link_libraries(brulion PRIVATE brulion_background brulion_shell brulion_platform brulion_core_sync
        brulion_core_log Qt6::CorePrivate)
endif()
