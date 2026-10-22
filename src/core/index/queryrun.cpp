// Query execution (docs/SYNTAX.md §10) over a VaultIndex — see index/query.h for the semantics.
#include "index/query.h"

#include "dates/expr.h"
#include "dates/local.h"
#include "index/querymessages_p.h"
#include "index/resolve_p.h"
#include "index/textutil_p.h"
#include "index/vaultindex_p.h"
#include "text/text.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>
#include <numeric>

namespace brulion {

using namespace Qt::StringLiterals;
using namespace index_detail;

namespace {

bool isThisPage(const QString &folded)
{
    static const QSet<QString> words = {u"this"_s,    u"current"_s, u"ta"_s,   u"ten"_s,
                                        u"biezaca"_s, u"biezacy"_s, u"tutaj"_s};
    return words.contains(folded);
}

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

struct Candidate {
    QueryItem item;
    View view;
};

/// Sort value of an item (`present` false = missing, sorts last).
struct SortValue {
    bool present = false;
    int number = 0;
    QString text;
    DateSpec date;
    QString nameKey; ///< nameSortKey(text) for QuerySortKey::Page (set by sortCandidates)
};

struct GroupInfo {
    QString key;
    QString label;
    int rank = 0;
    QString order;
};

/// Per-note data computed once per run.
struct NoteMeta {
    QStringList pageTags;
    QList<std::optional<QStringList>> effective; ///< per block
};

class Runner {
public:
    Runner(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart,
           const QueryOptions &options)
        : m_query(query), m_index(index), m_today(today), m_weekStart(weekStart), m_options(options)
    { }

    QueryResult run();

private:
    QList<Candidate> candidates() const;
    bool matchesAll(const View &view);
    bool test(const QueryFilter &f, const View &view);
    bool pageMatches(const QString &value, const View &view);
    bool pathMatches(const QString &value, const View &view);
    bool refMatches(const QString &value, const View &view);
    bool textMatches(const QString &value, const View &view);
    bool dateMatches(QueryDateField field, const QueryDateCondition &cond, const View &view);
    bool isOverdue(QueryDateField field, const DateSpec &value, const View &view) const;
    QList<DateSpec> dateValues(QueryDateField field, const View &view) const;
    QStringList effectiveTags(const View &view);
    NoteMeta &meta(const Entry *entry);
    QSet<int> refLines(const VaultPath &path, const QString &value);
    /// The tag keys that name the page `ref:value` means.
    const QSet<QString> &refTags(const QString &value);
    std::optional<VaultPath> resolveCached(const QString &target);
    QString folded(const QString &value);
    void collapseNested(QList<Candidate> &cands) const;
    void sortCandidates(QList<Candidate> &cands) const;
    SortValue sortValue(QuerySortKey key, const View &view) const;
    /// `keyA` / `keyB`: nameSortKey() of the titles (used by page queries only).
    int defaultCompare(const View &a, const QString &keyA, const View &b, const QString &keyB) const;
    QList<GroupInfo> groupsOf(const View &view);
    QList<QueryGroup> buildGroups(const QList<Candidate> &cands);

    const Query &m_query;
    const VaultIndex &m_index;
    QDate m_today;
    Qt::DayOfWeek m_weekStart;
    const QueryOptions &m_options;
    QHash<const Entry *, NoteMeta> m_meta;
    QHash<QString, std::optional<dates::DateExpr>> m_exprs;
    QHash<QString, std::optional<VaultPath>> m_resolved;
    QHash<QString, QSet<int>> m_refLines;
    QHash<QString, QSet<QString>> m_refTags;
    QHash<QString, QString> m_folded;
};

// ---- candidates ---------------------------------------------------------------------------------

QList<Candidate> Runner::candidates() const
{
    QList<Candidate> out;
    for (const VaultPath &path : m_index.paths()) {
        const Entry *e = IndexAccess::entry(m_index, path);
        if (!e)
            continue;
        const QList<Block> &blocks = e->note.blocks;
        switch (m_query.type) {
        case QueryType::Pages:
            out.append(Candidate{QueryItem{path, -1, std::nullopt, std::nullopt}, View{e, -1, nullptr, nullptr}});
            break;
        case QueryType::Tasks:
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                if (const auto &task = blocks[i].task)
                    out.append(
                            Candidate{QueryItem{path, int(i), task, std::nullopt}, View{e, int(i), &*task, nullptr}});
            }
            break;
        case QueryType::Blocks:
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                const Block &b = blocks[i];
                if (b.kind == BlockKind::Frontmatter || (b.kind == BlockKind::Code && b.lang == u"query"))
                    continue;
                out.append(Candidate{QueryItem{path, int(i), b.task, std::nullopt},
                                     View{e, int(i), b.task ? &*b.task : nullptr, nullptr}});
            }
            break;
        case QueryType::Reminders:
            for (qsizetype i = 0; i < blocks.size(); ++i) {
                const Block &b = blocks[i];
                for (const Reminder &r : b.reminders) {
                    out.append(Candidate{QueryItem{path, int(i), b.task, r},
                                         View{e, int(i), b.task ? &*b.task : nullptr, &r}});
                }
            }
            break;
        }
    }
    return out;
}

