#include "queryresultmodel.h"

#include "blocklistmodel.h"
#include "blockrender_p.h"
#include "blocktext.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "queryhost.h"

#include "markdown/blockclipboard.h"
#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"
#include "markdown/journal.h"
#include "util/paths.h"

#include <QDate>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <ranges>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

constexpr int DefaultRefreshDelay = 150;
constexpr int ShowMoreStep = 50;

bool isOpenStatus(const QString &status)
{
    return status == u"todo" || status == u"doing";
}

/// `key` of a host item's `task` or `reminder` map (queryhost.h).
QVariant itemField(const QVariantMap &item, const QString &map, const QString &key)
{
    return item.value(map).toMap().value(key);
}

QString itemTaskStatus(const QVariantMap &item)
{
    return itemField(item, u"task"_s, u"status"_s).toString();
}

} // namespace

QueryResultModel::QueryResultModel(QObject *parent) : QAbstractListModel(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(DefaultRefreshDelay);
    connect(&m_timer, &QTimer::timeout, this, &QueryResultModel::timerFired);
}

QueryResultModel::~QueryResultModel() = default;

void QueryResultModel::classBegin()
{
    m_completing = true;
}

void QueryResultModel::componentComplete()
{
    m_completing = false;
    if (!m_ready)
        refresh();
}

int QueryResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> QueryResultModel::roleNames() const
{
    static const QHash<int, QByteArray> names{
            {KindRole, "kind"},         {LabelRole, "label"},     {CountRole, "count"},       {PathRole, "path"},
            {TitleRole, "title"},       {FolderRole, "folder"},   {LineRole, "line"},         {TextRole, "text"},
            {HtmlRole, "html"},         {StatusRole, "status"},   {IsTaskRole, "isTask"},     {BlockIdRole, "blockId"},
            {DueRole, "due"},           {OverdueRole, "overdue"}, {ReminderRole, "reminder"}, {AlarmRole, "alarm"},
            {ShowPageRole, "showPage"}, {JournalRole, "journal"}, {RawRole, "raw"},           {CellsRole, "cells"}};
    return names;
}

QVariant QueryResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const QVariantMap &item = r.item;
    switch (role) {
    case KindRole: return r.group ? u"group"_s : u"item"_s;
    case LabelRole: return r.group ? item.value(u"label"_s) : QVariant(QString());
    case CountRole: return r.count;
    case PathRole: return item.value(u"path"_s).toString();
    case TitleRole: return item.value(u"title"_s).toString();
    case FolderRole: return item.value(u"folder"_s).toString();
    case LineRole: return item.value(u"line"_s, -1).toInt();
    case Qt::DisplayRole:
    case TextRole:
        // Dataview rows: their cells as plain text (screen readers, tests).
        if (item.contains(u"cells"_s)) {
            QStringList plain;
            const QStringList cells = item.value(u"cells"_s).toStringList();
            for (const QString &cell : cells)
                plain.append(markdown::stripInlineMarkup(cell).simplified());
            return plain.join(u" · "_s);
        }
        return item.value(u"text"_s).toString().simplified();
    case HtmlRole:
    case CellsRole:
        if (r.group)
            return role == CellsRole ? QVariant(QStringList()) : QVariant(QString());
        if (!r.htmlValid) {
            r.html = renderItem(item);
            r.cells.clear();
            if (m_type == u"table") {
                const QStringList cells = item.value(u"cells"_s).toStringList();
                for (const QString &cell : cells)
                    r.cells.append(renderMarkdown(cell));
            }
            r.htmlValid = true;
        }
        return role == CellsRole ? QVariant(r.cells) : QVariant(r.html);
    case StatusRole: return r.status;
    case IsTaskRole: return !r.group && !r.status.isEmpty();
    case BlockIdRole: return item.value(u"blockId"_s).toString();
    case DueRole: return itemField(item, u"task"_s, u"due"_s).toString();
    case OverdueRole: {
        const QString due = itemField(item, u"task"_s, u"due"_s).toString();
        if (due.isEmpty() || !isOpenStatus(r.status) || !m_controller)
            return false;
        const QDate date = QDate::fromString(due.left(10), u"yyyy-MM-dd"_s);
        return date.isValid() && date < m_controller->currentDateTime().date();
    }
    case ReminderRole: return itemField(item, u"reminder"_s, u"at"_s).toString();
    case AlarmRole: return itemField(item, u"reminder"_s, u"alarm"_s).toBool();
    case ShowPageRole:
        // Items of this page need no page name; grouped by page, the header names it; Dataview's
        // LIST and TABLE rows name their page themselves.
        return !r.group && !m_groupedByPage && !item.contains(u"cells"_s)
               && !(m_controller && item.value(u"path"_s).toString() == m_controller->path());
    case JournalRole: {
        // "YYYY-MM-DD" when the item's page is a daily note (shown as a date), else "".
        const auto date = markdown::journalDateFromPath(item.value(u"path"_s).toString(), m_config);
        return date ? date->toString(u"yyyy-MM-dd"_s) : QString();
    }
    case RawRole: return r.group ? QString() : item.value(u"raw"_s).toString();
    default: return {};
    }
}

