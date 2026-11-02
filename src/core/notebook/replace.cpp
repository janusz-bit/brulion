#include "notebook/replace.h"

#include "drawing/excalidrawmd.h"
#include "text/text.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QStringDecoder>

#include <algorithm>
#include <atomic>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

constexpr qsizetype ContextBefore = 40;
constexpr qsizetype ContextAfter = 70;

/// A cut does not fall inside a surrogate pair or between a letter and its marks.
qsizetype snapBack(QStringView s, qsizetype at)
{
    while (at > 0 && at < s.size() && (s.at(at).isLowSurrogate() || s.at(at).isMark()))
        --at;
    return at;
}

/// `text`'s lines around `range`: up to ContextBefore units before it and ContextAfter after it.
void fillContext(ReplaceHit &hit, QStringView text, qsizetype lineStart, qsizetype lineEnd)
{
    hit.matched = text.sliced(hit.start, hit.length).toString();
    const qsizetype matchEnd = hit.start + hit.length;
    qsizetype from = lineStart;
    if (hit.start - lineStart > ContextBefore)
        from = snapBack(text, hit.start - ContextBefore);
    hit.before = text.sliced(from, hit.start - from).toString();
    if (from > lineStart)
        hit.before.prepend(u'…');
    qsizetype to = lineEnd;
    if (lineEnd - matchEnd > ContextAfter)
        to = snapBack(text, matchEnd + ContextAfter);
    hit.after = text.sliced(matchEnd, to - matchEnd).toString();
    if (to < lineEnd)
        hit.after.append(u'…');
}

qsizetype lineEndFrom(QStringView text, qsizetype from)
{
    for (qsizetype i = from; i < text.size(); ++i) {
        if (text.at(i) == u'\n' || text.at(i) == u'\r')
            return i;
    }
    return text.size();
}

bool validUtf8(QByteArrayView bytes)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    [[maybe_unused]] const QString decoded = decoder.decode(bytes);
    return !decoder.hasError();
}

/// A drawing of Obsidian's plugin; else the bytes as Brulion can write them back: UTF-8 (a BOM is
/// the vault's), a size it keeps in History.
ReadKind kindOf(const DetachedRead &read)
{
    if (!read.ok)
        return ReadKind::Unusable;
    if (drawing::looksLikeExcalidrawMarkdown(QByteArrayView(read.bytes).first(std::min<qsizetype>(read.bytes.size(), 4096))))
        return ReadKind::Drawing;
    return read.bytes.size() <= ReplaceMaxFileBytes && validUtf8(read.bytes) ? ReadKind::Note : ReadKind::Unusable;
}

/// The vault's snapshots of this write are kept whatever came just before ("replace": History).
class HistoryReason {
public:
    explicit HistoryReason(Vault &vault) : m_vault(vault), m_previous(vault.historyReason())
    {
        m_vault.setHistoryReason(u"replace"_s);
    }
    ~HistoryReason() { m_vault.setHistoryReason(m_previous); }
    HistoryReason(const HistoryReason &) = delete;
    HistoryReason &operator=(const HistoryReason &) = delete;

private:
    Vault &m_vault;
    QString m_previous;
};

} // namespace

bool isReplaceableNote(const VaultPath &path)
{
    if (!paths::isMarkdown(path) || path.endsWith(u".excalidraw.md"_s, Qt::CaseInsensitive))
        return false;
    if (path.startsWith(u'.') || path.contains(u"/."_s))
        return false;
    return !paths::isConflictCopy(path);
}

ReplaceFile planFile(const VaultPath &path, QStringView text, QStringView query, const text::FindOptions &options,
                     int previewHits)
{
    ReplaceFile file;
    file.path = path;
    const QList<text::TextRange> ranges = text::findMatches(text, query, options);
    file.hitCount = int(ranges.size());
    const qsizetype shown = std::min<qsizetype>(ranges.size(), std::max(previewHits, 0));
    file.hits.reserve(shown);
    int line = 0;
    qsizetype lineStart = 0;
    for (qsizetype i = 0; i < shown; ++i) {
        const text::TextRange range = ranges.at(i);
        // Forward to the match's line (queries have no line breaks: it stays on one).
        for (qsizetype at = lineStart; at < range.start; ++at) {
            const QChar c = text.at(at);
            if (c == u'\n' || (c == u'\r' && !(at + 1 < text.size() && text.at(at + 1) == u'\n'))) {
                ++line;
                lineStart = at + 1;
            }
        }
        ReplaceHit hit;
        hit.index = int(i);
        hit.line = line;
        hit.start = range.start;
        hit.length = range.length;
        const qsizetype lineEnd = lineEndFrom(text, range.end());
        fillContext(hit, text, lineStart, lineEnd);
        hit.inLink = text::isInsideLink(text, range);
        file.hits.append(std::move(hit));
    }
    return file;
}

