// ViewResultModel (viewresultmodel.h): a database view for the editor's DatabaseView.
#include "viewresultmodel.h"

#include "blocklistmodel.h"
#include "blocktext.h"
#include "editorhost.h"
#include "pagecontroller.h"
#include "queryhost.h"

#include "index/query.h"
#include "markdown/inlinerenderer.h"
#include "notebook/views.h"
#include "util/paths.h"
#include "views/bases.h"
#include "views/edits.h"
#include "views/view.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QLocale>

using namespace Qt::StringLiterals;

namespace brulion::editor {

// ---- ViewListModel ---------------------------------------------------------------------------------

ViewListModel::ViewListModel(QList<QByteArray> roles, QObject *parent)
    : QAbstractListModel(parent), m_roles(std::move(roles))
{ }

int ViewListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant ViewListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const int at = role - Qt::UserRole - 1;
    if (at < 0 || at >= m_roles.size())
        return {};
    return m_items.at(index.row()).value(QString::fromLatin1(m_roles.at(at)));
}

QHash<int, QByteArray> ViewListModel::roleNames() const
{
    QHash<int, QByteArray> out;
    for (qsizetype i = 0; i < m_roles.size(); ++i)
        out.insert(Qt::UserRole + 1 + int(i), m_roles.at(i));
    return out;
}

void ViewListModel::setItems(const QList<QVariantMap> &items)
{
    if (items == m_items)
        return;
    if (items.size() == m_items.size()) {
        m_items = items;
        if (!m_items.isEmpty())
            Q_EMIT dataChanged(index(0), index(int(m_items.size()) - 1));
        return;
    }
    beginResetModel();
    m_items = items;
    endResetModel();
}

// ---- ViewResultModel -----------------------------------------------------------------------------

struct ViewResultModel::Row {
    QStringList cells; ///< rendered
    QStringList texts;
    QString headline;
    QStringList details;
    QString status;
    bool isTask = false;
    QString cover;
    QString coverColor;
};

namespace {

QString statusOf(const QVariantMap &row)
{
    return row.value(u"task"_s).toMap().value(u"status"_s).toString();
}

} // namespace

ViewResultModel::ViewResultModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_view(std::make_unique<views::ViewResult>())
    , m_columns(new ViewListModel(
              {"label", "kind", "type", "editable", "sortKey", "sorted", "summaryLabel", "summaryText", "number"},
              this))
    , m_groups(new ViewListModel({"label", "value", "count", "droppable", "summary", "group"}, this))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(150);
    connect(&m_timer, &QTimer::timeout, this, &ViewResultModel::timerFired);
}

ViewResultModel::~ViewResultModel() = default;

void ViewResultModel::classBegin()
{
    m_completing = true;
}

void ViewResultModel::componentComplete()
{
    m_completing = false;
    // The first run right away: the block is created with its rows (QueryResultModel does the same).
    if (m_controller && !m_source.isEmpty())
        refresh();
    else
        schedule();
}

int ViewResultModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> ViewResultModel::roleNames() const
{
    return {{PathRole, "path"},      {TitleRole, "title"},   {CellsRole, "cells"}, {TextsRole, "texts"},
            {StatusRole, "status"},  {IsTaskRole, "isTask"}, {CoverRole, "cover"}, {CoverColorRole, "coverColor"},
            {GroupRole, "group"},    {DateRole, "date"},     {LineRole, "line"},   {HeadlineRole, "headline"},
            {DetailsRole, "details"}};
}

QVariant ViewResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size() || index.row() >= m_view->rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    const views::Row &v = m_view->rows.at(index.row());
    switch (role) {
    case PathRole: return v.path;
    case TitleRole: return v.title;
    case CellsRole: return r.cells;
    case TextsRole: return r.texts;
    case StatusRole: return r.status;
    case IsTaskRole: return r.isTask;
    case CoverRole: return r.cover;
    case CoverColorRole: return r.coverColor;
    case GroupRole: return v.group;
    case DateRole: return v.date.isValid() ? v.date.toString(Qt::ISODate) : QString();
    case LineRole: return v.line;
    case HeadlineRole: return r.headline;
    case DetailsRole: return r.details;
    default: return {};
    }
}

void ViewResultModel::setController(PageController *controller)
{
    if (m_controller == controller)
        return;
    if (m_controller)
        disconnect(m_controller, nullptr, this, nullptr);
    m_controller = controller;
    if (m_controller) {
        connect(m_controller, &PageController::hostChanged, this, &ViewResultModel::connectHost);
        connect(m_controller, &PageController::paletteChanged, this, &ViewResultModel::rebuild);
        connect(m_controller, &PageController::todayChanged, this, &ViewResultModel::schedule);
        connect(m_controller, &PageController::localeChanged, this, &ViewResultModel::rebuild);
        connect(m_controller, &PageController::readOnlyChanged, this, &ViewResultModel::rebuild);
        connect(m_controller, &QObject::destroyed, this, [this] {
            connectHost();
            Q_EMIT controllerChanged();
        });
    }
    connectHost();
    Q_EMIT controllerChanged();
}

