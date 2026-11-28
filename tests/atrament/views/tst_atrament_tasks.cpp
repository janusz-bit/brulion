// The Tasks view's list drawn by Atrament (src/app/views/atrament/tasksview.h; docs/ATRAMENT.md "The
// views in Atrament (M3)", Tasks): a TaskListModel over an in-memory VaultIndex drawn headless on
// Atrament's test platform — its sections and tasks as rows (a header with its count, a task's box,
// its text with clickable links and tags, its chips), what a click asks of the view (the box:
// "toggle" with the task's file and line; the row: "open"; a header: "toggleSection"; a tag: "link"),
// the right-click menus, the keys (↑/↓ through the tasks, Space, Enter), the rows for screen readers,
// and the rows following the model when a filter changes.
#include "modelrows.h"
#include "pageicons.h"
#include "planningtest.h"
#include "tasklistmodel.h"
#include "tasksview.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include <QColor>
#include <QStringListModel>
#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::planning;
using namespace brulion::views::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;
using brulion::editor::atr::u16View;

namespace {

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

// Today 2026-11-28: one task overdue, one today, one later, one without a date, one done.
const test::Files Vault = {
        {u"Dziennik/2026-11-28.md"_s, u"- [ ] Zadzwonić do Łucji 📅 2026-11-28\n- [ ] Kupić płytki 📅 2026-11-20\n"
                                      "- [x] Wymienić żarówkę 📅 2026-11-25 🔺 ✅ 2026-11-27\n"_s},
        {u"Projekty.md"_s, u"# Projekty\n- [ ] Napisać raport 📅 2026-12-15 ⏫\n- [ ] Przejrzeć notatki #praca\n"
                           "- [ ] Remont łazienki\n  - [ ] Kupić fugę\n"_s},
};

struct Harness {
    std::unique_ptr<VaultIndex> index;
    TaskListModel model;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<TasksView> view;
    QStringList actions; // "action key=value …", in the order asked

    explicit Harness(const test::Files &files = Vault, at::Size size = {520, 800})
        : index(test::makeIndex(files)), tw(platform->newWindow(size, 1))
    {
        model.setToday(u"2026-11-28"_s);
        model.setLocale(u"pl"_s);
        model.setVaultIndexObject(index.get());
        view = app.newEntity<TasksView>(&model);
        view.update(app, [this](TasksView &v, at::Context<TasksView> &) {
            v.onAction = [this](const QString &action, const QVariantMap &args) {
                QStringList parts{action};
                for (auto it = args.cbegin(); it != args.cend(); ++it)
                    if (it.key() != u"x" && it.key() != u"y")
                        parts.append(it.key() + u'=' + it.value().toString());
                actions.append(parts.join(u' '));
            };
        });
        app.openWindow(tw, view);
        window().setAccessibilityEnabled(true);
        tw.settle();
    }
    const TasksView &tasks() { return view.read(app); }
    at::Window &window() { return *app.windows().front(); }
    int rowOf(const QString &textPart)
    {
        for (int r = 0; r < model.rowCount(); ++r)
            if (model.data(model.index(r), TaskListModel::TextRole).toString().contains(textPart)
                || model.data(model.index(r), TaskListModel::SectionLabelRole).toString() == textPart)
                return r;
        return -1;
    }
    at::Rect part(int row, TasksView::Part p)
    {
        const std::optional<at::Rect> b = tasks().partBounds(row, p);
        return b.value_or(at::Rect{});
    }
    const at::AccessibilityNode *node(at::Role role, std::u16string_view name)
    {
        for (const at::AccessibilityNode *n : window().accessibilityTree().nodes())
            if (n->role == role && n->name.find(name) != std::u16string_view::npos)
                return n;
        return nullptr;
    }
    QString dump() { return str(window().accessibilityTree().dump()); }
    // The smallest node of any role whose name holds `name` (a row's list item is named by all its
    // text: the chip's own node is inside it).
    const at::AccessibilityNode *named(const QString &name)
    {
        const at::AccessibilityNode *best = nullptr;
        for (const at::AccessibilityNode *n : window().accessibilityTree().nodes())
            if (str(n->name).contains(name)
                && (!best || n->bounds.width() * n->bounds.height() < best->bounds.width() * best->bounds.height()))
                best = n;
        return best;
    }
};

} // namespace

