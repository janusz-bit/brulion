// Obsidian Dataview queries — the body of a ```dataview block (docs/SYNTAX.md §10 "Dataview
// queries"), read by our own parser and evaluated against the VaultIndex. Read-only: the
// results show pages, values and tasks; nothing is written, nothing from a note runs as a
// script (```dataviewjs blocks and `$= …` stay code).
//
//   TABLE file.mtime AS "Changed", status, length(file.tasks) AS Tasks
//   FROM #projekt AND "Projekty" AND -"Projekty/Archiwum"
//   WHERE status != "done" AND (due <= date(today) + dur(7 days) OR !due)
//   SORT due ASC, file.name
//   LIMIT 20
//
// Query types: LIST [WITHOUT ID] [expr], TABLE [WITHOUT ID] expr [AS name], …, TASK (CALENDAR is
// not supported). Data commands, in any order and repeatable, applied one after another: FROM
// (once, right after the type: #tag, "folder" or "path/file", [[page]] = pages linking to it,
// [[]] = this page, outgoing([[page]]), combined with AND, OR, - / ! and parentheses), WHERE expr,
// SORT expr [ASC|DESC], …, GROUP BY expr [AS name], FLATTEN expr [AS name], LIMIT n. Keywords
// are case-insensitive; `//` starts a comment line.
//
// Expressions: literals (numbers, "strings", true, false, null, [[links]], list(…), object(…)),
// fields (`status`, `file.name`, `this.file.folder`, `row["due date"]`, `list[0]`; a field of a
// list maps over it, a field of a link reads the linked page), arithmetic (+ - * / %, dates ±
// durations, date − date), comparisons (= != < <= > >=), and / or / !, lambdas `(x) => x * 2`,
// and functions (contains, icontains, econtains, containsword, length, lower, upper, startswith,
// endswith, replace, split, join, substring, padleft, padright, truncate, date, dur, number,
// string, link, embed, elink, typeof, default, ldefault, choice, round, trunc, floor, ceil, min,
// max, sum, product, average, minby, maxby, reduce, sort, reverse, unique, flat, slice, filter,
// map, any, all, none, nonnull, firstvalue, extract, list, array, object, meta, dateformat,
// striptime, localtime, display). Regular-expression functions are not supported (a note
// synced from anywhere could make them run for minutes).
//
// Rows: pages (LIST, TABLE) or tasks (TASK). A page has the implicit `file` object (name, path,
// folder, ext, link, size, ctime, cday, mtime, mday, day, tags, etags, inlinks, outlinks,
// aliases, tasks, lists, frontmatter) and its fields: frontmatter keys and every `key:: value`
// of the note, also `[key:: value]` / `(key:: value)` inside a line; a key written twice holds a
// list. A task has status, checked, completed, fullyCompleted, text, line, lineCount, path,
// section, link, tags, outlinks, children, task, parent, blockId, created, due, start,
// scheduled, completion and its own fields; other names read its page (`file.name`).
//
// Limits — a query comes from any note, imports and sync too: expressions and FROM sources at most
// MaxExpressionDepth deep and MaxExpressionNesting nested (else parseQuery() fails), the values a
// query builds at most MaxValueDepth deep and MaxValueItems / MaxTextLength large (value.h), at
// most MaxRows rows and Options::maxSteps steps (else runQuery() fails). Stack: the deepest query
// they allow needs about 256 KiB (optimized build); run queries on a thread with at least 512 KiB.
#pragma once

#include "dataview/value.h"
#include "index/vaultindex.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace brulion::dataview {

/// Rows returned when the query has no LIMIT (the total still counts them all).
inline constexpr int DefaultRowLimit = 500;
/// Most rows a query may make (FLATTEN of large lists): beyond it the query stops with an error.
inline constexpr int MaxRows = 100000;
/// The deepest expression or FROM source parseQuery() accepts: a chain like `a OR b OR c` is as deep
/// as it has operators, and a call, list, lambda, field, index or `-` / `!` adds a level to its
/// parts. Evaluating, selecting and freeing it recurse once per level (about 1 KB of stack each).
inline constexpr int MaxExpressionDepth = 256;
/// Parentheses, calls, lists, lambdas, indexes and `-` / `!` nested in one another (parsing recurses
/// through about ten functions, some 2 KB of stack, per level).
inline constexpr int MaxExpressionNesting = 64;

enum class QueryType { List, Table, Task };

struct Expr;    // ast_p.h
struct Source;  // ast_p.h
struct Command; // ast_p.h

/// A validated query (ast_p.h has its parts).
struct Query {
    QString source;
    QueryType type = QueryType::List;
    bool withoutId = false;
    /// TABLE columns (expression, header); LIST: at most one (the value after the link).
    QList<std::pair<std::shared_ptr<const Expr>, QString>> columns;
    std::shared_ptr<const Source> from; ///< null: every page
    QList<std::shared_ptr<const Command>> commands;
};

/// Parse a ```dataview block. Returns the Query or a translated message saying what is wrong and
/// where ("Line 2: …"; QCoreApplication::translate context "brulion::Dataview").
std::variant<Query, QString> parseQuery(const QString &source);

/// Extra context for runQuery().
struct Options {
    /// The page holding the query: `this`, [[]] and relative links.
    VaultPath currentPath;
    /// `date(now)`, `date(today)` and friends (local wall-clock time).
    QDateTime now;
    /// `date(sow)` / `date(eow)`.
    Qt::DayOfWeek weekStart = Qt::Monday;
    /// A file's size and modification time (`file.size`, `file.mtime`, `file.ctime`); without it
    /// (or for a file it does not know) the size of the indexed text and no times.
    std::function<std::optional<FileStat>(const VaultPath &path)> stat;
    /// Rows returned without LIMIT.
    int defaultLimit = DefaultRowLimit;
    /// Steps an evaluation may take before the query stops with an error (a guard against
    /// queries that would freeze the app).
    qint64 maxSteps = 20000000;
};

/// One result row.
struct Row {
    VaultPath path;      ///< the page the row came from ("" for a group of a grouped TABLE)
    int blockIndex = -1; ///< the task's block (TASK), else -1
    std::optional<Task> task;
    /// TABLE: the cells as Markdown (the first the page link unless WITHOUT ID); LIST: one text
    /// ("[[Page]]: value", the value alone WITHOUT ID); TASK: empty.
    QStringList cells;
};

/// Rows under one key (LIST and TASK with GROUP BY; TASK without it: one group per page).
struct Group {
    QString key;   ///< the page's path for TASK groups by page, else the key as plain text
    QString label; ///< the key as plain text
    QList<Row> rows;
};

struct Result {
    QueryType type = QueryType::List;
    QStringList headers; ///< TABLE: one per cell (translated "File" / the group's name for the first)
    QList<Row> rows;     ///< ungrouped results (and the rows of a grouped TABLE: one per group)
    QList<Group> groups; ///< grouped results (LIST / TASK)
    int total = 0;       ///< rows (or tasks in the groups) before the default limit
};

/// Run a parsed query against the index. Returns the Result or a translated message (an
/// expression too costly, too many rows).
std::variant<Result, QString> runQuery(const Query &query, const VaultIndex &index, const Options &options);

/// A value as Markdown for a result cell: links as [[links]] (the shortest target), dates ISO
/// ("2026-10-15", "2026-10-15 09:30"), durations in words (translated), lists joined with ", ",
/// null as "–".
QString toMarkdown(const Value &value, const VaultIndex &index);
/// A value as plain text (group labels, `display()`): a link as its display text or page title.
QString toPlainText(const Value &value);

} // namespace brulion::dataview
