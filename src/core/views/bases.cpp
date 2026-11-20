// Obsidian Bases (bases.h): reading a base, running a view, writing a view's order and sort back.
#include "views/bases.h"
#include "views/cells_p.h"
#include "views/messages_p.h"

#include "dataview/dataview.h"
#include "markdown/frontmatter.h"
#include "text/text.h"
#include "util/paths.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <numeric>

using namespace Qt::StringLiterals;

namespace brulion::views {

namespace {

using dataview::Value;
using dataview::Expression;
using dataview::Syntax;

QString text(const QVariant &v)
{
    if (v.typeId() == QMetaType::Bool)
        return v.toBool() ? u"true"_s : u"false"_s;
    return v.toString().trimmed();
}

/// A filter of the YAML: a statement (a string), or { and | or | not: [filters] }.
std::optional<BaseFilter> readFilter(const QVariant &v, QStringList &problems, int depth = 0)
{
    if (depth > MaxBaseFilterDepth) {
        problems.append(messages::badFilter(u"…"_s, messages::unsupportedKey(u"filters"_s)));
        return std::nullopt;
    }
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        for (auto [key, value] : m.asKeyValueRange()) {
            const QString k = key.trimmed().toLower();
            BaseFilter f;
            if (k == u"and")
                f.kind = BaseFilter::Kind::And;
            else if (k == u"or")
                f.kind = BaseFilter::Kind::Or;
            else if (k == u"not")
                f.kind = BaseFilter::Kind::Not;
            else {
                problems.append(messages::unsupportedKey(key));
                continue;
            }
            const QVariantList items = value.typeId() == QMetaType::QVariantList ? value.toList() : QVariantList{value};
            for (const QVariant &item : items) {
                if (std::optional<BaseFilter> child = readFilter(item, problems, depth + 1))
                    f.children.append(*std::move(child));
            }
            return f; // (a filter object holds one of and / or / not)
        }
        return std::nullopt;
    }
    const QString statement = text(v);
    if (statement.isEmpty())
        return std::nullopt;
    BaseFilter f;
    f.statement = statement;
    const auto parsed = dataview::parseExpression(statement, Syntax::Bases);
    if (const QString *error = std::get_if<QString>(&parsed)) {
        problems.append(messages::badFilter(statement, *error));
        // A statement that does not parse matches nothing (as Obsidian shows no results for it).
        const auto never = dataview::parseExpression(u"false"_s, Syntax::Bases);
        f.expression = std::get<Expression>(never);
        return f;
    }
    f.expression = std::get<Expression>(parsed);
    return f;
}

ViewKind kindOf(const QString &type, QString &notice)
{
    const QString t = type.trimmed().toLower();
    if (t == u"table" || t.isEmpty())
        return ViewKind::Table;
    if (t == u"cards" || t == u"card" || t == u"gallery")
        return ViewKind::Gallery;
    if (t == u"list")
        return ViewKind::List;
    if (t == u"kanban" || t == u"board")
        return ViewKind::Board;
    if (t == u"calendar")
        return ViewKind::Calendar;
    notice = t == u"map" ? messages::mapAsTable() : messages::unknownViewType(type);
    return ViewKind::Table;
}

std::optional<BaseSort> readSort(const QVariant &v)
{
    if (v.typeId() == QMetaType::QVariantMap) {
        const QVariantMap m = v.toMap();
        BaseSort s;
        s.property = text(m.value(u"property"_s));
        s.descending = text(m.value(u"direction"_s)).compare(u"desc", Qt::CaseInsensitive) == 0;
        if (s.property.isEmpty())
            return std::nullopt;
        return s;
    }
    BaseSort s;
    s.property = text(v);
    if (s.property.isEmpty())
        return std::nullopt;
    return s;
}

/// Formulas in an order that computes every formula after those it reads; a cycle's members out.
QList<std::pair<QString, Expression>> orderFormulas(const QList<std::pair<QString, Expression>> &formulas,
                                                    QStringList &problems)
{
    QHash<QString, qsizetype> at;
    for (qsizetype i = 0; i < formulas.size(); ++i)
        at.insert(formulas.at(i).first, i);
    QList<int> state(formulas.size(), 0); // 0 new, 1 visiting, 2 done, 3 in a cycle
    QList<std::pair<QString, Expression>> out;
    // Iterative depth-first search (a base may hold thousands of formulas chained).
    for (qsizetype start = 0; start < formulas.size(); ++start) {
        if (state[start] != 0)
            continue;
        QList<std::pair<qsizetype, qsizetype>> stack{{start, 0}};
        state[start] = 1;
        while (!stack.isEmpty()) {
            auto &[node, next] = stack.last();
            const QStringList &deps = formulas.at(node).second.formulas();
            if (next < deps.size()) {
                const qsizetype dep = at.value(deps.at(next++), -1);
                if (dep < 0 || state[dep] == 2)
                    continue;
                if (state[dep] == 1 || state[dep] == 3) {
                    // A cycle: every formula on the stack from dep up is in it.
                    for (auto &[n, unused] : stack) {
                        Q_UNUSED(unused);
                        if (n == dep || state[n] == 3)
                            state[n] = 3;
                    }
                    for (qsizetype k = stack.size() - 1; k >= 0 && stack.at(k).first != dep; --k)
                        state[stack.at(k).first] = 3;
                    state[dep] = 3;
                    continue;
                }
                state[dep] = 1;
                stack.append({dep, 0});
                continue;
            }
            if (state[node] != 3) {
                // A formula reading one in a cycle is left out too.
                const bool broken = std::ranges::any_of(deps, [&](const QString &d) {
                    const qsizetype i = at.value(d, -1);
                    return i >= 0 && state[i] == 3;
                });
                state[node] = broken ? 3 : 2;
                if (!broken)
                    out.append(formulas.at(node));
            }
            stack.removeLast();
        }
    }
    for (qsizetype i = 0; i < formulas.size(); ++i) {
        if (state[i] == 3)
            problems.append(messages::circularFormula(formulas.at(i).first));
    }
    return out;
}

/// The default summaries of Bases by their names (case-insensitive) -> view.h's functions.
QString defaultSummary(const QString &name)
{
    const QString n = name.trimmed().toLower();
    static const QHash<QString, QString> table{
            {u"average"_s, u"average"_s}, {u"min"_s, u"min"_s},         {u"max"_s, u"max"_s},
            {u"sum"_s, u"sum"_s},         {u"range"_s, u"range"_s},     {u"median"_s, u"median"_s},
            {u"stddev"_s, u"stddev"_s},   {u"earliest"_s, u"earliest"_s}, {u"latest"_s, u"latest"_s},
            {u"checked"_s, u"checked"_s}, {u"unchecked"_s, u"unchecked"_s}, {u"empty"_s, u"empty"_s},
            {u"filled"_s, u"filled"_s},   {u"unique"_s, u"unique"_s},   {u"count"_s, u"count"_s},
    };
    return table.value(n);
}

/// The expression reading a property id (`note.x` -> note["x"], `file.x`, `formula.x`).
Expression propertyExpression(const QString &id)
{
    QString source;
    if (id.startsWith(u"note."))
        source = u"note[\""_s + id.sliced(5).replace(u'"', u"\\\""_s) + u"\"]"_s;
    else if (id.startsWith(u"formula."))
        source = u"formula[\""_s + id.sliced(8).replace(u'"', u"\\\""_s) + u"\"]"_s;
    else
        source = id;
    const auto parsed = dataview::parseExpression(source, Syntax::Bases);
    if (const Expression *e = std::get_if<Expression>(&parsed))
        return *e;
    return {};
}

QString scalarYaml(const QString &s)
{
    static const QStringList reserved{u"true"_s, u"false"_s, u"null"_s, u"yes"_s, u"no"_s, u"on"_s, u"off"_s, u"~"_s};
    const bool plain = !s.isEmpty() && (s[0].isLetter() || s[0] == u'_')
                       && std::ranges::all_of(s, [](QChar c) {
                              return c.isLetterOrNumber() || c == u'_' || c == u'.' || c == u'-' || c == u'/' || c.isMark();
                          })
                       && !reserved.contains(s.toLower());
    if (plain)
        return s;
    QString out = s;
    out.replace(u'\\', u"\\\\"_s);
    out.replace(u'"', u"\\\""_s);
    return u'"' + out + u'"';
}

// ---- running a view -----------------------------------------------------------------------------------

class Runner {
public:
    Runner(const Base &base, const BaseView &view, const VaultIndex &index, const ViewOptions &options)
        : m_base(base), m_view(view), m_index(index), m_options(options), m_cells(index, options)
    { }

