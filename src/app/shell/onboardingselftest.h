// Start screen self-test — `brulion --onboarding --selftest-onboarding --size 1280x800` (desktop)
// or `--size 412x915 --touch` (phone): the start screen (OnboardingView) in the real QML app,
// headless, with real input (finger taps with --touch, mouse clicks and keys otherwise):
//   1. recent notebooks: two that exist, one on a drive that is not plugged in and one of a
//      temporary folder that is gone → three rows, the missing one marked "Folder not found"
//      (with "Remove from list" on wide windows, its × on phones), the temporary one dropped;
//   2. the layout: on the desktop two columns (the recent notebooks left of the start card) and
//      nothing to scroll at 1280×800; on the phone one column, the recent notebooks above it;
//   3. a click / tap on the missing notebook: a warning, nothing created, it stays marked;
//   4. "Remove from list" → its row goes, the others stay;
//   5. more notebooks: the phone shows five and "Show all (7)", which shows the rest; the desktop
//      shows every row;
//   6. the one primary action, "Open notebook", with a new folder typed: the folder is created
//      and opens; closed again, the start screen lists it first — and on the desktop the keyboard
//      starts on it (Down moves to the next row, Delete removes that one and the focus moves on,
//      Tab leaves the list — one tab stop — for the folder field).
// Prints "selftest: PASS|FAIL <what>" lines; the exit code is the number of failures. Run by ctest
// as tst_shell_onboarding_qml* (tests/app/shell/CMakeLists.txt). Desktop builds only (nested
// event processing, the UI driver).
#pragma once

class QQmlApplicationEngine;

namespace brulion::shell {

class AppController;

/// Start the self-test once the app shows the start screen; quits the app with the failure count.
void startOnboardingSelfTest(QQmlApplicationEngine *engine, AppController *app);

} // namespace brulion::shell
