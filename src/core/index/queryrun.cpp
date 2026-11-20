// Query execution (docs/SYNTAX.md §10) over a VaultIndex — see index/query.h for the semantics.
#include "index/query.h"
#include "index/querycache.h"

#include "dates/expr.h"
#include "dates/local.h"
#include "index/querymessages_p.h"
#include "index/resolve_p.h"
#include "index/textutil_p.h"
#include "index/vaultindex_p.h"
#include "markdown/pageprops.h"
#include "markdown/properties.h"
#include "markdown/tasks.h"
#include "text/text.h"
#include "util/paths.h"

#include <QSet>
#include <QVarLengthArray>

#include <algorithm>
#include <numeric>

namespace brulion {

using namespace Qt::StringLiterals;
using namespace index_detail;

namespace {

int priorityRank(Priority p)
{
    switch (p) {
    case Priority::Highest: return 0;
    case Priority::High: return 1;
    case Priority::Medium: return 2;
    case Priority::None: return 3;
    case Priority::Low: return 4;
    case Priority::Lowest: return 5;
    }
    return 3;
}

int statusRank(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Doing: return 0;
    case TaskStatus::Todo: return 1;
    case TaskStatus::Deferred: return 2;
    case TaskStatus::Done: return 3;
    case TaskStatus::Cancelled: return 4;
    }
    return 1;
}

QString statusKey(TaskStatus s)
{
    switch (s) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

QString priorityKey(Priority p)
{
    switch (p) {
    case Priority::Highest: return u"highest"_s;
    case Priority::High: return u"high"_s;
    case Priority::Medium: return u"medium"_s;
    case Priority::None: return u"none"_s;
    case Priority::Low: return u"low"_s;
    case Priority::Lowest: return u"lowest"_s;
    }
    return {};
}

QueryDateField dateFieldOf(QuerySortKey key)
{
    switch (key) {
    case QuerySortKey::Scheduled: return QueryDateField::Scheduled;
    case QuerySortKey::Start: return QueryDateField::Start;
    case QuerySortKey::Done: return QueryDateField::Done;
    case QuerySortKey::Created: return QueryDateField::Created;
    case QuerySortKey::Remind: return QueryDateField::Remind;
    case QuerySortKey::Date: return QueryDateField::Date;
    default: return QueryDateField::Due;
    }
}

bool isClosed(TaskStatus s)
{
    return s == TaskStatus::Done || s == TaskStatus::Cancelled;
}

// ---- workouts (docs/SYNTAX.md §5 "Gym") ---------------------------------------------------------
// The checkboxes of a workout's exercise lines are its sets' status, not tasks: type:tasks leaves
// out the blocks under a block tagged #trening / #workout in a daily note — unless the query names
// that tag (`tag:trening`). The tagged line itself stays a task ("- [ ] Siłownia #trening").

bool isWorkoutTag(QStringView tag)
{
    return tag == u"trening" || tag == u"workout";
}

bool namesWorkoutTag(const Query &query)
{
    return std::ranges::any_of(query.filters, [](const QueryFilter &f) {
        return f.kind == QueryFilterKind::Tag && !f.negate
               && std::ranges::any_of(f.values, [](const QString &v) { return isWorkoutTag(v); });
    });
}

bool hasWorkouts(const ParsedNote &note)
{
    return note.journalDate && std::ranges::any_of(note.tags, [](const QString &t) { return isWorkoutTag(t); });
}

// ---- ignored pages (docs/SYNTAX.md §5 "Ignoring a page's tasks") ---------------------------------
// A page with `tasks:: ignore` keeps its tasks out of type:tasks — unless the query names pages
// or folders (`page:`, `path:`), which asks for those pages' tasks, these too.

bool namesPages(const Query &query)
{
    return std::ranges::any_of(query.filters, [](const QueryFilter &f) {
        return (f.kind == QueryFilterKind::Page || f.kind == QueryFilterKind::Path) && !f.negate;
    });
}

bool isExerciseLine(const ParsedNote &note, int block)
{
    for (int i = note.blocks[block].parent; i >= 0; i = note.blocks[i].parent) {
        if (std::ranges::any_of(note.blocks[i].tags, [](const QString &t) { return isWorkoutTag(t); }))
            return true;
    }
    return false;
}

/// An item under evaluation.
struct View {
    const Entry *entry = nullptr;
    int block = -1;
    const Task *task = nullptr;
    const Reminder *reminder = nullptr;

    const ParsedNote &note() const { return entry->note; }
    const VaultPath &path() const { return entry->path; }
    int line() const
    {
        if (reminder)
            return reminder->line;
        return block >= 0 ? entry->note.blocks[block].line : 0;
    }
};

/// A candidate item of one note: where it is (`reminder`: its ordinal in the block).
struct Candidate {
    View view;
    int reminder = -1;
};

struct GroupInfo {
    QString key;
    QString label;
    int rank = 0;
    QString order;
    double number = 0; ///< numeric group values (rank PropertyNumberRank) compare by it
    QString value;     ///< group:<property>: the value as written
};

GroupInfo groupInfo(const QString &key, const QString &label, int rank, const QString &order)
{
    GroupInfo g;
    g.key = key;
    g.label = label;
    g.rank = rank;
    g.order = order;
    return g;
}

// ---- property values (docs/SYNTAX.md §10 "Properties") ------------------------------------------

/// One value of a property as filters, sorting and groups read it: a scalar, or one item of a list.
struct PropertyItem {
    QString text;   ///< as written ("[[Dom]]", "w toku", "12.5", "true")
    QString folded; ///< foldKey(text); a link's: foldKey(its target)
    QString link;   ///< a `[[link]]`'s (or a Logseq list item's) target, else ""
    std::optional<double> number;
    std::optional<DateSpec> date;
    std::optional<bool> boolean;
};

/// Group ranks of property values: numbers first (by value), dates, checkboxes, then texts and links.
constexpr int PropertyNumberRank = 0;
constexpr int PropertyDateRank = 1;
constexpr int PropertyBoolRank = 2;
constexpr int PropertyTextRank = 3;

/// "YYYY-MM-DD", "YYYY-MM-DD HH:mm[:ss]" / "…THH:mm[:ss]" (valid) as a date.
std::optional<DateSpec> propertyDate(QStringView t)
{
    if (t.size() < 10 || t[4] != u'-' || t[7] != u'-')
        return std::nullopt;
    const QDate d = QDate::fromString(t.first(10).toString(), Qt::ISODate);
    if (!d.isValid())
        return std::nullopt;
    if (t.size() == 10)
        return DateSpec{d, std::nullopt};
    if (t[10] != u'T' && t[10] != u' ')
        return std::nullopt;
    const QStringView time = t.sliced(11);
    QTime tm = QTime::fromString(time.toString(), u"HH:mm"_s);
    if (!tm.isValid())
        tm = QTime::fromString(time.toString(), u"HH:mm:ss"_s);
    if (!tm.isValid())
        return std::nullopt;
    return DateSpec{d, tm};
}

/// A text value: a `[[link]]` (its target; a date link `[[2026-10-28]]` is a date too), a date,
/// a number (also "3,5", "1 200"), true / false, else a text.
PropertyItem propertyText(const QString &written)
{
    PropertyItem item;
    item.text = written.trimmed();
    QStringView t(item.text);
    if (t.startsWith(u"[[") && t.endsWith(u"]]") && t.size() > 4) {
        QStringView target = t.sliced(2, t.size() - 4);
        if (const qsizetype bar = target.indexOf(u'|'); bar >= 0)
            target = target.first(bar);
        item.link = target.trimmed().toString();
        item.folded = text::foldKey(item.link);
        item.date = propertyDate(item.link);
        return item;
    }
    item.folded = text::foldKey(t);
    if ((item.date = propertyDate(t)))
        return item;
    if (t.compare(u"true", Qt::CaseInsensitive) == 0 || t.compare(u"false", Qt::CaseInsensitive) == 0) {
        item.boolean = t.compare(u"true", Qt::CaseInsensitive) == 0;
        return item;
    }
    if (const auto n = markdown::parseNumberInput(t))
        item.number = n->toDouble();
    return item;
}

bool isNamesKey(const QString &folded)
{
    return folded == u"tags" || folded == u"tag" || folded == u"aliases" || folded == u"alias";
}

/// The items of a value as parseNote() keeps it (`logseq`: a list's items are names — links or
/// tags — written without their `[[ ]]` / '#').
void addPropertyItems(const QVariant &v, bool logseq, QList<PropertyItem> &out, int depth = 0)
{
    if (depth > 4 || !v.isValid() || v.isNull())
        return;
    switch (v.typeId()) {
    case QMetaType::Bool: {
        PropertyItem item;
        item.boolean = v.toBool();
        item.text = item.folded = *item.boolean ? u"true"_s : u"false"_s;
        out.append(item);
        return;
    }
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
    case QMetaType::Double: {
        PropertyItem item;
        item.number = v.toDouble();
        item.text = item.folded = v.toString();
        out.append(item);
        return;
    }
    case QMetaType::QString:
        if (const QString t = v.toString().trimmed(); !t.isEmpty())
            out.append(propertyText(t));
        return;
    case QMetaType::QStringList:
        for (const QString &name : v.toStringList()) {
            if (name.trimmed().isEmpty())
                continue;
            if (!logseq) {
                out.append(propertyText(name));
                continue;
            }
            PropertyItem item;
            item.text = name.trimmed();
            item.link = item.text;
            item.folded = text::foldKey(item.text);
            item.date = propertyDate(item.text);
            out.append(item);
        }
        return;
    case QMetaType::QVariantList: {
        const QVariantList list = v.toList();
        // `up: [[Dom]]` unquoted: a list holding a list holding "Dom".
        if (list.size() == 1 && list.first().typeId() == QMetaType::QString && depth > 0) {
            out.append(propertyText(u"[["_s + list.first().toString() + u"]]"_s));
            return;
        }
        for (const QVariant &item : list)
            addPropertyItems(item, logseq, out, depth + 1);
        return;
    }
    default: return; // maps and other values: no items
    }
}

} // namespace

namespace index_detail {

/// Sort value of an item (`present` false = missing, sorts last).
struct QuerySortValue {
    bool present = false;
    int number = 0; ///< priority / status rank; QuerySortKey::Property: the value's kind (group ranks)
    QString text;
    DateSpec date;
    QString nameKey; ///< nameSortKey(text) for QuerySortKey::Page and property texts
    double real = 0; ///< QuerySortKey::Property: a number's value
};

/// A match of a query: where it is and what it sorts by — no Task copy (a QueryCache keeps these
/// across index changes; QueryItems are made for the returned ones).
struct QueryMatch {
    VaultPath path;
    /// The note's record when the match was found: valid while the note is unchanged — the
    /// matches of a changed note are found again — and read only at the index version the
    /// matches were brought up to (else looked up by path).
    const Entry *entry = nullptr;
    int block = -1;
    int reminder = -1; ///< ordinal in the block's reminders (type:reminders)
    int line = 0;
    QVarLengthArray<QuerySortValue, 1> keys; ///< one per QuerySort of the query
    QString title;                           ///< type:pages: the note's title …
    QString titleKey;                        ///< … and its nameSortKey()
    DateSpec at;                             ///< type:reminders: the reminder's time
};

} // namespace index_detail

namespace {

using SortValue = QuerySortValue;

class Runner {
public:
    Runner(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart,
           const QueryOptions &options)
        : m_query(query), m_index(index), m_today(today), m_weekStart(weekStart), m_options(options)
    { }

