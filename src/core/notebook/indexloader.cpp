#include "notebook/indexloader.h"

#include "index/indexcache.h"
#include "index/searchindex.h"
#include "index/vaultindex.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>

namespace brulion::notebook {

using namespace Qt::StringLiterals;

namespace {

/// A note to load.
struct Item {
    VaultPath path;
    QString absolute;     ///< Vault::absolutePath(), taken on the owner's thread
    quint64 sequence = 0; ///< VaultIndex::reserveSequence() base + position in path order
};

/// A note read and prepared by a worker (or by the owner itself).
struct Result {
    qsizetype item = 0;
    DetachedRead read;
    PreparedNote prepared;
    bool fromCache = false;
    qint64 bytes = 0; ///< memory held while it waits (back-pressure)
};

/// A full-text document for the search builder.
struct SearchJob {
    VaultPath path;
    SearchIndex::Document document;
    quint64 contentKey = 0;
};

} // namespace

/// State shared by the loader and its jobs (each job holds it: a cancelled loader may go away
/// while a worker finishes its current file). Everything but `items` and `next` under `mutex`.
struct LoaderShared {
    std::mutex mutex;
    std::condition_variable workersCv; ///< space for results, cancellation
    std::condition_variable ownerCv;   ///< a result, the search builder's end
    std::condition_variable searchCv;  ///< search jobs, closing, cancellation

    QList<Item> items; ///< notes in merge order; never changed after start()
    std::atomic<qsizetype> next{0};

    std::deque<Result> results;
    int inFlight = 0; ///< notes workers are reading and preparing (count against maxPending)
    qint64 pendingBytes = 0;
    int maxPending = 256;
    qint64 maxPendingBytes = 64LL << 20;
    bool cancelled = false;
    IndexLoader *receiver = nullptr; ///< read on worker threads: no QPointer
    bool posted = false;             ///< a wake-up for the owner is on its way
    VaultConfig config;

    // The index cache (opened by the first job that needs it, or inline).
    QString cachePath;
    indexcache::Identity cacheIdentity;
    VaultConfig cacheConfig; ///< the config the cache's identity was taken with
    bool cacheOpening = false;
    bool cacheOpened = false;
    std::shared_ptr<const indexcache::Reader> cache;
    QString cacheStatus = QStringLiteral("none");
    qsizetype recordsRejected = 0;

    bool buildSearch = false;
    bool searchFromCache = false;
    std::deque<SearchJob> searchQueue;
    bool searchStarted = false;
    bool searchClosed = false;    ///< every document is queued
    bool searchAbandoned = false; ///< the owner builds the search itself after all
    std::optional<SearchIndex> searchResult;
};

struct IndexLoader::Private {
    IndexLoader *q;
    Vault &vault;
    VaultIndex &index;
    WorkerPool pool;
    Options options;
    std::shared_ptr<LoaderShared> s = std::make_shared<LoaderShared>();

    bool started = false;
    bool done = false;
    bool searchAdopted = false;
    /// Inline mode: the cache's full-text index, decoded in steps after the notes.
    std::optional<indexcache::Reader::SearchDecoder> inlineSearch;
    bool inlineSearchSet = false;
    QList<VaultPath> attachments;
    qsizetype nextAttachment = 0;
    qsizetype mergedNotes = 0;
    QSet<VaultPath> forgotten;
    Stats stats;
    QElapsedTimer clock;

    Private(IndexLoader *owner, Vault &v, VaultIndex &i, WorkerPool p, Options o)
        : q(owner), vault(v), index(i), pool(std::move(p)), options(std::move(o))
    { }

    bool threaded() const { return !pool.isInline(); }
    qsizetype noteCount() const { return s->items.size(); }

    // ---- jobs (any thread; touch only `shared` and the values they captured) -------------------

    /// Wake the owner (once until it looked): queued to its thread. Call with the mutex held.
    static void postLocked(LoaderShared &shared)
    {
        if (shared.cancelled || !shared.receiver || shared.posted)
            return;
        shared.posted = true;
        IndexLoader *receiver = shared.receiver;
        QMetaObject::invokeMethod(receiver, [receiver] { receiver->d->onPosted(); }, Qt::QueuedConnection);
    }

