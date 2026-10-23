// The query language of ```query blocks (docs/SYNTAX.md §10): parseQuery() validates the text,
// runQuery() evaluates it against a VaultIndex; findQueryNames() / retargetQueryNames() find and
// rewrite the filter values that name pages, folders and tags (renames and moves). Part of the
// index module — see index/index.h.
//
//   type:tasks status:todo,doing tag:praca -tag:prywatne due:<=+7d sort:due,-priority limit:20
//   "dokładna fraza" słowo #tag [[Strona]] page:this path:Projekty/ ref:[[Jan]] group:page
//
// Syntax: tokens are separated by whitespace or newlines; `"…"` (also „…” “…” «…») and `[[…]]`
// may contain spaces; `//` starts a comment up to the end of the line, and so does `#` followed
// by a space at the start of a line (`# Moje zadania`; `#tag` is a tag filter). `key:value`
// filters accept comma lists (values ORed); all filters are ANDed; a leading `-` negates a
// filter or excludes a word.
// Bare words and quoted phrases search the text (case- and diacritic-insensitive); `#tag` is
// `tag:tag`; `[[Page]]` is `ref:Page`. Keys and values also have Polish spellings (`termin:`,
// `status:otwarte`, `typ:zadania`, `sortuj:`, …).
//
// Filters: type (tasks|blocks|pages|reminders; default tasks when a task filter, sort or group
// is used, reminders when `remind:` is, else blocks), status (todo, doing, done, cancelled,
// deferred, open = todo+doing, closed = done+cancelled, all), priority (highest … lowest, none),
// tag (`a` also matches `a/b`; block tags incl. ancestors' and page tags), page (title, path,
// alias or link target; `*` wildcards; `this` = the page containing the query), path (prefix;
// `*` wildcards), ref (items linking to a page), text, and the date fields due, scheduled,
// start, done, created, remind and date (journal date of the page) with an optional `<`, `<=`,
// `>`, `>=` before a dates::resolveDateExpr() expression (`today`, `+7d`, `this-week`,
// `2026-10-15`, `today..+7d`, `overdue`, `none`, `any`). sort: due, scheduled, start, done,
// created, remind, date, priority, status, page, path (`-key` descending; missing values last).
// group: page, due, scheduled, date, tag, status, priority. limit: 1…MaxQueryLimit.
#pragma once

#include "index/vaultindex.h"
#include "types.h"

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTime>

#include <optional>
#include <variant>

namespace brulion {

/// Largest accepted `limit:`.
inline constexpr int MaxQueryLimit = 10000;
/// Items returned when the query has no `limit:`.
inline constexpr int DefaultQueryLimit = 500;

/// What a query returns.
enum class QueryType { Tasks, Blocks, Pages, Reminders };

/// Date fields: task dates, reminder times (`remind`) and the journal date of the page (`date`).
enum class QueryDateField { Due, Scheduled, Start, Done, Created, Remind, Date };

/// Comparison written before a date expression; Within (no operator) = inside the range.
enum class QueryDateOp { Within, Before, OnOrBefore, After, OnOrAfter };

/// One date condition: operator + expression understood by dates::resolveDateExpr().
struct QueryDateCondition {
    QueryDateOp op = QueryDateOp::Within;
    QString expr;

    friend bool operator==(const QueryDateCondition &, const QueryDateCondition &) = default;
};

enum class QueryFilterKind { Status, Priority, Tag, Page, Path, Ref, Text, Date };

/// A filter. Values of one filter are ORed; `negate` (written `-key:`) inverts the filter.
struct QueryFilter {
    QueryFilterKind kind = QueryFilterKind::Text;
    bool negate = false;
    /// Tag (lower-case NFC, no '#'), Page, Path, Ref and Text values.
    QStringList values;
    QList<TaskStatus> statuses;                 ///< Status
    QList<Priority> priorities;                 ///< Priority (Priority::None = no priority set)
    QueryDateField field = QueryDateField::Due; ///< Date
    QList<QueryDateCondition> dates;            ///< Date

    friend bool operator==(const QueryFilter &, const QueryFilter &) = default;
};

enum class QuerySortKey { Due, Scheduled, Start, Done, Created, Remind, Date, Priority, Status, Page, Path };

struct QuerySort {
    QuerySortKey key = QuerySortKey::Due;
    bool descending = false; ///< written `-key`; missing values stay last either way

