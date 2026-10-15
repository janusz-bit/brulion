// File exchange for import and export: reading what the user picked (files or a whole folder,
// local paths or Android content:// documents) into io::InputFile values, and writing exported
// bytes to a picked destination.
//
//   FileGatherer   reads picked URLs from the event loop in small chunks (progress, cancel), so
//                  a big folder never freezes the window; folders become "<folder name>/<path>"
//                  entries (importers recognise vault roots such as "Vault/.obsidian/…"); junk
//                  and other apps' internals that never hold notes (.git, node_modules, .trash,
//                  .stversions) are skipped; limits per file / in total / number of files give
//                  warnings instead of exhausting memory.
//   writeToUrl()   atomic write to a local file (QSaveFile) or a write to a content:// document
//                  (Android "Save as" through the system's document picker).
//   Browser        WebAssembly has no file system the user can see: QFileDialog::
//                  getOpenFileContent() / saveFileContent() (in the controllers) instead.
#pragma once

#include "io/files.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace brulion::ioviews {

/// Limits of one pick.
struct GatherLimits {
    qint64 maxFileBytes = 512LL * 1024 * 1024;   ///< larger files are skipped (warning)
    qint64 maxTotalBytes = 1536LL * 1024 * 1024; ///< reading stops here (warning)
    int maxFiles = 50000;                        ///< reading stops here (warning)
};

/// "Eksport Notion.zip" for a file URL, "Mój sejf" for a folder; the display name of an
/// Android document (content://…) as far as Qt knows it, else the last path segment.
QString displayNameOf(const QUrl &url);

/// The file system path for QFile: a local path, or the URL itself for content:// documents.
QString fileSystemPathOf(const QUrl &url);

/// Folders whose content is never imported when a folder is picked.
bool isSkippedFolder(const QString &name);

/// Write `data` to a picked destination. Local files are replaced atomically. False with
/// `error` (translated) on failure.
bool writeToUrl(const QUrl &url, const QByteArray &data, QString *error = nullptr);

class FileGatherer : public QObject {
    Q_OBJECT
public:
    explicit FileGatherer(QObject *parent = nullptr);

    void setLimits(const GatherLimits &limits) { m_limits = limits; }
    /// Files read per event loop turn (default 32; also bounded by bytes per turn).
    void setChunkSize(int files) { m_chunk = qMax(1, files); }

    /// Start reading `urls` (files, or folders: read recursively). A running gather is cancelled.
    void start(const QList<QUrl> &urls);
    void cancel();
    bool isRunning() const { return m_running; }

Q_SIGNALS:
    /// `done` of `total` files read so far.
    void progress(int done, int total);
    /// Everything read: the files, warnings (translated), a name for the source ("Eksport.zip",
    /// "Sejf", "3 files") and the number of bytes.
    void finished(const QList<brulion::io::InputFile> &files, const QStringList &warnings, const QString &sourceName,
                  qint64 bytes);
    void cancelled();

private:
    struct Pending {
        QString fsPath;   ///< for QFile
        QString relative; ///< InputFile::path
        qint64 size = 0;
    };
    void collect(const QUrl &url);
    void step();

    GatherLimits m_limits;
    int m_chunk = 32;
    bool m_running = false;
    QList<Pending> m_pending;
    qsizetype m_next = 0;
    QList<io::InputFile> m_files;
    QStringList m_warnings;
    QString m_sourceName;
    qint64 m_bytes = 0;
    bool m_stopped = false;
    QTimer m_timer;
};

} // namespace brulion::ioviews
