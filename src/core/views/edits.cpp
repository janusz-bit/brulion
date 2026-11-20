// Edits of database views (edits.h).
#include "views/edits.h"
#include "views/cells_p.h"
#include "views/messages_p.h"

#include "markdown/blockprops.h"
#include "markdown/edit.h"
#include "markdown/pageprops.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "markdown/tokenedit.h"
#include "markdown/tokens.h"
#include "text/text.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::views {

namespace {

using markdown::PropertySource;
using markdown::PropertyType;

bool isEmptyValue(const QVariant &v)
{
    if (!v.isValid() || v.isNull())
        return true;
    if (v.typeId() == QMetaType::QString)
        return v.toString().trimmed().isEmpty();
    if (v.typeId() == QMetaType::QStringList)
        return v.toStringList().isEmpty();
    if (v.typeId() == QMetaType::QVariantList)
        return v.toList().isEmpty();
    return false;
}

QStringList listOf(const QVariant &v)
{
    QStringList out;
    if (v.typeId() == QMetaType::QStringList || v.typeId() == QMetaType::QVariantList) {
        for (const QVariant &item : v.toList()) {
            if (const QString s = item.toString().trimmed(); !s.isEmpty())
                out.append(s);
        }
        return out;
    }
    for (const QString &part : markdown::detail::splitTopLevelCommas(v.toString())) {
        if (const QString s = part.trimmed(); !s.isEmpty())
            out.append(s);
    }
    return out;
}

/// A number written plainly ("12.5", "1200"): no exponent, no trailing zeros.
QString numberText(double n)
{
    if (std::abs(n - std::round(n)) < 1e-9 && std::abs(n) < 1e15)
        return QString::number(qint64(std::llround(n)));
    QString s = QString::number(n, 'f', 10);
    while (s.endsWith(u'0'))
        s.chop(1);
    if (s.endsWith(u'.'))
        s.chop(1);
    return s;
}

/// A value as written on a `key:: value` line (one line: line breaks become blanks).
std::optional<QString> blockLineValue(const QVariant &value, const QString &type)
{
    if (isEmptyValue(value))
        return QString();
    if (type == u"checkbox") {
        if (value.typeId() == QMetaType::Bool)
            return value.toBool() ? u"true"_s : u"false"_s;
        const QString t = value.toString().trimmed().toLower();
        if (t == u"true" || t == u"false")
            return t;
        return std::nullopt;
    }
    if (type == u"number") {
        if (value.typeId() == QMetaType::Double || value.typeId() == QMetaType::LongLong
            || value.typeId() == QMetaType::Int)
            return numberText(value.toDouble());
        const auto n = markdown::parseNumberInput(value.toString());
        if (!n)
            return std::nullopt;
        return numberText(n->toDouble());
    }
    if (type == u"date") {
        const QString t = value.toString().trimmed();
        if (!QDate::fromString(t.left(10), Qt::ISODate).isValid())
            return std::nullopt;
        return t.left(10);
    }
    if (type == u"list" || type == u"tags" || type == u"aliases" || value.typeId() == QMetaType::QStringList) {
        QStringList items;
        for (const QString &item : listOf(value)) {
            if (type == u"tags")
                items.append(item.startsWith(u'#') ? item : u'#' + item);
            else if (item.startsWith(u"[[") || item.startsWith(u'#'))
                items.append(item);
            else
                items.append(u"[["_s + item + u"]]"_s);
        }
        return items.join(u", "_s);
    }
    QString t = value.toString();
    t.replace(u'\r', u' ');
    t.replace(u'\n', u' ');
    return t.trimmed();
}

/// A cell editor's value in the form markdown::setPageProperty() takes for `type`.
QVariant pageValue(const QVariant &value, PropertyType type)
{
    switch (type) {
    case PropertyType::List:
    case PropertyType::Tags:
    case PropertyType::Aliases: return listOf(value);
    case PropertyType::Checkbox:
        if (value.typeId() == QMetaType::Bool)
            return value;
        return value.toString().trimmed().toLower() == u"true";
    default:
        if (value.typeId() == QMetaType::Double && type == PropertyType::Text)
            return numberText(value.toDouble());
        return value;
    }
}

/// The type a value written into a new property reads as.
PropertyType typeOfValue(const QVariant &value)
{
    switch (value.typeId()) {
    case QMetaType::Bool: return PropertyType::Checkbox;
    case QMetaType::Double:
    case QMetaType::Int:
    case QMetaType::LongLong: return PropertyType::Number;
    case QMetaType::QStringList:
    case QMetaType::QVariantList: return PropertyType::List;
    default: break;
    }
    const QString t = value.toString().trimmed();
    if (t == u"true" || t == u"false")
        return PropertyType::Checkbox;
    if (markdown::parseNumberInput(t) && !t.contains(u','))
        return PropertyType::Number;
    if (t.size() == 10 && QDate::fromString(t, Qt::ISODate).isValid())
        return PropertyType::Date;
    if (t.startsWith(u"[[") && t.endsWith(u"]]"))
        return PropertyType::Text; // (YAML: a quoted link, as Obsidian writes one)
    return PropertyType::Text;
}

EditOutcome refused(const QString &content, const QString &error)
{
    return EditOutcome{content, false, error};
}

EditOutcome outcome(const QString &content, const QString &edited)
{
    return EditOutcome{edited, edited != content, QString()};
}

EditOutcome pagePropertyEdit(const QString &content, const RowEdit &edit)
{
    const markdown::PageProperties props = markdown::readPageProperties(content);
    const QString folded = queryPropertyKey(edit.key);
    const markdown::PageProperty *existing = nullptr;
    for (const markdown::PageProperty &p : props.items) {
        if (queryPropertyKey(p.key) == folded) {
            existing = &p;
            break;
        }
    }
    if (isEmptyValue(edit.value)) {
        if (!existing)
            return outcome(content, content);
        const markdown::PropertyEditResult r = markdown::removePageProperty(content, existing->source, existing->key);
        if (r.status == markdown::PropertyEditStatus::Unsafe)
            return refused(content, messages::yamlNotEditable());
        return outcome(content, r.content);
    }
    if (existing && !existing->editable)
        return refused(content, messages::notEditable());
    PropertyType type = PropertyType::Text;
    if (const auto t = markdown::parsePropertyType(edit.type); t && *t != PropertyType::Unsupported)
        type = *t;
    else if (existing && !existing->empty && existing->type != PropertyType::Unsupported)
        type = existing->type;
    else
        type = typeOfValue(edit.value);
    if (const auto fixed = markdown::fixedPropertyType(existing ? existing->key : edit.key))
        type = *fixed;
    const PropertySource source = existing ? existing->source : props.addTarget();
    const QString key = existing ? existing->key : edit.key.trimmed();
    if (!existing && !markdown::checkPropertyKey(key, source).isEmpty())
        return refused(content, source == PropertySource::Logseq ? messages::blockKeyNotAscii(key)
                                                                 : messages::notEditable());
    const markdown::PropertyEditResult r =
            markdown::setPageProperty(content, source, key, pageValue(edit.value, type), type);
    switch (r.status) {
    case markdown::PropertyEditStatus::Changed:
    case markdown::PropertyEditStatus::Unchanged: return outcome(content, r.content);
    case markdown::PropertyEditStatus::InvalidValue: return refused(content, messages::invalidValue(edit.type));
    case markdown::PropertyEditStatus::Unsafe: return refused(content, messages::yamlNotEditable());
    default: return refused(content, messages::notEditable());
    }
}

EditOutcome blockPropertyEdit(const QString &content, const RowEdit &edit)
{
    if (!markdown::isBlockPropertyKey(edit.key))
        return refused(content, messages::blockKeyNotAscii(edit.key));
    QString type = edit.type;
    if (type.isEmpty())
        type = markdown::propertyTypeName(typeOfValue(edit.value));
    const std::optional<QString> value = blockLineValue(edit.value, type);
    if (!value)
        return refused(content, messages::invalidValue(type));
    const std::optional<QString> current =
            markdown::blockPropertyText(content, edit.line, edit.ownEndLine, edit.key);
    if (current && *current == *value)
        return outcome(content, content);
    if (!current && value->isEmpty())
        return outcome(content, content);
    const std::optional<QString> edited =
            markdown::setBlockProperty(content, edit.line, edit.ownEndLine, edit.key, *value);
    if (!edited)
        return refused(content, messages::notEditable());
    return outcome(content, *edited);
}

std::optional<markdown::TokenKind> dateKind(const QString &key)
{
    if (key == u"due")
        return markdown::TokenKind::Due;
    if (key == u"scheduled")
        return markdown::TokenKind::Scheduled;
    if (key == u"start")
        return markdown::TokenKind::Start;
    return std::nullopt;
}

QString dateEmoji(markdown::TokenKind kind)
{
    switch (kind) {
    case markdown::TokenKind::Scheduled: return u"⏳"_s;
    case markdown::TokenKind::Start: return u"🛫"_s;
    default: return u"📅"_s;
    }
}

/// "YYYY-MM-DD[ HH:mm]" checked, or nullopt.
std::optional<QString> dateValue(const QVariant &v)
{
    const QString t = v.toString().trimmed();
    if (t.size() < 10 || !QDate::fromString(t.left(10), Qt::ISODate).isValid())
        return std::nullopt;
    if (t.size() == 10)
        return t;
    const QString time = t.sliced(11).left(5);
    if (!QTime::fromString(time, u"HH:mm"_s).isValid())
        return t.left(10);
    return t.left(10) + u' ' + time;
}

/// A property value as written in a note from a NewRow / group value: a number, a checkbox, a
/// date stay what they read as; a text stays text.
QVariant writtenValue(const QString &value)
{
    const QString t = value.trimmed();
    if (t == u"true" || t == u"false")
        return t == u"true";
    if (const auto n = markdown::parseNumberInput(t); n && !t.contains(u','))
        return n->toDouble();
    return t;
}

} // namespace