    /// Open the index cache once (the first caller does it, the others wait for it).
    static void ensureCache(LoaderShared &shared)
    {
        {
            std::unique_lock lock(shared.mutex);
            if (shared.cacheOpened || shared.cachePath.isEmpty())
                return;
            if (shared.cacheOpening) {
                shared.workersCv.wait(lock, [&] { return shared.cacheOpened || shared.cancelled; });
                return;
            }
            shared.cacheOpening = true;
        }
        QString status;
        std::shared_ptr<const indexcache::Reader> reader;
        QFile file(shared.cachePath);
        if (!QFileInfo::exists(shared.cachePath)) {
            status = u"absent"_s;
        } else if (!QFileInfo(shared.cachePath).isFile() || !file.open(QIODevice::ReadOnly)) {
            status = u"invalid: cannot read"_s;
        } else {
            QString why;
            reader = indexcache::Reader::open(file.readAll(), shared.cacheIdentity, &why);
            status = reader ? u"used"_s : u"invalid: "_s + why;
        }
        std::lock_guard lock(shared.mutex);
        shared.searchFromCache = reader && reader->hasSearch();
        shared.cache = std::move(reader);
        shared.cacheStatus = status;
        shared.cacheOpened = true;
        shared.workersCv.notify_all();
    }

    /// The note of `item` from the cache, when it has a valid record for this text.
    static std::optional<ParsedNote> fromCache(LoaderShared &shared, const Item &item, const DetachedRead &read,
                                               const QString &text)
    {
        std::shared_ptr<const indexcache::Reader> cache;
        {
            std::lock_guard lock(shared.mutex);
            cache = shared.cache;
        }
        if (!cache)
            return std::nullopt;
        const indexcache::Record *record = cache->find(item.path);
        if (!record || record->length <= 0 || record->fileSize != read.bytes.size()
            || record->contentKey != VaultIndex::contentKey(text))
            return std::nullopt;
        std::optional<ParsedNote> note = cache->decodeNote(*record, item.path, text, shared.cacheConfig);
        if (!note) {
            std::lock_guard lock(shared.mutex);
            ++shared.recordsRejected;
        }
        return note;
    }

    /// Read and prepare note `i` (pure apart from reading the file).
    static Result process(LoaderShared &shared, qsizetype i, const VaultConfig &config)
    {
        ensureCache(shared);
        const Item &item = shared.items.at(i);
        Result r;
        r.item = i;
        r.read = Vault::readDetached(item.path, item.absolute);
        if (r.read.ok) {
            if (std::optional<ParsedNote> cached = fromCache(shared, item, r.read, r.read.text)) {
                // Prepared with the config the cache was written for: a config changed since
                // makes updatePrepared() parse it again.
                r.prepared = VaultIndex::prepareParsed(item.path, r.read.text, std::move(*cached), shared.cacheConfig);
                r.fromCache = true;
            } else {
                r.prepared = VaultIndex::prepare(item.path, r.read.text, config);
            }
            // Documents of notes from the cache come with its search section (if it has one).
            if (shared.buildSearch && (!r.fromCache || !shared.searchFromCache)) {
                const ParsedNote &note = r.prepared.note();
                SearchJob job{item.path, SearchIndex::prepare(note.title, note.aliases, r.read.text),
                              r.prepared.contentKey()};
                std::lock_guard lock(shared.mutex);
                if (!shared.searchAbandoned) {
                    shared.searchQueue.push_back(std::move(job));
                    shared.searchCv.notify_one();
                }
            }
        }
        r.bytes = r.read.bytes.size() + 2 * r.read.text.size() + 1024;
        return r;
    }

    static void runWorker(const std::shared_ptr<LoaderShared> &shared)
    {
        LoaderShared &s = *shared;
        for (;;) {
            const qsizetype i = s.next.fetch_add(1);
            if (i >= s.items.size())
                break;
            VaultConfig config;
            {
                std::unique_lock lock(s.mutex);
                s.workersCv.wait(lock, [&] {
                    return s.cancelled
                           || (qsizetype(s.results.size()) + s.inFlight < s.maxPending
                               && s.pendingBytes < s.maxPendingBytes);
                });
                if (s.cancelled)
                    return;
                ++s.inFlight;
                config = s.config;
            }
            Result r = process(s, i, config);
            std::lock_guard lock(s.mutex);
            --s.inFlight;
            if (s.cancelled)
                return;
            s.pendingBytes += r.bytes;
            s.results.push_back(std::move(r));
            postLocked(s);
            s.ownerCv.notify_all();
        }
    }

