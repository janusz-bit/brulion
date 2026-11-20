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
// `2026-10-15`, `today..+7d`, `overdue`, `none`, `any`), and is (blocked, blocking: task
// dependencies `🆔` / `⛔`; running, tracked: a `:LOGBOOK:` clock running / any clocked time —
// SYNTAX §5). sort: due, scheduled, start, done,
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

enum class QueryFilterKind { Status, Priority, Tag, Page, Path, Ref, Text, Date, Is, Property };

/// How a property filter compares (`key::value`, `key::>value`, `key::a..b`, `key::none`, …).
enum class QueryPropertyOp { Equals, Less, LessOrEqual, Greater, GreaterOrEqual, Range, Glob, None, Any };

/// One condition of a property filter (`koszt::>100`). The value is typed when the filter runs,
/// by the property's value it meets: a number compares with numbers, a date expression with dates,
/// a `[[link]]` with links (the page it resolves to), anything else as text (folded).
struct QueryPropertyCondition {
    QueryPropertyOp op = QueryPropertyOp::Equals;
    QString value;       ///< as written, unquoted (a link keeps its `[[ ]]`); Range: the lower end
    QString to;          ///< Range: the upper end
    bool quoted = false; ///< written in quotes: a text, never a keyword, number, date or pattern

    friend bool operator==(const QueryPropertyCondition &, const QueryPropertyCondition &) = default;
};

/// A filter. Values of one filter are ORed; `negate` (written `-key:`) inverts the filter.
struct QueryFilter {
    QueryFilterKind kind = QueryFilterKind::Text;
    bool negate = false;
    /// Tag (lower-case NFC, no '#'), Page, Path, Ref and Text values; Is: "blocked", "blocking",
    /// "running", "tracked".
    QStringList values;
    QList<TaskStatus> statuses;                 ///< Status
    QList<Priority> priorities;                 ///< Priority (Priority::None = no priority set)
    QueryDateField field = QueryDateField::Due; ///< Date
    QList<QueryDateCondition> dates;            ///< Date
    QString property;                           ///< Property: the key folded (queryPropertyKey())
    QString propertyName;                       ///< Property: the key as written
    QList<QueryPropertyCondition> conditions;   ///< Property

    friend bool operator==(const QueryFilter &, const QueryFilter &) = default;
};

enum class QuerySortKey { Due, Scheduled, Start, Done, Created, Remind, Date, Priority, Status, Page, Path, Property };

struct QuerySort {
    QuerySortKey key = QuerySortKey::Due;
    bool descending = false; ///< written `-key`; missing values stay last either way
    QString property;        ///< Property: the key folded (queryPropertyKey())
    QString name;            ///< as written (without '-' and a trailing `::`)

    friend bool operator==(const QuerySort &, const QuerySort &) = default;
};

enum class QueryGroupKey { None, Page, Due, Scheduled, Date, Tag, Status, Priority, Property };

/// How a query's results are shown (`view:`); None: the plain list of a ```query block.
enum class QueryView { None, List, Table, Board, Gallery, Calendar };

/// What a column (`columns:`) shows. Page columns read the row's page; task columns its task (a
/// page row reads a property of that name instead); Property a property (as filters read it);
/// Formula an expression (Dataview syntax) the views module evaluates.
enum class QueryColumnKind {
    Title,    ///< the page (a link)
    Path,     ///< the file's path
    Folder,   ///< its folder
    Tags,     ///< the page's (a block row: the block's) tags
    Modified, ///< the file's modification time
    Size,     ///< the file's size
    Date,     ///< the journal date of the page
    Text,     ///< a block row's text (a page row: its title)
    Status,
    Priority,
    Due,
    Scheduled,
    Start,
    Done,
    Created,
    Property,
    Formula,
};

/// One column of a view.
struct QueryColumn {
    QueryColumnKind kind = QueryColumnKind::Property;
    QString name;     ///< as written ("koszt", "Brutto" of `"Brutto = koszt * 1.23"`, "title")
    QString property; ///< Property: the key folded (queryPropertyKey())
    QString formula;  ///< Formula: the expression
    QString label;    ///< a label written with `=` ("" for none: the view names the column)

    friend bool operator==(const QueryColumn &, const QueryColumn &) = default;
};

/// A column summary (`summary:koszt=sum`): the column (QueryColumn::name folded) and the function
/// (canonical English name: sum, average, median, min, max, range, stddev, count, empty, filled,
/// unique, checked, unchecked, earliest, latest).
struct QuerySummary {
    QString column;
    QString function;

    friend bool operator==(const QuerySummary &, const QuerySummary &) = default;
};

/// A validated query.
struct Query {
    QString source; ///< the query text as written
    QueryType type = QueryType::Blocks;
    bool explicitType = false; ///< written `type:`; otherwise inferred
    QList<QueryFilter> filters;
    QList<QuerySort> sort;
    QueryGroupKey group = QueryGroupKey::None;
    QString groupProperty;     ///< group Property: the key folded
    QString groupPropertyName; ///< … as written
    std::optional<int> limit;
    QueryView view = QueryView::None;
    QList<QueryColumn> columns;    ///< `columns:` in the order written (empty: the view's default)
    QList<QuerySummary> summaries; ///< `summary:`
};

/// The folded form of a property key in queries and views: case- and diacritic-insensitive,
/// blanks and '_' as '-' ("Termin oddania" -> "termin-oddania", "Budżet" -> "budzet").
QString queryPropertyKey(QStringView key);
/// The canonical summary function of a written name (English or Polish: "suma" -> "sum"); "" for
/// none.
QString querySummaryFunction(QStringView name);

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
    QString key;   ///< path, ISO date, tag, status / priority name, a property value folded (a
                   ///< link: the page's path); empty for "none"
    QString label; ///< display text (translated for "none", statuses and priorities)
    QList<QueryItem> items;
    QString value; ///< group:<property>: the value as written in a note ("[[Dom]]", "w toku")
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
/// Default order: tasks/blocks by path then line, reminders by time, pages by title. A query run
/// again and again (after every save) is cheaper through a QueryCache (index/querycache.h): the
/// same result, only the changed notes evaluated again.
QueryResult runQuery(const Query &query, const VaultIndex &index, QDate today, Qt::DayOfWeek weekStart = Qt::Monday,
                     const QueryOptions &options = {});

/// Translated group/status labels (also handy for UIs).
QString queryStatusLabel(TaskStatus status);
QString queryPriorityLabel(Priority priority);

/// A `key:value` token of a query's text (any spelling of a key; `key::value` property filters as
/// "::"): what edits of the query by a view's UI (its columns, its sort) replace.
struct QueryKeyToken {
    QString key;        ///< canonical English key ("sort", "columns", "group", "view", "summary",
                        ///< "status", …) or "::" for a property filter
    qsizetype from = 0; ///< the token (a leading '-' included)
    qsizetype to = 0;
    qsizetype valueFrom = 0; ///< where its value starts (after the colon or colons)
    bool negated = false;

    friend bool operator==(const QueryKeyToken &, const QueryKeyToken &) = default;
};
/// The key tokens of `source` in order (comments and quoted phrases skipped, as parseQuery() reads
/// them); empty for a source with an unclosed quote.
QList<QueryKeyToken> queryKeyTokens(const QString &source);

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
/// a ```query fence (its own lines; the fence lines and other code are not looked at). `parsed`:
/// markdown::parseNote() of `content` when the caller has it (else it is parsed when needed).
QList<QueryNameRef> findQueryNames(const QString &content, const ParsedNote *parsed = nullptr);

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
