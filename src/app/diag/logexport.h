// The exported event log: its header (what it contains and what it never contains), the file
// name and the clipboard cap. Pure functions (unit-tested in tests/app/diag); translated texts.
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace brulion::diag {

/// What the header says about this app and device.
struct ExportInfo {
    QString version;         ///< "0.2.1"
    QString device;          ///< platform::deviceDescription()
    QString qtVersion;       ///< "6.11.2"
    QDateTime created;       ///< local time
    bool detailed = false;   ///< the detailed log is on
    QStringList recentSyncs; ///< one line per recent sync run (WorkspaceSync::historyLines())
};

/// The lines that start every export (translated):
///   Brulion — rejestr zdarzeń
///   Wersja 0.2.1 · Android 15, arm64 · Qt 6.11.2
///   Utworzono 2026-10-18 14:05:12 (UTC+02:00) · szczegółowość: normalna
///   Zawiera: … (server address and Nextcloud user, note names and paths, the notebook folder)
///   Nie zawiera: … (passwords, app passwords, tokens, cookies, note contents — removed)
///   Ostatnie synchronizacje: + the recentSyncs lines indented
QStringList exportHeader(const ExportInfo &info);

/// "brulion-rejestr-zdarzen-2026-10-18-1403.txt" (translated stem).
QString suggestedFileName(const QDateTime &now);

/// Android's clipboard takes about 1 MB per transaction; keep `text` within `maxBytes` (UTF-8):
/// the header lines up to the "----" line stay, the newest entries are kept, and a note says
/// that older ones were left out. Unchanged when it fits.
QString capForClipboard(const QString &text, qint64 maxBytes = 256 * 1024);

} // namespace brulion::diag
