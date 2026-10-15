# Notifier adapters (library brulion_notify, src/app/notify): the freedesktop message built for a
# reminder (actions, hints, escaping), DesktopNotifier against a fake notification bus (actions,
# snooze, open, ringing alarms, the tray fallback), the JSON handed to Android's alarm scheduler
# and the platform factory. No session bus or device needed.
brulion_add_test(tst_notify SOURCES tst_notify.cpp LIBS brulion_notify)
