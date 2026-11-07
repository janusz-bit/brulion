#include "streamfind.h"

#include "blocklistmodel.h"
#include "markdown/frontmatter.h"

#include <QTimer>

#include <memory>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

/// Recount the other days this long after a change (a day's edit, a file synced in, new days).
constexpr int RecountDelayMs = 150;

QString oneLine(const QString &text)
{
    QString line = text;
    line.replace(u'\r', u' ').replace(u'\n', u' ');
    return line;
}

} // namespace

StreamFind::StreamFind(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(RecountDelayMs);
    connect(&m_timer, &QTimer::timeout, this, &StreamFind::recount);
}

StreamFind::~StreamFind() = default;

void StreamFind::setHost(EditorHost *host)
{
    if (m_host == host)
        return;
    disconnect(m_hostConnection);
    m_host = host;
    m_cache.clear();
    if (m_host)
        m_hostConnection = connect(m_host, &EditorHost::fileChanged, this, &StreamFind::onFileChanged);
    Q_EMIT hostChanged();
    schedule();
}

void StreamFind::setModel(QAbstractItemModel *model)
{
    if (m_model == model)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_modelConnections))
        disconnect(c);
    m_modelConnections.clear();
    m_model = model;
    if (m_model) {
        const auto changed = [this] { schedule(); };
        m_modelConnections = {connect(m_model, &QAbstractItemModel::rowsInserted, this, changed),
                              connect(m_model, &QAbstractItemModel::rowsRemoved, this, changed),
                              connect(m_model, &QAbstractItemModel::rowsMoved, this, changed),
                              connect(m_model, &QAbstractItemModel::modelReset, this, changed),
                              connect(m_model, &QAbstractItemModel::dataChanged, this, changed)};
    }
    Q_EMIT modelChanged();
    schedule();
}

void StreamFind::setFind(NoteFind *find)
{
    if (m_find == find)
        return;
    for (const QMetaObject::Connection &c : std::as_const(m_findConnections))
        disconnect(c);
    m_findConnections.clear();
    if (m_find)
        m_find->setWrap(true);
    m_find = find;
    if (m_find) {
        m_find->setWrap(false);
        m_findConnections = {
                connect(m_find, &NoteFind::matchesChanged, this,
                        [this] {
                            // A new query or new options: every day again (a day's edit: its own count).
                            if (m_dirty || m_find->appliedQuery() != m_query || m_find->options() != m_options)
                                recount();
                            else
                                Q_EMIT changed();
                        }),
                connect(m_find, &NoteFind::activeChanged, this, [this] { recount(); }),
                connect(m_find, &NoteFind::controllerChanged, this, [this] { Q_EMIT changed(); }),
                connect(m_find, &NoteFind::endReached, this, &StreamFind::moveToDay),
        };
    }
    Q_EMIT findChanged();
    recount();
}

void StreamFind::setDayLimit(int limit)
{
    limit = std::max(0, limit);
    if (m_dayLimit == limit)
        return;
    m_dayLimit = limit;
    Q_EMIT dayLimitChanged();
    schedule();
}

void StreamFind::attach(PageController *page)
{
    if (!page || m_pageConnections.contains(page))
        return;
    m_pages.append(page);
    const auto changed = [this] { schedule(); };
    BlockListModel *blocks = page->blocks();
    m_pageConnections.insert(page, {connect(blocks, &QAbstractItemModel::dataChanged, this, changed),
                                    connect(blocks, &QAbstractItemModel::rowsInserted, this, changed),
                                    connect(blocks, &QAbstractItemModel::rowsRemoved, this, changed),
                                    connect(blocks, &QAbstractItemModel::modelReset, this, changed),
                                    connect(page, &PageController::loadingChanged, this, changed),
                                    connect(page, &QObject::destroyed, this, [this, page] { detach(page); })});
    schedule();
}

