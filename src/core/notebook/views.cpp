// Database views as JSON (views.h).
#include "notebook/views.h"

#include "notebook/json.h"
#include "views/bases.h"
#include "views/edits.h"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace brulion::notebook {

namespace {

QString columnKindName(views::ColumnKind kind)
{
    using K = views::ColumnKind;
    switch (kind) {
    case K::Title: return u"title"_s;
    case K::Path: return u"path"_s;
    case K::Folder: return u"folder"_s;
    case K::Tags: return u"tags"_s;
    case K::Modified: return u"mtime"_s;
    case K::Size: return u"size"_s;
    case K::Date: return u"date"_s;
    case K::Text: return u"text"_s;
    case K::Status: return u"status"_s;
    case K::Priority: return u"priority"_s;
    case K::Due: return u"due"_s;
    case K::Scheduled: return u"scheduled"_s;
    case K::Start: return u"start"_s;
    case K::Done: return u"done"_s;
    case K::Created: return u"created"_s;
    case K::Property: return u"property"_s;
    case K::Formula: return u"formula"_s;
    }
    return {};
}

QJsonValue valueJson(const QVariant &v)
{
    switch (v.typeId()) {
    case QMetaType::Bool: return v.toBool();
    case QMetaType::Double:
    case QMetaType::Int:
    case QMetaType::LongLong: return v.toDouble();
    case QMetaType::QStringList: return QJsonArray::fromStringList(v.toStringList());
    case QMetaType::QString: return v.toString();
    default: return v.isValid() ? QJsonValue(v.toString()) : QJsonValue();
    }
}

QJsonObject failed(const QString &language, const QString &error)
{
    return QJsonObject{{u"ok"_s, false}, {u"error"_s, error}, {u"language"_s, language}};
}

views::ViewOptions optionsOf(const ViewRequest &request)
{
    views::ViewOptions o;
    o.today = request.now.date();
    o.now = request.now;
    o.weekStart = request.weekStart;
    o.currentPath = request.currentPath;
    o.stat = request.stat;
    o.month = request.month;
    o.newPagesFolder = request.newPagesFolder;
    return o;
}

} // namespace

QJsonObject viewResultJson(const views::ViewResult &view)
{
    QJsonArray columns;
    for (const views::Column &c : view.columns) {
        columns.append(QJsonObject{{u"id"_s, c.id},
                                   {u"label"_s, c.label},
                                   {u"kind"_s, columnKindName(c.kind)},
                                   {u"property"_s, c.property},
                                   {u"type"_s, c.type},
                                   {u"editable"_s, c.editable},
                                   {u"summary"_s, c.summary},
                                   {u"summaryLabel"_s, c.summaryLabel},
                                   {u"summaryText"_s, c.summaryText},
                                   {u"sortKey"_s, c.sortKey}});
    }
    QJsonArray rows;
    for (const views::Row &r : view.rows) {
        QJsonArray cells;
        for (const views::Cell &cell : r.cells) {
            cells.append(QJsonObject{{u"markdown"_s, cell.markdown},
                                     {u"text"_s, cell.text},
                                     {u"value"_s, valueJson(cell.value)},
                                     {u"empty"_s, cell.empty},
                                     {u"error"_s, cell.error},
                                     {u"target"_s, cell.target}});
        }
        QJsonObject row{
                {u"path"_s, r.path},           {u"blockIndex"_s, r.blockIndex},
                {u"line"_s, r.line},           {u"ownEndLine"_s, r.ownEndLine},
                {u"title"_s, r.title},         {u"cover"_s, r.cover},
                {u"coverKind"_s, r.coverKind}, {u"date"_s, r.date.isValid() ? r.date.toString(Qt::ISODate) : QString()},
                {u"group"_s, r.group},         {u"cells"_s, cells}};
        if (r.task)
            row.insert(u"task"_s, taskJson(*r.task));
        rows.append(row);
    }
    QJsonArray groups;
    for (const views::Group &g : view.groups) {
        QJsonArray members;
        for (const int i : g.rows)
            members.append(i);
        groups.append(QJsonObject{{u"key"_s, g.key},
                                  {u"label"_s, g.label},
                                  {u"value"_s, g.value},
                                  {u"rows"_s, members},
                                  {u"summaryTexts"_s, QJsonArray::fromStringList(g.summaryTexts)},
                                  {u"droppable"_s, g.droppable}});
    }
    QJsonArray properties;
    for (const auto &[key, value] : view.newRow.properties)
        properties.append(QJsonObject{{u"key"_s, key}, {u"value"_s, value}});
    return QJsonObject{
            {u"kind"_s, views::viewKindName(view.kind)},
            {u"type"_s, queryTypeName(view.type)},
            {u"total"_s, view.total},
            {u"limited"_s, view.limited},
            {u"notice"_s, view.notice},
            {u"groupKind"_s, view.groupKind},
            {u"groupProperty"_s, view.groupProperty},
            {u"groupColumn"_s, view.groupColumn},
            {u"dateColumn"_s, view.dateColumn},
            {u"month"_s, view.month.isValid() ? view.month.toString(Qt::ISODate) : QString()},
            {u"columns"_s, columns},
            {u"rows"_s, rows},
            {u"groups"_s, groups},
            {u"newRow"_s, QJsonObject{{u"possible"_s, view.newRow.possible},
                                      {u"task"_s, view.newRow.task},
                                      {u"folder"_s, view.newRow.folder},
                                      {u"tags"_s, QJsonArray::fromStringList(view.newRow.tags)},
                                      {u"properties"_s, properties},
                                      {u"taskPage"_s, view.newRow.taskPage}}},
    };
}

