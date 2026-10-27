// The app's identity on Windows for notifications and the taskbar (docs/WINDOWS.md
// "Powiadomienia"): the AppUserModelID JanuszBit.Brulion — the same in the installer's Start menu
// shortcut (packaging/windows/brulion.iss) — registered per user under
// HKCU\Software\Classes\AppUserModelId\<id> with the name and icon a toast shows. That is how an
// app without a package shows toasts (WindowsToastBus, src/app/notify/windowstoastbus.h), the
// portable ZIP too. A separate profile (BRULION_PROFILE, src/ipc/profile.h) has an id of its own,
// so tests never touch the real one. The pure parts run on every platform (tests/platform).
#pragma once

#include <QString>

class QSettings;

namespace brulion::platform {

/// "JanuszBit.Brulion", or "JanuszBit.Brulion.<profile>" for the organisation "Brulion-<profile>".
QString appUserModelId(const QString &organizationName);
/// The id of this process (QCoreApplication::organizationName()).
QString appUserModelId();

/// Writes the registration of `id` into `classes` (the AppUserModelId key; an INI file in the
/// tests): DisplayName, and IconUri when `iconPath` is not empty — only what differs from what is
/// there (a portable ZIP moved gets its new icon path). False when it could not be written.
bool registerAppUserModelId(QSettings &classes, const QString &id, const QString &displayName,
                            const QString &iconPath);

/// Windows: this process's AppUserModelID and its per-user registration (icon: brulion.ico next
/// to the program). Call right after the application's names are set. Nothing elsewhere.
void applyAppIdentity();

} // namespace brulion::platform