void ViewResultModel::connectHost()
{
    EditorHost *host = m_controller ? m_controller->host() : nullptr;
    if (host != m_host) {
        if (m_host)
            disconnect(m_host, nullptr, this, nullptr);
        m_host = host;
        if (m_host) {
            connect(m_host, &EditorHost::indexChanged, this, &ViewResultModel::schedule);
            connect(m_host, &EditorHost::webPictureChanged, this, &ViewResultModel::rebuild);
        }
        m_ready = false;
        Q_EMIT availableChanged();
    }
    schedule();
}

void ViewResultModel::setSource(const QString &source)
{
    if (m_source == source)
        return;
    m_source = source;
    Q_EMIT sourceChanged();
    schedule();
}

void ViewResultModel::setLanguage(const QString &language)
{
    const QString l = language == u"base" ? language : u"query"_s;
    if (m_language == l)
        return;
    m_language = l;
    m_ready = false;
    Q_EMIT languageChanged();
    schedule();
}

void ViewResultModel::setBasePath(const QString &path)
{
    if (m_basePath == path)
        return;
    m_basePath = path;
    Q_EMIT basePathChanged();
    schedule();
}

void ViewResultModel::setThisPath(const QString &path)
{
    if (m_thisPath == path)
        return;
    m_thisPath = path;
    Q_EMIT thisPathChanged();
    schedule();
}

void ViewResultModel::setBlockRow(int row)
{
    if (m_blockRow == row)
        return;
    m_blockRow = row;
    Q_EMIT blockRowChanged();
}

void ViewResultModel::setViewIndex(int view)
{
    view = std::max(0, view);
    if (m_viewIndex == view)
        return;
    m_viewIndex = view;
    m_viewName.clear();
    Q_EMIT viewIndexChanged();
    refresh();
}

void ViewResultModel::setViewName(const QString &name)
{
    if (m_viewName == name)
        return;
    m_viewName = name;
    Q_EMIT viewNameChanged();
    schedule();
}

void ViewResultModel::setRefreshDelay(int ms)
{
    ms = std::max(0, ms);
    if (m_timer.interval() == ms)
        return;
    m_timer.setInterval(ms);
    Q_EMIT refreshDelayChanged();
}

bool ViewResultModel::available() const
{
    return qobject_cast<QueryHost *>(m_host.data()) != nullptr;
}

QString ViewResultModel::kind() const
{
    return views::viewKindName(m_view->kind);
}

QString ViewResultModel::type() const
{
    return m_result.value(u"type"_s).toString();
}

int ViewResultModel::total() const
{
    return m_view->total;
}

bool ViewResultModel::limited() const
{
    return m_view->limited;
}

QString ViewResultModel::notice() const
{
    return m_view->notice;
}

QStringList ViewResultModel::problems() const
{
    return m_result.value(u"problems"_s).toStringList();
}

QStringList ViewResultModel::viewNames() const
{
    QStringList out;
    for (const QVariant &v : m_result.value(u"views"_s).toList())
        out.append(v.toMap().value(u"name"_s).toString());
    return out;
}

QVariantList ViewResultModel::viewEntries() const
{
    QVariantList out;
    const QVariantList views = m_result.value(u"views"_s).toList();
    for (qsizetype i = 0; i < views.size(); ++i) {
        const QVariantMap v = views.at(i).toMap();
        out.append(QVariantMap{{u"name"_s, v.value(u"name"_s)}, {u"view"_s, int(i)}, {u"kind"_s, v.value(u"kind"_s)}});
    }
    return out;
}

QStringList ViewResultModel::viewKinds() const
{
    QStringList out;
    for (const QVariant &v : m_result.value(u"views"_s).toList())
        out.append(v.toMap().value(u"kind"_s).toString());
    return out;
}

int ViewResultModel::columnTotal() const
{
    return int(m_view->columns.size());
}

int ViewResultModel::groupCount() const
{
    return int(m_view->groups.size());
}

bool ViewResultModel::movable() const
{
    return !m_view->groupKind.isEmpty() && m_controller && !m_controller->readOnly();
}

bool ViewResultModel::canAddRow() const
{
    return m_view->newRow.possible && m_controller && !m_controller->readOnly();
}

bool ViewResultModel::canChangeView() const
{
    if (!m_controller || m_controller->readOnly())
        return false;
    return !m_basePath.isEmpty() || m_blockRow >= 0;
}