QVariantMap QueryResultModel::get(int row) const
{
    QVariantMap out;
    if (row < 0 || row >= m_rows.size())
        return out;
    const QModelIndex idx = index(row);
    const QHash<int, QByteArray> names = roleNames();
    for (const auto &[role, name] : names.asKeyValueRange())
        out.insert(QString::fromLatin1(name), data(idx, role));
    return out;
}

void QueryResultModel::setController(PageController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &PageController::hostChanged, this, &QueryResultModel::connectHost);
        connect(m_controller, &PageController::pathChanged, this, &QueryResultModel::schedule);
        connect(m_controller, &PageController::nowChanged, this, &QueryResultModel::schedule);
        // Midnight: `today` / `overdue` filters, "today"/"tomorrow" chips and overdue marks move on.
        connect(m_controller, &PageController::todayChanged, this, &QueryResultModel::schedule);
        connect(m_controller, &PageController::todayChanged, this, &QueryResultModel::invalidateHtml);
        connect(m_controller, &PageController::paletteChanged, this, &QueryResultModel::invalidateHtml);
        connect(m_controller, &PageController::localeChanged, this, &QueryResultModel::invalidateHtml);
        // Not setController(nullptr): the QPointer is null already when destroyed() comes.
        connect(m_controller, &QObject::destroyed, this, [this] {
            connectHost();
            Q_EMIT controllerChanged();
        });
    }
    connectHost();
    Q_EMIT controllerChanged();
}

void QueryResultModel::connectHost()
{
    EditorHost *host = m_controller ? m_controller->host() : nullptr;
    if (host != m_host) {
        if (m_host)
            disconnect(m_host, nullptr, this, nullptr);
        m_host = host;
        if (m_host)
            connect(m_host, &EditorHost::indexChanged, this, &QueryResultModel::schedule);
        m_ready = false;
        Q_EMIT availableChanged();
    }
    schedule();
}

void QueryResultModel::setLanguage(const QString &language)
{
    const QString l = language == u"dataview" ? language : u"query"_s;
    if (m_language == l)
        return;
    m_language = l;
    m_ready = false;
    Q_EMIT languageChanged();
    schedule();
}

QVariantMap QueryResultModel::runHost(QueryHost *queries) const
{
    const QString path = m_controller ? m_controller->path() : QString();
    return dataview() ? queries->runDataview(m_source, path) : queries->runQuery(m_source, path);
}

void QueryResultModel::setSource(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();
    schedule();
}

bool QueryResultModel::available() const
{
    return qobject_cast<QueryHost *>(m_host.data()) != nullptr;
}

int QueryResultModel::hiddenCount() const
{
    return std::max(0, int(m_items.size()) - m_maxItems);
}

void QueryResultModel::setMaxItems(int max)
{
    max = std::max(1, max);
    if (m_maxItems == max)
        return;
    m_maxItems = max;
    Q_EMIT maxItemsChanged();
    rebuildRows();
    Q_EMIT resultChanged();
}

void QueryResultModel::showMore()
{
    setMaxItems(m_maxItems + ShowMoreStep);
}

void QueryResultModel::setRefreshDelay(int ms)
{
    ms = std::max(0, ms);
    if (m_timer.interval() == ms)
        return;
    m_timer.setInterval(ms);
    Q_EMIT refreshDelayChanged();
}

