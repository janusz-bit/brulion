// Database views as JSON (docs/SYNTAX.md §10 "Views", "Obsidian Bases"): what the app's view blocks
// and base pages draw (QueryHost::runView) and what `brulion-cli query` / `brulion-cli base` print —
// one function for both. Part of the notebook module — see notebook/notebook.h.
//
//   {ok, error, language: "query" | "base", kind: "table" | "board" | "gallery" | "calendar" | "list",
//    type: "pages" | "tasks" | "blocks" | "reminders", total, limited, notice, problems: [text],
//    views: [{name, type, kind}] (bases), view (its index), month ("YYYY-MM-01", calendars),
//    groupKind, groupProperty, groupColumn, dateColumn,
//    columns: [{id, label, kind, property, type, editable, summary, summaryLabel, summaryText, sortKey}],
//    rows: [{path, blockIndex, line, ownEndLine, title, cover, coverKind, date, group,
//            task?: {status, …}, cells: [{markdown, text, value, empty, error, target}]}],
//    groups: [{key, label, value, rows: [row indices], summaryTexts, droppable}],
//    newRow: {possible, task, folder, tags, properties: [{key, value}], taskPage}}
#pragma once

#include "index/querycache.h"
#include "views/view.h"

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

namespace brulion::notebook {

struct ViewRequest {
    VaultPath currentPath; ///< the page showing the view (`page:this`, `this`): a note or the .base file
    QDateTime now;
    Qt::DayOfWeek weekStart = Qt::Monday;
    int view = 0;     ///< bases: the view (0-based)
    QString viewName; ///< bases: the view by its name (`![[Books.base#Reading]]`), before `view`
    QDate month;      ///< calendars: the month shown (invalid: the current one)
    std::function<std::optional<FileStat>(const VaultPath &path)> stat;
    QString newPagesFolder;
};

/// Run `source` — the body of a ```query block with `view:` ("query"), a ```base block or a .base
/// file's text ("base") — and give its view as JSON (see the file comment). `cache`: a QueryCache
/// kept for this query (the app's blocks run again after every save), or null.
QJsonObject runView(const VaultIndex &index, const QString &language, const QString &source, const ViewRequest &request,
                    QueryCache *cache = nullptr);

/// A ViewResult as JSON (the keys of the file comment, without ok / error / views).
QJsonObject viewResultJson(const views::ViewResult &view);

/// The ViewResult a viewResultJson() was made of (what views/edits.h needs: columns, rows with their
/// cells, groups, the new row; a row's task is left out).
views::ViewResult viewResultFromJson(const QJsonObject &json);

/// The view as plain text for the command line: a table of the cells' text (columns separated by
/// two blanks, a board's or a grouped table's groups as headings).
QString viewText(const QJsonObject &view);

} // namespace brulion::notebook