QString ViewResultModel::month() const
{
    return m_view->month.isValid() ? m_view->month.toString(Qt::ISODate) : QString();
}

void ViewResultModel::setMonth(const QString &month)
{
    const QDate m = QDate::fromString(month, Qt::ISODate);
    if (!m.isValid() || m == m_month)
        return;
    m_month = QDate(m.year(), m.month(), 1);
    refresh();
}

QString ViewResultModel::monthTitle() const
{
    if (!m_view->month.isValid())
        return {};
    const QLocale locale(m_controller && m_controller->locale() == u"en" ? QLocale::English : QLocale::Polish);
    QString name = locale.standaloneMonthName(m_view->month.month(), QLocale::LongFormat);
    if (!name.isEmpty())
        name[0] = name[0].toUpper();
    return name + u' ' + QString::number(m_view->month.year());
}

QStringList ViewResultModel::weekDays() const
{
    const QLocale locale(m_controller && m_controller->locale() == u"en" ? QLocale::English : QLocale::Polish);
    const int start = int(weekStartDay());
    QStringList out;
    for (int i = 0; i < 7; ++i)
        out.append(locale.standaloneDayName((start - 1 + i) % 7 + 1, QLocale::ShortFormat));
    return out;
}

void ViewResultModel::hold(QObject *holder)
{
    if (!holder || m_holders.contains(holder))
        return;
    const bool wasHeld = held();
    m_holders.insert(holder, connect(holder, &QObject::destroyed, this, [this, holder] { release(holder); }));
    if (!wasHeld)
        Q_EMIT heldChanged();
}

void ViewResultModel::release(QObject *holder)
{
    const auto it = m_holders.constFind(holder);
    if (it == m_holders.cend())
        return;
    disconnect(it.value());
    m_holders.erase(it);
    if (held())
        return;
    Q_EMIT heldChanged();
    if (m_pending)
        schedule();
}

void ViewResultModel::timerFired()
{
    if (held())
        m_pending = true;
    else
        refresh();
}