void QueryResultModel::hold(QObject *holder)
{
    if (!holder || m_holders.contains(holder))
        return;
    const bool wasHeld = held();
    m_holders.insert(holder, connect(holder, &QObject::destroyed, this, [this, holder] { release(holder); }));
    if (!wasHeld)
        Q_EMIT heldChanged();
}

void QueryResultModel::release(QObject *holder)
{
    const auto it = m_holders.constFind(holder);
    if (it == m_holders.cend())
        return;
    disconnect(it.value());
    m_holders.erase(it);
    if (held())
        return;
    Q_EMIT heldChanged();
    // Not right here: the menu letting go may be in the middle of closing, on a row that the
    // run would destroy.
    if (m_pending)
        schedule();
}

void QueryResultModel::timerFired()
{
    if (held())
        m_pending = true;
    else
        refresh();
}

void QueryResultModel::schedule()
{
    // The first run comes right away (the block appears with its results: from QML when the
    // model is completed), later ones debounced (an index change per save while typing).
    // Held rows (a menu open on one) wait: the run comes when they are let go.
    if (m_completing)
        return;
    if (held()) {
        m_pending = true;
        return;
    }
    if (!m_ready) {
        QTimer::singleShot(0, this, [this] {
            if (!m_ready)
                refresh();
        });
        return;
    }
    m_timer.start();
}

void QueryResultModel::refresh()
{
    m_timer.stop();
    m_pending = false;
    auto *queries = qobject_cast<QueryHost *>(m_host.data());
    bool ok = true;
    QString error;
    QString type;
    int total = 0;
    bool complete = true;
    QVariantList items;
    QVariantList groups;
    if (m_host)
        m_config = vaultConfigFromMap(m_host->vaultConfig());
    QStringList columns;
    if (queries && m_controller && !m_source.trimmed().isEmpty()) {
        const QVariantMap r = runHost(queries);
        columns = r.value(u"columns"_s).toStringList();
        ok = r.value(u"ok"_s).toBool();
        error = r.value(u"error"_s).toString();
        type = r.value(u"type"_s).toString();
        total = r.value(u"total"_s).toInt();
        complete = r.value(u"complete"_s, true).toBool();
        items = r.value(u"items"_s).toList();
        groups = r.value(u"groups"_s).toList();
    }
    const bool same = m_ready && ok == m_ok && error == m_error && type == m_type && total == m_total
                      && complete == m_complete && items == m_items && groups == m_groups && columns == m_columns;
    m_ready = queries != nullptr;
    if (same) {
        // Statuses shown ahead of the index are confirmed (or corrected) by the new run.
        for (int k = 0; k < m_rows.size(); ++k) {
            Row &row = m_rows[k];
            if (const QString status = itemTaskStatus(row.item); !row.group && row.status != status) {
                row.status = status;
                Q_EMIT dataChanged(index(k), index(k), {StatusRole, IsTaskRole, OverdueRole});
            }
        }
        Q_EMIT resultChanged();
        return;
    }
    m_ok = ok;
    m_error = error;
    m_type = type;
    m_total = total;
    m_complete = complete;
    m_items = items;
    m_groups = groups;
    m_columns = columns;
    rebuildRows();
    Q_EMIT resultChanged();
}

void QueryResultModel::rebuildRows()
{
    beginResetModel();
    m_rows.clear();
    // Grouped by page: the group header names the page, the items need not repeat it.
    m_groupedByPage = !m_groups.isEmpty() && std::ranges::all_of(std::as_const(m_groups), [](const QVariant &g) {
        const QVariantMap group = g.toMap();
        const QString key = group.value(u"key"_s).toString();
        const QVariantList items = group.value(u"items"_s).toList();
        return std::ranges::all_of(items,
                                   [&key](const QVariant &i) { return i.toMap().value(u"path"_s).toString() == key; });
    });
    // A row for each item, maxItems in all (a group's header comes before its items).
    int shown = 0;
    const auto addItems = [this, &shown](const QVariantList &items) {
        for (const QVariant &i : items | std::views::take(m_maxItems - shown)) {
            Row &row = m_rows.emplaceBack();
            row.item = i.toMap();
            row.status = itemTaskStatus(row.item);
            ++shown;
        }
    };
    if (m_groups.isEmpty())
        addItems(m_items);
    for (const QVariant &g : std::as_const(m_groups)) {
        if (shown >= m_maxItems)
            break;
        const QVariantMap group = g.toMap();
        const QVariantList groupItems = group.value(u"items"_s).toList();
        Row &header = m_rows.emplaceBack();
        header.group = true;
        header.item = {{u"key"_s, group.value(u"key"_s)}, {u"label"_s, group.value(u"label"_s)}};
        if (m_groupedByPage && !groupItems.isEmpty()) {
            const QVariantMap first = groupItems.constFirst().toMap();
            for (const QString &key : {u"path"_s, u"title"_s, u"folder"_s})
                header.item.insert(key, first.value(key));
        }
        header.count = int(groupItems.size());
        addItems(groupItems);
    }
    endResetModel();
}

