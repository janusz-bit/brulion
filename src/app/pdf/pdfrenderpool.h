// The threads PDFs are drawn and searched on, and the open PDFs they share (docs/UI.md "PDF viewer",
// docs/PERFORMANCE.md "PDF viewer"):
//
//   PdfRenderPool::start(task, priority)   a QThreadPool of at most 2 threads (Android too: its
//                                          PdfRenderer serialises pdfium; two keep a search from
//                                          starving the pages); visible pages first, search last
//   PdfReaderPool::instance().acquire(file, version)
//                                          a lease of an open reader (platform::PdfReader) for one
//                                          thread; given back when the lease goes, it waits idle for
//                                          the next (same file and version) until MaxIdlePerFile /
//                                          MaxIdle push it out (the least recently used) or it has
//                                          been idle IdleCloseMs — so Syncthing on Windows can
//                                          replace the file (an open file cannot be renamed there)
//
// The browser (no threads, no renderer) never starts anything: platform::pdfAvailable() is false.
#pragma once

#include "pdfpages.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>

QT_BEGIN_NAMESPACE
class QThreadPool;
QT_END_NAMESPACE

namespace brulion::pdfview {

class PdfRenderPool {
public:
    enum Priority { Page = 10, Detail = 8, Thumbnail = 4, Sizes = 2, Search = 1 };
    static constexpr int MaxThreads = 2;
    /// `task` on one of the threads (the browser, no threads: never — nothing calls it there; it
    /// runs on the spot then). Any thread.
    static void start(std::function<void()> task, int priority);
    /// The pool (tests wait for it); null without threads.
    static QThreadPool *pool();
};

class PdfReaderPool : public QObject {
    Q_OBJECT

public:
    static PdfReaderPool &instance();
    static constexpr int MaxIdlePerFile = 2;
    static constexpr int MaxIdle = 4;
    static constexpr int IdleCloseMs = 10'000;

    /// One reader for one thread (move-only); given back to the pool when it goes.
    class Lease {
    public:
        Lease() = default;
        Lease(Lease &&other) noexcept;
        Lease &operator=(Lease &&other) noexcept;
        Lease(const Lease &) = delete;
        Lease &operator=(const Lease &) = delete;
        ~Lease();
        platform::PdfReader *operator->() const { return m_reader.get(); }
        platform::PdfReader *get() const { return m_reader.get(); }
        explicit operator bool() const { return m_reader != nullptr; }

    private:
        friend class PdfReaderPool;
        void release();
        PdfReaderPool *m_pool = nullptr;
        QString m_file;
        QString m_version;
        std::unique_ptr<platform::PdfReader> m_reader;
    };

    /// A reader of `file` at `version` ("<mtime ms>-<size>"): an idle one, or one opened now
    /// (`info` filled; a null lease when the file cannot be opened — `info` says why). Any thread;
    /// the reader is attached to the calling thread.
    Lease acquire(const QString &file, const QString &version, platform::PdfInfo *info = nullptr);
    /// The file changed or went: its idle readers are closed (Workspace on fileChanged and renames;
    /// a leased one is not given back to the pool when its version differs).
    void forget(const QString &file);
    /// Every idle reader closed (the viewer closed; tests; the app quits).
    void closeIdle();
    /// While suspended (counted: suspendIdle() / resumeIdle() in pairs) no reader waits idle: the
    /// idle ones are closed now and a lease given back is closed — Brulion's own sync run may
    /// replace, move or delete a PDF, which Windows refuses while a reader holds it open
    /// (WorkspaceSync: from the run's start to its end). Any thread.
    void suspendIdle();
    void resumeIdle();
    bool idleSuspended() const { return m_suspended.load() > 0; }
    /// The idle readers (tests).
    int idleCount() const;
    /// A page that could not be drawn (the image provider; any thread): pageFailed() on the GUI
    /// thread.
    void reportFailure(const QString &file, const QString &version, int page);
    /// Opened readers since the start (tests: a second acquire reuses the idle one).
    int openedCount() const;

Q_SIGNALS:
    void pageFailed(const QString &file, const QString &version, int page);

private:
    PdfReaderPool();
    void giveBack(const QString &file, const QString &version, std::unique_ptr<platform::PdfReader> reader);
    void closeExpired();

    struct Idle {
        QString file;
        QString version;
        std::unique_ptr<platform::PdfReader> reader;
        QElapsedTimer since;
    };
    mutable std::mutex m_lock;
    std::list<Idle> m_idle; ///< the most recently given back first
    QTimer m_closer;        ///< (GUI thread; a child, moves with the pool) closes readers idle for IdleCloseMs
    int m_opened = 0;
    std::atomic_int m_suspended = 0;
};

} // namespace brulion::pdfview