void ViewResultModel::schedule()
{
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

QString ViewResultModel::viewText() const
{
    if (!m_basePath.isEmpty() && m_host) {
        bool ok = false;
        const QString text = m_host->read(m_basePath, &ok);
        if (ok)
            return text;
    }
    return m_source;
}

void ViewResultModel::refresh()
{
    m_timer.stop();
    m_pending = false;
    auto *queries = qobject_cast<QueryHost *>(m_host.data());
    QVariantMap result;
    const QString text = viewText();
    if (queries && m_controller && !text.trimmed().isEmpty()) {
        QVariantMap state{{u"view"_s, m_viewIndex}, {u"viewName"_s, m_viewName}};
        if (m_month.isValid())
            state.insert(u"month"_s, m_month.toString(Qt::ISODate));
        const QString current = !m_thisPath.isEmpty()  ? m_thisPath
                                : m_basePath.isEmpty() ? m_controller->path()
                                                       : m_basePath;
        result = queries->runView(m_language, text, current, state);
    } else if (queries && m_controller && m_language == u"base") {
        // An empty base: one table of every file (as Obsidian shows a new base).
        result = queries->runView(m_language, u"views:\n  - type: table\n"_s,
                                  m_basePath.isEmpty() ? m_controller->path() : m_basePath, {});
    }
    m_ready = queries != nullptr;
    if (result == m_result && !m_rows.isEmpty()) {
        Q_EMIT resultChanged();
        return;
    }
    m_result = result;
    m_ok = result.value(u"ok"_s).toBool();
    m_error = result.value(u"error"_s).toString();
    m_complete = result.value(u"complete"_s, true).toBool();
    if (const int view = result.value(u"view"_s, m_viewIndex).toInt();
        view != m_viewIndex && m_language == u"base" && !m_viewName.isEmpty()) {
        m_viewIndex = view; // (a view by its name)
        Q_EMIT viewIndexChanged();
    }
    m_written.clear();
    *m_view = m_ok ? notebook::viewResultFromJson(QJsonObject::fromVariantMap(result)) : views::ViewResult();
    rebuild();
}

QString ViewResultModel::render(const QString &markdown) const
{
    if (!m_controller)
        return markdown::InlineRenderer::escape(markdown);
    markdown::RenderContext ctx = m_controller->blocks()->renderContextFor(m_controller->path(), markdown, 0);
    ctx.inlineCalc = nullptr;
    ctx.hideTransclusions = false;
    ctx.dateLinks = false;
    return markdown::InlineRenderer(ctx).render(markdown);
}

QUrl ViewResultModel::coverUrl(const QString &cover, const QString &kind) const
{
    if (!m_host || cover.isEmpty())
        return {};
    if (kind == u"file")
        return m_host->assetUrl(cover);
    if (kind == u"web") {
        const QVariantMap picture = m_host->webPicture(cover);
        if (picture.value(u"state"_s).toString() == u"ready")
            return picture.value(u"url"_s).toUrl();
    }
    return {};
}

void ViewResultModel::rebuild()
{
    QList<Row> rows;
    rows.reserve(m_view->rows.size());
    const QList<QVariant> resultRows = m_result.value(u"rows"_s).toList();
    const markdown::Palette palette = m_controller ? m_controller->rendererPalette() : markdown::Palette();
    for (qsizetype i = 0; i < m_view->rows.size(); ++i) {
        const views::Row &v = m_view->rows.at(i);
        Row r;
        for (qsizetype c = 0; c < v.cells.size(); ++c) {
            const views::Cell &cell = v.cells.at(c);
            r.cells.append(cell.error ? markdown::InlineRenderer::escape(cell.markdown) : render(cell.markdown));
            r.texts.append(cell.text);
        }
        r.headline = r.cells.value(0);
        for (qsizetype c = 1; c < v.cells.size(); ++c) {
            if (c == m_view->groupColumn || v.cells.at(c).empty || c >= m_view->columns.size())
                continue;
            r.details.append(u"<span style=\"color:"_s + palette.muted + u"\">"_s
                             + markdown::InlineRenderer::escape(m_view->columns.at(c).label) + u"</span>&nbsp; "_s
                             + r.cells.at(c));
        }
        const QVariantMap source = resultRows.value(i).toMap();
        r.status = statusOf(source);
        r.isTask = !r.status.isEmpty();
        if (v.coverKind == u"color")
            r.coverColor = v.cover;
        else
            r.cover = coverUrl(v.cover, v.coverKind).toString();
        rows.append(r);
    }
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
    // Columns and groups.
    QString sortedKey;
    bool sortedDesc = false;
    if (m_language == u"query") {
        if (const auto parsed = parseQuery(m_source); std::holds_alternative<Query>(parsed)) {
            const Query &q = std::get<Query>(parsed);
            if (!q.sort.isEmpty()) {
                sortedKey = queryPropertyKey(q.sort.first().name);
                sortedDesc = q.sort.first().descending;
            }
        }
    } else if (const auto base = views::parseBase(viewText()); std::holds_alternative<views::Base>(base)) {
        const views::Base &b = std::get<views::Base>(base);
        const qsizetype view = std::min<qsizetype>(m_viewIndex, b.views.size() - 1);
        if (view >= 0 && !b.views.at(view).sort.isEmpty()) {
            sortedKey = views::baseProperty(b.views.at(view).sort.first().property);
            sortedDesc = b.views.at(view).sort.first().descending;
        }
    }
    QList<QVariantMap> columns;
    const QVariantList resultColumns = m_result.value(u"columns"_s).toList();
    for (qsizetype c = 0; c < m_view->columns.size(); ++c) {
        const views::Column &col = m_view->columns.at(c);
        QString key = col.sortKey;
        if (key.endsWith(u"::"))
            key.chop(2);
        const bool sorted = !col.sortKey.isEmpty()
                            && (m_language == u"base" ? col.sortKey == sortedKey : queryPropertyKey(key) == sortedKey);
        columns.append(QVariantMap{{u"label"_s, col.label},
                                   {u"kind"_s, resultColumns.value(c).toMap().value(u"kind"_s).toString()},
                                   {u"type"_s, col.type},
                                   {u"editable"_s, col.editable && m_controller && !m_controller->readOnly()},
                                   {u"sortKey"_s, col.sortKey},
                                   {u"sorted"_s, sorted ? (sortedDesc ? -1 : 1) : 0},
                                   {u"summaryLabel"_s, col.summaryLabel},
                                   {u"summaryText"_s, col.summaryText},
                                   {u"number"_s, int(c)}});
    }
    m_columns->setItems(columns);
    QList<QVariantMap> groups;
    for (qsizetype g = 0; g < m_view->groups.size(); ++g) {
        const views::Group &group = m_view->groups.at(g);
        QString summary;
        for (const QString &s : group.summaryTexts) {
            if (!s.isEmpty()) {
                summary = s;
                break;
            }
        }
        groups.append(QVariantMap{{u"label"_s, group.label},
                                  {u"value"_s, group.value},
                                  {u"count"_s, int(group.rows.size())},
                                  {u"droppable"_s, group.droppable && movable()},
                                  {u"summary"_s, summary},
                                  {u"group"_s, int(g)}});
    }
    m_groups->setItems(groups);
    ++m_revision;
    Q_EMIT resultChanged();
}

int ViewResultModel::groupRowCount(int group) const
{
    return group >= 0 && group < m_view->groups.size() ? int(m_view->groups.at(group).rows.size()) : 0;
}

int ViewResultModel::groupRowAt(int group, int i) const
{
    if (group < 0 || group >= m_view->groups.size() || i < 0 || i >= m_view->groups.at(group).rows.size())
        return -1;
    return m_view->groups.at(group).rows.at(i);
}

QString ViewResultModel::rowLine(int row) const
{
    if (row < 0 || row >= m_rows.size())
        return {};
    QString html = u"•&nbsp; "_s + m_rows.at(row).headline;
    for (const QString &d : m_rows.at(row).details)
        html += u"&nbsp;&nbsp;·&nbsp; "_s + d;
    return html;
}

QString ViewResultModel::groupLabel(int group) const
{
    return group >= 0 && group < m_view->groups.size() ? m_view->groups.at(group).label : QString();
}

QString ViewResultModel::columnSummary(int column) const
{
    if (column < 0 || column >= m_view->columns.size() || m_view->columns.at(column).summaryText.isEmpty())
        return {};
    return m_view->columns.at(column).summaryLabel + u": "_s + m_view->columns.at(column).summaryText;
}

QList<int> ViewResultModel::tableLines() const
{
    if (m_view->groups.isEmpty())
        return shownRows();
    QList<int> out;
    for (qsizetype g = 0; g < m_view->groups.size(); ++g) {
        out.append(-1 - int(g));
        out.append(m_view->groups.at(g).rows);
    }
    return out;
}

int ViewResultModel::lineOfRow(int row) const
{
    return int(tableLines().indexOf(row));
}

int ViewResultModel::neighbourRow(int row, int step) const
{
    const QList<int> lines = tableLines();
    qsizetype i = lines.indexOf(row);
    if (i < 0 || step == 0)
        return -1;
    const int direction = step > 0 ? 1 : -1;
    for (i += direction; i >= 0 && i < lines.size(); i += direction) {
        if (lines.at(i) >= 0)
            return lines.at(i);
    }
    return -1;
}

double ViewResultModel::columnWidth(int column, bool touch) const
{
    if (column < 0 || column >= m_view->columns.size())
        return 0;
    const views::Column &c = m_view->columns.at(column);
    if (column == 0 || c.kind == views::ColumnKind::Title || c.kind == views::ColumnKind::Text)
        return touch ? 180 : 220;
    const QString &t = c.type;
    if (t == u"checkbox")
        return 76;
    if (t == u"number")
        return 104;
    if (t == u"date" || t == u"taskdate")
        return 124;
    if (t == u"datetime")
        return 150;
    if (t == u"status" || t == u"priority")
        return 124;
    if (c.kind == views::ColumnKind::Path)
        return 220;
    return 160;
}

double ViewResultModel::columnOffset(int column, bool touch) const
{
    double x = 0;
    for (int c = 0; c < column && c < m_view->columns.size(); ++c)
        x += columnWidth(c, touch);
    return x;
}

double ViewResultModel::tableWidth(bool touch) const
{
    return columnOffset(int(m_view->columns.size()), touch);
}

QList<int> ViewResultModel::shownRows() const
{
    QList<int> out;
    if (m_view->groups.isEmpty() || m_view->kind == views::ViewKind::Calendar) {
        for (int i = 0; i < m_rows.size(); ++i)
            out.append(i);
        return out;
    }
    for (const views::Group &g : m_view->groups) {
        for (const int r : g.rows) {
            if (!out.contains(r))
                out.append(r);
        }
    }
    return out;
}

QStringList ViewResultModel::rowCells(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).cells : QStringList();
}

