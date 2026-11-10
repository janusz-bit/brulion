#include "replacesession.h"

#include "workspace.h"

#include "util/paths.h"

#include <QElapsedTimer>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {
constexpr int ScanDebounceMs = 350;
constexpr int SliceMs = 12;

/// A query or replacement is one line: line breaks become spaces.
QString oneLine(QString text)
{
    text.replace(u'\r', u' ').replace(u'\n', u' ');
    return text;
}

/// The index of the file `path` in `plan`, else -1.
int fileIndex(const notebook::ReplacePlan &plan, const QString &path)
{
    const auto it = std::ranges::find(plan.files, path, &notebook::ReplaceFile::path);
    return it == plan.files.cend() ? -1 : int(it - plan.files.cbegin());
}
} // namespace

ReplaceSession::ReplaceSession(Workspace *workspace) : QAbstractListModel(workspace), m_workspace(workspace)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(ScanDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &ReplaceSession::startScan);
    m_slices.setInterval(0);
    connect(&m_slices, &QTimer::timeout, this, [this] {
        if (m_applyMode == ApplyMode::Applying)
            applyStep();
        else if (m_applyMode == ApplyMode::Undoing)
            undoStep();
        else if (m_scan)
            scanStep();
    });
    // Another notebook: nothing of this one applies any more.
    connect(workspace, &Workspace::openChanged, this, [this] {
        reset();
        m_slices.stop();
        m_applyMode = ApplyMode::None;
        m_written.clear();
        ++m_token;
        Q_EMIT stateChanged();
    });
}

ReplaceSession::~ReplaceSession() = default;

int ReplaceSession::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ReplaceSession::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row &row = m_rows.at(index.row());
    const notebook::ReplaceFile &file = m_plan.files.at(row.file);
    const notebook::ReplaceHit *hit = row.hit >= 0 ? &file.hits.at(row.hit) : nullptr;
    switch (role) {
    case KindRole: return row.more ? u"more"_s : hit ? u"hit"_s : u"file"_s;
    case PathRole: return file.path;
    case TitleRole: {
        const QString name = paths::basename(file.path);
        const qsizetype dot = name.lastIndexOf(u'.');
        return dot > 0 ? name.first(dot) : name;
    }
    case FolderRole: return paths::dirname(file.path);
    case HitsRole: return row.more ? file.hitCount - int(file.hits.size()) : file.hitCount;
    case TickRole: {
        if (hit) {
            const Ticks ticks = ticksOf(file.path);
            return ticks.file && !ticks.excluded.contains(hit->index) ? 2 : 0;
        }
        const int ticked = tickedInFile(file);
        return ticked == 0 ? 0 : ticked == file.hitCount ? 2 : 1;
    }
    case HitRole: return hit ? hit->index : -1;
    case LineRole: return hit ? hit->line + 1 : 0;
    case BeforeRole: return hit ? hit->before : QString();
    case MatchedRole: return hit ? hit->matched : QString();
    case AfterRole: return hit ? hit->after : QString();
    case InLinkRole: return hit ? hit->inLink : false;
    default: return {};
    }
}

QHash<int, QByteArray> ReplaceSession::roleNames() const
{
    static const QHash<int, QByteArray> names{
            {KindRole, "kind"},     {PathRole, "path"},       {TitleRole, "title"}, {FolderRole, "folder"},
            {HitsRole, "hits"},     {TickRole, "tick"},       {HitRole, "hit"},     {LineRole, "line"},
            {BeforeRole, "before"}, {MatchedRole, "matched"}, {AfterRole, "after"}, {InLinkRole, "inLink"},
    };
    return names;
}

template<typename T>
bool ReplaceSession::setParameter(T &parameter, const T &value)
{
    if (parameter == value)
        return false;
    parameter = value;
    Q_EMIT parametersChanged();
    return true;
}

void ReplaceSession::setQuery(const QString &query)
{
    if (setParameter(m_query, oneLine(query)))
        scheduleScan();
}