QJsonObject runView(const VaultIndex &index, const QString &language, const QString &source, const ViewRequest &request,
                    QueryCache *cache)
{
    const views::ViewOptions options = optionsOf(request);
    if (language == u"base") {
        const auto parsed = views::parseBase(source);
        if (const QString *error = std::get_if<QString>(&parsed))
            return failed(language, *error);
        const views::Base &base = std::get<views::Base>(parsed);
        const int view = request.viewName.isEmpty() ? request.view : views::baseViewNamed(base, request.viewName);
        const auto result = views::runBase(base, view, index, options);
        if (const QString *error = std::get_if<QString>(&result))
            return failed(language, *error);
        QJsonObject out = viewResultJson(std::get<views::ViewResult>(result));
        QJsonArray viewsJson;
        for (const views::BaseView &v : base.views)
            viewsJson.append(
                    QJsonObject{{u"name"_s, v.name}, {u"type"_s, v.type}, {u"kind"_s, views::viewKindName(v.kind)}});
        out.insert(u"views"_s, viewsJson);
        out.insert(u"view"_s, view >= 0 && view < base.views.size() ? view : 0);
        out.insert(u"problems"_s, QJsonArray::fromStringList(base.problems));
        out.insert(u"ok"_s, true);
        out.insert(u"error"_s, QString());
        out.insert(u"language"_s, language);
        return out;
    }
    const std::variant<Query, QString> parsed = parseQuery(source);
    if (const QString *error = std::get_if<QString>(&parsed))
        return failed(u"query"_s, *error);
    const Query &query = std::get<Query>(parsed);
    QueryOptions qo;
    qo.currentPath = request.currentPath;
    qo.now = request.now.time();
    // A calendar keeps the rows of its month: the query runs without the default limit.
    qo.defaultLimit = query.view == QueryView::Calendar ? MaxQueryLimit : options.defaultLimit;
    const QueryResult result = cache ? cache->run(query, index, options.today, request.weekStart, qo)
                                     : runQuery(query, index, options.today, request.weekStart, qo);
    const auto built = views::buildQueryView(query, result, index, options);
    if (const QString *error = std::get_if<QString>(&built))
        return failed(u"query"_s, *error);
    QJsonObject out = viewResultJson(std::get<views::ViewResult>(built));
    out.insert(u"ok"_s, true);
    out.insert(u"error"_s, QString());
    out.insert(u"language"_s, u"query"_s);
    out.insert(u"columnNames"_s,
               QJsonArray::fromStringList(views::columnNames(query, std::get<views::ViewResult>(built))));
    return out;
}

