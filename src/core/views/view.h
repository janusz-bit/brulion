// Database views (docs/SYNTAX.md §10 "Views", "Obsidian Bases"): a query's results — or a base's —
// shown as a table, a board, a gallery, a calendar or a list, with columns of built-in fields,
// properties and formulas, groups and column summaries. GUI-free: the app draws a ViewResult
// (src/app/editor/viewresultmodel.h), the command line prints it. Edits are pure functions
// (views/edits.h); Obsidian's .base files are read and written by views/bases.h.
//
//   const Query query = std::get<Query>(parseQuery(u"type:pages tag:projekt view:board group:status"_s));
//   const QueryResult result = runQuery(query, index, today);
//   const ViewResult view = std::get<ViewResult>(buildQueryView(query, result, index, options));
//
// Cells are Markdown (the app renders them like any text: links, tags, dates); `value` is what a
// cell editor starts from, typed by `Column::type`. Rows are the ones shown (the query's `limit:`,
// else DefaultQueryLimit; a calendar only those of its month's grid) and formulas are evaluated for
// them only — with Dataview's limits (dataview/dataview.h "Limits"): a formula too costly or a
// value too large shows its message in the cell instead of freezing the app.
#pragma once

#include "index/query.h"
#include "index/vaultindex.h"
#include "types.h"

#include <QDate>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <functional>
#include <optional>
#include <variant>

namespace brulion::views {

/// How the rows are laid out.
enum class ViewKind { List, Table, Board, Gallery, Calendar };

/// "list", "table", "board", "gallery", "calendar".
QString viewKindName(ViewKind kind);

/// What a column shows; the kinds of index/query.h (Title, Path, …, Property, Formula).
using ColumnKind = QueryColumnKind;

/// One column.
struct Column {
    QString id;    ///< stable within the view: "title", "status", "prop:koszt", "formula:Brutto",
                   ///< Bases: the property as the base names it ("note.status", "file.name")
    QString label; ///< the header (a label of the query or the base, else the name as written)
    ColumnKind kind = ColumnKind::Property;
    QString property; ///< Property: the key to write, as the notes spell it ("Budżet")
    /// How a cell is edited: "text", "number", "checkbox", "date", "datetime", "list", "tags",
    /// "aliases" (page and block properties), "status", "priority", "taskdate" (a task's 📅 ⏳ 🛫);
    /// "" read-only (built-ins, formulas, maps).
    QString type;
    bool editable = false;
    QString summary;      ///< the summary function (querySummaryFunction() names; Bases: its name)
    QString summaryLabel; ///< its name for the header ("Sum", translated; a Bases formula's name)
    QString summaryText;  ///< the summary of the rows shown ("" none)
    /// What sorting by the column writes into the query (`sort:` names: "page", "due", a property);
    /// "" when the column cannot be sorted by (formulas, folders, sizes).
    QString sortKey;

    friend bool operator==(const Column &, const Column &) = default;
};

/// One cell.
struct Cell {
    QString markdown; ///< what it shows ("" empty)
    QString text;     ///< as plain text (summaries, CSV, accessibility)
    /// What its editor starts from: QString (text, date "YYYY-MM-DD", datetime, status, priority
    /// names), double (number), bool (checkbox), QStringList (list, tags, aliases); invalid: empty.
    QVariant value;
    bool empty = true;
    bool error = false; ///< a formula's message instead of a value
    /// Where an edit of this cell goes: the row's page ("") or its block ("block"); "" also for
    /// read-only cells.
    QString target;

    friend bool operator==(const Cell &, const Cell &) = default;
};

/// One row: a page, a block or a task (blockIndex ≥ 0), with its cells (one per column).
struct Row {
    VaultPath path;
    int blockIndex = -1;
    int line = 0; ///< the block's first line (0-based); 0 for pages
    int ownEndLine = 0;
    std::optional<Task> task;
    QString title; ///< the page's title
    QList<Cell> cells;
    /// Gallery and board cards: the cover — a note's `banner` (or a Bases `image:` property), else
    /// its first picture: a vault path, an http(s) URL, or a colour "#rrggbb"; "" for none.
    QString cover;
    QString coverKind; ///< "file", "web", "color" or ""
    /// Calendar: the day it is on (the view's date column), invalid when it has none.
    QDate date;
    int group = -1; ///< its group (ViewResult::groups), -1 when ungrouped
};

/// A group of rows: a board's column, a grouped table's section.
struct Group {
    QString key;   ///< QueryGroup::key ("" for the group without a value)
    QString label; ///< shown
    /// What a card dropped here writes: a status or priority name, a property value as written,
    /// a date (YYYY-MM-DD); "" removes the property (the group without a value).
    QString value;
    QList<int> rows;          ///< indices into ViewResult::rows, in order
    QStringList summaryTexts; ///< per column: the group's summary ("" none)
    bool droppable = false;   ///< cards may be dropped here (the group is something an edit writes)

