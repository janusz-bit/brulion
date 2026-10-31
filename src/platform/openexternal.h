// A file of the notebook handed to the system's app for it (docs/UI.md "PDF viewer": "Otwórz w innej
// aplikacji"; the browser build's PDFs open in a new tab). One implementation per build, chosen by
// src/platform/CMakeLists.txt:
//
//   desktop   QDesktopServices::openUrl(QUrl::fromLocalFile(path)): the default app (xdg-open,
//             ShellExecute) — openexternal_desktop.cpp
//   Android   ACTION_VIEW of a content:// URI of Brulion's FileProvider with a read grant; a file
//             outside the provider's roots (a memory card) copied into the cache first —
//             openexternal_android.cpp → AndroidBridge::viewFile → OpenWith.kt
//   browser   a new tab with a blob: URL of the bytes (typed explicitly: a file whose bytes are HTML
//             never becomes a page of Brulion's origin); a blocked tab → the bytes saved as a
//             download — wasm/wasmopen.cpp
//
// The caller checks what may be opened (Workspace::openInOtherApp: a PDF of the notebook, by its name
// and its "%PDF-" start): never a file a note names that could be a program.
#pragma once

#include <QByteArray>
#include <QString>

#include <functional>

namespace brulion::platform {

enum class OpenResult {
    Opened,  ///< handed over (Android: also when the file is being copied first)
    NoApp,   ///< no app on this device opens this type (Android)
    Blocked, ///< the browser blocked the new tab: the bytes were saved as a download instead
    Failed,
};

/// The file `absolutePath` (type `mimeType`, shown as `displayName`) handed to the system's app.
/// The browser: `bytes` are the file's (IDBFS cannot be shown to the browser's viewer otherwise) —
/// this MUST run inside a click / tap / key handler, browsers open tabs only on a user's gesture.
OpenResult openFileExternally(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                              const QByteArray &bytes = {});

/// Tests and self-tests: the handover replaced (records instead of launching apps); {} restores it.
void setOpenFileHook(std::function<OpenResult(const QString &path, const QString &mimeType)> hook);

namespace detail {
/// The platform's handover (one of openexternal_*.cpp, wasm/wasmopen.cpp).
OpenResult openFileExternallyNow(const QString &absolutePath, const QString &mimeType, const QString &displayName,
                                 const QByteArray &bytes);
} // namespace detail

} // namespace brulion::platform
