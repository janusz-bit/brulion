// Running a Dataview query: the pages of FROM, the rows, the data commands one after another,
// and the result as Markdown cells.
#include "dataview/eval_p.h"
#include "dataview/messages_p.h"

#include "util/paths.h"

#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dataview {

namespace {

using PathSet = QSet<VaultPath>;

bool hasTag(const ParsedNote &note, const QString &tag)
{
    const QString prefix = tag + u'/';
    return std::any_of(note.tags.cbegin(), note.tags.cend(),
                       [&](const QString &t) { return t == tag || t.startsWith(prefix); });
}

PathSet allPages(const VaultIndex &index)
{
    const QList<VaultPath> paths = index.paths();
    return PathSet(paths.cbegin(), paths.cend());
}

PathSet select(const Source &s, Context &ctx)
{
    const VaultIndex &index = ctx.index();
    PathSet out;
    switch (s.kind) {
    case Source::Kind::Tag:
        index.forEachNote([&](const VaultPath &path, const ParsedNote &note) {
            if (hasTag(note, s.value))
                out.insert(path);
        });
        return out;
    case Source::Kind::Folder: {
        if (s.value.isEmpty())
            return allPages(index);
        // A file ("Projekty/Dom" or "Projekty/Dom.md") or a folder (with the index's spelling).
        const QString file = paths::isMarkdown(s.value) ? s.value : s.value + u".md"_s;
        const QString folder = index.noteFolder(s.value).value_or(s.value) + u'/';
        index.forEachNote([&](const VaultPath &path, const ParsedNote &) {
            if (path.compare(file, Qt::CaseInsensitive) == 0 || path.startsWith(folder))
                out.insert(path);
        });
        return out;
    }
    case Source::Kind::Link:
    case Source::Kind::Current: {
        const VaultPath target = s.kind == Source::Kind::Current
                                         ? ctx.options().currentPath
                                         : index.resolveLink(s.value, ctx.options().currentPath).value_or(QString());
        if (target.isEmpty())
            return out;
        for (const Backlink &b : index.backlinks(target))
            out.insert(b.path);
        return out;
    }
    case Source::Kind::Outgoing: {
        const auto target = index.resolveLink(s.value, ctx.options().currentPath);
        if (!target)
            return out;
        for (const ResolvedLink &l : index.outgoingLinks(*target)) {
            if (!l.target.isEmpty() && index.note(l.target))
                out.insert(l.target);
        }
        return out;
    }
    case Source::Kind::And: return select(*s.a, ctx).intersect(select(*s.b, ctx));
    case Source::Kind::Or: return select(*s.a, ctx).unite(select(*s.b, ctx));
    case Source::Kind::Not: return allPages(index).subtract(select(*s.a, ctx));
    }
    return out;
}

void checkRows(qsizetype n)
{
    if (n > MaxRows)
        throw EvalError{messages::tooManyRows(MaxRows)};
}

QList<RowData> initialRows(const Query &q, Context &ctx)
{
    QList<VaultPath> pages;
    if (q.from) {
        const PathSet set = select(*q.from, ctx);
        pages = QList<VaultPath>(set.cbegin(), set.cend());
        std::ranges::sort(pages);
    } else {
        pages = ctx.index().paths();
    }
    QList<RowData> rows;
    for (const VaultPath &path : std::as_const(pages)) {
        ctx.step();
        if (q.type == QueryType::Task) {
            const ParsedNote *note = ctx.index().note(path);
            if (!note)
                continue;
            for (qsizetype i = 0; i < note->blocks.size(); ++i) {
                if (!note->blocks.at(i).task)
                    continue;
                RowData row;
                row.path = path;
                row.blockIndex = int(i);
                row.base = ctx.item(path, int(i));
                rows.append(row);
            }
        } else {
            RowData row;
            row.path = path;
            row.base = ctx.page(path);
            rows.append(row);
        }
        checkRows(rows.size());
    }
    return rows;
}

void apply(const Command &cmd, QList<RowData> &rows, Context &ctx)
{
    switch (cmd.kind) {
    case Command::Kind::Where: {
        QList<RowData> kept;
        for (const RowData &row : std::as_const(rows)) {
            if (evaluate(*cmd.expr, ctx, rowObject(row)).truthy())
                kept.append(row);
        }
        rows = kept;
        return;
    }
    case Command::Kind::Sort: {
        QList<std::pair<ValueList, qsizetype>> keyed;
        keyed.reserve(rows.size());
        for (qsizetype i = 0; i < rows.size(); ++i) {
            const Value obj = rowObject(rows.at(i));
            ValueList keys;
            for (const SortKey &k : cmd.sort)
                keys.append(evaluate(*k.expr, ctx, obj));
            keyed.append({keys, i});
        }
        ctx.step(rows.size());
        std::ranges::stable_sort(keyed, [&](const auto &a, const auto &b) {
            for (qsizetype k = 0; k < cmd.sort.size(); ++k) {
                const int c = compare(a.first.at(k), b.first.at(k));
                if (c != 0)
                    return cmd.sort.at(k).descending ? c > 0 : c < 0;
            }
            return false;
        });
        QList<RowData> sorted;
        sorted.reserve(rows.size());
        for (const auto &k : std::as_const(keyed))
            sorted.append(rows.at(k.second));
        rows = sorted;
        return;
    }
    case Command::Kind::GroupBy: {
        QList<std::pair<Value, QList<RowData>>> groups;
        for (const RowData &row : std::as_const(rows)) {
            const Value key = evaluate(*cmd.expr, ctx, rowObject(row));
            auto it = std::ranges::find_if(groups, [&](const auto &g) { return equals(g.first, key); });
            ctx.step(groups.size() / 8 + 1);
            if (it == groups.end())
                groups.append({key, {row}});
            else
                it->second.append(row);
        }
        std::ranges::stable_sort(groups, [](const auto &a, const auto &b) { return compare(a.first, b.first) < 0; });
        QList<RowData> out;
        for (const auto &[key, members] : std::as_const(groups)) {
            RowData g;
            g.group = true;
            g.key = key;
            g.rows = members;
            g.path = members.isEmpty() ? QString() : members.first().path;
            ValueList objects;
            for (const RowData &m : members)
                objects.append(rowObject(m));
            QList<std::pair<QString, Value>> fields{{u"key"_s, key}, {u"rows"_s, Value::list(objects)}};
            const QString name = canonicalKey(cmd.name);
            if (name != u"key" && name != u"rows")
                fields.append({name, key});
            g.base = Value::map(fields);
            out.append(g);
        }
        rows = out;
        return;
    }
    case Command::Kind::Flatten: {
        QList<RowData> out;
        for (const RowData &row : std::as_const(rows)) {
            const Value v = evaluate(*cmd.expr, ctx, rowObject(row));
            if (!v.is(Value::Type::List)) {
                RowData r = row;
                r.extra.append({canonicalKey(cmd.name), v});
                out.append(r);
                continue;
            }
            for (const Value &item : v.asList()) {
                RowData r = row;
                r.extra.append({canonicalKey(cmd.name), item});
                out.append(r);
                checkRows(out.size());
            }
        }
        rows = out;
        return;
    }
    case Command::Kind::Limit:
        if (rows.size() > cmd.limit)
            rows = rows.mid(0, cmd.limit);
        return;
    }
}

/// A Markdown link to a row's page.
QString pageLink(const VaultPath &path, Context &ctx)
{
    return toMarkdown(Value::link(LinkValue{path, QString(), QString(), false, true}), ctx.index());
}

/// The first cell of a row: its page, or a group's key.
QString idCell(const RowData &row, Context &ctx)
{
    return row.group ? toMarkdown(row.key, ctx.index()) : pageLink(row.path, ctx);
}

Row taskRow(const RowData &row, Context &ctx)
{
    Row out;
    out.path = row.path;
    out.blockIndex = row.blockIndex;
    if (const ParsedNote *note = ctx.index().note(row.path)) {
        if (row.blockIndex >= 0 && row.blockIndex < note->blocks.size())
            out.task = note->blocks.at(row.blockIndex).task;
    }
    return out;
}

/// The group's own rows' tasks (nested groups give their members).
void taskRowsOf(const RowData &row, Context &ctx, QList<Row> &out)
{
    if (row.group) {
        for (const RowData &m : row.rows)
            taskRowsOf(m, ctx, out);
    } else if (row.blockIndex >= 0) {
        out.append(taskRow(row, ctx));
    }
}

Result build(const Query &q, const QList<RowData> &rows, Context &ctx)
{
    Result r;
    r.type = q.type;
    const bool grouped = !rows.isEmpty() && rows.first().group;
    QString groupName;
    for (const auto &cmd : q.commands) {
        if (cmd->kind == Command::Kind::GroupBy)
            groupName = cmd->name;
    }
    switch (q.type) {
    case QueryType::Table: {
        if (!q.withoutId)
            r.headers.append(grouped ? (groupName.isEmpty() ? messages::groupHeader() : groupName) : messages::fileHeader());
        for (const auto &col : q.columns)
            r.headers.append(col.second);
        for (const RowData &row : rows) {
            Row out;
            out.path = row.group ? QString() : row.path;
            if (!q.withoutId)
                out.cells.append(idCell(row, ctx));
            const Value obj = rowObject(row);
            for (const auto &col : q.columns)
                out.cells.append(toMarkdown(evaluate(*col.first, ctx, obj), ctx.index()));
            r.rows.append(out);
        }
        break;
    }
    case QueryType::List: {
        const auto text = [&](const RowData &row) {
            if (q.columns.isEmpty())
                return idCell(row, ctx);
            const QString value = toMarkdown(evaluate(*q.columns.first().first, ctx, rowObject(row)), ctx.index());
            return q.withoutId ? value : idCell(row, ctx) + u": "_s + value;
        };
        if (grouped && q.columns.isEmpty()) {
            for (const RowData &row : rows) {
                Group g;
                g.key = toPlainText(row.key);
                g.label = g.key;
                for (const RowData &m : row.rows) {
                    Row item;
                    item.path = m.path;
                    item.cells = {m.group ? toMarkdown(m.key, ctx.index()) : pageLink(m.path, ctx)};
                    g.rows.append(item);
                }
                r.groups.append(g);
            }
        } else {
            for (const RowData &row : rows) {
                Row item;
                item.path = row.group ? QString() : row.path;
                item.cells = {text(row)};
                r.rows.append(item);
            }
        }
        break;
    }
    case QueryType::Task: {
        if (grouped) {
            for (const RowData &row : rows) {
                Group g;
                g.key = toPlainText(row.key);
                g.label = g.key;
                taskRowsOf(row, ctx, g.rows);
                r.groups.append(g);
            }
        } else {
            // Grouped by page, the pages in the rows' order.
            for (const RowData &row : rows) {
                if (row.blockIndex < 0)
                    continue;
                if (r.groups.isEmpty() || r.groups.last().key != row.path) {
                    Group g;
                    g.key = row.path;
                    const ParsedNote *note = ctx.index().note(row.path);
                    g.label = note && !note->title.isEmpty() ? note->title : paths::pageTitle(row.path);
                    r.groups.append(g);
                }
                r.groups.last().rows.append(taskRow(row, ctx));
            }
        }
        break;
    }
    }
    return r;
}

/// Cut the result to `limit` rows (or items over all groups); total counts them all.
void cut(Result &r, int limit)
{
    if (r.groups.isEmpty()) {
        r.total = int(r.rows.size());
        if (r.rows.size() > limit)
            r.rows = r.rows.mid(0, limit);
        return;
    }
    int total = 0;
    for (const Group &g : std::as_const(r.groups))
        total += int(g.rows.size());
    r.total = total;
    int left = limit;
    QList<Group> kept;
    for (Group g : std::as_const(r.groups)) {
        if (left <= 0)
            break;
        if (g.rows.size() > left)
            g.rows = g.rows.mid(0, left);
        left -= int(g.rows.size());
        kept.append(g);
    }
    r.groups = kept;
}

QString sanitizedDisplay(QString s)
{
    s.replace(u"]]"_s, u"] ]"_s).replace(u'|', u'/').replace(u'\n', u' ').replace(u'\r', u' ');
    return s;
}

} // namespace

