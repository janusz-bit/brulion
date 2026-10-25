# Notifier adapters (library brulion_notify, src/app/notify): the freedesktop message built for a
# reminder (actions, hints, escaping, the sound hints of every choice), DesktopNotifier against a
# fake notification bus (actions, snooze, open, ringing alarms with the chosen sound, sound
# samples, the tray fallback), the JSON handed to Android's alarm scheduler and sound channels,
# and the platform factory. No session bus or device needed.
brulion_add_test(tst_notify SOURCES tst_notify.cpp LIBS brulion_notify)
# The sound settings of reminders and alarms (ReminderSounds): stored choices read back (a map and
# a real INI file), the modes each platform offers, the remembered ringtone, the system picker's
# answers, previews, the system settings.
brulion_add_test(tst_notify_sounds SOURCES tst_notify_sounds.cpp LIBS brulion_notify)

# Settings → Reminders in the real app (`--selftest-sounds`, src/app/notify/notifyselftest.h):
# "Silent" stored and shown, "Ringtone" without one opens the sound menu, a theme sound chosen,
# the ringtone back after "Silent", "Play sample" — on a phone with finger taps, on the desktop
# with mouse clicks. Fresh homes (settings and the sample notebook of the run).
get_filename_component(_brulion_qt_qml_root "${Qt6Qml_DIR}/../../../${QT6_INSTALL_QML}" ABSOLUTE)
if(TARGET brulion AND BRULION_DESKTOP
   AND EXISTS "${_brulion_qt_qml_root}/QtQuick/Controls")
    set(_sounds_homes ${CMAKE_CURRENT_BINARY_DIR}/sounds-selftest-homes)
    set(_sounds_remove ${_sounds_homes})
    foreach(_kind phone desktop)
        brulion_test_profile(_profile sounds-${_kind})
        list(APPEND _sounds_remove ${_profile_DIRS})
    endforeach()
    add_test(NAME tst_notify_sounds_qml_homes COMMAND ${CMAKE_COMMAND} -E rm -rf ${_sounds_remove})
    set_tests_properties(tst_notify_sounds_qml_homes PROPERTIES FIXTURES_SETUP notify_sounds_homes)
    add_test(NAME tst_notify_sounds_qml
        COMMAND brulion --demo --selftest-sounds --no-single-instance
            --size 412x915 --touch --now "2026-10-14 10:00" --locale pl)
    add_test(NAME tst_notify_sounds_qml_desktop
        COMMAND brulion --demo --selftest-sounds --no-single-instance
            --size 1280x800 --now "2026-10-14 10:00" --locale en)
    math(EXPR _sounds_timeout "90 * ${BRULION_TEST_SLOWDOWN}")
    foreach(_pair "tst_notify_sounds_qml;phone" "tst_notify_sounds_qml_desktop;desktop")
        list(GET _pair 0 _test)
        list(GET _pair 1 _kind)
        set(_home ${_sounds_homes}/${_kind})
        brulion_test_profile(_profile sounds-${_kind})
        # No session bus: "Play sample" must not reach the desktop of whoever runs the tests.
        set_tests_properties(${_test} PROPERTIES
            ENVIRONMENT "BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN};QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_FORCE_STDERR_LOGGING=1;QML_IMPORT_PATH=${_brulion_qt_qml_root};XDG_DATA_HOME=${_home}/data;XDG_CONFIG_HOME=${_home}/config;XDG_CACHE_HOME=${_home}/cache;XDG_STATE_HOME=${_home}/state;DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/brulion-test-bus;BRULION_TEST_SLOWDOWN=${BRULION_TEST_SLOWDOWN}${_profile}"
            FIXTURES_REQUIRED notify_sounds_homes TIMEOUT ${_sounds_timeout} RUN_SERIAL TRUE
            FAIL_REGULAR_EXPRESSION "selftest: FAIL;TypeError;ReferenceError")
    endforeach()
endif()