views::ViewResult viewResultFromJson(const QJsonObject &json)
{
    using K = views::ColumnKind;
    static const QHash<QString, K> kinds{
            {u"title"_s, K::Title},       {u"path"_s, K::Path},       {u"folder"_s, K::Folder},
            {u"tags"_s, K::Tags},         {u"mtime"_s, K::Modified},  {u"size"_s, K::Size},
            {u"date"_s, K::Date},         {u"text"_s, K::Text},       {u"status"_s, K::Status},
            {u"priority"_s, K::Priority}, {u"due"_s, K::Due},         {u"scheduled"_s, K::Scheduled},
            {u"start"_s, K::Start},       {u"done"_s, K::Done},       {u"created"_s, K::Created},
            {u"property"_s, K::Property}, {u"formula"_s, K::Formula},
    };
    static const QHash<QString, views::ViewKind> viewKinds{
            {u"list"_s, views::ViewKind::List},         {u"table"_s, views::ViewKind::Table},
            {u"board"_s, views::ViewKind::Board},       {u"gallery"_s, views::ViewKind::Gallery},
            {u"calendar"_s, views::ViewKind::Calendar},
    };
    const auto variantOf = [](const QJsonValue &v) -> QVariant {
        switch (v.type()) {
        case QJsonValue::Bool: return v.toBool();
        case QJsonValue::Double: return v.toDouble();
        case QJsonValue::String: return v.toString();
        case QJsonValue::Array: {
            QStringList items;
            for (const QJsonValue &item : v.toArray())
                items.append(item.toString());
            return items;
        }
        default: return {};
        }
    };
    views::ViewResult out;
    out.kind = viewKinds.value(json.value(u"kind"_s).toString(), views::ViewKind::Table);
    out.total = json.value(u"total"_s).toInt();
    out.limited = json.value(u"limited"_s).toBool();
    out.notice = json.value(u"notice"_s).toString();
    out.groupKind = json.value(u"groupKind"_s).toString();
    out.groupProperty = json.value(u"groupProperty"_s).toString();
    out.groupColumn = json.value(u"groupColumn"_s).toInt(-1);
    out.dateColumn = json.value(u"dateColumn"_s).toInt(-1);
    out.month = QDate::fromString(json.value(u"month"_s).toString(), Qt::ISODate);
    for (const QJsonValue &c : json.value(u"columns"_s).toArray()) {
        const QJsonObject o = c.toObject();
        views::Column col;
        col.id = o.value(u"id"_s).toString();
        col.label = o.value(u"label"_s).toString();
        col.kind = kinds.value(o.value(u"kind"_s).toString(), K::Property);
        col.property = o.value(u"property"_s).toString();
        col.type = o.value(u"type"_s).toString();
        col.editable = o.value(u"editable"_s).toBool();
        col.summary = o.value(u"summary"_s).toString();
        col.summaryLabel = o.value(u"summaryLabel"_s).toString();
        col.summaryText = o.value(u"summaryText"_s).toString();
        col.sortKey = o.value(u"sortKey"_s).toString();
        out.columns.append(col);
    }
    for (const QJsonValue &r : json.value(u"rows"_s).toArray()) {
        const QJsonObject o = r.toObject();
        views::Row row;
        row.path = o.value(u"path"_s).toString();
        row.blockIndex = o.value(u"blockIndex"_s).toInt(-1);
        row.line = o.value(u"line"_s).toInt();
        row.ownEndLine = o.value(u"ownEndLine"_s).toInt();
        row.title = o.value(u"title"_s).toString();
        row.cover = o.value(u"cover"_s).toString();
        row.coverKind = o.value(u"coverKind"_s).toString();
        row.date = QDate::fromString(o.value(u"date"_s).toString(), Qt::ISODate);
        row.group = o.value(u"group"_s).toInt(-1);
        for (const QJsonValue &cellValue : o.value(u"cells"_s).toArray()) {
            const QJsonObject c = cellValue.toObject();
            views::Cell cell;
            cell.markdown = c.value(u"markdown"_s).toString();
            cell.text = c.value(u"text"_s).toString();
            cell.value = variantOf(c.value(u"value"_s));
            cell.empty = c.value(u"empty"_s).toBool();
            cell.error = c.value(u"error"_s).toBool();
            cell.target = c.value(u"target"_s).toString();
            row.cells.append(cell);
        }
        out.rows.append(row);
    }
    for (const QJsonValue &g : json.value(u"groups"_s).toArray()) {
        const QJsonObject o = g.toObject();
        views::Group group;
        group.key = o.value(u"key"_s).toString();
        group.label = o.value(u"label"_s).toString();
        group.value = o.value(u"value"_s).toString();
        for (const QJsonValue &m : o.value(u"rows"_s).toArray())
            group.rows.append(m.toInt());
        for (const QJsonValue &t : o.value(u"summaryTexts"_s).toArray())
            group.summaryTexts.append(t.toString());
        group.droppable = o.value(u"droppable"_s).toBool();
        out.groups.append(group);
    }
    const QJsonObject newRow = json.value(u"newRow"_s).toObject();
    out.newRow.possible = newRow.value(u"possible"_s).toBool();
    out.newRow.task = newRow.value(u"task"_s).toBool();
    out.newRow.folder = newRow.value(u"folder"_s).toString();
    for (const QJsonValue &t : newRow.value(u"tags"_s).toArray())
        out.newRow.tags.append(t.toString());
    for (const QJsonValue &p : newRow.value(u"properties"_s).toArray())
        out.newRow.properties.append(
                {p.toObject().value(u"key"_s).toString(), p.toObject().value(u"value"_s).toString()});
    out.newRow.taskPage = newRow.value(u"taskPage"_s).toString();
    return out;
}

