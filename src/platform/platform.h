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

/// The screen does not turn off by itself while `on` — Android: what the app does on screen only
/// (a model on the device being downloaded or answering: src/app/shell/localai.h) would stop with
/// it. Nothing elsewhere: a computer goes on working behind a dark screen.
void keepScreenOn(bool on);

/// An AI agent on another computer works for this device (docs/UI.md "AI agents (ACP)": a turn of
/// it runs, or one this device lost is fetched) — Android: a foreground service (AgentService.kt)
/// with a quiet ongoing notification of `title` and `text` and a button `stop` keeps the app's
/// process, its network and its event loop going with the screen off or another app in front, the
/// CPU too unless `waiting` (every turn waits on the user's answer: nothing comes meanwhile).
/// Called again it updates the notification; `title` "" ends it. The button comes back as
/// AndroidBridge::agentStopRequested(). Nothing elsewhere: a computer keeps the app running anyway.
void setAgentWork(const QString &title, const QString &text, const QString &stop, bool waiting);

/// The app lock's hold on the window (docs/UI.md "App lock") — Android: `secure` sets FLAG_SECURE
/// (nothing of the window on screenshots, screen recordings, a cast screen or the recent apps'
/// preview), `hideRecents` only leaves the recent apps' preview out (Android 13+:
/// setRecentsScreenshotEnabled(false); older Android needs `secure` for that). Kept for an activity
/// made anew. Nothing elsewhere.
void setSecureWindow(bool secure, bool hideRecents);

/// Android: the phone has a screen lock (PIN, pattern, password) or biometrics the app lock can ask
/// for (KeyguardManager.isDeviceSecure()). False elsewhere.
bool deviceAuthAvailable();

/// Android: asks for the phone's biometrics or screen lock (BiometricPrompt with `title`, `subtitle`
/// and the cancel button's `cancel`); the answer comes as AndroidBridge::deviceAuthFinished(). False
/// when it cannot be asked (not Android, no screen lock).
bool requestDeviceAuth(const QString &title, const QString &subtitle, const QString &cancel);

/// Copies decrypted text as sensitive (Android 13+: ClipDescription.EXTRA_IS_SENSITIVE — no preview
/// of the clipboard, no keyboard suggestion of it). False where the platform has no such copy: the
/// caller copies through QClipboard with its own hints.
bool copySensitiveText(const QString &text);

/// A second launch is about to hand its request to the running Brulion (main.cpp): on Windows
/// that process may then take the foreground this one was given (AllowSetForegroundWindow:
/// Explorer's "Open with" brings the running window to the front, not just flashes it).
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