    friend bool operator==(const Group &, const Group &) = default;
};

/// What a new row ("+") becomes: a page in `folder` with `tags` and `properties` (the query's
/// equality filters, a board column's value), or — for a task view — a task on `taskPage`.
struct NewRow {
    bool possible = false;
    bool task = false;                             ///< a task (written on `taskPage`), else a page
    QString folder;                                ///< new pages go there ("" the notebook's folder for new pages)
    QStringList tags;                              ///< written in `tags:`
    QList<std::pair<QString, QString>> properties; ///< key (as written) and value written
    VaultPath taskPage;                            ///< task views: the page with the query

    friend bool operator==(const NewRow &, const NewRow &) = default;
};

struct ViewResult {
    ViewKind kind = ViewKind::Table;
    QueryType type = QueryType::Pages; ///< what the rows are
    QList<Column> columns;
    QList<Row> rows;
    QList<Group> groups; ///< grouped views and every board (in order)
    int total = 0;       ///< matches before the limit
    QString notice;      ///< a note on what is shown differently ("Map views are shown as a table.")
    /// Boards: what drops change — "status", "priority", "property", "date"; "" none (a group by
    /// page or tag).
    QString groupKind;
    QString groupProperty; ///< "property" / "date" drops: the key written (as notes spell it)
    int groupColumn = -1;  ///< the column showing the group's value (-1 none)
    int dateColumn = -1;   ///< calendars: the column the days come from (-1: the journal date)
    QDate month;           ///< calendars: the month shown (the first day)
    NewRow newRow;
    bool limited = false; ///< more rows match than are shown
};

/// Extra context for building a view.
struct ViewOptions {
    QDate today;
    QDateTime now;
    Qt::DayOfWeek weekStart = Qt::Monday;
    /// The page showing the view (the query's page, the page embedding a base, or the .base file).
    VaultPath currentPath;
    /// A file's size and modification time (mtime and size columns, formulas' file.mtime).
    std::function<std::optional<FileStat>(const VaultPath &path)> stat;
    /// Calendars: the month shown (any day of it); invalid: today's month.
    QDate month;
    /// Rows shown without a `limit:`.
    int defaultLimit = DefaultQueryLimit;
    /// Where new pages go when the view names no folder (VaultConfig::newPagesFolder).
    QString newPagesFolder;
    /// The steps all formulas of one build may take (dataview::Options::maxSteps): past them each
    /// formula cell shows the message instead of a value.
    qint64 maxSteps = 20000000;
};

/// The rows a calendar grid of `month` shows: from the first day of the week holding the 1st to 6
/// weeks later (`weekStart`).
std::pair<QDate, QDate> calendarRange(QDate month, Qt::DayOfWeek weekStart);

/// The view of a query's results (`query.view` set; QueryView::None is shown as a list): columns
/// (`columns:`, else a default — pages: the title and the page properties most results have; tasks:
/// the task, status, due date, priority and page; blocks: the text and page), cells, groups (a
/// board's columns: `group:`, for tasks the status by default; a status board always shows to do,
/// in progress and done), summaries (`summary:`), the new row. `result` is runQuery()'s (with
/// `options.defaultLimit`; a calendar keeps only the rows of its month's grid, so give it the
/// query run with MaxQueryLimit). A translated message for a view that cannot be built (a board
/// grouped by something no edit writes is shown, only without drops; a summary of a column the
/// view does not show, a formula that does not parse).
std::variant<ViewResult, QString> buildQueryView(const Query &query, const QueryResult &result, const VaultIndex &index,
                                                 const ViewOptions &options);

/// The summary of `values` (cell values, Cell::value) for `function` (sum, average, median, min,
/// max, range, stddev, count, empty, filled, unique, checked, unchecked, earliest, latest): its
/// text ("1 234.5", "2026-10-15", "3"), "" when nothing can be said (a sum of no numbers).
QString summarize(const QString &function, const QList<QVariant> &values);
/// The translated name of a summary function ("Sum", "Average", …).
QString summaryLabel(const QString &function);

} // namespace brulion::views