    std::variant<ViewResult, QString> run();

private:
    struct Candidate {
        VaultPath path;
        Value row;
        Value formulas; ///< a map of the formulas' values (null until computed)
    };

    /// Evaluate `e` for a candidate; an error message sets m_error (a run too costly stops).
    Value eval(const Expression &e, Candidate &c);
    Value formulaMap(Candidate &c);
    bool passes(const BaseFilter &f, Candidate &c);
    void columns();
    void cells(Row &row, Candidate &c);
    void groups(QList<qsizetype> &order, QList<Candidate> &candidates);
    void summaries();
    void newRow();

    const Base &m_base;
    const BaseView &m_view;
    const VaultIndex &m_index;
    const ViewOptions &m_options;
    detail::CellReader m_cells;
    ViewResult m_out;
    QString m_error;
    QStringList m_ids;                 ///< the columns' property ids
    QList<Expression> m_columnExprs;   ///< one per column (null for properties and built-ins)
};

Value Runner::formulaMap(Candidate &c)
{
    if (!c.formulas.isNull() || m_base.formulas.isEmpty())
        return c.formulas;
    QList<std::pair<QString, Value>> values;
    for (const auto &[name, expr] : m_base.formulas) {
        const QList<std::pair<QString, Value>> bound{{u"formula"_s, Value::map(values)}};
        const auto v = m_cells.evaluator().evaluate(expr, c.row, bound);
        if (const QString *error = std::get_if<QString>(&v)) {
            if (m_cells.evaluator().exhausted())
                m_error = *error;
            values.append({name, Value()});
        } else {
            values.append({name, std::get<Value>(v)});
        }
    }
    c.formulas = Value::map(values);
    return c.formulas;
}

Value Runner::eval(const Expression &e, Candidate &c)
{
    if (e.isNull() || !m_error.isEmpty())
        return Value();
    QList<std::pair<QString, Value>> bound;
    if (!m_base.formulas.isEmpty())
        bound.append({u"formula"_s, formulaMap(c)});
    const auto v = m_cells.evaluator().evaluate(e, c.row, bound);
    if (const QString *error = std::get_if<QString>(&v)) {
        if (m_cells.evaluator().exhausted())
            m_error = *error;
        return Value();
    }
    return std::get<Value>(v);
}

bool Runner::passes(const BaseFilter &f, Candidate &c)
{
    switch (f.kind) {
    case BaseFilter::Kind::Statement: return eval(f.expression, c).truthy();
    case BaseFilter::Kind::And:
        return std::ranges::all_of(f.children, [&](const BaseFilter &child) { return passes(child, c); });
    case BaseFilter::Kind::Or:
        return std::ranges::any_of(f.children, [&](const BaseFilter &child) { return passes(child, c); });
    case BaseFilter::Kind::Not:
        return std::ranges::none_of(f.children, [&](const BaseFilter &child) { return passes(child, c); });
    }
    return false;
}

void Runner::columns()
{
    QHash<QString, PropertyKeyInfo> info;
    for (const PropertyKeyInfo &k : m_index.propertyKeys())
        info.insert(queryPropertyKey(k.key), k);
    QStringList order = m_view.order;
    if (order.isEmpty())
        order = {u"file.name"_s};
    for (const QString &written : std::as_const(order)) {
        const QString id = baseProperty(written);
        Column col;
        col.id = id;
        col.sortKey = id;
        Expression expr;
        const QString display = m_base.displayNames.value(id);
        if (id.startsWith(u"note.")) {
            const QString key = id.sliced(5);
            col.kind = ColumnKind::Property;
            col.property = key;
            col.label = display.isEmpty() ? key : display;
            const auto it = info.constFind(queryPropertyKey(key));
            col.type = it != info.cend() && !it->type.isEmpty() ? it->type : u"text"_s;
            col.editable = col.type != u"unsupported";
        } else if (id.startsWith(u"formula.")) {
            const QString name = dataview::canonicalKey(id.sliced(8));
            col.kind = ColumnKind::Formula;
            col.label = display.isEmpty() ? id.sliced(8) : display;
            for (const auto &[n, e] : m_base.formulas) {
                if (n == name)
                    expr = e;
            }
        } else {
            static const QHash<QString, ColumnKind> builtins{
                    {u"file.name"_s, ColumnKind::Title},     {u"file.path"_s, ColumnKind::Path},
                    {u"file.folder"_s, ColumnKind::Folder},  {u"file.tags"_s, ColumnKind::Tags},
                    {u"file.mtime"_s, ColumnKind::Modified}, {u"file.size"_s, ColumnKind::Size},
            };
            const auto it = builtins.constFind(id);
            if (it != builtins.cend()) {
                col.kind = *it;
                col.label = display.isEmpty() ? (*it == ColumnKind::Title ? messages::fileName() : messages::columnLabel(*it))
                                              : display;
            } else {
                col.kind = ColumnKind::Formula;
                col.label = display.isEmpty() ? id : display;
                expr = propertyExpression(id);
            }
        }
        m_out.columns.append(col);
        m_ids.append(id);
        m_columnExprs.append(expr);
    }
}

void Runner::cells(Row &row, Candidate &c)
{
    for (qsizetype i = 0; i < m_out.columns.size(); ++i) {
        const Column &col = m_out.columns.at(i);
        Cell cell;
        switch (col.kind) {
        case ColumnKind::Property:
            cell = m_cells.propertyCell(row, queryPropertyKey(col.property));
            if (!m_index.note(row.path))
                cell = {}; // an attachment has no properties
            break;
        case ColumnKind::Formula: cell = detail::valueCell(eval(m_columnExprs.at(i), c), m_index); break;
        default: cell = m_cells.builtinCell(row, col.kind); break;
        }
        row.cells.append(cell);
    }
    if (m_out.kind == ViewKind::Gallery || m_out.kind == ViewKind::Board) {
        if (!m_view.image.isEmpty()) {
            const Value v = eval(propertyExpression(baseProperty(m_view.image)), c);
            if (!v.isNull())
                m_cells.coverFrom(row, dataview::toMarkdown(v, m_index));
        }
        if (row.cover.isEmpty())
            m_cells.cover(row);
    }
}

std::variant<ViewResult, QString> Runner::run()
{
    m_out.kind = m_view.kind;
    m_out.type = QueryType::Pages;
    m_out.notice = m_view.notice;
    if (m_out.kind == ViewKind::Board && !m_view.groupBy) {
        m_out.kind = ViewKind::Table;
        m_out.notice = messages::boardNeedsGroup();
    }
    columns();
    // Every file of the notebook: notes and attachments, by path.
    QList<VaultPath> paths = m_index.paths();
    paths.append(m_index.attachments());
    std::ranges::sort(paths);
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    QList<Candidate> candidates;
    for (const VaultPath &path : std::as_const(paths)) {
        if (path == m_options.currentPath && path.endsWith(u".base"))
            continue; // (a base does not list itself)
        Candidate c{path, m_cells.evaluator().file(path), Value()};
        if (m_base.filters && !passes(*m_base.filters, c))
            continue;
        if (m_view.filters && !passes(*m_view.filters, c))
            continue;
        if (!m_error.isEmpty())
            return m_error;
        candidates.append(std::move(c));
    }
    if (!m_error.isEmpty())
        return m_error;
    // Sort: the view's keys (a missing value last), then the file name.
    QList<Expression> sortExprs;
    for (const BaseSort &s : m_view.sort)
        sortExprs.append(propertyExpression(baseProperty(s.property)));
    QList<QList<Value>> keys(candidates.size());
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        for (const Expression &e : std::as_const(sortExprs))
            keys[i].append(eval(e, candidates[i]));
    }
    if (!m_error.isEmpty())
        return m_error;
    QList<qsizetype> order(candidates.size());
    std::iota(order.begin(), order.end(), 0);
    QStringList nameKeys;
    nameKeys.reserve(candidates.size());
    for (const Candidate &c : std::as_const(candidates))
        nameKeys.append(text::nameSortKey(paths::basename(c.path)));
    std::ranges::stable_sort(order, [&](qsizetype a, qsizetype b) {
        for (qsizetype k = 0; k < sortExprs.size(); ++k) {
            const Value &x = keys[a][k];
            const Value &y = keys[b][k];
            if (x.isNull() != y.isNull())
                return !x.isNull();
            if (x.isNull())
                continue;
            const int c = dataview::compare(x, y);
            if (c != 0)
                return m_view.sort.at(k).descending ? c > 0 : c < 0;
        }
        return text::compareNameKeys(nameKeys.at(a), paths::basename(candidates.at(a).path), nameKeys.at(b),
                                     paths::basename(candidates.at(b).path))
               < 0;
    });
    m_out.total = int(order.size());
    const int limit = m_view.limit > 0 ? std::min(m_view.limit, MaxQueryLimit) : m_options.defaultLimit;
    if (order.size() > limit) {
        order.resize(limit);
        m_out.limited = true;
    }
    groups(order, candidates);
    // Rows in order (grouped: group by group).
    for (const qsizetype i : std::as_const(order)) {
        Candidate &c = candidates[i];
        Row row;
        row.path = c.path;
        const ParsedNote *note = m_index.note(c.path);
        row.title = note ? note->title : paths::basename(c.path);
        cells(row, c);
        if (!m_error.isEmpty())
            return m_error;
        m_out.rows.append(std::move(row));
    }
    summaries();
    newRow();
    return m_out;
}

