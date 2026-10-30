// File exchange for import and export: reading what the user picked (files or a whole folder,
// local paths or Android content:// documents) into io::InputFile values, and writing exported
// bytes to a picked destination.
//
//   FileGatherer      reads picked URLs: the listing (folders walked recursively) runs off the
//                     GUI thread, the files are read in small batches (also off the GUI thread,
//                     merged from the event loop: progress, cancel), so neither a big folder nor
//                     a slow document provider (Google Drive, an SD card) freezes the window — on
//                     Android a blocked GUI thread is an "app not responding" dialog. Folders
//                     become "<folder name>/<path>" entries (importers recognise vault roots such
//                     as "Vault/.obsidian/…"); junk and other apps' internals that never hold
//                     notes (.git, node_modules, .trash, .stversions) are skipped; limits per
//                     file / in total / number of files give warnings instead of exhausting memory.
//   DocumentProvider  how picked documents that are no local files are read: Android's Storage
//                     Access Framework hands out content:// URIs — a document (FilePicker.OpenFiles)
//                     or a document tree (FilePicker.OpenFolder). Android: the bridge's DocumentsContract
//                     listing (one query per folder), with Qt's content file engine (QFile / QDir
//                     on the URI) as the fallback; tests: an in-memory fake.
//   writeToUrl()      atomic write to a local file (QSaveFile) or a write to a content:// document
//                     (Android "Save as" through the system's document picker).
//   Browser           WebAssembly has no file system the user can see: the browser's own picker
//                     and downloads (platform/wasm/wasmfiles.h, in the controllers) instead.
// Pickers on the desktop and Android: FilePicker (src/app/ui/filepicker.h) in QML.
#pragma once

#include "io/files.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <memory>
#include <optional>

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

/// The file system path for QFile: a local path, or the URL itself (exactly as encoded) for
/// content:// documents.
QString fileSystemPathOf(const QUrl &url);

/// An Android document or document tree (content://…), read through a DocumentProvider.
bool isDocumentUrl(const QUrl &url);

/// What the user typed or a route passed: a local path ("/storage/emulated/0/Download/x.zip",
/// "~/Notatki" is not expanded) or a URL (file://, content://). Empty for blank input.
QUrl urlFromUserInput(const QString &pathOrUrl);

/// Folders whose content is never imported when a folder is picked.
bool isSkippedFolder(const QString &name);

/// Write `data` to a picked destination. Local files are replaced atomically. False with
/// `error` (translated) on failure.
bool writeToUrl(const QUrl &url, const QByteArray &data, QString *error = nullptr);

/// Reads picked documents that are no local files (content:// URIs). Every function may run on
/// a worker thread at the same time as others: implementations keep no mutable state.
class DocumentProvider {
public:
    struct Entry {
        QString uri;  ///< what children() / read() take (a picked tree: its root document)
        QString name; ///< display name ("Zakupy.md")
        bool isDir = false;
        qint64 size = -1;       ///< -1 = unknown
        bool isVirtual = false; ///< no bytes of its own (a Google Docs file in Drive): skipped
    };
    virtual ~DocumentProvider();
    /// The picked document or tree; std::nullopt with `error` when it cannot be reached.
    virtual std::optional<Entry> stat(const QUrl &url, QString *error) const = 0;
    /// The entries of a folder (one level); std::nullopt with `error` when it cannot be listed.
    virtual std::optional<QList<Entry>> children(const Entry &folder, QString *error) const = 0;
    /// The bytes of a file, at most `maxBytes` + 1 (more than `maxBytes`: too large, the caller
    /// skips it); std::nullopt with `error` when it cannot be read.
    virtual std::optional<QByteArray> read(const Entry &file, qint64 maxBytes, QString *error) const = 0;
};

/// Qt's content file engine: QFileInfo / QDir / QFile on the URI string (Qt 6.11 reads Android
/// documents and lists document trees that way — one query per property of every entry).
std::shared_ptr<const DocumentProvider> qtDocumentProvider();
/// The provider for content:// URLs here: Android — the bridge (DocumentsContract, one query
/// per folder; Qt's engine where the bridge call fails); elsewhere Qt's engine.
std::shared_ptr<const DocumentProvider> platformDocumentProvider();