    /// The matches of one note (filtered; type:blocks without nested matches), unsorted.
    void matchesOf(const Entry &entry, QList<QueryMatch> &out);
    /// The result order: the sort keys, the default order, then the place in the vault — the
    /// stable sort of the candidates in path / block / reminder order as a total order.
    bool less(const QueryMatch &a, const QueryMatch &b) const;
    /// Sort the matches of the whole result.
    void sort(QList<QueryMatch> &matches) const;
    /// The result of `sorted` matches: total, the first `limit` as items, groups.
    QueryResult finish(const QList<QueryMatch> &sorted);
    /// The QueryItem of a match (the note as it is in the index now).
    QueryItem itemOf(const QueryMatch &match) const;
    /// The matches' Entry pointers may be read (the index has not changed since they were found
    /// or brought up to date); else notes are looked up by path.
    void trustEntries(bool on) { m_trustEntries = on; }

private:
    View viewOf(const QueryMatch &match) const;
    bool matchesAll(const View &view);
    bool test(const QueryFilter &f, const View &view);
    bool pageMatches(const QString &value, const View &view);
    bool pathMatches(const QString &value, const View &view);
    bool refMatches(const QString &value, const View &view);
    bool textMatches(const QString &value, const View &view);
    bool dateMatches(QueryDateField field, const QueryDateCondition &cond, const View &view);
    bool isOverdue(QueryDateField field, DateSpec value, const View &view) const;
    /// The item's dates of a field (at most a few: no allocation for the usual one).
    using DateValues = QVarLengthArray<DateSpec, 4>;
    DateValues dateValues(QueryDateField field, const View &view) const;
    /// An item's tags: a block's own, its ancestors' and the page's; a page's: ParsedNote::tags.
    QStringList effectiveTags(const View &view);
    QSet<int> refLines(const VaultPath &path, const QString &value);
    /// The tag keys that name the page `ref:value` means.
    const QSet<QString> &refTags(const QString &value);
    std::optional<VaultPath> resolveCached(const QString &target);
    QString folded(const QString &value);
    /// Drop the candidates (of one note) inside the block of another one (type:blocks).
    void collapseNested(QList<Candidate> &cands) const;
    SortValue sortValue(const QuerySort &sort, const View &view);
    int defaultCompare(const QueryMatch &a, const QueryMatch &b) const;
    QList<GroupInfo> groupsOf(const View &view);
    QList<QueryGroup> buildGroups(const QList<QueryItem> &items, const QList<View> &views);
    /// The values of the property `key` (folded) of an item: a page's page properties; a block's
    /// own, else its nearest ancestor's, else its page's. `found`: the key is there (maybe empty).
    QList<PropertyItem> propertyItems(const View &view, const QString &key, bool *found = nullptr);
    /// A note's page property as written: its folded key, value and where it is written.
    struct RawProperty {
        QString key;  ///< folded
        QString name; ///< as written
        QVariant value;
        bool logseq = false;
    };
    /// The page properties of a note, keys folded (once per run; values typed when asked).
    const QList<RawProperty> &pageProperties(const Entry &entry);
    /// queryPropertyKey() of a key as written, folded once per run (keys repeat across notes).
    const QString &foldedKey(const QString &key);
    bool propertyMatches(const QueryPropertyCondition &c, const PropertyItem &item);
    /// Do the link targets `a` and `b` (written in the item's note / the query) name one page?
    bool sameLink(const QString &a, const QString &b);

