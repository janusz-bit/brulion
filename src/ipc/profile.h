// The app's names on the desktop, shared by the app and brulion-cli: QCoreApplication's
// organisation and application name decide where the settings, the app data (History, LocalStore,
// the event log) and the single-instance socket are (QStandardPaths, QSettings, SingleInstance).
//
// BRULION_PROFILE=<name> (letters, digits, "-", "_"; at most 32) gives a separate profile: the
// organisation "Brulion-<name>" instead of "Brulion", so nothing of the user's own settings and
// notebooks list is read or written. For tests and trying things out on Windows, where the XDG_*
// variables that the Linux tests set do not move QStandardPaths (docs/WINDOWS.md). Another value is
// ignored (the default profile).
#pragma once

#include <QString>

namespace brulion::ipc {

/// "Brulion", or "Brulion-<name>" for a valid BRULION_PROFILE.
QString organizationName();

/// Sets QCoreApplication's organisation name (organizationName()), application name ("Brulion")
/// and organisation domain. Call right after the application object exists.
void applyAppNames();

} // namespace brulion::ipc