class TestAtramentTasks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // (The app's icons from the source tree: the check of a done task's box, the chevrons.)
    void initTestCase() { editor::atr::setPageIconFolder(QStringLiteral(BRULION_SOURCE_DIR "/assets/icons")); }
    void sectionsAndTasksDrawn();
    void clicksAskTheView();
    void menusAtTheirPoint();
    void keysMoveToggleAndOpen();
    void rowsFollowTheModel();
    void subtasksFold();
    void lookFromItsMap();
    void currentTaskAtTheEnd();
    void chipsOfEveryKind();
    void doneTasksAndLinkMenus();
    void keysTheListLeavesAlone();
    void modelRowsFollowTheirModel();
    void headersCurrentAndLook();
    void nearestTasks();
};

// Every row of the model drawn by Atrament: the sections' headers with their counts (Done folded,
// its tasks left out), each task's box (checked when done), its text and its chips — the due date's
// label (an overdue one says so), the priority, the page it is written in.
void TestAtramentTasks::sectionsAndTasksDrawn()
{
    Harness h;
    QVERIFY(h.model.rowCount() >= 8);
    QCOMPARE(h.tasks().drawnRows(), h.model.rowCount());
    const at::AccessibilityNode *overdue = h.node(at::Role::Heading, u"Przeterminowane");
    QVERIFY2(overdue || h.node(at::Role::Heading, u"Overdue"), qPrintable(h.dump()));
    const at::AccessibilityNode *box = h.node(at::Role::CheckBox, u"Kupić płytki");
    QVERIFY2(box, qPrintable(h.dump()));
    QVERIFY(box->states.checkable);
    QVERIFY(!box->states.checked);
    // The chips: the overdue date, the priority; the page of a task in a page (not of a day's).
    const QString dump = h.dump();
    const QString due = h.model.data(h.model.index(h.rowOf(u"Kupić"_s)), TaskListModel::DueLabelRole).toString();
    QVERIFY(!due.isEmpty());
    QVERIFY2(dump.contains(due), qPrintable(dump));
    QVERIFY2(dump.contains(u"Projekty"_s), qPrintable(dump));
    const QString priority = h.model.data(h.model.index(h.rowOf(u"raport"_s)), TaskListModel::PriorityNameRole).toString();
    QVERIFY(!priority.isEmpty());
    // Each task row below its header, in the model's order.
    float last = -1;
    for (int r = 0; r < h.model.rowCount(); ++r) {
        const at::Rect b = h.part(r, TasksView::Part::Row);
        QVERIFY2(b.height() > 0 && b.top() >= last, qPrintable(u"row %1 at %2"_s.arg(r).arg(b.top())));
        last = b.top();
    }
}