void StreamFind::detach(PageController *page)
{
    const auto it = m_pageConnections.find(page);
    if (it == m_pageConnections.end())
        return;
    for (const QMetaObject::Connection &c : std::as_const(*it))
        disconnect(c);
    m_pageConnections.erase(it);
    m_pages.removeIf([page](const QPointer<PageController> &p) { return p.isNull() || p.data() == page; });
    // Its edits are in its file now (or soon: the file's change comes through fileChanged()).
    schedule();
}

PageController *StreamFind::attached(const QString &path) const
{
    for (const QPointer<PageController> &p : m_pages) {
        if (p && p->path() == path)
            return p;
    }
    return nullptr;
}

QString StreamFind::activePath() const
{
    return m_find && m_find->controller() ? m_find->controller()->path() : QString();
}

void StreamFind::schedule()
{
    m_dirty = true;
    if (m_find && m_find->active())
        m_timer.start();
}

void StreamFind::refresh()
{
    if (m_find)
        m_find->refresh();
    if (m_dirty || m_timer.isActive()
        || (m_find && (m_find->appliedQuery() != m_query || m_find->options() != m_options)))
        recount();
}

void StreamFind::onFileChanged(const QString &path)
{
    m_cache.remove(path);
    for (const Day &d : std::as_const(m_days)) {
        if (d.path == path) {
            schedule();
            return;
        }
    }
}

QString StreamFind::textOf(const QString &path, bool *ok)
{
    if (PageController *page = attached(path); page && page->loaded()) {
        *ok = true;
        return page->sourceMode() ? page->sourceText() : page->text();
    }
    if (const auto it = m_cache.constFind(path); it != m_cache.cend()) {
        *ok = it->ok;
        return it->text;
    }
    CachedText cached;
    if (m_host)
        cached.text = m_host->read(path, &cached.ok);
    if (!cached.ok)
        cached.text.clear();
    m_cache.insert(path, cached);
    *ok = cached.ok;
    return cached.text;
}

void StreamFind::countDay(Day &day, const QString &text) const
{
    const QList<text::TextRange> matches = text::findMatches(text, m_query, m_options);
    day.matches = int(matches.size());
    day.links = 0;
    day.frontmatter = 0;
    if (matches.isEmpty())
        return;
    const std::optional<markdown::FrontmatterBlock> frontmatter = markdown::findFrontmatter(text);
    const qsizetype bodyStart = frontmatter ? frontmatter->bodyStart : 0;
    for (const text::TextRange &range : matches) {
        if (range.start < bodyStart)
            ++day.frontmatter;
        else if (text::isInsideLink(text, range))
            ++day.links;
    }
}

void StreamFind::recount()
{
    m_timer.stop();
    m_dirty = false;
    m_days.clear();
    m_limited = false;
    if (!m_find || !m_find->active() || !m_model) {
        m_query.clear();
        Q_EMIT changed();
        return;
    }
    m_query = m_find->appliedQuery();
    m_options = m_find->options();
    const int pathRole = m_model->roleNames().key("path", -1);
    const int rows = pathRole >= 0 ? m_model->rowCount() : 0;
    const int limit = m_dayLimit > 0 ? std::min(rows, m_dayLimit) : rows;
    m_limited = limit < rows;
    const QString active = activePath();
    qsizetype searched = 0;
    // Texts of days that left the stream go from the cache.
    QHash<QString, CachedText> kept;
    for (int r = 0; r < limit; ++r) {
        Day day;
        day.path = m_model->data(m_model->index(r, 0), pathRole).toString();
        if (day.path != active && !m_query.isEmpty() && !day.path.isEmpty()) {
            bool ok = false;
            const QString text = textOf(day.path, &ok);
            if (const auto it = m_cache.constFind(day.path); it != m_cache.cend())
                kept.insert(day.path, *it);
            searched += text.size();
            if (searched > MaxSearchedChars) {
                m_limited = true;
                break;
            }
            if (ok)
                countDay(day, text);
        }
        m_days.append(day);
    }
    if (!m_query.isEmpty())
        m_cache = std::move(kept);
    Q_EMIT changed();
}