    const Query &m_query;
    const VaultIndex &m_index;
    QDate m_today;
    Qt::DayOfWeek m_weekStart;
    const QueryOptions &m_options;
    /// effectiveTags() by note and block (each computed once per run).
    QHash<const Entry *, QList<std::optional<QStringList>>> m_effectiveTags;
    /// Resolved date expressions by condition (each filter's conditions resolved once per run).
    QHash<const QueryDateCondition *, std::optional<dates::DateExpr>> m_exprs;
    QHash<QString, std::optional<VaultPath>> m_resolved;
    QHash<QString, QSet<int>> m_refLines;
    QHash<QString, QSet<QString>> m_refTags;
    QHash<QString, QString> m_folded;
    QHash<const Entry *, QList<RawProperty>> m_pageProps;
    QHash<QString, QString> m_foldedKeys;
    /// A property condition's value read once per run: as a number, a date (resolved), a link.
    struct ConditionValue {
        QString folded, foldedTo;
        std::optional<double> number, numberTo;
        std::optional<dates::DateExpr> date; ///< Range: the range from..to
        std::optional<bool> boolean;
        QString link; ///< `[[X]]`: X
    };
    QHash<const QueryPropertyCondition *, ConditionValue> m_conditions;
    const ConditionValue &conditionValue(const QueryPropertyCondition &c);
    bool m_trustEntries = true;
};

// ---- candidates ---------------------------------------------------------------------------------

void Runner::matchesOf(const Entry &e, QList<QueryMatch> &out)
{
    QList<Candidate> cands;
    const QList<Block> &blocks = e.note.blocks;
    switch (m_query.type) {
    case QueryType::Pages: cands.append(Candidate{View{&e, -1, nullptr, nullptr}, -1}); break;
    case QueryType::Tasks: {
        if (markdown::tasksIgnored(e.note) && !namesPages(m_query))
            break;
        const bool workouts = !namesWorkoutTag(m_query) && hasWorkouts(e.note);
        for (const int i : e.taskBlocks()) {
            if (!workouts || !isExerciseLine(e.note, i))
                cands.append(Candidate{View{&e, i, &*blocks[i].task, nullptr}, -1});
        }
        break;
    }
    case QueryType::Blocks:
        for (qsizetype i = 0; i < blocks.size(); ++i) {
            const Block &b = blocks[i];
            if (b.kind == BlockKind::Frontmatter || (b.kind == BlockKind::Code && b.lang == u"query"))
                continue;
            cands.append(Candidate{View{&e, int(i), b.task ? &*b.task : nullptr, nullptr}, -1});
        }
        break;
    case QueryType::Reminders:
        for (const int i : e.reminderBlocks()) {
            const Block &b = blocks[i];
            for (qsizetype r = 0; r < b.reminders.size(); ++r)
                cands.append(Candidate{View{&e, i, b.task ? &*b.task : nullptr, &b.reminders[r]}, int(r)});
        }
        break;
    }
    cands.removeIf([&](const Candidate &c) { return !matchesAll(c.view); });
    if (m_query.type == QueryType::Blocks)
        collapseNested(cands); // only ever looks at one note
    if (cands.isEmpty())
        return;
    // Sort values (and name keys) once per match: in the comparator they would cost
    // O(n log n) allocations (tens of thousands of tasks in a big vault).
    std::optional<QString> titleKey; // nameSortKey() of the note's title, computed once per note
    const auto noteTitleKey = [&]() -> const QString & {
        if (!titleKey)
            titleKey = nameSortKey(e.note.title);
        return *titleKey;
    };
    for (const Candidate &c : std::as_const(cands)) {
        QueryMatch m;
        m.path = e.path;
        m.entry = &e;
        m.block = c.view.block;
        m.reminder = c.reminder;
        m.line = c.view.line();
        m.keys.reserve(m_query.sort.size());
        for (const QuerySort &s : m_query.sort) {
            m.keys.append(sortValue(s, c.view));
            if (s.key == QuerySortKey::Page)
                m.keys.last().nameKey = noteTitleKey();
        }
        if (m_query.type == QueryType::Pages) {
            m.title = e.note.title;
            m.titleKey = noteTitleKey();
        }
        if (c.view.reminder)
            m.at = c.view.reminder->at;
        out.append(std::move(m));
    }
}

// ---- filters ------------------------------------------------------------------------------------

bool Runner::matchesAll(const View &view)
{
    return std::ranges::all_of(m_query.filters, [&](const QueryFilter &f) { return test(f, view) != f.negate; });
}

bool Runner::test(const QueryFilter &f, const View &view)
{
    switch (f.kind) {
    case QueryFilterKind::Status: return view.task && f.statuses.contains(view.task->status);
    case QueryFilterKind::Priority: return view.task && f.priorities.contains(view.task->priority);
    case QueryFilterKind::Tag: {
        const QStringList tags = effectiveTags(view);
        return std::ranges::any_of(f.values, [&](const QString &v) {
            return std::ranges::any_of(tags, [&](const QString &t) {
                return t == v || (t.size() > v.size() && t.startsWith(v) && t[v.size()] == u'/');
            });
        });
    }
    case QueryFilterKind::Page:
        return std::ranges::any_of(f.values, [&](const QString &v) { return pageMatches(v, view); });
    case QueryFilterKind::Path:
        return std::ranges::any_of(f.values, [&](const QString &v) { return pathMatches(v, view); });
    case QueryFilterKind::Ref:
        return std::ranges::any_of(f.values, [&](const QString &v) { return refMatches(v, view); });
    case QueryFilterKind::Text:
        return std::ranges::any_of(f.values, [&](const QString &v) { return textMatches(v, view); });
    case QueryFilterKind::Date:
        return std::ranges::any_of(f.dates, [&](const QueryDateCondition &c) { return dateMatches(f.field, c, view); });
    case QueryFilterKind::Property: {
        const QList<PropertyItem> items = propertyItems(view, f.property);
        return std::ranges::any_of(f.conditions, [&](const QueryPropertyCondition &c) {
            if (c.op == QueryPropertyOp::None)
                return items.isEmpty();
            if (c.op == QueryPropertyOp::Any)
                return !items.isEmpty();
            return std::ranges::any_of(items, [&](const PropertyItem &item) { return propertyMatches(c, item); });
        });
    }
    case QueryFilterKind::Is:
        return view.task && std::ranges::any_of(f.values, [&](const QString &v) {
                   if (v == u"blocked")
                       return m_index.isTaskBlocked(view.path(), view.block);
                   if (v == u"blocking")
                       return m_index.isTaskBlocking(view.path(), view.block);
                   if (v == u"running")
                       return view.task->clockStart.isValid();
                   if (v == u"tracked")
                       return view.task->clockedSeconds > 0 || view.task->clockStart.isValid();
                   return false;
               });
    }
    return false;
}

bool Runner::pageMatches(const QString &value, const View &view)
{
    const QString v = value.trimmed();
    const QString fv = folded(v);
    if (isThisPageValue(fv))
        return !m_options.currentPath.isEmpty() && m_options.currentPath == view.path();
    const Entry &e = *view.entry;
    if (v.contains(u'*')) {
        return globMatch(e.foldedPath, fv)
               || std::ranges::any_of(e.names, [&](const NameCandidate &n) { return globMatch(n.folded, fv); });
    }
    return e.foldedPath == fv || std::ranges::find(e.names, fv, &NameCandidate::folded) != e.names.cend()
           || resolveCached(v) == view.path();
}

bool Runner::pathMatches(const QString &value, const View &view)
{
    const QString p = folded(value.trimmed());
    QStringView prefix(p);
    while (prefix.startsWith(u"./"))
        prefix = prefix.sliced(2);
    while (prefix.startsWith(u'/'))
        prefix = prefix.sliced(1);
    const QString fp = text::foldKey(view.path());
    return prefix.contains(u'*') ? globMatch(fp, prefix, true) : fp.startsWith(prefix);
}

bool Runner::refMatches(const QString &value, const View &view)
{
    const QList<Block> &blocks = view.note().blocks;
    const QSet<int> lines = refLines(view.path(), value);
    if (!lines.isEmpty()) {
        if (view.block < 0)
            return true;
        for (int b = view.block; b >= 0; b = blocks[b].parent) {
            for (const int l : lines) {
                if (l >= blocks[b].line && l <= blocks[b].ownEndLine)
                    return true;
            }
        }
    }
    // A tag names a page too (SYNTAX §3): the block's, an ancestor's or — for pages — any tag.
    const QSet<QString> &keys = refTags(value);
    if (keys.isEmpty())
        return false;
    const auto named = [&keys](const QStringList &tags) {
        return std::any_of(tags.cbegin(), tags.cend(), [&keys](const QString &t) { return keys.contains(t); });
    };
    if (view.block < 0)
        return named(view.note().tags);
    for (int b = view.block; b >= 0; b = blocks[b].parent) {
        if (named(blocks[b].tags))
            return true;
    }
    return false;
}

const QSet<QString> &Runner::refTags(const QString &value)
{
    if (const auto it = m_refTags.constFind(value); it != m_refTags.cend())
        return *it;
    QStringList keys;
    if (isThisPageValue(folded(value.trimmed()))) {
        if (!m_options.currentPath.isEmpty())
            keys = m_index.tagsNaming(m_options.currentPath);
    } else if (const std::optional<VaultPath> target = resolveCached(value)) {
        keys = m_index.tagsNaming(*target);
    } else if (const QString name = tagName(cleanTarget(value)); !name.isEmpty()) {
        keys.append(name.toLower()); // no page (yet): the tag of that name
    }
    return *m_refTags.insert(value, QSet<QString>(keys.cbegin(), keys.cend()));
}

bool Runner::textMatches(const QString &value, const View &view)
{
    const QString needle = folded(value);
    if (view.block >= 0)
        return view.entry->foldedBlockTexts().value(view.block).contains(needle);
    return view.entry->foldedTitleAndContent().contains(needle);
}

Runner::DateValues Runner::dateValues(QueryDateField field, const View &view) const
{
    DateValues out;
    const Task *t = view.task;
    const auto addSpec = [&out](const std::optional<DateSpec> &spec) {
        if (spec && spec->isValid())
            out.append(*spec);
    };
    const auto addDate = [&out](std::optional<QDate> date) {
        if (date && date->isValid())
            out.append(DateSpec{*date, std::nullopt});
    };
    switch (field) {
    case QueryDateField::Due: addSpec(t ? t->due : std::nullopt); break;
    case QueryDateField::Scheduled: addSpec(t ? t->scheduled : std::nullopt); break;
    case QueryDateField::Start: addSpec(t ? t->start : std::nullopt); break;
    case QueryDateField::Done: addDate(t ? t->doneDate : std::nullopt); break;
    case QueryDateField::Created: addDate(t ? t->created : std::nullopt); break;
    case QueryDateField::Date: addDate(view.note().journalDate); break;
    case QueryDateField::Remind:
        if (view.reminder) {
            out.append(view.reminder->at);
        } else {
            const QList<Reminder> &list =
                    view.block >= 0 ? view.note().blocks[view.block].reminders : view.note().reminders;
            for (const Reminder &r : list)
                out.append(r.at);
        }
        break;
    }
    return out;
}

bool Runner::isOverdue(QueryDateField field, DateSpec value, const View &view) const
{
    const bool open = !view.task || !isClosed(view.task->status);
    switch (field) {
    case QueryDateField::Remind: {
        if (!open)
            return false;
        // Wall-clock comparison: going through QDateTime would shift times inside a DST gap
        // (02:30 on the spring-forward day becomes 03:30) and compare in UTC.
        if (value.date != m_today)
            return value.date < m_today;
        return value.time.value_or(QTime(0, 0)) < m_options.now.value_or(QTime(0, 0));
    }
    case QueryDateField::Due:
    case QueryDateField::Scheduled:
    case QueryDateField::Start: return open && value.date < m_today;
    default: return value.date < m_today;
    }
}

bool Runner::dateMatches(QueryDateField field, const QueryDateCondition &cond, const View &view)
{
    const auto [cached, added] = m_exprs.tryEmplace(&cond); // the query outlives the run: conditions stay put
    if (added)
        *cached = dates::resolveDateExpr(cond.expr, m_today, m_weekStart);
    if (!cached->has_value())
        return false;
    const dates::DateExpr &expr = **cached;
    const DateValues values = dateValues(field, view);
    if (const auto *special = std::get_if<dates::SpecialDate>(&expr)) {
        switch (*special) {
        case dates::SpecialDate::None: return values.isEmpty();
        case dates::SpecialDate::Any: return !values.isEmpty();
        case dates::SpecialDate::Overdue:
            return std::ranges::any_of(values, [&](DateSpec v) { return isOverdue(field, v, view); });
        }
        return false;
    }
    const auto &range = std::get<dates::DateRange>(expr);
    return std::ranges::any_of(values, [&](DateSpec v) {
        const QDate d = v.date;
        switch (cond.op) {
        case QueryDateOp::Before: return d < range.from;
        case QueryDateOp::OnOrBefore: return d <= range.to;
        case QueryDateOp::After: return d > range.to;
        case QueryDateOp::OnOrAfter: return d >= range.from;
        case QueryDateOp::Within: return range.contains(d);
        }
        return false;
    });
}

QStringList Runner::effectiveTags(const View &view)
{
    if (view.block < 0)
        return view.note().tags;
    const QList<Block> &blocks = view.note().blocks;
    QList<std::optional<QStringList>> &perBlock = m_effectiveTags[view.entry];
    if (perBlock.isEmpty())
        perBlock.resize(blocks.size());
    std::optional<QStringList> &cached = perBlock[view.block];
    if (!cached) {
        QStringList tags = view.entry->pageTags();
        for (int b = view.block; b >= 0; b = blocks[b].parent) {
            for (const QString &t : blocks[b].tags) {
                if (!tags.contains(t))
                    tags.append(t);
            }
        }
        cached = std::move(tags);
    }
    return *cached;
}

std::optional<VaultPath> Runner::resolveCached(const QString &target)
{
    if (const auto it = m_resolved.constFind(target); it != m_resolved.cend())
        return *it;
    return *m_resolved.insert(target, m_index.resolveLink(target, m_options.currentPath));
}

QSet<int> Runner::refLines(const VaultPath &path, const QString &value)
{
    const QString cacheKey = path + QChar(0) + value;
    if (const auto it = m_refLines.constFind(cacheKey); it != m_refLines.cend())
        return *it;
    std::optional<VaultPath> target;
    if (isThisPageValue(folded(value.trimmed()))) {
        if (m_options.currentPath.isEmpty())
            return *m_refLines.insert(cacheKey, {}); // no current page: no link is to `this`
        target = m_options.currentPath;
    } else {
        target = resolveCached(value);
    }
    QSet<int> lines;
    const QString key = linkKey(value);
    const QList<ResolvedLink> links = m_index.outgoingLinks(path);
    for (const ResolvedLink &l : links) {
        const bool byId = l.link.kind == LinkKind::BlockRef || l.link.kind == LinkKind::BlockEmbed;
        const bool hit = target ? l.target == *target
                                : (!byId && l.target.isEmpty() && linkKey(l.link.target, TargetForm::Parsed) == key);
        if (hit)
            lines.insert(l.link.line);
    }
    return *m_refLines.insert(cacheKey, lines);
}

QString Runner::folded(const QString &value)
{
    if (const auto it = m_folded.constFind(value); it != m_folded.cend())
        return *it;
    return *m_folded.insert(value, text::foldKey(value));
}

// ---- properties ---------------------------------------------------------------------------------

const QString &Runner::foldedKey(const QString &key)
{
    if (const auto it = m_foldedKeys.constFind(key); it != m_foldedKeys.cend())
        return *it;
    return *m_foldedKeys.insert(key, queryPropertyKey(key));
}

const QList<Runner::RawProperty> &Runner::pageProperties(const Entry &entry)
{
    if (const auto it = m_pageProps.constFind(&entry); it != m_pageProps.cend())
        return *it;
    QList<RawProperty> out;
    const ParsedNote &note = entry.note;
    const auto add = [&](const QString &key, const QVariant &value, bool logseq) {
        const QString &k = foldedKey(key);
        if (k.isEmpty() || std::ranges::any_of(out, [&k](const RawProperty &p) { return p.key == k; }))
            return;
        out.append(RawProperty{k, key, value, logseq});
    };
    for (auto [key, value] : note.frontmatter.asKeyValueRange())
        add(key, value, false);
    for (auto [key, value] : note.properties.asKeyValueRange())
        add(key, value, true);
    return *m_pageProps.insert(&entry, out);
}

/// The items of a page property's value (tags and aliases by their names).
QList<PropertyItem> itemsOf(const QString &folded, const QVariant &value, bool logseq)
{
    QList<PropertyItem> items;
    if (isNamesKey(folded)) {
        for (const QString &name : markdown::namesFrom(value, folded.startsWith(u"tag"))) {
            PropertyItem item;
            item.text = item.link = name;
            item.folded = text::foldKey(name);
            items.append(item);
        }
    } else {
        addPropertyItems(value, logseq, items);
    }
    return items;
}

QList<PropertyItem> Runner::propertyItems(const View &view, const QString &key, bool *found)
{
    if (found)
        *found = false;
    if (!view.entry)
        return {};
    const QList<Block> &blocks = view.note().blocks;
    for (int b = view.block; b >= 0 && b < blocks.size(); b = blocks[b].parent) {
        for (auto [k, value] : blocks[b].properties.asKeyValueRange()) {
            if (foldedKey(k) != key)
                continue;
            if (found)
                *found = true;
            QList<PropertyItem> items;
            addPropertyItems(value, true, items);
            return items;
        }
    }
    for (const RawProperty &p : pageProperties(*view.entry)) {
        if (p.key == key) {
            if (found)
                *found = true;
            return itemsOf(p.key, p.value, p.logseq);
        }
    }
    return {};
}

const Runner::ConditionValue &Runner::conditionValue(const QueryPropertyCondition &c)
{
    const auto [it, added] = m_conditions.tryEmplace(&c); // the query outlives the run
    if (!added)
        return *it;
    ConditionValue &v = *it;
    const QString value = c.value.trimmed();
    v.folded = text::foldKey(value);
    if (c.quoted)
        return v;
    if (value.startsWith(u"[[") && value.endsWith(u"]]") && value.size() > 4) {
        v.link = value.sliced(2, value.size() - 4).trimmed();
        v.folded = text::foldKey(v.link);
        if (const qsizetype bar = v.link.indexOf(u'|'); bar >= 0)
            v.link = v.link.first(bar).trimmed();
    }
    const QString plain = v.link.isEmpty() ? value : v.link;
    if (const auto n = markdown::parseNumberInput(plain))
        v.number = n->toDouble();
    const QString folded = v.folded;
    if (folded == u"true" || folded == u"yes" || folded == u"tak")
        v.boolean = true;
    else if (folded == u"false" || folded == u"no" || folded == u"nie")
        v.boolean = false;
    if (c.op == QueryPropertyOp::Range) {
        const QString to = c.to.trimmed();
        v.foldedTo = text::foldKey(to);
        if (const auto n = markdown::parseNumberInput(to))
            v.numberTo = n->toDouble();
        v.date = dates::resolveDateExpr(QString(plain + u".."_s + to), m_today, m_weekStart);
    } else {
        v.date = dates::resolveDateExpr(plain, m_today, m_weekStart);
    }
    return v;
}

bool Runner::sameLink(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return false;
    if (folded(a) == folded(b))
        return true;
    const std::optional<VaultPath> x = resolveCached(a);
    return x && x == resolveCached(b);
}

bool Runner::propertyMatches(const QueryPropertyCondition &c, const PropertyItem &item)
{
    const ConditionValue &v = conditionValue(c);
    // A number with a number, a date with a date, else a text with a text (folded).
    const auto numberCompare = [&](double x) -> std::optional<int> {
        if (!v.number)
            return std::nullopt;
        const double y = *v.number;
        return x < y ? -1 : (x > y ? 1 : 0);
    };
    const auto dateRange = [&]() -> const dates::DateRange * {
        return v.date ? std::get_if<dates::DateRange>(&*v.date) : nullptr;
    };
    switch (c.op) {
    case QueryPropertyOp::None:
    case QueryPropertyOp::Any: return false; // (whole-property conditions: test())
    case QueryPropertyOp::Glob:
        return globMatch(item.folded, v.folded) || (!item.link.isEmpty() && globMatch(folded(item.link), v.folded));
    case QueryPropertyOp::Equals: {
        if (c.quoted)
            return item.folded == v.folded || (!item.link.isEmpty() && folded(item.link) == v.folded);
        if (!v.link.isEmpty())
            return item.folded == v.folded || (!item.link.isEmpty() && sameLink(v.link, item.link));
        if (item.boolean && v.boolean)
            return *item.boolean == *v.boolean;
        if (item.number && v.number)
            return numberCompare(*item.number) == 0;
        if (item.date && v.date) {
            if (const auto *special = std::get_if<dates::SpecialDate>(&*v.date))
                return *special == dates::SpecialDate::Overdue ? item.date->date < m_today
                                                               : *special == dates::SpecialDate::Any;
            if (const dates::DateRange *range = dateRange())
                return range->contains(item.date->date);
        }
        if (item.folded == v.folded)
            return true;
        return !item.link.isEmpty() && sameLink(c.value.trimmed(), item.link);
    }
    case QueryPropertyOp::Range: {
        if (item.number && v.number && v.numberTo)
            return *item.number >= *v.number && *item.number <= *v.numberTo;
        if (item.date) {
            const dates::DateRange *range = dateRange();
            return range && range->contains(item.date->date);
        }
        if (item.number || v.number || v.numberTo || v.date)
            return false;
        return QString::compare(item.folded, v.folded) >= 0 && QString::compare(item.folded, v.foldedTo) <= 0;
    }
    default: break;
    }
    // <, <=, >, >=
    int cmp = 0;
    if (item.number && v.number) {
        cmp = *numberCompare(*item.number);
    } else if (item.date && v.date) {
        const dates::DateRange *range = dateRange();
        if (!range)
            return false;
        const QDate d = item.date->date;
        switch (c.op) {
        case QueryPropertyOp::Less: return d < range->from;
        case QueryPropertyOp::LessOrEqual: return d <= range->to;
        case QueryPropertyOp::Greater: return d > range->to;
        case QueryPropertyOp::GreaterOrEqual: return d >= range->from;
        default: return false;
        }
    } else if (!item.number && !item.date && !item.boolean && !v.number && !v.date) {
        cmp = QString::compare(item.folded, v.folded);
    } else {
        return false;
    }
    switch (c.op) {
    case QueryPropertyOp::Less: return cmp < 0;
    case QueryPropertyOp::LessOrEqual: return cmp <= 0;
    case QueryPropertyOp::Greater: return cmp > 0;
    case QueryPropertyOp::GreaterOrEqual: return cmp >= 0;
    default: return false;
    }
}

// ---- sorting and grouping -----------------------------------------------------------------------

SortValue Runner::sortValue(const QuerySort &sort, const View &view)
{
    const QuerySortKey key = sort.key;
    switch (key) {
    case QuerySortKey::Property: {
        // The first value: numbers by value, dates, checkboxes, then texts (as names compare).
        const QList<PropertyItem> items = propertyItems(view, sort.property);
        if (items.isEmpty())
            return SortValue{};
        const PropertyItem &first = items.first();
        SortValue v;
        v.present = true;
        if (first.number) {
            v.number = PropertyNumberRank;
            v.real = *first.number;
        } else if (first.date) {
            v.number = PropertyDateRank;
            v.date = *first.date;
        } else if (first.boolean) {
            v.number = PropertyBoolRank;
            v.real = *first.boolean ? 1 : 0;
        } else {
            v.number = PropertyTextRank;
            v.text = first.link.isEmpty() ? first.text : first.link;
            v.nameKey = nameSortKey(v.text);
        }
        return v;
    }
    case QuerySortKey::Priority:
        return view.task ? SortValue{true, priorityRank(view.task->priority), {}, {}, {}} : SortValue{};
    case QuerySortKey::Status:
        return view.task ? SortValue{true, statusRank(view.task->status), {}, {}, {}} : SortValue{};
    case QuerySortKey::Page: return SortValue{true, 0, view.note().title, {}, {}};
    case QuerySortKey::Path: return SortValue{true, 0, view.path(), {}, {}};
    default: break;
    }
    // The earliest date of the field.
    const DateValues values = dateValues(dateFieldOf(key), view);
    const auto earliest =
            std::ranges::min_element(values, [](DateSpec a, DateSpec b) { return dates::compareDateSpecs(a, b) < 0; });
    return earliest == values.end() ? SortValue{} : SortValue{true, 0, {}, *earliest, {}};
}

int Runner::defaultCompare(const QueryMatch &a, const QueryMatch &b) const
{
    if (m_query.type == QueryType::Pages) {
        const int c = compareNameKeys(a.titleKey, a.title, b.titleKey, b.title);
        return c != 0 ? c : QString::compare(a.path, b.path);
    }
    if (m_query.type == QueryType::Reminders && a.reminder >= 0 && b.reminder >= 0) {
        if (const int c = dates::compareDateSpecs(a.at, b.at); c != 0)
            return c;
    }
    const int c = QString::compare(a.path, b.path);
    return c != 0 ? c : a.line - b.line;
}

bool Runner::less(const QueryMatch &a, const QueryMatch &b) const
{
    const auto compareKey = [](QuerySortKey key, const SortValue &x, const SortValue &y) {
        switch (key) {
        case QuerySortKey::Priority:
        case QuerySortKey::Status: return x.number - y.number;
        case QuerySortKey::Page: return x.text == y.text ? 0 : compareNameKeys(x.nameKey, x.text, y.nameKey, y.text);
        case QuerySortKey::Path: return QString::compare(x.text, y.text);
        case QuerySortKey::Property:
            if (x.number != y.number)
                return x.number - y.number;
            switch (x.number) {
            case PropertyNumberRank:
            case PropertyBoolRank: return x.real < y.real ? -1 : (x.real > y.real ? 1 : 0);
            case PropertyDateRank: return dates::compareDateSpecs(x.date, y.date);
            default: // ("w toku" and "W toku" are one value: the next sort key decides)
                return x.nameKey == y.nameKey || text::foldKey(x.text) == text::foldKey(y.text)
                               ? 0
                               : compareNameKeys(x.nameKey, x.text, y.nameKey, y.text);
            }
        default: return dates::compareDateSpecs(x.date, y.date);
        }
    };
    const qsizetype keyCount = m_query.sort.size();
    for (qsizetype k = 0; k < keyCount; ++k) {
        const SortValue &x = a.keys[k];
        const SortValue &y = b.keys[k];
        if (!x.present && !y.present)
            continue;
        if (x.present != y.present)
            return x.present; // missing values last in both directions
        const QuerySort &s = m_query.sort[k];
        const int c = compareKey(s.key, x, y);
        if (c != 0)
            return s.descending ? c > 0 : c < 0;
    }
    if (const int c = defaultCompare(a, b); c != 0)
        return c < 0;
    // Ties (in one note: defaultCompare() orders by path) keep the document order: what a stable
    // sort of the candidates in the vault's order gives.
    if (a.block != b.block)
        return a.block < b.block;
    return a.reminder < b.reminder;
}

void Runner::sort(QList<QueryMatch> &matches) const
{
    // Through a permutation: a match is too big to move around n log n times.
    QList<qsizetype> order(matches.size());
    std::iota(order.begin(), order.end(), 0);
    std::ranges::sort(order, [&](qsizetype x, qsizetype y) { return less(matches.at(x), matches.at(y)); });
    QList<QueryMatch> sorted;
    sorted.reserve(matches.size());
    for (const qsizetype i : std::as_const(order))
        sorted.append(std::move(matches[i]));
    matches = std::move(sorted);
}

View Runner::viewOf(const QueryMatch &match) const
{
    View view;
    view.entry = m_trustEntries && match.entry ? match.entry : IndexAccess::entry(m_index, match.path);
    if (!view.entry || match.block >= view.entry->note.blocks.size())
        return {}; // the note left the index or changed since the match was found: nothing to read
    view.block = match.block;
    if (match.block >= 0) {
        const Block &b = view.entry->note.blocks[match.block];
        view.task = b.task ? &*b.task : nullptr;
        if (match.reminder >= 0 && match.reminder < b.reminders.size())
            view.reminder = &b.reminders[match.reminder];
    }
    return view;
}

QueryItem Runner::itemOf(const QueryMatch &match) const
{
    QueryItem item{match.path, match.block, std::nullopt, std::nullopt};
    const View view = viewOf(match);
    if (view.task)
        item.task = *view.task;
    if (view.reminder)
        item.reminder = *view.reminder;
    return item;
}

QList<GroupInfo> Runner::groupsOf(const View &view)
{
    if (!view.entry)
        return {}; // a note gone from the index (viewOf()): in no group
    switch (m_query.group) {
    case QueryGroupKey::None: return {};
    case QueryGroupKey::Page: return {groupInfo(view.path(), view.note().title, 0, view.note().title)};
    case QueryGroupKey::Due:
    case QueryGroupKey::Scheduled:
    case QueryGroupKey::Date: {
        const QueryDateField field = m_query.group == QueryGroupKey::Due         ? QueryDateField::Due
                                     : m_query.group == QueryGroupKey::Scheduled ? QueryDateField::Scheduled
                                                                                 : QueryDateField::Date;
        const DateValues values = dateValues(field, view);
        if (values.isEmpty())
            return {groupInfo(QString(), messages::noDate(), 1, QString())};
        const QString day = values.first().date.toString(Qt::ISODate);
        return {groupInfo(day, day, 0, day)};
    }
    case QueryGroupKey::Tag: {
        const QStringList tags = effectiveTags(view);
        if (tags.isEmpty())
            return {groupInfo(QString(), messages::noTag(), 1, QString())};
        QList<GroupInfo> out;
        for (const QString &t : tags)
            out.append(groupInfo(t, u"#"_s + t, 0, t));
        return out;
    }
    case QueryGroupKey::Status:
        if (!view.task)
            return {groupInfo(QString(), messages::noStatus(), 99, QString())};
        return {groupInfo(statusKey(view.task->status), queryStatusLabel(view.task->status),
                          statusRank(view.task->status), QString())};
    case QueryGroupKey::Priority:
        if (!view.task)
            return {groupInfo(QString(), messages::noPriority(), 99, QString())};
        return {groupInfo(priorityKey(view.task->priority), queryPriorityLabel(view.task->priority),
                          priorityRank(view.task->priority), QString())};
    case QueryGroupKey::Property: {
        // A group per value (a list: per item), "No value" last.
        const QList<PropertyItem> items = propertyItems(view, m_query.groupProperty);
        QList<GroupInfo> out;
        QSet<QString> keys;
        for (const PropertyItem &item : items) {
            GroupInfo g;
            g.value = item.text;
            if (!item.link.isEmpty()) {
                const std::optional<VaultPath> target = resolveCached(item.link);
                g.key = target ? *target : u"link:"_s + folded(item.link);
                g.label = target ? paths::pageTitle(*target) : item.link;
                g.rank = PropertyTextRank;
                g.order = g.label;
            } else if (item.number) {
                g.key = u"n:"_s + QString::number(*item.number, 'g', 15);
                g.label = item.text;
                g.rank = PropertyNumberRank;
                g.number = *item.number;
            } else if (item.date) {
                g.key = item.date->toString();
                g.label = item.text;
                g.rank = PropertyDateRank;
                g.order = g.key;
            } else if (item.boolean) {
                g.key = item.folded;
                g.label = item.text;
                g.rank = PropertyBoolRank;
                g.number = *item.boolean ? 1 : 0;
            } else {
                g.key = item.folded;
                g.label = item.text;
                g.rank = PropertyTextRank;
                g.order = item.text;
            }
            if (!keys.contains(g.key)) {
                keys.insert(g.key);
                out.append(g);
            }
        }
        if (out.isEmpty())
            out.append(groupInfo(QString(), messages::noValue(), 99, QString()));
        return out;
    }
    }
    return {};
}

QList<QueryGroup> Runner::buildGroups(const QList<QueryItem> &items, const QList<View> &views)
{
    QList<QueryGroup> groups;
    QList<GroupInfo> infos;
    QHash<QString, qsizetype> byKey;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const QList<GroupInfo> itemGroups = groupsOf(views[i]);
        for (const GroupInfo &g : itemGroups) {
            const auto [at, added] = byKey.tryEmplace(g.key, groups.size());
            if (added) {
                groups.append(QueryGroup{g.key, g.label, {}, g.value});
                infos.append(g);
            }
            groups[*at].items.append(items[i]);
        }
    }
    QList<qsizetype> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    const bool property = m_query.group == QueryGroupKey::Property;
    const bool byName = m_query.group == QueryGroupKey::Page || property;
    QStringList nameKeys;
    if (byName) {
        nameKeys.reserve(infos.size());
        for (const GroupInfo &g : std::as_const(infos))
            nameKeys.append(nameSortKey(g.order));
    }
    std::ranges::stable_sort(order, [&](qsizetype a, qsizetype b) {
        const GroupInfo &x = infos.at(a);
        const GroupInfo &y = infos.at(b);
        if (x.rank != y.rank)
            return x.rank < y.rank;
        if (property && (x.rank == PropertyNumberRank || x.rank == PropertyBoolRank) && x.number != y.number)
            return x.number < y.number;
        const int c = byName && !(property && x.rank == PropertyDateRank)
                              ? compareNameKeys(nameKeys.at(a), x.order, nameKeys.at(b), y.order)
                              : QString::compare(x.order, y.order);
        if (c != 0)
            return c < 0;
        return x.key < y.key;
    });
    QList<QueryGroup> out;
    out.reserve(groups.size());
    for (const qsizetype i : std::as_const(order))
        out.append(std::move(groups[i]));
    return out;
}

void Runner::collapseNested(QList<Candidate> &cands) const
{
    QSet<int> matched;
    for (const Candidate &c : std::as_const(cands))
        matched.insert(c.view.block);
    cands.removeIf([&matched](const Candidate &c) {
        const QList<Block> &blocks = c.view.note().blocks;
        for (int p = blocks[c.view.block].parent; p >= 0; p = blocks[p].parent) {
            if (matched.contains(p))
                return true;
        }
        return false;
    });
}

QueryResult Runner::finish(const QList<QueryMatch> &sorted)
{
    QueryResult result;
    result.type = m_query.type;
    result.total = int(sorted.size());
    const int limit = std::max(0, m_query.limit.value_or(m_options.defaultLimit));
    const qsizetype count = std::min<qsizetype>(limit, sorted.size());
    const bool grouped = m_query.group != QueryGroupKey::None;
    result.items.reserve(count);
    QList<View> views;
    if (grouped)
        views.reserve(count);
    for (qsizetype i = 0; i < count; ++i) {
        result.items.append(itemOf(sorted[i]));
        if (grouped)
            views.append(viewOf(sorted[i]));
    }
    if (grouped)
        result.groups = buildGroups(result.items, views);
    return result;
}

/// Same query (all of it: a cache keyed by the text alone would trust callers to set `source`).
bool sameQuery(const Query &a, const Query &b)
{
    return a.source == b.source && a.type == b.type && a.explicitType == b.explicitType && a.filters == b.filters
           && a.sort == b.sort && a.group == b.group && a.groupProperty == b.groupProperty && a.limit == b.limit
           && a.view == b.view && a.columns == b.columns && a.summaries == b.summaries;
}

bool hasRemindFilter(const Query &query)
{
    return std::ranges::any_of(query.filters, [](const QueryFilter &f) {
        return f.kind == QueryFilterKind::Date && f.field == QueryDateField::Remind;
    });
}

} // namespace