// The box: "toggle" with the task's file, line, text and status; the row: "open" at its line; a
// section's header: "toggleSection"; a #tag: "link" with its URL and the task's file.
void TestAtramentTasks::clicksAskTheView()
{
    Harness h;
    const int task = h.rowOf(u"Kupić płytki"_s);
    QVERIFY(task >= 0);
    h.tw.simulateClick(h.part(task, TasksView::Part::Box).center());
    QCOMPARE(h.actions.size(), 1);
    const QString text = h.model.data(h.model.index(task), TaskListModel::TextRole).toString();
    QCOMPARE(h.actions.last(), u"toggle line=1 path=Dziennik/2026-11-28.md status=todo text="_s + text);
    const at::Rect row = h.part(task, TasksView::Part::Row);
    h.tw.simulateClick({row.right() - 4, row.top() + 4});
    QCOMPARE(h.actions.last(), u"open line=1 path=Dziennik/2026-11-28.md"_s);
    const int header = h.rowOf(h.model.data(h.model.index(0), TaskListModel::SectionLabelRole).toString());
    QCOMPARE(header, 0);
    h.tw.simulateClick(h.part(0, TasksView::Part::Row).center());
    QVERIFY2(h.actions.last().startsWith(u"toggleSection section="_s), qPrintable(h.actions.last()));
    const at::AccessibilityNode *tag = h.node(at::Role::Link, u"praca");
    QVERIFY2(tag, qPrintable(h.dump()));
    h.tw.simulateClick(tag->bounds.center());
    QCOMPARE(h.actions.last(), u"link path=Projekty.md url=tag:praca"_s);
}

// A right-click on the box: its status menu; on the row: the task's menu; each at the click's point.
void TestAtramentTasks::menusAtTheirPoint()
{
    Harness h;
    QVariantMap last;
    h.view.update(h.app, [&](TasksView &v, at::Context<TasksView> &) {
        v.onAction = [&](const QString &action, const QVariantMap &args) {
            last = args;
            last.insert(u"action"_s, action);
        };
    });
    const int task = h.rowOf(u"raport"_s);
    const at::Point onBox = h.part(task, TasksView::Part::Box).center();
    h.tw.simulateClick(onBox, at::MouseButton::Right);
    QCOMPARE(last.value(u"action"_s).toString(), u"statusMenu"_s);
    QCOMPARE(last.value(u"path"_s).toString(), u"Projekty.md"_s);
    QCOMPARE(last.value(u"line"_s).toInt(), 1);
    QCOMPARE(last.value(u"x"_s).toReal(), qreal(onBox.x));
    QCOMPARE(last.value(u"y"_s).toReal(), qreal(onBox.y));
    const at::Rect row = h.part(task, TasksView::Part::Row);
    h.tw.simulateClick({row.right() - 4, row.top() + 4}, at::MouseButton::Right);
    QCOMPARE(last.value(u"action"_s).toString(), u"menu"_s);
    QCOMPARE(last.value(u"key"_s).toString(), u"Projekty.md:1"_s);
    // A right-click on a chip: its token's menu, with the token's value.
    const at::Rect chip = h.part(task, TasksView::Part::Chip); // (the due date's, the first)
    QVERIFY(chip.width() > 0);
    h.tw.simulateClick(chip.center(), at::MouseButton::Right);
    QCOMPARE(last.value(u"action"_s).toString(), u"tokenMenu"_s);
    QCOMPARE(last.value(u"kind"_s).toString(), u"due"_s);
    QCOMPARE(last.value(u"value"_s).toString(), h.model.data(h.model.index(task), TaskListModel::DueRole).toString());
    QCOMPARE(last.value(u"path"_s).toString(), u"Projekty.md"_s);
    const QString priority = h.model.data(h.model.index(task), TaskListModel::PriorityNameRole).toString();
    QVERIFY(!priority.isEmpty());
}

// ↓ / ↑ move through the tasks (headers skipped), the current one marked for screen readers; Space
// toggles it, Enter opens it.
void TestAtramentTasks::keysMoveToggleAndOpen()
{
    Harness h;
    h.tw.simulateClick(h.part(h.rowOf(u"Kupić"_s), TasksView::Part::Text).center());
    h.actions.clear();
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Kupić"_s));
    h.tw.simulateKeystrokes(u"down");
    h.tw.settle();
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Zadzwonić"_s)); // past Today's header
    h.tw.simulateKeystrokes(u"space");
    QVERIFY2(h.actions.size() == 1 && h.actions.last().startsWith(u"toggle "_s) && h.actions.last().contains(u"Zadzwonić"_s),
             qPrintable(h.actions.join(u" | "_s)));
    h.tw.simulateKeystrokes(u"enter");
    QCOMPARE(h.actions.last(), u"open line=0 path=Dziennik/2026-11-28.md"_s);
    h.tw.simulateKeystrokes(u"up");
    h.tw.settle();
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Kupić"_s));
    h.tw.simulateKeystrokes(u"up"); // the first task: it stays
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Kupić"_s));
    const at::AccessibilityNode *box = h.node(at::Role::CheckBox, u"Kupić płytki");
    QVERIFY(box);
    // The current task gone from the list (a filter, ticked off into Done): the task at its place.
    h.model.setTagFilter(u"praca"_s);
    h.tw.settle();
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Przejrzeć notatki"_s));
    h.model.setTagFilter(u"nic-takiego"_s);
    h.tw.settle();
    QCOMPARE(h.tasks().currentRow(), -1);
}

