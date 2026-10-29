#pragma once

#include <QByteArray>
#include <QString>

namespace brulion::platform {

/// "linux", "windows", "android" or "wasm" — the target this binary was built for.
QString targetName();

/// Folder of the event log files (brulion.log, brulion.log.1): Linux QStandardPaths::
/// StateLocation + "/logs" ($XDG_STATE_HOME/Brulion/Brulion/logs, ~/.local/state/… by default),
/// Windows the same (%LOCALAPPDATA%\Brulion\Brulion\State\logs), Android the app's files dir
/// (AppLocalDataLocation) + "/logs" — under filesDir, so Qt's FileProvider can share a copy —, the
/// browser "" (the log lives in memory only: a file there would be written back to IndexedDB all
/// the time).
QString logDirectory();

/// The device for the log's session line and export header: "Android 15, arm64", "NixOS 26.05
/// (Linux 6.18), x86_64", "WebAssembly (wasm32)". No user or host names.
QString deviceDescription();

/// A second launch is about to hand its request to the running Brulion (main.cpp): on Windows
/// that process may then take the foreground this one was given (AllowSetForegroundWindow:
/// Explorer's "Otwórz za pomocą" brings the running window to the front, not just flashes it).
/// Nothing elsewhere (Wayland: the activation token goes with the message, ipc/appmessage.h).
void allowForegroundActivation();

/// A second launch handed its request to the running Brulion and ends (main.cpp): on X11 the
/// launcher's startup notification (the desktop entry's StartupNotify=true) would keep its busy
/// cursor until its timeout, waiting for a window this process never shows. Qt's xcb plugin ends
/// it ("remove: ID=…") once the process shows a top-level window: a 1×1 tooltip window outside the
/// screen — never managed by the window manager, never painted — shown and closed at once. Only on
/// X11 and when `startupId` (DESKTOP_STARTUP_ID, read before the application object took it) is
/// set; nothing elsewhere (Wayland: the token goes with the message and ends it there).
void endStartupNotification(const QByteArray &startupId);

} // namespace brulion::platform