std::variant<Result, QString> runQuery(const Query &query, const VaultIndex &index, const Options &options)
{
    Options opts = options;
    if (!opts.now.isValid())
        opts.now = QDateTime::currentDateTime();
    Context ctx(index, opts);
    try {
        QList<RowData> rows = initialRows(query, ctx);
        bool limited = false;
        for (const auto &cmd : query.commands) {
            apply(*cmd, rows, ctx);
            limited = limited || cmd->kind == Command::Kind::Limit;
        }
        Result r = build(query, rows, ctx);
        cut(r, limited ? MaxRows : std::max(1, opts.defaultLimit));
        return r;
    } catch (const EvalError &e) {
        return e.message.isEmpty() ? messages::tooCostly() : e.message;
    }
}

QString toMarkdown(const Value &value, const VaultIndex &index)
{
    switch (value.type()) {
    case Value::Type::Null: return u"–"_s;
    case Value::Type::String: return QString(value.asString()).replace(u'\n', u' ').replace(u'\r', u' ');
    case Value::Type::Link: {
        const LinkValue &l = value.asLink();
        QString target = l.resolved ? index.linkTarget(l.path).value_or(paths::stripMarkdownExtension(l.path)) : l.path;
        target = sanitizedDisplay(target);
        QString display = l.display;
        if (!l.subpath.isEmpty()) {
            target += u'#' + sanitizedDisplay(l.subpath);
            if (display.isEmpty() && !l.subpath.startsWith(u'^'))
                display = paths::pageTitle(l.path) + u" > "_s + l.subpath;
        }
        if (display.isEmpty() && l.resolved && target != paths::pageTitle(l.path))
            display = paths::pageTitle(l.path);
        if (target.isEmpty())
            return sanitizedDisplay(display);
        return u"[["_s + target + (display.isEmpty() ? QString() : u'|' + sanitizedDisplay(display)) + u"]]"_s;
    }
    case Value::Type::List: {
        QStringList parts;
        for (const Value &v : value.asList())
            parts.append(toMarkdown(v, index));
        return parts.join(u", "_s);
    }
    case Value::Type::Object: {
        QStringList parts;
        const Object &o = value.asObject();
        for (const QString &k : o.keys())
            parts.append(k + u": "_s + toMarkdown(o.field(k).value_or(Value()), index));
        return parts.join(u", "_s);
    }
    default: {
        Options none;
        Context ctx(index, none);
        return toText(value, ctx);
    }
    }
}