QString ViewResultModel::rowHeadline(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).headline : QString();
}

QStringList ViewResultModel::rowDetails(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).details : QStringList();
}

QString ViewResultModel::rowCover(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).cover : QString();
}

QString ViewResultModel::rowCoverColor(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).coverColor : QString();
}

QString ViewResultModel::rowStatus(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).status : QString();
}

QString ViewResultModel::rowTitle(int row) const
{
    return row >= 0 && row < m_view->rows.size() ? m_view->rows.at(row).title : QString();
}

QString ViewResultModel::rowText(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).texts.value(0) : QString();
}

// ---- groups and days ---------------------------------------------------------------------------------

Qt::DayOfWeek ViewResultModel::weekStartDay() const
{
    return m_host ? vaultConfigFromMap(m_host->vaultConfig()).weekStartsOn : Qt::Monday;
}

QList<int> ViewResultModel::groupRows(int group) const
{
    if (group < 0 || group >= m_view->groups.size())
        return {};
    return m_view->groups.at(group).rows;
}

QString ViewResultModel::dayDate(int day) const
{
    if (!m_view->month.isValid())
        return {};
    return views::calendarRange(m_view->month, weekStartDay()).first.addDays(day).toString(Qt::ISODate);
}

int ViewResultModel::dayNumber(int day) const
{
    return QDate::fromString(dayDate(day), Qt::ISODate).day();
}