/// What a group of a base is keyed by: a link's file, else its plain text.
QString groupKeyOf(const Value &v)
{
    if (v.isNull())
        return {};
    if (v.is(Value::Type::Link))
        return v.asLink().path;
    return text::foldKey(dataview::toPlainText(v));
}

void Runner::groups(QList<qsizetype> &order, QList<Candidate> &candidates)
{
    if (!m_view.groupBy)
        return;
    const QString id = baseProperty(m_view.groupBy->property);
    const Expression expr = propertyExpression(id);
    struct G {
        QString key;
        Value value;
        QList<qsizetype> members;
    };
    QList<G> groups;
    QHash<QString, qsizetype> at;
    for (const qsizetype i : std::as_const(order)) {
        Value v = eval(expr, candidates[i]);
        // A list groups by its items (a note with tags a and b is in both groups).
        const QList<Value> values = v.is(Value::Type::List) && !v.asList().isEmpty() ? v.asList() : QList<Value>{v};
        for (const Value &item : values) {
            const QString key = groupKeyOf(item);
            auto [it, added] = at.tryEmplace(key, groups.size());
            if (added)
                groups.append(G{key, item, {}});
            groups[*it].members.append(i);
        }
    }
    // Groups by value (ascending or descending), the one without a value last.
    std::ranges::stable_sort(groups, [&](const G &a, const G &b) {
        if (a.value.isNull() != b.value.isNull())
            return !a.value.isNull();
        const int c = dataview::compare(a.value, b.value);
        return m_view.groupBy->descending ? c > 0 : c < 0;
    });
    if (m_view.groupOrder) {
        // Only the groups listed, in that order ("" for the one without a value).
        QList<G> listed;
        for (const QString &want : *m_view.groupOrder) {
            const QString key = want.isEmpty() || want == u"null" ? QString() : text::foldKey(want);
            const auto it = std::ranges::find_if(groups, [&](const G &g) {
                return g.key == key || (!g.value.isNull() && text::foldKey(dataview::toPlainText(g.value)) == key);
            });
            if (it != groups.cend())
                listed.append(*it);
            else if (!key.isEmpty())
                listed.append(G{u"new:"_s + key, Value::string(want), {}});
        }
        groups = listed;
    }
    // The column of the group's property; what dropping a card writes.
    const qsizetype column = m_ids.indexOf(id);
    m_out.groupColumn = int(column);
    if (id.startsWith(u"note.")) {
        m_out.groupKind = u"property"_s;
        m_out.groupProperty = id.sliced(5);
    } else if (m_view.kind == ViewKind::Board) {
        m_out.notice = messages::boardWithoutDrops();
    }
    // The rows go group by group; each group lists its rows.
    QList<qsizetype> rowsOrder;
    QHash<qsizetype, int> rowOf;
    for (const G &g : std::as_const(groups)) {
        Group out;
        out.key = g.key;
        out.label = g.value.isNull() ? messages::noValue() : dataview::toPlainText(g.value);
        if (g.value.is(Value::Type::Link)) {
            const QString target = m_index.linkTarget(g.value.asLink().path).value_or(g.value.asLink().path);
            out.value = u"[["_s + target + u"]]"_s;
            if (const ParsedNote *note = m_index.note(g.value.asLink().path))
                out.label = note->title;
        } else if (!g.value.isNull()) {
            out.value = dataview::toPlainText(g.value);
        }
        out.droppable = m_out.groupKind == u"property";
        for (const qsizetype member : g.members) {
            auto [it, added] = rowOf.tryEmplace(member, int(rowsOrder.size()));
            if (added)
                rowsOrder.append(member);
            out.rows.append(*it);
        }
        m_out.groups.append(out);
    }
    // Rows outside every listed group are not shown.
    order = rowsOrder;
}