EditOutcome applyRowEdit(const QString &content, const RowEdit &edit, QDate today)
{
    switch (edit.field) {
    case EditField::Property:
        return edit.blockRow ? blockPropertyEdit(content, edit) : pagePropertyEdit(content, edit);
    case EditField::Status: {
        const QString name = edit.value.toString().trimmed();
        if (name.isEmpty())
            return outcome(content, markdown::removeTaskMarker(content, edit.line));
        const std::optional<TaskStatus> status = detail::statusNamed(name);
        if (!status)
            return refused(content, messages::invalidValue(u"status"_s));
        return outcome(content, markdown::setTaskStatus(content, edit.line, *status, today));
    }
    case EditField::Priority: {
        const QString name = edit.value.toString().trimmed();
        const Priority p = name.isEmpty() ? Priority::None : markdown::priorityFromName(name);
        if (!name.isEmpty() && p == Priority::None && name != u"none")
            return refused(content, messages::invalidValue(u"priority"_s));
        const QString token = p == Priority::None ? QString() : markdown::tokens::priorityEmoji(p).toString();
        return outcome(content, markdown::setBlockToken(content, edit.line, markdown::TokenKind::Priority, token));
    }
    case EditField::TaskDate: {
        const std::optional<markdown::TokenKind> kind = dateKind(edit.key);
        if (!kind)
            return refused(content, messages::notEditable());
        QString token;
        if (!isEmptyValue(edit.value)) {
            const std::optional<QString> d = dateValue(edit.value);
            if (!d)
                return refused(content, messages::invalidValue(u"date"_s));
            token = dateEmoji(*kind) + u' ' + *d;
        }
        return outcome(content, markdown::setBlockToken(content, edit.line, *kind, token));
    }
    }
    return outcome(content, content);
}