bool ViewResultModel::dayInMonth(int day) const
{
    const QDate d = QDate::fromString(dayDate(day), Qt::ISODate);
    return d.isValid() && d.month() == m_view->month.month();
}

bool ViewResultModel::dayIsToday(int day) const
{
    return m_controller && QDate::fromString(dayDate(day), Qt::ISODate) == m_controller->currentDateTime().date();
}

QList<int> ViewResultModel::dayRows(int day) const
{
    const QDate d = QDate::fromString(dayDate(day), Qt::ISODate);
    QList<int> out;
    for (qsizetype i = 0; i < m_view->rows.size(); ++i) {
        if (m_view->rows.at(i).date == d)
            out.append(int(i));
    }
    return out;
}

void ViewResultModel::moveMonth(int months)
{
    const QDate base = months == 0 || !m_view->month.isValid()
                               ? (m_controller ? m_controller->currentDateTime().date() : QDate::currentDate())
                               : m_view->month;
    const QDate target = months == 0 ? base : base.addMonths(months);
    m_month = QDate(target.year(), target.month(), 1);
    refresh();
}

// ---- cells ---------------------------------------------------------------------------------------------

QVariant ViewResultModel::cellValue(int row, int column) const
{
    if (row < 0 || row >= m_view->rows.size() || column < 0 || column >= m_view->rows.at(row).cells.size())
        return {};
    return m_view->rows.at(row).cells.at(column).value;
}

QString ViewResultModel::cellText(int row, int column) const
{
    if (row < 0 || row >= m_view->rows.size() || column < 0 || column >= m_view->rows.at(row).cells.size())
        return {};
    return m_view->rows.at(row).cells.at(column).text;
}

QString ViewResultModel::cellEditText(int row, int column) const
{
    const auto written = m_written.constFind(qint64(row) * 65536 + column);
    if (written != m_written.cend())
        return *written;
    const QVariant v = cellValue(row, column);
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList().join(u", "_s);
    if (v.typeId() == QMetaType::Double) {
        const double n = v.toDouble();
        return n == std::floor(n) && std::abs(n) < 1e15 ? QString::number(qint64(n)) : QString::number(n, 'g', 15);
    }
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? u"true"_s : u"false"_s;
    return v.toString();
}

QString ViewResultModel::columnType(int column) const
{
    if (column < 0 || column >= m_view->columns.size())
        return {};
    return m_view->columns.at(column).editable ? m_view->columns.at(column).type : QString();
}

QString ViewResultModel::columnLabel(int column) const
{
    if (column < 0 || column >= m_view->columns.size())
        return {};
    return m_view->columns.at(column).label;
}

bool ViewResultModel::cellEditable(int row, int column) const
{
    if (!m_controller || m_controller->readOnly() || row < 0 || row >= m_view->rows.size() || column < 0
        || column >= m_view->columns.size())
        return false;
    const views::Column &col = m_view->columns.at(column);
    if (!col.editable)
        return false;
    const views::Row &r = m_view->rows.at(row);
    // A task's fields need a task; an attachment has no properties.
    if ((col.type == u"status" || col.type == u"priority" || col.type == u"taskdate") && r.blockIndex < 0)
        return false;
    return paths::isMarkdown(r.path);
}

// ---- edits -----------------------------------------------------------------------------------------------

QVariantMap ViewResultModel::applyEdit(int row, const void *editPtr)
{
    const views::RowEdit &edit = *static_cast<const views::RowEdit *>(editPtr);
    const views::Row &r = m_view->rows.at(row);
    QVariantMap answer{{u"ok"_s, false}, {u"error"_s, QString()}, {u"undo"_s, false}, {u"title"_s, r.title}};
    if (!m_controller || !m_host)
        return answer;
    // A status goes as the checkbox writes it (this page: an undo step; elsewhere the status history).
    if (edit.field == views::EditField::Status) {
        const bool done = m_controller->setTaskStatusAt(r.path, r.line, edit.value.toString());
        answer.insert(u"ok"_s, done);
        if (done)
            m_timer.start();
        return answer;
    }
    const QDate today = m_controller->currentDateTime().date();
    if (r.path == m_controller->path()) {
        QString error;
        const bool changed = m_controller->applyContentEdit([&](const QString &content) {
            const views::EditOutcome o = views::applyRowEdit(content, edit, today);
            error = o.error;
            return o.content;
        });
        answer.insert(u"ok"_s, changed);
        answer.insert(u"error"_s, error);
        if (changed)
            m_timer.start();
        return answer;
    }
    bool ok = false;
    const QString before = m_host->read(r.path, &ok);
    if (!ok)
        return answer;
    const views::EditOutcome o = views::applyRowEdit(before, edit, today);
    answer.insert(u"error"_s, o.error);
    if (!o.changed)
        return answer;
    if (!m_host->write(r.path, o.content))
        return answer;
    m_undoPath = r.path;
    m_undoBefore = before;
    m_undoAfter = o.content;
    Q_EMIT undoChanged();
    answer.insert(u"ok"_s, true);
    answer.insert(u"undo"_s, true);
    m_timer.start();
    return answer;
}