    friend bool operator==(const QuerySort &, const QuerySort &) = default;
};

enum class QueryGroupKey { None, Page, Due, Scheduled, Date, Tag, Status, Priority };

/// A validated query.
struct Query {
    QString source; ///< the query text as written
    QueryType type = QueryType::Blocks;
    bool explicitType = false; ///< written `type:`; otherwise inferred
    QList<QueryFilter> filters;
    QList<QuerySort> sort;
    QueryGroupKey group = QueryGroupKey::None;
    std::optional<int> limit;
};

/// Parse and validate a query (the body of a ```query block). Returns the Query, or a
/// translated, helpful error message (QCoreApplication::translate context "brulion::Query").
std::variant<Query, QString> parseQuery(const QString &source);

/// One result item. Pages have blockIndex -1.
struct QueryItem {
    VaultPath path;
    int blockIndex = -1;
    std::optional<Task> task;         ///< tasks; blocks/reminders whose block is a task
    std::optional<Reminder> reminder; ///< reminders
};

/// Items of one group (`group:` queries), in result order.
struct QueryGroup {
    QString key;   ///< path, ISO date, tag, status / priority name; empty for "none"
    QString label; ///< display text (translated for "none", statuses and priorities)
    QList<QueryItem> items;
};

struct QueryResult {
    QueryType type = QueryType::Blocks;
    QList<QueryItem> items;   ///< matches after sorting and `limit`
    QList<QueryGroup> groups; ///< for `group:` queries (built from `items`; a tag group per tag)
    int total = 0;            ///< number of matches before `limit`
};

/// Extra context for runQuery().
struct QueryOptions {
    /// Page containing the query block: `page:this` and relative `ref:` targets use it.
    VaultPath currentPath;
    /// Current wall-clock time: `remind:overdue` = reminders before today at this time
    /// (default: before today).
    std::optional<QTime> now;
    /// Items returned without `limit:`.
    int defaultLimit = DefaultQueryLimit;
};

/// Run a parsed query against the index. `today` defines `today`, `overdue` etc.; `weekStart`
/// defines `this-week`. Semantics: block tags include their ancestors' tags and the page tags;
/// dates compare by day; `overdue` = before today on an open task (done/cancelled never are);
/// `none` = missing, `any` = present; missing dates never satisfy a range. `type:blocks` drops
/// blocks whose ancestor also matched (shown inside it), frontmatter and ```query blocks.
/// Default order: tasks/blocks by path then line, reminders by time, pages by title.
QueryResult runQuery(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart = Qt::Monday,
                     const QueryOptions &options = {});

/// Translated group/status labels (also handy for UIs).
QString queryStatusLabel(TaskStatus status);
QString queryPriorityLabel(Priority priority);

// ---- Names in query blocks (rename and move, docs/SYNTAX.md §2 "Rewriting on rename and move") ----

/// A filter value of a ```query block that names a page, a folder or a tag: what renaming or
/// moving them rewrites (VaultIndex::planRelocation()). `tag:` and `#tag` (Tag), `ref:` and
/// `[[Page]]` (Ref), `page:` (Page), `path:` (Path) — any spelling of the key, negated or not,
/// each value of a comma list.
struct QueryNameRef {
    QueryFilterKind kind = QueryFilterKind::Ref;
    /// As written, without quotes, `[[ ]]`, a tag's '#' or the blanks inside them ("Dom",
    /// "Projekty/", "praca", "Jan Kowalski"); not lower-cased.
    QString value;
    qsizetype from = 0; ///< `value`'s range in the note
    qsizetype to = 0;
    bool bracketed = false; ///< written in `[[ ]]` (`ref:[[Jan Kowalski]]`, `[[Dom]]`, `#[[Mój tag]]`)
    QChar quote;            ///< the opening quote when written in quotes (`page:"Lista zakupów"`), else null

    friend bool operator==(const QueryNameRef &, const QueryNameRef &) = default;
};

/// Every QueryNameRef of the ```query blocks of a note, in document order — read as parseQuery()
/// reads the block (comments, quotes and `[[…]]` with spaces; a `//` value is text). Never outside
/// a ```query fence (its own lines; the fence lines and other code are not looked at).
QList<QueryNameRef> findQueryNames(const QString &content);

/// How `value` is written in place of `name.value` (the text for [from, to)), in the name's own
/// form: inside its `[[ ]]` or quotes, or bare — in `[[ ]]` when it holds a blank, a comma, a
/// quote, `[[` / `]]` or starts with `//`, so the query keeps reading it as one value. "" when no
/// form can hold it (empty, a line break, `[[` / `]]` in brackets, its closing quote in quotes).
QString formatQueryName(const QueryNameRef &name, const QString &value);

/// One value to replace (retargetQueryNames()).
struct QueryNameEdit {
    qsizetype from = 0; ///< a QueryNameRef's range (findQueryNames() of the same content)
    qsizetype to = 0;
    QString value; ///< the new value, unformatted ("Archiwum/Dom", "Nowa nazwa")

    friend bool operator==(const QueryNameEdit &, const QueryNameEdit &) = default;
};

/// Replace filter values of ```query blocks (content in → content out): each edit names a value
/// by its range and is written by formatQueryName(). Edits that match no value, overlap an
/// earlier one or cannot be written are ignored (`applied`: how many were written); line endings
/// and every other byte are preserved, and content without changes is returned as is.
QString retargetQueryNames(const QString &content, const QList<QueryNameEdit> &edits, int *applied = nullptr);

} // namespace brulion