void ReplaceSession::setReplacement(const QString &replacement)
{
    setParameter(m_replacement, oneLine(replacement));
}

void ReplaceSession::setMatchCase(bool on)
{
    if (setParameter(m_options.matchCase, on))
        scheduleScan();
}

void ReplaceSession::setMatchDiacritics(bool on)
{
    if (setParameter(m_options.matchDiacritics, on))
        scheduleScan();
}

void ReplaceSession::setWholeWord(bool on)
{
    if (setParameter(m_options.wholeWord, on))
        scheduleScan();
}

qreal ReplaceSession::scanProgress() const
{
    return m_scan && m_scan->total() > 0 ? qreal(m_scan->done()) / m_scan->total() : 0;
}

qreal ReplaceSession::applyProgress() const
{
    const qsizetype total = m_applyMode == ApplyMode::Applying ? m_choices.size() : m_written.size();
    return total > 0 ? qreal(m_applyAt) / total : 0;
}

ReplaceSession::Ticks ReplaceSession::ticksOf(const QString &path) const
{
    return m_ticks.value(path, Ticks{m_defaultTicked, {}});
}

int ReplaceSession::tickedInFile(const notebook::ReplaceFile &file) const
{
    const Ticks ticks = ticksOf(file.path);
    return ticks.file ? file.hitCount - int(ticks.excluded.size()) : 0;
}

int ReplaceSession::tickedFiles() const
{
    return int(std::ranges::count_if(m_plan.files,
                                     [this](const notebook::ReplaceFile &file) { return tickedInFile(file) > 0; }));
}

int ReplaceSession::tickedHits() const
{
    int out = 0;
    for (const notebook::ReplaceFile &file : m_plan.files)
        out += tickedInFile(file);
    return out;
}

void ReplaceSession::forgetResults()
{
    beginResetModel();
    m_plan = {};
    m_rows.clear();
    m_ticks.clear();
    m_defaultTicked = true;
    endResetModel();
    Q_EMIT resultsChanged();
}

void ReplaceSession::keepTicks()
{
    if (m_plan.files.isEmpty())
        return;
    // (A scan still taking kept ticks back: those of notes it has not reached stay kept.)
    if (!m_scan || !m_kept.valid || m_kept.query != m_query || m_kept.options != m_options)
        m_kept = {};
    m_kept.valid = true;
    m_kept.query = m_query;
    m_kept.options = m_options;
    m_kept.defaultTicked = m_defaultTicked;
    for (const notebook::ReplaceFile &file : std::as_const(m_plan.files)) {
        if (const auto it = m_ticks.constFind(file.path); it != m_ticks.cend())
            m_kept.files.insert(file.path, { file.hash, *it });
    }
}

void ReplaceSession::dropKeptTicks()
{
    m_kept = {};
}

void ReplaceSession::adoptKeptTicks()
{
    if (!m_kept.valid)
        return;
    for (const notebook::ReplaceFile &file : std::as_const(m_plan.files)) {
        const auto it = m_kept.files.constFind(file.path);
        if (it != m_kept.files.cend() && it->hash == file.hash && !m_ticks.contains(file.path))
            m_ticks.insert(file.path, it->ticks);
    }
    if (!m_scan) // the scan is done: every note had its ticks back
        m_kept = {};
}

void ReplaceSession::scheduleScan()
{
    reset();
    if (!m_query.isEmpty())
        m_debounce.start();
}

void ReplaceSession::scanNow()
{
    reset();
    startScan();
}

void ReplaceSession::reset()
{
    m_debounce.stop();
    stopScan();
    forgetResults();
}

void ReplaceSession::stopScan()
{
    if (!m_scan)
        return;
    m_scan.reset();
    if (m_applyMode == ApplyMode::None)
        m_slices.stop();
    Q_EMIT stateChanged();
}

