# The background check of the notes (library brulion_background, src/app/background; Android runs
# it in a process of its own while Brulion is closed): its target's JSON (the Kotlin side keeps it),
# the reminders it computes — the very set the app's ReminderScheduler hands Android —, a check of a
# notebook folder that is missing, empty or full of reminders, and one with a Nextcloud sync against
# the in-memory WebDAV server of the sync tests (a reminder written on another device arrives; the
# app syncing at that moment only skips the sync).
if(TARGET brulion_background AND TARGET brulion_sync_testsupport)
    brulion_add_test(tst_background SOURCES tst_background.cpp
        LIBS brulion_background brulion_sync_testsupport brulion_core_index brulion_core_vault brulion_core_notebook)
endif()