// A filter set on the model: the rows that went are gone from the view, the rows left drawn.
void TestAtramentTasks::rowsFollowTheModel()
{
    Harness h;
    const int before = h.model.rowCount();
    h.model.setTagFilter(u"praca"_s);
    h.tw.settle();
    QVERIFY(h.model.rowCount() < before);
    QCOMPARE(h.tasks().drawnRows(), h.model.rowCount());
    QVERIFY(h.node(at::Role::CheckBox, u"Przejrzeć notatki"));
    QVERIFY(!h.node(at::Role::CheckBox, u"Kupić płytki"));
    h.model.setTagFilter(QString());
    h.tw.settle();
    QCOMPARE(h.tasks().drawnRows(), before);
    QVERIFY(h.node(at::Role::CheckBox, u"Kupić płytki"));
}

// A task with subtasks listed under it: its subtask indented, its chip ("1 subtask") folds them.
void TestAtramentTasks::subtasksFold()
{
    Harness h;
    const int parent = h.rowOf(u"Remont"_s);
    const int child = h.rowOf(u"Kupić fugę"_s);
    QVERIFY(parent >= 0 && child == parent + 1);
    QVERIFY(h.part(child, TasksView::Part::Box).left() > h.part(parent, TasksView::Part::Box).left() + 10);
    const QString chip = h.model.data(h.model.index(parent), TaskListModel::ProgressRole).toString();
    const at::AccessibilityNode *fold = h.node(at::Role::Button, chip.isEmpty() ? u"subtask" : u16View(chip));
    QVERIFY2(fold, qPrintable(h.dump()));
    h.tw.simulateClick(fold->bounds.center());
    QCOMPARE(h.actions.last(), u"fold key=Projekty.md:3"_s);
    // A flat list (no subtasks under it): its progress is no button.
    h.model.setNestSubtasks(false);
    h.tw.settle();
    QVERIFY(h.model.data(h.model.index(h.rowOf(u"Remont"_s)), TaskListModel::SubtaskCountRole).toInt() == 0);
    QVERIFY2(!h.node(at::Role::Button, chip.isEmpty() ? u"subtask" : u16View(chip)), qPrintable(h.dump()));
}

// AtramentView.viewLook as TasksLook: colours (a QColor or a name), sizes, the UI font, the text's
// palette; what the map lacks keeps its default; a part by its name.
void TestAtramentTasks::lookFromItsMap()
{
    const TasksLook defaults;
    const TasksLook look = TasksLook::fromMap({{u"background"_s, QColor(u"#102030"_s)},
                                               {u"danger"_s, u"#ff0000"_s},
                                               {u"text"_s, QColor(Qt::black)},
                                               {u"textSize"_s, 18},
                                               {u"indent"_s, 30.5},
                                               {u"uiFamily"_s, u"Brulion Inter"_s}});
    QCOMPARE(look.background, at::rgb(0x102030));
    QCOMPARE(look.danger, at::rgb(0xff0000));
    QCOMPARE(look.text.text, at::rgb(0x000000));
    QCOMPARE(look.textSize, 18.0f);
    QCOMPARE(look.indent, 30.5f);
    QCOMPARE(look.lineHeight, defaults.lineHeight);
    QCOMPARE(look.accent, defaults.accent);
    QCOMPARE(look.text.link, defaults.text.link);
    QVERIFY(look.uiFamily == u"Brulion Inter");
    QCOMPARE(TasksView::partNamed(u"box"_s), TasksView::Part::Box);
    QCOMPARE(TasksView::partNamed(u"chip"_s), TasksView::Part::Chip);
    QCOMPARE(TasksView::partNamed(u"text"_s), TasksView::Part::Text);
    QCOMPARE(TasksView::partNamed(u"anything"_s), TasksView::Part::Row);
}

