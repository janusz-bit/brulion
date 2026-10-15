# Cross-module tests of the core: end-to-end scenarios on a realistic Polish vault (vault, index,
# queries, calc, calendar, notifications, block editor, export/import, two syncing devices) and
# consistency of the syntax rules (tags, links, tasks, reminders) between the modules that
# implement them. The sync test support library (in-memory WebDAV server) comes from
# tests/core/sync. QTEST_MAIN with the offscreen platform (io uses QtGui).
brulion_add_test(tst_integration SOURCES tst_integration.cpp integrationtest.h
    LIBS brulion_core_io brulion_core_reminders brulion_core_calc brulion_sync_testsupport)
brulion_add_test(tst_integration_consistency SOURCES tst_integration_consistency.cpp
    LIBS brulion_core_io brulion_core_reminders brulion_core_sync)