void QueryResultModel::invalidateHtml()
{
    for (Row &r : m_rows)
        r.htmlValid = false;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {HtmlRole, CellsRole, OverdueRole});
}

QString QueryResultModel::renderMarkdown(const QString &markdown) const
{
    if (!m_controller)
        return markdown::InlineRenderer::escape(markdown);
    markdown::RenderContext ctx = m_controller->blocks()->renderContextFor(m_controller->path(), markdown, 0);
    ctx.inlineCalc = nullptr;
    ctx.hideTransclusions = false;
    ctx.dateLinks = false;
    return markdown::InlineRenderer(ctx).render(markdown);
}

QString QueryResultModel::renderItem(const QVariantMap &item) const
{
    // Dataview's LIST rows: one Markdown text ("[[Page]]: value"); TABLE rows draw their cells.
    if (item.contains(u"cells"_s))
        return m_type == u"table" ? QString() : renderMarkdown(item.value(u"cells"_s).toStringList().value(0));
    const QString title = item.value(u"title"_s).toString();
    const QString raw = item.value(u"raw"_s).toString();
    if (m_type == u"pages" || raw.trimmed().isEmpty())
        return markdown::InlineRenderer::escape(m_type == u"pages" ? title : item.value(u"text"_s).toString().simplified());
    const markdown::BlockDocument doc(raw);
    if (doc.isEmpty())
        return markdown::InlineRenderer::escape(item.value(u"text"_s).toString().simplified());
    const markdown::BlockEntry &e = doc.at(0);
    const BlockShape shape = shapeOf(e);
    if (!m_controller)
        return markdown::InlineRenderer::escape(plainTextOf(e, 300));
    markdown::RenderContext ctx =
            m_controller->blocks()->renderContextFor(item.value(u"path"_s).toString(), raw, 0);
    ctx.inlineCalc = nullptr; // the item's page variables are not at hand: `= expr` shown as written
    ctx.hideTransclusions = false; // no embed frames in result rows: placeholder links
    const markdown::InlineRenderer renderer(ctx);
    QString html = detail::renderBodyHtml(e, shape, renderer);
    if (html.isEmpty())
        return markdown::InlineRenderer::escape(plainTextOf(e, 300));
    // A task's subtask progress and clocked time after its text (notebook::queryItemJson's
    // `subtasks`, `task.clockedSeconds`, `task.clockStart`; docs/SYNTAX.md §5).
    const QVariantMap task = item.value(u"task"_s).toMap();
    if (!task.isEmpty()) {
        const QVariantMap sub = item.value(u"subtasks"_s).toMap();
        const QDateTime start = QDateTime::fromString(task.value(u"clockStart"_s).toString(), u"yyyy-MM-ddTHH:mm:ss"_s);
        const QString chips =
                markdown::taskChipsHtml(sub.value(u"done"_s).toInt(), sub.value(u"total"_s).toInt(),
                                        task.value(u"clockedSeconds"_s).toLongLong(), start, ctx.palette, ctx.locale);
        if (!chips.isEmpty())
            html += u' ' + chips;
    }
    return html;
}

