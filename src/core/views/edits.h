// Edits of database views (docs/SYNTAX.md §10 "Views"), as pure functions (content in → content
// out): a table cell writes a page property (YAML frontmatter or `key:: value` lines, through
// markdown/pageprops.h — only that property's lines change, its type kept) or a block's `key::`
// line, a task's status, priority or date; a card dropped on a board's column or a calendar's day
// is such an edit; a new row is a new page (or task) with the view's tag, folder and property
// values; the view's columns and sort are written back into the query's text. Part of the views
// module — see views/view.h.
#pragma once

#include "views/view.h"

#include <QDate>
#include <QString>
#include <QVariant>

#include <optional>

namespace brulion::views {

/// What an edit of a row writes.
enum class EditField { Property, Status, Priority, TaskDate };

struct RowEdit {
    EditField field = EditField::Property;
    /// Property: the key to write (Column::property); TaskDate: "due", "scheduled" or "start".
    QString key;
    /// The new value as a cell editor gives it (Cell::value's forms; a text for any type is read
    /// as that type: "12,5" a number, "a, [[B]]" a list); invalid, "" or an empty list removes the
    /// property, the date or the priority.
    QVariant value;
    /// Property: the type to write ("text", "number", "checkbox", "date", "datetime", "list",
    /// "tags", "aliases"); "" keeps the property's type, or reads it from the value.
    QString type;
    bool blockRow = false; ///< a block row: its own `key::` line (else the page's property)
    int line = 0;          ///< block rows (and tasks): the block's first line (0-based)
    int ownEndLine = 0;    ///< block rows: the last line of the block's own text
};

struct EditOutcome {
    QString content; ///< the new content (the input when nothing changed)
    bool changed = false;
    QString error; ///< a translated reason when the edit was refused
};

/// Apply one edit to a note's `content`. Every other byte stays (line endings too); a value that
/// reads the same is not rewritten.
EditOutcome applyRowEdit(const QString &content, const RowEdit &edit, QDate today);

/// The edit that moves `row` into the group `to` of a board (`from`: the group it is dragged out
/// of, -1 for none): its status, priority, date or property becomes the group's value — a list
/// property swaps the old group's value for the new one, other items kept. nullopt when the
/// view's groups are nothing an edit writes or the group takes no drops.
std::optional<RowEdit> moveEdit(const ViewResult &view, const Row &row, int from, int to);
/// The edit that puts `row` of a calendar on `day` (its date column's date; a time stays).
std::optional<RowEdit> dateEdit(const ViewResult &view, const Row &row, QDate day);

/// The content of a new page for a view's new row: frontmatter with the view's tags and property
/// values (NewRow), the value of the board column `group` (-1 none) and, for a calendar, `day` in
/// its date property. "" when the view makes no pages.
QString newPageContent(const ViewResult &view, int group = -1, QDate day = {});
/// The lines of a new task for a task view (NewRow::task): `- [ ] text` with the view's tags, the
/// board column's status, priority or date, the calendar's day as the due date and the view's
/// property values as `key:: value` lines under it; each line ends with `eol`.
QString newTaskLines(const ViewResult &view, const QString &text, int group = -1, QDate day = {},
                     const QString &eol = QStringLiteral("\n"));

/// The query `source` with its `key:` option (canonical: "columns", "sort", "group", "view",
/// "summary", "limit") set to `value`: the first token of the key gets the value (other tokens of
/// it go), a missing one is added on a line of its own at the end; an empty `value` removes the
/// option. Comments, filters and line endings stay as written.
QString setQueryOption(const QString &source, const QString &key, const QString &value);
/// Items written as one comma list of a query (a name with a blank, a comma or a quote in quotes).
QString queryList(const QStringList &items);
/// The `columns:` value of a view's columns, as the query names them (a formula as
/// `"Label = expression"`).
QStringList columnNames(const Query &query, const ViewResult &view);

} // namespace brulion::views
