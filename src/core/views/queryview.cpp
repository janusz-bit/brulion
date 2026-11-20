// The view of a query's results (view.h buildQueryView()).
#include "views/cells_p.h"
#include "views/messages_p.h"
#include "views/view.h"

#include "dataview/dataview.h"
#include "markdown/blockprops.h"
#include "text/text.h"
#include "util/paths.h"

#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::views {

QString viewKindName(ViewKind kind)
{
    switch (kind) {
    case ViewKind::List: return u"list"_s;
    case ViewKind::Table: return u"table"_s;
    case ViewKind::Board: return u"board"_s;
    case ViewKind::Gallery: return u"gallery"_s;
    case ViewKind::Calendar: return u"calendar"_s;
    }
    return {};
}

std::pair<QDate, QDate> calendarRange(QDate month, Qt::DayOfWeek weekStart)
{
    const QDate first(month.year(), month.month(), 1);
    const int back = (first.dayOfWeek() - int(weekStart) + 7) % 7;
    const QDate from = first.addDays(-back);
    return {from, from.addDays(41)};
}

namespace {

using detail::CellReader;

QString columnId(const QueryColumn &c)
{
    switch (c.kind) {
    case QueryColumnKind::Title: return u"title"_s;
    case QueryColumnKind::Path: return u"path"_s;
    case QueryColumnKind::Folder: return u"folder"_s;
    case QueryColumnKind::Tags: return u"tags"_s;
    case QueryColumnKind::Modified: return u"mtime"_s;
    case QueryColumnKind::Size: return u"size"_s;
    case QueryColumnKind::Date: return u"date"_s;
    case QueryColumnKind::Text: return u"text"_s;
    case QueryColumnKind::Status: return u"status"_s;
    case QueryColumnKind::Priority: return u"priority"_s;
    case QueryColumnKind::Due: return u"due"_s;
    case QueryColumnKind::Scheduled: return u"scheduled"_s;
    case QueryColumnKind::Start: return u"start"_s;
    case QueryColumnKind::Done: return u"done"_s;
    case QueryColumnKind::Created: return u"created"_s;
    case QueryColumnKind::Property: return u"prop:"_s + c.property;
    case QueryColumnKind::Formula: return u"formula:"_s + queryPropertyKey(c.label);
    }
    return {};
}

/// The `sort:` name of a column (view.h Column::sortKey).
QString sortKeyOf(const QueryColumn &c, QueryType type)
{
    const bool tasks = type != QueryType::Pages;
    switch (c.kind) {
    case QueryColumnKind::Title: return u"page"_s;
    case QueryColumnKind::Text: return type == QueryType::Pages ? u"page"_s : QString();
    case QueryColumnKind::Path: return u"path"_s;
    case QueryColumnKind::Date: return u"date"_s;
    case QueryColumnKind::Status: return tasks ? u"status"_s : QString();
    case QueryColumnKind::Priority: return tasks ? u"priority"_s : QString();
    case QueryColumnKind::Due: return tasks ? u"due"_s : QString();
    case QueryColumnKind::Scheduled: return tasks ? u"scheduled"_s : QString();
    case QueryColumnKind::Start: return tasks ? u"start"_s : QString();
    case QueryColumnKind::Done: return tasks ? u"done"_s : QString();
    case QueryColumnKind::Created: return tasks ? u"created"_s : QString();
    case QueryColumnKind::Property: {
        // A name that is a built-in sort key of the query's type needs the property mark.
        static const QStringList builtins{u"due"_s,   u"termin"_s, u"scheduled"_s, u"start"_s,  u"done"_s,
                                          u"created"_s, u"remind"_s, u"date"_s,    u"data"_s,   u"priority"_s,
                                          u"status"_s, u"page"_s,  u"title"_s,     u"path"_s,   u"strona"_s};
        const bool clash = builtins.contains(c.property);
        QString name = c.name;
        if (name.contains(u' ') || name.contains(u','))
            name = u'"' + name + u'"';
        return clash || c.name.contains(u' ') ? name + u"::"_s : name;
    }
    default: return {};
    }
}

QueryColumn builtin(QueryColumnKind kind)
{
    QueryColumn c;
    c.kind = kind;
    return c;
}

QueryColumn propertyColumn(const QString &name)
{
    QueryColumn c;
    c.kind = QueryColumnKind::Property;
    c.name = name;
    c.property = queryPropertyKey(name);
    return c;
}

/// Page-property keys no table needs a column for (the page's look and mode, tags and aliases).
bool isHiddenKey(const QString &folded)
{
    static const QSet<QString> hidden{u"tags"_s,   u"tag"_s,       u"aliases"_s,  u"alias"_s,     u"brulion"_s,
                                      u"icon"_s,   u"banner"_s,    u"banner-y"_s, u"banner-icon"_s, u"cssclasses"_s,
                                      u"cssclass"_s, u"publish"_s, u"position"_s};
    return hidden.contains(folded);
}

class Builder {
public:
    Builder(const Query &query, const QueryResult &result, const VaultIndex &index, const ViewOptions &options)
        : m_query(query), m_result(result), m_index(index), m_options(options), m_cells(index, options)
    { }