void Runner::summaries()
{
    for (const auto &[property, name] : m_view.summaries) {
        const qsizetype column = m_ids.indexOf(baseProperty(property));
        if (column < 0)
            continue;
        Column &col = m_out.columns[column];
        const QString function = defaultSummary(name);
        const auto custom = std::ranges::find_if(m_base.summaries, [&](const auto &s) {
            return s.first.compare(name, Qt::CaseInsensitive) == 0;
        });
        const auto summaryOf = [&](const QList<int> &rows) -> QString {
            if (!function.isEmpty()) {
                QList<QVariant> values;
                for (const int r : rows)
                    values.append(m_out.rows.at(r).cells.at(column).value);
                return summarize(function, values);
            }
            if (custom == m_base.summaries.cend())
                return messages::unknownSummary(name);
            // A custom summary: its formula over `values`, the column's values.
            dataview::ValueList values;
            for (const int r : rows) {
                const QVariant &v = m_out.rows.at(r).cells.at(column).value;
                switch (v.typeId()) {
                case QMetaType::Double: values.append(Value::number(v.toDouble())); break;
                case QMetaType::Bool: values.append(Value::boolean(v.toBool())); break;
                case QMetaType::QStringList: {
                    dataview::ValueList items;
                    for (const QString &s : v.toStringList())
                        items.append(Value::string(s));
                    values.append(Value::list(items));
                    break;
                }
                default:
                    if (const auto d = QDate::fromString(v.toString().left(10), Qt::ISODate); d.isValid() && v.toString().size() == 10)
                        values.append(Value::date(d));
                    else
                        values.append(v.isValid() ? Value::string(v.toString()) : Value());
                    break;
                }
            }
            const auto result = m_cells.evaluator().evaluate(custom->second, Value(),
                                                             {{u"values"_s, Value::list(values)}});
            if (const QString *error = std::get_if<QString>(&result))
                return *error;
            return dataview::toPlainText(std::get<Value>(result));
        };
        col.summary = function.isEmpty() ? name : function;
        col.summaryLabel = function.isEmpty() ? name : summaryLabel(function);
        QList<int> all(m_out.rows.size());
        std::iota(all.begin(), all.end(), 0);
        col.summaryText = summaryOf(all);
        for (Group &g : m_out.groups) {
            while (g.summaryTexts.size() < m_out.columns.size())
                g.summaryTexts.append(QString());
            g.summaryTexts[column] = summaryOf(g.rows);
        }
    }
}