int QueryResultModel::currentLine(const Row &row) const
{
    const int line = row.item.value(u"line"_s, -1).toInt();
    if (!m_pending)
        return line;
    // The rows waited for a menu while the index changed: the task may be on another line now
    // (an edit above it, a sync), and its old line may hold another task. Ask the index where
    // the same block of the same file is now: the same text — without the status marker and the
    // date tokens, which the menu itself or another device may have changed meanwhile — the
    // nearest one when the text repeats (the unchanged source line when two are as near).
    auto *queries = qobject_cast<QueryHost *>(m_host.data());
    if (!queries || !m_controller)
        return -1;
    const QString path = row.item.value(u"path"_s).toString();
    const QString text = row.item.value(u"text"_s).toString();
    const QString raw = row.item.value(u"raw"_s).toString();
    const QVariantMap result = runHost(queries);
    QVariantList items = result.value(u"items"_s).toList();
    const QVariantList groups = result.value(u"groups"_s).toList();
    for (const QVariant &g : groups)
        items.append(g.toMap().value(u"items"_s).toList());
    int best = -1;
    bool bestSameRaw = false;
    for (const QVariant &i : std::as_const(items)) {
        const QVariantMap item = i.toMap();
        const int at = item.value(u"line"_s, -1).toInt();
        if (at < 0 || item.value(u"path"_s).toString() != path || item.value(u"text"_s).toString() != text)
            continue;
        const bool sameRaw = item.value(u"raw"_s).toString() == raw;
        const int distance = std::abs(at - line);
        const int bestDistance = std::abs(best - line);
        if (best < 0 || distance < bestDistance || (distance == bestDistance && sameRaw && !bestSameRaw)) {
            best = at;
            bestSameRaw = sameRaw;
        }
    }
    return best;
}

bool QueryResultModel::writeStatus(Row &row, const QString &status)
{
    const int line = currentLine(row);
    if (!m_controller->setTaskStatusAt(row.item.value(u"path"_s).toString(), line, status))
        return false;
    // Where the task is now: the next lookup (the rows still waiting) starts there.
    row.item.insert(u"line"_s, line);
    row.status = status;
    return true;
}

bool QueryResultModel::toggleTask(int row)
{
    // (A task's toggled status is never the one it has: setTaskStatus() writes it.)
    return row >= 0 && row < m_rows.size() && setTaskStatus(row, PageController::toggledStatus(m_rows.at(row).status));
}

bool QueryResultModel::setTaskStatus(int row, const QString &status)
{
    if (row < 0 || row >= m_rows.size() || !m_controller)
        return false;
    Row &r = m_rows[row];
    if (r.group || r.status.isEmpty() || r.status == status)
        return false;
    if (!writeStatus(r, status))
        return false;
    Q_EMIT dataChanged(index(row), index(row), {LineRole, StatusRole, IsTaskRole, OverdueRole});
    m_timer.start();
    return true;
}

QVariantMap QueryResultModel::open(int row, bool newPane)
{
    if (row < 0 || row >= m_rows.size() || !m_controller)
        return {};
    const Row &r = m_rows.at(row);
    const QString path = r.item.value(u"path"_s).toString();
    if (path.isEmpty())
        return {};
    const bool page = r.group || m_type == u"pages" || r.item.contains(u"cells"_s);
    return m_controller->openLocation(path, page ? -1 : r.item.value(u"line"_s, -1).toInt(), newPane);
}

QVariantMap QueryResultModel::activateLink(int row, const QString &url, bool newPane)
{
    if (!m_controller)
        return {};
    if (url.startsWith(u"task:"_s) || url.startsWith(u"reminder:"_s) || url.startsWith(u"date:"_s)
        || url.startsWith(u"recur:"_s) || url.startsWith(u"prio:"_s))
        return open(row, newPane);
    // `[[#Heading]]`, `[[#^id]]` in an item of another page name that page, not the query's.
    if (url.startsWith(u"page:#"_s) && row >= 0 && row < m_rows.size()) {
        const QString path = m_rows.at(row).item.value(u"path"_s).toString();
        if (!path.isEmpty() && path != m_controller->path()) {
            const QString page = QString::fromLatin1(QUrl::toPercentEncoding(paths::stripMarkdownExtension(path)));
            return m_controller->activateLink(-1, u"page:"_s + page + url.mid(5), newPane);
        }
    }
    return m_controller->activateLink(-1, url, newPane);
}

} // namespace brulion::editor