    static void runSearch(const std::shared_ptr<LoaderShared> &shared)
    {
        LoaderShared &s = *shared;
        {
            std::lock_guard lock(s.mutex);
            if (s.cancelled || s.searchAbandoned)
                return;
            s.searchStarted = true;
        }
        SearchIndex built;
        // Start from the cache's full-text index (its documents are checked by content key
        // when adopted); the workers send the documents of the notes they parse.
        ensureCache(s);
        std::shared_ptr<const indexcache::Reader> cache;
        {
            std::lock_guard lock(s.mutex);
            cache = s.searchFromCache ? s.cache : nullptr;
        }
        if (cache) {
            indexcache::Reader::SearchDecoder decoder = cache->searchDecoder();
            decoder.step(std::numeric_limits<int>::max());
            built = decoder.take();
            cache.reset();
        }
        for (;;) {
            std::deque<SearchJob> batch;
            {
                std::unique_lock lock(s.mutex);
                s.searchCv.wait(lock, [&] { return s.cancelled || s.searchClosed || !s.searchQueue.empty(); });
                if (s.cancelled)
                    return;
                if (s.searchQueue.empty())
                    break; // closed
                batch.swap(s.searchQueue);
            }
            for (SearchJob &job : batch)
                built.set(job.path, std::move(job.document), job.contentKey);
        }
        built.squeeze();
        std::lock_guard lock(s.mutex);
        if (s.cancelled)
            return;
        s.searchResult = std::move(built);
        postLocked(s);
        s.ownerCv.notify_all();
    }

    // ---- owner's thread ----------------------------------------------------------------------------

    void onPosted()
    {
        bool any = false;
        {
            std::lock_guard lock(s->mutex);
            s->posted = false;
            any = !s->results.empty();
        }
        adoptSearchIfReady();
        if (any)
            Q_EMIT q->resultsReady();
    }

    std::optional<Result> pop()
    {
        std::lock_guard lock(s->mutex);
        if (s->results.empty()) {
            s->posted = false; // the next result wakes the owner again
            return std::nullopt;
        }
        Result r = std::move(s->results.front());
        s->results.pop_front();
        s->pendingBytes -= r.bytes;
        s->workersCv.notify_all();
        return r;
    }

    void merge(Result &&r)
    {
        const Item &item = s->items.at(r.item);
        ++mergedNotes;
        ++stats.merged;
        if (forgotten.contains(item.path)) {
            ++stats.dropped;
            return;
        }
        if (!r.read.ok) {
            // Re-spelled since the scan (NFC <-> NFD), unreadable, gone: readText() resolves the
            // spelling and does the vault's bookkeeping of a failure.
            ++stats.readFailed;
            bool ok = false;
            const QString content = vault.readText(item.path, &ok);
            if (ok) {
                index.updatePrepared(VaultIndex::prepare(item.path, content, index.config()), item.sequence);
                ++stats.parsed;
            }
            return;
        }
        vault.adoptRead(r.read);
        index.updatePrepared(std::move(r.prepared), item.sequence);
        if (r.fromCache)
            ++stats.fromCache;
        else
            ++stats.parsed;
    }

    /// Merge attachments until `budgetMs` of `timer` passed (nullptr: all), at least one.
    void mergeAttachments(const QElapsedTimer *timer, int budgetMs)
    {
        while (nextAttachment < attachments.size()) {
            const VaultPath &path = attachments[nextAttachment++];
            ++stats.merged;
            if (!forgotten.contains(path))
                index.update(path, QString());
            if (timer && timer->elapsed() >= budgetMs)
                return;
        }
    }

    qsizetype pendingResults() const
    {
        std::lock_guard lock(s->mutex);
        return qsizetype(s->results.size());
    }

    bool notesMerged() const { return mergedNotes >= noteCount(); }

    /// Inline mode, once the cache is open: take its full-text index (decoded in steps).
    void setupInlineSearch()
    {
        if (inlineSearchSet || threaded())
            return;
        if (!s->cachePath.isEmpty() && !s->cacheOpened)
            return; // not opened yet
        inlineSearchSet = true;
        if (s->searchFromCache && s->cache) {
            inlineSearch.emplace(s->cache->searchDecoder());
            index.setSearchBuiltElsewhere(true);
        }
    }