void Runner::newRow()
{
    NewRow &n = m_out.newRow;
    n.possible = true;
    n.folder = m_options.newPagesFolder;
    // The view's own conditions on folders, tags and properties, where they are plain: a top-level
    // statement or an item of a top-level `and`.
    static const QRegularExpression inFolder(uR"(^file\.inFolder\(\s*["']([^"']+)["']\s*\)$)"_s);
    static const QRegularExpression hasTag(uR"(^file\.hasTag\(\s*["']([^"']+)["']\s*\)$)"_s);
    static const QRegularExpression equals(uR"(^(?:note\.)?([\p{L}\p{N}_-]+)\s*==?=?\s*["']([^"']*)["']$)"_s);
    const auto take = [&](const BaseFilter &f) {
        if (f.kind != BaseFilter::Kind::Statement || f.statement.size() > 500)
            return;
        const QString s = f.statement.trimmed();
        if (const auto m = inFolder.match(s); m.hasMatch())
            n.folder = m.captured(1);
        else if (const auto t = hasTag.match(s); t.hasMatch())
            n.tags.append(t.captured(1));
        else if (const auto e = equals.match(s); e.hasMatch() && !e.captured(1).startsWith(u"file"))
            n.properties.append({e.captured(1), e.captured(2)});
    };
    for (const std::optional<BaseFilter> *filters : {&m_base.filters, &m_view.filters}) {
        if (!*filters)
            continue;
        const BaseFilter &f = **filters;
        if (f.kind == BaseFilter::Kind::And) {
            for (const BaseFilter &child : f.children)
                take(child);
        } else {
            take(f);
        }
    }
}

} // namespace