// The current task the last one and the rows after the remaining tasks gone: the task before it.
void TestAtramentTasks::currentTaskAtTheEnd()
{
    Harness h;
    h.tw.simulateClick(h.part(h.rowOf(u"Kupić fugę"_s), TasksView::Part::Text).center());
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Kupić fugę"_s));
    h.model.setTagFilter(u"praca"_s);
    h.tw.settle();
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Przejrzeć notatki"_s));
    h.tw.simulateKeystrokes(u"down"); // the last: it stays
    QCOMPARE(h.tasks().currentRow(), h.rowOf(u"Przejrzeć notatki"_s));
}

// Every chip TasksRow has, in its colours: an overdue date and "Waits for" in the danger colours,
// the scheduled and start dates, the recurrence, the priority not; where a daily note's task is
// written ("Journal · …"); a plain task none but where it is.
void TestAtramentTasks::chipsOfEveryKind()
{
    Harness h({{u"journals/2026-11-28.md"_s, u"- [ ] Zapłacić rachunek 📅 2026-11-20 ⏳ 2026-11-29 🛫 2026-11-25 🔁 every week "
                                              "⏫ ⛔ zam1 ⏰ 2026-11-29 09:00\n- [ ] Zamówić węgiel 🆔 zam1\n- [ ] Zwykłe zadanie\n"
                                              "- DOING Napisać raport\n  :LOGBOOK:\n"
                                              "  CLOCK: [2026-11-28 Sat 08:00:00]--[2026-11-28 Sat 09:05:00] =>  01:05:00\n"
                                              "  :END:\n"_s}});
    const int bill = h.rowOf(u"Zapłacić"_s);
    QVERIFY(bill >= 0);
    const QString dump = h.dump();
    const auto data = [&](int role) { return h.model.data(h.model.index(bill), role).toString(); };
    const QString time = h.model.data(h.model.index(h.rowOf(u"Napisać raport"_s)), TaskListModel::TimeLabelRole).toString();
    for (const QString &label : {data(TaskListModel::ScheduledLabelRole), data(TaskListModel::RecurrenceLabelRole),
                                 data(TaskListModel::StartLabelRole), data(TaskListModel::ReminderLabelRole), time}) {
        QVERIFY(!label.isEmpty());
        QVERIFY2(dump.contains(label), qPrintable(label + u" in "_s + dump));
    }
    QVERIFY2(dump.contains(u"Waits for: Zamówić węgiel"_s), qPrintable(dump));
    QVERIFY2(dump.contains(u"Journal · "_s), qPrintable(dump));
    // The danger colours: the overdue date and the wait, nothing else.
    int danger = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == h.tasks().look().dangerChip)
            ++danger;
    QCOMPARE(danger, 2);
    // A plain task: no chip of subtasks, no progress.
    QVERIFY2(!dump.contains(u"subtask"_s), qPrintable(dump));
    QCOMPARE(h.tasks().rowData(-1).key, QString());
    QVERIFY(!h.tasks().partBounds(999, TasksView::Part::Row));
}