std::optional<RowEdit> moveEdit(const ViewResult &view, const Row &row, int from, int to)
{
    if (to < 0 || to >= view.groups.size() || !view.groups.at(to).droppable || view.groupKind.isEmpty())
        return std::nullopt;
    if (from == to)
        return std::nullopt;
    const Group &target = view.groups.at(to);
    RowEdit e;
    e.blockRow = row.blockIndex >= 0;
    e.line = row.line;
    e.ownEndLine = row.ownEndLine;
    e.value = target.value;
    if (view.groupKind == u"status") {
        e.field = EditField::Status;
    } else if (view.groupKind == u"priority") {
        e.field = EditField::Priority;
        if (target.value == u"none")
            e.value = QString();
    } else if (view.groupKind == u"taskdate") {
        e.field = EditField::TaskDate;
        e.key = view.groupProperty;
    } else if (view.groupKind == u"property" || view.groupKind == u"date") {
        e.field = EditField::Property;
        e.key = view.groupProperty;
        const Column *col = view.groupColumn >= 0 ? &view.columns.at(view.groupColumn) : nullptr;
        e.type = col ? col->type : QString();
        const bool list = e.type == u"list" || e.type == u"tags" || e.type == u"aliases";
        if (list && col) {
            // A list: the old group's value swapped for the new one, its other items kept.
            QStringList items = view.groupColumn < row.cells.size() ? listOf(row.cells.at(view.groupColumn).value)
                                                                    : QStringList();
            const QString old = from >= 0 && from < view.groups.size() ? view.groups.at(from).value : QString();
            const auto same = [](const QString &a, const QString &b) {
                const auto bare = [](QString s) {
                    s = s.trimmed();
                    if (s.startsWith(u"[[") && s.endsWith(u"]]"))
                        s = s.sliced(2, s.size() - 4);
                    if (s.startsWith(u'#'))
                        s.remove(0, 1);
                    return text::foldKey(s);
                };
                return bare(a) == bare(b);
            };
            if (!old.isEmpty())
                items.removeIf([&](const QString &i) { return same(i, old); });
            if (!target.value.isEmpty() && std::ranges::none_of(items, [&](const QString &i) { return same(i, target.value); }))
                items.append(target.value);
            e.value = items;
        } else {
            e.value = target.value.isEmpty() ? QVariant() : writtenValue(target.value);
            if (e.value.typeId() == QMetaType::Double && e.type == u"text")
                e.value = target.value;
        }
    } else {
        return std::nullopt;
    }
    return e;
}

