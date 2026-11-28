// TasksView — the Tasks view's list drawn by Atrament (docs/ATRAMENT.md "The views in Atrament (M3)",
// Tasks, step 1): the rows of a TaskListModel (src/app/planning/tasklistmodel.h) — its sections'
// headers and its tasks — in a measured list, in place of TasksView.qml's ListView of TasksRow
// delegates. A header shows its section and count and folds it; a task its box (its status: a click
// toggles it, a right-click opens its status menu), its text (the model's rich text as rendered
// spans: links and tags clickable, a link's menu, at most four lines), its chips (the due date — an
// overdue one in red —, scheduled, start, the next reminder, the recurrence, the priority, "Waits for",
// the subtasks' fold and progress, the clocked time) and where it is written. A subtask is indented
// by its depth.
//
// The view does nothing itself: what a row asks goes to `onAction(action, args)`, which TasksView.qml
// answers with the functions its delegates call today (toggle with its undo toast, the menus, opening
// a page):
//
//   "toggle"        {path, line, text, status}          the box clicked, Space
//   "open"          {path, line}                        the row clicked, Enter
//   "toggleSection" {section}                           a header clicked
//   "fold"          {key}                               the subtasks' chip clicked
//   "link"          {url, path}                         a link or a #tag of the text clicked
//   "linkMenu"      {url, path, x, y}                   a right-click on one
//   "statusMenu"    {key, path, line, text, status, clockRunning, x, y}  a right-click on the box
//   "menu"          {key, path, line, text, status, clockRunning, x, y}  a right-click on the row
//   "tokenMenu"     {kind, value, alarm, rule, priority, path, line, text, status, x, y}
//                                                       a right-click on a date, reminder,
//                                                       recurrence or priority chip
//
// (x, y: window px.) Keys while it has the focus: ↑ / ↓ move through the tasks (headers skipped),
// Space toggles the current one, Enter opens it. For screen readers the list's rows are its items: a
// header a heading, a task's box a checkbox named by the task's text.
#pragma once

#include "modelrows.h"
#include "renderedspans.h"
#include "ui/atrament.h"

#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace brulion::planning {
class TaskListModel;
}

namespace brulion::views::atr {

// TasksView.qml's look (Theme): the text's palette and fonts, the rows' and chips' colours.
struct TasksLook {
    editor::atr::PagePalette text;
    atrament::Color background = atrament::rgb(0xffffff);
    atrament::Color hover = atrament::rgba(0x8c959f1a);    // Theme.bgHover
    atrament::Color current = atrament::rgba(0x3b5bdb1f);  // the current row (Theme.selection)
    atrament::Color header = atrament::rgb(0x6e7781);      // a section's label (Theme.fgMuted)
    atrament::Color faint = atrament::rgb(0x8c959f);       // where it is written (Theme.fgFaint)
    atrament::Color box = atrament::rgb(0x8c959f);         // an open task's box (Theme.borderStrong)
    atrament::Color accent = atrament::rgb(0x3b5bdb);      // a done task's box (Theme.accent)
    atrament::Color danger = atrament::rgb(0xc92a2a);      // overdue (Theme.dangerText)
    atrament::Color chip = atrament::rgba(0x8c959f1f);     // a chip's background
    atrament::Color chipText = atrament::rgb(0x57606a);
    atrament::Color dangerChip = atrament::rgba(0xc92a2a1f);
    float textSize = 14;
    float lineHeight = 20;
    float smallSize = 11;
    float sideMargin = 16;
    float indent = 22; // per level of a subtask
    std::u16string uiFamily;

    // From AtramentView.viewLook: its fields by name (colours as QColor or names, sizes, uiFamily;
    // text, muted and link: the text's palette); what a map lacks keeps its default.
    static TasksLook fromMap(const QVariantMap &look);
};

class TasksView {
public:
    // The parts of a row the last frame drew (partBounds(); Chip: its chips that show a token, in
    // their order).
    enum class Part { Row, Box, Text, Chip };

    TasksView(atrament::Context<TasksView> &cx, planning::TaskListModel *model);
    ~TasksView();
    TasksView(const TasksView &) = delete;
    TasksView &operator=(const TasksView &) = delete;

    atrament::AnyElement render(atrament::Window &window, atrament::Context<TasksView> &cx);

    void setLook(TasksLook look, atrament::Context<TasksView> &cx);
    const TasksLook &look() const { return m_look; }

    // What a row asks (above).
    std::function<void(const QString &action, const QVariantMap &args)> onAction;

    // A part by its name ("box", "text", "chip"; anything else: the row).
    static Part partNamed(const QString &name);
    // Where the last frame drew a part of `row` (window px); nothing when it drew no such part.
    std::optional<atrament::Rect> partBounds(int row, Part part) const;
    // The rows the last frame drew.
    int drawnRows() const { return m_drawnRows; }
    // The task ↑ / ↓ / a click made current (−1: none).
    int currentRow() const { return m_current; }
    // Moves the current task by `step` tasks (headers skipped), shown in the view.
    void moveCurrent(int step, atrament::Context<TasksView> &cx);
    atrament::FocusHandle focusHandle() const { return m_focus; }
    // The next frame shows `row` (the least scroll that does).
    void revealRow(int row, atrament::Context<TasksView> &cx);
    int nearestTask(int row) const;
    int taskFrom(int row, int step) const;

    // A row as the model's roles give it.
    struct RowData {
        bool header = false;
        QString section, sectionLabel;
        int sectionCount = 0;
        bool collapsed = false;
        QString key, text, html, status, path, pageTitle, folder;
        int line = 0;
        bool isJournal = false;
        QString dueLabel, scheduledLabel, startLabel, recurrenceLabel, reminderLabel, priorityName;
        QString due, scheduled, start, recurrence, reminderSpec; // the tokens' values (their menus)
        bool overdue = false, reminderAlarm = false;
        bool blocked = false;
        QString blockedBy, progress, timeLabel;
        bool clockRunning = false;
        int depth = 0, subtaskCount = 0;
        bool folded = false;
        bool closed() const { return status == u"done" || status == u"cancelled"; }
    };
    RowData rowData(int row) const;

private:
    struct Drawn {
        int row;
        Part part;
        atrament::Rect bounds;
    };
    atrament::AnyElement renderRow(std::size_t row);
    atrament::AnyElement headerRow(int row, const RowData &d);
    atrament::AnyElement taskRow(int row, const RowData &d);
    atrament::AnyElement taskBox(int row, const RowData &d);
    atrament::AnyElement taskText(int row, const RowData &d);
    atrament::AnyElement chips(int row, const RowData &d);
    atrament::AnyElement chip(int row, const QString &text, bool danger, const QString &kind, const RowData &d);
    // A canvas over its parent that records the parent's bounds as `part` of `row` (partBounds()).
    atrament::AnyElement tracker(int row, Part part);
    void act(const QString &action, const QVariantMap &args) const;
    QVariantMap taskArgs(const RowData &d) const;
    QVariantMap menuArgs(const RowData &d) const;
    // A right-click's menu at the window point `at`.
    void menuAt(const QString &action, QVariantMap args, atrament::Point at) const;
    bool keyDown(const atrament::KeyDownEvent &e, atrament::Context<TasksView> &cx);

    QPointer<planning::TaskListModel> m_model;
    atrament::EntityId m_id;
    TasksLook m_look;
    ModelRows m_rows;
    atrament::FocusHandle m_focus;
    int m_current = -1;
    std::vector<Drawn> m_drawn; // the parts the frame drawn last recorded (cleared by render())
    int m_drawnRows = 0;
};

} // namespace brulion::views::atr