// ---- filters ------------------------------------------------------------------------------------

bool Runner::matchesAll(const View &view)
{
    for (const QueryFilter &f : m_query.filters) {
        if (test(f, view) == f.negate)
            return false;
    }
    return true;
}

bool Runner::test(const QueryFilter &f, const View &view)
{
    switch (f.kind) {
    case QueryFilterKind::Status: return view.task && f.statuses.contains(view.task->status);
    case QueryFilterKind::Priority: return view.task && f.priorities.contains(view.task->priority);
    case QueryFilterKind::Tag: {
        const QStringList tags = effectiveTags(view);
        return std::any_of(f.values.cbegin(), f.values.cend(), [&](const QString &v) {
            return std::any_of(tags.cbegin(), tags.cend(), [&](const QString &t) {
                return t == v || (t.size() > v.size() && t.startsWith(v) && t[v.size()] == u'/');
            });
        });
    }
    case QueryFilterKind::Page:
        return std::any_of(f.values.cbegin(), f.values.cend(), [&](const QString &v) { return pageMatches(v, view); });
    case QueryFilterKind::Path:
        return std::any_of(f.values.cbegin(), f.values.cend(), [&](const QString &v) { return pathMatches(v, view); });
    case QueryFilterKind::Ref:
        return std::any_of(f.values.cbegin(), f.values.cend(), [&](const QString &v) { return refMatches(v, view); });
    case QueryFilterKind::Text:
        return std::any_of(f.values.cbegin(), f.values.cend(), [&](const QString &v) { return textMatches(v, view); });
    case QueryFilterKind::Date:
        return std::any_of(f.dates.cbegin(), f.dates.cend(),
                           [&](const QueryDateCondition &c) { return dateMatches(f.field, c, view); });
    }
    return false;
}

bool Runner::pageMatches(const QString &value, const View &view)
{
    const QString v = value.trimmed();
    const QString fv = folded(v);
    if (isThisPage(fv))
        return !m_options.currentPath.isEmpty() && m_options.currentPath == view.path();
    const Entry &e = *view.entry;
    if (v.contains(u'*')) {
        if (globMatch(e.foldedPath, fv))
            return true;
        return std::any_of(e.names.cbegin(), e.names.cend(),
                           [&](const NameCandidate &n) { return globMatch(n.folded, fv); });
    }
    if (e.foldedPath == fv
        || std::any_of(e.names.cbegin(), e.names.cend(), [&](const NameCandidate &n) { return n.folded == fv; }))
        return true;
    return resolveCached(v) == view.path();
}

bool Runner::pathMatches(const QString &value, const View &view)
{
    QString p = folded(value.trimmed());
    while (p.startsWith(u"./"))
        p.remove(0, 2);
    while (p.startsWith(u'/'))
        p.remove(0, 1);
    const QString fp = text::foldKey(view.path());
    return p.contains(u'*') ? globMatch(fp, p, true) : fp.startsWith(p);
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
    auto it = m_refTags.find(value);
    if (it != m_refTags.end())
        return *it;
    QSet<QString> keys;
    const auto add = [&keys](const QStringList &list) {
        for (const QString &k : list)
            keys.insert(k);
    };
    if (isThisPage(folded(value.trimmed()))) {
        if (!m_options.currentPath.isEmpty())
            add(IndexAccess::tagKeysNaming(m_index, m_options.currentPath));
    } else if (const std::optional<VaultPath> target = resolveCached(value)) {
        add(IndexAccess::tagKeysNaming(m_index, *target));
    } else if (const QString name = tagName(cleanTarget(value)); !name.isEmpty()) {
        keys.insert(name.toLower()); // no page (yet): the tag of that name
    }
    return *m_refTags.insert(value, keys);
}