    std::variant<ViewResult, QString> build();

private:
    QList<QueryColumn> defaultColumns() const;
    QString addColumns(const QList<QueryColumn> &columns);
    Row rowOf(const QueryItem &item);
    void fillCells(Row &row);
    void buildGroups();
    void calendar();
    void summaries();
    void newRow();
    QDate rowDate(const Row &row);

    const Query &m_query;
    const QueryResult &m_result;
    const VaultIndex &m_index;
    const ViewOptions &m_options;
    CellReader m_cells;
    ViewResult m_out;
    QList<QueryColumn> m_columns;               ///< one per m_out.columns
    QList<dataview::Expression> m_formulas;     ///< one per column (null for non-formulas)
    QHash<QString, PropertyKeyInfo> m_keyInfo; ///< the notebook's page-property keys (folded)
    /// Calendar: what the days come from.
    QString m_dateKind;
    QString m_dateProperty; ///< folded
};

QList<QueryColumn> Builder::defaultColumns() const
{
    QList<QueryColumn> out;
    switch (m_query.type) {
    case QueryType::Tasks:
        out = {builtin(QueryColumnKind::Text), builtin(QueryColumnKind::Status), builtin(QueryColumnKind::Due),
               builtin(QueryColumnKind::Priority), builtin(QueryColumnKind::Title)};
        break;
    case QueryType::Blocks:
    case QueryType::Reminders: out = {builtin(QueryColumnKind::Text), builtin(QueryColumnKind::Title)}; break;
    case QueryType::Pages: {
        out = {builtin(QueryColumnKind::Title)};
        // The page properties most results have (at most five), as most notes spell them.
        QHash<QString, int> counts;
        QHash<QString, QString> spelling;
        QStringList order;
        const qsizetype scan = std::min<qsizetype>(m_result.items.size(), 300);
        for (qsizetype i = 0; i < scan; ++i) {
            const ParsedNote *note = m_index.note(m_result.items.at(i).path);
            if (!note)
                continue;
            QSet<QString> seen;
            const auto count = [&](const QString &key) {
                const QString k = queryPropertyKey(key);
                if (k.isEmpty() || isHiddenKey(k) || seen.contains(k))
                    return;
                seen.insert(k);
                if (!counts.contains(k)) {
                    order.append(k);
                    spelling.insert(k, key);
                }
                ++counts[k];
            };
            for (auto [key, value] : note->frontmatter.asKeyValueRange()) {
                Q_UNUSED(value);
                count(key);
            }
            for (auto [key, value] : note->properties.asKeyValueRange()) {
                Q_UNUSED(value);
                count(key);
            }
        }
        std::ranges::stable_sort(order, [&](const QString &a, const QString &b) { return counts[a] > counts[b]; });
        for (qsizetype i = 0; i < order.size() && i < 5; ++i)
            out.append(propertyColumn(spelling.value(order.at(i))));
        break;
    }
    }
    // A board or calendar shows the property it groups by.
    if (m_query.group == QueryGroupKey::Property
        && std::ranges::none_of(out, [&](const QueryColumn &c) { return c.property == m_query.groupProperty; }))
        out.insert(std::min<qsizetype>(1, out.size()), propertyColumn(m_query.groupPropertyName));
    return out;
}

/// Columns of the view; an error message for a formula that does not parse.
QString Builder::addColumns(const QList<QueryColumn> &columns)
{
    for (const PropertyKeyInfo &info : m_index.propertyKeys())
        m_keyInfo.insert(queryPropertyKey(info.key), info);
    const bool blockRows = m_query.type != QueryType::Pages;
    for (const QueryColumn &c : columns) {
        Column col;
        col.kind = c.kind;
        col.id = columnId(c);
        dataview::Expression formula;
        switch (c.kind) {
        case QueryColumnKind::Property: {
            const auto info = m_keyInfo.constFind(c.property);
            // Pages write the key as most notes spell it; block lines as the query wrote it.
            col.property = blockRows || info == m_keyInfo.cend() ? c.name : info->key;
            col.label = c.label.isEmpty() ? c.name : c.label;
            col.type = info != m_keyInfo.cend() && !info->type.isEmpty() ? info->type : u"text"_s;
            col.editable = col.type != u"unsupported" && (!blockRows || markdown::isBlockPropertyKey(col.property));
            break;
        }
        case QueryColumnKind::Formula: {
            col.label = c.label;
            const auto parsed = dataview::parseExpression(c.formula);
            if (const QString *error = std::get_if<QString>(&parsed))
                return messages::formulaError(c.label, *error);
            formula = std::get<dataview::Expression>(parsed);
            break;
        }
        case QueryColumnKind::Status:
        case QueryColumnKind::Priority:
            col.label = c.label.isEmpty() ? messages::columnLabel(c.kind) : c.label;
            col.type = c.kind == QueryColumnKind::Status ? u"status"_s : u"priority"_s;
            col.editable = m_query.type == QueryType::Tasks;
            break;
        case QueryColumnKind::Due:
        case QueryColumnKind::Scheduled:
        case QueryColumnKind::Start:
            col.label = c.label.isEmpty() ? messages::columnLabel(c.kind) : c.label;
            col.type = u"taskdate"_s;
            col.editable = m_query.type == QueryType::Tasks;
            break;
        default: col.label = c.label.isEmpty() ? messages::columnLabel(c.kind) : c.label; break;
        }
        col.sortKey = sortKeyOf(c, m_query.type);
        m_out.columns.append(col);
        m_columns.append(c);
        m_formulas.append(formula);
    }
    return {};
}

Row Builder::rowOf(const QueryItem &item)
{
    Row row;
    row.path = item.path;
    row.blockIndex = item.blockIndex;
    row.task = item.task;
    const ParsedNote *note = m_index.note(item.path);
    row.title = note ? note->title : paths::pageTitle(item.path);
    if (note && item.blockIndex >= 0 && item.blockIndex < note->blocks.size()) {
        row.line = note->blocks.at(item.blockIndex).line;
        row.ownEndLine = note->blocks.at(item.blockIndex).ownEndLine;
    }
    return row;
}

void Builder::fillCells(Row &row)
{
    for (qsizetype i = 0; i < m_columns.size(); ++i) {
        const QueryColumn &c = m_columns.at(i);
        Cell cell;
        switch (c.kind) {
        case QueryColumnKind::Property: cell = m_cells.propertyCell(row, c.property); break;
        case QueryColumnKind::Formula: {
            dataview::Evaluator &eval = m_cells.evaluator();
            const dataview::Value base = row.blockIndex >= 0 ? eval.block(row.path, row.blockIndex) : eval.page(row.path);
            const auto value = eval.evaluate(m_formulas.at(i), base);
            if (const QString *error = std::get_if<QString>(&value)) {
                cell.markdown = cell.text = *error;
                cell.error = true;
                cell.empty = false;
            } else {
                cell = detail::valueCell(std::get<dataview::Value>(value), m_index);
            }
            break;
        }
        default: cell = m_cells.builtinCell(row, c.kind); break;
        }
        // An empty property column takes its type from the first value it shows.
        Column &col = m_out.columns[i];
        if (c.kind == QueryColumnKind::Property && !cell.empty && !m_keyInfo.contains(c.property)
            && col.type == u"text"_s && row.blockIndex >= 0 && cell.value.isValid()) {
            switch (cell.value.typeId()) {
            case QMetaType::Double: col.type = u"number"_s; break;
            case QMetaType::Bool: col.type = u"checkbox"_s; break;
            case QMetaType::QStringList: col.type = u"list"_s; break;
            default: break;
            }
        }
        row.cells.append(cell);
    }
    if (m_out.kind == ViewKind::Gallery || m_out.kind == ViewKind::Board)
        m_cells.cover(row);
}

QDate Builder::rowDate(const Row &row)
{
    if (m_dateKind == u"due" || m_dateKind == u"scheduled" || m_dateKind == u"start") {
        if (!row.task)
            return {};
        const std::optional<DateSpec> &d = m_dateKind == u"due" ? row.task->due
                                           : m_dateKind == u"scheduled" ? row.task->scheduled
                                                                        : row.task->start;
        return d ? d->date : QDate();
    }
    if (m_dateKind == u"property") {
        const Cell c = m_cells.propertyCell(row, m_dateProperty);
        const QString v = c.value.toString().trimmed();
        return v.size() >= 10 ? QDate::fromString(v.first(10), Qt::ISODate) : QDate();
    }
    const ParsedNote *note = m_index.note(row.path);
    return note && note->journalDate ? *note->journalDate : QDate();
}

void Builder::calendar()
{
    // The days come from the `group:` date, else (tasks) the due date, else a date column, else the
    // journal date.
    switch (m_query.group) {
    case QueryGroupKey::Due: m_dateKind = u"due"_s; break;
    case QueryGroupKey::Scheduled: m_dateKind = u"scheduled"_s; break;
    case QueryGroupKey::Date: m_dateKind = u"journal"_s; break;
    case QueryGroupKey::Property:
        m_dateKind = u"property"_s;
        m_dateProperty = m_query.groupProperty;
        break;
    default:
        if (m_query.type == QueryType::Tasks) {
            m_dateKind = u"due"_s;
            break;
        }
        for (qsizetype i = 0; i < m_out.columns.size(); ++i) {
            const Column &c = m_out.columns.at(i);
            if (c.kind == QueryColumnKind::Property && (c.type == u"date" || c.type == u"datetime")) {
                m_dateKind = u"property"_s;
                m_dateProperty = m_columns.at(i).property;
                break;
            }
            if (c.kind == QueryColumnKind::Due) {
                m_dateKind = u"due"_s;
                break;
            }
        }
        if (m_dateKind.isEmpty())
            m_dateKind = u"journal"_s;
        break;
    }
    for (qsizetype i = 0; i < m_out.columns.size(); ++i) {
        const QueryColumn &c = m_columns.at(i);
        if ((m_dateKind == u"property" && c.kind == QueryColumnKind::Property && c.property == m_dateProperty)
            || (m_dateKind == u"due" && c.kind == QueryColumnKind::Due)
            || (m_dateKind == u"scheduled" && c.kind == QueryColumnKind::Scheduled)
            || (m_dateKind == u"journal" && c.kind == QueryColumnKind::Date))
            m_out.dateColumn = int(i);
    }
    m_out.groupKind = m_dateKind == u"journal" ? QString() : m_dateKind == u"property" ? u"date"_s : u"taskdate"_s;
    if (m_dateKind == u"property") {
        const auto info = m_keyInfo.constFind(m_dateProperty);
        m_out.groupProperty = info != m_keyInfo.cend() && m_query.type == QueryType::Pages ? info->key
                                                                                           : m_query.groupPropertyName;
        if (m_out.groupProperty.isEmpty() && m_out.dateColumn >= 0)
            m_out.groupProperty = m_out.columns.at(m_out.dateColumn).property;
    } else if (m_out.groupKind == u"taskdate") {
        m_out.groupProperty = m_dateKind;
    }
}

void Builder::buildGroups()
{
    QueryGroupKey group = m_query.group;
    const bool board = m_out.kind == ViewKind::Board;
    if (board && group == QueryGroupKey::None && m_query.type == QueryType::Tasks)
        group = QueryGroupKey::Status;
    if (group == QueryGroupKey::None || m_out.kind == ViewKind::Calendar)
        return;
    switch (group) {
    case QueryGroupKey::Status: m_out.groupKind = u"status"_s; break;
    case QueryGroupKey::Priority: m_out.groupKind = u"priority"_s; break;
    case QueryGroupKey::Property: {
        m_out.groupKind = u"property"_s;
        const auto info = m_keyInfo.constFind(m_query.groupProperty);
        m_out.groupProperty = info != m_keyInfo.cend() && m_query.type == QueryType::Pages ? info->key
                                                                                           : m_query.groupPropertyName;
        if (m_query.type != QueryType::Pages && !markdown::isBlockPropertyKey(m_out.groupProperty))
            m_out.groupKind.clear(); // (block rows write `key::` lines: an ASCII key)
        break;
    }
    case QueryGroupKey::Due:
    case QueryGroupKey::Scheduled:
        m_out.groupKind = u"taskdate"_s;
        m_out.groupProperty = group == QueryGroupKey::Due ? u"due"_s : u"scheduled"_s;
        break;
    default: break; // page, tag, journal date: no edit moves a row there
    }
    if (m_query.type != QueryType::Tasks && (m_out.groupKind == u"status" || m_out.groupKind == u"priority"
                                              || m_out.groupKind == u"taskdate"))
        m_out.groupKind.clear();

    // The groups of the result (built from the items the view shows), by key.
    QList<QueryGroup> groups = m_result.groups;
    if (groups.isEmpty() && group == QueryGroupKey::Status) {
        // A board of tasks without `group:`: by status, as group:status would group them.
        QHash<QString, qsizetype> at;
        for (const QueryItem &item : m_result.items) {
            if (!item.task)
                continue;
            const QString key = detail::statusName(item.task->status);
            if (!at.contains(key)) {
                at.insert(key, groups.size());
                groups.append(QueryGroup{key, queryStatusLabel(item.task->status), {}, QString()});
            }
            groups[at.value(key)].items.append(item);
        }
    }
    // A status board always has to do, in progress and done; deferred and cancelled when they hold cards.
    if (board && group == QueryGroupKey::Status) {
        static const QList<TaskStatus> order{TaskStatus::Todo, TaskStatus::Doing, TaskStatus::Done,
                                             TaskStatus::Deferred, TaskStatus::Cancelled};
        QList<QueryGroup> sorted;
        for (const TaskStatus s : order) {
            const QString key = detail::statusName(s);
            const auto it = std::ranges::find(groups, key, &QueryGroup::key);
            if (it != groups.cend())
                sorted.append(*it);
            else if (s == TaskStatus::Todo || s == TaskStatus::Doing || s == TaskStatus::Done)
                sorted.append(QueryGroup{key, queryStatusLabel(s), {}, QString()});
        }
        groups = sorted;
    }
    // A board by a property also shows the notebook's other values of it (a few: a category, not a
    // free text), so a card can go where no card is yet; "No value" stays last.
    if (board && group == QueryGroupKey::Property && m_query.type == QueryType::Pages) {
        const QList<PropertyValueCount> known = m_index.propertyValues(m_out.groupProperty, {}, 13);
        if (!known.isEmpty() && known.size() <= 12) {
            QSet<QString> present;
            for (const QueryGroup &g : std::as_const(groups))
                present.insert(text::foldKey(g.value.isEmpty() ? g.label : g.value));
            qsizetype insertAt = groups.size();
            if (!groups.isEmpty() && groups.last().key.isEmpty())
                insertAt = groups.size() - 1;
            for (const PropertyValueCount &v : known) {
                QString value = v.value;
                if (present.contains(text::foldKey(value)))
                    continue;
                present.insert(text::foldKey(value));
                groups.insert(insertAt++, QueryGroup{u"new:"_s + text::foldKey(value), value, {}, value});
            }
        }
        if (std::ranges::none_of(groups, [](const QueryGroup &g) { return g.key.isEmpty(); }))
            groups.append(QueryGroup{QString(), messages::noValue(), {}, QString()});
    }
    // Rows by (path, block): where each item of a group is in m_out.rows.
    QHash<QString, int> rowAt;
    for (qsizetype i = 0; i < m_out.rows.size(); ++i)
        rowAt.insert(m_out.rows.at(i).path + u'\x1f' + QString::number(m_out.rows.at(i).blockIndex), int(i));
    for (const QueryGroup &g : std::as_const(groups)) {
        Group out;
        out.key = g.key;
        out.label = g.label;
        switch (group) {
        case QueryGroupKey::Status:
        case QueryGroupKey::Priority: out.value = g.key; break;
        case QueryGroupKey::Property: out.value = g.value; break;
        case QueryGroupKey::Due:
        case QueryGroupKey::Scheduled: out.value = g.key; break;
        default: break;
        }
        out.droppable = !m_out.groupKind.isEmpty() && !(group == QueryGroupKey::Status && g.key.isEmpty());
        for (const QueryItem &item : g.items) {
            const int r = rowAt.value(item.path + u'\x1f' + QString::number(item.blockIndex), -1);
            if (r < 0 || out.rows.contains(r))
                continue;
            out.rows.append(r);
            if (m_out.rows[r].group < 0)
                m_out.rows[r].group = int(m_out.groups.size());
        }
        m_out.groups.append(out);
    }
    for (qsizetype i = 0; i < m_columns.size(); ++i) {
        const QueryColumn &c = m_columns.at(i);
        if ((group == QueryGroupKey::Property && c.kind == QueryColumnKind::Property && c.property == m_query.groupProperty)
            || (group == QueryGroupKey::Status && c.kind == QueryColumnKind::Status)
            || (group == QueryGroupKey::Priority && c.kind == QueryColumnKind::Priority)
            || (group == QueryGroupKey::Due && c.kind == QueryColumnKind::Due)
            || (group == QueryGroupKey::Scheduled && c.kind == QueryColumnKind::Scheduled))
            m_out.groupColumn = int(i);
    }
}

void Builder::summaries()
{
    for (const QuerySummary &s : m_query.summaries) {
        qsizetype column = -1;
        for (qsizetype i = 0; i < m_columns.size() && column < 0; ++i) {
            const QueryColumn &c = m_columns.at(i);
            if (queryPropertyKey(c.name) == s.column || (!c.label.isEmpty() && queryPropertyKey(c.label) == s.column)
                || (c.kind == QueryColumnKind::Property && c.property == s.column)
                || queryPropertyKey(m_out.columns.at(i).label) == s.column || m_out.columns.at(i).id == s.column)
                column = i;
        }
        if (column < 0)
            continue; // reported by build()
        Column &col = m_out.columns[column];
        col.summary = s.function;
        col.summaryLabel = summaryLabel(s.function);
        QList<QVariant> values;
        for (const Row &r : std::as_const(m_out.rows))
            values.append(r.cells.at(column).value);
        col.summaryText = summarize(s.function, values);
        for (Group &g : m_out.groups) {
            while (g.summaryTexts.size() < m_out.columns.size())
                g.summaryTexts.append(QString());
            QList<QVariant> groupValues;
            for (const int r : std::as_const(g.rows))
                groupValues.append(m_out.rows.at(r).cells.at(column).value);
            g.summaryTexts[column] = summarize(s.function, groupValues);
        }
    }
}

void Builder::newRow()
{
    NewRow &n = m_out.newRow;
    if (m_query.type == QueryType::Blocks || m_query.type == QueryType::Reminders)
        return;
    n.possible = true;
    n.task = m_query.type == QueryType::Tasks;
    n.taskPage = m_options.currentPath;
    if (n.task && n.taskPage.isEmpty())
        n.possible = false;
    for (const QueryFilter &f : m_query.filters) {
        if (f.negate)
            continue;
        switch (f.kind) {
        case QueryFilterKind::Path:
            if (f.values.size() == 1 && !f.values.first().contains(u'*') && n.folder.isEmpty()) {
                QString folder = f.values.first().trimmed();
                while (folder.endsWith(u'/'))
                    folder.chop(1);
                // A path that names a note's file names that note's folder.
                if (m_index.contains(folder + u".md"_s))
                    folder = paths::dirname(folder);
                n.folder = folder;
            }
            break;
        case QueryFilterKind::Tag:
            if (f.values.size() == 1)
                n.tags.append(f.values.first());
            break;
        case QueryFilterKind::Property:
            if (f.conditions.size() == 1 && f.conditions.first().op == QueryPropertyOp::Equals
                && !f.conditions.first().value.contains(u'*'))
                n.properties.append({f.propertyName, f.conditions.first().value});
            break;
        default: break;
        }
    }
    if (n.folder.isEmpty())
        n.folder = m_options.newPagesFolder;
}

std::variant<ViewResult, QString> Builder::build()
{
    switch (m_query.view) {
    case QueryView::None:
    case QueryView::List: m_out.kind = ViewKind::List; break;
    case QueryView::Table: m_out.kind = ViewKind::Table; break;
    case QueryView::Board: m_out.kind = ViewKind::Board; break;
    case QueryView::Gallery: m_out.kind = ViewKind::Gallery; break;
    case QueryView::Calendar: m_out.kind = ViewKind::Calendar; break;
    }
    m_out.type = m_result.type;
    m_out.total = m_result.total;
    if (const QString error = addColumns(m_query.columns.isEmpty() ? defaultColumns() : m_query.columns);
        !error.isEmpty())
        return error;
    for (const QuerySummary &s : m_query.summaries) {
        const bool found = std::ranges::any_of(m_columns, [&](const QueryColumn &c) {
            return queryPropertyKey(c.name) == s.column || (!c.label.isEmpty() && queryPropertyKey(c.label) == s.column)
                   || (c.kind == QueryColumnKind::Property && c.property == s.column);
        }) || std::ranges::any_of(m_out.columns, [&](const Column &c) {
            return queryPropertyKey(c.label) == s.column || c.id == s.column;
        });
        if (!found)
            return messages::summaryWithoutColumn(s.column);
    }
    if (m_out.kind == ViewKind::Calendar) {
        calendar();
        const QDate month = m_options.month.isValid() ? m_options.month : m_options.today;
        m_out.month = QDate(month.year(), month.month(), 1);
    }
    const auto [from, to] = calendarRange(m_out.month.isValid() ? m_out.month : m_options.today, m_options.weekStart);
    const int limit = m_query.limit.value_or(m_options.defaultLimit);
    int shown = 0;
    for (const QueryItem &item : m_result.items) {
        Row row = rowOf(item);
        if (m_out.kind == ViewKind::Calendar) {
            row.date = rowDate(row);
            if (!row.date.isValid() || row.date < from || row.date > to)
                continue;
        }
        if (shown >= limit) {
            m_out.limited = true;
            break;
        }
        ++shown;
        fillCells(row);
        m_out.rows.append(std::move(row));
    }
    m_out.limited = m_out.limited || m_result.total > m_result.items.size();
    if (m_out.kind == ViewKind::Calendar)
        m_out.total = int(m_out.rows.size()) + (m_out.limited ? 1 : 0);
    buildGroups();
    summaries();
    newRow();
    return m_out;
}

} // namespace

std::variant<ViewResult, QString> buildQueryView(const Query &query, const QueryResult &result,
                                                 const VaultIndex &index, const ViewOptions &options)
{
    return Builder(query, result, index, options).build();
}

} // namespace brulion::views
