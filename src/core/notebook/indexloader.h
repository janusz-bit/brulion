// IndexLoader — opens a notebook's index fast without stalling its thread: reads, decodes and
// parses the notes on worker threads (or takes them from the index cache, indexcache.h), builds
// the full-text index on one more thread, and merges the notes into the VaultIndex on the owner's
// thread a slice at a time (step()). WorkerPool — the threads it may use. Part of the notebook
// module — see notebook/notebook.h; docs/PERFORMANCE.md (L1, L2).
#pragma once

#include "types.h"

#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace brulion {
class Vault;
class VaultIndex;
} // namespace brulion

namespace brulion::notebook {

/// Threads the IndexLoader and the IndexCacheKeeper may use. Core never starts threads itself:
/// the app (src/app/shell/indexworkers.h) and brulion-cli pass a pool; the default — no threads —
/// does all the work on the calling thread, a slice at a time (WebAssembly).
struct WorkerPool {
    /// Parse workers (the pool needs one thread more: the full-text index builder). 0 = inline.
    int threads = 0;
    /// Run a job on a pool thread.
    std::function<void(std::function<void()>)> start;

    bool isInline() const { return threads <= 0 || !start; }
};

/// Opens a notebook's index (see the file comment). The index and the vault are touched on the
/// owner's thread only; workers run pure functions on values they own. Notes are merged
/// attachments first (cheap), then the journals folder newest first, then the rest by path; the
/// recency of every note (VaultIndex::reserveSequence()) follows the path order whatever the merge
/// order. A note the vault reports changed while loading must go to forget() before the caller
/// indexes the file itself (a worker's older read of it is dropped).
class IndexLoader : public QObject {
    Q_OBJECT

public:
    struct Options {
        QString cachePath;           ///< index cache to read (indexcache.h); "" = none
        QString firstFolder;         ///< merged first, newest first (the journals folder)
        int maxPending = 256;        ///< prepared notes waiting for a merge before workers pause
        qint64 maxPendingBytes = 64LL << 20;
    };
    struct Stats {
        qsizetype total = 0;       ///< files to index (notes + attachments)
        qsizetype merged = 0;      ///< of them in the index
        qsizetype fromCache = 0;   ///< notes taken from the index cache (no parsing)
        qsizetype parsed = 0;      ///< notes parsed
        qsizetype attachments = 0;
        qsizetype dropped = 0;     ///< results dropped: the file changed meanwhile (forget())
        qsizetype readFailed = 0;  ///< worker reads that failed (read again on the owner's thread)
        qsizetype recordsRejected = 0; ///< cache records that failed validation (parsed instead)
        int workers = 0;
        /// "none" (no cache path), "used", "absent", "disabled", "invalid: <reason>"
        QString cache = QStringLiteral("none");
        bool searchFromCache = false; ///< the full-text index started from the cache's
        qint64 elapsedMs = 0;         ///< from start() to the last merge
    };
    enum class Step {
        Done,    ///< every file merged
        More,    ///< more to merge now
        Waiting, ///< nothing ready yet: resultsReady() follows
    };

    IndexLoader(Vault &vault, VaultIndex &index, WorkerPool pool, Options options, QObject *parent = nullptr);
    /// Cancels: the workers stop after their current file (never waits for them).
    ~IndexLoader() override;

    /// Start loading `files` (Vault::files()).
    void start(const QList<FileStat> &files);
    /// Merge what is ready for about `budgetMs` (inline: also read and parse).
    Step step(int budgetMs);
    /// Everything now: blocks, and this thread takes unstarted files itself.
    void finish();
    /// A pending or late result for `path` is dropped (the caller indexes the file itself).
    void forget(const VaultPath &path);
    /// The vault's config changed: notes prepared with the old one are parsed again at the merge.
    void configChanged();
    /// Every file merged (the full-text index may still be building elsewhere).
    bool isDone() const;
    /// Share of the files merged, 0…1.
    qreal progress() const;
    Stats stats() const;
    /// Notes prepared by the workers and waiting for a merge (tests: the back-pressure).
    qsizetype pendingResults() const;

Q_SIGNALS:
    /// Results are ready for step() (queued; once per empty → non-empty).
    void resultsReady();
    /// Every file merged (from step() or finish()).
    void finished();

private:
    struct Private;
    std::unique_ptr<Private> d;
};

} // namespace brulion::notebook