// A done task (Done unfolded): its box checked (a check drawn in it); a right-click on a #tag: the
// link's menu with the task's file; the UI font from the look.
void TestAtramentTasks::doneTasksAndLinkMenus()
{
    Harness h;
    QVariantMap last;
    h.view.update(h.app, [&](TasksView &v, at::Context<TasksView> &cx) {
        TasksLook look = v.look();
        look.uiFamily = u"Brulion Inter";
        v.setLook(look, cx);
        v.onAction = [&](const QString &action, const QVariantMap &args) {
            last = args;
            last.insert(u"action"_s, action);
        };
    });
    h.model.toggleSection(u"done"_s);
    h.tw.settle();
    const at::AccessibilityNode *done = h.node(at::Role::CheckBox, u"Wymienić żarówkę");
    QVERIFY2(done, qPrintable(h.dump()));
    QVERIFY(done->states.checked);
    // Its past due date and its highest priority are no danger: it is done (only Kupić's is).
    int danger = 0;
    for (const at::Primitive &p : h.tw.lastScene()->primitives())
        if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == h.tasks().look().dangerChip)
            ++danger;
    QCOMPARE(danger, 1);
    const at::AccessibilityNode *tag = h.node(at::Role::Link, u"praca");
    QVERIFY(tag);
    h.tw.simulateClick(tag->bounds.center(), at::MouseButton::Right);
    QCOMPARE(last.value(u"action"_s).toString(), u"linkMenu"_s);
    QCOMPARE(last.value(u"url"_s).toString(), u"tag:praca"_s);
    QCOMPARE(last.value(u"path"_s).toString(), u"Projekty.md"_s);
}

// Keys with a modifier are not the list's; Space and Enter with no current task do nothing.
void TestAtramentTasks::keysTheListLeavesAlone()
{
    Harness h;
    h.view.update(h.app, [&](TasksView &v, at::Context<TasksView> &) { h.window().focus(v.focusHandle()); });
    h.tw.simulateKeystrokes(u"space enter");
    QVERIFY(h.actions.isEmpty());
    h.tw.simulateKeystrokes(u"ctrl-down");
    QCOMPARE(h.tasks().currentRow(), -1);
    h.tw.simulateKeystrokes(u"down");
    QVERIFY(h.tasks().currentRow() > 0);
}

// ModelRows over a plain list model: a row per row, rows inserted, removed and moved in place,
// the model reset; a row's data changed makes its height an estimate again; each change told
// (rows gone: true); a row revealed only when it exists.
void TestAtramentTasks::modelRowsFollowTheirModel()
{
    QStringListModel model({u"a"_s, u"b"_s, u"c"_s});
    ModelRows rows(20);
    QList<bool> told;
    rows.follow(&model, [&](bool went) { told.append(went); });
    QCOMPARE(rows.heights().count(), std::size_t(3));
    rows.heights().setMeasured(1, 33);
    model.insertRows(1, 2);
    QCOMPARE(rows.heights().count(), std::size_t(5));
    QCOMPARE(rows.heights().heightOf(3), 33.0f); // "b" moved down with its height
    QCOMPARE(told, QList<bool>{false});
    model.removeRows(0, 1);
    QCOMPARE(rows.heights().count(), std::size_t(4));
    QCOMPARE(rows.heights().heightOf(2), 33.0f);
    QCOMPARE(told.last(), true);
    QVERIFY(model.moveRows({}, 2, 1, {}, 0)); // "b" to the top
    QCOMPARE(rows.heights().count(), std::size_t(4));
    QCOMPARE(rows.heights().heightOf(0), 33.0f);
    QCOMPARE(told.last(), true);
    QVERIFY(model.moveRows({}, 0, 1, {}, 3)); // and down again, before the last
    QCOMPARE(rows.heights().heightOf(2), 33.0f);
    QVERIFY(model.moveRows({}, 2, 1, {}, 0));
    // Two rows moved keep their heights, in their order.
    rows.heights().setMeasured(1, 44);
    QVERIFY(model.moveRows({}, 0, 2, {}, 4)); // rows 0, 1 behind the last two
    QCOMPARE(rows.heights().heightOf(2), 33.0f);
    QCOMPARE(rows.heights().heightOf(3), 44.0f);
    QVERIFY(model.moveRows({}, 2, 2, {}, 0));
    // The list's anchor follows its row: rows in front of it inserted and removed.
    rows.list().scrollToAnchor(2, 5);
    model.insertRows(0, 3);
    QCOMPARE(rows.list().anchorRow(), std::size_t(5));
    model.removeRows(0, 2);
    QCOMPARE(rows.list().anchorRow(), std::size_t(3));
    QVERIFY(model.moveRows({}, 0, 1, {}, 5)); // a row from in front of it to behind it
    QCOMPARE(rows.list().anchorRow(), std::size_t(2));
    model.removeRows(0, 1);
    model.insertRows(0, 1);
    rows.heights().setMeasured(0, 41);
    model.setData(model.index(0), u"B"_s);
    QVERIFY(!rows.heights().isMeasured(0));
    QCOMPARE(told.last(), false);
    model.setStringList({u"x"_s});
    QCOMPARE(rows.heights().count(), std::size_t(1));
    QCOMPARE(told.last(), true);
    QVERIFY(rows.reveal(0));
    QVERIFY(!rows.reveal(1));
    QVERIFY(!rows.reveal(-1));
    ModelRows none(20);
    none.follow(nullptr, [&](bool) { told.append(false); });
    QCOMPARE(none.heights().count(), std::size_t(0));
}

