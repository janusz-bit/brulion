// AttachmentIndexer — the text of the notebook's PDFs, documents and pictures for the whole-notebook search
// (docs/UI.md "Searching attachments", docs/ARCHITECTURE.md; docs/PERFORMANCE.md "Searching
// attachments"). Owned by the shell's Workspace (`Workspace.attachments` in QML).
//
//   * Background, offline, never in the way: one worker thread at the lowest priority (Linux:
//     SCHED_IDLE) reads one file at a time, page by page — a 100+ MB lecture scan costs one page's
//     picture of memory, never the file —, waits while a Nextcloud sync runs (PdfReaderPool's
//     suspendIdle: it may move the PDFs) and while paused; the GUI thread only merges what it
//     found, a few pages per event-loop turn. Starts a few seconds after the notebook's index is
//     ready.
//   * PDFs: their text layer (platform/pdfpages.h PdfReader::pageText — poppler, Qt PDF, Android 15+;
//     none in the browser); a page with too little text of its own (a scan) waits for OCR.
//     Pictures (png, jpg, jpeg, webp, bmp): OCR only. Documents (DOCX, PPTX, ODT, EPUB): their
//     paragraphs from the XML inside (core attachments/documenttext.h: bounded, no OCR, on every
//     platform that has the worker thread); a slide, a file of an EPUB or a part of a text is a "page".
//   * OCR (platform/ocr.h, Tesseract): off by default — it costs seconds of CPU a page —, turned on
//     in Settings → Search; languages "auto" (the UI language and English) or chosen; the text
//     layer of every file comes first, then OCR page by page.
//   * Cached on the device, never in the notebook: <app cache>/attachment-text/<sha256>.bat (core
//     attachments/record.h: one file per content, so a renamed, moved or copied file is not read
//     again) and the notebook's manifest (path → size, modification time, hash: an unchanged file is
//     not even hashed again). A changed file is read again; records no notebook on this device
//     names any more are pruned beyond the cap (512 MiB, 128 MiB on Android). A newer extractor
//     (ExtractorVersion) reads files again.
//   * The search (core attachments::AttachmentSearch): search() for the search view's model,
//     with excerpts read from the cache; page() for the PDF viewer's search on scanned pages and
//     matchRects() for the picture view's highlights (OCR words' boxes).
//
// QML: state ("off" | "unavailable" | "idle" | "waiting" | "reading" | "recognizing" | "paused"),
// progress (filesTotal, filesDone, currentFile, currentPage, currentPages, pagesToRecognize,
// pagesRecognized), what is indexed (indexedFiles, indexedPages, cacheBytes), the settings
// (enabled, ocrEnabled, ocrLanguagesSetting — AppSettings keeps them, AppController hands them over)
// and what this device offers (pdfTextAvailable, ocrAvailable, ocrUnavailableReason, ocrEngineName,
// ocrInstalledLanguages, ocrLanguages, ocrUserDataDir); paused, clearCache().
#pragma once

#include "attachments/attachmentsearch.h"
#include "attachments/manifest.h"
#include "attachments/record.h"

#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>

QT_BEGIN_NAMESPACE
class QImage;
QT_END_NAMESPACE

namespace brulion {
class Vault;
struct VaultChange;
} // namespace brulion

