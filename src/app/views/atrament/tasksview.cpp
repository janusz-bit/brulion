#include "tasksview.h"

#include "markdown/renderedtext.h"
#include "pageicons.h"
#include "tasklistmodel.h"

#include <QCoreApplication>
#include <QColor>
#include <QHash>

#include <algorithm>

namespace brulion::views::atr {

namespace at = brulion::atrament;
using editor::atr::u16View;
using planning::TaskListModel;
using namespace Qt::StringLiterals;

namespace {

// TasksRow's words (its translations; not lupdate's: no context of its own).
QString rowWords(const char *text, int n = -1)
{
    return QCoreApplication::translate("TasksRow", text, nullptr, n);
}

QString roleText(const QModelIndex &i, int role)
{
    return i.data(role).toString();
}

// A chip of a task: its text, in the danger colours or not, the token it shows (its menu; "": none).
struct Chip {
    QString text;
    bool danger = false;
    QString kind;
};

// The chips of a task's dates and reminder (TasksRow's first ones), each with its token.
void dateChips(const TasksView::RowData &d, std::vector<Chip> &out)
{
    auto add = [&out](const QString &text, bool danger, const QString &kind) {
        if (!text.isEmpty())
            out.push_back({text, danger, kind});
    };
    add(d.overdue ? rowWords("%1, overdue").arg(d.dueLabel) : d.dueLabel, d.overdue && !d.closed(), u"due"_s);
    add(d.scheduledLabel, false, u"scheduled"_s);
    add(d.startLabel.isEmpty() ? QString() : rowWords("from %1").arg(d.startLabel), false, u"start"_s);
    add(d.closed() ? QString() : d.reminderLabel, false, u"reminder"_s);
    add(d.recurrenceLabel, false, u"recurrence"_s);
}

// The chips TasksRow shows for the task, in its order: its dates, "Waits for", its subtasks (the
// chip that folds them), its clocked time.
std::vector<Chip> chipsOf(const TasksView::RowData &d)
{
    std::vector<Chip> out;
    dateChips(d, out);
    if (d.blocked && !d.closed())
        out.push_back({rowWords("Waits for: %1").arg(d.blockedBy), true, {}});
    const QString progress = !d.progress.isEmpty() ? d.progress : d.subtaskCount > 0 ? rowWords("%n subtask(s)", d.subtaskCount) : QString();
    if (!progress.isEmpty())
        out.push_back({progress, false, d.subtaskCount > 0 ? u"fold"_s : QString()});
    if (!d.timeLabel.isEmpty())
        out.push_back({d.timeLabel, false, {}});
    return out;
}

// The value of the token a chip shows, as TasksView.qml's tokenValue() reads it from the roles.
QString tokenValue(const TasksView::RowData &d, const QString &kind)
{
    const QHash<QString, QString> values{{u"due"_s, d.due},
                                         {u"scheduled"_s, d.scheduled},
                                         {u"start"_s, d.start},
                                         {u"reminder"_s, d.reminderSpec},
                                         {u"recurrence"_s, d.recurrence}};
    return values.value(kind, d.priorityName);
}

// The priority's word (TasksRow's priorityLabel).
QString priorityLabel(const QString &name)
{
    static const QHash<QString, const char *> words{{u"highest"_s, "Highest"},
                                                     {u"high"_s, "High"},
                                                     {u"medium"_s, "Medium"},
                                                     {u"low"_s, "Low"},
                                                     {u"lowest"_s, "Lowest"}};
    const char *word = words.value(name, nullptr);
    return word ? rowWords(word) : QString();
}

// Where the task is written: its page (and folder), a day of the journal.
QString whereLabel(const TasksView::RowData &d)
{
    if (d.isJournal)
        return rowWords("Journal · %1").arg(d.pageTitle);
    return d.folder.isEmpty() ? d.pageTitle : d.pageTitle + u" · "_s + d.folder;
}

} // namespace

TasksView::TasksView(at::Context<TasksView> &cx, TaskListModel *model)
    : m_model(model)
    , m_id(cx.entityId())
    , m_rows(m_look.lineHeight + 30)
{
    // The model's changes: the list follows its rows, the windows draw again (rows gone — the
    // current task ticked off into Done, a filter —: the task now at its place is current, as a
    // ListView's currentIndex stays).
    at::App *app = &cx.app();
    const at::EntityId id = m_id;
    m_rows.follow(model, [app, id](bool rowsWent) {
        app->updateEntity<TasksView>(id, [rowsWent](TasksView &v, at::Context<TasksView> &c) {
            if (rowsWent)
                v.m_current = v.nearestTask(v.m_current);
            c.notify();
        });
    });
}

TasksView::~TasksView() = default;

void TasksView::setLook(TasksLook look, at::Context<TasksView> &cx)
{
    m_look = std::move(look);
    m_rows.remeasure();
    cx.notify();
}

TasksView::RowData TasksView::rowData(int row) const
{
    RowData d;
    if (!m_model || row < 0 || row >= m_model->rowCount())
        return d;
    const QModelIndex i = m_model->index(row);
    d.header = roleText(i, TaskListModel::RowTypeRole) == u"header";
    d.section = roleText(i, TaskListModel::SectionRole);
    d.sectionLabel = roleText(i, TaskListModel::SectionLabelRole);
    d.sectionCount = i.data(TaskListModel::SectionCountRole).toInt();
    d.collapsed = i.data(TaskListModel::CollapsedRole).toBool();
    d.key = roleText(i, TaskListModel::KeyRole);
    d.text = roleText(i, TaskListModel::TextRole);
    d.html = roleText(i, TaskListModel::HtmlRole);
    d.status = roleText(i, TaskListModel::StatusRole);
    d.path = roleText(i, TaskListModel::PathRole);
    d.line = i.data(TaskListModel::LineRole).toInt();
    d.pageTitle = roleText(i, TaskListModel::PageTitleRole);
    d.folder = roleText(i, TaskListModel::FolderRole);
    d.isJournal = i.data(TaskListModel::IsJournalRole).toBool();
    d.dueLabel = roleText(i, TaskListModel::DueLabelRole);
    d.overdue = i.data(TaskListModel::OverdueRole).toBool();
    d.scheduledLabel = roleText(i, TaskListModel::ScheduledLabelRole);
    d.startLabel = roleText(i, TaskListModel::StartLabelRole);
    d.recurrenceLabel = roleText(i, TaskListModel::RecurrenceLabelRole);
    d.reminderLabel = roleText(i, TaskListModel::ReminderLabelRole);
    d.reminderAlarm = i.data(TaskListModel::ReminderAlarmRole).toBool();
    d.priorityName = roleText(i, TaskListModel::PriorityNameRole);
    d.due = roleText(i, TaskListModel::DueRole);
    d.scheduled = roleText(i, TaskListModel::ScheduledRole);
    d.start = roleText(i, TaskListModel::StartRole);
    d.recurrence = roleText(i, TaskListModel::RecurrenceRole);
    d.reminderSpec = roleText(i, TaskListModel::ReminderSpecRole);
    d.blocked = i.data(TaskListModel::BlockedRole).toBool();
    d.blockedBy = roleText(i, TaskListModel::BlockedByRole);
    d.progress = roleText(i, TaskListModel::ProgressRole);
    d.timeLabel = roleText(i, TaskListModel::TimeLabelRole);
    d.clockRunning = i.data(TaskListModel::ClockRunningRole).toBool();
    d.depth = i.data(TaskListModel::DepthRole).toInt();
    d.subtaskCount = i.data(TaskListModel::SubtaskCountRole).toInt();
    d.folded = i.data(TaskListModel::FoldedRole).toBool();
    return d;
}

at::AnyElement TasksView::render(at::Window &, at::Context<TasksView> &cx)
{
    m_drawn.clear();
    m_drawnRows = 0;
    if (!m_focus)
        m_focus = cx.newFocusHandle();
    TasksView *self = this;
    const at::EntityId id = cx.entityId();
    return at::div()
            .id(u"tasks")
            .sizeFull()
            .flexCol()
            .bg(m_look.background)
            .trackFocus(m_focus)
            .onKeyDown([id](const at::KeyDownEvent &e, at::Window &w, at::App &a) {
                a.updateEntity<TasksView>(id, [&](TasksView &v, at::Context<TasksView> &c) {
                    if (v.keyDown(e, c))
                        w.stopPropagation();
                });
            })
            .child(at::list(u"tasks", m_rows.heights(),
                            [self](std::size_t row, at::Window &, at::App &) -> at::AnyElement { return self->renderRow(row); })
                           .trackScroll(m_rows.list())
                           .ariaLabel(u"Tasks")
                           .flex1());
}

at::AnyElement TasksView::renderRow(std::size_t row)
{
    const RowData d = rowData(int(row));
    return d.header ? headerRow(int(row), d) : taskRow(int(row), d);
}

at::AnyElement TasksView::tracker(int row, Part part)
{
    TasksView *self = this;
    return at::canvas({})
            .onPrepaint([self, row, part](at::Rect b, at::Window &, at::App &) {
                self->m_drawn.push_back({row, part, b});
                self->m_drawnRows += part == Part::Row ? 1 : 0;
            })
            .absolute()
            .inset(0);
}

// A section's header: its label and count (TasksView.qml's sectionHeader); a click folds it.
at::AnyElement TasksView::headerRow(int row, const RowData &d)
{
    const TasksView *self = this;
    const QString section = d.section;
    const QString label = d.sectionLabel + u"  "_s + QString::number(d.sectionCount);
    return at::div()
            .id(at::ElementId::number(qHash(u"header:"_s + d.section)))
            .relative()
            .flexRow()
            .itemsCenter()
            .gap(6)
            .px(m_look.sideMargin)
            .pt(14)
            .pb(4)
            .textSize(m_look.smallSize)
            .fontSemibold()
            .textColor(m_look.header)
            .cursorPointer()
            .role(at::Role::Heading)
            .ariaLevel(2)
            .ariaLabel(u16View(label))
            .expanded(!d.collapsed)
            .onClick([self, section](const at::ClickEvent &, at::Window &, at::App &) {
                self->act(u"toggleSection"_s, {{u"section"_s, section}});
            })
            .child(at::canvas([icon = d.collapsed ? u"chevron-right"_s : u"chevron-down"_s,
                               ink = m_look.header](at::Rect b, at::Window &w, at::App &) {
                       if (const auto i = editor::atr::pageIcon(icon))
                           w.paintIcon(*i, b, ink);
                   }).size(14))
            .child(at::text(u16View(label)))
            .child(tracker(row, Part::Row));
}

// A task: its box, its text and chips under it, indented by its depth; a click opens it, a
// right-click its menu.
at::AnyElement TasksView::taskRow(int row, const RowData &d)
{
    TasksView *self = this;
    const QVariantMap args = taskArgs(d);
    const bool current = row == m_current;
    at::Div column = at::div().flex1().minW(0).flexCol().gap(4).child(taskText(row, d)).child(chips(row, d));
    return at::div()
            .id(at::ElementId::number(qHash(d.key)))
            .relative()
            .flexRow()
            .itemsStart()
            .gap(10)
            .pl(m_look.sideMargin + float(d.depth) * m_look.indent)
            .pr(m_look.sideMargin)
            .py(8)
            .bg(current ? m_look.current : at::transparent())
            .hover([look = m_look.hover, current](at::Style &s) {
                if (!current)
                    s.background = at::Background::solid(look);
            })
            .cursorPointer()
            .onClick([id = m_id, row, args](const at::ClickEvent &, at::Window &, at::App &a) {
                a.updateEntity<TasksView>(id, [&](TasksView &v, at::Context<TasksView> &c) {
                    v.m_current = row;
                    c.notify();
                    v.act(u"open"_s, {{u"path"_s, args.value(u"path"_s)}, {u"line"_s, args.value(u"line"_s)}});
                });
            })
            .onMouseDown(at::MouseButton::Right,
                         [self, menu = menuArgs(d)](const at::MouseDownEvent &e, at::Window &, at::App &) {
                             self->menuAt(u"menu"_s, menu, e.position);
                         })
            .child(taskBox(row, d))
            .child(std::move(column))
            .child(tracker(row, Part::Row));
}

// The task's box (TasksRow's checkbox): filled with a check when it is done, a cross when cancelled.
at::AnyElement TasksView::taskBox(int row, const RowData &d)
{
    const TasksView *self = this;
    const QVariantMap args = taskArgs(d);
    const bool closed = d.closed();
    const at::Color ink = closed ? m_look.accent : m_look.box;
    const QString icon = d.status == u"cancelled" ? u"x"_s : u"check"_s;
    const at::Color background = m_look.background;
    return at::div()
            .id(u"box")
            .relative()
            .size(18)
            .mt((m_look.lineHeight - 18) / 2)
            .flexShrink0()
            .rounded(5)
            .border(1.5f)
            .borderColor(ink)
            .bg(closed ? ink : at::transparent())
            .cursorPointer()
            .role(at::Role::CheckBox)
            .checked(d.status == u"done")
            .ariaLabel(u16View(d.text))
            .onClick([self, args](const at::ClickEvent &, at::Window &, at::App &) { self->act(u"toggle"_s, args); })
            .onMouseDown(at::MouseButton::Right,
                         [self, menu = menuArgs(d)](const at::MouseDownEvent &e, at::Window &w, at::App &) {
                             self->menuAt(u"statusMenu"_s, menu, e.position);
                             w.stopPropagation();
                         })
            .when(closed,
                  [icon, background](at::Div &&box) {
                      return std::move(box).child(at::canvas([icon, background](at::Rect b, at::Window &w, at::App &) {
                          if (const auto i = editor::atr::pageIcon(icon))
                              w.paintIcon(*i, b.inset(at::Edges::all(2)), background);
                      }).absolute().inset(0));
                  })
            .child(tracker(row, Part::Box));
}

// The task's text: the model's rich text as spans, its links and tags clickable (a #tag filters,
// other links open: "link"), at most four lines.
at::AnyElement TasksView::taskText(int row, const RowData &d)
{
    const TasksView *self = this;
    const markdown::RenderedText rt = markdown::renderedTextFromHtml(d.html.isEmpty() ? d.text.toHtmlEscaped() : d.html);
    at::Text text = at::text(u16View(rt.text)).spans(editor::atr::renderedSpans(rt, m_look.text)).maxLines(4);
    const QString path = d.path;
    for (const editor::atr::RenderedLink &l : editor::atr::renderedLinks(rt)) {
        const QString url = l.href;
        std::move(text).link(
                l.range,
                [self, url, path](const at::ClickEvent &, at::Window &, at::App &) {
                    self->act(u"link"_s, {{u"url"_s, url}, {u"path"_s, path}});
                },
                [self, url, path](at::Point p, at::Window &, at::App &) {
                    self->menuAt(u"linkMenu"_s, {{u"url"_s, url}, {u"path"_s, path}}, p);
                });
    }
    return at::div()
            .relative()
            .textSize(m_look.textSize)
            .lineHeight(m_look.lineHeight)
            .textColor(d.closed() ? m_look.faint : m_look.text.text)
            .child(std::move(text))
            .child(tracker(row, Part::Text));
}

// The task's chips (TasksRow's Flow): its dates, reminder, recurrence, priority, dependencies,
// progress, time; then where it is written (a subtask's is its task's, shown above).
at::AnyElement TasksView::chips(int row, const RowData &d)
{
    at::Div flow = at::div().flexRow().flexWrap().itemsCenter().gap(4).textSize(m_look.smallSize).fontMedium();
    if (!m_look.uiFamily.empty())
        std::move(flow).fontFamily(m_look.uiFamily);
    std::vector<Chip> all = chipsOf(d);
    if (const QString p = priorityLabel(d.priorityName); !p.isEmpty())
        all.push_back({p, d.priorityName == u"highest" && !d.closed(), u"priority"_s});
    for (const Chip &c : all)
        std::move(flow).child(chip(row, c.text, c.danger, c.kind, d));
    if (d.depth == 0)
        std::move(flow).child(at::div()
                                      .h(20)
                                      .flexRow()
                                      .itemsCenter()
                                      .pl(2)
                                      .textColor(m_look.faint)
                                      .whitespaceNowrap()
                                      .overflowHidden()
                                      .child(at::text(u16View(whereLabel(d)))));
    return flow;
}

// A chip (BChip, small); one that shows a token opens its menu on a right-click (PlanningTokenMenu).
at::AnyElement TasksView::chip(int row, const QString &text, bool danger, const QString &kind, const RowData &d)
{
    at::Div pill = at::div()
                           .relative()
                           .h(20)
                           .flexRow()
                           .itemsCenter()
                           .px(7)
                           .roundedFull()
                           .whitespaceNowrap()
                           .bg(danger ? m_look.dangerChip : m_look.chip)
                           .textColor(danger ? m_look.danger : m_look.chipText)
                           .child(at::text(u16View(text)));
    if (kind.isEmpty())
        return pill;
    const TasksView *self = this;
    if (kind == u"fold")
        return std::move(pill)
                .cursorPointer()
                .role(at::Role::Button)
                .expanded(!d.folded)
                .ariaLabel(u16View(text))
                .onClick([self, key = d.key](const at::ClickEvent &, at::Window &, at::App &) {
                    self->act(u"fold"_s, {{u"key"_s, key}});
                });
    QVariantMap args = taskArgs(d);
    args.insert({{u"kind"_s, kind},
                 {u"value"_s, tokenValue(d, kind)},
                 {u"alarm"_s, d.reminderAlarm},
                 {u"rule"_s, d.recurrence},
                 {u"priority"_s, d.priorityName}});
    return std::move(pill).child(tracker(row, Part::Chip)).onMouseDown(at::MouseButton::Right,
                                       [self, args](const at::MouseDownEvent &e, at::Window &w, at::App &) {
                                           self->menuAt(u"tokenMenu"_s, args, e.position);
                                           w.stopPropagation();
                                       });
}

QVariantMap TasksView::taskArgs(const RowData &d) const
{
    return {{u"path"_s, d.path}, {u"line"_s, d.line}, {u"text"_s, d.text}, {u"status"_s, d.status}};
}

// A task's menus: its key and its clock's state besides taskArgs().
QVariantMap TasksView::menuArgs(const RowData &d) const
{
    QVariantMap args = taskArgs(d);
    args.insert(u"key"_s, d.key);
    args.insert(u"clockRunning"_s, d.clockRunning);
    return args;
}

void TasksView::act(const QString &action, const QVariantMap &args) const
{
    if (onAction)
        onAction(action, args);
}

void TasksView::menuAt(const QString &action, QVariantMap args, at::Point at) const
{
    args.insert(u"x"_s, qreal(at.x));
    args.insert(u"y"_s, qreal(at.y));
    act(action, args);
}

std::optional<at::Rect> TasksView::partBounds(int row, Part part) const
{
    for (const Drawn &d : m_drawn)
        if (d.row == row && d.part == part)
            return d.bounds;
    return std::nullopt;
}

TasksView::Part TasksView::partNamed(const QString &name)
{
    static const QHash<QString, Part> parts{{u"box"_s, Part::Box}, {u"text"_s, Part::Text}, {u"chip"_s, Part::Chip}};
    return parts.value(name, Part::Row);
}

namespace {

at::Color colorAt(const QVariantMap &look, const QString &key, at::Color fallback)
{
    const QColor c = look.value(key).value<QColor>();
    return c.isValid() ? at::Color{float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())} : fallback;
}

} // namespace

