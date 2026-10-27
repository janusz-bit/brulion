// Autostart — "Start when you sign in" (Settings → Application; docs/WINDOWS.md "Autostart"):
// Windows starts Brulion hidden in the tray at sign-in, so reminders ring from then on.
//
// The value "Brulion" of HKCU\Software\Microsoft\Windows\CurrentVersion\Run holds the command,
// `"C:\…\brulion.exe" --hidden` (the installer's task "autostart" writes the same). Task Manager →
// Startup apps may switch it off without removing it: it then writes a binary value of the same
// name under …\Explorer\StartupApproved\Run whose first byte is odd (0x03; 0x02 = on). The switch
// is on when the Run value starts this very brulion.exe and Task Manager has not switched it off;
// turning it on also clears Task Manager's "off" (the user asked for it explicitly).
//
// refresh() at start: an entry of Brulion's (a brulion.exe with --hidden) whose program no longer
// exists — the portable ZIP moved, an install folder gone — points to this brulion.exe instead.
// An entry for another existing brulion.exe (an installed copy next to a portable one) is left
// alone.
//
// Only on Windows and in the default profile: a Run entry cannot set BRULION_PROFILE, so in a
// separate profile (tests, trying things out) the switch is shown, but disabled. Elsewhere it is
// not offered (Linux: the NixOS module's option, nix/module.nix). The class works on any QSettings
// pair, so the tests run it on INI files on every platform.
#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

class QSettings;

namespace brulion::platform {

/// The Run value's command for `exePath`: the path with backslashes, in quotes, then " --hidden".
QString autostartCommand(const QString &exePath);
/// The program a Run command starts: its first word, or the quoted text at its start ("" when
/// empty or the quote is not closed).
QString autostartProgram(const QString &command);
/// `command` starts `exePath` (case-insensitive like NTFS, "/" and "\" alike, "." and ".." resolved).
bool autostartTargets(const QString &command, const QString &exePath);
/// `command` is one of Brulion's: a brulion.exe started with exactly --hidden.
bool isBrulionAutostartCommand(const QString &command);
/// Task Manager switched the entry off: the StartupApproved value's first byte is odd. An empty
/// value is not "off".
bool startupApprovedDisabled(const QByteArray &value);

class Autostart {
public:
    enum class Availability {
        Unsupported,     ///< not Windows: not offered
        SeparateProfile, ///< Windows with BRULION_PROFILE: shown, disabled
        Available,
    };

    /// The value's name in both keys.
    static QString valueName();

    /// Over `run` (the Run key) and `approved` (StartupApproved\Run), both outliving this object.
    Autostart(QSettings *run, QSettings *approved, const QString &exePath,
              Availability availability = Availability::Available);
    ~Autostart();

    /// This process: on Windows the registry and QCoreApplication::applicationFilePath()
    /// (SeparateProfile when `separateProfile`), Unsupported elsewhere.
    static std::unique_ptr<Autostart> forThisProcess(bool separateProfile);

    Availability availability() const { return m_availability; }
    /// The Run value starts this program and Task Manager has not switched it off.
    bool enabled() const;
    /// Writes or removes the Run value; false (and `error`) when not available or not written.
    bool setEnabled(bool on, QString *error = nullptr);
    /// Points Brulion's entry to this program when the one it starts is gone (`exists`: default
    /// QFileInfo::exists). True when it rewrote it.
    bool refresh(const std::function<bool(const QString &path)> &exists = {});
    /// The Run value ("" when there is none).
    QString command() const;

private:
    std::unique_ptr<QSettings> m_ownedRun;
    std::unique_ptr<QSettings> m_ownedApproved;
    QSettings *m_run = nullptr;
    QSettings *m_approved = nullptr;
    QString m_exePath;
    Availability m_availability;
};

} // namespace brulion::platform