namespace brulion::attachsearch {

class IndexerWorker;

/// The files read: PDFs, documents (DOCX, PPTX, ODT, EPUB) and pictures with text worth reading (no
/// SVG, GIF, icons).
bool isSearchableAttachment(const QString &path);
/// Where a box on a picture's own pixels (fractions of the file's stored picture) lies on the picture
/// as its EXIF orientation shows it (fractions) — what the picture view and a note show, and the
/// boxes OCR's words keep. `transformation`: QImageIOHandler::Transformations as int.
QRectF orientedBox(const QRectF &box, int transformation);
/// A record an older extractor wrote, brought up to ExtractorVersion without reading the file
/// again: version 1 kept an OCR picture's word boxes on the file's own pixels, which turn by the
/// picture's EXIF orientation `transformation` (QImageIOHandler::Transformations as int) onto the
/// picture as shown. A record of any other version comes back unchanged.
attachments::Record upgradedRecord(attachments::Record record, int transformation);
/// The pixel size a PDF page of `points` is drawn at for OCR: about 300 dpi, the long side 1000…4200
/// px (a small slide gets enough pixels, a poster no more than that).
QSize ocrPagePixels(QSizeF points);

class AttachmentIndexer : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool ocrEnabled READ ocrEnabled WRITE setOcrEnabled NOTIFY settingsChanged FINAL)
    Q_PROPERTY(QString ocrLanguagesSetting READ ocrLanguagesSetting WRITE setOcrLanguagesSetting NOTIFY settingsChanged
                       FINAL)
    /// The languages OCR reads now ("pol+eng"; "" when none of them is installed).
    Q_PROPERTY(QString ocrLanguages READ ocrLanguages NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY stateChanged FINAL)

    /// This build reads PDFs' text (platform::pdfTextAvailable()).
    Q_PROPERTY(bool pdfTextAvailable READ pdfTextAvailable CONSTANT FINAL)
    /// This build reads the text of documents (DOCX, PPTX, ODT, EPUB: everywhere with threads, not
    /// in the browser).
    Q_PROPERTY(bool documentTextAvailable READ documentTextAvailable CONSTANT FINAL)
    Q_PROPERTY(bool ocrAvailable READ ocrAvailable NOTIFY engineChanged FINAL)
    /// "" | "unsupported" | "noLibrary" | "noLanguages" (platform/ocr.h); "probing" until known.
    Q_PROPERTY(QString ocrUnavailableReason READ ocrUnavailableReason NOTIFY engineChanged FINAL)
    Q_PROPERTY(QString ocrEngineName READ ocrEngineName NOTIFY engineChanged FINAL)
    Q_PROPERTY(QStringList ocrInstalledLanguages READ ocrInstalledLanguages NOTIFY engineChanged FINAL)
    Q_PROPERTY(QString ocrUserDataDir READ ocrUserDataDir NOTIFY engineChanged FINAL)

    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(int filesTotal READ filesTotal NOTIFY progressChanged FINAL)
    Q_PROPERTY(int filesDone READ filesDone NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY progressChanged FINAL)
    Q_PROPERTY(int currentPage READ currentPage NOTIFY progressChanged FINAL)
    Q_PROPERTY(int currentPages READ currentPages NOTIFY progressChanged FINAL)
    Q_PROPERTY(int pagesToRecognize READ pagesToRecognize NOTIFY progressChanged FINAL)
    Q_PROPERTY(int pagesRecognized READ pagesRecognized NOTIFY progressChanged FINAL)
    Q_PROPERTY(int indexedFiles READ indexedFiles NOTIFY contentChanged FINAL)
    Q_PROPERTY(int indexedPages READ indexedPages NOTIFY contentChanged FINAL)
    Q_PROPERTY(qint64 cacheBytes READ cacheBytes NOTIFY contentChanged FINAL)
    /// cacheBytes in words ("12,5 MB" in the UI's locale).
    Q_PROPERTY(QString cacheSizeText READ cacheSizeText NOTIFY contentChanged FINAL)

