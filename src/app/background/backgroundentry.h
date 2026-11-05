// Android only: main() of the background check's process (docs/ANDROID.md "Sprawdzanie notatek w
// tle"). BackgroundCheckService.kt (a QtService in the process ":background") starts Brulion's
// library with the argument BackgroundCheckArgument; main() hands over to runBackgroundCheck()
// before anything of the app exists — a QAndroidService instead of the QGuiApplication, no window,
// no QML, no AppSettings.
#pragma once

namespace brulion::background {

/// The argument of the service's `android.app.arguments` (android/AndroidManifest.xml).
inline constexpr char BackgroundCheckArgument[] = "--background-check";

/// Runs one Check (backgroundcheck.h) on the target the Kotlin side keeps and hands the result to
/// it (AndroidBridge::backgroundCheckFinished()); returns the exit code.
int runBackgroundCheck(int argc, char *argv[]);

} // namespace brulion::background