// A header says whether its section is folded; only the current task has the current row's colour;
// a bigger text size from the look measures the rows again (they grow).
void TestAtramentTasks::headersCurrentAndLook()
{
    Harness h;
    QVERIFY(h.node(at::Role::Heading, u"Overdue")->states.expanded);
    QCOMPARE(h.node(at::Role::Heading, u"Overdue")->level, 2);
    QVERIFY(!h.node(at::Role::Heading, u"Done")->states.expanded);
    auto currentQuads = [&] {
        int n = 0;
        for (const at::Primitive &p : h.tw.lastScene()->primitives())
            if (const auto *q = std::get_if<at::Quad>(&p); q && q->background.color == h.tasks().look().current)
                ++n;
        return n;
    };
    QCOMPARE(currentQuads(), 0);
    h.view.update(h.app, [&](TasksView &v, at::Context<TasksView> &cx) { v.moveCurrent(1, cx); });
    h.tw.settle();
    QCOMPARE(currentQuads(), 1);
    const int task = h.rowOf(u"raport"_s);
    const float before = h.part(task, TasksView::Part::Row).height();
    h.view.update(h.app, [](TasksView &v, at::Context<TasksView> &cx) {
        TasksLook look = v.look();
        look.textSize = 28;
        look.lineHeight = 40;
        v.setLook(look, cx);
    });
    h.tw.settle();
    QVERIFY2(h.part(task, TasksView::Part::Row).height() > before + 10,
             qPrintable(u"%1 → %2"_s.arg(before).arg(h.part(task, TasksView::Part::Row).height())));
}

// The task at a row or the first after it, else the last before it: from a header, from the last
// row (Done's header), from nowhere.
void TestAtramentTasks::nearestTasks()
{
    Harness h;
    const int last = h.model.rowCount() - 1;
    QCOMPARE(h.model.data(h.model.index(last), TaskListModel::RowTypeRole).toString(), u"header"_s);
    QCOMPARE(h.tasks().nearestTask(0), 1);
    QCOMPARE(h.tasks().nearestTask(last), h.rowOf(u"Kupić fugę"_s));
    QCOMPARE(h.tasks().nearestTask(last + 5), h.rowOf(u"Kupić fugę"_s));
    QCOMPARE(h.tasks().nearestTask(-1), -1);
}

QTEST_GUILESS_MAIN(TestAtramentTasks)
#include "tst_atrament_tasks.moc"