bool queryResolvesNames(const Query &query)
{
    // (Property values may be links: filters compare and groups key them by the page they name.)
    return query.group == QueryGroupKey::Property || std::ranges::any_of(query.filters, [](const QueryFilter &f) {
               return f.kind == QueryFilterKind::Page || f.kind == QueryFilterKind::Ref
                      || f.kind == QueryFilterKind::Property;
           });
}

QueryResult runQuery(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart,
                     const QueryOptions &options)
{
    Runner runner(query, index, today, weekStart, options);
    QList<QueryMatch> matches;
    const QList<const Entry *> entries = IndexAccess::sortedEntries(index);
    for (const Entry *e : entries)
        runner.matchesOf(*e, matches);
    runner.sort(matches);
    return runner.finish(matches);
}

// ---- QueryCache ---------------------------------------------------------------------------------

struct QueryCache::Private {
    IndexFollower follower;
    bool used = false; ///< an update() ran (query, today, … are set)
    Query query;
    QDate today;
    Qt::DayOfWeek weekStart = Qt::Monday;
    QueryOptions options;
    QList<QueryMatch> sorted; ///< every match of the vault, in result order
    NoteCacheSync last;
    quint64 serial = 0; ///< the index (VaultIndex::serial()) and version the matches are up to
    int version = -1;