ReplaceFile planDetachedRead(const DetachedRead &read, QStringView query, const text::FindOptions &options,
                             int previewHits, ReadKind *kind)
{
    const ReadKind found = kindOf(read);
    if (kind)
        *kind = found;
    if (found != ReadKind::Note) {
        ReplaceFile none;
        none.path = read.path;
        return none;
    }
    ReplaceFile file = planFile(read.path, read.text, query, options, previewHits);
    file.hash = read.hash;
    return file;
}

ReplacePlan planReplace(Vault &vault, const QString &query, const text::FindOptions &options,
                        const ReplaceLimits &limits)
{
    ReplaceScan scan(vault, {}, query, options, limits);
    while (scan.step(1000) != ReplaceScan::Step::Done) { }
    return scan.plan();
}

ReplacedFile applyToFile(Vault &vault, const ReplaceFile &file, const ReplacePlan &plan, const QString &replacement,
                         const QSet<int> &excluded, ChangeSource source)
{
    ReplacedFile out;
    out.path = file.path;
    const QString absolutePath = vault.absolutePath(file.path);
    const DetachedRead read = Vault::readDetached(file.path, absolutePath);
    if (!read.ok) {
        out.status = QFile::exists(absolutePath) ? ReplaceStatus::Failed : ReplaceStatus::Changed;
        return out;
    }
    if (read.hash != file.hash) {
        out.status = ReplaceStatus::Changed;
        return out;
    }
    QList<text::TextRange> ranges = text::findMatches(read.text, plan.query, plan.options);
    QList<text::TextRange> chosen;
    chosen.reserve(ranges.size());
    for (qsizetype i = 0; i < ranges.size(); ++i) {
        if (!excluded.contains(int(i)))
            chosen.append(ranges.at(i));
    }
    out.status = ReplaceStatus::Written;
    out.replaced = int(chosen.size());
    if (chosen.isEmpty())
        return out;
    QString after = text::replaceMatches(read.text, chosen, replacement);
    if (after == read.text) {
        out.replaced = 0; // the same text: nothing written, nothing to undo
        return out;
    }
    out.before = read.text;
    out.after = std::move(after);
    const HistoryReason reason(vault);
    switch (vault.writeTextIfUnchanged(file.path, out.after, out.before, source)) {
    case WriteOutcome::Written:
        break;
    case WriteOutcome::Changed:
        out.status = ReplaceStatus::Changed;
        out.replaced = 0;
        break;
    case WriteOutcome::Failed:
        out.status = ReplaceStatus::Failed;
        out.replaced = 0;
        break;
    }
    return out;
}

QList<ReplacedFile> applyReplace(Vault &vault, const ReplacePlan &plan, const QString &replacement,
                                 const QList<ReplaceChoice> &choices, ChangeSource source)
{
    QList<ReplacedFile> out;
    out.reserve(choices.size());
    for (const ReplaceChoice &choice : choices) {
        const auto file = std::find_if(plan.files.cbegin(), plan.files.cend(),
                                       [&](const ReplaceFile &f) { return f.path == choice.path; });
        if (file != plan.files.cend())
            out.append(applyToFile(vault, *file, plan, replacement, choice.excluded, source));
    }
    return out;
}

UndoReplaceStatus undoReplacedFile(Vault &vault, const ReplacedFile &file, ChangeSource source)
{
    // On the disk, not the vault's listing: writeTextIfUnchanged() would create a deleted file.
    if (!QFileInfo(vault.absolutePath(file.path)).isFile())
        return UndoReplaceStatus::Changed;
    const HistoryReason reason(vault);
    switch (vault.writeTextIfUnchanged(file.path, file.before, file.after, source)) {
    case WriteOutcome::Written:
        return UndoReplaceStatus::Restored;
    case WriteOutcome::Changed:
        return UndoReplaceStatus::Changed;
    case WriteOutcome::Failed:
        break;
    }
    return UndoReplaceStatus::Failed;
}

// ---- ReplaceScan -----------------------------------------------------------------------------------

namespace {

struct ScanJob {
    VaultPath path;
    QString absolutePath;
};

struct ScanResult {
    int job = 0;
    ReplaceFile file;
    ReadKind kind = ReadKind::Note;
};

/// Shared with the workers, who may outlive the scan by the note they are reading.
struct ScanShared {
    QList<ScanJob> jobs;
    QString query;
    text::FindOptions options;
    int previewPerFile = 0;
    std::atomic<qsizetype> next{0};
    std::atomic<bool> cancelled{false};
    QMutex mutex;
    QList<ScanResult> results; ///< guarded by mutex
    QObject *owner = nullptr;  ///< guarded by mutex; the scan, until it goes
    bool notified = false;     ///< guarded by mutex