QString viewText(const QJsonObject &view)
{
    const QJsonArray columns = view.value(u"columns"_s).toArray();
    const QJsonArray rows = view.value(u"rows"_s).toArray();
    const QJsonArray groups = view.value(u"groups"_s).toArray();
    QList<int> widths;
    for (const QJsonValue &c : columns)
        widths.append(int(c.toObject().value(u"label"_s).toString().size()));
    const auto cellText = [](const QJsonValue &cell) {
        QString t = cell.toObject().value(u"text"_s).toString();
        t.replace(u'\n', u' ');
        return t.size() > 40 ? t.left(39) + u'…' : t;
    };
    for (const QJsonValue &r : rows) {
        const QJsonArray cells = r.toObject().value(u"cells"_s).toArray();
        for (qsizetype i = 0; i < cells.size() && i < widths.size(); ++i)
            widths[i] = std::max(widths[i], int(cellText(cells.at(i)).size()));
    }
    const auto line = [&](const QStringList &parts) {
        QString out;
        for (qsizetype i = 0; i < parts.size(); ++i)
            out += i + 1 < parts.size() ? parts.at(i).leftJustified(widths.value(i)) + u"  "_s : parts.at(i);
        return out.trimmed() + u'\n';
    };
    QString out;
    QStringList header;
    for (const QJsonValue &c : columns)
        header.append(c.toObject().value(u"label"_s).toString());
    out += line(header);
    const auto rowLine = [&](const QJsonValue &r) {
        QStringList parts;
        for (const QJsonValue &cell : r.toObject().value(u"cells"_s).toArray())
            parts.append(cellText(cell));
        return line(parts);
    };
    if (groups.isEmpty()) {
        for (const QJsonValue &r : rows)
            out += rowLine(r);
    } else {
        for (const QJsonValue &g : groups) {
            const QJsonObject group = g.toObject();
            const QJsonArray members = group.value(u"rows"_s).toArray();
            out += u"\n## "_s + group.value(u"label"_s).toString() + u" (" + QString::number(members.size()) + u")\n";
            for (const QJsonValue &m : members)
                out += rowLine(rows.at(m.toInt()));
        }
    }
    QStringList summaries;
    for (const QJsonValue &c : columns) {
        const QJsonObject col = c.toObject();
        if (!col.value(u"summaryText"_s).toString().isEmpty())
            summaries.append(col.value(u"label"_s).toString() + u" — "_s + col.value(u"summaryLabel"_s).toString()
                             + u": "_s + col.value(u"summaryText"_s).toString());
    }
    if (!summaries.isEmpty())
        out += u'\n' + summaries.join(u'\n') + u'\n';
    if (const QString notice = view.value(u"notice"_s).toString(); !notice.isEmpty())
        out += u'\n' + notice + u'\n';
    return out;
}

} // namespace brulion::notebook
