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
    # Windows: qml-smoke.sh is a bash script (not run there; the self-tests below are).
    if(NOT WIN32)
        add_test(NAME tst_appio_qml
            COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/qml-smoke.sh $<TARGET_FILE:brulion> ${CMAKE_CURRENT_BINARY_DIR}/qml-smoke)
        set_tests_properties(tst_appio_qml PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root}"
            TIMEOUT 300 RUN_SERIAL TRUE)
    endif()

    # The ways to the import screen and an import in the real app (`--selftest-import`,
    # src/app/io/ioselftest.h): onboarding's "Import notes" with a new notebook, the empty
    # journal's button, the drawer and the "More" menu, the phone's steps per app, a vault typed
    # as a path → preview → import → report → "Open the imported folder" — on a phone with finger
    # taps, on the desktop with mouse clicks. Fresh homes (the run's notebooks live in a temporary
    # folder of the test itself).
    if(TARGET brulion AND BRULION_DESKTOP)
        set(_import_homes ${CMAKE_CURRENT_BINARY_DIR}/import-selftest-homes)
        set(_import_remove ${_import_homes})
        foreach(_kind phone desktop)
            brulion_test_profile(_profile import-${_kind})
            list(APPEND _import_remove ${_profile_DIRS})
        endforeach()
        add_test(NAME tst_appio_import_qml_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_import_remove})
        set_tests_properties(tst_appio_import_qml_homes PROPERTIES FIXTURES_SETUP appio_import_homes)
        add_test(NAME tst_appio_import_qml
            COMMAND brulion --onboarding --selftest-import --no-single-instance
                --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
        add_test(NAME tst_appio_import_qml_desktop
            COMMAND brulion --onboarding --selftest-import --no-single-instance
                --size 1280x800 --now "2026-10-14 10:00" --locale en)
        math(EXPR _import_timeout "120 * ${BRULION_TEST_SLOWDOWN}")
        foreach(_pair "tst_appio_import_qml;phone" "tst_appio_import_qml_desktop;desktop")
            list(GET _pair 0 _test)
            list(GET _pair 1 _kind)
            set(_home ${_import_homes}/${_kind})
            brulion_test_profile(_profile import-${_kind})
            set_tests_properties(${_test} PROPERTIES
                ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state;BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN}${_profile}"
                FIXTURES_REQUIRED appio_import_homes TIMEOUT ${_import_timeout} RUN_SERIAL TRUE
                FAIL_REGULAR_EXPRESSION "selftest: FAIL;TypeError;ReferenceError")
        endforeach()
    endif()
endif()
