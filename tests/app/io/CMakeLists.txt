# Tests of the sync and import/export controllers (library brulion_ioviews, src/app/io): the
# import collision policy and preview, reading picked folders, export scopes and saving, the
# Nextcloud form (validation, connection test against the core's in-memory WebDAV server, saving
# into AppSettings), the .stignore helpers and conflict resolution with a version-history base.
# Real Workspaces over QTemporaryDir vaults; no network.
brulion_add_test(tst_appio_import SOURCES tst_appio_import.cpp
    LIBS brulion_ioviews brulion_core_io brulion_core_vault)
brulion_add_test(tst_appio_export SOURCES tst_appio_export.cpp
    LIBS brulion_ioviews brulion_core_io brulion_core_vault)
brulion_add_test(tst_appio_conflicts SOURCES tst_appio_conflicts.cpp
    LIBS brulion_ioviews brulion_core_sync brulion_core_vault)
if(TARGET brulion_sync_testsupport)
    brulion_add_test(tst_appio_syncsetup SOURCES tst_appio_syncsetup.cpp
        LIBS brulion_ioviews brulion_sync_testsupport brulion_core_vault)
endif()

# The views in the real app (qml-smoke.sh): SyncView (overview, Syncthing, Nextcloud with an
# unreachable server, conflicts with a resolver) and ImportExportView (import with a picked
# folder and its preview, export) at desktop and phone sizes; QML warnings from these files fail.
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND NOT ANDROID AND NOT EMSCRIPTEN AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    add_test(NAME tst_appio_qml
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
    set_tests_properties(tst_appio_qml PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
        TIMEOUT 300 RUN_SERIAL TRUE)
endif()
