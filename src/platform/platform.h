#pragma once

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

} // namespace brulion::platform
