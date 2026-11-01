// PdfThumbnails — the thumbnails of PDFs embedded in notes (`![[wykład.pdf]]`, docs/UI.md "PDF
// viewer"), as PNG files cached on the device (Workspace: <app cache>/pdf-thumbnails), handed to
// the editor through EditorHost::pdfThumbnail (a state machine like the pictures from the internet:
// loading / ready / failed / unsupported — never an image provider's error, which QML reports as a
// warning). Drawn once per (file, size, modification time, page, pixel size) off the GUI thread
// (PdfRenderPool, PdfReaderPool); the journal scrolls without drawing again, and after a restart
// the thumbnails are there at once.
//
//   const Entry e = thumbs.entry(absolutePath, size, mtimeMs, page);  // never blocks on drawing
//   connect(&thumbs, &PdfThumbnails::changed, …)                       // a thumbnail became ready / failed
//
// Ready: "<key>.png" with tEXt chunks Brulion-Pages, Brulion-PageWidth, Brulion-PageHeight (read back
// with QImageReader::text(), without decoding). Failed: "<key>.err" (JSON {error, pages}) for what the
// same bytes give again (locked, unreadable, nopages): a locked or broken PDF is not opened again on
// every scroll; a new mtime is a new key (sync delivered a repaired file). A failed drawing ("render":
// out of memory, a lost reader) is remembered in memory only, so it is drawn again after a restart. A
// remembered Ready entry whose PNG is gone (pruned) is drawn again. A cached PNG whose header cannot
// be read is deleted and drawn again; a cache folder that
// cannot be written makes the entry Failed "cache" (the editor shows the file's chip then). Pruning:
// 2 s after a store the folder is listed (on the pool) and the oldest files deleted until it holds at
// most setCacheBytes(); a hit read from the disk touches its PNG when it is older than a day. At most
// MaxMemoryEntries are remembered in memory (the least recently asked go first).
#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QTimer>

#include <functional>
#include <list>
#include <memory>

namespace brulion::pdfview {

class PdfThumbnails : public QObject {
    Q_OBJECT

public:
    enum class State { Loading, Ready, Failed, Unsupported };
    struct Entry {
        State state = State::Loading;
        QString file;      ///< the cached PNG (Ready; "" when kept in memory only)
        QSize pixels;      ///< its size
        int pages = 0;     ///< the PDF's page count (0: unknown)
        QSizeF pagePoints; ///< the drawn page's size
        QString error;     ///< "locked" | "unreadable" | "nopages" | "render" | "cache"
    };
    /// What a renderer drew: the picture (null on failure), the page count, the page's size, an
    /// error code (Entry::error, or "unsupported": no renderer here).
    struct Rendered {
        QImage image;
        int pages = 0;
        QSizeF pagePoints;
        QString error;
    };
    /// Draws page `page` of `file` (at `version`) with its long side `longSide` pixels — any thread.
    /// A page beyond the end draws page 1 (`![[a.pdf#page=999]]`).
    using Renderer = std::function<Rendered(const QString &file, const QString &version, int page, int longSide)>;

    static constexpr int MaxMemoryEntries = 512;
    static constexpr int DefaultLongSide = 768;
    static constexpr qint64 DefaultCacheBytes = 64LL * 1024 * 1024;
    static constexpr int PruneDelayMs = 2000;

    /// `cacheDir` "" → in memory only (tests); the renderer defaults to PdfReaderPool + PdfReader.
    explicit PdfThumbnails(const QString &cacheDir, QObject *parent = nullptr, Renderer renderer = {});
    ~PdfThumbnails() override;

    /// The long side drawn (pixels; Workspace: 420 logical px × the primary screen's ratio, rounded
    /// up to 512 / 768 / 1024 / 1280 / 1536 — one size per device: the embed's frame is at most 420
    /// high). A new side is a new key.
    void setLongSide(int pixels);
    int longSide() const { return m_longSide; }
    /// The side for a screen's device pixel ratio (setLongSide()'s rounding).
    static int longSideForRatio(qreal devicePixelRatio);
    void setCacheBytes(qint64 bytes) { m_cacheBytes = bytes; }
    QString cacheDir() const { return m_cacheDir; }

    /// GUI thread, never blocks on drawing: the memory map, else the disk (`<key>.png` / `.err`),
    /// else a job is queued (once per key) and Loading returned; changed(file) follows.
    Entry entry(const QString &file, qint64 size, qint64 mtimeMs, int page);
    /// "pdfthumb/1|<file>|<size>|<mtime>|<page>|<side>" → SHA-1 hex (pure; tests).
    static QString cacheKey(const QString &file, qint64 size, qint64 mtimeMs, int page, int side);
    /// Jobs queued or running (tests).
    int pendingCount() const { return int(m_pending.size()); }

Q_SIGNALS:
    /// A thumbnail of `file` (absolute) became ready or failed: entry() has it.
    void changed(const QString &file);

private:
    struct Job;
    void remember(const QString &key, const Entry &entry);
    void forget(const QString &key);
    Entry fromDisk(const QString &key);
    void finished(const QString &key, const QString &file, const Entry &entry);
    void schedulePrune();

    QString m_cacheDir;
    Renderer m_renderer;
    bool m_customRenderer = false;
    int m_longSide = DefaultLongSide;
    qint64 m_cacheBytes = DefaultCacheBytes;
    QHash<QString, Entry> m_entries;
    std::list<QString> m_order; ///< keys, the most recently asked first
    QHash<QString, std::list<QString>::iterator> m_positions;
    QSet<QString> m_pending;
    QTimer m_prune;
    /// Set when this object goes: jobs still running drop their results.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace brulion::pdfview