bool Runner::textMatches(const QString &value, const View &view)
{
    const QString needle = folded(value);
    if (view.block >= 0)
        return view.entry->foldedBlockTexts().value(view.block).contains(needle);
    return view.entry->foldedTitleAndContent().contains(needle);
}

QList<DateSpec> Runner::dateValues(QueryDateField field, const View &view) const
{
    QList<DateSpec> out;
    const Task *t = view.task;
    auto addSpec = [&](const std::optional<DateSpec> &spec) {
        if (spec && spec->isValid())
            out.append(*spec);
    };
    auto addDate = [&](const std::optional<QDate> &date) {
        if (date && date->isValid())
            out.append(DateSpec{*date, std::nullopt});
    };
    switch (field) {
    case QueryDateField::Due:
        if (t)
            addSpec(t->due);
        break;
    case QueryDateField::Scheduled:
        if (t)
            addSpec(t->scheduled);
        break;
    case QueryDateField::Start:
        if (t)
            addSpec(t->start);
        break;
    case QueryDateField::Done:
        if (t)
            addDate(t->doneDate);
        break;
    case QueryDateField::Created:
        if (t)
            addDate(t->created);
        break;
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

bool Runner::isOverdue(QueryDateField field, const DateSpec &value, const View &view) const
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
    auto cached = m_exprs.find(cond.expr);
    if (cached == m_exprs.end())
        cached = m_exprs.insert(cond.expr, dates::resolveDateExpr(cond.expr, m_today, m_weekStart));
    if (!cached->has_value())
        return false;
    const dates::DateExpr &expr = **cached;
    const QList<DateSpec> values = dateValues(field, view);
    if (const auto *special = std::get_if<dates::SpecialDate>(&expr)) {
        switch (*special) {
        case dates::SpecialDate::None: return values.isEmpty();
        case dates::SpecialDate::Any: return !values.isEmpty();
        case dates::SpecialDate::Overdue:
            return std::any_of(values.cbegin(), values.cend(),
                               [&](const DateSpec &v) { return isOverdue(field, v, view); });
        }
        return false;
    }
    const auto &range = std::get<dates::DateRange>(expr);
    return std::any_of(values.cbegin(), values.cend(), [&](const DateSpec &v) {
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

NoteMeta &Runner::meta(const Entry *entry)
{
    auto it = m_meta.find(entry);
    if (it != m_meta.end())
        return *it;
    const ParsedNote &note = entry->note;
    NoteMeta m;
    m.pageTags = pageLevelTags(note);
    m.effective.resize(note.blocks.size());
    return *m_meta.insert(entry, std::move(m));
}

QStringList Runner::effectiveTags(const View &view)
{
    if (view.block < 0)
        return view.note().tags;
    NoteMeta &m = meta(view.entry);
    std::optional<QStringList> &cached = m.effective[view.block];
    if (!cached) {
        QStringList tags = m.pageTags;
        const QList<Block> &blocks = view.note().blocks;
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
    auto it = m_resolved.find(target);
    if (it == m_resolved.end())
        it = m_resolved.insert(target, m_index.resolveLink(target, m_options.currentPath));
    return *it;
}

QSet<int> Runner::refLines(const VaultPath &path, const QString &value)
{
    const QString cacheKey = path + QChar(0) + value;
    auto it = m_refLines.find(cacheKey);
    if (it != m_refLines.end())
        return *it;
    QSet<int> lines;
    std::optional<VaultPath> target;
    if (isThisPage(folded(value.trimmed()))) {
        if (!m_options.currentPath.isEmpty())
            target = m_options.currentPath;
    } else {
        target = resolveCached(value);
    }
    const QString key = linkKey(value);
    for (const ResolvedLink &l : m_index.outgoingLinks(path)) {
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
    auto it = m_folded.find(value);
    if (it == m_folded.end())
        it = m_folded.insert(value, text::foldKey(value));
    return *it;
}

// ---- sorting and grouping -----------------------------------------------------------------------

SortValue Runner::sortValue(QuerySortKey key, const View &view) const
{
    SortValue v;
    switch (key) {
    case QuerySortKey::Priority:
        if (view.task)
            v = SortValue{true, priorityRank(view.task->priority), {}, {}, {}};
        return v;
    case QuerySortKey::Status:
        if (view.task)
            v = SortValue{true, statusRank(view.task->status), {}, {}, {}};
        return v;
    case QuerySortKey::Page: return SortValue{true, 0, view.note().title, {}, {}};
    case QuerySortKey::Path: return SortValue{true, 0, view.path(), {}, {}};
    default: break;
    }
    const QList<DateSpec> values = dateValues(dateFieldOf(key), view);
    for (const DateSpec &d : values) {
        if (!v.present || dates::compareDateSpecs(d, v.date) < 0) {
            v.present = true;
            v.date = d;
        }
    }
    return v;
}

int Runner::defaultCompare(const View &a, const QString &keyA, const View &b, const QString &keyB) const
{
    if (m_query.type == QueryType::Pages) {
        if (const int c = compareNameKeys(keyA, a.note().title, keyB, b.note().title); c != 0)
            return c;
        return a.path() < b.path() ? -1 : (b.path() < a.path() ? 1 : 0);
    }
    if (m_query.type == QueryType::Reminders && a.reminder && b.reminder) {
        if (const int c = dates::compareDateSpecs(a.reminder->at, b.reminder->at); c != 0)
            return c;
    }
    if (a.path() != b.path())
        return a.path() < b.path() ? -1 : 1;
    return a.line() - b.line();
}

void Runner::sortCandidates(QList<Candidate> &cands) const
{
    auto compareKey = [](QuerySortKey key, const SortValue &a, const SortValue &b) {
        switch (key) {
        case QuerySortKey::Priority:
        case QuerySortKey::Status: return a.number - b.number;
        case QuerySortKey::Page: return a.text == b.text ? 0 : compareNameKeys(a.nameKey, a.text, b.nameKey, b.text);
        case QuerySortKey::Path: return a.text < b.text ? -1 : (b.text < a.text ? 1 : 0);
        default: return dates::compareDateSpecs(a.date, b.date);
        }
    };
    // Sort values (and the name keys of compareNames()) are computed once per item: in the
    // comparator they would cost O(n log n) allocations (tens of thousands of tasks in a big
    // vault).
    const qsizetype keyCount = m_query.sort.size();
    QList<SortValue> values;
    values.reserve(cands.size() * keyCount);
    for (const Candidate &c : std::as_const(cands)) {
        for (const QuerySort &s : m_query.sort) {
            values.append(sortValue(s.key, c.view));
            if (s.key == QuerySortKey::Page)
                values.last().nameKey = nameSortKey(values.last().text);
        }
    }
    QList<QString> titleKeys;
    if (m_query.type == QueryType::Pages) {
        titleKeys.reserve(cands.size());
        for (const Candidate &c : std::as_const(cands))
            titleKeys.append(nameSortKey(c.view.note().title));
    }
    const auto titleKey = [&](qsizetype i) -> const QString & {
        static const QString none;
        return titleKeys.isEmpty() ? none : titleKeys[i];
    };
    QList<qsizetype> order(cands.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](qsizetype x, qsizetype y) {
        for (qsizetype k = 0; k < keyCount; ++k) {
            const SortValue &a = values[x * keyCount + k];
            const SortValue &b = values[y * keyCount + k];
            if (!a.present && !b.present)
                continue;
            if (a.present != b.present)
                return a.present; // missing values last in both directions
            const QuerySort &s = m_query.sort[k];
            const int c = compareKey(s.key, a, b);
            if (c != 0)
                return s.descending ? c > 0 : c < 0;
        }
        return defaultCompare(cands.at(x).view, titleKey(x), cands.at(y).view, titleKey(y)) < 0;
    });
    QList<Candidate> sorted;
    sorted.reserve(cands.size());
    for (const qsizetype i : std::as_const(order))
        sorted.append(std::move(cands[i]));
    cands = std::move(sorted);
}

QList<GroupInfo> Runner::groupsOf(const View &view)
{
    switch (m_query.group) {
    case QueryGroupKey::None: return {};
    case QueryGroupKey::Page: return {GroupInfo{view.path(), view.note().title, 0, view.note().title}};
    case QueryGroupKey::Due:
    case QueryGroupKey::Scheduled:
    case QueryGroupKey::Date: {
        const QueryDateField field = m_query.group == QueryGroupKey::Due         ? QueryDateField::Due
                                     : m_query.group == QueryGroupKey::Scheduled ? QueryDateField::Scheduled
                                                                                 : QueryDateField::Date;
        const QList<DateSpec> values = dateValues(field, view);
        if (values.isEmpty())
            return {GroupInfo{QString(), messages::noDate(), 1, QString()}};
        const QString day = values.first().date.toString(Qt::ISODate);
        return {GroupInfo{day, day, 0, day}};
    }
    case QueryGroupKey::Tag: {
        const QStringList tags = effectiveTags(view);
        if (tags.isEmpty())
            return {GroupInfo{QString(), messages::noTag(), 1, QString()}};
        QList<GroupInfo> out;
        for (const QString &t : tags)
            out.append(GroupInfo{t, u"#"_s + t, 0, t});
        return out;
    }
    case QueryGroupKey::Status:
        if (!view.task)
            return {GroupInfo{QString(), messages::noStatus(), 99, QString()}};
        return {GroupInfo{statusKey(view.task->status), queryStatusLabel(view.task->status),
                          statusRank(view.task->status), QString()}};
    case QueryGroupKey::Priority:
        if (!view.task)
            return {GroupInfo{QString(), messages::noPriority(), 99, QString()}};
        return {GroupInfo{priorityKey(view.task->priority), queryPriorityLabel(view.task->priority),
                          priorityRank(view.task->priority), QString()}};
    }
    return {};
}

QList<QueryGroup> Runner::buildGroups(const QList<Candidate> &cands)
{
    QList<QueryGroup> groups;
    QList<GroupInfo> infos;
    QHash<QString, qsizetype> byKey;
    for (const Candidate &c : cands) {
        for (const GroupInfo &g : groupsOf(c.view)) {
            auto it = byKey.constFind(g.key);
            if (it == byKey.constEnd()) {
                it = byKey.insert(g.key, groups.size());
                groups.append(QueryGroup{g.key, g.label, {}});
                infos.append(g);
            }
            groups[*it].items.append(c.item);
        }
    }
    QList<qsizetype> order(groups.size());
    std::iota(order.begin(), order.end(), 0);
    const bool byName = m_query.group == QueryGroupKey::Page;
    QList<QString> nameKeys;
    if (byName) {
        nameKeys.reserve(infos.size());
        for (const GroupInfo &g : std::as_const(infos))
            nameKeys.append(nameSortKey(g.order));
    }
    std::stable_sort(order.begin(), order.end(), [&](qsizetype a, qsizetype b) {
        const GroupInfo &x = infos[a];
        const GroupInfo &y = infos[b];
        if (x.rank != y.rank)
            return x.rank < y.rank;
        const int c = byName ? compareNameKeys(nameKeys[a], x.order, nameKeys[b], y.order)
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
    QHash<const Entry *, QSet<int>> matched;
    for (const Candidate &c : std::as_const(cands))
        matched[c.view.entry].insert(c.view.block);
    cands.removeIf([&](const Candidate &c) {
        const QSet<int> inNote = matched.value(c.view.entry);
        const QList<Block> &blocks = c.view.note().blocks;
        for (int p = blocks[c.view.block].parent; p >= 0; p = blocks[p].parent) {
            if (inNote.contains(p))
                return true;
        }
        return false;
    });
}

QueryResult Runner::run()
{
    QueryResult result;
    result.type = m_query.type;
    QList<Candidate> cands = candidates();
    cands.removeIf([&](const Candidate &c) { return !matchesAll(c.view); });
    if (m_query.type == QueryType::Blocks)
        collapseNested(cands);
    sortCandidates(cands);
    result.total = int(cands.size());
    const int limit = std::max(0, m_query.limit.value_or(m_options.defaultLimit));
    if (cands.size() > limit)
        cands.resize(limit);
    result.items.reserve(cands.size());
    for (const Candidate &c : std::as_const(cands))
        result.items.append(c.item);
    if (m_query.group != QueryGroupKey::None)
        result.groups = buildGroups(cands);
    return result;
}

} // namespace

QueryResult runQuery(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart,
                     const QueryOptions &options)
{
    return Runner(query, index, today, weekStart, options).run();
}

} // namespace brulion