public:
    /// Bump when what is read from a file changes (the text rules, OCR's input): every record of
    /// an older one is read again.
    /// (2: an OCR picture's word boxes on the picture as its EXIF orientation shows it —
    /// upgradedRecord() turns version 1's.)
    static constexpr quint32 ExtractorVersion = 2;
    /// A PDF page with fewer letters and digits of its own is read by OCR: a scan (none, or a page
    /// number), a slide that is one picture. Its own few words stay searchable with OCR's.
    static constexpr int MinTextLayerLetters = 8;
    /// Pictures larger than this are not read (a camera's raw file named .png).
    static constexpr qint64 MaxPictureBytes = 64LL * 1024 * 1024;
    static constexpr qint64 DefaultCacheBytes = 512LL * 1024 * 1024;

    explicit AttachmentIndexer(QObject *parent = nullptr);
    ~AttachmentIndexer() override;

    /// Follow `vault`'s PDFs and pictures (none: stop). `cacheDir`: the records' folder (shared by
    /// the notebooks of this device); `manifestFile`: this notebook's manifest. Nothing is read
    /// before start().
    void attach(Vault *vault, const QString &cacheDir, const QString &manifestFile);
    void detach();
    /// Begin (the notebook's index is ready): after `delayMs`.
    void start(int delayMs = 3000);
    bool attached() const { return m_vault != nullptr; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool ocrEnabled() const { return m_ocrEnabled; }
    void setOcrEnabled(bool on);
    QString ocrLanguagesSetting() const { return m_ocrLanguagesSetting; }
    void setOcrLanguagesSetting(const QString &setting);
    QString ocrLanguages() const;
    /// The UI language ("pl", "en"): "auto" OCR languages follow it.
    void setUiLanguage(const QString &language);
    bool paused() const { return m_paused.load(); }
    void setPaused(bool paused);
    /// The records' cap (tests).
    void setCacheBytes(qint64 bytes) { m_cacheCap = bytes; }

    bool pdfTextAvailable() const;
    static bool documentTextAvailable();
    // What this device's OCR is: looked for when first asked (or when OCR is turned on), in the
    // background — "probing" and nothing available until engineChanged() says otherwise.
    bool ocrAvailable() const
    {
        probeEngine();
        return m_ocrAvailable;
    }
    QString ocrUnavailableReason() const
    {
        probeEngine();
        return m_ocrReason;
    }
    QString ocrEngineName() const
    {
        probeEngine();
        return m_ocrEngine;
    }
    QStringList ocrInstalledLanguages() const
    {
        probeEngine();
        return m_ocrInstalled;
    }
    QString ocrUserDataDir() const
    {
        probeEngine();
        return m_ocrUserDir;
    }
    /// The OCR library was looked for, or is being (tests: not before anybody asked).
    bool engineAsked() const { return m_probeAsked; }

    QString state() const;
    int filesTotal() const { return m_filesTotal; }
    int filesDone() const { return m_filesDone; }
    QString currentFile() const { return m_currentFile; }
    int currentPage() const { return m_currentPage; }
    int currentPages() const { return m_currentPages; }
    int pagesToRecognize() const { return m_pagesToRecognize; }
    int pagesRecognized() const { return m_pagesRecognized; }
    int indexedFiles() const { return int(m_search.fileCount()); }
    int indexedPages() const { return int(m_search.pageCount()); }
    qint64 cacheBytes() const { return m_cacheBytesUsed; }
    QString cacheSizeText() const;
    /// Nothing waits to be read (the worker is idle or stopped): tests wait for it.
    bool idle() const;

    /// The attachments' best `limit` files for `query` (its words, no filters), with excerpts.
    QList<attachments::AttachmentSearch::Hit> search(QStringView query, int limit,
                                                     attachments::Scopes kinds = attachments::AttachmentScopes);
    /// Grows with every change of what is searched.
    quint64 revision() const { return m_search.revision(); }
    const attachments::AttachmentSearch &index() const { return m_search; }

    /// Page `page` of the file at `path` as the cache holds it (nullopt: not read / unknown).
    std::optional<attachments::Page> page(const QString &path, int page);
    /// The same by the file's absolute name and version — any thread (the PDF viewer's search
    /// job): only when the cache's record was made from exactly that size and time.
    std::optional<attachments::Page> pageOfFile(const QString &absoluteFile, qint64 size, qint64 mtime, int page) const;
    /// pageOfFile() as a function that outlives the indexer (it then finds nothing): the PDF
    /// viewer's search jobs keep it (pdfview::PdfViewModel::setOcrLookup, set by Workspace).
    using PageLookup = std::function<std::optional<attachments::Page>(const QString &absoluteFile, qint64 size,
                                                                      qint64 mtime, int page)>;
    PageLookup pageLookup() const;

    /// The boxes (fractions of the picture as shown, its EXIF orientation applied: {x, y, width, height, box — the same as a
    /// rect, for typed delegates}) of the places of `query` on page `page` of `path` that OCR read
    /// (the picture view's highlights; [] without) — the query as a phrase, else its words.
    Q_INVOKABLE QVariantList matchRects(const QString &path, int page, const QString &query);
    /// What OCR read on page `page` of `path` ("" without).
    Q_INVOKABLE QString recognizedText(const QString &path, int page = 1);
    /// Forget every record of this device (all notebooks') and read the notebook's files again.
    Q_INVOKABLE void clearCache();
    /// Tesseract's languages by their own names: "pol+eng" → "Polski, English" (an unknown code as
    /// it is).
    Q_INVOKABLE static QString languageNames(const QString &codes);

Q_SIGNALS:
    void settingsChanged();
    void stateChanged();
    void progressChanged();
    void engineChanged();
    /// What is searched changed (coalesced: at most a few times a second).
    void contentChanged();

private:
    friend class IndexerWorker;
    struct FileJob;

    void onVaultChanged(const QList<brulion::VaultChange> &changes);
    void queueAll();
    void queue(const QString &path, bool front);
    void forget(const QString &path);
    void stopWorker();
    void ensureWorker();
    void applyFile(quint64 generation, const QString &path, const attachments::ManifestEntry &entry,
                   attachments::AttachmentSearch::Prepared prepared);
    void applyPages(quint64 generation, const QString &path, QList<attachments::AttachmentSearch::PreparedPage> pages);
    void applyProgress(quint64 generation, const QVariantMap &progress);
    /// The worker wrote `path`'s record: its pages read before are read again.
    void recordSaved(quint64 generation, const QString &path);
    void applyEngine(bool available, const QString &reason, const QString &name, const QStringList &languages,
                     const QString &userDir);
    void contentTouched();
    QString recordPath(const QByteArray &hash) const;
    void saveManifestLater();
    void saveManifest();
    void measureCache();

    /// What the worker and other threads share with the indexer (the lookup keeps it past the
    /// indexer's end): the manifest and where the records are, under `lock`.
    struct Shared {
        std::mutex lock;
        QString cacheDir;
        /// path → entry.
        attachments::Manifest manifest;
        /// absolute file → path.
        QHash<QString, QString> pathOfFile;
    };
    static std::optional<attachments::Page> pageOf(Shared &shared, const QString &absoluteFile, qint64 size,
                                                   qint64 mtime, int page);

    QPointer<Vault> m_vault;
    QString m_cacheDir;
    QString m_manifestFile;
    attachments::AttachmentSearch m_search;
    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    std::unique_ptr<IndexerWorker> m_worker;
    quint64 m_generation = 0;
    bool m_started = false;
    QTimer m_startTimer;
    QTimer m_contentTimer;
    QTimer m_manifestTimer;

    bool m_enabled = true;
    bool m_ocrEnabled = false;
    QString m_ocrLanguagesSetting = QStringLiteral("auto");
    QString m_uiLanguage = QStringLiteral("en");
    std::atomic_bool m_paused = false;
    qint64 m_cacheCap = DefaultCacheBytes;

    /// Looks for the OCR library on a pool thread, once (applyEngine() gets the answer).
    void probeEngine() const;
    mutable bool m_probeAsked = false;
    bool m_ocrAvailable = false;
    QString m_ocrReason = QStringLiteral("probing");
    QString m_ocrEngine;
    QStringList m_ocrInstalled;
    QString m_ocrUserDir;

    QString m_state = QStringLiteral("idle");
    int m_filesTotal = 0;
    int m_filesDone = 0;
    QString m_currentFile;
    int m_currentPage = 0;
    int m_currentPages = 0;
    int m_pagesToRecognize = 0;
    int m_pagesRecognized = 0;
    qint64 m_cacheBytesUsed = 0;
    bool m_workerIdle = true;

    /// Pages read for excerpts lately (GUI thread): path + page → page.
    QCache<QString, attachments::Page> m_pages{64};
};

} // namespace brulion::attachsearch
