// ViewResultModel — a database view (docs/SYNTAX.md §10 "Views", "Obsidian Bases") for the editor's
// DatabaseView: a ```query block with `view:`, a ```base block, a .base file shown as its own
// page, or a ```dataview CALENDAR block (language "dataview": read-only, no columns to change). Runs the view through the page's host (QueryHost::runView), renders its cells' Markdown with
// the page's renderer, and gives QML what it draws: the rows (this list model), the columns and the
// groups (columnModel, groupModel), a calendar's days. Edits — a cell, a card dragged to another
// column or day, a new row, a column or sort changed in the header — go through views/edits.h:
// on this page through PageController (one undo step), in another file through the host (undoLast()
// puts it back while the file holds what was written); the view's own text (the block's query or
// YAML, the .base file) changes for its columns and sort.
//
//   ViewResultModel { id: view; controller: block.controller; source: block.rowData.editText
//                     language: "query"; blockRow: block.index }
//   Repeater { model: view.columnModel; delegate: … }   // label, type, editable, sorted, summary…
//   Repeater { model: view; delegate: … }               // cells (HTML), path, title, cover…
//
// Like QueryResultModel it runs the first time when completed (the block is created with its rows)
// and again — debounced — when the index or the source changes; a cell being edited holds the
// rows (hold() / release()) so a run cannot take the editor away.
#pragma once

#include "types.h"

#include <QAbstractListModel>
#include <QPointer>
#include <QQmlParserStatus>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <memory>

namespace brulion {
class EditorHost;
namespace markdown {
struct Palette;
}
namespace views {
struct ViewResult;
}
} // namespace brulion

namespace brulion::editor {

class PageController;

/// A list of maps with fixed roles (the columns and groups of a view).
class ViewListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ViewListModel(QList<QByteArray> roles, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setItems(const QList<QVariantMap> &items);

private:
    QList<QByteArray> m_roles;
    QList<QVariantMap> m_items;
};

class ViewResultModel : public QAbstractListModel, public QQmlParserStatus {
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    Q_PROPERTY(brulion::editor::PageController *controller READ controller WRITE setController NOTIFY controllerChanged
                       FINAL)
    /// The view's text: a query (with `view:`) or a base's YAML.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged FINAL)
    /// "query" or "base".
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)
    /// A .base file shown on its own page: its text is read from it, columns and sort written into it.
    Q_PROPERTY(QString basePath READ basePath WRITE setBasePath NOTIFY basePathChanged FINAL)
    /// The page `this` means in a base's expressions when it is not the page with the view's text:
    /// the note embedding a .base file (`![[Books.base]]`); "" the controller's page, or the .base
    /// file shown on its own page.
    Q_PROPERTY(QString thisPath READ thisPath WRITE setThisPath NOTIFY thisPathChanged FINAL)
    /// The block holding the view (its query or YAML changes with the columns and sort); -1 none.
    Q_PROPERTY(int blockRow READ blockRow WRITE setBlockRow NOTIFY blockRowChanged FINAL)
    /// A base's view shown (0-based).
    Q_PROPERTY(int viewIndex READ viewIndex WRITE setViewIndex NOTIFY viewIndexChanged FINAL)
    /// A base's view by its name (`![[Books.base#Reading]]`); "" none.
    Q_PROPERTY(QString viewName READ viewName WRITE setViewName NOTIFY viewNameChanged FINAL)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged FINAL)
    Q_PROPERTY(bool ready READ ready NOTIFY resultChanged FINAL)
    Q_PROPERTY(bool ok READ ok NOTIFY resultChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY resultChanged FINAL)
    Q_PROPERTY(bool complete READ complete NOTIFY resultChanged FINAL)
    /// "table", "board", "gallery", "calendar", "list".
    Q_PROPERTY(QString kind READ kind NOTIFY resultChanged FINAL)
    /// "pages", "tasks", "blocks", "reminders".
    Q_PROPERTY(QString type READ type NOTIFY resultChanged FINAL)
    Q_PROPERTY(int total READ total NOTIFY resultChanged FINAL)
    Q_PROPERTY(bool limited READ limited NOTIFY resultChanged FINAL)
    Q_PROPERTY(QString notice READ notice NOTIFY resultChanged FINAL)
    Q_PROPERTY(QStringList problems READ problems NOTIFY resultChanged FINAL)
    /// A base's views as { name, view (its index), kind } for a menu's delegates.
    Q_PROPERTY(QVariantList viewEntries READ viewEntries NOTIFY resultChanged FINAL)
    /// A base's views (names) and their kinds.
    Q_PROPERTY(QStringList viewNames READ viewNames NOTIFY resultChanged FINAL)
    Q_PROPERTY(QStringList viewKinds READ viewKinds NOTIFY resultChanged FINAL)
    Q_PROPERTY(int columnTotal READ columnTotal NOTIFY resultChanged FINAL)
    /// Columns: label, kind, type, editable, sortKey, sorted (1 ascending, -1 descending, 0),
    /// summaryLabel, summaryText, number (its index; a delegate's `column` is the model's own).
    Q_PROPERTY(QAbstractItemModel *columnModel READ columnModel CONSTANT FINAL)
    /// Groups (a board's columns): label, value, count, droppable, summary (the first column's
    /// summary of the group), group (its index).
    Q_PROPERTY(QAbstractItemModel *groupModel READ groupModel CONSTANT FINAL)
    Q_PROPERTY(int groupCount READ groupCount NOTIFY resultChanged FINAL)
    /// Cards may be dragged between groups / days (what they show is something an edit writes).
    Q_PROPERTY(bool movable READ movable NOTIFY resultChanged FINAL)
    /// A new row can be made ("+").
    Q_PROPERTY(bool canAddRow READ canAddRow NOTIFY resultChanged FINAL)
    /// The view's own text can be changed (columns, sort): a block of an editable page or a .base file.
    Q_PROPERTY(bool canChangeView READ canChangeView NOTIFY resultChanged FINAL)
    /// Calendars: the month shown ("YYYY-MM-01"), its title ("October 2026"), the week days' names.
    Q_PROPERTY(QString month READ month WRITE setMonth NOTIFY resultChanged FINAL)
    Q_PROPERTY(QString monthTitle READ monthTitle NOTIFY resultChanged FINAL)
    Q_PROPERTY(QStringList weekDays READ weekDays NOTIFY resultChanged FINAL)
    /// Something holds the rows (a cell being edited, a card being dragged).
    Q_PROPERTY(bool held READ held NOTIFY heldChanged FINAL)
    /// An edit of another file that undoLast() can put back.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged FINAL)
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)
    /// Grows with every new result (bindings that read rows through the invokables below).
    Q_PROPERTY(int revision READ revision NOTIFY resultChanged FINAL)
    /// Rows in their order of display: ungrouped 0 … n-1, grouped group by group.
    Q_PROPERTY(QList<int> shownRows READ shownRows NOTIFY resultChanged FINAL)