QString toPlainText(const Value &value)
{
    switch (value.type()) {
    case Value::Type::Null: return u"–"_s;
    case Value::Type::Boolean: return value.asBool() ? u"true"_s : u"false"_s;
    case Value::Type::Number: return formatNumber(value.asNumber());
    case Value::Type::String: return value.asString();
    case Value::Type::Date: return formatDate(value.asDate());
    case Value::Type::Duration: return formatDuration(value.asDuration());
    case Value::Type::Link: {
        const LinkValue &l = value.asLink();
        if (!l.display.isEmpty())
            return l.display;
        const QString title = l.resolved ? paths::pageTitle(l.path) : l.path;
        return l.subpath.isEmpty() ? title : title + u" > "_s + l.subpath;
    }
    case Value::Type::List: {
        QStringList parts;
        for (const Value &v : value.asList())
            parts.append(toPlainText(v));
        return parts.join(u", "_s);
    }
    case Value::Type::Object: {
        QStringList parts;
        const Object &o = value.asObject();
        for (const QString &k : o.keys())
            parts.append(k + u": "_s + toPlainText(o.field(k).value_or(Value())));
        return parts.join(u", "_s);
    }
    case Value::Type::Function: return u"<function>"_s;
    }
    return QString();
}

} // namespace brulion::dataview
