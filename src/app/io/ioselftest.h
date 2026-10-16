// Import self-test — `brulion --onboarding --selftest-import --size 412x915 --touch` (phone) or
// `--size 1280x800` (desktop): the ways to the import screen and an import, in the real QML
// app, headless, with real input (finger taps with --touch, mouse clicks otherwise):
//   1. onboarding: "Import notes" with a new notebook folder → that notebook opens on the import tab;
//   2. the empty journal's "Import notes" button → the import tab;
//   3. phones: the drawer's "Import notes" → the import tab, the drawer closed;
//   4. the top bar's "More" menu: "Import notes" → the import tab;
//   5. with an app chosen the phone's steps for it show (phones only);
//   6. an Obsidian vault (Polish names) typed as a path → the preview counts it, nothing written
//      yet → "Import" → the report → "Open the imported folder" → All pages in Import/Obsidian.
// Prints "selftest: PASS|FAIL <what>" lines; the exit code is the number of failures. Run by
// ctest as tst_appio_import_qml* (tests/app/io/CMakeLists.txt). Desktop builds only (nested
// event processing).
#pragma once

class QQmlApplicationEngine;

namespace brulion::shell {
class AppController;
}

namespace brulion::ioviews {

/// Start the self-test once the app shows onboarding; quits the application with the failure count.
void startImportSelfTest(QQmlApplicationEngine *engine, shell::AppController *app);

} // namespace brulion::ioviews
