#include "replacesession.h"

#include "workspace.h"

#include "util/paths.h"

#include <QElapsedTimer>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {
constexpr int ScanDebounceMs = 350;
constexpr int SliceMs = 12;
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
    const Ticks ticks = m_ticks.value(file.path);
    const bool isHit = row.hit >= 0;
    const notebook::ReplaceHit *hit = isHit ? &file.hits.at(row.hit) : nullptr;
    switch (role) {
    case KindRole: return row.more ? u"more"_s : isHit ? u"hit"_s : u"file"_s;
    case PathRole: return file.path;
    case TitleRole: {
        const QString name = paths::basename(file.path);
        const qsizetype dot = name.lastIndexOf(u'.');
        return dot > 0 ? name.first(dot) : name;
    }
    case FolderRole: return paths::dirname(file.path);
    case HitsRole: return row.more ? file.hitCount - int(file.hits.size()) : file.hitCount;
    case TickRole: {
        if (isHit)
            return ticks.file && !ticks.excluded.contains(hit->index) ? 2 : 0;
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
    return { { KindRole, "kind" },        { PathRole, "path" },       { TitleRole, "title" },
             { FolderRole, "folder" },    { HitsRole, "hits" },       { TickRole, "tick" },
             { HitRole, "hit" },          { LineRole, "line" },       { BeforeRole, "before" },
             { MatchedRole, "matched" },  { AfterRole, "after" },     { InLinkRole, "inLink" } };
}

void ReplaceSession::setQuery(const QString &query)
{
    QString line = query;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
    if (m_query == line)
        return;
    m_query = line;
    Q_EMIT parametersChanged();
    scheduleScan();
}

void ReplaceSession::setReplacement(const QString &replacement)
{
    QString line = replacement;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
    if (m_replacement == line)
        return;
    m_replacement = line;
    Q_EMIT parametersChanged();
}

void ReplaceSession::setMatchCase(bool on)
{
    if (m_options.matchCase == on)
        return;
    m_options.matchCase = on;
    Q_EMIT parametersChanged();
    scheduleScan();
}

void ReplaceSession::setMatchDiacritics(bool on)
{
    if (m_options.matchDiacritics == on)
        return;
    m_options.matchDiacritics = on;
    Q_EMIT parametersChanged();
    scheduleScan();
}

void ReplaceSession::setWholeWord(bool on)
{
    if (m_options.wholeWord == on)
        return;
    m_options.wholeWord = on;
    Q_EMIT parametersChanged();
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

int ReplaceSession::tickedInFile(const notebook::ReplaceFile &file) const
{
    const auto it = m_ticks.constFind(file.path);
    if (it == m_ticks.cend())
        return file.hitCount;
    return it->file ? file.hitCount - int(it->excluded.size()) : 0;
}

int ReplaceSession::tickedFiles() const
{
    int out = 0;
    for (const notebook::ReplaceFile &file : m_plan.files)
        out += tickedInFile(file) > 0 ? 1 : 0;
    return out;
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
    endResetModel();
    Q_EMIT resultsChanged();
}

void ReplaceSession::scheduleScan()
{
    stopScan();
    forgetResults();
    if (!m_query.isEmpty())
        m_debounce.start();
    else
        m_debounce.stop();
}

void ReplaceSession::scanNow()
{
    m_debounce.stop();
    stopScan();
    forgetResults();
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
    if (!m_scan->takeMerged().isEmpty()) {
        m_plan = m_scan->plan();
        rebuildRows();
        Q_EMIT resultsChanged();
    } else {
        // (Notes without hits and skipped ones count too.)
        m_plan.skipped = m_scan->plan().skipped;
    }
    Q_EMIT progressChanged();
    switch (step) {
    case notebook::ReplaceScan::Step::Done:
        m_plan = m_scan->plan();
        m_scan.reset();
        m_slices.stop();
        rebuildRows();
        Q_EMIT resultsChanged();
        Q_EMIT stateChanged();
        break;
    case notebook::ReplaceScan::Step::More:
        m_slices.start(); // the event loop gets its turn
        break;
    case notebook::ReplaceScan::Step::Waiting:
        m_slices.stop(); // resultsReady() follows
        break;
    }
}

void ReplaceSession::rebuildRows()
{
    QList<Row> rows;
    for (int f = 0; f < m_plan.files.size(); ++f) {
        const notebook::ReplaceFile &file = m_plan.files.at(f);
        rows.append({ f, -1, false });
        for (int h = 0; h < file.hits.size(); ++h)
            rows.append({ f, h, false });
        if (file.hitCount > file.hits.size())
            rows.append({ f, -1, true });
    }
    // (While a scan merges; afterwards the rows only change when a new scan starts.)
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int ReplaceSession::fileRow(int file) const
{
    for (int r = 0; r < m_rows.size(); ++r) {
        if (m_rows.at(r).file == file)
            return r;
    }
    return -1;
}

void ReplaceSession::fileTickChanged(int file)
{
    const int first = fileRow(file);
    if (first < 0)
        return;
    int last = first;
    while (last + 1 < m_rows.size() && m_rows.at(last + 1).file == file)
        ++last;
    Q_EMIT dataChanged(index(first), index(last), { TickRole });
    Q_EMIT resultsChanged();
}

void ReplaceSession::setFileTicked(const QString &path, bool ticked)
{
    for (int f = 0; f < m_plan.files.size(); ++f) {
        if (m_plan.files.at(f).path == path) {
            Ticks &ticks = m_ticks[path];
            ticks.file = ticked;
            ticks.excluded.clear();
            fileTickChanged(f);
            return;
        }
    }
}

void ReplaceSession::setHitTicked(const QString &path, int hit, bool ticked)
{
    for (int f = 0; f < m_plan.files.size(); ++f) {
        const notebook::ReplaceFile &file = m_plan.files.at(f);
        if (file.path != path)
            continue;
        Ticks &ticks = m_ticks[path];
        if (ticked && !ticks.file) {
            // From nothing ticked: only this one (as far as the preview names them).
            ticks.file = true;
            ticks.excluded.clear();
            for (int i = 0; i < file.hitCount; ++i) {
                if (i != hit)
                    ticks.excluded.insert(i);
            }
        } else if (ticked) {
            ticks.excluded.remove(hit);
        } else {
            ticks.excluded.insert(hit);
        }
        fileTickChanged(f);
        return;
    }
}

void ReplaceSession::tickAll(bool ticked)
{
    m_ticks.clear();
    if (!ticked) {
        for (const notebook::ReplaceFile &file : std::as_const(m_plan.files))
            m_ticks[file.path].file = false;
    }
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
        const auto file = std::find_if(m_applyPlan.files.cbegin(), m_applyPlan.files.cend(),
                                       [&](const notebook::ReplaceFile &f) { return f.path == choice.path; });
        if (file == m_applyPlan.files.cend())
            continue;
        notebook::ReplacedFile done = notebook::applyToFile(*vault, *file, m_applyPlan, m_applyReplacement, choice.excluded);
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