std::optional<RowEdit> dateEdit(const ViewResult &view, const Row &row, QDate day)
{
    if (view.kind != ViewKind::Calendar || !day.isValid() || view.groupKind.isEmpty())
        return std::nullopt;
    RowEdit e;
    e.blockRow = row.blockIndex >= 0;
    e.line = row.line;
    e.ownEndLine = row.ownEndLine;
    QString time;
    if (view.dateColumn >= 0 && view.dateColumn < row.cells.size()) {
        const QString current = row.cells.at(view.dateColumn).value.toString();
        if (current.size() > 10)
            time = current.sliced(10); // " 09:30" / "T09:30"
    }
    e.value = QString(day.toString(Qt::ISODate) + time);
    if (view.groupKind == u"taskdate") {
        e.field = EditField::TaskDate;
        e.key = view.groupProperty;
        return e;
    }
    e.field = EditField::Property;
    e.key = view.groupProperty;
    e.type = time.isEmpty() ? u"date"_s : u"datetime"_s;
    if (time.startsWith(u'T') || time.startsWith(u' '))
        e.value = QString(day.toString(Qt::ISODate) + u' ' + time.sliced(1));
    return e;
}

QString newPageContent(const ViewResult &view, int group, QDate day)
{
    if (!view.newRow.possible || view.newRow.task)
        return {};
    QString content;
    const auto set = [&](const QString &key, const QVariant &value, PropertyType type) {
        const markdown::PropertyEditResult r = markdown::setPageProperty(content, PropertySource::Yaml, key, value, type);
        if (r.status == markdown::PropertyEditStatus::Changed)
            content = r.content;
    };
    for (const auto &[key, value] : view.newRow.properties) {
        const QVariant v = writtenValue(value);
        set(key, v, typeOfValue(v));
    }
    if (group >= 0 && group < view.groups.size() && view.groupKind == u"property" && !view.groups.at(group).value.isEmpty()) {
        const Column *col = view.groupColumn >= 0 ? &view.columns.at(view.groupColumn) : nullptr;
        const QString type = col ? col->type : QString();
        const QString value = view.groups.at(group).value;
        if (type == u"list" || type == u"tags" || type == u"aliases")
            set(view.groupProperty, QStringList{value}, markdown::parsePropertyType(type).value_or(PropertyType::List));
        else
            set(view.groupProperty, writtenValue(value), typeOfValue(writtenValue(value)));
    }
    if (day.isValid() && view.groupKind == u"date" && !view.groupProperty.isEmpty())
        set(view.groupProperty, day.toString(Qt::ISODate), PropertyType::Date);
    for (const QString &tag : view.newRow.tags) {
        const markdown::PropertyEditResult r = markdown::addPageListItem(content, PropertyType::Tags, tag);
        if (r.status == markdown::PropertyEditStatus::Changed)
            content = r.content;
    }
    return content;
}

