// Shell self-test — `brulion --demo --route journals --selftest-shell --size 412x915 --touch`:
// drives the real QML shell headlessly with key events and checks what the C++ unit tests
// cannot see: focus and key routing (the back key walks the history and is left unhandled at
// home, so Android may close the app; Escape and back with the drawer or the command palette
// open never act underneath them; the focus comes back to the shell after a popup closed), rows
// with context menus driven by real input (phones: finger taps and long presses through the
// drawer's page tree, All pages, tasks and the editor's bullets; desktops: left and right clicks),
// switching to the documentation notebook and back with the sidebar's notebook switcher, and
// closing the notebook from the UI. Prints one line per check (BRULION_SELFTEST_DEBUG=1 adds
// the focused item); the exit code is the number of failures. Run by ctest as tst_shell_qml
// (tests/app/shell/CMakeLists.txt). Desktop builds only (nested event processing).
#pragma once

class QQmlApplicationEngine;

namespace brulion::shell {

class AppController;

/// Start the self-test once the shell is ready; quits the application with the failure count.
void startShellSelfTest(QQmlApplicationEngine *engine, AppController *app);

} // namespace brulion::shell