public:
    enum Role {
        PathRole = Qt::UserRole + 1,
        TitleRole,
        CellsRole, ///< the cells rendered (HTML)
        TextsRole, ///< the cells' plain text
        StatusRole,
        IsTaskRole,
        CoverRole,      ///< a picture's URL ("" none)
        CoverColorRole, ///< a colour cover ("#rrggbb", "" none)
        GroupRole,
        DateRole,
        LineRole,
        HeadlineRole, ///< the first cell rendered (a card's title)
        DetailsRole,  ///< the other cells rendered (a card's lines), the group's column left out
    };
    Q_ENUM(Role)

    explicit ViewResultModel(QObject *parent = nullptr);
    ~ViewResultModel() override;

    void classBegin() override;
    void componentComplete() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    PageController *controller() const { return m_controller; }
    void setController(PageController *controller);
    QString source() const { return m_source; }
    void setSource(const QString &source);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    QString basePath() const { return m_basePath; }
    void setBasePath(const QString &path);
    QString thisPath() const { return m_thisPath; }
    void setThisPath(const QString &path);
    int blockRow() const { return m_blockRow; }
    void setBlockRow(int row);
    int viewIndex() const { return m_viewIndex; }
    void setViewIndex(int view);
    QString viewName() const { return m_viewName; }
    void setViewName(const QString &name);
    bool available() const;
    bool ready() const { return m_ready; }
    bool ok() const { return m_ok; }
    QString error() const { return m_error; }
    bool complete() const { return m_complete; }
    QString kind() const;
    QString type() const;
    int total() const;
    bool limited() const;
    QString notice() const;
    QStringList problems() const;
    QStringList viewNames() const;
    QVariantList viewEntries() const;
    QStringList viewKinds() const;
    int columnTotal() const;
    QAbstractItemModel *columnModel() const { return m_columns; }
    QAbstractItemModel *groupModel() const { return m_groups; }
    int groupCount() const;
    bool movable() const;
    bool canAddRow() const;
    bool canChangeView() const;
    QString month() const;
    void setMonth(const QString &month);
    QString monthTitle() const;
    QStringList weekDays() const;
    bool held() const { return !m_holders.isEmpty(); }
    bool canUndo() const { return !m_undoPath.isEmpty(); }
    int refreshDelay() const { return m_timer.interval(); }
    void setRefreshDelay(int ms);

    int revision() const { return m_revision; }
    QList<int> shownRows() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void hold(QObject *holder);
    Q_INVOKABLE void release(QObject *holder);

    /// A row's parts (the roles, for delegates that show rows by number: boards, calendars).
    Q_INVOKABLE QStringList rowCells(int row) const;
    Q_INVOKABLE QString rowHeadline(int row) const;
    Q_INVOKABLE QStringList rowDetails(int row) const;
    Q_INVOKABLE QString rowCover(int row) const;
    Q_INVOKABLE QString rowCoverColor(int row) const;
    Q_INVOKABLE QString rowStatus(int row) const;
    Q_INVOKABLE QString rowTitle(int row) const;
    Q_INVOKABLE QString rowText(int row) const; ///< the first cell as plain text (accessibility)
    /// A group's label; a column's summary ("Sum: 12") or "".
    Q_INVOKABLE QString groupLabel(int group) const;
    Q_INVOKABLE QString columnSummary(int column) const;
    /// The number of rows of group `group`, and its `i`-th row.
    Q_INVOKABLE int groupRowCount(int group) const;
    Q_INVOKABLE int groupRowAt(int group, int i) const;
    /// A list view's line: the first cell, then the others after "·" (rendered).
    Q_INVOKABLE QString rowLine(int row) const;
    /// A table's lines: row numbers, and -1 - g before the rows of group g (grouped tables).
    Q_INVOKABLE QList<int> tableLines() const;
    /// The width of a table column, where it starts, the table's width (`touch`: a touch screen's
    /// narrower title columns).
    Q_INVOKABLE double columnWidth(int column, bool touch) const;
    Q_INVOKABLE double columnOffset(int column, bool touch) const;
    Q_INVOKABLE double tableWidth(bool touch) const;
    /// The line of the table showing `row` (tableLines()), -1 none.
    Q_INVOKABLE int lineOfRow(int row) const;
    /// The row after (`step` > 0) or before (< 0) `row` in the table's order (a group's header line
    /// skipped), -1 when there is none.
    Q_INVOKABLE int neighbourRow(int row, int step) const;
    /// The rows of group `group` in order (a board's column).
    Q_INVOKABLE QList<int> groupRows(int group) const;
    /// Calendars: the day `day` of the grid (0 … 41) as "YYYY-MM-DD", its day of the month, whether it
    /// is in the month shown, whether it is today, and its rows.
    Q_INVOKABLE QString dayDate(int day) const;
    Q_INVOKABLE int dayNumber(int day) const;
    Q_INVOKABLE bool dayInMonth(int day) const;
    Q_INVOKABLE bool dayIsToday(int day) const;
    Q_INVOKABLE QList<int> dayRows(int day) const;
    /// Show the month `months` from the one shown (-1 the previous); 0 today's month.
    Q_INVOKABLE void moveMonth(int months);

    /// A cell's value for its editor (Cell::value: text, number, bool, list), its plain text, its
    /// column's editing type ("text", "number", "checkbox", "date", "datetime", "list", "tags",
    /// "aliases", "status", "priority", "taskdate"; "" read-only) and whether it may be edited.
    Q_INVOKABLE QVariant cellValue(int row, int column) const;
    Q_INVOKABLE QString cellText(int row, int column) const;
    Q_INVOKABLE QString columnType(int column) const;
    Q_INVOKABLE QString columnLabel(int column) const;
    Q_INVOKABLE bool cellEditable(int row, int column) const;
    /// The items of a list cell as one line of text ("a, [[B]]"), what its text editor starts from.
    Q_INVOKABLE QString cellEditText(int row, int column) const;

    /// Write `value` into cell (`row`, `column`): { ok, error (translated), undo: another file was
    /// written (undoLast()), title: the row's page }.
    Q_INVOKABLE QVariantMap editCell(int row, int column, const QVariant &value);
    /// Move card `row` from group `from` to group `to` (a board): its status, priority, date or
    /// property becomes the group's value. Same answer as editCell().
    Q_INVOKABLE QVariantMap moveRow(int row, int from, int to);
    /// Put `row` of a calendar on grid day `day` (0 … 41).
    Q_INVOKABLE QVariantMap moveRowToDay(int row, int day);
    /// A new row named `title`: a page (in the view's folder, with its tag and values, the value of
    /// group `group`, the calendar day `day` (-1 none)) or a task. { ok, error, path }.
    Q_INVOKABLE QVariantMap addRow(const QString &title, int group, int day);
    /// Put back the last edit of another file (while the file holds what was written).
    Q_INVOKABLE bool undoLast();
    /// A task's status on a card ("todo", "doing", "done", …); same answer as editCell().
    Q_INVOKABLE QVariantMap setRowStatus(int row, const QString &status);

    /// Sort by `column` (`descending`), written into the view's text (the query's `sort:`, the base
    /// view's `sort:`); `column` -1 removes the sort. False when the text cannot be changed.
    Q_INVOKABLE bool sortBy(int column, bool descending);
    /// The view's columns written into its text: one hidden, one added (a property name or
    /// "Label = expression"; a base: a property id), one moved.
    Q_INVOKABLE bool hideColumn(int column);
    Q_INVOKABLE bool addColumn(const QString &name);
    Q_INVOKABLE bool moveColumn(int from, int to);
    /// Property names a column can show (the notebook's page properties, most used first), without
    /// those shown.
    Q_INVOKABLE QStringList columnSuggestions() const;

    /// Open the row's page at its block: { action: "scrollTo", row } on this page, else navigated.
    Q_INVOKABLE QVariantMap open(int row, bool newPane = false);
    /// A link in a cell was clicked.
    Q_INVOKABLE QVariantMap activateLink(int row, const QString &url, bool newPane = false);