QString newTaskLines(const ViewResult &view, const QString &text, int group, QDate day, const QString &eol)
{
    TaskStatus status = TaskStatus::Todo;
    QString priority;
    QString due;
    QList<std::pair<QString, QString>> properties = view.newRow.properties;
    if (group >= 0 && group < view.groups.size()) {
        const Group &g = view.groups.at(group);
        if (view.groupKind == u"status")
            status = detail::statusNamed(g.value).value_or(TaskStatus::Todo);
        else if (view.groupKind == u"priority" && g.value != u"none")
            priority = markdown::tokens::priorityEmoji(markdown::priorityFromName(g.value)).toString();
        else if (view.groupKind == u"taskdate" && view.groupProperty == u"due")
            due = g.value;
        else if (view.groupKind == u"property" && !g.value.isEmpty())
            properties.append({view.groupProperty, g.value});
    }
    if (day.isValid() && view.groupKind == u"taskdate")
        due = day.toString(Qt::ISODate);
    QString line = u"- ["_s + markdown::statusChar(status) + u"] "_s + text.simplified();
    for (const QString &tag : view.newRow.tags)
        line += tag.contains(u' ') ? QString(u" #[["_s + tag + u"]]"_s) : QString(u" #"_s + tag);
    if (!priority.isEmpty())
        line += u' ' + priority;
    if (!due.isEmpty())
        line += u" 📅 "_s + due;
    QString out = line + eol;
    for (const auto &[key, value] : std::as_const(properties)) {
        if (markdown::isBlockPropertyKey(key))
            out += u"  "_s + key + u":: "_s + value + eol;
    }
    return out;
}

QString setQueryOption(const QString &source, const QString &key, const QString &value)
{
    const QList<QueryKeyToken> tokens = queryKeyTokens(source);
    QList<const QueryKeyToken *> matching;
    for (const QueryKeyToken &t : tokens) {
        if (t.key == key && !t.negated)
            matching.append(&t);
    }
    QString out = source;
    // From the last token back: earlier offsets stay valid.
    for (qsizetype i = matching.size() - 1; i >= 0; --i) {
        const QueryKeyToken &t = *matching.at(i);
        if (i == 0 && !value.isEmpty()) {
            out.replace(t.valueFrom, t.to - t.valueFrom, value);
            continue;
        }
        // The token and one blank before it (or after it, at a line's start) go.
        qsizetype from = t.from;
        qsizetype to = t.to;
        if (from > 0 && (out[from - 1] == u' ' || out[from - 1] == u'\t'))
            --from;
        else if (to < out.size() && (out[to] == u' ' || out[to] == u'\t'))
            ++to;
        out.remove(from, to - from);
    }
    if (matching.isEmpty() && !value.isEmpty()) {
        const QString eol = source.contains(u"\r\n") ? u"\r\n"_s : u"\n"_s;
        const QString option = key + u':' + value;
        if (out.trimmed().isEmpty())
            return option;
        // A line of its own at the end, before the final line break.
        const bool endsWithBreak = out.endsWith(u'\n');
        QString body = out;
        QString tail;
        if (endsWithBreak) {
            const qsizetype cut = body.endsWith(u"\r\n") ? 2 : 1;
            tail = body.right(cut);
            body.chop(cut);
        }
        out = body + eol + option + tail;
    }
    return out;
}

QString queryList(const QStringList &items)
{
    QStringList out;
    for (const QString &item : items) {
        const bool plain = !item.isEmpty() && std::ranges::none_of(item, [](QChar c) {
            return c.isSpace() || c == u',' || c == u'"' || c == u'„' || c == u'“' || c == u'”' || c == u'«';
        });
        if (plain && !item.startsWith(u"//"))
            out.append(item);
        else
            out.append(u'"' + QString(item).replace(u'"', u'\'') + u'"');
    }
    return out.join(u',');
}

QStringList columnNames(const Query &query, const ViewResult &view)
{
    const auto builtinName = [](QueryColumnKind kind) -> QString {
        switch (kind) {
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
        default: return {};
        }
    };
    QStringList out;
    if (!query.columns.isEmpty()) {
        for (const QueryColumn &c : query.columns) {
            if (c.kind == QueryColumnKind::Formula)
                out.append(c.label + u" = "_s + c.formula);
            else if (c.kind == QueryColumnKind::Property)
                out.append(c.label.isEmpty() ? c.name : c.label + u" = "_s + c.name);
            else
                out.append(c.label.isEmpty() ? builtinName(c.kind) : c.label + u" = "_s + builtinName(c.kind));
        }
        return out;
    }
    for (const Column &c : view.columns)
        out.append(c.kind == QueryColumnKind::Property ? c.property : builtinName(c.kind));
    return out;
}

} // namespace brulion::views