TasksLook TasksLook::fromMap(const QVariantMap &look)
{
    TasksLook t;
    t.text.text = colorAt(look, u"text"_s, t.text.text);
    t.text.muted = colorAt(look, u"muted"_s, t.text.muted);
    t.text.link = colorAt(look, u"link"_s, t.text.link);
    for (const auto &[key, color] : {std::pair{u"background"_s, &t.background}, std::pair{u"hover"_s, &t.hover},
                                     std::pair{u"current"_s, &t.current}, std::pair{u"header"_s, &t.header},
                                     std::pair{u"faint"_s, &t.faint}, std::pair{u"box"_s, &t.box},
                                     std::pair{u"accent"_s, &t.accent}, std::pair{u"danger"_s, &t.danger},
                                     std::pair{u"chip"_s, &t.chip}, std::pair{u"chipText"_s, &t.chipText},
                                     std::pair{u"dangerChip"_s, &t.dangerChip}})
        *color = colorAt(look, key, *color);
    for (const auto &[key, size] : {std::pair{u"textSize"_s, &t.textSize}, std::pair{u"lineHeight"_s, &t.lineHeight},
                                    std::pair{u"smallSize"_s, &t.smallSize}, std::pair{u"sideMargin"_s, &t.sideMargin},
                                    std::pair{u"indent"_s, &t.indent}})
        *size = look.value(key, *size).toFloat();
    t.uiFamily = look.value(u"uiFamily"_s).toString().toStdU16String();
    return t;
}