QVariantMap ViewResultModel::editCell(int row, int column, const QVariant &value)
{
    if (!cellEditable(row, column))
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    const views::Column &col = m_view->columns.at(column);
    const views::Row &r = m_view->rows.at(row);
    views::RowEdit e;
    e.blockRow = r.blockIndex >= 0;
    e.line = r.line;
    e.ownEndLine = r.ownEndLine;
    e.value = value;
    if (col.type == u"status") {
        e.field = views::EditField::Status;
    } else if (col.type == u"priority") {
        e.field = views::EditField::Priority;
    } else if (col.type == u"taskdate") {
        e.field = views::EditField::TaskDate;
        e.key = col.kind == views::ColumnKind::Scheduled ? u"scheduled"_s
                : col.kind == views::ColumnKind::Start   ? u"start"_s
                                                         : u"due"_s;
    } else {
        e.field = views::EditField::Property;
        e.key = col.property;
        e.type = col.type;
    }
    const QVariantMap answer = applyEdit(row, &e);
    if (answer.value(u"ok"_s).toBool() && value.typeId() == QMetaType::QString)
        m_written.insert(qint64(row) * 65536 + column, value.toString());
    return answer;
}

QVariantMap ViewResultModel::moveRow(int row, int from, int to)
{
    if (row < 0 || row >= m_view->rows.size() || !movable())
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    const std::optional<views::RowEdit> e = views::moveEdit(*m_view, m_view->rows.at(row), from, to);
    if (!e)
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    return applyEdit(row, &*e);
}

QVariantMap ViewResultModel::moveRowToDay(int row, int day)
{
    if (row < 0 || row >= m_view->rows.size() || !movable())
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    const QDate d = QDate::fromString(dayDate(day), Qt::ISODate);
    if (d == m_view->rows.at(row).date)
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    const std::optional<views::RowEdit> e = views::dateEdit(*m_view, m_view->rows.at(row), d);
    if (!e)
        return {{u"ok"_s, false}, {u"error"_s, QString()}};
    return applyEdit(row, &*e);
}

QVariantMap ViewResultModel::setRowStatus(int row, const QString &status)
{
    if (row < 0 || row >= m_view->rows.size() || !m_controller)
        return {{u"ok"_s, false}};
    const views::Row &r = m_view->rows.at(row);
    const bool done = m_controller->setTaskStatusAt(r.path, r.line, status);
    if (done)
        m_timer.start();
    return {{u"ok"_s, done}};
}

QVariantMap ViewResultModel::addRow(const QString &title, int group, int day)
{
    QVariantMap answer{{u"ok"_s, false}, {u"error"_s, QString()}, {u"path"_s, QString()}};
    const QString name = title.simplified();
    if (!canAddRow() || name.isEmpty() || !m_host)
        return answer;
    const QDate d = day >= 0 ? QDate::fromString(dayDate(day), Qt::ISODate) : QDate();
    if (m_view->newRow.task) {
        const VaultPath page = m_view->newRow.taskPage;
        const auto append = [&](const QString &content) {
            const QString eol = content.contains(u"\r\n") ? u"\r\n"_s : u"\n"_s;
            QString out = content;
            if (!out.isEmpty() && !out.endsWith(u'\n'))
                out += eol;
            return out + views::newTaskLines(*m_view, name, group, d, eol);
        };
        bool done = false;
        if (page == m_controller->path()) {
            done = m_controller->applyContentEdit(append);
        } else {
            bool ok = false;
            const QString before = m_host->read(page, &ok);
            done = ok && m_host->write(page, append(before));
        }
        answer.insert(u"ok"_s, done);
        answer.insert(u"path"_s, page);
        if (done)
            m_timer.start();
        return answer;
    }
    QString folder = m_view->newRow.folder;
    const QString file = paths::sanitizeFileName(name);
    if (file.isEmpty())
        return answer;
    EditorHost *host = m_host;
    const VaultPath path = paths::uniquePath(paths::join(folder, file + u".md"_s), [host](const VaultPath &p) {
        bool exists = false;
        host->read(p, &exists);
        return exists;
    });
    if (!m_host->write(path, views::newPageContent(*m_view, group, d)))
        return answer;
    answer.insert(u"ok"_s, true);
    answer.insert(u"path"_s, path);
    m_timer.start();
    return answer;
}