class FileGatherer : public QObject {
    Q_OBJECT
public:
    explicit FileGatherer(QObject *parent = nullptr);

    void setLimits(const GatherLimits &limits) { m_limits = limits; }
    const GatherLimits &limits() const { return m_limits; }
    /// Files read per batch (default 32; also bounded by bytes per batch).
    void setChunkSize(int files) { m_chunk = qMax(1, files); }
    /// How content:// URLs are read (default platformDocumentProvider(); tests: a fake).
    void setDocumentProvider(std::shared_ptr<const DocumentProvider> provider);

    /// Start reading `urls` (files, or folders: read recursively). A running gather is cancelled.
    void start(const QList<QUrl> &urls);
    void cancel();
    bool isRunning() const { return m_running; }

    /// The source's name for `count` picked files: `single` (the one file's name) for one, else
    /// "3 files" (also the browser's pick, ImportController::pickInBrowser()).
    static QString sourceNameFor(int count, const QString &single);
    /// The warning for a file above GatherLimits::maxFileBytes, which is skipped:
    /// "“Eksport.zip” (600 MB) is too large and was skipped."
    static QString tooLargeWarning(const QString &name, qint64 bytes);
    /// The warning when reading stopped at GatherLimits::maxTotalBytes (`maxTotalBytes`): "Only the
    /// first 1.5 GB were read: the selection is too large to import at once." (also the browser's
    /// pick, ImportController::setPickedFiles()).
    static QString totalLimitWarning(qint64 maxTotalBytes);

Q_SIGNALS:
    /// `done` of `total` files read so far (`total` 0: still looking for the files).
    void progress(int done, int total);
    /// Everything read: the files, warnings (translated), a name for the source ("Eksport.zip",
    /// "Sejf", "3 files") and the number of bytes.
    void finished(const QList<brulion::io::InputFile> &files, const QStringList &warnings, const QString &sourceName,
                  qint64 bytes);
    void cancelled();

private:
    struct Pending {
        QString fsPath;        ///< for QFile; the document URI of a document
        QString relative;      ///< InputFile::path
        qint64 size = 0;       ///< -1: unknown (some document providers)
        bool document = false; ///< read through the DocumentProvider
    };
    struct Listing {
        QList<Pending> pending;
        QStringList warnings;
        QString sourceName;
    };
    /// One file of a batch, read off the GUI thread.
    struct Read {
        bool ok = false;
        QByteArray data;
        QString error;
    };
    static Listing list(const QList<QUrl> &urls, const GatherLimits &limits,
                        const std::shared_ptr<const DocumentProvider> &documents);
    static void collectLocal(const QUrl &url, const GatherLimits &limits, Listing &out);
    static void collectDocument(const QUrl &url, const GatherLimits &limits, const DocumentProvider &documents,
                                Listing &out);
    /// The files of `batch`, in its order.
    static QList<Read> readBatch(const QList<Pending> &batch, qint64 maxFileBytes,
                                 const std::shared_ptr<const DocumentProvider> &documents);
    void listed(const Listing &listing);
    void step();
    void merge(const QList<Read> &reads);
    /// The total limit is reached: nothing after the files read or planned so far (one warning).
    void stopForTotal();
    void finish();

    GatherLimits m_limits;
    int m_chunk = 32;
    std::shared_ptr<const DocumentProvider> m_documents;
    bool m_running = false;
    bool m_reading = false; ///< a batch is being read off the GUI thread
    int m_generation = 0;   ///< results of a cancelled / restarted gather are dropped
    QList<Pending> m_pending;
    qsizetype m_next = 0;
    QList<Pending> m_batch; ///< being read
    QList<io::InputFile> m_files;
    QStringList m_warnings;
    QString m_sourceName;
    qint64 m_bytes = 0;
    bool m_stopped = false;
    QTimer m_timer;
};

} // namespace brulion::ioviews