void TasksView::moveCurrent(int step, at::Context<TasksView> &cx)
{
    const int from = m_current < 0 && step > 0 ? 0 : m_current + step;
    const int r = from < 0 ? -1 : taskFrom(from, step);
    if (r < 0)
        return;
    m_current = r;
    revealRow(r, cx);
}

// The task at `row` or the first after it, else the last before it (−1: none, or `row` was none).
int TasksView::nearestTask(int row) const
{
    const int after = taskFrom(row, 1);
    return after >= 0 ? after : taskFrom(row - 1, -1);
}

// The first task from `row` on in `step`'s direction (rows past the end: from the last); −1: none.
int TasksView::taskFrom(int row, int step) const
{
    const int count = m_model ? m_model->rowCount() : 0;
    for (int r = std::min(row, count - 1); r >= 0 && r < count; r += step)
        if (!rowData(r).header)
            return r;
    return -1;
}

void TasksView::revealRow(int row, at::Context<TasksView> &cx)
{
    if (m_rows.reveal(row))
        cx.notify();
}

// ↑ / ↓ through the tasks, Space toggles the current one, Enter opens it.
bool TasksView::keyDown(const at::KeyDownEvent &e, at::Context<TasksView> &cx)
{
    const at::Keystroke &k = e.keystroke;
    if (k.modifiers.any())
        return false;
    if (k.key == u"down" || k.key == u"up") {
        moveCurrent(k.key == u"down" ? 1 : -1, cx);
        return true;
    }
    if (m_current < 0 || (k.key != u"space" && k.key != u"enter"))
        return false;
    const RowData d = rowData(m_current);
    if (k.key == u"space")
        act(u"toggle"_s, taskArgs(d));
    else
        act(u"open"_s, {{u"path"_s, d.path}, {u"line"_s, d.line}});
    return true;
}

} // namespace brulion::views::atr