int StreamFind::countIn(const QString &path) const
{
    if (m_find && path == activePath())
        return m_find->count();
    for (const Day &d : m_days) {
        if (d.path == path)
            return d.matches;
    }
    return 0;
}

int StreamFind::count() const
{
    int n = m_find ? m_find->count() : 0;
    for (const Day &d : m_days)
        n += d.matches;
    return n;
}

int StreamFind::current() const
{
    if (!m_find || m_find->current() < 0)
        return -1;
    const QString active = activePath();
    int before = 0;
    for (const Day &d : m_days) {
        if (d.path == active)
            return before + m_find->current();
        before += d.matches;
    }
    return m_find->current(); // (a day the stream does not list: counted on its own)
}

int StreamFind::dayCount() const
{
    int n = m_find && m_find->count() > 0 ? 1 : 0;
    for (const Day &d : m_days)
        n += d.matches > 0 ? 1 : 0;
    return n;
}

int StreamFind::linkMatches() const
{
    int n = m_find ? m_find->linkMatches() : 0;
    for (const Day &d : m_days)
        n += d.links;
    return n;
}

int StreamFind::frontmatterMatches() const
{
    int n = m_find ? m_find->frontmatterMatches() : 0;
    for (const Day &d : m_days)
        n += d.frontmatter;
    return n;
}

void StreamFind::next()
{
    if (!m_find)
        return;
    refresh();
    m_find->next(); // at the day's end: endReached() → moveToDay()
}

void StreamFind::previous()
{
    if (!m_find)
        return;
    refresh();
    m_find->previous();
}

void StreamFind::moveToDay(bool forward)
{
    if (!m_find || !m_find->active())
        return;
    if (m_dirty || m_timer.isActive())
        recount();
    const QString active = activePath();
    const int n = int(m_days.size());
    int activeRow = -1;
    for (int i = 0; i < n && activeRow < 0; ++i) {
        if (m_days.at(i).path == active)
            activeRow = i;
    }
    for (int step = 1; step <= n; ++step) {
        int i = 0;
        if (activeRow < 0)
            i = forward ? step - 1 : n - step;
        else
            i = ((forward ? activeRow + step : activeRow - step) % n + n) % n;
        if (i == activeRow) {
            // Round the stream back to this day: its first / last match, if it has one.
            if (m_find->count() > 0)
                m_find->enter(forward);
            return;
        }
        if (m_days.at(i).matches <= 0)
            continue;
        const QString path = m_days.at(i).path;
        // Once that day's controller is the bar's (the view switches the day written in): its first
        // match going down, its last going up. Queued: the bar's editor follows the day too.
        QPointer<NoteFind> find = m_find;
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(m_find, &NoteFind::controllerChanged, this, [this, find, path, forward, connection] {
            disconnect(*connection);
            QTimer::singleShot(0, this, [find, path, forward] {
                if (find && find->controller() && find->controller()->path() == path)
                    find->enter(forward);
            });
        });
        Q_EMIT dayRequested(i, path);
        // The view could not switch (no such day on screen): nothing waits for it.
        if (activePath() != path)
            disconnect(*connection);
        return;
    }
    // Only this day has matches, or none at all.
    if (activeRow < 0 && m_find->count() > 0)
        m_find->enter(forward);
}

bool StreamFind::replaceCurrent(const QString &replacement)
{
    if (!m_find)
        return false;
    refresh();
    if (m_find->count() == 0 && !m_find->stale()) {
        m_find->next(); // nothing in this day: show the next day's first match, replace nothing
        return false;
    }
    return m_find->replaceCurrent(replacement);
}