// ---- YAML write-back ----------------------------------------------------------------------------------

namespace {

struct YLine {
    QString text;
    QString eol;
    int indent = 0;
    bool blank = false; ///< empty, only blanks, or only a comment
};

QList<YLine> yamlLines(const QString &yaml)
{
    QList<YLine> out;
    qsizetype start = 0;
    while (start <= yaml.size()) {
        qsizetype end = start;
        while (end < yaml.size() && yaml[end] != u'\n' && yaml[end] != u'\r')
            ++end;
        YLine l;
        l.text = yaml.sliced(start, end - start);
        if (end < yaml.size()) {
            l.eol = yaml[end] == u'\r' && end + 1 < yaml.size() && yaml[end + 1] == u'\n' ? u"\r\n"_s
                                                                                         : QString(yaml[end]);
        }
        while (l.indent < l.text.size() && l.text[l.indent] == u' ')
            ++l.indent;
        const QStringView rest = QStringView(l.text).sliced(l.indent);
        l.blank = rest.trimmed().isEmpty() || rest.startsWith(u'#');
        out.append(l);
        if (end >= yaml.size())
            break;
        start = end + l.eol.size();
    }
    return out;
}

QString joinLines(const QList<YLine> &lines)
{
    QString out;
    for (const YLine &l : lines)
        out += l.text + l.eol;
    return out;
}

/// Does the line (from `indent`) hold the key `key` of a mapping: `key:` / `"key":`?
bool isKeyLine(const YLine &l, qsizetype from, QStringView key)
{
    QStringView rest = QStringView(l.text).sliced(std::min<qsizetype>(from, l.text.size()));
    if (rest.startsWith(u'"') || rest.startsWith(u'\'')) {
        const QChar q = rest.front();
        const qsizetype close = rest.indexOf(q, 1);
        if (close < 0 || rest.sliced(1, close - 1) != key)
            return false;
        rest = rest.sliced(close + 1);
    } else {
        if (!rest.startsWith(key))
            return false;
        rest = rest.sliced(key.size());
    }
    rest = rest.trimmed();
    return rest.startsWith(u':') && (rest.size() == 1 || rest[1].isSpace());
}

/// The value after `key:` on a key line ("" for a block value; a comment is no value).
QString inlineValue(const YLine &l)
{
    const qsizetype colon = l.text.indexOf(u':');
    QString rest = l.text.sliced(colon + 1).trimmed();
    if (rest.startsWith(u'#'))
        return {};
    return rest;
}

/// Replace (or add) `key`'s value in view `view` of `yaml` by `block` (lines under `key:` at the
/// key's indentation + 2, without line endings; empty: `key: []` — or, `remove`, no key at all).
std::optional<QString> setViewKey(const QString &yaml, int view, const QString &key, const QStringList &block,
                                  bool remove)
{
    QList<YLine> lines = yamlLines(yaml);
    const QString eol = yaml.contains(u"\r\n") ? u"\r\n"_s : u"\n"_s;
    // `views:` at the top level, a block value.
    qsizetype viewsLine = -1;
    for (qsizetype i = 0; i < lines.size(); ++i) {
        if (lines[i].indent == 0 && !lines[i].blank && isKeyLine(lines[i], 0, u"views")) {
            viewsLine = i;
            break;
        }
    }
    if (viewsLine < 0 || !inlineValue(lines[viewsLine]).isEmpty())
        return std::nullopt;
    // Its items: `- ` lines at one indentation.
    int seqIndent = -1;
    QList<qsizetype> items;
    qsizetype end = lines.size();
    for (qsizetype i = viewsLine + 1; i < lines.size(); ++i) {
        const YLine &l = lines[i];
        if (l.blank)
            continue;
        const bool dash = QStringView(l.text).sliced(l.indent).startsWith(u"- ") || l.text.sliced(l.indent) == u"-";
        if (seqIndent < 0) {
            if (!dash)
                return std::nullopt;
            seqIndent = l.indent;
        }
        if (l.indent < seqIndent || (l.indent == seqIndent && !dash)) {
            end = i;
            break;
        }
        if (l.indent == seqIndent && dash)
            items.append(i);
    }
    if (view < 0 || view >= items.size())
        return std::nullopt;
    const qsizetype first = items[view];
    qsizetype last = view + 1 < items.size() ? items[view + 1] - 1 : end - 1;
    while (last > first && lines[last].blank)
        --last;
    // The item's keys start right after "- ".
    qsizetype keyIndent = seqIndent + 1;
    while (keyIndent < lines[first].text.size() && lines[first].text[keyIndent] == u' ')
        ++keyIndent;
    const QStringView firstRest = QStringView(lines[first].text).sliced(keyIndent);
    if (firstRest.startsWith(u'{') || firstRest.startsWith(u'['))
        return std::nullopt; // a flow mapping: not edited
    if (isKeyLine(lines[first], keyIndent, key))
        return std::nullopt; // (the key on the item's first line: unusual, not edited)
    qsizetype keyLine = -1;
    for (qsizetype i = first + 1; i <= last; ++i) {
        if (!lines[i].blank && lines[i].indent == keyIndent && isKeyLine(lines[i], keyIndent, key)) {
            keyLine = i;
            break;
        }
        if (!lines[i].blank && lines[i].indent < keyIndent)
            return std::nullopt;
    }
    const QString pad(keyIndent, u' ');
    QList<YLine> replacement;
    const QString lineEol = lines[first].eol.isEmpty() ? eol : lines[first].eol;
    if (!remove) {
        YLine head;
        head.text = pad + key + (block.isEmpty() ? u": []"_s : u":"_s);
        head.eol = lineEol;
        replacement.append(head);
        for (const QString &b : block) {
            YLine l;
            l.text = pad + u"  "_s + b;
            l.eol = lineEol;
            replacement.append(l);
        }
    }
    if (keyLine >= 0) {
        // The key's value: its line, and lines below it indented deeper (or block items at its indent).
        qsizetype valueEnd = keyLine;
        for (qsizetype i = keyLine + 1; i <= last; ++i) {
            const YLine &l = lines[i];
            if (l.blank || l.indent > keyIndent
                || (l.indent == keyIndent && QStringView(l.text).sliced(l.indent).startsWith(u"- ")))
                valueEnd = l.blank ? valueEnd : i;
            else
                break;
        }
        const QString keptEol = lines[valueEnd].eol;
        lines.remove(keyLine, valueEnd - keyLine + 1);
        if (!replacement.isEmpty())
            replacement.last().eol = keptEol;
        for (qsizetype k = 0; k < replacement.size(); ++k)
            lines.insert(keyLine + k, replacement[k]);
    } else if (!remove) {
        // A new key after the item's last line.
        if (lines[last].eol.isEmpty()) {
            lines[last].eol = eol;
            replacement.last().eol.clear();
        }
        for (qsizetype k = 0; k < replacement.size(); ++k)
            lines.insert(last + 1 + k, replacement[k]);
    }
    return joinLines(lines);
}

/// Did the edit change only `key` of view `view`, to `wanted`?
bool verifyEdit(const QString &before, const QString &after, int view, const QString &key,
                const std::optional<QVariant> &wanted)
{
    QVariantMap a = markdown::parseYaml(before);
    QVariantMap b = markdown::parseYaml(after);
    QVariantList va = a.take(u"views"_s).toList();
    QVariantList vb = b.take(u"views"_s).toList();
    if (a != b || va.size() != vb.size() || view >= va.size())
        return false;
    for (qsizetype i = 0; i < va.size(); ++i) {
        QVariantMap ma = va[i].toMap();
        QVariantMap mb = vb[i].toMap();
        if (i == view) {
            const QVariant got = mb.take(key);
            ma.remove(key);
            if (wanted ? got.toList() != wanted->toList() : got.isValid())
                return false;
        }
        if (ma != mb)
            return false;
    }
    return true;
}

} // namespace