Q_SIGNALS:
    void controllerChanged();
    void sourceChanged();
    void languageChanged();
    void basePathChanged();
    void blockRowChanged();
    void thisPathChanged();
    void viewIndexChanged();
    void viewNameChanged();
    void availableChanged();
    void resultChanged();
    void heldChanged();
    void undoChanged();
    void refreshDelayChanged();

private:
    struct Row;
    void schedule();
    void timerFired();
    void connectHost();
    void rebuild();
    /// The rendered cells, headline and details of row `i` of the view (rebuild(), followMacros()).
    void renderCells(Row &row, qsizetype i, const markdown::Palette &palette) const;
    /// EditorHost::macrosChanged: the rows that show a macro of an extension, rendered again.
    void followMacros();
    /// `markdown` rendered as the view's page renders text, its macros as in the note `path` (the
    /// row's); `*showsMacro` set when it shows one.
    QString render(const QString &markdown, const QString &path = QString(), bool *showsMacro = nullptr) const;
    QUrl coverUrl(const QString &cover, const QString &kind) const;
    /// Apply a views::RowEdit to row `row`'s file (this page: PageController; else the host).
    QVariantMap applyEdit(int row, const void *edit);
    /// The view's text with `change` made (query options / base YAML), written where it lives.
    bool writeViewText(const QString &text);
    QString viewText() const;
    QStringList currentColumns() const;
    Qt::DayOfWeek weekStartDay() const;
    bool setColumns(const QStringList &columns);

    QPointer<PageController> m_controller;
    QPointer<EditorHost> m_host;
    QString m_source;
    QString m_language = QStringLiteral("query");
    QString m_basePath;
    QString m_thisPath;
    int m_blockRow = -1;
    int m_viewIndex = 0;
    QString m_viewName;
    QDate m_month;
    QTimer m_timer;
    bool m_ready = false;
    bool m_ok = false;
    bool m_complete = true;
    bool m_completing = false;
    bool m_pending = false;
    QString m_error;
    QVariantMap m_result; ///< the host's answer
    std::unique_ptr<views::ViewResult> m_view;
    /// The text a cell was last written with (row * 65536 + column), until the next refresh reads it
    /// from the files: the rows wait for an edit (hold()), so cellEditText() would show the old one
    /// to a Tab that comes back to the cell.
    QHash<qint64, QString> m_written;
    QList<Row> m_rows;
    ViewListModel *m_columns = nullptr;
    ViewListModel *m_groups = nullptr;
    QHash<QObject *, QMetaObject::Connection> m_holders;
    QString m_undoPath;
    QString m_undoBefore;
    QString m_undoAfter;
    int m_revision = 0;
};

} // namespace brulion::editor