    /// May the matches' Entry pointers be read with `index`?
    bool current(const VaultIndex &index) const { return index.serial() == serial && index.version() == version; }
};

QueryCache::QueryCache() : d(std::make_unique<Private>()) { }
QueryCache::~QueryCache() = default;
QueryCache::QueryCache(QueryCache &&) noexcept = default;
QueryCache &QueryCache::operator=(QueryCache &&) noexcept = default;

const NoteCacheSync &QueryCache::update(const Query &query, const VaultIndex &index, QDate today,
                                        Qt::DayOfWeek weekStart, const QueryOptions &options)
{
    if (!d->used || !sameQuery(d->query, query))
        d->follower.reset();
    const QString key =
            today.toString(Qt::ISODate) + u'\x1f' + QString::number(int(weekStart)) + u'\x1f' + options.currentPath;
    NoteCacheSync sync = d->follower.follow(index, key, queryResolvesNames(query));
    if (hasRemindFilter(query)) // remind:overdue depends on the time of day
        sync = NoteCacheSync{true, {}};
    d->used = true;
    d->query = query;
    d->today = today;
    d->weekStart = weekStart;
    d->options = options;
    Runner runner(d->query, index, today, weekStart, d->options);
    if (sync.full) {
        d->sorted.clear();
        const QList<const Entry *> entries = IndexAccess::sortedEntries(index);
        for (const Entry *e : entries)
            runner.matchesOf(*e, d->sorted);
        runner.sort(d->sorted);
    } else if (!sync.changed.isEmpty()) {
        const QSet<VaultPath> changed(sync.changed.cbegin(), sync.changed.cend());
        d->sorted.removeIf([&](const QueryMatch &m) { return changed.contains(m.path); });
        QList<QueryMatch> fresh;
        for (const VaultPath &path : std::as_const(sync.changed)) {
            if (const Entry *e = IndexAccess::entry(index, path))
                runner.matchesOf(*e, fresh);
        }
        if (!fresh.isEmpty()) {
            runner.sort(fresh);
            QList<QueryMatch> merged;
            merged.reserve(d->sorted.size() + fresh.size());
            std::merge(std::make_move_iterator(d->sorted.begin()), std::make_move_iterator(d->sorted.end()),
                       std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()),
                       std::back_inserter(merged),
                       [&runner](const QueryMatch &a, const QueryMatch &b) { return runner.less(a, b); });
            d->sorted = std::move(merged);
        }
    }
    d->last = std::move(sync);
    d->serial = index.serial();
    d->version = index.version();
    return d->last;
}