std::optional<QString> setBaseViewOrder(const QString &yaml, int view, const QStringList &order)
{
    QStringList block;
    QVariantList wanted;
    for (const QString &id : order) {
        block.append(u"- "_s + scalarYaml(id));
        wanted.append(id);
    }
    const std::optional<QString> out = setViewKey(yaml, view, u"order"_s, block, false);
    if (!out || !verifyEdit(yaml, *out, view, u"order"_s, QVariant(wanted)))
        return std::nullopt;
    return out;
}

std::optional<QString> setBaseViewSort(const QString &yaml, int view, const QList<BaseSort> &sort)
{
    QStringList block;
    QVariantList wanted;
    for (const BaseSort &s : sort) {
        block.append(u"- property: "_s + scalarYaml(s.property));
        block.append(u"  direction: "_s + (s.descending ? u"DESC"_s : u"ASC"_s));
        wanted.append(QVariantMap{{u"property"_s, s.property}, {u"direction"_s, s.descending ? u"DESC"_s : u"ASC"_s}});
    }
    const std::optional<QString> out = setViewKey(yaml, view, u"sort"_s, block, sort.isEmpty());
    if (!out || !verifyEdit(yaml, *out, view, u"sort"_s, sort.isEmpty() ? std::nullopt : std::optional(QVariant(wanted))))
        return std::nullopt;
    return out;
}