    ScanResult run(int job) const
    {
        const ScanJob &item = jobs.at(job);
        ScanResult result;
        result.job = job;
        const DetachedRead read = Vault::readDetached(item.path, item.absolutePath);
        result.file = planDetachedRead(read, query, options, previewPerFile, &result.kind);
        return result;
    }
};

} // namespace

struct ReplaceScan::Private {
    WorkerPool pool;
    ReplaceLimits limits;
    std::shared_ptr<ScanShared> shared;
    ReplacePlan plan;
    QList<int> fileJobs; ///< the job of each plan file: results arrive in any order
    QList<VaultPath> merged;
    int done = 0;

    void merge(ScanResult &&result)
    {
        ++done;
        ++plan.notesScanned;
        if (result.kind == ReadKind::Unusable)
            plan.skipped.append(result.file.path);
        if (result.kind != ReadKind::Note)
            return;
        if (result.file.hitCount == 0)
            return;
        ReplaceFile file = std::move(result.file);
        plan.totalHits += file.hitCount;
        const qsizetype room = std::max(0, limits.previewTotal - plan.previewHits);
        if (file.hits.size() > room)
            file.hits.resize(room);
        plan.previewHits += int(file.hits.size());
        const auto at = std::lower_bound(fileJobs.cbegin(), fileJobs.cend(), result.job);
        const qsizetype row = std::distance(fileJobs.cbegin(), at);
        fileJobs.insert(row, result.job);
        merged.append(file.path);
        plan.files.insert(row, std::move(file));
    }
};

ReplaceScan::ReplaceScan(Vault &vault, WorkerPool pool, const QString &query, const text::FindOptions &options,
                         const ReplaceLimits &limits, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->pool = std::move(pool);
    d->limits = limits;
    d->plan.query = query;
    d->plan.options = options;
    d->shared = std::make_shared<ScanShared>();
    d->shared->query = query;
    d->shared->options = options;
    d->shared->previewPerFile = limits.previewPerFile;
    d->shared->owner = this;
    const QList<FileStat> notes = vault.markdownFiles();
    d->shared->jobs.reserve(notes.size());
    for (const FileStat &note : notes) {
        if (isReplaceableNote(note.path))
            d->shared->jobs.append({note.path, vault.absolutePath(note.path)});
    }
    if (d->pool.isInline() || d->shared->jobs.isEmpty())
        return;
    const int workers = std::min<qsizetype>(d->pool.threads, d->shared->jobs.size());
    for (int i = 0; i < workers; ++i) {
        d->pool.start([shared = d->shared] {
            for (;;) {
                if (shared->cancelled.load())
                    return;
                const qsizetype job = shared->next.fetch_add(1);
                if (job >= shared->jobs.size())
                    return;
                ScanResult result = shared->run(int(job));
                const QMutexLocker lock(&shared->mutex);
                shared->results.append(std::move(result));
                if (shared->owner && !shared->notified) {
                    shared->notified = true;
                    QMetaObject::invokeMethod(shared->owner, "resultsReady", Qt::QueuedConnection);
                }
            }
        });
    }
}

ReplaceScan::~ReplaceScan()
{
    d->shared->cancelled.store(true);
    const QMutexLocker lock(&d->shared->mutex);
    d->shared->owner = nullptr;
}

ReplaceScan::Step ReplaceScan::step(int budgetMs)
{
    QElapsedTimer clock;
    clock.start();
    if (d->pool.isInline()) {
        while (d->shared->next.load() < d->shared->jobs.size()) {
            d->merge(d->shared->run(int(d->shared->next.fetch_add(1))));
            if (clock.elapsed() >= budgetMs)
                break;
        }
        return isDone() ? Step::Done : Step::More;
    }
    QList<ScanResult> ready;
    {
        const QMutexLocker lock(&d->shared->mutex);
        ready.swap(d->shared->results);
        d->shared->notified = false;
    }
    for (ScanResult &result : ready)
        d->merge(std::move(result));
    return isDone() ? Step::Done : Step::Waiting;
}

bool ReplaceScan::isDone() const
{
    return d->done >= d->shared->jobs.size();
}

int ReplaceScan::done() const
{
    return d->done;
}

int ReplaceScan::total() const
{
    return int(d->shared->jobs.size());
}

const ReplacePlan &ReplaceScan::plan() const
{
    return d->plan;
}

QList<VaultPath> ReplaceScan::takeMerged()
{
    return std::exchange(d->merged, {});
}

} // namespace brulion::notebook
