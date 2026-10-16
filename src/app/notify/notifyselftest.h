// Sound settings self-test — `brulion --demo --selftest-sounds --size 412x915 --touch` (phone) or
// `--size 1280x800` (desktop): Settings → Reminders in the real QML app, headless, with real input
// (finger taps with --touch, mouse clicks otherwise):
//   1. "Silent" for reminders → stored in AppSettings, the notifier gets it, "Play sample" off;
//   2. "Ringtone" with none chosen yet → the sound menu opens, the mode stays;
//   3. a theme sound from "Choose ringtone…" (alarms) → custom, stored, shown in the summary;
//   4. "Silent", then "Ringtone" again → the chosen sound comes back without the menu;
//   5. "Default" → "Play sample" asks the notifier to play it.
// Prints "selftest: PASS|FAIL <what>" lines; the exit code is the number of failures. Run by
// ctest as tst_notify_sounds_qml* (tests/app/notify/CMakeLists.txt). Desktop builds only (nested
// event processing).
#pragma once

class QQmlApplicationEngine;

namespace brulion::shell {
class AppController;
}

namespace brulion::notify {

/// Start the self-test once the notebook is open; quits the application with the failure count.
void startSoundsSelfTest(QQmlApplicationEngine *engine, shell::AppController *app);

} // namespace brulion::notify