QueryResult QueryCache::result(const VaultIndex &index) const
{
    if (!d->used)
        return {};
    Runner runner(d->query, index, d->today, d->weekStart, d->options);
    runner.trustEntries(d->current(index));
    return runner.finish(d->sorted);
}

QueryResult QueryCache::run(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart,
                            const QueryOptions &options)
{
    update(query, index, today, weekStart, options);
    return result(index);
}

QList<QueryItem> QueryCache::itemsOf(const VaultPath &path, const VaultIndex &index) const
{
    QList<QueryItem> out;
    if (!d->used)
        return out;
    Runner runner(d->query, index, d->today, d->weekStart, d->options);
    runner.trustEntries(d->current(index));
    for (const QueryMatch &m : std::as_const(d->sorted)) {
        if (m.path == path)
            out.append(runner.itemOf(m));
    }
    return out;
}

QList<QueryItem> QueryCache::itemsOf(const QSet<VaultPath> &paths, const VaultIndex &index) const
{
    QList<QueryItem> out;
    if (!d->used || paths.isEmpty())
        return out;
    Runner runner(d->query, index, d->today, d->weekStart, d->options);
    runner.trustEntries(d->current(index));
    for (const QueryMatch &m : std::as_const(d->sorted)) {
        if (paths.contains(m.path))
            out.append(runner.itemOf(m));
    }
    return out;
}

int QueryCache::total() const
{
    return int(d->sorted.size());
}

const NoteCacheSync &QueryCache::lastSync() const
{
    return d->last;
}

void QueryCache::clear()
{
    d = std::make_unique<Private>();
}

} // namespace brulion
