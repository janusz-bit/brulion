#include "queryresultmodel.h"

#include "blocklistmodel.h"
#include "blockrender_p.h"
#include "blocktext.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "queryhost.h"

#include "markdown/blockdocument.h"
#include "markdown/inlinerenderer.h"
#include "markdown/journal.h"
#include "util/paths.h"

#include <QDate>
#include <QUrl>

#include <cstdlib>

using namespace Qt::StringLiterals;

namespace brulion::editor {

namespace {

constexpr int DefaultRefreshDelay = 150;
constexpr int ShowMoreStep = 50;

bool isOpenStatus(const QString &status)
{
    return status == u"todo" || status == u"doing";
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
    return {{KindRole, "kind"},       {LabelRole, "label"},     {CountRole, "count"},       {PathRole, "path"},
            {TitleRole, "title"},     {FolderRole, "folder"},   {LineRole, "line"},         {TextRole, "text"},
            {HtmlRole, "html"},       {StatusRole, "status"},   {IsTaskRole, "isTask"},     {BlockIdRole, "blockId"},
            {DueRole, "due"},         {OverdueRole, "overdue"}, {ReminderRole, "reminder"}, {AlarmRole, "alarm"},
            {ShowPageRole, "showPage"}, {JournalRole, "journal"}, {RawRole, "raw"}};
}

QVariant QueryResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    Row &r = const_cast<Row &>(m_rows[index.row()]); // html is rendered lazily
    const QVariantMap &item = r.item;
    const QVariantMap task = item.value(u"task"_s).toMap();
    const QVariantMap reminder = item.value(u"reminder"_s).toMap();
    switch (role) {
    case KindRole: return r.group ? u"group"_s : u"item"_s;
    case LabelRole: return r.group ? item.value(u"label"_s) : QVariant(QString());
    case CountRole: return r.count;
    case PathRole: return item.value(u"path"_s).toString();
    case TitleRole: return item.value(u"title"_s).toString();
    case FolderRole: return item.value(u"folder"_s).toString();
    case LineRole: return item.value(u"line"_s, -1).toInt();
    case Qt::DisplayRole:
    case TextRole: return item.value(u"text"_s).toString().simplified();
    case HtmlRole:
        if (r.group)
            return QString();
        if (!r.htmlValid) {
            r.html = renderItem(item);
            r.htmlValid = true;
        }
        return r.html;
    case StatusRole: return r.status;
    case IsTaskRole: return !r.group && !r.status.isEmpty();
    case BlockIdRole: return item.value(u"blockId"_s).toString();
    case DueRole: return task.value(u"due"_s).toString();
    case OverdueRole: {
        const QString due = task.value(u"due"_s).toString();
        if (due.isEmpty() || !isOpenStatus(r.status) || !m_controller)
            return false;
        const QDate date = QDate::fromString(due.left(10), u"yyyy-MM-dd"_s);
        return date.isValid() && date < m_controller->currentDateTime().date();
    }
    case ReminderRole: return reminder.value(u"at"_s).toString();
    case AlarmRole: return reminder.value(u"alarm"_s).toBool();
    case ShowPageRole:
        // Items of this page need no page name; grouped by page, the header names it.
        return !r.group && !m_groupedByPage && !(m_controller && item.value(u"path"_s).toString() == m_controller->path());
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
    for (auto it = names.constBegin(); it != names.constEnd(); ++it)
        out.insert(QString::fromLatin1(it.value()), data(idx, it.key()));
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
        // Midnight: `today` / `overdue` filters, "dziś"/"jutro" chips and overdue marks move on.
        connect(m_controller, &PageController::todayChanged, this, &QueryResultModel::schedule);
        connect(m_controller, &PageController::todayChanged, this, &QueryResultModel::invalidateHtml);
        connect(m_controller, &PageController::paletteChanged, this, &QueryResultModel::invalidateHtml);
        connect(m_controller, &PageController::localeChanged, this, &QueryResultModel::invalidateHtml);
        connect(m_controller, &QObject::destroyed, this, [this] { setController(nullptr); });
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
    const auto it = m_holders.find(holder);
    if (it == m_holders.end())
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
    if (queries && m_controller && !m_source.trimmed().isEmpty()) {
        const QVariantMap r = queries->runQuery(m_source, m_controller->path());
        ok = r.value(u"ok"_s).toBool();
        error = r.value(u"error"_s).toString();
        type = r.value(u"type"_s).toString();
        total = r.value(u"total"_s).toInt();
        complete = r.value(u"complete"_s, true).toBool();
        items = r.value(u"items"_s).toList();
        groups = r.value(u"groups"_s).toList();
    }
    const bool same = m_ready && ok == m_ok && error == m_error && type == m_type && total == m_total
                      && complete == m_complete && items == m_items && groups == m_groups;
    m_ready = queries != nullptr;
    if (same) {
        // Statuses shown ahead of the index are confirmed (or corrected) by the new run.
        for (int k = 0; k < m_rows.size(); ++k) {
            Row &row = m_rows[k];
            const QString status = row.item.value(u"task"_s).toMap().value(u"status"_s).toString();
            if (!row.group && row.status != status) {
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
    rebuildRows();
    Q_EMIT resultChanged();
}

void QueryResultModel::rebuildRows()
{
    beginResetModel();
    m_rows.clear();
    // Grouped by page: the group header names the page, the items need not repeat it.
    m_groupedByPage = !m_groups.isEmpty();
    for (const QVariant &g : std::as_const(m_groups)) {
        const QVariantMap group = g.toMap();
        for (const QVariant &i : group.value(u"items"_s).toList()) {
            if (i.toMap().value(u"path"_s).toString() != group.value(u"key"_s).toString()) {
                m_groupedByPage = false;
                break;
            }
        }
    }
    const auto addItem = [this](const QVariantMap &item) {
        Row row;
        row.item = item;
        row.status = item.value(u"task"_s).toMap().value(u"status"_s).toString();
        m_rows.append(row);
    };
    int shown = 0;
    if (!m_groups.isEmpty()) {
        for (const QVariant &g : std::as_const(m_groups)) {
            if (shown >= m_maxItems)
                break;
            const QVariantMap group = g.toMap();
            const QVariantList groupItems = group.value(u"items"_s).toList();
            Row header;
            header.group = true;
            header.item = {{u"key"_s, group.value(u"key"_s)}, {u"label"_s, group.value(u"label"_s)}};
            if (m_groupedByPage && !groupItems.isEmpty()) {
                const QVariantMap first = groupItems.first().toMap();
                header.item.insert(u"path"_s, first.value(u"path"_s));
                header.item.insert(u"title"_s, first.value(u"title"_s));
                header.item.insert(u"folder"_s, first.value(u"folder"_s));
            }
            header.count = int(groupItems.size());
            m_rows.append(header);
            for (const QVariant &i : groupItems) {
                if (shown >= m_maxItems)
                    break;
                addItem(i.toMap());
                ++shown;
            }
        }
    } else {
        for (const QVariant &i : std::as_const(m_items)) {
            if (shown >= m_maxItems)
                break;
            addItem(i.toMap());
            ++shown;
        }
    }
    endResetModel();
}

void QueryResultModel::invalidateHtml()
{
    for (Row &r : m_rows)
        r.htmlValid = false;
    if (!m_rows.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1), {HtmlRole, OverdueRole});
}

QString QueryResultModel::renderItem(const QVariantMap &item) const
{
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
    const QString html = detail::renderBodyHtml(e, shape, renderer);
    return html.isEmpty() ? markdown::InlineRenderer::escape(plainTextOf(e, 300)) : html;
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
    const QVariantMap result = queries->runQuery(m_source, m_controller->path());
    QVariantList items = result.value(u"items"_s).toList();
    for (const QVariant &g : result.value(u"groups"_s).toList())
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
    if (row < 0 || row >= m_rows.size() || !m_controller)
        return false;
    Row &r = m_rows[row];
    if (r.group || r.status.isEmpty())
        return false;
    const QString next = PageController::toggledStatus(r.status);
    if (!writeStatus(r, next))
        return false;
    Q_EMIT dataChanged(index(row), index(row), {LineRole, StatusRole, IsTaskRole, OverdueRole});
    m_timer.start();
    return true;
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
    const Row &r = m_rows[row];
    const QString path = r.item.value(u"path"_s).toString();
    if (path.isEmpty())
        return {};
    return m_controller->openLocation(path, r.group || m_type == u"pages" ? -1 : r.item.value(u"line"_s, -1).toInt(),
                                      newPane);
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
        const QString path = m_rows[row].item.value(u"path"_s).toString();
        if (!path.isEmpty() && path != m_controller->path()) {
            const QString page = QString::fromLatin1(QUrl::toPercentEncoding(paths::stripMarkdownExtension(path)));
            return m_controller->activateLink(-1, u"page:"_s + page + url.mid(5), newPane);
        }
    }
    return m_controller->activateLink(-1, url, newPane);
}

} // namespace brulion::editor