// ---- reading a base ------------------------------------------------------------------------------------

std::variant<Base, QString> parseBase(const QString &yaml)
{
    if (yaml.size() > MaxBaseBytes)
        return messages::notYaml();
    Base base;
    const QVariantMap data = markdown::parseYaml(yaml);
    if (data.isEmpty() && !yaml.trimmed().isEmpty() && !yaml.trimmed().startsWith(u'#'))
        return messages::notYaml();
    if (data.contains(u"filters"_s))
        base.filters = readFilter(data.value(u"filters"_s), base.problems);
    QList<std::pair<QString, Expression>> formulas;
    for (auto [name, value] : data.value(u"formulas"_s).toMap().asKeyValueRange()) {
        const QString source = text(value);
        const auto parsed = dataview::parseExpression(source, Syntax::Bases);
        if (const QString *error = std::get_if<QString>(&parsed)) {
            base.problems.append(messages::badFormula(name, *error));
            continue;
        }
        formulas.append({dataview::canonicalKey(name), std::get<Expression>(parsed)});
    }
    base.formulas = orderFormulas(formulas, base.problems);
    for (auto [id, value] : data.value(u"properties"_s).toMap().asKeyValueRange()) {
        const QString display = text(value.toMap().value(u"displayName"_s));
        if (!display.isEmpty())
            base.displayNames.insert(baseProperty(id), display);
    }
    for (auto [name, value] : data.value(u"summaries"_s).toMap().asKeyValueRange()) {
        const auto parsed = dataview::parseExpression(text(value), Syntax::Bases);
        if (const QString *error = std::get_if<QString>(&parsed)) {
            base.problems.append(messages::badFormula(name, *error));
            continue;
        }
        base.summaries.append({name, std::get<Expression>(parsed)});
    }
    const QVariantList views = data.value(u"views"_s).toList();
    for (qsizetype i = 0; i < views.size(); ++i) {
        const QVariantMap v = views.at(i).toMap();
        BaseView view;
        view.type = text(v.value(u"type"_s));
        view.kind = kindOf(view.type, view.notice);
        view.name = text(v.value(u"name"_s));
        if (view.name.isEmpty())
            view.name = view.type.isEmpty() ? u"Table"_s : view.type.left(1).toUpper() + view.type.sliced(1);
        if (v.contains(u"filters"_s)) {
            QStringList problems;
            view.filters = readFilter(v.value(u"filters"_s), problems);
            for (const QString &p : std::as_const(problems))
                base.problems.append(messages::badView(int(i) + 1, p));
        }
        for (const QVariant &o : v.value(u"order"_s).toList()) {
            if (const QString id = text(o); !id.isEmpty())
                view.order.append(id);
        }
        for (const QVariant &s : v.value(u"sort"_s).toList()) {
            if (std::optional<BaseSort> sort = readSort(s))
                view.sort.append(*sort);
        }
        if (v.contains(u"groupBy"_s))
            view.groupBy = readSort(v.value(u"groupBy"_s));
        if (v.contains(u"groupOrder"_s)) {
            QStringList order;
            for (const QVariant &g : v.value(u"groupOrder"_s).toList())
                order.append(g.isValid() && !g.isNull() ? text(g) : QString());
            view.groupOrder = order;
        }
        bool ok = false;
        const int limit = v.value(u"limit"_s).toInt(&ok);
        view.limit = ok && limit > 0 ? limit : 0;
        for (auto [property, name] : v.value(u"summaries"_s).toMap().asKeyValueRange())
            view.summaries.append({property, text(name)});
        view.image = text(v.value(u"image"_s));
        base.views.append(view);
    }
    if (base.views.isEmpty()) {
        BaseView table;
        table.type = u"table"_s;
        table.name = u"Table"_s;
        base.views.append(table);
    }
    return base;
}

std::variant<ViewResult, QString> runBase(const Base &base, int view, const VaultIndex &index,
                                          const ViewOptions &options)
{
    const BaseView &v = base.views.at(view >= 0 && view < base.views.size() ? view : 0);
    return Runner(base, v, index, options).run();
}

int baseViewNamed(const Base &base, const QString &name)
{
    for (qsizetype i = 0; i < base.views.size(); ++i) {
        if (base.views.at(i).name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return int(i);
    }
    return 0;
}

QString baseProperty(const QString &id)
{
    const QString t = id.trimmed();
    if (t.startsWith(u"note.") || t.startsWith(u"file.") || t.startsWith(u"formula.") || t == u"file")
        return t;
    return u"note."_s + t;
}

} // namespace brulion::views