    /// Decode the cache's full-text index for about `budgetMs`; true when there is nothing (more)
    /// to decode (then it is adopted).
    bool inlineSearchStep(int budgetMs)
    {
        if (!inlineSearch)
            return true;
        if (!inlineSearch->step(budgetMs))
            return false;
        SearchIndex built = inlineSearch->take(); // empty when it failed: everything is rebuilt
        inlineSearch.reset();
        searchAdopted = true;
        index.adoptSearch(std::move(built));
        return true;
    }

    void closeSearchIfDone()
    {
        if (!notesMerged() || !s->buildSearch)
            return;
        std::lock_guard lock(s->mutex);
        if (!s->searchClosed) {
            s->searchClosed = true;
            s->searchCv.notify_all();
        }
    }

    void adoptSearchIfReady()
    {
        if (searchAdopted || !s->buildSearch || !notesMerged())
            return;
        std::optional<SearchIndex> built;
        {
            std::lock_guard lock(s->mutex);
            if (!s->searchResult)
                return;
            built = std::move(s->searchResult);
            s->searchResult.reset();
        }
        searchAdopted = true;
        index.adoptSearch(std::move(*built));
    }

    void checkDone()
    {
        if (done || nextAttachment < attachments.size() || !notesMerged() || inlineSearch)
            return;
        done = true;
        stats.elapsedMs = clock.elapsed();
        {
            std::lock_guard lock(s->mutex);
            stats.cache = s->cacheStatus;
            stats.recordsRejected = s->recordsRejected;
            stats.searchFromCache = s->searchFromCache;
        }
        closeSearchIfDone();
        adoptSearchIfReady();
        {
            // The cache's bytes are not needed any more (a search builder holds its own copy).
            std::lock_guard lock(s->mutex);
            s->cache.reset();
        }
        Q_EMIT q->finished();
    }
};

IndexLoader::IndexLoader(Vault &vault, VaultIndex &index, WorkerPool pool, Options options, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>(this, vault, index, std::move(pool), std::move(options)))
{
    d->s->receiver = this;
    d->s->maxPending = std::max(1, d->options.maxPending);
    d->s->maxPendingBytes = std::max<qint64>(1, d->options.maxPendingBytes);
    d->stats.workers = d->threaded() ? d->pool.threads : 0;
}

IndexLoader::~IndexLoader()
{
    {
        std::lock_guard lock(d->s->mutex);
        d->s->cancelled = true;
        d->s->receiver = nullptr;
    }
    d->s->workersCv.notify_all();
    d->s->searchCv.notify_all();
    d->s->ownerCv.notify_all();
    if (d->s->buildSearch && !d->searchAdopted)
        d->index.setSearchBuiltElsewhere(false);
}

void IndexLoader::start(const QList<FileStat> &files)
{
    if (d->started)
        return;
    d->started = true;
    d->clock.start();
    QList<VaultPath> notes;
    for (const FileStat &f : files) {
        if (f.isDir || !VaultIndex::isIndexable(f.path))
            continue;
        if (paths::isMarkdown(f.path))
            notes.append(f.path);
        else
            d->attachments.append(f.path);
    }
    std::sort(notes.begin(), notes.end());
    const quint64 base = d->index.reserveSequence(notes.size());
    // Merge order: the first folder (journals) newest first, then the rest by path.
    const QString first = d->options.firstFolder.isEmpty() ? QString() : d->options.firstFolder + u'/';
    QList<Item> firstItems;
    QList<Item> rest;
    for (qsizetype i = 0; i < notes.size(); ++i) {
        Item item{notes[i], d->vault.absolutePath(notes[i]), base + quint64(i)};
        if (!first.isEmpty() && notes[i].startsWith(first))
            firstItems.append(std::move(item));
        else
            rest.append(std::move(item));
    }
    std::reverse(firstItems.begin(), firstItems.end());
    d->s->items = firstItems + rest;
    d->s->config = d->index.config();
    d->s->cachePath = d->options.cachePath;
    d->s->cacheConfig = d->index.config();
    d->s->cacheIdentity = indexcache::Identity::current(d->index.config(), d->vault.id());
    if (d->s->items.isEmpty())
        d->s->cachePath.clear(); // nothing to take from it
    d->stats.total = d->s->items.size() + d->attachments.size();
    d->stats.attachments = d->attachments.size();
    if (d->threaded() && !d->s->items.isEmpty()) {
        d->s->buildSearch = true;
        d->index.setSearchBuiltElsewhere(true);
        const std::shared_ptr<LoaderShared> shared = d->s;
        const int workers = int(std::min<qsizetype>(d->pool.threads, d->s->items.size()));
        d->pool.start([shared] { Private::runSearch(shared); });
        for (int k = 0; k < workers; ++k)
            d->pool.start([shared] { Private::runWorker(shared); });
    }
    d->checkDone(); // nothing to load: done at once (an empty notebook is ready right away)
}

IndexLoader::Step IndexLoader::step(int budgetMs)
{
    if (!d->started || d->done)
        return Step::Done;
    QElapsedTimer timer;
    timer.start();
    {
        VaultIndex::Batch batch(d->index);
        d->mergeAttachments(&timer, budgetMs);
        // At least one note per step, then until the budget is used.
        while (!d->notesMerged() && (timer.elapsed() < budgetMs || d->nextAttachment >= d->attachments.size())) {
            if (std::optional<Result> r = d->pop()) {
                d->merge(std::move(*r));
            } else if (d->threaded()) {
                break; // the workers are on it: resultsReady() follows
            } else {
                const qsizetype i = d->s->next.fetch_add(1);
                if (i >= d->noteCount())
                    break;
                d->merge(Private::process(*d->s, i, d->index.config()));
                d->setupInlineSearch();
            }
            if (timer.elapsed() >= budgetMs)
                break;
        }
        if (d->notesMerged()) {
            d->setupInlineSearch();
            d->inlineSearchStep(std::max<int>(1, budgetMs - int(timer.elapsed())));
        }
    }
    d->checkDone();
    if (d->done)
        return Step::Done;
    if (!d->threaded() || d->nextAttachment < d->attachments.size())
        return Step::More;
    std::lock_guard lock(d->s->mutex);
    return d->s->results.empty() ? Step::Waiting : Step::More;
}

void IndexLoader::finish()
{
    if (!d->started || d->done)
        return;
    {
        VaultIndex::Batch batch(d->index);
        d->mergeAttachments(nullptr, 0);
        while (!d->notesMerged()) {
            if (std::optional<Result> r = d->pop()) {
                d->merge(std::move(*r));
                continue;
            }
            // Take an unstarted note; else wait for one a worker is on.
            const qsizetype i = d->s->next.fetch_add(1);
            if (i < d->noteCount()) {
                d->merge(Private::process(*d->s, i, d->index.config()));
                continue;
            }
            std::unique_lock lock(d->s->mutex);
            d->s->ownerCv.wait(lock, [&] { return !d->s->results.empty() || d->s->cancelled; });
        }
        d->setupInlineSearch();
        d->inlineSearchStep(std::numeric_limits<int>::max());
    }
    // The full-text index: wait for the builder (it is behind by the last documents only); if it
    // never got a thread, the index builds its documents itself.
    if (d->s->buildSearch && !d->searchAdopted) {
        d->closeSearchIfDone();
        std::unique_lock lock(d->s->mutex);
        if (!d->s->searchStarted) {
            d->s->searchAbandoned = true;
            d->s->searchQueue.clear();
            lock.unlock();
            d->searchAdopted = true; // nothing to adopt
            d->index.setSearchBuiltElsewhere(false);
        } else {
            d->s->ownerCv.wait(lock, [&] { return d->s->searchResult.has_value() || d->s->cancelled; });
        }
    }
    d->checkDone();
    d->adoptSearchIfReady();
}

void IndexLoader::forget(const VaultPath &path)
{
    if (!path.isEmpty())
        d->forgotten.insert(path);
}

void IndexLoader::configChanged()
{
    std::lock_guard lock(d->s->mutex);
    d->s->config = d->index.config();
}

bool IndexLoader::isDone() const
{
    return d->done || !d->started;
}

qreal IndexLoader::progress() const
{
    if (d->done || d->stats.total <= 0)
        return 1.0;
    return std::clamp(qreal(d->stats.merged) / qreal(d->stats.total), 0.0, 1.0);
}

qsizetype IndexLoader::pendingResults() const
{
    return d->pendingResults();
}

IndexLoader::Stats IndexLoader::stats() const
{
    Stats out = d->stats;
    if (!d->done && d->clock.isValid())
        out.elapsedMs = d->clock.elapsed();
    return out;
}

} // namespace brulion::notebook
