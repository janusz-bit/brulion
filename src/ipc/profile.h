// The app's names on the desktop, shared by the app and brulion-cli: QCoreApplication's
// organisation and application name decide where the settings, the app data (History, LocalStore,
// the event log) and the single-instance socket are (QStandardPaths, QSettings, SingleInstance).
//
// BRULION_PROFILE=<name> (letters, digits, "-", "_"; at most 32) gives a separate profile: the
// organisation "Brulion-<name>" instead of "Brulion", so nothing of the user's own settings and
// notebooks list is read or written. For tests and trying things out on Windows, where the XDG_*
// variables that the Linux tests set do not move QStandardPaths (docs/WINDOWS.md). Another value is
// ignored (the default profile). `--profile <name>` on the command line of the app and of
// brulion-cli is the same (it wins over the variable).
//
// Test runs keep out of the user's own profile by themselves: a run of the app that only tests or
// photographs it (--selftest-*, --journey, --screenshot) without a home of its own — no
// --profile, no BRULION_PROFILE, and XDG_CONFIG_HOME unset or ~/.config — gets the profile
// "test" (chooseProfile()), so its notebooks never land among the user's recent notebooks.
#pragma once

#include <QString>
#include <QStringList>

namespace brulion::ipc {

/// "Brulion", or "Brulion-<name>" for a valid BRULION_PROFILE.
QString organizationName();

/// `name` can name a profile: letters, digits, "-", "_", 1 to 32 of them.
bool isValidProfileName(const QString &name);

/// The value of `--profile <name>` / `--profile=<name>` among `arguments` (the options before a
/// lone "--open": the files after it are no options); "" without one.
QString profileArgument(const QStringList &arguments);

/// The profile a run uses and why.
struct ProfileChoice {
    QString profile;      ///< "" = the default profile
    bool implied = false; ///< a test run's own "test", nobody asked for it
    QString error;        ///< `--profile` with a name that cannot be one (the run should not start)
};

/// The profile of a run with `arguments`: `--profile`, else `environmentProfile` (BRULION_PROFILE,
/// when valid), else "test" for a test run (--selftest-*, --journey, --screenshot) whose
/// `xdgConfigHome` is unset or the user's own (`home` + "/.config"), else the default.
ProfileChoice chooseProfile(const QStringList &arguments, const QString &environmentProfile,
                            const QString &xdgConfigHome, const QString &home);

/// The app at start, before applyAppNames(): chooseProfile() for this process (its environment)
/// and BRULION_PROFILE set to it, so the names follow and so do the programs it starts. The
/// returned choice says what was chosen (its `error`: do not start).
ProfileChoice applyProfile(const QStringList &arguments);

/// Sets QCoreApplication's organisation name (organizationName()), application name ("Brulion")
/// and organisation domain. Call right after the application object exists.
void applyAppNames();

} // namespace brulion::ipc