void ReplaceSession::startScan()
{
    if (!m_workspace->vault() || m_query.isEmpty() || applying())
        return;
    if (m_kept.valid && (m_kept.query != m_query || m_kept.options != m_options))
        m_kept = {};
    if (m_kept.valid)
        m_defaultTicked = m_kept.defaultTicked;
    // What editors hold goes to the files first: the plan looks at the files.
    Q_EMIT m_workspace->saveOpenEditorsRequested();
    m_scan = std::make_unique<notebook::ReplaceScan>(*m_workspace->vault(), m_workspace->workerPool(), m_query, m_options);
    connect(m_scan.get(), &notebook::ReplaceScan::resultsReady, this, &ReplaceSession::scanStep);
    Q_EMIT stateChanged();
    Q_EMIT progressChanged();
    scanStep();
}

void ReplaceSession::scanStep()
{
    if (!m_scan)
        return;
    const notebook::ReplaceScan::Step step = m_scan->step(SliceMs);
    if (!m_scan->takeMerged().isEmpty())
        showPlan(m_scan->plan());
    else
        m_plan.skipped = m_scan->plan().skipped; // (notes without hits and skipped ones count too)
    Q_EMIT progressChanged();
    if (!m_scan) // a slot stopped it
        return;
    switch (step) {
    case notebook::ReplaceScan::Step::Done: {
        notebook::ReplacePlan plan = m_scan->plan();
        m_scan.reset();
        if (m_applyMode == ApplyMode::None)
            m_slices.stop();
        showPlan(std::move(plan));
        Q_EMIT stateChanged();
        break;
    }
    case notebook::ReplaceScan::Step::More:
        m_slices.start(); // the event loop gets its turn
        break;
    case notebook::ReplaceScan::Step::Waiting:
        if (m_applyMode == ApplyMode::None)
            m_slices.stop(); // resultsReady() follows
        break;
    }
}

void ReplaceSession::showPlan(notebook::ReplacePlan plan)
{
    QList<Row> rows;
    rows.reserve(2 * plan.files.size() + plan.previewHits);
    for (int f = 0; f < plan.files.size(); ++f) {
        const notebook::ReplaceFile &file = plan.files.at(f);
        rows.append({ f, -1, false });
        for (int h = 0; h < file.hits.size(); ++h)
            rows.append({ f, h, false });
        if (file.hitCount > file.hits.size())
            rows.append({ f, -1, true });
    }
    // (While a scan merges; afterwards the rows only change when a new scan starts.)
    beginResetModel();
    m_plan = std::move(plan);
    adoptKeptTicks();
    m_rows = std::move(rows);
    endResetModel();
    Q_EMIT resultsChanged();
}

void ReplaceSession::fileTickChanged(int file)
{
    // (The rows are in the order of their files.)
    const auto [first, end] = std::ranges::equal_range(std::as_const(m_rows), file, {}, &Row::file);
    if (first == end)
        return;
    Q_EMIT dataChanged(index(int(first - m_rows.cbegin())), index(int(end - m_rows.cbegin()) - 1), {TickRole});
    Q_EMIT resultsChanged();
}

void ReplaceSession::setFileTicked(const QString &path, bool ticked)
{
    const int f = fileIndex(m_plan, path);
    if (f < 0)
        return;
    m_ticks.insert(path, Ticks{ticked, {}});
    fileTickChanged(f);
}

void ReplaceSession::setHitTicked(const QString &path, int hit, bool ticked)
{
    const int f = fileIndex(m_plan, path);
    const int hitCount = f < 0 ? 0 : m_plan.files.at(f).hitCount;
    if (hit < 0 || hit >= hitCount)
        return;
    // Its own ticks from here on (the default ones if it has none yet), changed in place.
    Ticks &ticks = *m_ticks.insert(path, ticksOf(path));
    if (ticked && !ticks.file) {
        // From nothing ticked: only this one (as far as the preview names them).
        ticks.file = true;
        ticks.excluded.clear();
        for (int i = 0; i < hitCount; ++i) {
            if (i != hit)
                ticks.excluded.insert(i);
        }
    } else if (ticked) {
        ticks.excluded.remove(hit);
    } else {
        ticks.excluded.insert(hit);
    }
    fileTickChanged(f);
}