QVariantMap StreamFind::replaceAll(const QString &replacement)
{
    QVariantMap out{{u"count"_s, 0},
                    {u"days"_s, 0},
                    {u"token"_s, 0},
                    {u"changed"_s, QStringList()},
                    {u"failed"_s, QStringList()}};
    if (!m_find || !m_host)
        return out;
    // Reading mode (the stream's days share it): no day is changed, not even one only on disk.
    if (PageController *page = m_find->controller(); page && page->readOnly()) {
        Q_EMIT page->editRefused();
        return out;
    }
    refresh();
    const QString line = oneLine(replacement);
    QList<UndoEdit> edits;
    QList<UndoFile> files;
    QStringList changedPaths;
    QStringList failedPaths;
    int count = 0;
    int days = 0;
    // The day written in: one undo step in its editor (in source mode its TextEdit's).
    if (PageController *page = m_find->controller()) {
        const QString before = page->undoToken();
        if (const int n = m_find->replaceAll(line); n > 0) {
            count += n;
            ++days;
            if (!page->sourceMode() && page->undoToken() != before)
                edits.append(UndoEdit{page, page->path(), page->undoToken()});
        }
    }
    // Editors elsewhere (the side panel) save what they hold before files are written.
    Q_EMIT m_host->saveOpenEditorsRequested();
    const QString active = activePath();
    for (const Day &day : std::as_const(m_days)) {
        if (day.path == active || day.matches <= 0)
            continue;
        if (PageController *page = attached(day.path); page && page->loaded() && !page->sourceMode()) {
            const QList<text::TextRange> matches = text::findMatches(page->text(), m_query, m_options);
            if (matches.isEmpty() || !page->replaceTextRanges(matches, line))
                continue;
            count += int(matches.size());
            ++days;
            edits.append(UndoEdit{page, page->path(), page->undoToken()});
            continue;
        }
        bool ok = false;
        const QString before = m_host->read(day.path, &ok);
        if (!ok) {
            failedPaths.append(day.path);
            continue;
        }
        const QList<text::TextRange> matches = text::findMatches(before, m_query, m_options);
        const QString after = text::replaceMatches(before, matches, line);
        if (matches.isEmpty() || after == before)
            continue;
        switch (m_host->writeIfUnchanged(day.path, after, before)) {
        case EditorHost::WriteResult::Written:
            count += int(matches.size());
            ++days;
            files.append(UndoFile{day.path, before, after});
            m_cache.remove(day.path);
            break;
        case EditorHost::WriteResult::Changed: changedPaths.append(day.path); break;
        case EditorHost::WriteResult::Failed: failedPaths.append(day.path); break;
        }
    }
    if (count > 0) {
        m_undoEdits = edits;
        m_undoFiles = files;
        out[u"token"_s] = ++m_token;
    }
    out[u"count"_s] = count;
    out[u"days"_s] = days;
    out[u"changed"_s] = changedPaths;
    out[u"failed"_s] = failedPaths;
    recount();
    return out;
}

QVariantMap StreamFind::undo(int token)
{
    QVariantMap out{{u"restored"_s, 0}, {u"left"_s, QStringList()}};
    if (token <= 0 || token != m_token || !m_host)
        return out;
    int restored = 0;
    QStringList left;
    for (const UndoEdit &e : std::as_const(m_undoEdits)) {
        if (e.controller && e.controller->path() == e.path && e.controller->undoStep(e.token))
            ++restored;
        else
            left.append(e.path);
    }
    for (const UndoFile &f : std::as_const(m_undoFiles)) {
        if (m_host->writeIfUnchanged(f.path, f.before, f.after) == EditorHost::WriteResult::Written) {
            ++restored;
            m_cache.remove(f.path);
        } else {
            left.append(f.path);
        }
    }
    m_undoEdits.clear();
    m_undoFiles.clear();
    ++m_token; // undone once
    out[u"restored"_s] = restored;
    out[u"left"_s] = left;
    recount();
    return out;
}

} // namespace brulion::editor