bool ViewResultModel::undoLast()
{
    if (m_undoPath.isEmpty() || !m_host)
        return false;
    const QString path = m_undoPath;
    m_undoPath.clear();
    Q_EMIT undoChanged();
    bool ok = false;
    // Only while the file holds what the edit wrote: a later change is not undone.
    if (m_host->read(path, &ok) != m_undoAfter || !ok)
        return false;
    const bool written = m_host->write(path, m_undoBefore);
    if (written)
        m_timer.start();
    return written;
}

// ---- the view's own text ---------------------------------------------------------------------------------

bool ViewResultModel::writeViewText(const QString &text)
{
    if (!canChangeView() || text.isEmpty())
        return false;
    if (!m_basePath.isEmpty()) {
        bool ok = false;
        const QString before = m_host ? m_host->read(m_basePath, &ok) : QString();
        if (!ok || before == text || !m_host->write(m_basePath, text))
            return false;
        m_undoPath = m_basePath;
        m_undoBefore = before;
        m_undoAfter = text;
        Q_EMIT undoChanged();
        refresh();
        return true;
    }
    if (m_blockRow < 0 || text == m_source)
        return false;
    m_controller->setEditText(m_blockRow, text, -1);
    return true;
}

QStringList ViewResultModel::currentColumns() const
{
    if (m_language == u"query")
        return m_result.value(u"columnNames"_s).toStringList();
    QStringList out;
    for (const views::Column &c : m_view->columns)
        out.append(c.id);
    return out;
}

bool ViewResultModel::setColumns(const QStringList &columns)
{
    if (columns.isEmpty())
        return false;
    if (m_language == u"query")
        return writeViewText(views::setQueryOption(m_source, u"columns"_s, views::queryList(columns)));
    const std::optional<QString> yaml = views::setBaseViewOrder(viewText(), m_viewIndex, columns);
    return yaml && writeViewText(*yaml);
}

bool ViewResultModel::sortBy(int column, bool descending)
{
    if (column >= m_view->columns.size())
        return false;
    const QString key = column >= 0 ? m_view->columns.at(column).sortKey : QString();
    if (column >= 0 && key.isEmpty())
        return false;
    if (m_language == u"query")
        return writeViewText(views::setQueryOption(
                m_source, u"sort"_s, key.isEmpty() ? QString() : (descending ? u"-"_s : QString()) + key));
    QList<views::BaseSort> sort;
    if (!key.isEmpty())
        sort.append(views::BaseSort{key, descending});
    const std::optional<QString> yaml = views::setBaseViewSort(viewText(), m_viewIndex, sort);
    return yaml && writeViewText(*yaml);
}

bool ViewResultModel::hideColumn(int column)
{
    QStringList columns = currentColumns();
    if (column < 0 || column >= columns.size() || columns.size() <= 1)
        return false;
    columns.removeAt(column);
    return setColumns(columns);
}

bool ViewResultModel::addColumn(const QString &name)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return false;
    QStringList columns = currentColumns();
    columns.append(m_language == u"base" && !n.contains(u'.') ? u"note."_s + n : n);
    return setColumns(columns);
}

bool ViewResultModel::moveColumn(int from, int to)
{
    QStringList columns = currentColumns();
    if (from < 0 || from >= columns.size() || to < 0 || to >= columns.size() || from == to)
        return false;
    columns.move(from, to);
    return setColumns(columns);
}

QStringList ViewResultModel::columnSuggestions() const
{
    QStringList out;
    if (!m_host)
        return out;
    QSet<QString> shown;
    for (const views::Column &c : m_view->columns) {
        if (c.kind == views::ColumnKind::Property)
            shown.insert(queryPropertyKey(c.property));
    }
    for (const QVariant &k : m_host->propertyKeys()) {
        const QString key = k.toMap().value(u"key"_s).toString();
        if (!key.isEmpty() && !shown.contains(queryPropertyKey(key)) && out.size() < 30)
            out.append(key);
    }
    return out;
}

// ---- navigation ---------------------------------------------------------------------------------------------

QVariantMap ViewResultModel::open(int row, bool newPane)
{
    if (row < 0 || row >= m_view->rows.size() || !m_controller)
        return {};
    const views::Row &r = m_view->rows.at(row);
    return m_controller->openLocation(r.path, r.blockIndex >= 0 ? r.line : -1, newPane);
}

QVariantMap ViewResultModel::activateLink(int row, const QString &url, bool newPane)
{
    if (!m_controller)
        return {};
    if (url.startsWith(u"task:"_s) || url.startsWith(u"date:"_s) || url.startsWith(u"prio:"_s) || url == u"open:")
        return open(row, newPane);
    return m_controller->activateLink(-1, url, newPane);
}

} // namespace brulion::editor