void ReplaceSession::tickAll(bool ticked)
{
    m_ticks.clear();
    m_defaultTicked = ticked;
    m_kept = {};
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), { TickRole });
    Q_EMIT resultsChanged();
}

bool ReplaceSession::apply()
{
    if (applying() || m_scan || tickedHits() == 0 || !m_workspace->vault())
        return false;
    Q_EMIT m_workspace->saveOpenEditorsRequested();
    m_choices.clear();
    for (const notebook::ReplaceFile &file : std::as_const(m_plan.files)) {
        if (tickedInFile(file) > 0)
            m_choices.append({ file.path, m_ticks.value(file.path).excluded });
    }
    m_applyPlan = m_plan;
    m_applyReplacement = m_replacement;
    m_written.clear();
    m_changed.clear();
    m_failed.clear();
    m_appliedHits = 0;
    m_applyAt = 0;
    ++m_token;
    m_applyMode = ApplyMode::Applying;
    Q_EMIT stateChanged();
    Q_EMIT progressChanged();
    applyStep();
    return true;
}

void ReplaceSession::applyStep()
{
    Vault *vault = m_workspace->vault();
    QElapsedTimer clock;
    clock.start();
    while (vault && m_applyAt < m_choices.size() && clock.elapsed() < SliceMs) {
        const notebook::ReplaceChoice &choice = m_choices.at(m_applyAt++);
        const int file = fileIndex(m_applyPlan, choice.path);
        if (file < 0)
            continue;
        notebook::ReplacedFile done = notebook::applyToFile(*vault, m_applyPlan.files.at(file), m_applyPlan,
                                                            m_applyReplacement, choice.excluded);
        switch (done.status) {
        case notebook::ReplaceStatus::Written:
            m_appliedHits += done.replaced;
            if (!done.after.isEmpty() || !done.before.isEmpty())
                m_written.append(std::move(done));
            break;
        case notebook::ReplaceStatus::Changed:
            m_changed.append(choice.path);
            break;
        case notebook::ReplaceStatus::Failed:
            m_failed.append(choice.path);
            break;
        }
    }
    Q_EMIT progressChanged();
    if (vault && m_applyAt < m_choices.size()) {
        m_slices.start();
        return;
    }
    m_slices.stop();
    m_applyMode = ApplyMode::None;
    m_choices.clear();
    m_applyPlan = {};
    const int files = int(m_written.size());
    const QStringList changed = m_changed;
    const QStringList failed = m_failed;
    Q_EMIT stateChanged();
    Q_EMIT applied(m_token, m_appliedHits, files, changed, failed);
}

bool ReplaceSession::canUndo(int token) const
{
    return token == m_token && !m_written.isEmpty() && !applying() && m_workspace->vault();
}

bool ReplaceSession::undo(int token)
{
    if (!canUndo(token))
        return false;
    // The rescan after the replace: the files are about to change again (onUndone() looks anew).
    m_debounce.stop();
    stopScan();
    m_changed.clear();
    m_undoRestored = 0;
    m_applyAt = 0;
    m_applyMode = ApplyMode::Undoing;
    Q_EMIT stateChanged();
    undoStep();
    return true;
}

void ReplaceSession::undoStep()
{
    Vault *vault = m_workspace->vault();
    QElapsedTimer clock;
    clock.start();
    while (vault && m_applyAt < m_written.size() && clock.elapsed() < SliceMs) {
        const notebook::ReplacedFile &file = m_written.at(m_applyAt++);
        if (notebook::undoReplacedFile(*vault, file) == notebook::UndoReplaceStatus::Restored)
            ++m_undoRestored;
        else
            m_changed.append(file.path);
    }
    Q_EMIT progressChanged();
    if (vault && m_applyAt < m_written.size()) {
        m_slices.start();
        return;
    }
    m_slices.stop();
    m_applyMode = ApplyMode::None;
    m_written.clear();
    const QStringList left = m_changed;
    Q_EMIT stateChanged();
    Q_EMIT undone(m_undoRestored, left);
}

} // namespace brulion::shell
