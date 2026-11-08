#include "shellselftest.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "demovault.h"
#include "keyboardinsets.h"
#include "shortcutmap.h"
#include "traypopup.h"
#include "uidriver.h"
#include "workspace.h"
#include "workspacesync.h"

#include "drawing/backdrop.h"
#include "drawing/drawing.h"
#include "drawing/svgwrite.h"
#include "log/categories.h"
#include "log/logger.h"
#include "log/redact.h"
#include "notebook/share.h"
#include "openexternal.h"
#include "pdfpages.h"
#include "sync/synchistory.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QClipboard>
#include <QColor>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputDevice>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLocale>
#include <QLoggingCategory>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScreen>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrlQuery>
#include <QWheelEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

// Input and item finding: uidriver.h (shared with the user journey and the remote control).
using uidriver::closeMenus;
using uidriver::enclosingFlickable;
using uidriver::findShown;
using uidriver::hasAncestor;
using uidriver::isA;
using uidriver::linkPoint;
using uidriver::openMenus;
using uidriver::pointIn;
using uidriver::renderedTextContaining;
using uidriver::settle;
using uidriver::shown;
using uidriver::shownNamed;
using uidriver::waitFor;

/// Press and release `key` in the window, straight to the focused item (window Shortcut objects
/// are not involved). True when an item handled it (accepted the press or the release) — an
/// unhandled back key is what lets Android close the app.
bool sendKey(QQuickWindow *window, int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier,
             const QString &text = QString())
{
    if (qEnvironmentVariableIsSet("BRULION_SELFTEST_DEBUG")) {
        const QQuickItem *focus = window->activeFocusItem();
        const QString name = focus ? QString::fromLatin1(focus->metaObject()->className()) + u' ' + focus->objectName()
                                   : u"none"_s;
        std::fprintf(stderr, "selftest: key %d, window active %d, focus on %s\n", key, window->isActive(),
                     qPrintable(name));
    }
    return uidriver::sendKey(window, key, modifiers, text, 50); // then bindings, transitions and queued calls settle
}

/// The objectName of the item with the keyboard focus ("" for none or an unnamed one).
QString focusName(const QQuickWindow *window)
{
    const QQuickItem *focus = window->activeFocusItem();
    return focus ? focus->objectName() : QString();
}

/// A tap with one finger at `scenePos`, as a touch screen sends it (press, `holdMs`, release).
void touchTap(QQuickWindow *window, QPointF scenePos, int holdMs = 110)
{
    uidriver::touchTapNow(window, scenePos, holdMs);
}

/// A finger dragged from `from` by `delta` (scene coordinates) in small steps, then lifted.
void touchDrag(QQuickWindow *window, QPointF from, QPointF delta)
{
    uidriver::touchDragNow(window, from, delta);
}

/// The mouse pointer moved to `scenePos`, no button pressed (hover).
void mouseMove(QQuickWindow *window, QPointF scenePos)
{
    uidriver::mouseMoveNow(window, scenePos);
}

/// A click (left by default; with `modifiers` held) at `scenePos` of the window, as a real mouse
/// sends it.
void mouseClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button = Qt::LeftButton,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    uidriver::mouseClickNow(window, scenePos, button, modifiers);
}

/// A left-button drag with the mouse from `from` to `to` (scene coordinates) in small steps; held
/// there `holdMs`, then — unless `escape` (Escape pressed first, the drag cancelled) — released.
void mouseDrag(QQuickWindow *window, QPointF from, QPointF to, int holdMs = 0, bool escape = false)
{
    uidriver::mouseDragNow(window, from, to, holdMs, escape);
}

/// A tap on touch UIs (the phone run), a left click otherwise.
void tapOrClick(QQuickWindow *window, QPointF scenePos, bool touch)
{
    uidriver::tapOrClickNow(window, scenePos, touch);
}

/// Does a press at `item`'s centre reach it (or an item inside it)? A button that has just
/// appeared in a layout keeps its old place until the layout's polish runs: tapped before that,
/// the press lands on whatever the layout has already moved there.
bool takesPressAtCentre(QQuickWindow *window, QQuickItem *item)
{
    for (QQuickItem *p = item ? uidriver::pressTargetAt(window, pointIn(item)) : nullptr; p; p = p->parentItem()) {
        if (p == item)
            return true;
    }
    return false;
}

/// `word` typed on an on-screen keyboard that still composes it: preedit text of `item`, nothing
/// committed (Android's keyboards keep every word so until a space or a tap elsewhere).
void composeWord(QQuickItem *item, const QString &word)
{
    if (!item)
        return;
    QInputMethodEvent event(word, {{QInputMethodEvent::Cursor, int(word.size()), 1, QVariant()}});
    QCoreApplication::sendEvent(item, &event);
}

/// Close the tooltips waiting for deletion, and return those still alive of hidden controls. A
/// tooltip whose Loader went inactive (BIconButton's, when its button hid: the hover ends) has
/// its bindings cleared at once but is deleted — and only so closed — when the app's event loop
/// runs, which this running self-test does not let it do: open, it takes the next press
/// elsewhere just to close itself. A live tooltip of a hidden control would do that in the app
/// too (a bug).
QStringList closeToolTipsWaitingForDeletion(QQuickWindow *window)
{
    QStringList stray;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickPopupItem") && item->parent() && item->parent()->inherits("QQuickToolTip")) {
            QObject *tip = item->parent();
            const auto *owner = tip->property("parent").value<QQuickItem *>();
            const QQmlContext *context = qmlContext(tip);
            const bool deleted = !owner || !owner->window() || !context || !context->isValid();
            const QString text = tip->property("text").toString();
            std::fprintf(stderr, "selftest: an open tooltip \"%s\" (%s)\n", qPrintable(text),
                         deleted              ? "waiting for deletion: closed"
                         : owner->isVisible() ? "of a control shown"
                                              : "of a hidden control");
            if (deleted)
                QMetaObject::invokeMethod(tip, "close");
            else if (!owner->isVisible())
                stray.append(text);
        }
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            visit(child);
    };
    visit(window->contentItem());
    settle(50);
    return stray;
}

/// Right-click TapHandlers on screen that a finger triggers too: Qt hands touch points (they have
/// no buttons) to a TapHandler whatever its acceptedButtons, so unless acceptedDevices leaves the
/// touchscreen out, every tap also does the right-click's job. Only items a finger can reach
/// (lists also hold pooled delegates and ones still being created, whose bindings have not run).
QStringList fingerRightClickHandlers(QQuickWindow *window)
{
    QStringList found;
    const QRectF screen(0, 0, window->width(), window->height());
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        const QObjectList children = item->children();
        for (const QObject *child : children) {
            if (!child->inherits("QQuickTapHandler") || !shown(item) || !screen.contains(pointIn(item)))
                continue;
            const auto buttons = child->property("acceptedButtons").value<Qt::MouseButtons>();
            const auto devices = child->property("acceptedDevices").value<QInputDevice::DeviceTypes>();
            if (buttons.testFlag(Qt::RightButton) && !buttons.testFlag(Qt::LeftButton)
                && devices.testFlag(QInputDevice::DeviceType::TouchScreen))
                found.append(QString::fromLatin1(item->metaObject()->className()) + u" (\""_s
                             + item->property("text").toString().left(30) + u"\")"_s);
        }
        const QList<QQuickItem *> items = item->childItems();
        for (QQuickItem *childItem : items)
            visit(childItem);
    };
    visit(window->contentItem());
    return found;
}

using Check = std::function<void(bool, const char *)>;

/// 5e'. The calendar's swipe to the previous week / month (phones, real touch input) against the
///      drawer's edge strip (checkDrawerSwipe): the grids reach the window's left edge and swipe
///      sideways from there, so on them there is no strip — a finger that starts at the edge (or,
///      with Android's back zone, just beyond it) and moves right shows the previous period, the
///      drawer stays closed. The drag is not a fast flick: such a swipe once did nothing (its
///      length was read after Qt had reset it). Menu still opens the drawer, a swipe on it closes it
///      (the grid under it took that swipe); the agenda (a vertical list) has the strip. Runs on
///      the calendar `calendarView` shows, switched between its modes and back, the drawer
///      closed.
void checkCalendarEdgeSwipe(QQuickWindow *window, QQuickItem *calendarView, const Check &check)
{
    QObject *drawer = window->findChild<QObject *>(u"shellDrawer"_s);
    QQuickItem *shellItem = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
    if (!drawer || !shellItem) {
        check(false, "the shell and its drawer exist");
        return;
    }
    const auto run = [&](const QString &js) {
        QQmlExpression expression(qmlContext(shellItem), shellItem, js);
        expression.evaluate();
        if (expression.hasError())
            std::fprintf(stderr, "selftest: %s\n", qPrintable(expression.error().toString()));
        return !expression.hasError();
    };
    const auto opened = [drawer] { return drawer->property("opened").toBool(); };
    const auto closed = [drawer] { return !drawer->property("visible").toBool(); };
    const auto margin = [drawer] { return drawer->property("dragMargin").toReal(); };
    const QVariant mode = calendarView->property("mode");
    run(u"GestureInsets.simulate(32, 32)"_s);
    for (const char *gridMode : {"week", "month"}) {
        const bool week = qstrcmp(gridMode, "week") == 0;
        calendarView->setProperty("mode", QString::fromLatin1(gridMode));
        const char *type = week ? "CalendarWeekGrid" : "CalendarMonthGrid";
        QQuickItem *grid = nullptr;
        waitFor([&] { return (grid = findShown(calendarView, [type](QQuickItem *item) { return isA(item, type); })); });
        auto *calendar = grid ? grid->property("calendar").value<QObject *>() : nullptr;
        if (!calendar) {
            check(false, "the calendar shows its grid");
            continue;
        }
        settle(300);
        const qreal y = grid->mapToScene(QPointF(0, grid->height() / 2)).y();
        check(grid->mapToScene(QPointF(0, 0)).x() < 1 && waitFor([&] { return margin() <= 0; }),
              week ? "the week grid reaches the left edge, and there is no edge strip on it"
                   : "the month grid reaches the left edge, and there is no edge strip on it");
        // From the very edge (Android's back gesture there on a phone with gesture navigation),
        // and from just beyond the system's 32 px zone, where the strip would lie.
        for (const qreal x : {qreal(6), qreal(32 + 12)}) {
            const QString before = calendar->property("anchorDate").toString();
            touchDrag(window, QPointF(x, y), QPointF(window->width() * 0.5, 0));
            const bool previous = waitFor([&] {
                const QString now = calendar->property("anchorDate").toString();
                return !now.isEmpty() && now < before;
            });
            check(previous && closed(),
                  week ? (x < 32 ? "a swipe right on the week from the left edge shows the previous week, the drawer "
                                   "stays closed"
                                 : "a swipe right on the week from beyond the system's zone shows the previous week, "
                                   "the drawer stays closed")
                       : (x < 32 ? "a swipe right on the month from the left edge shows the previous month, the "
                                   "drawer stays closed"
                                 : "a swipe right on the month from beyond the system's zone shows the previous "
                                   "month, the drawer stays closed"));
            if (!closed()) {
                QMetaObject::invokeMethod(drawer, "close");
                waitFor(closed);
            }
        }
        QMetaObject::invokeMethod(calendar, "goToday");
    }
    // The drawer opened by its button (here by a call) closes with a swipe on it, no strip needed.
    QMetaObject::invokeMethod(drawer, "open");
    check(waitFor(opened, 3000) && margin() <= 0, "on the calendar's grid the drawer opens by its button");
    const qreal w = drawer->property("width").toReal();
    touchDrag(window, QPointF(w * 0.7, window->height() * 0.45), QPointF(-w * 0.6, 0));
    check(waitFor(closed, 3000), "on the calendar's grid a swipe to the left closes the drawer");
    if (!closed()) {
        QMetaObject::invokeMethod(drawer, "close");
        waitFor(closed);
    }
    calendarView->setProperty("mode", u"agenda"_s);
    const qreal strip = shellItem->property("edgeSwipeStrip").toReal();
    check(strip > 0 && waitFor([&] { return qAbs(margin() - (32 + strip)) <= 1; }),
          "the calendar's agenda has the edge strip");
    run(u"GestureInsets.simulate(-1, -1)"_s);
    calendarView->setProperty("mode", mode);
    settle(200);
}

/// 3c. Task statuses wherever a checkbox is shown, with real input: a right-click (desktop) or a
///     long press (--touch) on the checkbox opens the status menu with the task's status checked
///     and does not toggle it; a click / tap on a status writes it — in Tasks, the backlinks under a
///     page, the calendar's agenda, its day panel (on phones a bottom sheet, which stays open) and
///     a journal day's editor; "No status" removes the checkbox;
///     "/zrobione" (the Polish "/done") typed into a journal day completes its block. The agenda shows a task written
///     into another note inside its day, the day panel the status it wrote. The notes are put back
///     afterwards.
void checkTaskStatusMenus(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const auto total = [ws](const QString &status) {
        return ws->runQuery(u"type:tasks status:"_s + status).value(u"total"_s).toInt();
    };
    // Every note with a task, as it was (restored at the end).
    QHash<QString, QString> notes;
    for (const QVariant &v : ws->runQuery(u"type:tasks status:all"_s).value(u"items"_s).toList()) {
        const QString path = v.toMap().value(u"path"_s).toString();
        if (!notes.contains(path))
            notes.insert(path, ws->readFile(path));
    }
    const QString today = ws->journalPath(app->today(0));
    notes.insert(today, ws->readFile(today));
    const auto restore = [&] {
        closeMenus(window);
        for (auto it = notes.cbegin(); it != notes.cend(); ++it) {
            if (ws->readFile(it.key()) != it.value())
                ws->writeFile(it.key(), it.value());
        }
        settle(200);
    };
    const auto shownWhere = [](QQuickItem *root, const std::function<bool(QQuickItem *)> &pred) {
        QQuickItem *found = nullptr;
        waitFor([&] { return (found = findShown(root, pred)) != nullptr; });
        return found;
    };
    const auto named = [&](QQuickItem *root, const QString &name) {
        return shownWhere(root, [&name](QQuickItem *item) { return item->objectName() == name; });
    };
    // A visible item of the visual tree (list delegates are not in the QObject tree), on screen or
    // not (a day of the journal is taller than a phone's screen).
    std::function<QQuickItem *(QQuickItem *, const std::function<bool(QQuickItem *)> &)> visual =
            [&visual](QQuickItem *item, const std::function<bool(QQuickItem *)> &pred) -> QQuickItem * {
        if (!item->isVisible())
            return nullptr;
        if (pred(item))
            return item;
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = visual(child, pred))
                return found;
        }
        return nullptr;
    };
    // The context gesture on a checkbox, and a status of the menu it opened.
    const auto openStatusMenu = [&](QQuickItem *box) {
        if (!box)
            return false;
        if (qEnvironmentVariableIsSet("BRULION_SELFTEST_DEBUG")) {
            QString chain;
            for (const QQuickItem *p = box; p; p = p->parentItem())
                chain += QString::fromLatin1(p->metaObject()->className()).section(u'_', 0, 0) + u' ';
            const QPointF at = pointIn(box);
            std::fprintf(stderr, "selftest: status menu gesture at %.0f,%.0f on %s\n", at.x(), at.y(),
                         qPrintable(chain));
        }
        if (touch)
            touchTap(window, pointIn(box), 1000);
        else
            mouseClick(window, pointIn(box), Qt::RightButton);
        return waitFor([&] { return openMenus(window) == 1; });
    };
    const auto statusItem = [&](const char *status) {
        return named(window->contentItem(), u"taskStatus_"_s + QLatin1String(status));
    };
    const auto choose = [&](const char *status) {
        QQuickItem *item = statusItem(status);
        if (item)
            tapOrClick(window, pointIn(item), touch);
        return item != nullptr && waitFor([&] { return openMenus(window) == 0; });
    };
    const auto checked = [&](const char *status) {
        QQuickItem *item = statusItem(status);
        return item && item->property("checked").toBool();
    };

    // ---- Tasks: the row's checkbox opens the row's menu; a status, then "No status".
    app->navigate({{u"view"_s, u"tasks"_s}});
    const int allBefore = total(u"all"_s);
    QQuickItem *box = named(window->contentItem(), u"tasksRowTaskBox"_s);
    check(box != nullptr, "the tasks view shows task checkboxes");
    if (box) {
        const QString status = box->property("status").toString();
        // A status the task does not have yet.
        const char *target = status == u"doing" ? "deferred" : "doing";
        const int targetBefore = total(QLatin1String(target));
        const int openBefore = total(u"open"_s);
        check(openStatusMenu(box) && total(u"open"_s) == openBefore && total(QLatin1String(target)) == targetBefore,
              "a long press or right-click on a task's checkbox in Tasks opens the status menu (and does not toggle)");
        check(statusItem("todo") != nullptr && checked(status.toLatin1().constData()) && !checked(target),
              "the task's status is checked in the menu");
        check(choose(target) && waitFor([&] { return total(QLatin1String(target)) == targetBefore + 1; }),
              "choosing another status in Tasks writes it");
        // The same row may now sit elsewhere (sections); any task's checkbox will do.
        box = named(window->contentItem(), u"tasksRowTaskBox"_s);
        check(openStatusMenu(box) && choose("none") && waitFor([&] { return total(u"all"_s) == allBefore - 1; }),
              "“Bez statusu” in Tasks removes a task's checkbox");
    }
    // The keyboard: the Menu key opens a row's menu, the arrows reach "Open" (above the
    // statuses), Enter opens the task's note. First the list catches up with the last change (a
    // row it rebuilds takes its menu along), and the mouse pointer leaves the place where the
    // menu opens (the item under it would take the highlight).
    settle(1500);
    if (!touch)
        mouseMove(window, QPointF(1, 1));
    if (QQuickItem *row = shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "TasksRow"); })) {
        row->forceActiveFocus(Qt::TabFocusReason);
        sendKey(window, Qt::Key_Menu);
        const bool opened = waitFor([&] { return openMenus(window) == 1; });
        const QQuickItem *rowBox = named(row, u"tasksRowTaskBox"_s);
        const QString status = rowBox ? rowBox->property("status").toString() : QString();
        check(opened && focusName(window) == u"taskStatus_"_s + status,
              "the Menu key opens a task's menu in Tasks with its status highlighted");
        for (int i = 0; i < 8 && opened && focusName(window) != u"taskStatusOpen"; ++i)
            sendKey(window, Qt::Key_Up);
        const bool reached = focusName(window) == u"taskStatusOpen";
        sendKey(window, Qt::Key_Return);
        check(opened && reached && waitFor([&] { return app->currentView() != u"tasks" && openMenus(window) == 0; }),
              "in a task's menu in Tasks Up reaches “Otwórz” and Enter opens the note");
        closeMenus(window);
    } else {
        check(false, "the tasks view lists tasks for the keyboard");
    }
    restore();

    // ---- Tasks: a due date's chip has the date menu (PlanningTokenMenu); "A day later" writes it.
    app->navigate({{u"view"_s, u"tasks"_s}});
    settle(800);
    QQuickItem *dueChip =
            shownWhere(window->contentItem(), [](QQuickItem *item) { return item->objectName() == u"taskChip_due"; });
    check(dueChip != nullptr, "a task in Tasks shows its due date as a chip");
    if (dueChip) {
        QQuickItem *row = dueChip;
        while (row && !isA(row, "TasksRow"))
            row = row->parentItem();
        // Its `task` model object's `path`; without one the row's own (a required property).
        const QObject *task = row ? row->property("task").value<QObject *>() : nullptr;
        const QString path = task  ? task->property("path").toString()
                             : row ? row->property("path").toString()
                                   : QString();
        const QString before = path.isEmpty() ? QString() : ws->readFile(path);
        if (touch)
            touchTap(window, pointIn(dueChip), 1000);
        else
            mouseClick(window, pointIn(dueChip), Qt::RightButton);
        QQuickItem *later = nullptr;
        check(waitFor([&] {
                  return openMenus(window) == 1 && (later = named(window->contentItem(), u"dateChipDayLater"_s));
              }),
              "a long press or right-click on a due date's chip in Tasks opens its date menu");
        if (later)
            tapOrClick(window, pointIn(later), touch);
        check(!path.isEmpty() && waitFor([&] { return openMenus(window) == 0 && ws->readFile(path) != before; }),
              "“Dzień później” writes the task's new due date into its note");
        restore();
    }

    // ---- The backlinks under a page: today's "Przejrzeć [[Remont mieszkania]]…" task.
    const QString remont = u"Projekty/Remont mieszkania.md"_s;
    app->openPage(remont);
    QQuickItem *editor = nullptr;
    waitFor([&] {
        editor = findShown(window->contentItem(), [&](QQuickItem *item) {
            const auto *c =
                    item->objectName() == u"pageEditor" ? item->property("controller").value<QObject *>() : nullptr;
            return c && c->property("path").toString() == remont && c->property("loaded").toBool();
        });
        return editor != nullptr;
    });
    if (auto *list = editor ? editor->property("listView").value<QQuickItem *>() : nullptr) {
        QMetaObject::invokeMethod(list, "positionViewAtEnd");
        settle(300);
        // Below the blocks: scroll until the reference's checkbox is on screen.
        for (int i = 0; i < 20
                        && !findShown(
                                window->contentItem(),
                                [](QQuickItem *item) {
                                    return item->objectName() == u"refTaskBox"
                                           && item->property("status").toString() == u"todo";
                                });
             ++i) {
            list->setProperty("contentY", list->property("contentY").toReal() + 200);
            settle(60);
        }
    }
    box = shownWhere(window->contentItem(), [](QQuickItem *item) {
        return item->objectName() == u"refTaskBox" && item->property("status").toString() == u"todo";
    });
    check(box != nullptr, "the page's linked references show a task's checkbox");
    if (box) {
        const QString before = ws->readFile(today);
        check(openStatusMenu(box) && ws->readFile(today) == before && checked("todo"),
              "a long press or right-click on a backlink's checkbox opens the status menu (and does not toggle)");
        check(choose("deferred") && waitFor([&] {
                  return ws->readFile(today).contains(u"- [>] Przejrzeć [[Remont mieszkania]]"_s);
              }),
              "choosing “Oczekujące” on a backlink writes the status in its note");
    }
    restore();

    // ---- The calendar's agenda: a task with a date (the agenda scrolls to today first).
    app->navigate({{u"view"_s, u"calendar"_s}, {u"mode"_s, u"agenda"_s}});
    settle(600);
    // Inside the list's own area (a row half under the top bar is still "in the window").
    QQuickItem *agenda =
            shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "CalendarAgendaList"); });
    box = agenda ? named(agenda, u"planningTaskBox"_s) : nullptr;
    check(box != nullptr, "the agenda shows task checkboxes");
    if (box) {
        const char *target = box->property("status").toString() == u"cancelled" ? "deferred" : "cancelled";
        const int targetBefore = total(QLatin1String(target));
        const int closedBefore = total(u"closed"_s);
        check(openStatusMenu(box) && total(u"closed"_s) == closedBefore,
              "a long press or right-click on a checkbox in the agenda opens the status menu (and does not toggle)");
        check(choose(target) && waitFor([&] { return total(QLatin1String(target)) == targetBefore + 1; }),
              "choosing another status in the agenda writes it");
    }
    // The agenda follows a change made elsewhere: a task dated today written into another note
    // joins today's entries, and the day holds them all (its entries' height is their rows': it
    // once read its Repeater's counter with a bare `void`, compiled away by qmlcachegen).
    const QString chimney = u"Kominiarz.md"_s;
    ws->writeFile(chimney,
                  u"- [ ] Zadzwonić do kominiarza w sprawie przeglądu przewodów 📅 "_s + app->today(0) + u"\n"_s);
    const auto entryTitle = [](QQuickItem *row) {
        QVariant entry = row->property("entry");
        if (entry.canConvert<QJSValue>())
            entry = entry.value<QJSValue>().toVariant();
        return entry.toMap().value(u"title"_s).toString();
    };
    QQuickItem *chimneyRow =
            agenda ? shownWhere(agenda,
                                [&](QQuickItem *item) {
                                    return isA(item, "PlanningItemRow") && entryTitle(item).contains(u"kominiarza"_s);
                                })
                   : nullptr;
    QQuickItem *entries = chimneyRow ? chimneyRow->parentItem() : nullptr;
    const auto rowsHeight = [entries] {
        qreal h = 0;
        const QList<QQuickItem *> children = entries->childItems();
        for (QQuickItem *child : children) {
            if (isA(child, "PlanningItemRow"))
                h += child->height();
        }
        return h;
    };
    check(entries && waitFor([&] { return qAbs(entries->height() - rowsHeight()) < 0.5; }),
          "a task written into another note joins today's agenda, inside its day");
    ws->deletePage(chimney);
    restore();

    // ---- The agenda: tomorrow's time of a reminder repeated every day (an occurrence) has the
    //      series' menu without a snooze — that time still rings.
    const QString pills = u"Leki.md"_s;
    // Late in the day: today's time has not rung yet (no notification during the checks).
    ws->writeFile(pills, u"- Leki na ciśnienie ⏰ "_s + app->today(0) + u" 23:58 🔁 every day\n"_s);
    const auto isOccurrence = [](QQuickItem *row) {
        QVariant entry = row->property("entry");
        if (entry.canConvert<QJSValue>())
            entry = entry.value<QJSValue>().toVariant();
        return entry.toMap().value(u"occurrence"_s).toBool();
    };
    QQuickItem *pillsRow = agenda ? shownWhere(agenda,
                                               [&](QQuickItem *item) {
                                                   return isA(item, "PlanningItemRow")
                                                          && entryTitle(item).contains(u"ciśnienie"_s)
                                                          && isOccurrence(item);
                                               })
                                  : nullptr;
    check(pillsRow != nullptr, "the agenda shows the next times of a repeated reminder");
    if (pillsRow) {
        if (touch)
            touchTap(window, pointIn(pillsRow), 1000);
        else
            mouseClick(window, pointIn(pillsRow), Qt::RightButton);
        const bool opened = waitFor(
                [&] { return openMenus(window) == 1 && named(window->contentItem(), u"dateChipChange"_s) != nullptr; });
        const auto shownNow = [window](const QString &name) {
            return findShown(window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
        };
        check(opened && shownNow(u"dateChipAlarm"_s) && !shownNow(u"dateChipHourLater"_s)
                      && !shownNow(u"dateChipTomorrowSameTime"_s) && !shownNow(u"dateChipDayLater"_s),
              "a later time of a repeated reminder in the agenda has the series' menu, without a snooze");
        closeMenus(window);
    }
    ws->deletePage(pills);
    restore();

    // ---- The agenda: a day's header is a button — a tap / click selects the day (its number on
    //      the dark disc; the side panel follows, on phones the day's sheet opens) and the list
    //      stays where it is (the model keeps its four weeks); on a desktop Down and Enter pick the
    //      next day.
    {
        QQuickItem *calendarView =
                shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "CalendarView"); });
        QObject *daySheet = calendarView ? calendarView->findChild<QObject *>(u"calendarDaySheet"_s) : nullptr;
        const bool sideBySide = calendarView && calendarView->property("sideBySide").toBool();
        agenda = shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "CalendarAgendaList"); });
        auto *model = agenda ? qobject_cast<QAbstractItemModel *>(agenda->property("calendar").value<QObject *>())
                             : nullptr;
        const QString tomorrow = app->today(1);
        QQuickItem *header = agenda ? named(agenda, u"agendaDay_"_s + tomorrow) : nullptr;
        QQuickItem *list = header;
        while (list && !isA(list, "QQuickListView"))
            list = list->parentItem();
        check(header && list && model, "the agenda shows tomorrow's day header");
        if (header && list && model) {
            QQuickItem *day = header;
            while (day && !day->property("isSelected").isValid())
                day = day->parentItem();
            const QString start = model->property("rangeStart").toString();
            const qreal contentY = list->property("contentY").toReal();
            tapOrClick(window, pointIn(header, 0.4, 0.5), touch);
            check(waitFor([&] { return model->property("anchorDate").toString() == tomorrow; })
                          && day && day->property("isSelected").toBool(),
                  "a tap / click on a day's header in the agenda selects the day");
            check(model->property("rangeStart").toString() == start
                          && qAbs(list->property("contentY").toReal() - contentY) < 1,
                  "the agenda stays where it is when a day is picked");
            const bool sheetOpened =
                    daySheet && (sideBySide ? daySheet->property("opened").toBool()
                                            : waitFor([&] { return daySheet->property("opened").toBool(); }, 3000));
            const bool panelShows = findShown(window->contentItem(), [&](QQuickItem *item) {
                                        return isA(item, "CalendarDayPanel")
                                               && item->property("date").toString() == tomorrow;
                                    }) != nullptr;
            check(sideBySide ? panelShows && !sheetOpened : sheetOpened,
                  "the picked day shows in the side panel (on phones in its sheet)");
            if (sheetOpened) {
                QMetaObject::invokeMethod(daySheet, "close");
                waitFor([&] { return !daySheet->property("visible").toBool(); });
            }
            if (!touch) {
                // The next day of the list, read from the model.
                const int dateRole = model->roleNames().key("date", -1);
                int row = -1;
                for (int r = 0; r < model->rowCount() && row < 0; ++r) {
                    if (model->data(model->index(r, 0), dateRole).toString() == tomorrow)
                        row = r;
                }
                const QString next = row >= 0 && row + 1 < model->rowCount()
                                             ? model->data(model->index(row + 1, 0), dateRole).toString()
                                             : QString();
                sendKey(window, Qt::Key_Down);
                sendKey(window, Qt::Key_Return);
                check(!next.isEmpty() && waitFor([&] { return model->property("anchorDate").toString() == next; }),
                      "Down and Enter in the agenda pick the next day");
            }
        }
        if (model)
            QMetaObject::invokeMethod(model, "goToday");
    }

    // ---- A day of the week view: its day panel, beside the week on wide windows and a bottom
    //      sheet (a modal dialog) on phones, where the menu opens over the sheet and the sheet
    //      stays open after the choice. The day in two days has dated tasks.
    app->navigate({{u"view"_s, u"calendar"_s}, {u"mode"_s, u"week"_s}});
    settle(600);
    // The week's finished entries (yesterday's done tasks) carry a check — a cancelled one an x —,
    // as in the month: the state is never told by colour alone.
    {
        const auto stateIcons = [window] {
            QQuickItem *grid = findShown(window->contentItem(), [](QQuickItem *i) { return isA(i, "CalendarWeekGrid"); });
            const QList<QQuickItem *> items = grid ? uidriver::allItems(grid) : QList<QQuickItem *>();
            return std::count_if(items.cbegin(), items.cend(), [](QQuickItem *i) {
                const QString name = i->property("name").toString();
                return isA(i, "BIcon") && shown(i) && (name == u"check" || name == u"x");
            });
        };
        check(waitFor([&] { return stateIcons() > 0; }), "the week grid marks a done entry with a check");
    }
    QQuickItem *calendarView =
            shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "CalendarView"); });
    QObject *daySheet = calendarView ? calendarView->findChild<QObject *>(u"calendarDaySheet"_s) : nullptr;
    if (calendarView)
        uidriver::callQml(calendarView, "showDay", {QVariant(app->today(2))});
    const bool inSheet = daySheet && waitFor([&] { return daySheet->property("opened").toBool(); }, 1000);
    std::fprintf(stderr, "selftest: the calendar's day panel %s\n", inSheet ? "in a bottom sheet" : "beside the week");
    box = shownWhere(window->contentItem(), [](QQuickItem *item) {
        if (item->objectName() != u"planningTaskBox" || item->property("status").toString() != u"todo")
            return false;
        for (const QQuickItem *p = item->parentItem(); p; p = p->parentItem()) {
            if (isA(p, "CalendarDayPanel"))
                return true;
        }
        return false;
    });
    check(box != nullptr, "the calendar's day panel shows an open task's checkbox");
    // The day panel's checkboxes of a status.
    const auto panelBoxes = [window](const QString &status) {
        int n = 0;
        std::function<void(QQuickItem *, bool)> visit = [&](QQuickItem *item, bool inPanel) {
            if (!item->isVisible())
                return;
            inPanel = inPanel || isA(item, "CalendarDayPanel");
            if (inPanel && item->objectName() == u"planningTaskBox" && item->property("status").toString() == status)
                ++n;
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children)
                visit(child, inPanel);
        };
        visit(window->contentItem(), false);
        return n;
    };
    const int panelDoingBefore = panelBoxes(u"doing"_s);
    if (box) {
        const int doingBefore = total(u"doing"_s);
        const int closedBefore = total(u"closed"_s);
        check(openStatusMenu(box) && total(u"closed"_s) == closedBefore && checked("todo")
                      && (!inSheet || daySheet->property("opened").toBool()),
              "a long press or right-click on a checkbox in the day panel opens the status menu (and does not "
              "toggle)");
        check(choose("doing") && waitFor([&] { return total(u"doing"_s) == doingBefore + 1; })
                      && (!inSheet || daySheet->property("opened").toBool()),
              "choosing “W toku” in the day panel writes it, and its sheet stays open");
        // (The panel's entries follow the calendar's revision, read for real.)
        check(waitFor([&] { return panelBoxes(u"doing"_s) == panelDoingBefore + 1; }),
              "the day panel shows the status it wrote");
    }
    if (inSheet)
        QMetaObject::invokeMethod(daySheet, "close");
    restore();
    if (touch && calendarView && window->width() < 900) {
        if (daySheet)
            waitFor([&] { return !daySheet->property("visible").toBool(); });
        checkCalendarEdgeSwipe(window, calendarView, check);
    }

    // ---- A day of the journal stream: its checkbox's menu, and a "/" command typed into a block.
    app->goHome();
    QObject *day = nullptr;
    QQuickItem *dayEditor = nullptr;
    waitFor([&] {
        dayEditor = visual(window->contentItem(), [&](QQuickItem *item) {
            const auto *c = item->objectName() == u"journalDayEditor" ? item->property("controller").value<QObject *>()
                                                                      : nullptr;
            return c && c->property("path").toString() == today && c->property("loaded").toBool();
        });
        day = dayEditor ? dayEditor->property("controller").value<QObject *>() : nullptr;
        return day != nullptr;
    });
    if (!day && qEnvironmentVariableIsSet("BRULION_SELFTEST_DEBUG")) {
        std::fprintf(stderr, "selftest: no day editor for %s, view %s\n", qPrintable(today),
                     qPrintable(app->currentView()));
        std::function<void(QQuickItem *, int)> dump = [&](QQuickItem *item, int depth) {
            if (item->objectName() == u"journalDayEditor") {
                const auto *c = item->property("controller").value<QObject *>();
                std::fprintf(stderr, "selftest: day editor %s loaded %d visible %d depth %d\n",
                             qPrintable(c ? c->property("path").toString() : u"-"_s),
                             c && c->property("loaded").toBool(), item->isVisible(), depth);
            }
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children)
                dump(child, depth + 1);
        };
        dump(window->contentItem(), 0);
    }
    check(day != nullptr, "today's journal day is shown with its editor");
    if (!day) {
        restore();
        app->goHome();
        return;
    }
    const auto saved = [&] {
        bool ok = false;
        QMetaObject::invokeMethod(day, "save", qReturnArg(ok));
        return ws->readFile(today);
    };
    box = named(dayEditor, u"taskBox"_s);
    check(box != nullptr, "today's task has its checkbox");
    if (box) {
        const QString before = saved();
        check(openStatusMenu(box) && saved() == before && checked("todo"),
              "a long press or right-click on a journal task's checkbox opens the status menu (and does not toggle)");
        check(choose("doing") && waitFor([&] { return saved().contains(u"- [/] Przejrzeć [[Remont mieszkania]]"_s); }),
              "choosing “W toku” in a journal day writes the status");
    }
    const QStringList lines = ws->readFile(today).split(u'\n');
    qsizetype ideaLine = -1;
    for (qsizetype i = 0; i < lines.size() && ideaLine < 0; ++i) {
        if (lines.at(i).startsWith(u"- Pomysł na obiad"_s))
            ideaLine = i;
    }
    int row = -1;
    QMetaObject::invokeMethod(day, "rowOfLine", qReturnArg(row), int(ideaLine));
    check(row >= 0, "today's journal has its dinner idea");
    if (row >= 0) {
        uidriver::callQml(dayEditor, "startEditing", {QVariant(row), QVariant(-1)});
        const bool editing = waitFor([&] {
            const QQuickItem *f = window->activeFocusItem();
            return day->property("editingRow").toInt() == row && f
                   && QByteArray(f->metaObject()->className()).contains("InlineEditor");
        });
        for (const QChar ch : u" /zrobione"_s)
            sendKey(window, 0, Qt::NoModifier, QString(ch));
        check(editing && waitFor([&] { return dayEditor->property("suggestionsOpen").toBool(); }),
              "“/zrobione” typed into a journal day opens the commands");
        sendKey(window, Qt::Key_Return);
        check(waitFor([&] { return saved().contains(u"\n- [x] Pomysł na obiad"_s) && saved().contains(u"✅"_s); })
                      && !saved().contains(u"/zrobione"_s),
              "the command completes the journal's block, its text gone");
        QMetaObject::invokeMethod(day, "stopEditing");
    }
    restore();
    app->goHome();
}

/// 5c. The event log ("Event log") and the details of a sync run, with real taps / clicks:
///     lines logged anywhere reach the page (a secret never does), the search ignores diacritics,
///     an area chip filters, the detailed mode switches the Logger; the sync center's history
///     opens a run's details, whose "Show in the event log" opens the page at the run's time.
/// 3d. Tags name pages (docs/SYNTAX.md §3), with real input: a tap / click on a #tag in a journal
///     day opens the page the tag names — one not written yet opens empty, with the tagged blocks
///     among its references and, at every width, the tag overview's “#” among the page actions
///     and “Tasks with this tag” in the page menu (opening Tasks filtered by the tag), no row of
///     buttons under the title, a tag's sub-tags as chips there; ready to type on a desktop, waiting for a tap on a touch screen — and back returns; in Tasks a click
///     on a tag filters the list while a Ctrl+click (desktop) or a long press (--touch) opens the
///     tag's page; a row of the tag list opens the tag's page, its trailing button the overview,
///     whose "Create page" opens the page not written yet; the sidebar's rename dialog counts
///     the links and tags it rewrites, and a "/" in it moves a page (renamed too: the toast says
///     both).
void checkTagLinks(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const auto view = [app] { return app->currentView(); };
    const auto routePath = [app] { return app->currentRoute().value(u"path"_s).toString(); };
    const QString tagPage = u"pomysł.md"_s; // #pomysł: the sample notebook has no such page
    const auto onScreenText = [window](const QString &needle) {
        QQuickItem *text = nullptr;
        waitFor([&] { return (text = renderedTextContaining(window->contentItem(), needle)) != nullptr; });
        return text;
    };

    // ---- A tag in today's journal.
    app->goHome();
    settle(300);
    QQuickItem *idea = onScreenText(u"Pomysł na obiad"_s);
    const QPointF tag = idea ? linkPoint(idea, u"tag:"_s) : QPointF(-1, -1);
    check(tag.x() >= 0, "today's journal shows the #pomysł tag as a link");
    if (tag.x() >= 0) {
        tapOrClick(window, tag, touch);
        check(waitFor([&] { return view() == u"page" && routePath() == tagPage; }),
              "a tap or click on a #tag opens the page the tag names");
        QObject *refs = window->findChild<QObject *>(u"pageReferences"_s);
        check(refs && waitFor([&] { return refs->property("linkedCount").toInt() >= 2; }),
              "the tag's page, not written yet, lists the tagged blocks among its references");
        // (The editor's header is a list's: in the visual tree only.)
        check(waitFor([&] {
                  return findShown(window->contentItem(),
                                   [](QQuickItem *item) { return item->objectName() == u"pageTagOverview"; })
                         != nullptr;
              }),
              "the page header offers the tag overview (a “#” among the page actions)");
        // At every width: no row of two buttons under the title — "Tasks with this tag" is in the
        // page menu, and opens Tasks filtered by the tag.
        {
            check(!findShown(window->contentItem(),
                             [](QQuickItem *item) { return item->objectName() == u"pageTagStrip"; }),
                  "the page header has no strip row of buttons");
            QQuickItem *menuButton = nullptr;
            waitFor([&] {
                QQuickItem *header = findShown(window->contentItem(), [&](QQuickItem *item) {
                    return isA(item, "ContentPageHeader") && item->property("path").toString() == tagPage;
                });
                menuButton = header ? header->property("menuAnchor").value<QQuickItem *>() : nullptr;
                return menuButton && menuButton->isVisible();
            });
            if (menuButton)
                tapOrClick(window, pointIn(menuButton), touch);
            QQuickItem *tagTasks = nullptr;
            check(waitFor([&] {
                      tagTasks = findShown(window->contentItem(),
                                           [](QQuickItem *item) { return item->objectName() == u"pageMenuTagTasks"; });
                      return tagTasks != nullptr;
                  }),
                  "the page menu offers “Zadania z tym tagiem”");
            if (tagTasks) {
                tapOrClick(window, pointIn(tagTasks), touch);
                check(waitFor([&] {
                          return view() == u"tasks"
                                 && app->currentRoute().value(u"query"_s).toString() == u"tag:pomysł";
                      }),
                      "the page menu's “Zadania z tym tagiem” opens Tasks filtered by the tag");
                app->back();
                waitFor([&] { return view() == u"page" && routePath() == tagPage; });
            } else {
                closeMenus(window);
            }
        }
        // Not written yet: on a desktop it opens ready to type; on a touch screen it waits for a tap
        // (the keyboard would cover the tagged blocks it lists), which starts typing.
        QQuickItem *tagEditor = nullptr;
        QObject *tagController = nullptr;
        waitFor([&] {
            tagEditor = findShown(window->contentItem(), [&](QQuickItem *item) {
                auto *c =
                        item->objectName() == u"pageEditor" ? item->property("controller").value<QObject *>() : nullptr;
                return c && c->property("path").toString() == tagPage && c->property("loaded").toBool();
            });
            tagController = tagEditor ? tagEditor->property("controller").value<QObject *>() : nullptr;
            return tagController != nullptr;
        });
        check(tagController != nullptr, "the tag's page is in the editor");
        if (tagController) {
            settle(200);
            const int editing = tagController->property("editingRow").toInt();
            if (touch) {
                check(editing < 0, "on a touch screen the tag's page, not written yet, does not start typing");
                const QString startText = QCoreApplication::translate("ContentPageHeader", "Start writing");
                QQuickItem *start = findShown(window->contentItem(), [&](QQuickItem *item) {
                    return isA(item, "BButton") && item->property("text").toString() == startText;
                });
                if (start)
                    tapOrClick(window, start->mapToScene(QPointF(start->width() / 2, start->height() / 2)), true);
                check(start && waitFor([&] { return tagController->property("editingRow").toInt() >= 0; }),
                      "a tap on its “Zacznij pisać” starts typing");
            } else {
                check(editing >= 0, "on a desktop the tag's page, not written yet, opens ready to type");
            }
            QMetaObject::invokeMethod(tagController, "stopEditing");
        }
        check(!app->workspace()->exists(tagPage), "looking at a tag's page does not create it");
        check(app->back() && view() == u"journals", "back returns from the tag's page");
    }

    // ---- Tasks: a click filters by the tag, Ctrl+click opens its page, a long press its menu.
    app->navigate({{u"view"_s, u"tasks"_s}, {u"query"_s, u"zakwas"_s}});
    settle(600);
    QQuickItem *task = onScreenText(u"zakwas"_s);
    QPointF taskTag = task ? linkPoint(task, u"tag:"_s) : QPointF(-1, -1);
    check(taskTag.x() >= 0, "a task in Tasks shows its #zakupy tag as a link");
    if (taskTag.x() >= 0) {
        tapOrClick(window, taskTag, touch);
        settle(400);
        check(view() == u"tasks", "a tap or click on a tag in Tasks filters the list (and stays there)");
        task = onScreenText(u"zakwas"_s);
        taskTag = task ? linkPoint(task, u"tag:"_s) : QPointF(-1, -1);
        if (qEnvironmentVariableIsSet("BRULION_SELFTEST_DEBUG"))
            std::fprintf(stderr, "selftest: tag of a task at %.0f,%.0f in \"%s\"\n", taskTag.x(), taskTag.y(),
                         task ? qPrintable(task->property("text").toString().left(200)) : "");
        if (touch) {
            // A long press: the tag's menu (LinkMenu), "Filter by tag" first; its "Open tag page".
            touchTap(window, taskTag, 1000);
            check(waitFor([&] { return openMenus(window) == 1; }), "a long press on a tag in Tasks opens its menu");
            const auto menuItem = [window](const QString &name) {
                return findShown(window->contentItem(), [&](QQuickItem *item) { return item->objectName() == name; });
            };
            QQuickItem *filter = menuItem(u"linkMenuFilter"_s);
            check(filter != nullptr, "the tag's menu offers “Filtruj po tagu”");
            QQuickItem *open = menuItem(u"linkMenuOpen"_s);
            if (open)
                tapOrClick(window, pointIn(open), true);
        } else {
            mouseClick(window, taskTag, Qt::LeftButton, Qt::ControlModifier);
        }
        const bool opened =
                taskTag.x() >= 0 && waitFor([&] { return view() == u"page" && routePath() == u"zakupy.md"; });
        if (!opened)
            std::fprintf(stderr, "selftest: after the gesture on the tag: %s %s\n", qPrintable(view()),
                         qPrintable(routePath()));
        check(opened, touch ? "the tag menu's “Otwórz stronę tagu” in Tasks opens the tag's page"
                            : "a Ctrl+click on a tag in Tasks opens the tag's page");
        check(waitFor([&] { return openMenus(window) == 0; }), "no menu is left open on the tag's page");
        closeMenus(window);
    }

    // ---- The tag list: a row opens the tag's page, its trailing button the tag overview.
    app->navigate({{u"view"_s, u"tags"_s}});
    settle(400);
    const auto tagRow = [window] {
        QQuickItem *row = nullptr;
        waitFor([&] {
            row = findShown(window->contentItem(), [](QQuickItem *item) {
                return isA(item, "BListItem") && item->property("text").toString() == u"pomysł";
            });
            return row != nullptr;
        });
        return row;
    };
    QQuickItem *row = tagRow();
    check(row != nullptr, "the tag list shows #pomysł");
    if (row) {
        tapOrClick(window, pointIn(row, 0.3), touch);
        check(waitFor([&] { return view() == u"page" && routePath() == tagPage; }),
              "a row of the tag list opens the tag's page");
        app->back();
        settle(300);
        row = tagRow();
        QQuickItem *overview =
                row ? findShown(row, [](QQuickItem *item) { return item->objectName() == u"tagOverviewButton"; })
                    : nullptr;
        check(overview != nullptr, "a row of the tag list has a tag overview button");
        if (overview) {
            tapOrClick(window, pointIn(overview), touch);
            check(waitFor([&] {
                      return view() == u"tag" && app->currentRoute().value(u"tag"_s).toString() == u"pomysł";
                  }),
                  "the row's tag overview button opens the tag overview");
            QQuickItem *pageButton = nullptr;
            check(waitFor([&] {
                      pageButton = findShown(window->contentItem(),
                                             [](QQuickItem *item) { return item->objectName() == u"tagPageButton"; });
                      return pageButton != nullptr;
                  }) && !pageButton->property("pageExists").toBool()
                          && pageButton->property("iconName").toString() == u"plus",
                  "the tag overview offers to create the tag's page, not written yet");
            if (pageButton) {
                tapOrClick(window, pointIn(pageButton), touch);
                check(waitFor([&] { return view() == u"page" && routePath() == tagPage; })
                              && !app->workspace()->exists(tagPage),
                      "the tag overview's page button opens the tag's page (still not created)");
            }
        }
    }

    // ---- A page renamed (in its header) to a tag in use gets the tag overview's “#” with the rename;
    //      renamed again — the tags naming it rewritten in other notes — it keeps it.
    {
        const QString draft = u"Szkic pomysłów.md"_s;
        app->workspace()->writeFile(draft, u"- szkic\n"_s);
        app->openPage(draft);
        // The tag's "#" among the page actions.
        const auto strip = [window] {
            return findShown(window->contentItem(),
                             [](QQuickItem *item) { return item->objectName() == u"pageTagOverview"; });
        };
        const auto headerOf = [&](const QString &path) {
            QQuickItem *found = nullptr;
            waitFor([&] {
                QQuickItem *editor = findShown(window->contentItem(), [&](QQuickItem *item) {
                    auto *c = item->objectName() == u"pageEditor" ? item->property("controller").value<QObject *>()
                                                                  : nullptr;
                    return c && c->property("path").toString() == path && c->property("loaded").toBool();
                });
                found = editor ? findShown(editor, [](QQuickItem *item) { return isA(item, "ContentPageHeader"); })
                               : nullptr;
                return found != nullptr;
            });
            return found;
        };
        // The title edited in place (ContentPageHeader.renameRequested), confirmed when it asks.
        const auto renameInHeader = [&](QQuickItem *header, const QString &title) {
            if (!header)
                return;
            QMetaObject::invokeMethod(header, "renameRequested", Q_ARG(QString, title));
            for (QObject *dialog : window->findChildren<QObject *>(u"pageRenameDialog"_s)) {
                if (dialog->property("visible").toBool())
                    QMetaObject::invokeMethod(dialog, "tryAccept");
            }
        };
        QQuickItem *header = headerOf(draft);
        check(header && !strip(), "a page no tag names has no tag overview “#”");
        renameInHeader(header, u"pomysł"_s);
        check(strip() != nullptr && app->workspace()->exists(tagPage),
              "renamed to a tag in use, the page has the tag overview's “#” at once");
        renameInHeader(headerOf(tagPage), u"Pomysły kuchenne"_s);
        const QString renamed = u"Pomysły kuchenne.md"_s;
        // (No waiting: the rename is done when the call returns. The strip used to come only with
        // some later change — its binding had lost its dependency on Workspace.revision.)
        check(app->workspace()->exists(renamed) && strip() != nullptr && !app->workspace()->exists(tagPage),
              "renamed again, the tags naming it rewritten in other notes after it moved: the “#” stays");
        app->workspace()->deletePage(app->workspace()->exists(renamed) ? renamed : draft);
        app->goHome();
        settle(200);
    }

    // ---- A tag with sub-tags (#projekt/dom in the sample notebook): its page shows them as chips
    //      under the title, at every width, beside the header's "#".
    {
        app->openTag(u"projekt"_s);
        QQuickItem *subTags = nullptr;
        check(waitFor([&] {
                  subTags = findShown(window->contentItem(),
                                      [](QQuickItem *item) { return item->objectName() == u"pageSubTags"; });
                  return subTags && findShown(subTags, [](QQuickItem *item) {
                             return isA(item, "BChip") && item->property("text").toString().startsWith(u"#projekt/dom"_s);
                         }) != nullptr;
              }) && findShown(window->contentItem(),
                              [](QQuickItem *item) { return item->objectName() == u"pageTagOverview"; }),
              "a tag's page shows its sub-tags as chips under the title, and the tag overview's “#”");
        app->goHome();
        settle(200);
    }

    // ---- Renaming from the sidebar says how many links and tags change.
    QObject *rename = window->findChild<QObject *>(u"shellRename"_s);
    check(rename != nullptr, "the shell has its rename dialog");
    if (rename) {
        uidriver::callQml(rename, "openFor", {u"Witaj w Brulionie.md"_s, false});
        const int changes = rename->property("changes").toInt();
        check(waitFor([&] { return rename->property("opened").toBool(); }) && changes > 0
                      && rename->property("subtitle").toString().contains(QString::number(changes)),
              "the sidebar's rename dialog counts the links and tags naming the page");
        // A name taken by another page: said at the field (a moment after typing), Rename
        // disabled; the subtitle still says what a rename changes.
        const QString impact = rename->property("subtitle").toString();
        QQuickItem *nameField = nullptr;
        waitFor([&] {
            nameField = findShown(window->contentItem(),
                                  [](QQuickItem *item) { return item->objectName() == u"renameField"; });
            return nameField != nullptr;
        });
        if (nameField)
            nameField->setProperty("text", u"Przepisy"_s);
        const QString taken = QCoreApplication::translate("brulion::shell::Workspace",
                                                          "A page named “%1” already exists here.")
                                      .arg(u"Przepisy"_s);
        check(nameField && waitFor([&] { return nameField->property("errorText").toString() == taken; })
                      && !rename->property("acceptEnabled").toBool() && rename->property("subtitle").toString() == impact,
              "a taken name in the rename dialog is said at the field (Rename disabled), not in the subtitle");
        QMetaObject::invokeMethod(rename, "close");
        waitFor([&] { return !rename->property("opened").toBool(); });
        // A "/" moves the page, here renamed too: the toast names the folder and the new name.
        const QString probe = u"Próba przenosin.md"_s;
        app->workspace()->writeFile(probe, u"- żółw\n"_s);
        QString toast;
        const QMetaObject::Connection toasts = QObject::connect(app, &AppController::toastRequested, app,
                                                                [&toast](const QString &text) { toast = text; });
        uidriver::callQml(rename, "openFor", {probe, false});
        QQuickItem *field = nullptr;
        waitFor([&] {
            field = findShown(window->contentItem(),
                              [](QQuickItem *item) { return item->objectName() == u"renameField"; });
            return field != nullptr;
        });
        if (field) {
            field->setProperty("text", u"Projekty/Próba po zmianie"_s);
            QMetaObject::invokeMethod(rename, "tryAccept");
        }
        const QString expected = QCoreApplication::translate("ShellMoveWords", "Moved to “%1” as “%2”")
                                         .arg(u"Projekty"_s, u"Próba po zmianie"_s);
        const bool moved = field && waitFor([&] { return toast == expected; });
        if (!moved)
            std::fprintf(stderr, "selftest: the move's toast: %s\n", qPrintable(toast));
        check(moved && app->workspace()->exists(u"Projekty/Próba po zmianie.md"_s),
              "a “/” in the rename dialog moves the page, renamed too: the toast names the folder and the new name");
        QObject::disconnect(toasts);
        app->workspace()->deletePage(app->workspace()->exists(probe) ? probe : u"Projekty/Próba po zmianie.md"_s);
    }
    app->goHome();
    settle(200);
}

void checkLogsAndSyncDetails(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    using brulion::logging::Logger;
    Logger *logger = Logger::instance();
    check(logger != nullptr, "the event log is installed");
    if (!logger)
        return;
    const QString secret = u"Selftest-Sekret-987"_s;
    brulion::logging::addSecret(secret);
    qCInfo(brulion::logging::lcSync).noquote() << u"#98 selftest: Zażółć gęślą jaźń"_s;
    qCWarning(brulion::logging::lcNet).noquote()
            << u"#98 selftest: PUT https://jan:%1@chmura.invalid/x.md failed"_s.arg(secret);
    logger->flush();

    app->navigate({{u"view"_s, u"logs"_s}});
    const auto shownNamed = [window](const QString &name) {
        return findShown(window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
    };
    QQuickItem *list = nullptr;
    check(waitFor([&] { return (list = shownNamed(u"logsList"_s)) && list->property("count").toInt() > 0; }, 5000),
          "the event log page lists entries");
    if (!list)
        return;
    auto *model = list->property("model").value<QObject *>();
    const auto visible = [model] {
        QString text;
        if (model)
            QMetaObject::invokeMethod(model, "visibleText", qReturnArg(text));
        return text;
    };
    check(waitFor([&] { return visible().contains(u"#98 selftest: Zażółć gęślą jaźń"_s); }),
          "a line logged by the sync reaches the page");
    check(!visible().contains(secret) && visible().contains(u"https://***@chmura.invalid/x.md"_s),
          "the page shows the line without the secret");

    // The search ignores letter case and diacritics.
    if (QQuickItem *search = shownNamed(u"logsSearch"_s)) {
        tapOrClick(window, pointIn(search), touch);
        settle(50);
        for (const QChar ch : u"ZAZOLC"_s)
            sendKey(window, 0, Qt::NoModifier, QString(ch));
        check(waitFor([&] { return model && model->property("count").toInt() == 1; }),
              "searching \"ZAZOLC\" finds the line with \"Zażółć\"");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return model && model->property("search").toString().isEmpty(); }),
              "Escape clears the search");
    } else {
        check(false, "the search field is on screen");
    }

    // An area chip filters; "Everything" shows all again.
    if (QQuickItem *chip = shownNamed(u"logsGroup-net"_s)) {
        tapOrClick(window, pointIn(chip), touch);
        check(waitFor([&] { return model && model->property("group").toString() == u"net"_s; }),
              "the Network chip filters the list");
        check(!visible().contains(u"Zażółć gęślą jaźń"_s) && visible().contains(u"#98 selftest: PUT"_s),
              "only network lines are left");
        if (QQuickItem *all = shownNamed(u"logsGroup-all"_s))
            tapOrClick(window, pointIn(all), touch);
        check(waitFor([&] { return model && model->property("group").toString().isEmpty(); }),
              "Everything shows all lines");
    } else {
        check(false, "the area chips are on screen");
    }

    // The detailed mode (at the end of the list).
    QMetaObject::invokeMethod(list, "positionViewAtEnd");
    settle(150);
    if (QQuickItem *detailed = shownNamed(u"logsDetailed"_s)) {
        tapOrClick(window, pointIn(detailed, 0.9, 0.5), touch);
        check(waitFor([&] { return logger->verbosity() == brulion::logging::Verbosity::Detailed; }),
              "the switch turns the detailed log on");
        check(app->settings()->value(u"diagnostics/logVerbosity"_s).toString() == u"detailed"_s,
              "the detailed log is remembered");
        tapOrClick(window, pointIn(detailed, 0.9, 0.5), touch);
        check(waitFor([&] { return logger->verbosity() == brulion::logging::Verbosity::Normal; }),
              "the switch turns it off again");
    } else {
        check(false, "the detailed-log switch is on screen at the end of the list");
    }

    // Copy (desktop button): the export header and no secret on the clipboard.
    if (QQuickItem *copy = shownNamed(u"logsCopy"_s)) {
        QGuiApplication::clipboard()->clear();
        tapOrClick(window, pointIn(copy), touch);
        const QString text = QGuiApplication::clipboard()->text();
        check(text.isEmpty() || (text.contains(u"\n----\n"_s) && !text.contains(secret)),
              "Copy puts the shown lines with the header on the clipboard, without the secret");
    }
    brulion::logging::removeSecret(secret);

    // A sync run's details from the sync center's history.
    WorkspaceSync *sync = app->workspace()->sync();
    if (!sync || !sync->syncHistory()) {
        check(false, "the sync history exists for the open notebook");
        return;
    }
    brulion::sync::SyncRunRecord run;
    run.number = 42;
    run.trigger = brulion::sync::SyncTrigger::LocalChange;
    run.started = QDateTime::currentDateTime().addSecs(-90);
    run.finished = QDateTime::currentDateTime().addSecs(-28);
    run.durationMs = 62'000;
    run.outcome = brulion::sync::SyncOutcome::Failed;
    run.errorCode = u"timeout"_s;
    run.errorMessage =
            u"PROPFIND https://chmura.invalid/remote.php/dav/files/jan/Brulion/Dziennik/ timed out after 60 s"_s;
    run.phases = {{brulion::sync::SyncPhase::Connect, 0, 120, 0, 0},
                  {brulion::sync::SyncPhase::ListRemote, 120, 61'880, 12, 48'000}};
    sync->syncHistory()->add(run);
    app->navigate({{u"view"_s, u"sync"_s}, {u"tab"_s, u"nextcloud"_s}});
    QQuickItem *row = nullptr;
    check(waitFor([&] { return (row = shownNamed(u"syncHistoryRow"_s)) != nullptr; }, 5000),
          "the sync center lists the run in its history");
    if (!row)
        return;
    tapOrClick(window, pointIn(row), touch);
    QObject *details = nullptr;
    check(waitFor([&] {
              details = window->findChild<QObject *>(u"syncRunDetails"_s);
              return details && details->property("opened").toBool();
          }),
          "a history row opens the run's details");
    if (!details)
        return;
    check(details->property("known").toBool() && details->property("title").toString().contains(u"42"_s),
          "the details show run #42");
    QQuickItem *showLog = nullptr;
    check(waitFor([&] { return (showLog = shownNamed(u"syncDetailsShowLog"_s)) != nullptr; }),
          "the details offer the event log");
    if (showLog) {
        tapOrClick(window, pointIn(showLog), touch);
        check(waitFor([&] {
                  return app->currentView() == u"logs"_s && app->currentRoute().value(u"fromMs"_s).toLongLong() > 0;
              }),
              "\"Show in the event log\" opens the log at the run's time");
    }
}

/// 3b. Rows with a context menu, with the input people really use: a finger on phones (--touch:
///     real touch events through the window), the mouse on desktops. A tap or a left click does
///     what the row says — expands the folder, opens the page, zooms into the block — and never
///     opens the menu; a long press or a right-click opens it. Regression (Android, the drawer):
///     the rows' right-click TapHandlers took every finger tap too (touch points carry no
///     buttons, so Qt ignores a TapHandler's acceptedButtons for them): each tap opened the modal
///     context menu, which then swallowed the release — no folder expanded, no page opened.
/// 5d. The notebook switcher next to the notebook's name (in the drawer on phones): one tap / click
/// on another notebook switches to it — a checkable menu item has toggled `checked` before
/// `triggered` runs, which once made the first click do nothing — here to the documentation (on
/// its table of contents) and back to the sample notebook.
void checkNotebookSwitcher(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const QString sample = app->workspace()->rootPath();
    const auto activate = [&](QQuickItem *item) {
        if (touch)
            touchTap(window, pointIn(item));
        else
            mouseClick(window, pointIn(item));
    };
    const auto shownItem = [window](const std::function<bool(QQuickItem *)> &pred) {
        QQuickItem *found = nullptr;
        waitFor([&] { return (found = findShown(window->contentItem(), pred)) != nullptr; });
        return found;
    };
    const auto named = [](const QString &name) {
        return [name](QQuickItem *item) { return item->objectName() == name; };
    };
    // Opens the switcher's menu and taps / clicks the entry `pred` picks, once.
    const auto pick = [&](const std::function<bool(QQuickItem *)> &pred) {
        closeMenus(window);
        if (window->width() < 900) {
            // A fresh shell after each switch: its drawer, through its top bar's ☰.
            QQuickItem *menuButton = shownItem(named(u"topBarMenu"_s));
            if (!menuButton)
                return false;
            activate(menuButton);
        }
        QQuickItem *button = shownItem(named(u"notebookSwitch"_s));
        if (!button)
            return false;
        activate(button);
        QQuickItem *entry = shownItem(pred);
        if (!entry)
            return false;
        settle(150); // the menu's opening transition
        activate(entry);
        return true;
    };
    const auto ready = [app] { return app->status() == u"ready" && app->workspace()->ready(); };

    check(pick(named(u"notebookMenuDocs"_s)), "the notebook switcher lists the documentation");
    check(waitFor(
                  [&] {
                      return app->inDocsVault() && ready()
                             && app->currentRoute().value(u"path"_s).toString() == docsWelcomePage(app->notebooksLanguage());
                  },
                  10000),
          "one tap on the documentation opens it on its table of contents");
    const QString name = QFileInfo(sample).fileName();
    check(pick([&](QQuickItem *item) {
              return item->objectName() == u"notebookMenuRecent" && item->property("text").toString() == name;
          }),
          "the notebook switcher lists the sample notebook");
    check(waitFor([&] { return !app->inDocsVault() && ready() && app->workspace()->rootPath() == sample; }, 10000),
          "one tap on a recent notebook switches back to it");
    closeMenus(window);
}

/// 5e. The drawer's edge swipe (phones, real touch input; docs/UI.md "Window layout"): a finger
/// from the left edge opens it, a swipe to the left closes it; where the system keeps the edge for
/// its back gesture (GestureInsets, simulated here as Android's gesture navigation reports it) the
/// strip starts beyond that zone. What the strip must not take: a vertical scroll, a swipe that
/// starts farther in, a tap on what lies under it, the drag of a selection grip lying in it (no
/// strip while blocks are selected or the keyboard is up — the top bar's ☰ opens the drawer then,
/// the keyboard having taken the bottom navigation's place —; a swipe still closes it). Runs
/// on a page (Remont mieszkania) with the drawer closed and stays there, without leaving the page
/// view (the back button's check goes to another page and back, which keeps the view): a view the
/// shell leaves waits for its deferred deletion, which never comes while the self-test runs, and
/// later findChild() calls would find its items.
void checkDrawerSwipe(QQuickWindow *window, AppController *app, QObject *drawer, const Check &check)
{
    QQuickItem *shellItem = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
    if (!shellItem) {
        check(false, "the shell is on screen");
        return;
    }
    // True when it ran without an error.
    const auto run = [&](const QString &js) {
        QQmlExpression expression(qmlContext(shellItem), shellItem, js);
        expression.evaluate();
        if (expression.hasError())
            std::fprintf(stderr, "selftest: %s\n", qPrintable(expression.error().toString()));
        return !expression.hasError();
    };
    const auto opened = [drawer] { return drawer->property("opened").toBool(); };
    const auto closed = [drawer] { return !drawer->property("visible").toBool(); };
    const auto margin = [drawer] { return drawer->property("dragMargin").toReal(); };
    const qreal y = window->height() * 0.45;
    const auto swipeOpen = [&](qreal x) {
        touchDrag(window, QPointF(x, y), QPointF(window->width() * 0.6, 0));
        return waitFor(opened, 3000);
    };
    const auto swipeClose = [&] {
        const qreal w = drawer->property("width").toReal();
        touchDrag(window, QPointF(w * 0.7, y), QPointF(-w * 0.6, 0));
        return waitFor(closed, 3000);
    };

    check(margin() >= 20, "the drawer's edge strip is wide enough for a finger");
    check(swipeOpen(4), "a swipe from the left edge opens the drawer");
    check(swipeClose(), "a swipe to the left closes the drawer");
    check(swipeOpen(margin() - 6), "a swipe from the inner side of the edge strip opens the drawer");
    check(swipeClose(), "a swipe to the left closes it again");

    // Android's gesture navigation keeps 32 px of the edge for its back gesture: the strip lies
    // beyond them.
    const qreal plain = margin();
    if (run(u"GestureInsets.simulate(32, 32)"_s)) {
        check(waitFor([&] { return qAbs(margin() - (32 + plain)) <= 1; }),
              "with a system gesture zone at the edge the strip starts beyond it");
        check(swipeOpen(32 + plain / 2), "a swipe started just beyond the system's gesture zone opens the drawer");
        check(swipeClose(), "a swipe to the left closes the drawer (system gesture zone)");
        run(u"GestureInsets.simulate(-1, -1)"_s);
        check(waitFor([&] { return qAbs(margin() - plain) <= 1; }), "without the zone the strip is back at the edge");
    } else {
        check(false, "the system gesture insets can be simulated");
    }

    // A long press selects a block; the start grip of the selection lies at the left edge, in the
    // strip with a gesture-navigation zone: while blocks are selected (as while typing) there is
    // no strip, and dragging the grip is the grip's — the drawer used to take it.
    run(u"GestureInsets.simulate(32, 32)"_s);
    QQuickItem *editor = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BlockEditor"); });
    QQuickItem *text = renderedTextContaining(window->contentItem(), u"Budżet całkowity"_s);
    QQuickItem *grip = nullptr;
    if (editor && text) {
        touchTap(window, pointIn(text, 0.3), 1000);
        waitFor([&] {
            grip = findShown(window->contentItem(),
                             [](QQuickItem *item) { return item->objectName() == u"selectionGripStart"; });
            return grip != nullptr;
        });
    }
    check(grip && editor->property("selectionMode").toBool(), "a long press on a block selects it, with grips");
    if (grip) {
        const QPointF at = grip->mapToScene(QPointF(grip->width() / 2, grip->height() / 2));
        check(at.x() < 32 + plain, "the selection's start grip lies where the edge strip is");
        check(waitFor([&] { return margin() <= 0; }), "while blocks are selected there is no edge strip");
        touchDrag(window, at, QPointF(window->width() * 0.5, 0));
        settle(400);
        check(closed(), "dragging the selection's grip at the left edge does not open the drawer");
        check(editor->property("selectionMode").toBool(), "the blocks stay selected");
        // Opened by its button (here by a call) while there is no strip, the drawer still closes
        // with a swipe on it.
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor(opened, 3000) && margin() <= 0, "the drawer opens while blocks are selected");
        check(swipeClose(), "with no edge strip (blocks selected) a swipe to the left closes the drawer");
    }
    if (editor)
        QMetaObject::invokeMethod(editor, "leaveSelectionMode");
    check(waitFor([&] { return qAbs(margin() - (32 + plain)) <= 1; }), "the selection gone, the edge strip is back");
    run(u"GestureInsets.simulate(-1, -1)"_s);

    // The on-screen keyboard up (simulated): no strip either, and no bottom navigation with its
    // Menu — the top bar's ☰ (always there) opens the drawer then, and the drawer closes with a
    // swipe.
    const auto menuButton = [window] {
        return uidriver::topmost(window, [](QQuickItem *i) { return i->objectName() == u"topBarMenu"; });
    };
    if (run(u"KeyboardInsets.simulated = 300"_s)) {
        check(waitFor([&] { return margin() <= 0; }), "while the keyboard is up there is no edge strip");
        QQuickItem *button = nullptr;
        check(waitFor([&] { return (button = menuButton()) != nullptr; }),
              "while the keyboard hides the bottom navigation the top bar still shows ☰");
        // (Laid out with the top bar's next polish: found again once it settled.)
        settle(300);
        if ((button = menuButton()))
            tapOrClick(window, pointIn(button), true);
        else
            QMetaObject::invokeMethod(drawer, "open");
        check(waitFor(opened, 3000), "☰ opens the drawer while the keyboard is up");
        check(swipeClose(), "with the keyboard up a swipe to the left closes the drawer");
        run(u"KeyboardInsets.simulated = -1"_s);
        check(waitFor([&] { return qAbs(margin() - plain) <= 1; }), "the keyboard gone, the edge strip is back");
        settle(300);
        check(menuButton() != nullptr, "the keyboard gone, ☰ stays first in the top bar");
    } else {
        check(false, "the on-screen keyboard can be simulated");
    }

    // What the strip leaves alone.
    touchDrag(window, QPointF(margin() / 2, window->height() * 0.7), QPointF(0, -260));
    settle(400);
    check(closed(), "a vertical scroll started in the edge strip does not open the drawer");
    touchDrag(window, QPointF(margin() + 60, y), QPointF(window->width() * 0.5, 0));
    settle(400);
    check(closed(), "a swipe started beyond the edge strip does not open the drawer");
    // (A sideways swipe on a block's text that ends on it is a tap there — BlockText's
    // ReleaseWithinBounds — and edits the block, which takes the strip away: end it.)
    if (editor && editor->property("toolbarMode").toString() == u"block")
        uidriver::callQml(editor, "stopEditing", {QVariant(false)});
    waitFor([&] { return margin() > 0; });
    // The top bar's first button lies in the strip — ☰: a tap there is the button's (it opens
    // the drawer), not the strip's (which opens it only with a drag).
    const QString page = app->currentRoute().value(u"path"_s).toString();
    QQuickItem *firstButton = nullptr;
    waitFor([&] { return (firstButton = menuButton()) != nullptr; });
    const QPointF inStrip = firstButton ? QPointF(margin() / 2, pointIn(firstButton).y()) : QPointF();
    if (firstButton && firstButton->contains(firstButton->mapFromScene(inStrip))) {
        touchTap(window, inStrip);
        check(waitFor(opened, 3000), "a tap in the edge strip reaches the button under it (☰ opens the drawer)");
        check(swipeClose(), "a swipe to the left closes the drawer again");
    } else {
        check(false, "the top bar's ☰ lies in the edge strip");
    }

    check(app->currentView() == u"page" && app->currentRoute().value(u"path"_s).toString() == page && closed(),
          "the page is still shown, the drawer closed");
}

void checkRowsAndMenus(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const bool narrow = window->width() < 900;
    const auto view = [app] { return app->currentView(); };
    const auto routePath = [app] { return app->currentRoute().value(u"path"_s).toString(); };
    const auto pause = [](int ms) { settle(ms); };
    // A tap / a left click, and the gesture for the context menu: a long press / a right-click.
    const auto activate = [&](QQuickItem *item, qreal fx = 0.5) {
        if (!item)
            return; // the check after it fails
        if (touch)
            touchTap(window, pointIn(item, fx));
        else
            mouseClick(window, pointIn(item, fx));
    };
    const auto openMenuOn = [&](QQuickItem *item, qreal fx = 0.5) {
        if (!item)
            return;
        if (touch)
            touchTap(window, pointIn(item, fx), 1000);
        else
            mouseClick(window, pointIn(item, fx), Qt::RightButton);
    };
    const auto closeMenu = [&] {
        sendKey(window, Qt::Key_Escape);
        return waitFor([&] { return openMenus(window) == 0; });
    };
    // (The visual tree: Repeater and list delegates are not in the window's QObject tree.)
    const auto findNamed = [window](const QString &name) {
        return findShown(window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
    };
    const auto findType = [window](const char *type, const std::function<bool(QQuickItem *)> &pred = {}) {
        QQuickItem *found = nullptr;
        waitFor([&] {
            found = findShown(window->contentItem(),
                              [&](QQuickItem *item) { return isA(item, type) && (!pred || pred(item)); });
            return found != nullptr;
        });
        return found;
    };

    QObject *drawer = window->findChild<QObject *>(u"shellDrawer"_s);
    // The context menu of the sidebar on screen (the drawer's on phones). Not findChild() on the
    // window: a sidebar the shell dropped (narrow at start-up) waits for its deferred deletion,
    // which never comes while the self-test runs inside one event.
    QObject *treeMenu = nullptr;
    const auto drawerOpen = [drawer] { return drawer->property("opened").toBool(); };
    const auto drawerClosed = [drawer] { return !drawer->property("visible").toBool(); };
    // A row of the sidebar's page tree (the drawer's on phones), scrolled into view.
    const auto treeRow = [&](const QString &path) -> QQuickItem * {
        QQuickItem *list = findNamed(u"sidebarTree"_s);
        if (!list)
            return nullptr;
        if (auto *model = list->property("model").value<QObject *>()) {
            int row = -1;
            QMetaObject::invokeMethod(model, "indexOf", qReturnArg(row), path);
            if (row >= 0)
                QMetaObject::invokeMethod(list, "positionViewAtIndex", row, 4 /* ListView.Contain */);
        }
        pause(60);
        return findShown(list, [&](QQuickItem *item) {
            return item->property("isDir").isValid() && item->property("path").toString() == path;
        });
    };
    const auto expanded = [&](const QString &path) {
        QQuickItem *r = treeRow(path);
        return r && r->property("expanded").toBool();
    };
    const auto findTreeMenu = [&] {
        QQuickItem *sidebar = findNamed(u"sidebarTree"_s);
        while (sidebar && !isA(sidebar, "ShellSidebar"))
            sidebar = sidebar->parentItem();
        return sidebar ? sidebar->findChild<QObject *>(u"sidebarContextMenu"_s) : nullptr;
    };
    const QString folder = u"Projekty"_s;
    const QString page = u"Projekty/Remont mieszkania.md"_s;
    if (!drawer) {
        check(false, "the shell has its drawer");
        return;
    }
    const QString empty = u"Szkice"_s;
    const QString emptyText = QCoreApplication::translate("ShellSidebar", "Empty folder");
    // An empty folder opens as well and says so: a tap that showed nothing looked like a tap
    // that did not work.
    const auto checkEmptyFolder = [&] {
        activate(treeRow(empty));
        QQuickItem *r = nullptr;
        check(waitFor([&] { return (r = treeRow(empty)) && r->property("expanded").toBool(); })
                      && r->property("subtitle").toString() == emptyText && openMenus(window) == 0,
              "an empty folder opens and says it is empty");
    };
    check(!app->workspace()->createFolder(empty).isEmpty(), "an empty folder is created");
    app->goHome();
    pause(200);

    // ---- The page tree: the drawer on phones (opened from the top bar's ☰), the sidebar on
    //      desktops.
    if (narrow) {
        QQuickItem *menuButton = findNamed(u"topBarMenu"_s);
        check(menuButton != nullptr, "the top bar has ☰");
        if (!menuButton)
            return;
        activate(menuButton);
        check(waitFor(drawerOpen), "the top bar's ☰ opens the drawer");
    }
    treeMenu = findTreeMenu();
    if (!treeMenu) {
        check(false, "the sidebar's page tree has a context menu");
        return;
    }
    // Polled: a delegate still being created runs its bindings a moment later.
    const auto noFingerRightClicks = [&](const char *what) {
        QStringList bad;
        const bool none = waitFor([&] { return (bad = fingerRightClickHandlers(window)).isEmpty(); }, 3000);
        if (!none)
            std::fprintf(stderr, "selftest: right-click handlers that take a finger: %s\n",
                         qPrintable(bad.join(u", "_s)));
        check(none, what);
    };
    noFingerRightClicks(narrow ? "no right-click handler in the drawer reacts to a finger"
                               : "no right-click handler in the sidebar reacts to a finger");
    QQuickItem *row = treeRow(folder);
    check(row && row->property("isDir").toBool(), "the page tree lists the folder Projekty");
    if (!row)
        return;
    if (row->property("expanded").toBool()) { // remembered from an earlier run: start collapsed
        activate(row);
        waitFor([&] { return !expanded(folder); });
    }
    if (!touch) {
        openMenuOn(treeRow(folder));
        check(waitFor([&] { return treeMenu->property("visible").toBool(); })
                      && treeMenu->property("path").toString() == folder && treeMenu->property("isDir").toBool()
                      && !expanded(folder),
              "a right-click on a folder opens its context menu (and does not expand it)");
        check(closeMenu(), "Escape closes the folder's context menu");
    }
    activate(treeRow(folder));
    check(waitFor([&] { return expanded(folder) && treeRow(page); }) && openMenus(window) == 0,
          touch ? "a tap on a folder in the drawer expands it, without a menu"
                : "a click on a folder in the sidebar expands it, without a menu");
    if (!touch) {
        openMenuOn(treeRow(page));
        check(waitFor([&] { return treeMenu->property("visible").toBool(); })
                      && treeMenu->property("path").toString() == page && view() == u"journals",
              "a right-click on a page opens its context menu (and does not open the page)");
        check(closeMenu(), "Escape closes the page's context menu");
    }
    // The keyboard (desktop): the Menu key on the focused row opens the same menu with nothing
    // highlighted, Down walks the items shown (a folder's own are hidden for a page), Escape gives
    // the focus back to the row; Down, Down reaches "Open in side panel" (wide windows) and Enter
    // opens the page there. First the pointer leaves the place where the menu opens (the item
    // under it would take the highlight).
    if (!touch && !narrow && app->sidePanelAllowed()) {
        mouseMove(window, QPointF(1, 1));
        const auto menuFromKey = [&] {
            QQuickItem *r = treeRow(page);
            if (!r)
                return false;
            r->forceActiveFocus(Qt::TabFocusReason);
            sendKey(window, Qt::Key_Menu);
            return waitFor([&] { return treeMenu->property("visible").toBool(); })
                   && treeMenu->property("path").toString() == page;
        };
        bool opened = menuFromKey();
        sendKey(window, Qt::Key_Down);
        const QString first = focusName(window);
        sendKey(window, Qt::Key_Escape);
        QQuickItem *r = nullptr;
        check(opened && first == u"treeMenuOpenTab" && waitFor([&] { return openMenus(window) == 0; })
                      && (r = treeRow(page)) && r->hasActiveFocus(),
              "the Menu key opens a page's menu in the tree, Down highlights its first item, Escape gives the "
              "focus back to the row");
        opened = menuFromKey();
        sendKey(window, Qt::Key_Down);
        sendKey(window, Qt::Key_Down);
        const QString second = focusName(window);
        sendKey(window, Qt::Key_Return);
        check(opened && second == u"treeMenuOpenSide" && waitFor([&] {
                  return openMenus(window) == 0 && app->sidePanelVisible()
                         && app->sidePanelRoute().value(u"path"_s).toString() == page;
              }),
              "in a page's menu in the tree Down, Down reaches “Open in side panel” and Enter opens the page there");
        app->clearSidePanelRoute();
        app->setSidePanelVisible(false);
    }
    activate(treeRow(page));
    check(waitFor([&] { return view() == u"page" && routePath() == page; }) && openMenus(window) == 0,
          touch ? "a tap on a page in the drawer opens it, without a menu"
                : "a click on a page in the sidebar opens it, without a menu");
    if (narrow) {
        check(waitFor(drawerClosed), "opening a page from the drawer closes it");
        // ☰ always leads the top bar; the bottom navigation holds views only (no Menu, no
        // capture: nothing there repeats the top bar). Back follows ☰ with history, but not on
        // Android, whose back key / gesture walks the same history.
        check(findNamed(u"topBarMenu"_s) != nullptr && findNamed(u"bottomNav_menu"_s) == nullptr
                      && findNamed(u"bottomNav_capture"_s) == nullptr,
              "on a phone ☰ is on the top bar and the bottom navigation has views only");
        check(findNamed(u"bottomNav_search"_s) != nullptr && findNamed(u"topBarSearch"_s) == nullptr,
              "Search is on the bottom navigation, not again on the top bar");
        if (QQuickItem *topBar = findType("ShellTopBar")) {
            const bool expected = app->canGoBack() && !app->isAndroid();
            check(topBar->property("backShown").toBool() == expected && (findNamed(u"topBarBack"_s) != nullptr) == expected,
                  "the top bar's back button shows only with history and without the system's back");
        } else {
            check(false, "the top bar is shown");
        }
        // Through ☰ again: a long press opens the row's context menu.
        QQuickItem *menuButton = findNamed(u"topBarMenu"_s);
        if (!menuButton)
            return;
        activate(menuButton);
        check(waitFor(drawerOpen), "the top bar's ☰ opens the drawer again");
        checkEmptyFolder();
        if (QQuickItem *pageRow = treeRow(page)) {
            openMenuOn(pageRow);
            check(waitFor([&] { return treeMenu->property("visible").toBool(); })
                          && treeMenu->property("path").toString() == page && !drawerClosed(),
                  "a long press on a page in the drawer opens its context menu");
            check(closeMenu(), "Escape closes the context menu");
        } else {
            check(false, "the drawer still lists the page");
        }
        if (QQuickItem *folderRow = treeRow(folder)) {
            openMenuOn(folderRow);
            check(waitFor([&] { return treeMenu->property("visible").toBool(); })
                          && treeMenu->property("path").toString() == folder && expanded(folder),
                  "a long press on a folder in the drawer opens its context menu (the folder stays open)");
            check(closeMenu(), "Escape closes the folder's context menu");
        }
        sendKey(window, Qt::Key_Escape);
        check(waitFor(drawerClosed) && view() == u"page", "Escape closes the drawer");
        if (touch)
            checkDrawerSwipe(window, app, drawer, check);
    } else {
        checkEmptyFolder();
    }

    // ---- "All pages": a row opens its page; the long press / right-click opens its menu.
    app->navigate({{u"view"_s, u"allPages"_s}});
    const auto pagesRow = [&] {
        QQuickItem *list = findType("AllPagesView");
        return findShown(list, [](QQuickItem *item) { return !item->property("shownTitle").toString().isEmpty(); });
    };
    if (QQuickItem *r = pagesRow()) {
        noFingerRightClicks("no right-click handler in All pages reacts to a finger");
        openMenuOn(r, 0.3);
        check(waitFor([&] { return openMenus(window) == 1; }) && view() == u"allPages",
              "a long press or right-click on a row in All pages opens its menu");
        check(closeMenu(), "Escape closes the All pages menu");
        activate(pagesRow(), 0.3);
        check(waitFor([&] { return view() == u"page"; }) && openMenus(window) == 0,
              "a tap or click on a row in All pages opens the page, without a menu");
    } else {
        check(false, "All pages lists the pages");
    }

    // ---- Tasks: a row opens the task's note; the long press / right-click opens its menu.
    app->navigate({{u"view"_s, u"tasks"_s}});
    if (QQuickItem *r = findType("TasksRow")) {
        noFingerRightClicks("no right-click handler in the tasks view reacts to a finger");
        openMenuOn(r, 0.75);
        check(waitFor([&] { return openMenus(window) == 1; }) && view() == u"tasks",
              "a long press or right-click on a task opens its menu");
        check(closeMenu(), "Escape closes the task's menu");
        // Past the double-click interval since the long press's release: a tap right after it
        // is the second click of a double click (no clicked()).
        settle(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);
        activate(findType("TasksRow"), 0.75);
        check(waitFor([&] { return view() != u"tasks"; }) && openMenus(window) == 0,
              "a tap or click on a task opens its note, without a menu");
    } else {
        check(false, "the tasks view lists tasks");
    }

    // ---- The block editor's bullets (an outline page): a tap / click zooms into the block; the
    //      long press / right-click opens the block menu.
    app->openPage(page);
    // The controller of the editor on screen (earlier page views may still wait for their
    // deferred deletion, see treeMenu).
    QObject *controller = nullptr;
    waitFor([&] {
        QQuickItem *editor = findShown(window->contentItem(), [&](QQuickItem *item) {
            if (item->objectName() != u"pageEditor")
                return false;
            const auto *c = item->property("controller").value<QObject *>();
            return c && c->property("path").toString() == page && c->property("loaded").toBool();
        });
        controller = editor ? editor->property("controller").value<QObject *>() : nullptr;
        return controller != nullptr;
    });
    const QString zoomName = QCoreApplication::translate("BlockDelegate", "Zoom into block");
    const auto bullet = [&] {
        return findType("QQuickItem", [&zoomName](QQuickItem *item) {
            const QObjectList children = item->children();
            for (const QObject *c : children) {
                if (c->inherits("QQuickAccessibleAttached") && c->property("name").toString() == zoomName)
                    return true;
            }
            return false;
        });
    };
    QQuickItem *dot = controller ? bullet() : nullptr;
    check(dot != nullptr, "the outline page shows its bullets");
    if (dot) {
        noFingerRightClicks("no right-click handler in the block editor reacts to a finger");
        openMenuOn(dot);
        check(waitFor([&] { return openMenus(window) == 1; }) && controller->property("zoomBlock").toInt() < 0,
              "a long press or right-click on a bullet opens the block menu (and does not zoom)");
        check(findShown(window->contentItem(),
                        [](QQuickItem *item) {
                            const auto *sub = item->property("subMenu").value<QObject *>();
                            return sub && sub->objectName() == u"copyLinkMenu";
                        })
                      != nullptr,
              "the block menu offers “Copy link” (links to the block)");
        check(closeMenu(), "Escape closes the block menu");
        activate(bullet());
        check(waitFor([&] { return controller->property("zoomBlock").toInt() >= 0; }) && openMenus(window) == 0,
              "a tap or click on a bullet zooms into the block, without a menu");
        QMetaObject::invokeMethod(controller, "zoomOut");
    }
    // The block menu walked by keyboard (desktop): Down goes from item to item shown — past the
    // ones this block has not (a table's, a table cell's Row and Column submenus) — to "Zoom in",
    // and Enter zooms into the block. (An item under the pointer may be highlighted first: the
    // walk is a tail of the items shown.)
    if (dot && !touch) {
        settle(200); // (the zoom ended: the bullets laid out again)
        openMenuOn(bullet());
        const bool opened = waitFor([&] { return openMenus(window) == 1; });
        mouseMove(window, QPointF(1, 1));
        QStringList walked;
        for (int i = 0; i < 12 && opened && focusName(window) != u"blockMenuZoom"; ++i) {
            sendKey(window, Qt::Key_Down);
            walked << focusName(window);
        }
        const QStringList shownItems = {u"blockMenuCopy"_s, u"blockMenuCut"_s, u"blockMenuCopyText"_s,
                                        u"blockMenuSelect"_s, u"blockMenuZoom"_s};
        const bool tail = !walked.isEmpty() && walked.size() <= shownItems.size()
                          && shownItems.mid(shownItems.size() - walked.size()) == walked;
        if (!tail)
            std::fprintf(stderr, "selftest: the block menu's keyboard walk: %s\n", qPrintable(walked.join(u", "_s)));
        sendKey(window, Qt::Key_Return);
        check(opened && tail
                      && waitFor([&] {
                             return controller->property("zoomBlock").toInt() >= 0 && openMenus(window) == 0;
                         }),
              "the block menu is walked with Down over the items shown only, and Enter on “Zoom in” zooms in");
        QMetaObject::invokeMethod(controller, "zoomOut");
    }
}

/// 3e. Folders, with real input: the page opened is revealed and highlighted in the tree; a row's
///     menu (the "⋯" button on touch screens, a right-click on desktops) has "Move to…", whose
///     dialog moves the page into the folder chosen; the "Pages" header's "+" menu creates a root
///     folder (named in the dialog, typed with real keys) and a deleted empty folder leaves the
///     tree; on desktops a page dragged with the mouse onto a folder moves into it, onto the
///     "Pages" header back to the root, a closed folder hovered over opens and Escape cancels
///     the drag, the target follows the rows the list scrolls under a still pointer and the
///     header scrolled out of the list is none; a collapsed "Pages" stays collapsed when another
///     page opens, "Show in tree" opens it; a "/" typed into the page header's title stays in
///     the title (the page keeps its folder); All pages lists folders as rows (a tap opens one)
///     and the top bar's breadcrumb goes back. The sample notebook is put back afterwards.
void checkFolders(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const bool narrow = window->width() < 900;
    Workspace *ws = app->workspace();
    const QString recipes = u"Przepisy.md"_s;
    const QString remont = u"Projekty/Remont mieszkania.md"_s;
    QObject *drawer = window->findChild<QObject *>(u"shellDrawer"_s);
    const auto named = [window](const QString &name) {
        QQuickItem *found = nullptr;
        waitFor([&] {
            found = findShown(window->contentItem(), [&name](QQuickItem *item) { return item->objectName() == name; });
            return found != nullptr;
        });
        return found;
    };
    const auto openDrawer = [&] {
        if (!narrow || !drawer || drawer->property("opened").toBool())
            return;
        QMetaObject::invokeMethod(drawer, "open");
        waitFor([&] { return drawer->property("opened").toBool(); });
        settle(200);
    };
    const auto closeDrawer = [&] {
        if (!drawer || !drawer->property("visible").toBool())
            return;
        QMetaObject::invokeMethod(drawer, "close");
        waitFor([&] { return !drawer->property("visible").toBool(); });
    };
    const auto treeList = [window]() -> QQuickItem * {
        return findShown(window->contentItem(), [](QQuickItem *item) { return item->objectName() == u"sidebarTree"; });
    };
    // The sidebar's (or the drawer's) row of `path`, scrolled into view.
    const auto treeRow = [&](const QString &path) -> QQuickItem * {
        QQuickItem *list = treeList();
        if (!list)
            return nullptr;
        if (auto *model = list->property("model").value<QObject *>()) {
            int row = -1;
            QMetaObject::invokeMethod(model, "indexOf", qReturnArg(row), path);
            if (row < 0)
                return nullptr;
            QMetaObject::invokeMethod(list, "positionViewAtIndex", row, 4 /* ListView.Contain */);
        }
        settle(60);
        return findShown(list, [&](QQuickItem *item) {
            return item->objectName() == u"treeRow" && item->property("path").toString() == path;
        });
    };
    const auto treeModel = [&]() -> QObject * {
        QQuickItem *list = treeList();
        return list ? list->property("model").value<QObject *>() : nullptr;
    };
    const auto expanded = [&](const QString &folder) {
        QObject *model = treeModel();
        bool open = false;
        if (model)
            QMetaObject::invokeMethod(model, "isExpanded", qReturnArg(open), folder);
        return open;
    };
    const auto sidebarItem = [&]() -> QQuickItem * {
        QQuickItem *sidebar = treeList();
        while (sidebar && !isA(sidebar, "ShellSidebar"))
            sidebar = sidebar->parentItem();
        return sidebar;
    };
    const auto treeMenu = [&]() -> QObject * {
        QQuickItem *sidebar = sidebarItem();
        return sidebar ? sidebar->findChild<QObject *>(u"sidebarContextMenu"_s) : nullptr;
    };
    const auto tap = [&](QQuickItem *item) {
        if (item)
            tapOrClick(window, pointIn(item), touch);
    };
    const auto restore = [&] {
        closeMenus(window);
        for (const QString &moved : {u"Projekty/Przepisy.md"_s, u"templates/Przepisy.md"_s}) {
            if (ws->exists(moved))
                ws->movePath(moved, QString());
        }
        for (const QString &folder : {u"Zażółć"_s, u"Przewijanie"_s}) {
            if (ws->exists(folder))
                ws->deletePage(folder);
        }
        if (ws->exists(u"Projekty/TCP IP.md"_s))
            ws->renamePage(u"Projekty/TCP IP.md"_s, u"Remont mieszkania"_s);
        settle(200);
    };
    // The row of `path` lies wholly in the tree's visible part, where the sidebar left it (not
    // placed by the test as treeRow() does): a row under the list's bottom edge is behind the
    // sync footer — a tap there opened the sync center (the Android emulator's --folders run).
    // A row the list shows — not one of its pooled delegates (reuseItems): those are culled, not
    // hidden, so they look shown, at the place they had when they were pooled.
    const auto liveRow = [](QQuickItem *list, QQuickItem *item) {
        QQuickItem *live = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", qReturnArg(live), item->property("index").toInt());
        return live == item;
    };
    const auto rowIn = [&](QQuickItem *list, const QString &path) -> QQuickItem * {
        return !list ? nullptr : findShown(list, [&](QQuickItem *item) {
            return item->objectName() == u"treeRow" && item->property("path").toString() == path && liveRow(list, item);
        });
    };
    const auto rowInView = [&](const QString &path) {
        QQuickItem *list = treeList();
        const QQuickItem *r = rowIn(list, path);
        if (!r)
            return false;
        const QRectF in = r->mapRectToItem(list, QRectF(0, 0, r->width(), r->height()));
        return in.top() >= -0.5 && in.bottom() <= list->height() + 0.5;
    };
    const auto rowPlace = [&](const QString &path) {
        QQuickItem *list = treeList();
        const QQuickItem *r = rowIn(list, path);
        const QRectF in = r ? r->mapRectToItem(list, QRectF(0, 0, r->width(), r->height())) : QRectF();
        std::fprintf(stderr, "selftest: tree row %s at %.0f-%.0f of the list's %.0f px (contentY %.0f)\n",
                     qPrintable(path), in.top(), in.bottom(), list ? list->height() : -1.0,
                     list ? list->property("contentY").toReal() : -1.0);
    };
    // A sidebar laid out at another height before it shows the row: on phones the drawer gets
    // its system-bar padding only as it opens, and a keyboard that was up goes as it opens — its
    // list ends up shorter than it was when the row was revealed. Here the window is shorter.
    const QSize fullSize = window->size();
    const auto shortWindow = [&](bool shorter) {
        window->resize(fullSize.width(), shorter ? fullSize.height() * 2 / 3 : fullSize.height());
        settle(100);
    };

    // ---- The page opened elsewhere (a link, search, the switcher) shows in the tree — in its
    //      visible part, though the drawer was closed (and the sidebar laid out at another height).
    app->goHome();
    settle(200);
    openDrawer(); // (its list at the top, as a fresh start leaves it)
    if (QQuickItem *list = treeList()) {
        QMetaObject::invokeMethod(list, "positionViewAtBeginning");
        list->setProperty("scrolledAway", false);
    }
    settle(100);
    closeDrawer();
    app->openPage(remont);
    settle(200);
    shortWindow(true);
    openDrawer();
    const bool remontInView = waitFor([&] { return rowInView(remont); });
    if (!remontInView)
        rowPlace(remont);
    check(remontInView, "the page opened is scrolled into the tree's visible part (above the sync footer)");
    shortWindow(false);
    check(waitFor([&] { return rowInView(remont); }), "and stays in view when the sidebar grows again");
    QQuickItem *row = nullptr;
    check(waitFor([&] { return (row = treeRow(remont)) && row->property("selected").toBool(); }),
          "the page opened is revealed and highlighted in the tree");

    // ---- A row's menu: the "⋯" button (touch) or a right-click; "Move to…".
    row = treeRow(recipes);
    QObject *menu = treeMenu();
    check(row && menu, "the tree lists a page and has its menu");
    if (!row || !menu) {
        restore();
        return;
    }
    if (touch) {
        QQuickItem *more = findShown(row, [](QQuickItem *item) { return item->objectName() == u"treeRowMore"; });
        check(more != nullptr && more->height() >= 44, "touch screens show a “⋯” button on each row (44 px)");
        tap(more);
    } else {
        mouseClick(window, pointIn(row), Qt::RightButton);
    }
    check(waitFor([&] { return menu->property("visible").toBool() && menu->property("path").toString() == recipes; }),
          touch ? "the row's “⋯” opens its menu" : "a right-click on a row opens its menu");
    tap(named(u"treeMenuMove"_s));
    QObject *moveDialog = window->findChild<QObject *>(u"shellMoveDialog"_s);
    check(moveDialog && waitFor([&] { return moveDialog->property("opened").toBool(); }),
          "“Przenieś do…” opens the move dialog");
    if (moveDialog && moveDialog->property("opened").toBool()) {
        QQuickItem *target = nullptr;
        waitFor([&] {
            target = findShown(window->contentItem(), [](QQuickItem *item) {
                return item->objectName() == u"moveDialogRow" && item->property("text").toString() == u"Projekty";
            });
            return target != nullptr;
        });
        tap(target);
        check(waitFor([&] { return moveDialog->property("acceptEnabled").toBool(); }),
              "choosing a folder in the move dialog shows what changes and allows moving");
        const QString moveHere = QCoreApplication::translate("ShellMoveDialog", "Move here");
        QQuickItem *accept = findShown(window->contentItem(), [&](QQuickItem *item) {
            return isA(item, "BButton") && item->property("text").toString() == moveHere;
        });
        tap(accept);
        check(waitFor([&] { return ws->exists(u"Projekty/Przepisy.md"_s) && !ws->exists(recipes); }),
              "“Przenieś tutaj” moves the page into the folder");
        check(ws->resolveLink(u"Przepisy"_s, u"Witaj w Brulionie.md"_s) == u"Projekty/Przepisy.md"_s,
              "links to the moved page still reach it");
        openDrawer();
        check(waitFor([&] { return treeRow(u"Projekty/Przepisy.md"_s) != nullptr; }),
              "the tree shows the page in its new folder");
        QMetaObject::invokeMethod(moveDialog, "close");
    }
    restore();

    // ---- A root folder from the "Pages" header's "+" menu.
    openDrawer();
    QQuickItem *header = named(u"sidebarPagesHeader"_s);
    QQuickItem *plus = header ? findShown(header, [](QQuickItem *item) { return isA(item, "BIconButton"); }) : nullptr;
    check(plus != nullptr, "the “Strony” header has its “+” button");
    tap(plus);
    QQuickItem *newFolder = named(u"pagesMenuNewFolder"_s);
    check(newFolder != nullptr, "the “+” menu offers a new folder");
    tap(newFolder);
    QObject *newPage = window->findChild<QObject *>(u"shellNewPage"_s);
    check(newPage && waitFor([&] {
              return newPage->property("opened").toBool() && newPage->property("folderMode").toBool();
          }),
          "“Nowy folder” opens the dialog for a folder at the root");
    if (newPage && newPage->property("opened").toBool()) {
        waitFor([window] {
            const QQuickItem *f = window->activeFocusItem();
            return f && f->inherits("QQuickTextInput");
        });
        // A folder that exists (typed in another case: createFolder would only show it) is said
        // at the field while typing, Create disabled.
        if (QQuickItem *field = window->activeFocusItem()) {
            field->setProperty("text", u"projekty"_s);
            const QString exists = QCoreApplication::translate("brulion::shell::Workspace",
                                                               "A folder named “%1” already exists here.")
                                           .arg(u"Projekty"_s);
            check(waitFor([&] { return field->property("errorText").toString() == exists; })
                          && !newPage->property("acceptEnabled").toBool(),
                  "a folder that exists is said at the new folder's name field, Create disabled");
            field->setProperty("text", QString());
        }
        // (The drawer, closed for the dialog, opens again with the new folder revealed while it
        // lays itself out at another height: the keyboard's room on a phone.)
        shortWindow(true);
        if (touch) {
            // A phone's keyboard still composes the word when "Create" is tapped: preedit text,
            // outside the field's text (Android's keyboards compose every word).
            composeWord(window->activeFocusItem(), u"Zażółć"_s);
            const QString create = QCoreApplication::translate("ShellNewPageDialog", "Create");
            QQuickItem *accept = findShown(window->contentItem(), [&](QQuickItem *item) {
                return isA(item, "BButton") && item->property("text").toString() == create;
            });
            check(accept && accept->isEnabled(), "a name still being composed enables “Utwórz”");
            tap(accept);
        } else {
            for (const QChar ch : u"Zażółć"_s)
                sendKey(window, 0, Qt::NoModifier, QString(ch));
            sendKey(window, Qt::Key_Return);
        }
        check(waitFor([&] { return ws->exists(u"Zażółć"_s); }), "the typed folder is created at the root");
        openDrawer();
        const bool folderInView = waitFor([&] { return rowInView(u"Zażółć"_s); });
        if (!folderInView)
            rowPlace(u"Zażółć"_s);
        check(folderInView, "the new folder is revealed in the tree's visible part");
        shortWindow(false);
        check(waitFor([&] { return treeRow(u"Zażółć"_s) != nullptr; }), "the new folder shows in the tree");
        // The Delete key on a focused row asks first; then an empty folder leaves the tree at once
        // (folders have no file changes).
        if (QQuickItem *folderRow = treeRow(u"Zażółć"_s)) {
            folderRow->forceActiveFocus(Qt::TabFocusReason);
            sendKey(window, Qt::Key_Delete);
        }
        const QString trashText = QCoreApplication::translate("AppShell", "Move to trash");
        QQuickItem *confirmTrash = nullptr;
        check(waitFor([&] {
                  confirmTrash = findShown(window->contentItem(), [&](QQuickItem *item) {
                      return isA(item, "BButton") && item->property("text").toString() == trashText;
                  });
                  return confirmTrash != nullptr;
              }),
              "Delete on a row of the tree asks first");
        check(ws->exists(u"Zażółć"_s), "nothing is deleted before the confirmation");
        if (confirmTrash)
            tap(confirmTrash);
        check(waitFor([&] { return treeRow(u"Zażółć"_s) == nullptr; }) && !ws->exists(u"Zażółć"_s),
              "a deleted empty folder leaves the tree");
    }
    restore();

    // ---- A click (a tap) on a row in view leaves the tree where it is, though a reveal scrolled
    //      it there: the row stays under the pointer. (Placed from its top again, the list put the
    //      clicked row at its bottom edge and the next click hit another row.)
    for (int i = 1; i <= 30; ++i)
        ws->createPage(u"Strona %1"_s.arg(i, 2, 10, QChar(u'0')), u"Przewijanie"_s);
    const QString deepest = u"Przewijanie/Strona 30.md"_s;
    // The top of the row of `path` in the tree's visible part (-1: not wholly in it).
    const auto rowTop = [&](const QString &path) -> qreal {
        QQuickItem *list = treeList();
        const QQuickItem *r = rowIn(list, path);
        if (!r)
            return -1;
        const QRectF in = r->mapRectToItem(list, QRectF(0, 0, r->width(), r->height()));
        return in.top() >= -0.5 && in.bottom() <= list->height() + 0.5 ? in.top() : -1;
    };
    app->openPage(deepest);
    settle(200);
    openDrawer();
    check(waitFor([&] { return rowInView(deepest); }), "a page deep in a long folder is revealed in the tree's view");
    // The topmost of the folder's other pages wholly in view.
    QString clicked;
    qreal clickedTop = -1;
    if (QQuickItem *list = treeList()) {
        for (QQuickItem *item : uidriver::allItems(list)) {
            const QString path = item->property("path").toString();
            if (item->objectName() != u"treeRow" || !shown(item) || !liveRow(list, item)
                || !path.startsWith(u"Przewijanie/Strona ") || path == deepest)
                continue;
            const qreal top = rowTop(path);
            if (top >= 0 && (clickedTop < 0 || top < clickedTop)) {
                clickedTop = top;
                clicked = path;
            }
        }
        check(clickedTop >= 0 && !list->property("atYBeginning").toBool(),
              "the tree is scrolled to it, other pages of the folder in view above it");
    }
    if (clickedTop >= 0) {
        tap(rowIn(treeList(), clicked));
        check(waitFor([&] { return app->currentRoute().value(u"path"_s).toString() == clicked; }),
              "a click on such a row opens its page");
        settle(300);
        openDrawer(); // (the phone's drawer closed as the page opened)
        settle(100);
        const qreal after = rowTop(clicked);
        if (qAbs(after - clickedTop) > 1)
            std::fprintf(stderr, "selftest: clicked tree row %s: %.0f px from the list's top before, %.0f after\n",
                         qPrintable(clicked), clickedTop, after);
        check(qAbs(after - clickedTop) <= 1, "and the tree stays where it was: the clicked row under the pointer");
    }
    // The user's scrolling with a touchpad, a mouse wheel or the arrow keys: the list stays where
    // they left it when its geometry changes — the row revealed before is not brought back.
    if (QQuickItem *list = treeList(); !touch && list && sidebarItem()) {
        // `page` opened (revealed: kept in view), the list scrolled by `scroll`, the window resized.
        const auto staysScrolled = [&](const QString &page, const std::function<void()> &scroll) {
            if (app->currentRoute().value(u"path"_s).toString() != page) {
                app->openPage(page);
                settle(200);
            }
            const QString revealedBefore = sidebarItem()->property("revealedPath").toString();
            const qreal before = list->property("contentY").toReal();
            scroll();
            waitFor([&] { return !list->property("moving").toBool(); });
            settle(100);
            const qreal scrolled = list->property("contentY").toReal();
            const QString revealed = sidebarItem()->property("revealedPath").toString();
            const bool away = list->property("scrolledAway").toBool();
            shortWindow(true);
            shortWindow(false);
            const qreal resized = list->property("contentY").toReal();
            const bool ok = revealedBefore == page && qAbs(scrolled - before) > 1 && revealed.isEmpty()
                            && away == !list->property("atYBeginning").toBool() && qAbs(resized - scrolled) <= 1;
            if (!ok)
                std::fprintf(stderr,
                             "selftest: tree scrolled: revealed '%s', contentY %.0f -> %.0f -> %.0f after a resize, "
                             "revealed '%s', away %d\n",
                             qPrintable(revealedBefore), before, scrolled, resized, qPrintable(revealed), int(away));
            return ok;
        };
        const QPointF middle = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
        check(staysScrolled(clicked, [&] { uidriver::run(uidriver::wheel(window, middle, QPointF(0, -120))); }),
              "the tree scrolled with a touchpad stays there when the window changes");
        check(staysScrolled(deepest,
                            [&] {
                                QWheelEvent wheel(middle, window->mapToGlobal(middle), QPoint(), QPoint(0, 120),
                                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                                QCoreApplication::sendEvent(window, &wheel);
                            }),
              "the tree scrolled with a mouse wheel stays there when the window changes");
        check(staysScrolled(clicked,
                            [&] {
                                if (QQuickItem *r = rowIn(list, clicked))
                                    r->forceActiveFocus();
                                for (int i = 0; i < 8; ++i)
                                    sendKey(window, Qt::Key_Down);
                            }),
              "rows walked with the arrow keys: the tree stays where they left it when the window changes");
    }
    closeDrawer();
    app->goHome();
    restore();

    // ---- The window's sidebar hidden while its header grows (two pages starred) shows its
    //      navigation again when it comes back, on a route with no file (nothing revealed).
    if (!narrow) {
        QQuickItem *shellItem =
                findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
        app->navigate({{u"view"_s, u"tasks"_s}});
        settle(200);
        QQuickItem *list = treeList();
        QQuickItem *listHeader = list ? list->property("headerItem").value<QQuickItem *>() : nullptr;
        if (list) {
            QMetaObject::invokeMethod(list, "positionViewAtBeginning");
            list->setProperty("scrolledAway", false);
        }
        settle(100);
        const qreal headerBefore = listHeader ? listHeader->height() : 0;
        if (shellItem && list && listHeader) {
            QMetaObject::invokeMethod(shellItem, "toggleSidebar");
            waitFor([&] { return !list->isVisible(); });
            ws->setFavorite(recipes, true);
            ws->setFavorite(remont, true);
            settle(200);
            QMetaObject::invokeMethod(shellItem, "toggleSidebar");
            waitFor([&] { return list->isVisible(); });
            settle(200);
        }
        const bool grew = listHeader && listHeader->height() > headerBefore;
        const bool atTop = list && list->property("atYBeginning").toBool();
        if (!atTop)
            std::fprintf(stderr, "selftest: sidebar shown again: header %.0f -> %.0f px, contentY %.0f, originY %.0f\n",
                         headerBefore, listHeader ? listHeader->height() : -1.0,
                         list ? list->property("contentY").toReal() : -1.0,
                         list ? list->property("originY").toReal() : -1.0);
        check(grew && atTop, "the sidebar shown again after its header grew shows its navigation at the top");
        ws->setFavorite(recipes, false);
        ws->setFavorite(remont, false);
        settle(100);
    }

    // ---- Dragging rows with the mouse (desktop): onto a folder, onto the header, cancelled.
    if (!touch) {
        if (!expanded(u"Projekty"_s))
            tap(treeRow(u"Projekty"_s));
        QQuickItem *from = treeRow(recipes);
        QQuickItem *onto = treeRow(u"Projekty"_s);
        if (from && onto)
            mouseDrag(window, pointIn(from, 0.3), pointIn(onto, 0.3));
        check(waitFor([&] { return ws->exists(u"Projekty/Przepisy.md"_s); }),
              "a page dragged onto a folder with the mouse moves into it");
        from = treeRow(u"Projekty/Przepisy.md"_s);
        header = named(u"sidebarPagesHeader"_s);
        if (from && header)
            mouseDrag(window, pointIn(from, 0.3), pointIn(header, 0.3));
        check(waitFor([&] { return ws->exists(recipes); }), "dragged onto the “Strony” header it goes to the root");
        // A closed folder hovered over opens; Escape cancels: nothing moves.
        if (expanded(u"templates"_s))
            tap(treeRow(u"templates"_s));
        waitFor([&] { return !expanded(u"templates"_s); });
        from = treeRow(recipes);
        onto = treeRow(u"templates"_s);
        if (from && onto)
            mouseDrag(window, pointIn(from, 0.3), pointIn(onto, 0.3), 900, true);
        settle(300);
        check(expanded(u"templates"_s), "a closed folder opens while a row is held over it");
        check(ws->exists(recipes) && !ws->exists(u"templates/Przepisy.md"_s), "Escape cancels the drag");
        // A plain click still opens the page (no drag).
        tap(treeRow(recipes));
        check(waitFor([&] { return app->currentRoute().value(u"path"_s).toString() == recipes; }),
              "a click on a row still opens the page");
    }
    restore();

    // ---- The drop target follows the rows that scroll under a pointer held at the list's edge;
    //      the "Pages" header scrolled out above the list is no target (desktop drags, driven
    //      through the sidebar's own drag functions).
    if (!touch) {
        QQuickItem *sidebar = sidebarItem();
        QQuickItem *list = treeList();
        QObject *model = treeModel();
        for (int i = 1; i <= 40; ++i)
            ws->createFolder(u"Przewijanie/Folder %1"_s.arg(i, 2, 10, QChar(u'0')));
        if (model)
            QMetaObject::invokeMethod(model, "setExpanded", u"Przewijanie"_s, true);
        settle(200);
        auto *pagesHeader = sidebar ? sidebar->property("pagesHeader").value<QQuickItem *>() : nullptr;
        auto *content = list ? list->property("contentItem").value<QQuickItem *>() : nullptr;
        check(sidebar && list && model && pagesHeader && content && expanded(u"Przewijanie"_s),
              "the tree shows a folder of 40 subfolders to scroll through");
        if (sidebar && list && model && pagesHeader && content) {
            const auto folderAt = [&](QPointF scene) {
                const QPointF p = list->mapFromScene(scene);
                int index = -1;
                QMetaObject::invokeMethod(list, "indexAt", qReturnArg(index),
                                          p.x() + list->property("contentX").toReal(),
                                          p.y() + list->property("contentY").toReal());
                QVariantMap entry;
                if (index >= 0)
                    QMetaObject::invokeMethod(model, "get", qReturnArg(entry), index);
                const QString path = entry.value(u"path"_s).toString();
                return entry.value(u"isDir"_s).toBool() ? path : ws->folderOf(path);
            };
            QMetaObject::invokeMethod(list, "positionViewAtBeginning");
            settle(100);
            const qreal top = list->property("contentY").toReal();
            uidriver::callQml(sidebar, "beginDrag",
                              {QVariant(remont), QVariant(false), QVariant(u"Remont mieszkania"_s)});
            const QPointF still = list->mapToScene(QPointF(list->width() / 2, list->height() - 12));
            uidriver::callQml(sidebar, "dragTo", {QVariant(still)});
            const QString first = sidebar->property("dropFolder").toString();
            settle(700);
            const QString now = folderAt(still);
            std::fprintf(stderr, "selftest: drag scroll: %.0f -> %.0f px, target %s -> %s (under the pointer: %s)\n",
                         top, list->property("contentY").toReal(), qPrintable(first),
                         qPrintable(sidebar->property("dropFolder").toString()), qPrintable(now));
            check(list->property("contentY").toReal() > top + 100 && now != first
                          && sidebar->property("dropFolder").toString() == now,
                  "while the list scrolls under a still pointer the drop target follows the rows");
            // The header in view is the root; scrolled just above the list's top edge it is not.
            QMetaObject::invokeMethod(list, "positionViewAtBeginning");
            settle(100);
            uidriver::callQml(sidebar, "dragTo", {QVariant(pointIn(pagesHeader, 0.5))});
            const bool rootShown = sidebar->property("dropOnRoot").toBool() && sidebar->property("dropValid").toBool();
            // A moment later the ghost says what the move would rewrite.
            QQuickItem *impactLine = sidebar->findChild<QQuickItem *>(u"treeDragImpact"_s);
            check(impactLine && waitFor([&] {
                      return impactLine->isVisible() && !impactLine->property("text").toString().isEmpty()
                             && sidebar->property("dropValid").toBool();
                  }),
                  "the drag's ghost says what the move rewrites before the drop");
            const qreal headerBottom = pagesHeader->mapToItem(content, QPointF(0, pagesHeader->height())).y();
            list->setProperty("contentY", headerBottom + 4);
            settle(100);
            const QPointF above = list->mapToScene(QPointF(list->width() / 2, -10));
            const bool overHeader = pagesHeader->contains(pagesHeader->mapFromScene(above));
            uidriver::callQml(sidebar, "dragTo", {QVariant(above)});
            check(rootShown && overHeader && !sidebar->property("dropOnRoot").toBool()
                          && !sidebar->property("dropValid").toBool(),
                  "the “Strony” header is a drop target while it shows, not once scrolled out of the list");
            QMetaObject::invokeMethod(sidebar, "cancelDrag");
        }
    }
    restore();

    // ---- A collapsed "Pages" stays collapsed (the setting too) when another page opens;
    //      "Show in tree" opens it and shows the page.
    openDrawer();
    if (QQuickItem *sidebar = sidebarItem(); sidebar && (header = named(u"sidebarPagesHeader"_s))) {
        // The header scrolls with the tree, which the drag above may have left scrolled.
        if (QQuickItem *list = treeList()) {
            QMetaObject::invokeMethod(list, "positionViewAtBeginning");
            settle(100);
        }
        if (sidebar->property("pagesOpen").toBool())
            tapOrClick(window, pointIn(header, 0.15), touch);
        const bool collapsed = waitFor([&] { return !sidebar->property("pagesOpen").toBool(); })
                               && !app->settings()->value(u"ui/sidebar/pages"_s).toBool();
        if (!collapsed) {
            const QPointF at = pointIn(header, 0.15);
            const QQuickItem *list = treeList();
            std::fprintf(stderr,
                         "selftest: “Strony” header at %.0f,%.0f, visible %d, open %d, setting '%s', tree at %.0f\n",
                         at.x(), at.y(), int(header->isVisible()), int(sidebar->property("pagesOpen").toBool()),
                         qPrintable(app->settings()->value(u"ui/sidebar/pages"_s).toString()),
                         list ? list->property("contentY").toReal() : -1.0);
        }
        check(collapsed, "a tap on “Strony” collapses the section");
        app->openPage(recipes);
        settle(300);
        check(!sidebar->property("pagesOpen").toBool() && !app->settings()->value(u"ui/sidebar/pages"_s).toBool(),
              "a page opened leaves a collapsed “Strony” collapsed");
        // (On desktops the rows of the folder removed above were pooled: opened again, the tree
        // shows its own rows, not those.)
        app->requestUiAction(u"revealInTree"_s);
        check(waitFor([&] { return sidebar->property("pagesOpen").toBool() && rowInView(recipes); }),
              "“Pokaż w drzewie” scrolls the page into the tree's visible part");
        check(waitFor([&] {
                  QQuickItem *r = sidebar->property("pagesOpen").toBool() ? treeRow(recipes) : nullptr;
                  return r && r->property("selected").toBool();
              }) && app->settings()->value(u"ui/sidebar/pages"_s).toBool(),
              "“Pokaż w drzewie” opens the section and shows the page");
    } else {
        check(false, "the sidebar has its “Strony” header");
    }
    closeDrawer();

    // ---- A "/" typed into the page header's title is part of the title: the page stays in its
    //      folder ("TCP/IP" -> "Projekty/TCP IP.md"), the confirmation names the new title.
    app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, remont}, {u"open"_s, u"rename"_s}});
    check(waitFor([window] {
              const QQuickItem *f = window->activeFocusItem();
              return f && f->inherits("QQuickTextInput")
                     && f->property("selectedText").toString() == u"Remont mieszkania";
          }),
          "the page header's title edit starts with the title selected");
    for (const QChar ch : u"TCP/IP"_s)
        sendKey(window, 0, Qt::NoModifier, QString(ch));
    sendKey(window, Qt::Key_Return);
    // (Every page view has its dialog: the one that opened.)
    QObject *confirm = nullptr;
    waitFor([&] {
        for (QObject *d : window->findChildren<QObject *>(u"pageRenameDialog"_s)) {
            if (d->property("opened").toBool())
                confirm = d;
        }
        return confirm != nullptr;
    });
    if (confirm)
        std::fprintf(stderr, "selftest: title rename asks: %s -> %s, %d link(s), %d tag(s)\n",
                     qPrintable(confirm->property("path").toString()),
                     qPrintable(confirm->property("newName").toString()), confirm->property("links").toInt(),
                     confirm->property("tags").toInt());
    check(confirm && confirm->property("newName").toString() == u"TCP IP" && confirm->property("links").toInt() > 0,
          "renaming a linked page asks first, naming the title it gets");
    if (confirm && confirm->property("opened").toBool())
        QMetaObject::invokeMethod(confirm, "tryAccept");
    check(waitFor([&] { return ws->exists(u"Projekty/TCP IP.md"_s); }) && !ws->exists(u"TCP/IP.md"_s)
                  && !ws->exists(remont),
          "a “/” in the page header's title keeps the page in its folder");
    restore();
    check(ws->exists(remont), "the renamed page gets its name back");
    closeDrawer();

    // ---- A new page (Ctrl+N) from a page at the notebook root is offered the root, as a chip.
    app->openPage(recipes);
    settle(200);
    app->requestUiAction(u"newPage"_s);
    QObject *newPageDialog = window->findChild<QObject *>(u"shellNewPage"_s);
    check(newPageDialog && waitFor([&] { return newPageDialog->property("opened").toBool(); })
                  && newPageDialog->property("folderChosen").toBool()
                  && newPageDialog->property("folder").toString().isEmpty(),
          "a new page from a page at the root is offered the root (“W głównym folderze notatnika ×”)");
    if (newPageDialog) {
        QMetaObject::invokeMethod(newPageDialog, "close");
        waitFor([&] { return !newPageDialog->property("visible").toBool(); });
    }

    // ---- All pages as a folder browser; the breadcrumb goes back.
    app->navigate({{u"view"_s, u"allPages"_s}});
    QQuickItem *folderRow = nullptr;
    waitFor([&] {
        folderRow = findShown(window->contentItem(), [](QQuickItem *item) {
            return item->objectName() == u"allPagesFolderRow" && item->property("text").toString() == u"Projekty";
        });
        return folderRow != nullptr;
    });
    check(folderRow != nullptr, "All pages lists the notebook's folders as rows");
    tap(folderRow);
    check(waitFor([&] { return app->currentRoute().value(u"path"_s).toString() == u"Projekty"; }),
          "a folder row opens the folder in All pages");
    const QString allPages = QCoreApplication::translate("AppShell", "All pages");
    QQuickItem *bar = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "ShellTopBar"); });
    QQuickItem *crumb = bar ? findShown(bar,
                                        [&](QQuickItem *item) {
                                            return item->inherits("QQuickAbstractButton")
                                                   && item->property("text").toString() == allPages;
                                        })
                            : nullptr;
    check(crumb != nullptr, "the top bar shows a breadcrumb for a folder in All pages");
    tap(crumb);
    check(waitFor([&] {
              return app->currentView() == u"allPages" && app->currentRoute().value(u"path"_s).toString().isEmpty();
          }),
          "the breadcrumb goes back to all pages");
    restore();
    app->goHome();
    settle(200);
}

/// 5k. The path parts of a link to a page in a folder (docs/SYNTAX.md §2 "Path parts"), with real
///     input: in [[Projekty/Remont mieszkania]] a click or tap on "Projekty" (a folder of the
///     sample notebook without a page of that name) opens the folder's pages (All pages with its
///     path, the breadcrumb "Projekty"), on "Remont mieszkania" the page; the part's menu (a
///     right-click, a long press) offers "Show folder", and on the desktop its "Open in side
///     panel" shows the folder in the side panel. The note's text is never changed.
void checkLinkParts(QQuickWindow *window, AppController *app, const Check &check,
                    const std::function<QObject *(const QString &)> &controllerFor)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const QString path = u"Linki/Części ścieżki.md"_s;
    const QString content = u"- Zapytać się [[Projekty/Remont mieszkania]] o plan\n"_s;
    ws->write(path, content);
    const auto open = [&]() -> QQuickItem * {
        app->openPage(path);
        QQuickItem *text = nullptr;
        waitFor([&] {
            QObject *page = controllerFor(path);
            text = page && page->property("loaded").toBool() && app->currentRoute().value(u"path"_s).toString() == path
                           ? renderedTextContaining(window->contentItem(), u"part:1:Projekty"_s)
                           : nullptr;
            return text != nullptr;
        });
        settle(200);
        return text;
    };
    const auto routeIs = [app](const QString &view, const QString &routePath) {
        return waitFor([&] {
            return app->currentRoute().value(u"view"_s).toString() == view
                   && app->currentRoute().value(u"path"_s).toString() == routePath;
        });
    };
    QQuickItem *text = open();
    check(text != nullptr, "[[Projekty/Remont mieszkania]] is drawn with its path part as a link");
    if (!text)
        return;
    // "Projekty": the folder's pages.
    tapOrClick(window, linkPoint(text, u"part:1:Projekty"_s), touch);
    check(routeIs(u"allPages"_s, u"Projekty"_s), "a click or tap on “Projekty” opens the folder's pages");
    check(waitFor([&] {
              return findShown(window->contentItem(),
                               [](QQuickItem *item) {
                                   return item->property("text").toString() == u"Remont mieszkania"
                                          && item->isVisible();
                               })
                     != nullptr;
          }),
          "the folder's view lists its page");
    app->back();
    text = open();
    // "Remont mieszkania": the page, as before.
    if (text)
        tapOrClick(window, linkPoint(text, u"page:Projekty%2FRemont"_s), touch);
    check(text && routeIs(u"page"_s, u"Projekty/Remont mieszkania.md"_s),
          "a click or tap on “Remont mieszkania” opens the page");
    app->back();
    text = open();
    // The part's menu.
    if (text) {
        const QPointF at = linkPoint(text, u"part:1:Projekty"_s);
        if (touch)
            touchTap(window, at, 1000);
        else
            mouseClick(window, at, Qt::RightButton);
    }
    const auto menuItem = [window](const QString &name) {
        return findShown(window->contentItem(), [&](QQuickItem *item) { return item->objectName() == name; });
    };
    check(text && waitFor([&] { return openMenus(window) == 1 && menuItem(u"linkMenuOpen"_s) != nullptr; }),
          touch ? "a long press on “Projekty” opens its menu" : "a right-click on “Projekty” opens its menu");
    QQuickItem *openItem = menuItem(u"linkMenuOpen"_s);
    check(openItem && openItem->property("text").toString() == QCoreApplication::translate("LinkMenu", "Show folder"),
          "the folder's menu says “Pokaż folder”");
    QQuickItem *side = menuItem(u"linkMenuOpenSide"_s);
    if (!touch && app->sidePanelAllowed() && side) {
        tapOrClick(window, pointIn(side), false);
        check(waitFor([&] {
                  return app->sidePanelRoute().value(u"view"_s).toString() == u"allPages"
                         && app->sidePanelRoute().value(u"path"_s).toString() == u"Projekty" && app->sidePanelVisible();
              }),
              "its “Otwórz w panelu bocznym” shows the folder's pages in the side panel");
        app->clearSidePanelRoute(); // back to the page's own panel (the later steps use it)
        app->setSidePanelVisible(false);
    }
    closeMenus(window);
    settle(200);
    check(ws->readFile(path) == content, "the note with the link is unchanged");
    app->goHome();
    settle(200);
}

/// 5g. Links to blocks and headings (docs/SYNTAX.md §2 "Copied links"), with real input: a link
///     to a block of this page whose block was moved to another page opens that page with the
///     block selected; a heading the page lacks is said (followed from the text and as a route),
///     its name as plain text in the toast even when written with tags;
///     the palette's "Copy block reference" copies the edited block's ((id)) — read back from
///     the clipboard — and says so; on the desktop a right-click on a paragraph's text opens the
///     block menu.
void checkBlockLinks(QQuickWindow *window, AppController *app, const Check &check,
                     const std::function<QObject *(const QString &)> &controllerFor)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const QString oldPage = u"Linki/Stara strona.md"_s;
    const QString newPage = u"Linki/Nowa strona.md"_s;
    ws->write(newPage, u"- Wstęp\n- Przeniesiony blok z łazienką ^przen1\n"_s);
    ws->write(oldPage, u"# Stara strona\n\nZobacz [[Stara strona#^przen1]] i [[Stara strona#Brakujący nagłówek]].\n\n"
                       u"Akapit do skopiowania.\n\nZnaczniki: [[#<b>Pogrubiony</b> nagłówek]].\n"_s);
    QString lastToast;
    const QMetaObject::Connection toasts = QObject::connect(app, &AppController::toastRequested, app,
                                                            [&lastToast](const QString &text) { lastToast = text; });
    const auto loaded = [&](const QString &path) -> QObject * {
        QObject *page = nullptr;
        waitFor([&] {
            page = controllerFor(path);
            return page && page->property("loaded").toBool() && app->currentRoute().value(u"path"_s).toString() == path;
        });
        return page;
    };
    app->openPage(oldPage);
    QObject *page = loaded(oldPage);
    check(page != nullptr, "the page with links opens");
    if (!page)
        return;
    settle(300);
    // [[Stara strona#^przen1]]: the block lives on another page now.
    QQuickItem *text = renderedTextContaining(window->contentItem(), u"#^przen1"_s);
    const QPointF at = text ? linkPoint(text, u"#^przen1"_s) : QPointF(-1, -1);
    check(at.x() >= 0, "the page shows its link to the moved block");
    if (at.x() >= 0) {
        tapOrClick(window, at, touch);
        QObject *target = loaded(newPage);
        check(target && waitFor([&] { return target->property("selectedRow").toInt() == 1; }),
              "a link to a block moved to another page opens that page, the block selected");
        app->back();
        page = loaded(oldPage);
        settle(300);
    }
    // [[Stara strona#Brakujący nagłówek]]: said, not opened.
    const QString missing =
            QCoreApplication::translate("BlockEditor", "Heading “%1” is not on this page").arg(u"Brakujący nagłówek"_s);
    text = page ? renderedTextContaining(window->contentItem(), u"Brakuj"_s) : nullptr;
    const QPointF headingAt = text ? linkPoint(text, u"#Brakuj"_s) : QPointF(-1, -1);
    check(headingAt.x() >= 0, "the page shows its link to a missing heading");
    if (headingAt.x() >= 0) {
        lastToast.clear();
        tapOrClick(window, headingAt, touch);
        check(waitFor([&] { return lastToast == missing; })
                      && app->currentRoute().value(u"path"_s).toString() == oldPage,
              "a link to a heading the page lacks says so and stays");
    }
    // A heading named with markup: the toast quotes it as text (BToast shows plain text — note text
    // must never be rendered as markup, nor load images).
    text = page ? renderedTextContaining(window->contentItem(), u"Pogrubiony"_s) : nullptr;
    const QPointF markupAt = text ? linkPoint(text, u"Pogrubiony"_s) : QPointF(-1, -1);
    check(markupAt.x() >= 0, "the page shows its link to a heading written with markup");
    if (markupAt.x() >= 0) {
        lastToast.clear();
        tapOrClick(window, markupAt, touch);
        const QString quoted = QCoreApplication::translate("BlockEditor", "Heading “%1” is not on this page")
                                       .arg(u"<b>Pogrubiony</b> nagłówek"_s);
        QQuickItem *message = nullptr;
        check(waitFor([&] {
                  message = findShown(window->contentItem(), [&](QQuickItem *item) {
                      const QQuickItem *toast = item->parentItem() ? item->parentItem()->parentItem() : nullptr;
                      return toast && isA(toast, "BToast_") && item->property("text").toString() == quoted
                             && item->metaObject()->indexOfProperty("textFormat") >= 0;
                  });
                  return lastToast == quoted && message != nullptr;
              }) && message->property("textFormat").toInt() == Qt::PlainText,
              "a toast quoting note text shows it as plain text, tags and all");
    }
    lastToast.clear();
    app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, newPage}, {u"heading"_s, u"Brakujący nagłówek"_s}});
    check(waitFor([&] { return lastToast == missing; }), "a route to a missing heading opens the page and says so");
    app->back();
    page = loaded(oldPage);
    if (!page)
        return;
    // The palette's "Copy block reference" on the edited block.
    int row = -1;
    QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(row), 4);
    QMetaObject::invokeMethod(page, "startEditing", row, -1);
    check(waitFor([&] { return page->property("editingRow").toInt() == row; }), "the paragraph is edited");
    if (QClipboard *clipboard = QGuiApplication::clipboard())
        clipboard->clear();
    lastToast.clear();
    check(app->commands()->run(u"block.copyRef"_s), "the palette runs “Copy block reference”");
    const QString copied = QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString();
    static const QRegularExpression reference(u"^\\(\\(([a-z0-9]{6})\\)\\)$"_s);
    const QRegularExpressionMatch m = reference.match(copied);
    check(m.hasMatch() && page->property("sourceText").toString().contains(u"skopiowania. ^"_s + m.captured(1)),
          "the edited block's ((id)) is on the clipboard and the id in the page");
    check(lastToast == QCoreApplication::translate("BlockEditor", "Copied %1").arg(copied), "a toast says so");
    QMetaObject::invokeMethod(page, "stopEditing");
    // Desktop: a right-click on a paragraph's text opens the block menu; the side panel's outline
    // copies a heading's link from its menu.
    if (!touch) {
        if (QQuickItem *paragraph = renderedTextContaining(window->contentItem(), u"Akapit do skopiowania"_s)) {
            mouseClick(window, pointIn(paragraph, 0.2, 0.5), Qt::RightButton);
            check(waitFor([&] { return openMenus(window) == 1; }), "a right-click on a paragraph opens the block menu");
            closeMenus(window);
        } else {
            check(false, "the paragraph is shown");
        }
        app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, oldPage}, {u"open"_s, u"outline"_s}});
        QQuickItem *heading = nullptr;
        check(waitFor([&] {
                  QQuickItem *outline = findShown(window->contentItem(),
                                                  [](QQuickItem *item) { return isA(item, "ContentOutline"); });
                  heading = findShown(outline, [](QQuickItem *item) {
                      return isA(item, "BListItem") && item->property("text").toString() == u"Stara strona";
                  });
                  return heading != nullptr;
              }),
              "the side panel's outline lists the page's heading");
        if (heading) {
            mouseClick(window, pointIn(heading), Qt::RightButton);
            QQuickItem *copyHeading = nullptr;
            check(waitFor([&] {
                      copyHeading = findShown(window->contentItem(), [](QQuickItem *item) {
                          return item->objectName() == u"outlineCopyHeadingLink";
                      });
                      return copyHeading != nullptr;
                  }),
                  "a right-click on an outline heading opens its menu");
            if (copyHeading) {
                mouseClick(window, pointIn(copyHeading));
                check(waitFor([] {
                          return QGuiApplication::clipboard()
                                 && QGuiApplication::clipboard()->text() == u"[[Stara strona#Stara strona]]";
                      }),
                      "“Copy link to heading” in the outline copies [[Page#Heading]]");
            }
            closeMenus(window);
        }
        app->setSidePanelVisible(false);
    }
    // The journal stream: the palette copies from the day being written in (today); its page link
    // writes nothing into the day.
    app->goHome();
    const QString today = ws->journalPath(app->today(0));
    QObject *day = nullptr;
    std::function<QQuickItem *(QQuickItem *)> dayEditorIn = [&](QQuickItem *item) -> QQuickItem * {
        const auto *c =
                item->objectName() == u"journalDayEditor" ? item->property("controller").value<QObject *>() : nullptr;
        if (c && c->property("path").toString() == today && c->property("loaded").toBool())
            return item;
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = dayEditorIn(child))
                return found;
        }
        return nullptr;
    };
    waitFor([&] {
        QQuickItem *dayEditor = dayEditorIn(window->contentItem());
        day = dayEditor ? dayEditor->property("controller").value<QObject *>() : nullptr;
        return day != nullptr;
    });
    check(day != nullptr, "today's journal day is shown");
    if (day) {
        const QString before = ws->readFile(today);
        QMetaObject::invokeMethod(day, "startEditing", 0, -1);
        waitFor([&] { return day->property("editingRow").toInt() == 0; });
        if (QClipboard *clipboard = QGuiApplication::clipboard())
            clipboard->clear();
        check(app->commands()->run(u"page.copyLink"_s) && waitFor([&] {
                  return QGuiApplication::clipboard()
                         && QGuiApplication::clipboard()->text() == u"[["_s + paths::pageTitle(today) + u"]]"_s;
              }),
              "in the journal the palette's “Copy link to page” copies the day's link by its file name");
        QMetaObject::invokeMethod(day, "stopEditing");
        bool saved = false;
        QMetaObject::invokeMethod(day, "save", qReturnArg(saved));
        check(ws->readFile(today) == before, "copying the day's link writes nothing");
    }
    QObject::disconnect(toasts);
}

/// 5l. Link previews (docs/UI.md "Link previews") and rounded tags outside the editor's blocks:
/// the mouse pointer resting on a [[link]] opens the page's preview after a moment — rendered
/// text with rounded chips and tags —, it stays while the pointer is on it and closes once the
/// pointer left both; a page not written yet says so; a click on the header opens the page, a
/// click on a link inside follows it. The linked references under a page show tags as rounded
/// shapes too and cut a long block after 12 lines with "…". Desktop only: touch screens have no
/// hover (no previews there).
void checkLinkPreviews(QQuickWindow *window, AppController *app, const Check &check,
                       const std::function<QObject *(const QString &)> &controllerFor)
{
    if (QCoreApplication::arguments().contains(u"--touch"_s))
        return;
    Workspace *ws = app->workspace();
    const QString source = u"Podgląd źródło.md"_s;
    const QString target = u"Podgląd cel.md"_s;
    const QString third = u"Podgląd trzeci.md"_s;
    QString longText;
    for (int i = 0; i < 40; ++i)
        longText += u"Bardzo długi akapit o łące, źdźble trawy i żółwiu %1, "_s.arg(i);
    ws->write(third, u"- Trzecia strona\n"_s);
    ws->write(target,
              u"- Pierwszy blok o wyjeździe #podróże 📅 2026-10-20\n- Drugi blok prowadzi do [[Podgląd trzeci]]\n"_s);
    ws->write(source, u"- Zobacz [[Podgląd cel]] oraz [[Podgląd nowej strony]] #krótkie\n- "_s + longText
                              + u"[[Podgląd cel]] #długie\n"_s);
    const auto opened = [&](const QString &path) {
        return waitFor([&] {
            QObject *page = controllerFor(path);
            return page && page->property("loaded").toBool() && app->currentRoute().value(u"path"_s).toString() == path;
        });
    };
    const auto header = [window] { return shownNamed(window->contentItem(), u"linkPreviewHeader"_s); };
    const auto headerTitle = [window]() -> QString {
        QQuickItem *title = shownNamed(window->contentItem(), u"linkPreviewTitle"_s);
        return title ? title->property("text").toString() : QString();
    };
    app->openPage(source);
    check(opened(source), "the page with links opens");
    settle(300);
    QQuickItem *text = renderedTextContaining(window->contentItem(), u"nowej"_s);
    const QPointF at = text ? linkPoint(text, u"cel"_s) : QPointF(-1, -1);
    const QPointF missingAt = text ? linkPoint(text, u"nowej"_s) : QPointF(-1, -1);
    check(at.x() >= 0 && missingAt.x() >= 0, "the page shows its links");
    if (at.x() < 0 || missingAt.x() < 0)
        return;
    const QPointF away = pointIn(text, 0.02, 0.5); // "Zobacz": text, no link
    mouseMove(window, away);
    mouseMove(window, at);
    settle(150);
    check(header() == nullptr, "no preview at once: the pointer only passes");
    check(waitFor([&] { return header() != nullptr; }), "the pointer resting on a [[link]] opens the page's preview");
    check(waitFor([&] { return headerTitle() == u"Podgląd cel"; }), "its header names the page");
    QQuickItem *rounded = nullptr;
    check(waitFor([&] {
              QQuickItem *body = shownNamed(window->contentItem(), u"linkPreviewBody"_s);
              rounded = body ? findShown(body,
                                         [](QQuickItem *item) {
                                             return isA(item, "BRichText") && item->property("rich").toBool();
                                         })
                             : nullptr;
              return rounded != nullptr && renderedTextContaining(body, u"wyjeździe"_s) != nullptr;
          }),
          "the preview shows the page's text, its tag and date chip as rounded shapes");
    // The pointer moves onto the preview: it stays; it leaves both: the preview closes.
    if (QQuickItem *h = header())
        mouseMove(window, pointIn(h, 0.3, 0.5));
    settle(900);
    check(header() != nullptr, "the preview stays while the pointer is on it");
    mouseMove(window, away);
    check(waitFor([&] { return header() == nullptr; }), "the preview closes once the pointer left the link and it");
    // A page not written yet.
    mouseMove(window, missingAt);
    const QString notYet =
            QCoreApplication::translate("BLinkPreview", "“%1” does not exist yet. Click the title to create it.")
                    .arg(u"Podgląd nowej strony"_s);
    check(waitFor([&] {
              QQuickItem *message = shownNamed(window->contentItem(), u"linkPreviewMessage"_s);
              return message && message->property("text").toString() == notYet;
          }),
          "the preview of a link to a page not written yet says so");
    mouseMove(window, away);
    check(waitFor([&] { return header() == nullptr; }), "and closes");
    // A click on a link inside the preview follows it.
    mouseMove(window, at);
    check(waitFor([&] { return header() != nullptr; }), "the preview opens again");
    QQuickItem *inner = header() ? renderedTextContaining(header()->parentItem(), u"trzeci"_s) : nullptr;
    const QPointF innerAt = inner ? linkPoint(inner, u"trzeci"_s) : QPointF(-1, -1);
    check(innerAt.x() >= 0, "the preview shows the page's own link");
    if (innerAt.x() >= 0) {
        mouseMove(window, innerAt);
        mouseClick(window, innerAt);
        check(opened(third), "a click on a link in the preview follows it");
        check(waitFor([&] { return header() == nullptr; }), "and the preview closes");
        app->back();
        check(opened(source), "back on the page with links");
        settle(300);
    }
    // A click on the header opens the previewed page.
    text = renderedTextContaining(window->contentItem(), u"nowej"_s);
    const QPointF again = text ? linkPoint(text, u"cel"_s) : QPointF(-1, -1);
    mouseMove(window, away);
    waitFor([&] { return header() == nullptr; });
    mouseMove(window, again);
    check(waitFor([&] { return header() != nullptr; }), "the preview opens once more");
    if (QQuickItem *h = header()) {
        const QPointF on = pointIn(h, 0.3, 0.5);
        mouseMove(window, on);
        mouseClick(window, on);
        check(opened(target), "a click on the preview's header opens the page");
        check(waitFor([&] { return header() == nullptr; }), "and the preview closes");
    }
    // The page's linked references: rounded tags, a long block cut after 12 lines with "…".
    QQuickItem *cut = nullptr;
    check(waitFor([&] {
              cut = findShown(window->contentItem(), [](QQuickItem *item) {
                  return isA(item, "BRichText") && hasAncestor(item, "ContentRefRow")
                         && item->property("truncated").toBool();
              });
              return cut != nullptr;
          }),
          "a long linked reference is cut");
    if (cut) {
        const qreal line = cut->property("lineHeight").toReal();
        check(line > 0 && std::abs(cut->height() - 12 * line) < 1, "after 12 lines");
    }
    QQuickItem *tagged = findShown(window->contentItem(), [](QQuickItem *item) {
        return isA(item, "BRichText") && hasAncestor(item, "ContentRefRow") && item->property("tinted").toBool()
               && !item->property("truncated").toBool();
    });
    QQuickItem *edit =
            tagged ? findShown(tagged, [](QQuickItem *item) { return isA(item, "QQuickTextEdit"); }) : nullptr;
    QQuickItem *pill = edit ? findShown(edit,
                                        [](QQuickItem *item) {
                                            return isA(item, "QQuickRectangle")
                                                   && item->property("radius").toReal() * 2 >= item->height() - 0.5;
                                        })
                            : nullptr;
    check(pill != nullptr, "a linked reference's #tag is a rounded pill");
    mouseMove(window, QPointF(2, window->height() - 2));
}

/// 5b. A picture attachment opens as a picture (ContentPictureView): its bytes are not a note and
/// never reach the editor — the older view read them as text there, mojibake for a binary picture,
/// and a typed character was saved over it. "Show as text" (a real tap / click) and Ctrl+E
/// show the file's text, read-only, and back; the notes that embed the picture are listed under it
/// and in the side panel; fitting never enlarges a small picture; a file name is never markup; a
/// format the device cannot decode is named.
void checkPictureView(QQuickWindow *window, AppController *app, const Check &check,
                      const std::function<QObject *(const QString &)> &controllerFor)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *workspace = app->workspace();
    const auto pictureView = [window]() -> QQuickItem * {
        for (QQuickItem *v : window->findChildren<QQuickItem *>(u"pictureView"_s)) {
            if (v->isVisible())
                return v;
        }
        return nullptr;
    };
    // The shown one: a Loader's earlier items are only deleted once this test returns to the
    // event loop (they stay, hidden, among the view's children until then).
    const auto child = [](QQuickItem *view, const char *name) -> QQuickItem * {
        if (!view)
            return nullptr;
        const QList<QQuickItem *> found = view->findChildren<QQuickItem *>(QString::fromLatin1(name));
        for (QQuickItem *item : found) {
            if (item->isVisible())
                return item;
        }
        return found.value(0);
    };
    const auto opened = [&](const QString &path) {
        return app->currentRoute().value(u"path"_s).toString() == path && pictureView()
               && pictureView()->property("path").toString() == path;
    };
    const auto textShown = [&] {
        QQuickItem *text = child(pictureView(), "pictureText");
        return text && text->isVisible();
    };
    const auto pictureShown = [&] {
        QQuickItem *view = pictureView();
        return view && !textShown() && view->property("pictureLoaded").toBool();
    };

    const QString picture = u"assets/plan-mieszkania.svg"_s;
    const QString bytes = workspace->readFile(picture);
    app->openPage(picture);
    check(waitFor([&] { return opened(picture) && pictureShown(); }), "a picture opens as a picture, loaded and shown");
    check(controllerFor(picture) == nullptr, "the picture's bytes are not loaded into the block editor");
    QQuickItem *view = pictureView();
    if (!view) {
        check(false, "the page view has a picture view");
        return;
    }

    // The two notes of the sample notebook that embed it, under the picture (scrolled to).
    QQuickItem *refs = child(view, "pictureReferences");
    check(refs && waitFor([&] { return refs->isVisible() && refs->property("linkedCount").toInt() >= 2; }),
          "the notes that embed the picture are listed under it");

    // The bar fits the window: next to the size toggle (an icon on phones, named for screen
    // readers and in its tooltip) the file name keeps its room — the toggle's "Zoom to actual
    // size" left it none on a phone — and nothing runs off the window's edge. The toggle works.
    {
        QQuickItem *name = child(view, "pictureName");
        QQuickItem *size = child(view, "pictureSizeButton");
        QQuickItem *textButton = child(view, "pictureTextButton");
        const auto left = [](QQuickItem *item) { return item->mapToScene(QPointF(0, 0)).x(); };
        const auto right = [](QQuickItem *item) { return item->mapToScene(QPointF(item->width(), 0)).x(); };
        const auto nameOf = [](QQuickItem *button) {
            const QVariant label = button->property("label");
            return label.isValid() ? label.toString() : button->property("text").toString();
        };
        if (name && size && textButton && waitFor([&] { return size->isVisible(); })) {
            // (Once the row's layout has settled: the name may widen before the toggle moves.)
            const bool fits = waitFor([&] {
                return name->width() >= qMin<qreal>(name->property("implicitWidth").toReal(), 80)
                       && right(name) <= left(size) + 0.5 && right(size) <= left(textButton) + 0.5
                       && right(textButton) <= window->width() + 0.5;
            });
            std::fprintf(stderr,
                         "selftest: picture bar: name %.0f of %.0f px, size toggle %.0f–%.0f, text button %.0f–%.0f\n",
                         name->width(), name->property("implicitWidth").toReal(), left(size), right(size),
                         left(textButton), right(textButton));
            check(fits && nameOf(size) == QCoreApplication::translate("ContentPictureView", "Zoom to actual size"),
                  "the picture's bar shows the file name next to the size toggle and fits the window");
            // (The mouse still rests where an earlier step clicked the note mode toggle, hidden
            // since: its tooltip waits for a deletion this running test does not let happen —
            // see closeToolTipsWaitingForDeletion — and would take the click.)
            check(closeToolTipsWaitingForDeletion(window).isEmpty(), "no live tooltip outlives its hidden button");
            tapOrClick(window, pointIn(size), touch);
            check(waitFor([&] { return view->property("actualSize").toBool(); })
                          && nameOf(size) == QCoreApplication::translate("ContentPictureView", "Fit to view"),
                  "the size toggle shows the picture's own pixels");
            tapOrClick(window, pointIn(size), touch);
            check(waitFor([&] { return !view->property("actualSize").toBool(); }), "and fits it into the view again");
        } else {
            check(false, "the picture's bar has its name, its size toggle and its text button");
        }
    }

    // The top bar's More menu on a picture: no "Toggle note mode" (a picture is no note), and its
    // "Markdown source" item says what it does there, like the picture's bar — and does it.
    {
        const auto moreMenuItem = [&](const QString &text) {
            return findShown(window->contentItem(), [&](QQuickItem *item) {
                return isA(item, "BMenuItem") && item->property("text").toString() == text;
            });
        };
        const auto openMoreMenu = [&] {
            const QString more = QCoreApplication::translate("ShellTopBar", "More");
            QQuickItem *button = findShown(window->contentItem(), [&](QQuickItem *item) {
                return isA(item, "BIconButton") && item->property("label").toString() == more;
            });
            if (button)
                tapOrClick(window, pointIn(button), touch);
            return button && waitFor([&] { return openMenus(window) == 1; });
        };
        const QString noteMode = QCoreApplication::translate("ShellTopBar", "Toggle note mode");
        const QString source = QCoreApplication::translate("ShellTopBar", "Markdown source");
        for (const bool asText : {false, true}) {
            const QString wanted = QCoreApplication::translate("ShellTopBar", asText ? "Show picture" : "Show as text");
            QQuickItem *item = nullptr;
            check(openMoreMenu() && waitFor([&] { return (item = moreMenuItem(wanted)) != nullptr; })
                          && !moreMenuItem(noteMode) && !moreMenuItem(source),
                  asText ? "with a picture's text shown the More menu offers “Show picture”, not the note mode"
                         : "on a picture the More menu offers “Show as text”, not the note mode");
            if (item)
                tapOrClick(window, pointIn(item), touch);
            check(waitFor([&] { return openMenus(window) == 0 && (asText ? pictureShown() : textShown()); }),
                  asText ? "the More menu's “Show picture” shows the picture again"
                         : "the More menu's “Show as text” shows the picture's text");
            closeMenus(window);
        }
    }

    // "Show as text" in the picture's bar: the file's text, read-only; the editor stays
    // unloaded.
    if (QQuickItem *button = child(view, "pictureTextButton")) {
        tapOrClick(window, pointIn(button), touch);
        check(waitFor(textShown) && controllerFor(picture) == nullptr,
              "“Pokaż jako tekst” shows the file's text without loading it into the editor");
    } else {
        check(false, "the picture view has its text button");
    }
    QQuickItem *textEdit = child(view, "pictureTextEdit");
    check(textEdit && textEdit->property("readOnly").toBool()
                  && textEdit->property("text").toString().contains(u"<svg"_s),
          "the text view shows the file's text, read-only");
    if (textEdit) {
        // Keys typed there change nothing: a picture's bytes are never rewritten as text.
        tapOrClick(window, pointIn(textEdit, 0.2, 0.1), touch);
        textEdit->forceActiveFocus();
        sendKey(window, 0, Qt::NoModifier, u"Ż"_s);
        sendKey(window, Qt::Key_Backspace);
        settle(300);
        check(textEdit->property("text").toString().startsWith(bytes.left(200)) && workspace->readFile(picture) == bytes
                      && controllerFor(picture) == nullptr,
              "typing in the text view changes neither the view nor the file");
    }

    // Ctrl+E (the same branch as the "Markdown source" command) switches back and forth.
    app->requestUiAction(u"toggleSourceMode"_s);
    check(waitFor(pictureShown) && controllerFor(picture) == nullptr,
          "Ctrl+E in the text view shows the picture again");
    app->requestUiAction(u"toggleSourceMode"_s);
    check(waitFor(textShown) && controllerFor(picture) == nullptr, "Ctrl+E on the picture shows its text");
    app->requestUiAction(u"toggleSourceMode"_s);
    check(waitFor(pictureShown), "and Ctrl+E once more the picture");

    // A long text view scrolls: with a finger on phones (a TextEdit that selects by dragging
    // kept the finger, the text never moved), with the wheel on desktops; nothing gets selected.
    {
        QString svg = u"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\">\n"_s;
        for (int i = 0; i < 300; ++i)
            svg += u"  <rect x=\"%1\" y=\"1\" width=\"1\" height=\"1\"/> <!-- żółw %1 -->\n"_s.arg(i);
        svg += u"</svg>\n"_s;
        const QString longSvg = u"assets/długi szkic.svg"_s;
        check(workspace->writeFile(longSvg, svg), "a long SVG is added to the notebook");
        app->openPage(longSvg);
        check(waitFor([&] { return opened(longSvg) && pictureShown(); }), "the long SVG opens");
        app->requestUiAction(u"toggleSourceMode"_s);
        QQuickItem *flick = nullptr;
        QQuickItem *edit = nullptr;
        check(waitFor([&] {
                  flick = textShown() ? child(pictureView(), "pictureText") : nullptr;
                  edit = flick ? child(pictureView(), "pictureTextEdit") : nullptr;
                  return edit && flick->property("contentHeight").toReal() > 2 * flick->height();
              }),
              "the long SVG's text view is longer than the window");
        if (flick && edit) {
            const QPointF at = flick->mapToScene(QPointF(flick->width() / 2, flick->height() * 0.7));
            if (touch) {
                touchDrag(window, at, QPointF(0, -300));
            } else {
                QWheelEvent wheel(at, window->mapToGlobal(at), QPoint(), QPoint(0, -360), Qt::NoButton, Qt::NoModifier,
                                  Qt::NoScrollPhase, false);
                QCoreApplication::sendEvent(window, &wheel);
            }
            check(waitFor([&] { return flick->property("contentY").toReal() > 100; })
                          && edit->property("selectedText").toString().isEmpty(),
                  touch ? "a finger scrolls the text view and selects nothing" : "the wheel scrolls the text view");
        }

        // The file changes on disk while its text is shown (sync, another program): the text view
        // shows the new text (it read the file once).
        const QString changed = u"<!-- poprawione na laptopie: źdźbło -->\n"_s + svg;
        {
            QFile file(workspace->vault()->absolutePath(longSvg));
            check(file.open(QIODevice::WriteOnly) && file.write(changed.toUtf8()) > 0, "the long SVG changes on disk");
        }
        workspace->vault()->rescan();
        check(waitFor([&] {
                  QQuickItem *e = child(pictureView(), "pictureTextEdit");
                  return textShown() && e && e->property("text").toString().startsWith(changed.left(80));
              }),
              "the text view shows a change made on disk while it is open");

        // At most `textLimit` bytes of the file are read and shown, never half a character: a
        // limit inside "ż" (two bytes in UTF-8) stops before it. The note above says how much of
        // how much.
        const QByteArray utf8 = workspace->vault()->readBinary(longSvg);
        const qsizetype zz = utf8.indexOf(QStringView(u"żółw 5 ").toUtf8());
        app->requestUiAction(u"toggleSourceMode"_s);
        waitFor(pictureShown);
        if (QQuickItem *v = pictureView()) {
            const QVariant limitBefore = v->property("textLimit");
            v->setProperty("textLimit", int(zz + 1));
            app->requestUiAction(u"toggleSourceMode"_s);
            QQuickItem *note = nullptr;
            QQuickItem *e = nullptr;
            const QString expected = QString::fromUtf8(utf8.left(zz));
            const QLocale locale;
            const QString noteText =
                    QCoreApplication::translate("ContentPictureView", "The start of the file: %1 of %2.")
                            .arg(locale.formattedDataSize(zz, 1, QLocale::DataSizeTraditionalFormat),
                                 locale.formattedDataSize(utf8.size(), 1, QLocale::DataSizeTraditionalFormat));
            check(waitFor([&] {
                      note = textShown() ? child(pictureView(), "pictureTextNote") : nullptr;
                      e = child(pictureView(), "pictureTextEdit");
                      return note && note->isVisible() && e && e->property("text").toString() == expected;
                  }) && note->property("text").toString() == noteText,
                  "a long file's text view shows its start, cut before a character that does not fit whole");
            if (note)
                std::fprintf(stderr, "selftest: the text view's note: %s\n",
                             qPrintable(note->property("text").toString()));
            app->requestUiAction(u"toggleSourceMode"_s);
            waitFor(pictureShown);
            v->setProperty("textLimit", limitBefore);
        }
    }

    // route.open on a picture: "text" opens its text view, "links" (wide windows) the side panel
    // with the picture's references.
    app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, picture}, {u"open"_s, u"text"_s}});
    check(waitFor([&] { return opened(picture) && textShown(); }), "route.open \"text\" opens a picture's text view");
    if (window->width() >= 900) {
        app->setSidePanelVisible(false);
        app->goHome();
        waitFor([&] { return !pictureView(); });
        app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, picture}, {u"open"_s, u"links"_s}});
        QQuickItem *panel = nullptr;
        check(waitFor([&] {
                  panel = findShown(window->contentItem(),
                                    [](QQuickItem *item) { return isA(item, "ContentSidePanel"); });
                  return app->sidePanelVisible() && panel && panel->property("path").toString() == picture
                         && pictureShown();
              }),
              "route.open \"links\" opens the side panel with the references of the picture shown");
        const auto isRefs = [](QQuickItem *item) { return isA(item, "ContentBacklinks"); };
        check(waitFor([&] {
                  QQuickItem *panelRefs = panel ? findShown(panel, isRefs) : nullptr;
                  return panelRefs && panelRefs->property("linkedCount").toInt() >= 2;
              }),
              "the side panel lists the notes that embed the picture");
        // A narrower window with the side panel open leaves the picture a narrow view: its size
        // toggle is an icon there too (the view's width counts, not the window's), and the file
        // name keeps room next to it.
        const QSize windowSize = window->size();
        window->resize(900, windowSize.height());
        QQuickItem *name = nullptr;
        QQuickItem *size = nullptr;
        check(waitFor([&] {
                  QQuickItem *v = pictureView();
                  name = child(v, "pictureName");
                  size = child(v, "pictureSizeButton");
                  return v && v->width() < 600 && pictureShown() && name && size && size->isVisible()
                         && isA(size, "BIconButton");
              }) && name->width() >= qMin<qreal>(name->property("implicitWidth").toReal(), 40)
                      && name->mapToScene(QPointF(name->width(), 0)).x() <= size->mapToScene(QPointF(0, 0)).x() + 0.5,
              "in a narrow view on the desktop the picture's size toggle is an icon, next to the file name");
        if (name && size)
            std::fprintf(stderr, "selftest: a narrow picture view: %.0f px, name %.0f of %.0f px\n",
                         pictureView() ? pictureView()->width() : 0., name->width(),
                         name->property("implicitWidth").toReal());
        window->resize(windowSize);
        waitFor([&] { return pictureView() && pictureView()->width() >= 600; });
        app->setSidePanelVisible(false);
    }

    // A small picture keeps its own pixels: fitting only shrinks (no blurry enlargement, no
    // pointless "actual size" that would make it smaller).
    {
        QImage small(40, 30, QImage::Format_RGB32);
        small.fill(Qt::darkGreen);
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        small.save(&buffer, "PNG");
        const QString icon = u"assets/mała ikona.png"_s;
        check(workspace->vault()->writeBinary(icon, buffer.data()), "a small picture is added to the notebook");
        app->openPage(icon);
        check(waitFor([&] { return opened(icon) && pictureShown(); }), "the small picture opens");
        QQuickItem *image = child(pictureView(), "pictureImage");
        QQuickItem *size = child(pictureView(), "pictureSizeButton");
        check(image && qRound(image->width()) == 40 && qRound(image->height()) == 30 && size && !size->isVisible(),
              "a picture smaller than the view is shown at its own size, without a size toggle");
    }

    // A file name that looks like markup is shown as written (one line, not a line break). Windows
    // allows no "<" or ">" in file names: there entities stand for the markup.
    {
#ifdef Q_OS_WIN
        const QString oddName = u"a&lt;br&gt;b &amp;szkic.svg"_s;
#else
        const QString oddName = u"a<br>b <i>szkic.svg"_s;
#endif
        const QString odd = u"assets/"_s + oddName;
        check(workspace->writeFile(odd, bytes), "a picture with markup in its name is added to the notebook");
        app->openPage(odd);
        check(waitFor([&] { return opened(odd) && pictureShown(); }), "the picture with markup in its name opens");
        QQuickItem *name = child(pictureView(), "pictureName");
        check(name && name->property("lineCount").toInt() == 1
                      && name->property("text").toString() == oddName
                      && name->property("textFormat").toInt() == 0 /* Text.PlainText */,
              "a picture's file name is shown as plain text");
    }

    // A format this device cannot decode is named (WebP and AVIF need Qt's image plugins); one
    // it can is loaded (these bytes are no picture: "Cannot load").
    {
        const QString webp = u"assets/łąka.webp"_s;
        check(workspace->vault()->writeBinary(webp, QByteArray("RIFF\x00\x00\x00\x00WEBPVP8 ", 16)),
              "a WebP picture is added to the notebook");
        app->openPage(webp);
        const bool decodable = workspace->canDecodePicture(webp);
        std::fprintf(stderr, "selftest: this build %s decode WebP\n", decodable ? "can" : "cannot");
        QQuickItem *message = nullptr;
        check(waitFor([&] {
                  message = opened(webp) ? child(pictureView(), "pictureMessage") : nullptr;
                  return message && message->isVisible();
              }) && message->property("text").toString().contains(decodable ? u"łąka.webp"_s : u"WEBP"_s),
              "a picture that cannot be shown says why");

        // Embedded in a note it says so too, with its name (the embed only said "Cannot load
        // łąka.webp").
        const QString note = u"Łąka wiosną.md"_s;
        check(workspace->writeFile(note, u"Zdjęcie z łąki:\n\n![[łąka.webp]]\n"_s), "a note embeds the WebP picture");
        app->openPage(note);
        QQuickItem *embedText = nullptr;
        check(waitFor([&] {
                  QQuickItem *embed =
                          findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "EmbedView"); });
                  embedText =
                          embed ? findShown(embed,
                                            [](QQuickItem *item) {
                                                return item->inherits("QQuickText")
                                                       && item->property("text").toString().contains(u"łąka.webp"_s);
                                            })
                                : nullptr;
                  return embedText != nullptr;
              }) && (decodable || embedText->property("text").toString().contains(u"WEBP"_s)),
              "an embedded picture that cannot be shown says why");
        if (embedText)
            std::fprintf(stderr, "selftest: the embed says: %s\n", qPrintable(embedText->property("text").toString()));
    }

    // Back to the sample picture: the picture, the editor still unloaded.
    app->openPage(picture);
    check(waitFor([&] { return opened(picture) && pictureShown(); }) && controllerFor(picture) == nullptr,
          "the picture comes back and the editor stays unloaded");
}

/// 5k. PDFs (docs/UI.md "PDF viewer"): a note embedding a lecture written here (QPdfWriter, three
/// pages) shows its first page's thumbnail on a white page; a tap / click opens the PDF viewer
/// (route path = the PDF, the block editor holds no controller for it: no bytes loaded), "Page 1 of
/// 3"; Ctrl+G 3 Enter goes to page 3, Home back, PageDown moves; Ctrl++ zooms (the page's picture
/// asked for larger, sideways panning — the drawer's edge strip yields), Ctrl+0 fits the width again;
/// a pinch (phone run) or Ctrl+wheel (desktop) zooms, a double tap fits / doubles; Ctrl+F "gesla"
/// finds "gęślą" once with a highlight; Esc closes the search; a mouse drag over page 1's line (a
/// long press on its first word on the phone) selects it, tinted with "Copy", Ctrl+C copies it, Esc
/// clears it; Esc again returns to the note; the
/// note's link with #page=3 opens the viewer at page 3; "Draw on this page" on page 2 opens the
/// drawing editor on a drawing whose backdrop is page 2, embedded right after the PDF's block, Back
/// returns to the viewer on page 2; again on page 2 the existing drawing is offered and opened (no
/// second file); the More menu's "Open in another app" hands the file over (a recording hook);
/// the PDF moved to another folder while open reopens on the same page; a locked PDF says why
/// (BRULION_SELFTEST_PDF_DIR: tests/data/pdf), refuses a wrong password, opens with the right one and
/// asks again when opened again; spis.pdf's table of contents (poppler) jumps to "Rozdział 3", its
/// link to page 2 jumps, its web link keeps its address and no script is a link, the menu's two
/// pages side by side keeps page 2; a ZIP opens read-only, never in the editor nor handed over; a
/// DOCX opens read-only and "Open in another app" hands it over as a Word document. Builds without
/// a renderer: the chip, its click hands the file over, the viewer's message with its button.
void checkPdfViewer(QQuickWindow *window, AppController *app, const Check &check,
                    const std::function<QObject *(const QString &)> &controllerFor)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *workspace = app->workspace();
    const QString note = u"PDF próba.md"_s;
    QString pdf = u"assets/Wykład – ćwiczenia.pdf"_s;
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p(&writer);
        QFont font;
        font.setPointSize(24);
        p.setFont(font);
        p.drawText(QPointF(60, 120), u"Wykład 3 — Zażółć gęślą jaźń"_s);
        writer.setPageOrientation(QPageLayout::Landscape);
        writer.newPage();
        p.fillRect(QRectF(113, 85, 57, 57), Qt::red);
        writer.setPageOrientation(QPageLayout::Portrait);
        writer.newPage();
        p.drawText(QPointF(60, 120), u"Strona trzecia: żółw"_s);
        p.end();
        pdf = workspace->createAttachment(pdf, buffer.data());
    }
    check(!pdf.isEmpty()
                  && workspace->writeFile(
                          note,
                          u"- Slajdy ![[Wykład – ćwiczenia.pdf]]\n- Trzecia [[Wykład – ćwiczenia.pdf#page=3]]\n"_s),
          "a note embeds a three-page PDF");
    const QString absolute = workspace->absoluteFilePath(pdf);
    QStringList handed;
    platform::setOpenFileHook([&handed](const QString &file, const QString &mime) {
        handed.append(file + u'|' + mime);
        return platform::OpenResult::Opened;
    });
    const auto restoreHook = qScopeGuard([] { platform::setOpenFileHook({}); });
    const auto pdfView = [window]() { return shownNamed(window->contentItem(), u"pdfView"_s); };
    const auto viewModel = [&]() -> QObject * {
        QQuickItem *view = pdfView();
        return view ? view->property("model").value<QObject *>() : nullptr;
    };
    const auto modelValue = [&](const char *name) {
        QObject *m = viewModel();
        return m ? m->property(name) : QVariant();
    };
    const auto opened = [&](int page) {
        return app->currentRoute().value(u"path"_s).toString() == pdf && pdfView()
               && pdfView()->property("ready").toBool() && (page == 0 || modelValue("currentPage").toInt() == page);
    };

    if (!platform::pdfAvailable()) {
        std::fprintf(stderr, "selftest: this build has no PDF renderer\n");
        app->openPage(note);
        QQuickItem *chip = nullptr;
        check(waitFor([&] { return (chip = shownNamed(window->contentItem(), u"pdfEmbedChip"_s)) != nullptr; }),
              "without a renderer the PDF is embedded as its chip");
        if (chip)
            tapOrClick(window, pointIn(chip), touch);
        check(waitFor([&] { return handed.contains(absolute + u"|application/pdf"_s); })
                      && app->currentRoute().value(u"path"_s).toString() == note,
              "a click on the chip hands the PDF to another app, the note stays");
        app->openPage(pdf);
        check(waitFor([&] { return shownNamed(window->contentItem(), u"pdfOpenElsewhere"_s) != nullptr; })
                      && controllerFor(pdf) == nullptr,
              "the PDF's route says it is opened elsewhere, with its button, never in the editor");
        // Its name, size and the notes embedding it under the message (unless the side panel
        // lists them).
        check(waitFor(
                      [&] {
                          QQuickItem *v = pdfView();
                          QQuickItem *name = v ? shownNamed(v, u"pdfMessageName"_s) : nullptr;
                          return name && name->property("text").toString() == paths::basename(pdf)
                                 && shownNamed(v, u"pdfMessageSize"_s) != nullptr
                                 && (v->property("refsInSidePanel").toBool()
                                     || shownNamed(v, u"pdfMessageReferences"_s) != nullptr);
                      },
                      10000),
              "the panel names the file, its size and the notes embedding it");
        app->goHome();
        return;
    }

    // The thumbnail: drawn, white page with a hairline frame (also in the dark theme).
    app->openPage(note);
    QQuickItem *tile = nullptr;
    check(waitFor(
                  [&] {
                      tile = shownNamed(window->contentItem(), u"pdfEmbedTile"_s);
                      return tile && tile->property("loaded").toBool();
                  },
                  15000),
          "the PDF's thumbnail is drawn in the note");
    if (tile) {
        QQuickItem *frame = shownNamed(tile, u"pdfEmbedFrame"_s);
        const QImage grab = window->grabWindow();
        const QPointF inside =
                frame ? frame->mapToScene(QPointF(frame->width() / 2, frame->height() * 0.8)) : QPointF();
        const qreal dpr = window->effectiveDevicePixelRatio();
        const QColor paper = frame ? grab.pixelColor((inside * dpr).toPoint()) : QColor();
        std::fprintf(stderr, "selftest: PDF thumbnail %.0f × %.0f, paper %s\n", frame ? frame->width() : 0.0,
                     frame ? frame->height() : 0.0, qPrintable(paper.name()));
        check(frame && frame->property("color").value<QColor>() == QColor(Qt::white) && paper.lightness() > 245,
              "the thumbnail is a white page with a frame");
        // Opened by a tap / click on it.
        tapOrClick(window, pointIn(tile, 0.5, 0.3), touch);
    }
    const bool viewerOpened = waitFor([&] { return opened(1); }, 15000);

    std::fprintf(stderr, "selftest: PDF route %s, viewer %d, ready %d, page %d of %d\n",
                 qPrintable(app->currentRoute().value(u"path"_s).toString()), pdfView() != nullptr,
                 pdfView() ? pdfView()->property("ready").toBool() : false, modelValue("currentPage").toInt(),
                 modelValue("pageCount").toInt());
    check(viewerOpened && modelValue("pageCount").toInt() == 3,
          "a tap on the thumbnail opens the PDF viewer at page 1 of 3");
    check(controllerFor(pdf) == nullptr, "the PDF's bytes are not loaded into the block editor");
    QQuickItem *view = pdfView();
    if (!view) {
        check(false, "the page view has a PDF viewer");
        return;
    }
    {
        QQuickItem *pageButton = shownNamed(view, u"pdfPageButton"_s);
        const QString wanted = QCoreApplication::translate("ContentPdfView", "Page %1 of %2").arg(1).arg(3);
        check(pageButton
                      && (pageButton->property("text").toString() == wanted
                          || pageButton->property("text").toString() == u"1/3"_s),
              "the bar says “Page 1 of 3”");
    }
    view->forceActiveFocus();

    // The page jump.
    sendKey(window, Qt::Key_G, Qt::ControlModifier);
    QQuickItem *jumpField = nullptr;
    check(waitFor([&] { return (jumpField = shownNamed(window->contentItem(), u"pdfJumpField"_s)) != nullptr; }),
          "Ctrl+G opens the page jump");
    if (jumpField) {
        jumpField->forceActiveFocus();
        jumpField->setProperty("text", u"3"_s);
        sendKey(window, Qt::Key_Return);
    }
    check(waitFor([&] { return opened(3); }), "the page jump goes to page 3");
    view->forceActiveFocus();
    sendKey(window, Qt::Key_Home);
    check(waitFor([&] { return opened(1); }), "Home goes to the first page");
    QQuickItem *flick = shownNamed(view, u"pdfFlick"_s);
    const qreal top = flick ? flick->property("contentY").toReal() : 0;
    sendKey(window, Qt::Key_PageDown);
    check(flick && waitFor([&] { return flick->property("contentY").toReal() > top + 50; }), "PageDown scrolls down");

    // Zoom: the keys, then a pinch or Ctrl+wheel; the page's picture asked for larger.
    const int fitPercent = modelValue("zoomPercent").toInt();
    const auto pageImageWidth = [&] {
        QQuickItem *image = shownNamed(view, u"pdfPageImage"_s);
        return image ? image->property("sourceSize").toSizeF().width() : 0.0;
    };
    const qreal fitImage = pageImageWidth();
    sendKey(window, Qt::Key_Plus, Qt::ControlModifier);
    sendKey(window, Qt::Key_Plus, Qt::ControlModifier);
    check(waitFor([&] { return modelValue("zoomPercent").toInt() > fitPercent * 1.4; })
                  && view->property("pannedSideways").toBool(),
          "Ctrl++ zooms in, the pages pan sideways");
    check(waitFor([&] { return pageImageWidth() > fitImage; }) || modelValue("detailRevision").toInt() > 0,
          "the page's picture is asked for at the new size");
    sendKey(window, Qt::Key_0, Qt::ControlModifier);
    check(waitFor([&] { return modelValue("fitWidth").toBool() && modelValue("zoomPercent").toInt() == fitPercent; }),
          "Ctrl+0 fits the width again");
    if (flick) {
        const QPointF centre = flick->mapToScene(QPointF(flick->width() / 2, flick->height() / 2));
        if (touch)
            uidriver::run(uidriver::touchPinch(window, centre, 80, 240));
        else
            uidriver::run(uidriver::wheel(window, centre, QPointF(0, -240), Qt::ControlModifier));
        const bool zoomed = waitFor([&] { return modelValue("zoomPercent").toInt() > fitPercent * 1.2; });
        if (!zoomed) {
            QQuickItem *target = uidriver::pressTargetAt(window, centre);
            std::fprintf(stderr, "selftest: PDF zoom gesture: %s; press target %s %s; zoom %d, preview %g\n",
                         qPrintable(uidriver::itemsAt(window, centre)), target ? target->metaObject()->className() : "",
                         target ? qPrintable(target->objectName()) : "", modelValue("zoomPercent").toInt(),
                         view->property("previewScale").toReal());
        }
        check(zoomed, touch ? "a pinch zooms in" : "Ctrl+wheel zooms in");
        uidriver::run(touch ? uidriver::touchDoubleTap(window, centre) : uidriver::mouseDoubleClick(window, centre));
        check(waitFor([&] { return modelValue("fitWidth").toBool(); }), "a double tap fits the width again");
        settle(700); // (past the double-tap interval: two taps of their own)
        uidriver::run(touch ? uidriver::touchDoubleTap(window, centre) : uidriver::mouseDoubleClick(window, centre));
        check(waitFor([&] { return modelValue("zoomPercent").toInt() >= fitPercent * 1.9; }),
              "and another one zooms in twice");
        view->forceActiveFocus();
        sendKey(window, Qt::Key_0, Qt::ControlModifier);
        waitFor([&] { return modelValue("fitWidth").toBool(); });
    }

    // Search without diacritics, with a highlight; Esc closes it, Esc again goes back.
    view->forceActiveFocus();
    sendKey(window, Qt::Key_F, Qt::ControlModifier);
    QQuickItem *searchField = nullptr;
    check(waitFor([&] { return (searchField = shownNamed(view, u"pdfSearchField"_s)) != nullptr; }),
          "Ctrl+F opens the PDF's search");
    for (const QChar ch : u"gesla"_s)
        sendKey(window, 0, Qt::NoModifier, QString(ch));
    check(waitFor([&] { return modelValue("hitCount").toInt() == 1 && !modelValue("searching").toBool(); }, 10000)
                  && waitFor([&] { return shownNamed(view, u"pdfHit"_s) != nullptr; }),
          "“gesla” finds “gęślą” once, highlighted");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !view->property("searchOpen").toBool() && modelValue("hitCount").toInt() == 0; }),
          "Esc closes the search and its highlights");

    // Selecting text on page 1: a mouse drag over its line (a finger: a long press on its first
    // word), Ctrl+C copies it, Esc clears it (and stays in the viewer).
    if (platform::pdfSelectionAvailable()) {
        view->forceActiveFocus();
        sendKey(window, Qt::Key_Home);
        waitFor([&] { return opened(1); });
        QQuickItem *firstPage = findShown(view, [](QQuickItem *item) {
            return item->objectName() == u"pdfPage"_s && item->property("page").toInt() == 1;
        });
        const QRectF box =
                firstPage ? firstPage->mapRectToScene(QRectF(0, 0, firstPage->width(), firstPage->height())) : QRectF();
        const qreal lineY = box.top() + box.height() * 110.0 / 842;
        const QString line = u"Wykład 3 — Zażółć gęślą jaźń"_s;
        const auto selected = [&] { return modelValue("selectedText").toString().simplified(); };
        QString expected;
        if (touch) {
            uidriver::touchHoldNow(
                    window, QPointF(box.left() + box.width() * 75 / 595, lineY),
                    [&] { return selected() == u"Wykład"_s; }, 6000);
            expected = u"Wykład"_s;
            check(selected() == expected, "a long press selects the word under the finger");
        } else {
            const qreal before = flick ? flick->property("contentY").toReal() : 0;
            uidriver::mouseDragNow(window, QPointF(box.left() + 4, lineY), QPointF(box.right() - 4, lineY + 2));
            expected = line;
            if (!waitFor([&] { return selected() == line; }, 5000))
                std::fprintf(stderr, "selftest: PDF drag selected \"%s\"\n", qPrintable(selected()));
            check(waitFor([&] { return selected() == line; }, 5000) && flick
                          && qAbs(flick->property("contentY").toReal() - before) < 1,
                  "a mouse drag over the line selects its text (the pages do not pan)");
        }
        check(waitFor([&] {
                  return shownNamed(view, u"pdfSelection"_s) != nullptr
                         && shownNamed(view, u"pdfCopyButton"_s) != nullptr;
              }),
              "the selection is tinted, with “Copy” over it");
        view->forceActiveFocus();
        sendKey(window, Qt::Key_C, Qt::ControlModifier);
        check(waitFor([&] { return QGuiApplication::clipboard()->text().simplified() == expected; }),
              "Ctrl+C copies the selected text");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return modelValue("selectionPage").toInt() == 0; }) && opened(0),
              "Esc clears the selection and stays in the viewer");
    }
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return app->currentRoute().value(u"path"_s).toString() == note; }),
          "Esc again returns to the note");

    // The note's link to page 3.
    if (QObject *page = controllerFor(note)) {
        waitFor([&] { return page->property("loaded").toBool(); });
        QVariantMap result;
        QMetaObject::invokeMethod(page, "activateLink", qReturnArg(result), 1,
                                  u"page:Wyk%C5%82ad%20%E2%80%93%20%C4%87wiczenia.pdf#page=3"_s, false);
        check(waitFor([&] { return opened(3); }, 15000), "the link with #page=3 opens the viewer at page 3");
    } else {
        check(false, "the note is in the editor");
    }

    // "Draw on this page" on page 2.
    view = pdfView();
    const auto drawOnPage = [&] {
        QQuickItem *v = pdfView();
        if (!v)
            return false;
        if (QQuickItem *button = shownNamed(v, u"pdfDrawButton"_s)) {
            tapOrClick(window, pointIn(button), touch);
            return true;
        }
        QQuickItem *more = shownNamed(v, u"pdfMoreButton"_s);
        if (!more)
            return false;
        tapOrClick(window, pointIn(more), touch);
        QQuickItem *item = nullptr;
        if (!waitFor([&] { return (item = shownNamed(window->contentItem(), u"pdfMenuDraw"_s)) != nullptr; }))
            return false;
        tapOrClick(window, pointIn(item), touch);
        return true;
    };
    QString drawingPath;
    if (view) {
        uidriver::callQml(view, "jumpTo", {QVariant(2)});
        check(waitFor([&] { return opened(2); }), "the viewer goes to page 2");
        check(drawOnPage(), "“Draw on this page” is offered");
        check(waitFor(
                      [&] {
                          drawingPath = app->currentRoute().value(u"view"_s).toString() == u"drawing"
                                                ? app->currentRoute().value(u"path"_s).toString()
                                                : QString();
                          return !drawingPath.isEmpty();
                      },
                      15000),
              "“Draw on this page” opens the drawing editor");
        const std::optional<drawing::Drawing> made = drawing::readDrawing(workspace->readAttachment(drawingPath));
        const std::optional<drawing::BackdropSource> source = made ? drawing::backdropSource(*made) : std::nullopt;
        check(source && source->page == 2 && source->path == pdf, "the drawing's backdrop is page 2 of the PDF");
        const QStringList lines = workspace->readFile(note).split(u'\n');
        check(lines.size() >= 3 && lines.at(0).contains(u"Wykład – ćwiczenia.pdf"_s)
                      && lines.at(1).startsWith(u"- ![["_s) && lines.at(1).contains(paths::basename(drawingPath))
                      && lines.at(2).contains(u"#page=3"_s),
              "the drawing is embedded right after the PDF's block");
        app->back();
        check(waitFor([&] { return opened(2); }, 15000), "Back returns to the viewer on page 2");
        // Again on page 2: the drawing there is offered (no second file).
        const int svgs = int(QDir(workspace->absoluteFilePath(u"assets"_s)).entryList({u"*.svg"_s}).size());
        check(drawOnPage(), "“Draw on this page” again");
        QQuickItem *existing = nullptr;
        check(waitFor([&] { return (existing = shownNamed(window->contentItem(), u"pdfDrawExisting"_s)) != nullptr; }),
              "the drawing of page 2 is offered first");
        if (existing)
            tapOrClick(window, pointIn(existing), touch);
        check(waitFor([&] {
                  return app->currentRoute().value(u"view"_s).toString() == u"drawing"
                         && app->currentRoute().value(u"path"_s).toString() == drawingPath;
              }) && QDir(workspace->absoluteFilePath(u"assets"_s)).entryList({u"*.svg"_s}).size() == svgs,
              "choosing it opens the same drawing, no second file");
        closeMenus(window);
        app->back();
        check(waitFor([&] { return opened(2); }, 15000), "and Back returns to the viewer");
    }

    // The More menu's "Open in another app" (the shell's top bar).
    {
        const QString more = QCoreApplication::translate("ShellTopBar", "More");
        QQuickItem *button = findShown(window->contentItem(), [&](QQuickItem *item) {
            return isA(item, "BIconButton") && item->property("label").toString() == more;
        });
        if (button)
            tapOrClick(window, pointIn(button), touch);
        QQuickItem *item = nullptr;
        check(waitFor([&] { return (item = shownNamed(window->contentItem(), u"menuOpenInOtherApp"_s)) != nullptr; }),
              "the More menu offers “Open in another app” on a PDF");
        handed.clear();
        if (item)
            tapOrClick(window, pointIn(item), touch);
        check(waitFor([&] { return handed == QStringList{absolute + u"|application/pdf"_s}; }),
              "“Open in another app” hands the PDF over");
        closeMenus(window);
    }

    // Moved to another folder while it is open (the route keeps pdfPage): the viewer opens the
    // new path on the same page, and writes no other page into the route afterwards.
    {
        check(opened(2), "the viewer is on page 2 before the move");
        const QString moved = workspace->movePath(pdf, u"Dokumenty żółte"_s);
        if (!moved.isEmpty())
            pdf = moved; // (opened() reads it)
        // (The model opens the new file a moment later: until then it still shows the old one.)
        const QString movedFile = moved.isEmpty() ? QString() : workspace->absoluteFilePath(moved);
        check(!moved.isEmpty() && waitFor([&] {
            return app->currentRoute().value(u"path"_s).toString() == moved
                   && modelValue("file").toString() == movedFile;
        }),
              "the route and the viewer follow the moved PDF");
        waitFor([] { return false; }, 1500); // (reopened, and longer than the route write's 500 ms)
        const bool reopened =
                waitFor([&] { return opened(2); }, 15000) && app->currentRoute().value(u"pdfPage"_s).toInt() == 2;
        if (!reopened) // (once on a Windows runner: what the viewer shows instead)
            std::fprintf(stderr,
                         "selftest: after the move: route path %s, pdfPage %d; viewer %s, ready %d, file %s, status "
                         "%d, page %d of %d, error \"%s\"\n",
                         qPrintable(app->currentRoute().value(u"path"_s).toString()),
                         app->currentRoute().value(u"pdfPage"_s).toInt(), pdfView() ? "shown" : "none",
                         pdfView() ? int(pdfView()->property("ready").toBool()) : -1,
                         qPrintable(modelValue("file").toString()), modelValue("status").toInt(),
                         modelValue("currentPage").toInt(), modelValue("pageCount").toInt(),
                         qPrintable(modelValue("error").toString()));
        check(reopened, "it reopens on the same page, kept in the route");
        // A route naming only a zoom (remote control, tests): applied without route.open.
        app->navigate(QVariantMap{{u"view"_s, u"page"_s}, {u"path"_s, pdf}, {u"pdfScale"_s, 250}});
        check(waitFor([&] { return opened(0) && qAbs(modelValue("zoomPercent").toInt() - 250) <= 1; }, 15000),
              "the route's pdfScale 250 zooms to 250 %");
        app->back();
        check(waitFor([&] { return opened(2); }, 15000), "Back returns to page 2");
    }

    // A PDF protected with a password: its reason and the button, no password prompt.
    const QString data = qEnvironmentVariable("BRULION_SELFTEST_PDF_DIR");
    QFile lockedFile(QDir(data).filePath(u"zaszyfrowany.pdf"_s));
    if (!data.isEmpty() && lockedFile.open(QIODevice::ReadOnly)) {
        const QString locked = workspace->createAttachment(u"assets/sejf.pdf"_s, lockedFile.readAll());
        app->openPage(locked);
        check(waitFor(
                      [&] {
                          return pdfView() && modelValue("locked").toBool()
                                 && shownNamed(pdfView(), u"pdfOpenElsewhere"_s) != nullptr;
                      },
                      15000),
              "a PDF protected with a password says so, with “Open in another app”");
        check(waitFor([&] {
                  QQuickItem *name = pdfView() ? shownNamed(pdfView(), u"pdfMessageName"_s) : nullptr;
                  return name && name->property("text").toString() == u"sejf.pdf"_s
                         && shownNamed(pdfView(), u"pdfMessageSize"_s) != nullptr;
              }),
              "and names the file with its size");
        // Its password typed (a wrong one first): asked again, then the pages.
        if (platform::pdfPasswordAvailable()) {
            const auto typePassword = [&](const QString &password) {
                QQuickItem *field = pdfView() ? shownNamed(pdfView(), u"pdfPasswordField"_s) : nullptr;
                if (!field)
                    return false;
                if (touch)
                    tapOrClick(window, pointIn(field), touch);
                field->forceActiveFocus();
                for (const QChar ch : password)
                    sendKey(window, 0, Qt::NoModifier, QString(ch));
                sendKey(window, Qt::Key_Return);
                return true;
            };
            check(typePassword(u"jawne"_s) && waitFor([&] { return modelValue("passwordRejected").toBool(); }, 15000)
                          && shownNamed(pdfView(), u"pdfPasswordField"_s) != nullptr,
                  "a wrong password is refused and asked for again");
            check(typePassword(u"tajne"_s)
                          && waitFor(
                                  [&] {
                                      return pdfView() && pdfView()->property("ready").toBool()
                                             && modelValue("pageCount").toInt() == 3;
                                  },
                                  15000),
                  "the right password opens the PDF");
            check(waitFor([&] { return shownNamed(pdfView(), u"pdfPageImage"_s) != nullptr; })
                          && shownNamed(pdfView(), u"pdfDrawButton"_s) == nullptr,
                  "its pages are drawn, “Draw on this page” is not offered");
            app->goHome();
            app->openPage(locked);
            check(waitFor(
                          [&] {
                              return pdfView() && modelValue("locked").toBool()
                                     && shownNamed(pdfView(), u"pdfPasswordField"_s) != nullptr;
                          },
                          15000),
                  "opened again, it asks for the password again (never kept)");
        }
    }

    // A PDF with a table of contents and links (tests/data/pdf/spis.pdf): the outline jumps, a link
    // to a page jumps, a web link names its address; two pages side by side.
    QFile linkedFile(QDir(data).filePath(u"spis.pdf"_s));
    if (!data.isEmpty() && linkedFile.open(QIODevice::ReadOnly)
        && platform::pdfBackendName() != u"Android PdfRenderer"_s) {
        pdf = workspace->createAttachment(u"assets/spis treści.pdf"_s, linkedFile.readAll());
        app->openPage(pdf);
        check(waitFor([&] { return opened(1); }, 15000), "the PDF with a table of contents opens");
        // (poppler only: Qt PDF's bookmark model is not safe with a looping outline.)
        if (platform::pdfBackendName() == u"poppler"_s) {
            // (The button shows once the outline has been read, on a thread: until the bar's layout
            // has placed it, a click at its old place hits “Draw on this page” — on a busy runner.)
            QQuickItem *outlineButton = nullptr;
            check(waitFor([&] {
                      outlineButton = shownNamed(pdfView(), u"pdfOutlineButton"_s);
                      return outlineButton && takesPressAtCentre(window, outlineButton);
                  }),
                  "a PDF with a table of contents has its button");
            if (outlineButton && takesPressAtCentre(window, outlineButton))
                tapOrClick(window, pointIn(outlineButton), touch);
            QQuickItem *third = nullptr;
            check(waitFor(
                          [&] {
                              third = findShown(window->contentItem(), [](QQuickItem *item) {
                                  return item->objectName() == u"pdfOutlineEntry"_s
                                         && item->property("text").toString() == u"Rozdział 3"_s;
                              });
                              return third != nullptr;
                          },
                          10000), // (the dialog opens over a few frames: slow on a busy CI runner)
                  "the table of contents lists its chapters");
            if (!third) {
                // What there is instead: the entries shown, the outline the view has, the route,
                // the PDF views, the popups.
                QStringList entries;
                for (QQuickItem *item : uidriver::matching(window, [](QQuickItem *i) {
                         return i->objectName() == u"pdfOutlineEntry"_s;
                     }))
                    entries.append(item->property("text").toString());
                QQuickItem *shownPdf = pdfView();
                QObject *viewPdf = shownPdf ? shownPdf->property("model").value<QObject *>() : nullptr;
                qWarning().noquote() << "selftest: outline entries" << entries << "outline"
                                     << (viewPdf ? viewPdf->property("outline") : QVariant()) << "route"
                                     << app->currentRoute();
                for (QQuickItem *item :
                     uidriver::matching(window, [](QQuickItem *i) { return i->objectName() == u"pdfView"_s; }, false))
                    qWarning().noquote() << "selftest: a PDF view" << item->isVisible() << item->width()
                                         << item->height() << item->property("ready")
                                         << uidriver::itemsAt(window, pointIn(item));
                for (QObject *popup : uidriver::openPopups(window))
                    qWarning().noquote() << "selftest: a popup" << popup->metaObject()->className()
                                         << popup->objectName();
                qWarning().noquote() << "selftest: shown at 640,400:" << uidriver::itemsAt(window, QPointF(640, 400));
                closeMenus(window);
            }
            if (third)
                tapOrClick(window, pointIn(third), touch);
            check(third && waitFor([&] { return opened(3); }), "a chapter of the table of contents opens its page");
        }
        if (QQuickItem *shownPdf = pdfView())
            shownPdf->forceActiveFocus();
        sendKey(window, Qt::Key_Home);
        QQuickItem *link = nullptr;
        check(waitFor(
                      [&] {
                          link = findShown(pdfView(), [](QQuickItem *item) {
                              return item->objectName() == u"pdfLink"_s && item->property("page").toInt() == 2;
                          });
                          return opened(1) && link != nullptr;
                      },
                      10000),
              "page 1's links are there");
        const bool webLink =
                findShown(pdfView(),
                          [](QQuickItem *item) {
                              return item->objectName() == u"pdfLink"_s
                                     && item->property("url").toString().startsWith(u"https://example.org/"_s);
                          })
                != nullptr;
        // (Only page 1 has links: three of its six — never the script, the javascript: address, the launch.)
        const int linkCount = int(
                uidriver::matching(window, [](QQuickItem *item) { return item->objectName() == u"pdfLink"_s; }).size());
        check(webLink && linkCount == 3, "a web link keeps its address; scripts and launches are no links");
        if (link) {
            const QPointF at = pointIn(link);
            // (A toast of an earlier step may still lie over it.)
            waitFor([&] { return uidriver::pressTargetAt(window, at) == link; }, 10000);
            QQuickItem *target = uidriver::pressTargetAt(window, at);
            std::fprintf(stderr, "selftest: PDF link at %.0f,%.0f, press target %s %s; %s\n", at.x(), at.y(),
                         target ? target->metaObject()->className() : "",
                         target ? qPrintable(target->objectName()) : "", qPrintable(uidriver::itemsAt(window, at)));
            tapOrClick(window, at, touch);
        }
        const bool followed = waitFor([&] { return opened(2); });
        if (!followed)
            std::fprintf(stderr, "selftest: after the link: page %d, route pdfPage %d\n",
                         modelValue("currentPage").toInt(), app->currentRoute().value(u"pdfPage"_s).toInt());
        check(followed, "a link to page 2 goes there");
        // Two pages side by side (the zoom menu; ⋯ on phones), and back.
        QObject *m = viewModel();
        const auto sideBySide = [&] {
            QRectF one;
            QRectF two;
            QMetaObject::invokeMethod(m, "pageRect", qReturnArg(one), 1);
            QMetaObject::invokeMethod(m, "pageRect", qReturnArg(two), 2);
            return qAbs(one.y() - two.y()) < 1 && two.left() > one.right();
        };
        const bool wideBar = shownNamed(pdfView(), u"pdfZoomButton"_s) != nullptr;
        if (QQuickItem *button = shownNamed(pdfView(), wideBar ? u"pdfZoomButton"_s : u"pdfMoreButton"_s))
            tapOrClick(window, pointIn(button), touch);
        QQuickItem *item = nullptr;
        check(waitFor([&] {
                  return (item = shownNamed(window->contentItem(),
                                            wideBar ? u"pdfZoomTwoPages"_s : u"pdfMenuTwoPages"_s));
              }),
              "the menu offers two pages side by side");
        if (item)
            tapOrClick(window, pointIn(item), touch);
        check(m && waitFor([&] { return m->property("twoPages").toBool() && sideBySide(); }) && opened(2),
              "pages 1 and 2 side by side, page 2 still the current one");
        closeMenus(window);
        uidriver::callQml(pdfView(), "setTwoPages", {QVariant(false)});
        check(waitFor([&] { return m && !m->property("twoPages").toBool() && !sideBySide(); }),
              "and back to one page per row");
    }

    // Another attachment that is no note (a ZIP): read-only text, never the editor.
    const QString zip =
            workspace->createAttachment(u"assets/archiwum.zip"_s, QByteArray("PK\x03\x04 Zażółć\0\1\2", 18));
    app->openPage(zip);
    check(waitFor([&] {
              QQuickItem *picture = shownNamed(window->contentItem(), u"pictureView"_s);
              return picture && picture->property("textOnly").toBool()
                     && shownNamed(picture, u"pictureReadOnly"_s) != nullptr;
          }) && controllerFor(zip) == nullptr,
          "a ZIP opens read-only, not in the block editor");
    check(shownNamed(window->contentItem(), u"pictureOpenElsewhere"_s) == nullptr,
          "a ZIP is never handed to another app");
    // A document of the allow-list (a DOCX: a zip with Word's parts): read-only here, handed over.
    {
        const auto entry = [](const QByteArray &name, const QByteArray &bytes) {
            return QByteArray("PK\x03\x04", 4) + QByteArray(26, '\0') + name + bytes;
        };
        const QString docx = workspace->createAttachment(u"assets/Sprawozdanie – źródła.docx"_s,
                                                         entry("[Content_Types].xml", "<Types/>")
                                                                 + entry("word/document.xml", "<w:document/>"));
        app->openPage(docx);
        QQuickItem *button = nullptr;
        check(waitFor([&] {
                  return (button = shownNamed(window->contentItem(), u"pictureOpenElsewhere"_s)) != nullptr;
              }) && controllerFor(docx) == nullptr,
              "a DOCX opens read-only with “Open in another app”");
        handed.clear();
        if (button)
            tapOrClick(window, pointIn(button), touch);
        check(waitFor([&] {
                  return handed
                         == QStringList{
                                 workspace->absoluteFilePath(docx)
                                 + u"|application/vnd.openxmlformats-officedocument.wordprocessingml.document"_s};
              }),
              "it hands the DOCX to the system's app for Word documents");
    }
    app->goHome();
}

/// 5e. The default reminder time in the notebook's settings, set on the clock: saved at once, and
/// the field keeps following the notebook's config afterwards — a value synced from another
/// device shows (assigning the picked text once ended that binding: the field kept the old value
/// and wrote it back with the next edit).
/// 5i. A drawing (docs/UI.md "Drawing editor"): a tap / click on its embed opens the drawing
/// editor; a stroke drawn with real input — a finger on the phone run ("Draw with finger" is on until a
/// stylus is seen), the mouse on the desktop — is saved by itself, its undo kept (the save coming
/// back is the editor's own, not a change to merge); an undo (the bar's button / Ctrl+Z) is saved
/// too; back on the page, the embed loads the drawing saved again (its URL's version). The embed is
/// the drawing image provider's picture in the app's theme: switching the app to the other theme
/// loads the other one at once; the editor's paper follows the app's theme too.
void checkDrawing(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *workspace = app->workspace();
    const QString path = workspace->saveAttachment(u"Szkic z wykładu.svg"_s, drawing::toSvg(drawing::Drawing()));
    const QString page = u"Wykład z rysunkiem.md"_s;
    check(!path.isEmpty() && workspace->writeFile(page, u"- Notatki z tablicy: ![[Szkic z wykładu.svg]]\n"_s),
          "a page embeds a new drawing");
    const auto strokesInFile = [&] {
        const std::optional<drawing::Drawing> d = drawing::readDrawing(workspace->readAttachment(path));
        return d ? int(d->strokes.size()) : -1;
    };
    // (image://drawing/<the file's path, percent-encoded>?v=…&dark=…: Workspace::drawingUrl.)
    const auto embedImage = [window]() -> QQuickItem * {
        return uidriver::topmost(window, [](QQuickItem *item) {
            const QUrl url = item->property("source").toUrl();
            return item->objectName() == u"embedImage" && url.scheme() == u"image" && url.host() == u"drawing"
                   && QUrl::fromPercentEncoding(url.path().toUtf8()).endsWith(u"/Szkic z wykładu.svg"_s);
        });
    };
    const auto darkQuery = [](const QUrl &url) { return QUrlQuery(url).queryItemValue(u"dark"_s) == u"1"; };
    app->openPage(page);
    check(waitFor([&] { return embedImage() && embedImage()->property("status").toInt() == 1; }, 5000),
          "the drawing's embed is shown");
    QQuickItem *image = embedImage();
    if (!image)
        return;
    const QUrl before = image->property("source").toUrl();
    check(before.query().startsWith(u"v="_s), "a drawing's URL carries its version");
    check(uidriver::topmost(window, [](QQuickItem *item) { return item->objectName() == u"embedDrawingBadge"; })
                  != nullptr,
          "a drawing's embed has the pen in its corner");
    {
        // The app's theme switched: the embed shows the drawing in the other one at once.
        AppSettings *settings = app->settings();
        const QString mode = settings->themeMode();
        const bool wasDark = darkQuery(before);
        const auto embedShows = [&](bool dark) {
            QQuickItem *again = embedImage();
            return again && darkQuery(again->property("source").toUrl()) == dark
                   && again->property("status").toInt() == 1;
        };
        settings->setThemeMode(wasDark ? u"light"_s : u"dark"_s);
        check(waitFor([&] { return embedShows(!wasDark); }, 5000),
              "the app's other theme shows the drawing's embed in that theme");
        settings->setThemeMode(mode);
        check(waitFor([&] { return embedShows(wasDark); }, 5000), "and back");
        image = embedImage();
        if (!image)
            return;
    }
    tapOrClick(window, pointIn(image), touch);
    check(waitFor([&] { return app->currentRoute().value(u"view"_s).toString() == u"drawing"; }),
          "a tap / click on the drawing opens the drawing editor");
    check(app->currentRoute().value(u"path"_s).toString() == path, "on that drawing");
    QQuickItem *canvas = nullptr;
    waitFor([&] {
        canvas = uidriver::topmost(window, [](QQuickItem *item) { return item->objectName() == u"drawingCanvas"; });
        return canvas && canvas->width() > 100;
    });
    if (!canvas) {
        check(false, "the drawing editor shows its paper");
        return;
    }
    check(canvas->property("dark").toBool() == darkQuery(before) && canvas->property("theme").toString() == u"auto",
          "a new drawing's paper follows the app's theme");
    // Phones: the drawing's tool bar at the bottom takes the bottom navigation's place (no
    // keyboard involved: it stays away while the drawing is edited).
    QQuickItem *nav = window->findChild<QQuickItem *>(u"shellBottomNav"_s);
    const bool phone = window->width() < 900;
    if (phone) {
        check(nav && waitFor([&] { return !nav->isVisible(); }),
              "the drawing's tool bar at the bottom: the bottom navigation makes way");
        // With the bottom navigation's Menu gone, the top bar's ☰ (always there) opens the drawer.
        QObject *drawer = window->findChild<QObject *>(u"shellDrawer"_s);
        QQuickItem *menuButton = nullptr;
        waitFor([&] {
            menuButton = uidriver::topmost(window, [](QQuickItem *i) { return i->objectName() == u"topBarMenu"; });
            return menuButton != nullptr;
        });
        check(menuButton && drawer, "where the drawing's bar replaces the bottom navigation the top bar shows ☰");
        if (menuButton && drawer) {
            tapOrClick(window, pointIn(menuButton), touch);
            check(waitFor([drawer] { return drawer->property("opened").toBool(); }, 3000),
                  "the drawing editor's ☰ opens the drawer");
            QMetaObject::invokeMethod(drawer, "close");
            check(waitFor([drawer] { return !drawer->property("visible").toBool(); }, 3000)
                          && app->currentRoute().value(u"view"_s).toString() == u"drawing",
                  "the drawer closes over the drawing editor, still open");
        }
        // A phone held upright: every tool, the swatch, "More" and "Done" on screen (two lines),
        // none behind the bar's edge.
        QStringList hidden;
        const auto allShown = [&] {
            hidden.clear();
            for (const QString &name : {u"drawingUndo"_s, u"drawingRedo"_s, u"drawingPen"_s, u"drawingHighlighter"_s,
                                        u"drawingEraser"_s, u"drawingSelect"_s, u"drawingShape"_s, u"drawingText"_s,
                                        u"drawingStyle"_s, u"drawingMore"_s, u"drawingDone"_s}) {
                QQuickItem *item =
                        uidriver::topmost(window, [&name](QQuickItem *i) { return i->objectName() == name; });
                if (!item || !uidriver::shown(item) || !uidriver::onScreen(window, item))
                    hidden.append(name);
            }
            return hidden.isEmpty();
        };
        if (!waitFor(allShown))
            qWarning("selftest: off screen on the drawing's tool bar: %s", qPrintable(hidden.join(u", "_s)));
        check(hidden.isEmpty(), "the drawing's tool bar shows all its buttons on a phone");
    }
    settle(100);
    const QPointF from = pointIn(canvas, 0.25, 0.4);
    const QPointF to = pointIn(canvas, 0.7, 0.45);
    if (touch)
        touchDrag(window, from, to - from);
    else
        mouseDrag(window, from, to);
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 1; }),
          touch ? "a finger draws a stroke (no stylus seen yet)" : "the mouse draws a stroke");
    check(waitFor([&] { return strokesInFile() == 1 && !canvas->property("modified").toBool(); }, 5000),
          "the stroke is saved by itself");
    check(canvas->property("canUndo").toBool(), "its own save keeps the undo history (no merge)");
    if (touch) {
        QQuickItem *undo = uidriver::topmost(window, [](QQuickItem *item) { return item->objectName() == u"drawingUndo"; });
        if (undo)
            touchTap(window, pointIn(undo));
        check(undo != nullptr, "the bar at the bottom has Cofnij");
    } else {
        sendKey(window, Qt::Key_Z, Qt::ControlModifier);
    }
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 0; }), "Cofnij takes the stroke back");
    check(waitFor([&] { return strokesInFile() == 0; }, 5000), "the undo is saved too");
    QMetaObject::invokeMethod(canvas, "redo");
    check(waitFor([&] { return strokesInFile() == 1; }, 5000), "the redo is saved");

    // Shapes, the selection and text (drawshapes): a rectangle dragged out, selected and deleted
    // from the selection's bar, undone; on the desktop with the keys, text written on the paper.
    const auto tap = [&](QPointF at) {
        if (touch)
            touchTap(window, at);
        else
            mouseClick(window, at);
    };
    const auto button = [&](const QString &name) {
        return uidriver::topmost(window, [&name](QQuickItem *item) { return item->objectName() == name; });
    };
    const QPointF corner = pointIn(canvas, 0.3, 0.6);
    const QPointF far = pointIn(canvas, 0.6, 0.75);
    if (touch) {
        if (QQuickItem *shape = button(u"drawingShape"_s))
            touchTap(window, pointIn(shape));
    } else {
        sendKey(window, Qt::Key_R, Qt::NoModifier, u"r"_s);
    }
    check(waitFor([&] { return canvas->property("tool").toString() == u"rectangle"; }), "the rectangle tool");
    if (touch)
        touchDrag(window, corner, far - corner);
    else
        mouseDrag(window, corner, far);
    const auto elementTypes = [&] {
        QStringList types;
        const std::optional<drawing::Drawing> d = drawing::readDrawing(workspace->readAttachment(path));
        for (const drawing::Stroke &e : d ? d->strokes : QList<drawing::Stroke>())
            types.append(drawing::elementTypeName(e.type));
        return types;
    };
    check(waitFor([&] { return elementTypes() == QStringList{u"freedraw"_s, u"rectangle"_s}; }, 5000),
          "a rectangle dragged out is saved as Excalidraw's rectangle");
    if (touch) {
        if (QQuickItem *select = button(u"drawingSelect"_s))
            touchTap(window, pointIn(select));
    } else {
        sendKey(window, Qt::Key_V, Qt::NoModifier, u"v"_s);
    }
    check(waitFor([&] { return canvas->property("tool").toString() == u"select"; }), "the select tool");
    // Clicked again: how it selects (lasso, rectangle); it stays marked as the tool in use.
    if (QQuickItem *select = button(u"drawingSelect"_s)) {
        tap(pointIn(select));
        QQuickItem *lasso = nullptr;
        check(waitFor([&] { return (lasso = button(u"drawingSelectLasso"_s)) != nullptr; }, 3000),
              "the select button clicked again offers how it selects");
        if (lasso) {
            tap(pointIn(lasso));
            check(waitFor([&] { return button(u"drawingSelectLasso"_s) == nullptr; }, 3000),
                  "a choice closes the menu");
        }
        check(select->property("checked").toBool() && canvas->property("tool").toString() == u"select"
                      && canvas->property("selectMode").toString() == u"lasso",
              "the select button stays marked when clicked again");
    }
    tap(pointIn(canvas, 0.45, 0.6)); // on the rectangle's top edge: it alone
    check(waitFor([&] { return canvas->property("selectionCount").toInt() == 1; }), "a tap selects the rectangle");
    check(waitFor([&] {
              QQuickItem *bar = button(u"drawingSelectionBar"_s);
              return bar && bar->isVisible();
          }),
          "the selection's bar is shown");
    if (QQuickItem *remove = button(u"drawingSelectionDelete"_s))
        tap(pointIn(remove));
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 1; }), "Usuń on the bar deletes it");
    QMetaObject::invokeMethod(canvas, "undo");
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 2; }), "and Cofnij brings it back");
    if (!touch) {
        sendKey(window, Qt::Key_A, Qt::ControlModifier);
        check(waitFor([&] { return canvas->property("selectionCount").toInt() == 2; }), "Ctrl+A selects everything");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return canvas->property("selectionCount").toInt() == 0; }), "Escape clears it");
        // A change just now (the 1.2 s save timer runs): the text written meanwhile is never cut
        // short by that save — the editor stays open and keeps the keys while the user pauses.
        QMetaObject::invokeMethod(canvas, "undo");
        QMetaObject::invokeMethod(canvas, "redo");
        sendKey(window, Qt::Key_T, Qt::NoModifier, u"t"_s);
        mouseClick(window, pointIn(canvas, 0.3, 0.2));
        QQuickItem *editor = nullptr;
        check(waitFor([&] {
                  editor = button(u"drawingTextEditor"_s);
                  return editor && editor->hasActiveFocus();
              }),
              "a click with the text tool opens the text editor");
        const auto type = [&](const QString &text) {
            for (const QChar c : text)
                sendKey(window, c == u'Ć' ? Qt::Key_unknown : Qt::Key(c.toUpper().unicode()), Qt::NoModifier,
                        QString(c));
        };
        type(u"Ć"_s);
        settle(1800);
        check(editor && editor->hasActiveFocus() && canvas->property("textEditing").toBool()
                      && canvas->property("tool").toString() == u"text",
              "the save timer leaves a text being written alone");
        type(u"ma"_s);
        sendKey(window, Qt::Key_Escape);
        const auto texts = [&] {
            QStringList out;
            const std::optional<drawing::Drawing> d = drawing::readDrawing(workspace->readAttachment(path));
            for (const drawing::Stroke &e : d ? d->strokes : QList<drawing::Stroke>()) {
                if (e.type == drawing::ElementType::Text)
                    out.append(e.text);
            }
            return out;
        };
        check(waitFor([&] { return texts() == QStringList{u"Ćma"_s}; }, 5000), "text written on the paper is saved");
        check(editor && !editor->hasActiveFocus(), "the keys go back to the drawing");
        // A text low on the paper, then the window shrinks under it (a browser's keyboard resizes
        // the page and reports no overlap): the view moves so the text stays on the paper.
        {
            const QSize full = window->size();
            canvas->setProperty("tool", u"text"_s);
            mouseClick(window, pointIn(canvas, 0.3, 0.85));
            check(waitFor([&] { return editor && editor->hasActiveFocus(); }), "a text opens low on the paper");
            const qreal paperHeight = canvas->height();
            window->resize(full.width(), full.height() * 2 / 3);
            check(waitFor([&] { return canvas->height() < paperHeight - full.height() / 4; }),
                  "the paper shrinks with the window");
            const auto onPaper = [&] {
                if (!editor) // (a failed step above: no crash, that check failed already)
                    return false;
                const QRectF e = editor->mapRectToScene(QRectF(0, 0, editor->width(), editor->height()));
                const QRectF c = canvas->mapRectToScene(QRectF(0, 0, canvas->width(), canvas->height()));
                return e.bottom() <= c.bottom() + 1 && e.top() >= c.top() - 1;
            };
            check(waitFor(onPaper, 3000), "a window shrinking under a text being written keeps the text on the paper");
            sendKey(window, Qt::Key_Escape);
            window->resize(full);
            settle(300);
            check(texts() == QStringList{u"Ćma"_s}, "an empty text writes nothing");
        }
        QMetaObject::invokeMethod(canvas, "undo");
    }
    QMetaObject::invokeMethod(canvas, "undo");
    canvas->setProperty("tool", u"pen"_s);
    check(waitFor([&] { return strokesInFile() == 1 && canvas->property("strokeCount").toInt() == 1; }, 5000),
          "back to the stroke alone");

    // Another device's version on disk the vault has not reported yet (a missed folder watch,
    // Android's shared storage): the save reads the file first and merges, never writes over it.
    QQuickItem *view = canvas;
    while (view && view->metaObject()->indexOfMethod("save()") < 0)
        view = view->parentItem();
    const auto writeBehind = [&](const drawing::Drawing &d) {
        QFile f(workspace->vault()->absolutePath(path));
        const bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(drawing::toSvg(d)) > 0;
        f.close();
        return ok;
    };
    const auto drawStroke = [&](double y) {
        const QPointF a = pointIn(canvas, 0.25, y);
        const QPointF b = pointIn(canvas, 0.6, y + 0.02);
        if (touch)
            touchDrag(window, a, b - a);
        else
            mouseDrag(window, a, b);
    };
    drawing::Drawing theirs = drawing::readDrawing(workspace->readAttachment(path)).value_or(drawing::Drawing());
    drawing::Stroke remote;
    remote.id = u"kreskaZdalna"_s;
    remote.points = {{40, 400, 0.5}, {240, 420, 0.5}};
    theirs.strokes.append(remote);
    drawStroke(0.6);
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 2; }) && view
                  && canvas->property("modified").toBool() && writeBehind(theirs),
          "a stroke drawn here, another device's version written behind the vault's back");
    QVariant saved;
    if (view)
        uidriver::callQml(view, "save", {}, &saved);
    {
        const std::optional<drawing::Drawing> d = drawing::readDrawing(workspace->readAttachment(path));
        const bool kept = d && std::any_of(d->strokes.cbegin(), d->strokes.cend(),
                                           [](const drawing::Stroke &s) { return s.id == u"kreskaZdalna"; });
        check(d && d->strokes.size() == 3 && kept && canvas->property("strokeCount").toInt() == 3,
              "the save merges a version not reported yet: both devices' strokes are in the file");
    }

    // A version Brulion cannot edit (a frame added in excalidraw.com): shown as it is, never
    // written over; what was drawn here is kept in a new file beside it.
    drawing::Drawing shapes = drawing::readDrawing(workspace->readAttachment(path)).value_or(drawing::Drawing());
    shapes.foreign.append(QJsonObject{{u"type"_s, u"frame"_s},
                                      {u"id"_s, u"ramka"_s},
                                      {u"x"_s, 10},
                                      {u"y"_s, 10},
                                      {u"width"_s, 50},
                                      {u"height"_s, 40},
                                      {u"name"_s, u"Ramka"_s}});
    const QByteArray shapesBytes = drawing::toSvg(shapes);
    drawStroke(0.8);
    check(waitFor([&] { return canvas->property("strokeCount").toInt() == 4; }) && writeBehind(shapes),
          "a stroke drawn here, an Excalidraw version with a frame written behind the vault's back");
    if (view)
        uidriver::callQml(view, "save", {}, &saved);
    check(workspace->readAttachment(path) == shapesBytes, "the version with the frame is not written over");
    check(view && view->property("status").toString() == u"readonly"_s, "and the editor shows it as it is");
    {
        const QString folder = paths::dirname(path);
        const QString stem = QFileInfo(path).completeBaseName();
        int copies = 0;
        for (const QString &name : QDir(workspace->vault()->absolutePath(folder)).entryList({u"*.svg"_s}, QDir::Files)) {
            const QString other = folder.isEmpty() ? name : folder + u'/' + name;
            if (other == path || !name.startsWith(stem))
                continue;
            const std::optional<drawing::Drawing> d = drawing::readDrawing(workspace->readAttachment(other));
            if (d && d->strokes.size() == 4 && drawing::editable(*d))
                ++copies;
        }
        check(copies == 1, "what was drawn here is kept in a copy beside it");
    }
    app->back();
    check(waitFor([&] {
              QQuickItem *again = embedImage();
              return again && again->property("source").toUrl() != before && again->property("status").toInt() == 1;
          },
                  5000),
          "back on the page, the embed shows the drawing saved again");
    if (phone)
        check(nav && waitFor([&] { return nav->isVisible(); }), "back on the page, the bottom navigation too");
    app->goHome();
}

/// 5j. A drawing another program wrote (excalidraw.com frames its strokes with its own margins):
/// its embed has the shape of the file's own size from the start, the picture of the drawing image
/// provider the same — nothing below it moves when it has loaded — and keeps it while the app's
/// theme switches (the picture of the other theme loads behind the shown one).
void checkDrawingFrame(QQuickWindow *window, AppController *app, const Check &check)
{
    Workspace *workspace = app->workspace();
    drawing::Drawing d;
    drawing::Stroke s;
    s.id = u"kreskaŻółwia"_s;
    s.points = {{40, 60, 0.5}, {120, 90, 0.5}, {240, 70, 0.5}};
    d.strokes = {s};
    QByteArray bytes = drawing::toSvg(d);
    const QRectF rect = drawing::pictureRect(d);
    const int w = int(rect.width());
    const int h = int(rect.height());
    // The root of another program: the same scene in a wider frame.
    const int ownWidth = w + 96;
    const QByteArray root = "viewBox=\"0 0 " + QByteArray::number(w) + ' ' + QByteArray::number(h) + "\" width=\""
                            + QByteArray::number(w) + "\" height=\"" + QByteArray::number(h) + '"';
    const QByteArray other = "viewBox=\"0 0 " + QByteArray::number(ownWidth) + ' ' + QByteArray::number(h)
                             + "\" width=\"" + QByteArray::number(ownWidth) + "\" height=\"" + QByteArray::number(h)
                             + '"';
    check(bytes.contains(root), "a drawing's root names its picture's size");
    bytes.replace(root, other);
    const QString path = workspace->saveAttachment(u"Rysunek z innego programu.svg"_s, bytes);
    const QString page = u"Szkic z excalidraw.com.md"_s;
    check(!path.isEmpty() && workspace->writeFile(page, u"Przed rysunkiem\n\n![[Rysunek z innego programu.svg]]\n"_s),
          "a page embeds a drawing whose file names another frame");
    const auto embedImage = [window]() -> QQuickItem * {
        return uidriver::topmost(window, [](QQuickItem *item) {
            const QUrl url = item->property("source").toUrl();
            return item->objectName() == u"embedImage" && url.scheme() == u"image"
                   && QUrl::fromPercentEncoding(url.path().toUtf8()).endsWith(u"/Rysunek z innego programu.svg"_s);
        });
    };
    app->openPage(page);
    check(waitFor([&] { return embedImage() && embedImage()->property("status").toInt() == 1; }, 5000),
          "the embed of a drawing from another program is shown");
    QQuickItem *image = embedImage();
    QQuickItem *frame = image ? image->parentItem() : nullptr;
    if (!frame || frame->objectName() != u"embedPicture") {
        check(false, "the drawing's picture has its frame");
        return;
    }
    settle(100);
    const QSizeF shown = frame->size();
    const double own = double(ownWidth) / h;
    const double picture =
            image->property("implicitWidth").toDouble() / std::max(1.0, image->property("implicitHeight").toDouble());
    std::fprintf(stderr, "selftest: the frame %.0f x %.0f, the file %d x %d, the picture %.3f\n", shown.width(),
                 shown.height(), ownWidth, h, picture);
    check(std::abs(shown.width() / shown.height() - own) < 0.02 * own && std::abs(picture - own) < 0.02 * own,
          "the embed and its picture have the shape of the file's own size");
    AppSettings *settings = app->settings();
    const QString mode = settings->themeMode();
    const bool wasDark = QUrlQuery(image->property("source").toUrl()).queryItemValue(u"dark"_s) == u"1";
    bool moved = false;
    const auto showsTheme = [&](bool dark) {
        QQuickItem *again = embedImage();
        if (!again || !again->parentItem())
            return false;
        moved = moved || again->parentItem()->size() != shown;
        return QUrlQuery(again->property("source").toUrl()).queryItemValue(u"dark"_s) == (dark ? u"1"_s : u"0"_s)
               && again->property("status").toInt() == 1;
    };
    settings->setThemeMode(wasDark ? u"light"_s : u"dark"_s);
    const bool switched = waitFor([&] { return showsTheme(!wasDark); }, 5000);
    settings->setThemeMode(mode);
    const bool back = waitFor([&] { return showsTheme(wasDark); }, 5000);
    check(switched && back && !moved, "the embed keeps its size while the app's theme switches");
    app->goHome();
}

void checkSettingsClock(QQuickWindow *window, AppController *app, const Check &check)
{
    Workspace *workspace = app->workspace();
    const QString key = u"defaultReminderTime"_s;
    const QString original = workspace->vaultConfig().value(key).toString();
    const auto configValue = [&] { return workspace->vaultConfig().value(key).toString(); };
    // Through the visual tree: phones list every section from a Repeater, whose items are not
    // QObject children of the window. Shown, but maybe scrolled out of the view.
    const auto shownNamed = [window](const QString &name) -> QQuickItem * {
        std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *item) -> QQuickItem * {
            if (!item->isVisible())
                return nullptr;
            if (item->objectName() == name)
                return item;
            const QList<QQuickItem *> children = item->childItems();
            for (QQuickItem *child : children) {
                if (QQuickItem *found = visit(child))
                    return found;
            }
            return nullptr;
        };
        return visit(window->contentItem());
    };
    app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"vault"_s}});
    QQuickItem *field = nullptr;
    QQuickItem *button = nullptr;
    check(waitFor([&] {
              field = shownNamed(u"defaultReminderTimeField"_s);
              button = shownNamed(u"defaultReminderTimeClock"_s);
              return field && button && field->property("text").toString() == original;
          }),
          "the notebook settings show the default reminder time and its clock button");
    QObject *dialog = button ? button->findChild<QObject *>(u"defaultReminderTimeDialog"_s) : nullptr;
    if (!field || !button || !dialog) {
        check(false, "the default reminder time has its clock");
        return;
    }
    QMetaObject::invokeMethod(button, "clicked");
    check(waitFor([&] { return dialog->property("opened").toBool(); }), "the clock button opens the clock");
    auto *picker = dialog->property("picker").value<QQuickItem *>();
    uidriver::callQml(picker, "commitTime", {7, 35});
    QMetaObject::invokeMethod(dialog, "tryAccept");
    check(waitFor([&] { return configValue() == u"07:35" && field->property("text").toString() == u"07:35"; }),
          "a time picked on the clock is saved as the default reminder time");
    waitFor([&] { return !dialog->property("visible").toBool(); });
    const auto report = [&](bool saved, bool ok) {
        if (!ok)
            std::fprintf(stderr, "selftest: config saved %d, config value %s, field %s\n", int(saved),
                         qPrintable(configValue()), qPrintable(field->property("text").toString()));
        return ok;
    };
    bool saved = workspace->setConfig({{key, u"10:05"_s}});
    check(report(saved, waitFor([&] { return field->property("text").toString() == u"10:05"; })),
          "after a clock pick the field still follows the notebook's config (a synced change shows)");
    saved = workspace->setConfig({{key, original}});
    check(report(saved,
                 waitFor([&] { return field->property("text").toString() == original; }) && configValue() == original),
          "the default reminder time is back");
    app->goHome();
}

/// The page editor shown under `root` with its controller (a loaded page), else {nullptr, nullptr}.
std::pair<QQuickItem *, QObject *> shownPageEditor(QQuickWindow *window, QQuickItem *root, const QString &path)
{
    for (QQuickItem *e : window->findChildren<QQuickItem *>(u"pageEditor"_s)) {
        auto *c = e->property("controller").value<QObject *>();
        if (c && c->property("path").toString() == path && c->property("loaded").toBool() && shown(e)
            && (!root || root->isAncestorOf(e)))
            return {e, c};
    }
    return {nullptr, nullptr};
}

/// The editing toolbar's slot of `editor` (docked on another item in the journal stream), if shown.
QQuickItem *toolbarSlotOf(QQuickWindow *window, QQuickItem *editor)
{
    for (QQuickItem *candidate : window->findChildren<QQuickItem *>(u"editToolbarSlot"_s)) {
        if (candidate->isVisible() && editor->isAncestorOf(candidate))
            return candidate;
    }
    return nullptr;
}

/// "Share → Brulion" (AppController::receiveShare, ShellShareDialog): shared text opens the
/// dialog, "Save" (a real tap / click) writes one block at the end of today's journal — no
/// reminder from "jutro" (tomorrow) —, a shared .md file is offered as a new page titled after the file and
/// saved byte for byte, its copy removed; Escape drops a share and writes nothing.
/// "Clean up": what nothing refers to is listed with nothing ticked, a tap ticks it, "Open" and
/// Back keep the ticks, only what is shown and ticked goes to the trash, and the toast's Undo still
/// works after leaving the view.
/// Find and replace (docs/UI.md "Find and replace"): the bar in a page with real keys and taps, source
/// mode, the journal stream, and the notebook-wide view with its ticks, confirmation and Undo.
void checkFindAndReplace(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const QString note = u"Szukanie żółwia.md"_s;
    const QString other = u"Szukanie inne.md"_s;
    ws->writeFile(note, u"- pierwszy żółw\n\t- ukryty ŻÓŁW\n- bez niczego\n- ostatni zolw\n"_s);
    ws->writeFile(other, u"- zolw w innej notatce\n"_s);
    app->navigate({ { u"view"_s, u"page"_s }, { u"path"_s, note } });
    QObject *controller = nullptr;
    QQuickItem *editor = nullptr;
    check(waitFor([&] {
              editor = shownNamed(window->contentItem(), u"pageEditor"_s);
              controller = editor ? editor->property("controller").value<QObject *>() : nullptr;
              return controller && controller->property("loaded").toBool()
                     && controller->property("path").toString() == note;
          }),
          "the page to search is open");
    if (!controller || !editor)
        return;
    const auto text = [&] { return controller->property("sourceText").toString(); };
    const auto typeIn = [&](const QString &what) {
        for (const QChar ch : what)
            sendKey(window, 0, Qt::NoModifier, QString(ch));
    };
    // Ctrl+F and Ctrl+E as Shortcut objects (the keyboard's way: tst_remote_control_qml) ask the
    // same actions.
    const auto shortcut = [&](int key) {
        if (key == Qt::Key_F)
            app->requestUiAction(u"findInNote"_s);
        else
            app->requestUiAction(u"toggleSourceMode"_s);
        settle(100);
    };
    const auto named = [&](const QString &name) { return shownNamed(window->contentItem(), name); };
    const auto press = [&](const QString &name) {
        if (QQuickItem *item = named(name))
            tapOrClick(window, pointIn(item), touch);
    };
    QMetaObject::invokeMethod(controller, "toggleFold", Q_ARG(int, 0)); // the second block hidden

    // Ctrl+F opens the bar, the query goes to the nearest match; focus stays in the field.
    shortcut(Qt::Key_F);
    QQuickItem *field = nullptr;
    check(waitFor([&] { return (field = named(u"findField"_s)) != nullptr; }), "the find bar opens");
    if (!field)
        return;
    check(waitFor([&] { return field->hasActiveFocus(); }), "the find field has the keyboard");
    typeIn(u"zolw"_s);
    QQuickItem *status = nullptr;
    check(waitFor([&] {
              status = named(u"findStatus"_s);
              return status && status->property("text").toString().contains(u'3');
          }, 3000),
          "the bar counts three matches, without Polish letters and letter case");
    check(waitFor([&] { return controller->property("editingRow").toInt() == 0; }), "the first match is being shown");
    check(field->hasActiveFocus(), "going to a match leaves the keyboard in the field");
    sendKey(window, Qt::Key_Return);
    check(waitFor([&] { return controller->property("editingRow").toInt() == 1; }),
          "Enter goes to the next match, out of the folded block");
    check(field->hasActiveFocus() && text().contains(u"pierwszy żółw"_s), "Enter only moved: nothing typed or split");
    sendKey(window, Qt::Key_Return, Qt::ShiftModifier);
    check(waitFor([&] { return controller->property("editingRow").toInt() == 0; }), "Shift+Enter goes back");
    if (!touch) {
        QQuickItem *next = named(u"findNext"_s);
        if (next)
            tapOrClick(window, pointIn(next), touch);
        check(waitFor([&] { return controller->property("editingRow").toInt() == 1; }) && field->hasActiveFocus(),
              "the next button keeps the keyboard in the field too");
        sendKey(window, Qt::Key_Return, Qt::ShiftModifier);
        waitFor([&] { return controller->property("editingRow").toInt() == 0; });
    }

    // Replace: one, then all as one undo step.
    press(u"findReplaceToggle"_s);
    QQuickItem *replacement = nullptr;
    check(waitFor([&] { return (replacement = named(u"findReplacement"_s)) != nullptr && replacement->hasActiveFocus(); }),
          "the replace row opens with the keyboard in it");
    typeIn(u"kot"_s);
    press(u"findReplaceOne"_s);
    check(waitFor([&] { return text() == u"- pierwszy kot\n\t- ukryty ŻÓŁW\n- bez niczego\n- ostatni zolw\n"_s; }),
          "Replace changes the current match");
    check(waitFor([&] { return controller->property("editingRow").toInt() == 1; }), "and goes on to the next one");
    press(u"findReplaceAll"_s);
    check(waitFor([&] { return text() == u"- pierwszy kot\n\t- ukryty kot\n- bez niczego\n- ostatni kot\n"_s; }),
          "Replace all changes the rest");
    QMetaObject::invokeMethod(controller, "undo");
    check(waitFor([&] { return text() == u"- pierwszy kot\n\t- ukryty ŻÓŁW\n- bez niczego\n- ostatni zolw\n"_s; }),
          "one undo takes the whole Replace all back");

    // A match selected, then a click away and back: typing inserts, the old match is not selected
    // again to be typed over.
    if (QQuickItem *again = named(u"findField"_s))
        again->forceActiveFocus();
    settle(200);
    sendKey(window, Qt::Key_Return);
    settle(150);
    const int matchRow = controller->property("editingRow").toInt();
    QQuickItem *blocks = editor->property("listView").value<QQuickItem *>();
    const auto clickRow = [&](int row) {
        QQuickItem *item = nullptr;
        QMetaObject::invokeMethod(blocks, "itemAtIndex", qReturnArg(item), row);
        if (item)
            tapOrClick(window, pointIn(item, 0.3, 0.5), touch);
    };
    check(matchRow >= 0 && matchRow != 2, "the match is being shown in its block");
    clickRow(2);
    check(waitFor([&] { return controller->property("editingRow").toInt() == 2; }), "a click moves to another block");
    clickRow(matchRow);
    check(waitFor([&] { return controller->property("editingRow").toInt() == matchRow; }), "and back to the match's block");
    settle(200);
    const QString beforeTyping = text();
    sendKey(window, 0, Qt::NoModifier, u"X"_s);
    check(waitFor([&] { return text() != beforeTyping; }) && QString(text()).remove(u'X') == beforeTyping,
          "typing after clicking back does not overwrite the old match");
    QMetaObject::invokeMethod(controller, "undo");
    waitFor([&] { return text() == beforeTyping; });

    // With the replace row open, the bar says what a replace would do to links.
    controller->setProperty("sourceText", QString(text() + u"- [[zolw]]\n"_s));
    QQuickItem *riskNote = nullptr;
    check(waitFor([&] { return (riskNote = named(u"findReplaceNote"_s)) != nullptr; }, 3000),
          "the replace row warns about a match inside a link");
    QMetaObject::invokeMethod(controller, "undo");
    waitFor([&] { return text() == beforeTyping; });

    // Esc closes it and gives the keyboard back.
    if (named(u"findField"_s))
        named(u"findField"_s)->forceActiveFocus();
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return named(u"findBar"_s) == nullptr; }), "Esc closes the find bar");
    check(controller->property("sourceText").toString().contains(u"ŻÓŁW"_s), "closing changes nothing");

    // Source mode: the same bar on the whole text.
    shortcut(Qt::Key_E);
    check(waitFor([&] { return controller->property("sourceMode").toBool(); }), "source mode is on");
    shortcut(Qt::Key_F);
    check(waitFor([&] { return (field = named(u"findField"_s)) != nullptr && field->hasActiveFocus(); }),
          "the bar opens in source mode");
    QQuickItem *sourceEdit = editor->property("sourceTextEdit").value<QQuickItem *>();
    check(sourceEdit != nullptr, "the source's text is there");
    if (field) {
        field->forceActiveFocus();
        QMetaObject::invokeMethod(field, "selectAll");
    }
    typeIn(u"zolw"_s);
    check(waitFor([&] { return sourceEdit && sourceEdit->property("selectedText").toString() == u"ŻÓŁW"_s; }, 3000),
          "source mode selects the match in the text");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return named(u"findBar"_s) == nullptr; }), "Esc closes it in source mode too");
    shortcut(Qt::Key_E);
    check(waitFor([&] { return !controller->property("sourceMode").toBool(); }), "back to the blocks");
    QMetaObject::invokeMethod(controller, "save");

    // The journal stream: not a dead key, over every day — the count spans them, Enter goes on to
    // the next day with a match (scrolled to, written in), Esc closes.
    const QString day1 = ws->journalFileFor(app->today(-1));
    const QString day3 = ws->journalFileFor(app->today(-3));
    const bool had1 = ws->exists(day1);
    const bool had3 = ws->exists(day3);
    const QString old1 = had1 ? ws->readFile(day1) : QString();
    const QString old3 = had3 ? ws->readFile(day3) : QString();
    ws->writeFile(day1, old1 + u"\n- wydrzyk pierwszy\n- wydrzyk drugi\n"_s);
    ws->writeFile(day3, old3 + u"\n- trzeci wydrzyk\n"_s);
    app->navigate({ { u"view"_s, u"journals"_s } });
    settle(300);
    shortcut(Qt::Key_F);
    check(waitFor([&] { return (field = named(u"findField"_s)) != nullptr && field->hasActiveFocus(); }),
          "the bar opens in the journal stream");
    // (The stream's NoteFind whose bar is open: a view kept for Back may have one too.)
    const auto searchedDay = [&] {
        const QList<QObject *> finds = window->findChildren<QObject *>(u"journalFind"_s);
        for (QObject *find : finds) {
            if (!find->property("active").toBool())
                continue;
            QObject *ctl = find->property("controller").value<QObject *>();
            return ctl ? ctl->property("path").toString() : QString();
        }
        return QString();
    };
    typeIn(u"wydrzyk"_s);
    check(waitFor(
                  [&] {
                      status = named(u"findStatus"_s);
                      const QString shown = status ? status->property("text").toString() : QString();
                      return shown.contains(u'3') && shown.contains(u'2') && shown.contains(u"·"_s);
                  },
                  3000),
          "the stream's bar counts the matches of every day, and in how many days");
    check(waitFor([&] { return searchedDay() == day1 || searchedDay() == day3; }, 3000),
          "the search goes on to a day with a match");
    const QString firstDay = searchedDay();
    const QString secondDay = firstDay == day1 ? day3 : day1;
    for (int i = 0; i < 4 && searchedDay() != secondDay; ++i) {
        sendKey(window, Qt::Key_Return);
        settle(250);
    }
    check(searchedDay() == secondDay, "Enter goes on to the next day with a match");
    check(waitFor([&] { return named(u"findField"_s) && named(u"findField"_s)->hasActiveFocus(); }),
          "moving to another day leaves the keyboard in the field");
    for (int i = 0; i < 4 && searchedDay() != firstDay; ++i) {
        sendKey(window, Qt::Key_Return, Qt::ShiftModifier);
        settle(250);
    }
    check(searchedDay() == firstDay, "Shift+Enter goes back to the day before");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return named(u"findBar"_s) == nullptr; }), "Esc closes the stream's bar");
    check(ws->readFile(day3).endsWith(u"- trzeci wydrzyk\n"_s), "searching the days changes nothing");
    const auto restore = [&](const QString &path, bool had, const QString &old) {
        if (had)
            ws->writeFile(path, old);
        else
            ws->deletePage(path);
    };
    restore(day1, had1, old1);
    restore(day3, had3, old3);

    // The whole notebook.
    app->navigate({ { u"view"_s, u"replace"_s }, { u"query"_s, u"zolw"_s }, { u"replacement"_s, u"pies"_s } });
    QQuickItem *apply = nullptr;
    ReplaceSession *session = ws->replacing();
    check(waitFor([&] { return (apply = named(u"replaceApply"_s)) != nullptr && !session->scanning() && session->fileCount() >= 2; }, 5000),
          "the notebook-wide view previews the notes");
    const int all = session->tickedHits();
    check(all >= 3, "every hit is ticked at first");
    QQuickItem *list = named(u"replaceList"_s);
    settle(300); // the view fades in
    QQuickItem *hitTick = nullptr;
    if (list) {
        const QList<QQuickItem *> items = uidriver::allItems(list);
        for (QQuickItem *item : items) {
            if (item->objectName() == u"replaceHitTick"_s && uidriver::shown(item)) {
                hitTick = item;
                break;
            }
        }
    }
    check(hitTick != nullptr, "a hit has its tick");
    if (hitTick)
        tapOrClick(window, pointIn(hitTick), touch);
    check(waitFor([&] { return session->tickedHits() == all - 1; }), "a tap takes one hit out");
    // Opening a hit and coming back keeps what was unticked.
    const auto shownInList = [&](const QString &name) -> QQuickItem * {
        const QList<QQuickItem *> items = uidriver::allItems(named(u"replaceList"_s));
        for (QQuickItem *item : items) {
            if (item->objectName() == name && uidriver::shown(item))
                return item;
        }
        return nullptr;
    };
    if (QQuickItem *hitText = shownInList(u"replaceHitText"_s))
        tapOrClick(window, pointIn(hitText), touch);
    check(waitFor([&] { return app->currentView() == u"page"; }), "a tap on a hit opens its note");
    app->back();
    check(waitFor([&] { return app->currentView() == u"replace" && !session->scanning() && session->fileCount() >= 2; }, 5000),
          "Back returns to the preview");
    check(waitFor([&] { return session->tickedHits() == all - 1; }, 1000), "the unticked hit is still unticked");
    settle(300); // the view fades in
    hitTick = shownInList(u"replaceHitTick"_s);
    apply = named(u"replaceApply"_s);
    if (hitTick)
        tapOrClick(window, pointIn(hitTick), touch);
    check(waitFor([&] { return session->tickedHits() == all; }), "and puts it back");
    if (apply)
        tapOrClick(window, pointIn(apply), touch);
    const QString accept = QCoreApplication::translate("ReplaceView", "Replace");
    QQuickItem *acceptButton = nullptr;
    check(waitFor([&] {
              acceptButton = uidriver::findShown(window->contentItem(), [&](QQuickItem *item) {
                  return isA(item, "BButton") && item != apply && item->property("text").toString() == accept;
              });
              return acceptButton != nullptr;
          }),
          "the confirmation asks first");
    check(ws->readFile(other) == u"- zolw w innej notatce\n"_s, "nothing is written before the confirmation");
    // (The note's Replace all has a toast with Undo of its own: this replace's is the one to press.)
    if (QObject *toasts = window->findChild<QObject *>(u"toastHost"_s))
        QMetaObject::invokeMethod(toasts, "clear");
    settle(250);
    if (acceptButton)
        tapOrClick(window, pointIn(acceptButton), touch);
    check(waitFor([&] { return ws->readFile(other) == u"- pies w innej notatce\n"_s; }, 5000), "the replace is written");
    // Undo from another view: the toast outlives the view.
    QQuickItem *undo = nullptr;
    check(waitFor([&] { return (undo = named(u"toastAction"_s)) != nullptr; }), "the toast offers Undo");
    app->navigate({ { u"view"_s, u"tasks"_s } });
    settle(250);
    undo = named(u"toastAction"_s);
    if (undo)
        tapOrClick(window, pointIn(undo), touch);
    check(undo && waitFor([&] { return ws->readFile(other) == u"- zolw w innej notatce\n"_s; }, 5000),
          "Undo restores the notes after leaving the view");
    closeMenus(window);
    ws->writeFile(note, QString());
    ws->deletePage(note);
    ws->deletePage(other);
    app->goHome();
}

/// Weekly and monthly notes (docs/SYNTAX.md §1): this week's note opened (not written by it), its
/// period under the title with the next week's arrow, the template on the first change; the
/// calendar's week number and title open the week's and the month's notes.
void checkPeriodicNotes(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const auto named = [&](const QString &name) { return shownNamed(window->contentItem(), name); };
    const auto routePath = [&] { return app->currentRoute().value(u"path"_s).toString(); };
    const QString tpl = ws->vaultConfig().value(u"weeklyTemplate"_s).toString();
    const bool hadTemplate = ws->exists(tpl);
    const QString oldTemplate = hadTemplate ? ws->readFile(tpl) : QString();
    ws->writeFile(tpl, u"# Tydzień {{week}}\n- \n"_s);
    const QVariantMap week = ws->periodicNote(u"week"_s, app->today());
    const QString weekPath = week.value(u"path"_s).toString();
    const bool hadWeek = week.value(u"exists"_s).toBool();
    app->openPeriodicNote(u"week"_s);
    check(waitFor([&] { return app->currentView() == u"page" && routePath() == weekPath; }, 3000),
          "this week's note opens");
    QQuickItem *label = nullptr;
    check(waitFor(
                  [&] {
                      label = named(u"periodicLabel"_s);
                      return label && label->property("text").toString() == week.value(u"label"_s).toString();
                  },
                  3000),
          "the week is named under the title");
    check(hadWeek || !ws->exists(weekPath), "looking at a week writes nothing");
    settle(400); // the view fades in
    QQuickItem *next = nullptr;
    check(waitFor([&] { return (next = named(u"periodicNext"_s)) != nullptr; }), "the next week's arrow is there");
    // (With a mouse its click as a signal: a pointer left hovering the header's button keeps its
    // tooltip, which takes the Back key of the checks after this one.)
    if (next && touch)
        tapOrClick(window, pointIn(next), touch);
    else if (next)
        QMetaObject::invokeMethod(next, "clicked");
    const QString nextPath = ws->periodicNote(u"week"_s, app->today(7)).value(u"path"_s).toString();
    check(waitFor([&] { return routePath() == nextPath; }, 3000), "the arrow opens the next week's note");

    // The calendar: a week number of the month grid, the title (the month's note).
    app->navigate({{u"view"_s, u"calendar"_s}, {u"mode"_s, u"month"_s}, {u"date"_s, app->today()}});
    QQuickItem *number = nullptr;
    check(waitFor([&] { return (number = named(u"calendarWeekNumber0"_s)) != nullptr; }, 3000),
          "the month grid has week numbers");
    if (number) {
        const QString monday = number->property("monday").toString();
        const QString expected = ws->periodicNote(u"week"_s, monday).value(u"path"_s).toString();
        tapOrClick(window, pointIn(number), touch);
        check(waitFor([&] { return app->currentView() == u"page" && routePath() == expected; }, 3000),
              "a click on a week number opens that week's note");
    }
    app->navigate({{u"view"_s, u"calendar"_s}, {u"mode"_s, u"month"_s}, {u"date"_s, app->today()}});
    QQuickItem *title = nullptr;
    check(waitFor([&] { return (title = named(u"calendarTitleNote"_s)) != nullptr; }, 3000),
          "the calendar's title opens a note");
    if (title) {
        const QString expected = ws->periodicNote(u"month"_s, app->today()).value(u"path"_s).toString();
        settle(400); // the view fades in
        tapOrClick(window, pointIn(title, 0.2, 0.5), touch);
        check(waitFor([&] { return app->currentView() == u"page" && routePath() == expected; }, 3000),
              "a click on the month's title opens its note");
    }
    // The month's new note is being written in (a page not written yet): leave it as a user would.
    sendKey(window, Qt::Key_Escape);
    sendKey(window, Qt::Key_Escape);
    if (hadTemplate)
        ws->writeFile(tpl, oldTemplate);
    else
        ws->deletePage(tpl);
    app->goHome();
    settle(300);
}

/// Habits (docs/UI.md "Habits"): the view from the palette's command, a habit added through its
/// dialog (written as a `habit::` block), today's check ticked and opened again with real input
/// (the line in today's journal), a tick written in the journal itself shown in the card, a past
/// day of the month grid ticked; the notebook is left as it was.
void checkHabits(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const auto named = [&](const QString &name) { return shownNamed(window->contentItem(), name); };
    const QString today = app->today();
    const QString yesterday = app->today(-1);
    const QString journal = ws->journalFileFor(today);
    const bool hadJournal = ws->exists(journal);
    const QString oldJournal = hadJournal ? ws->readFile(journal) : QString();
    const QString pastJournal = ws->journalFileFor(yesterday);
    const bool hadPast = ws->exists(pastJournal);
    const QString oldPast = hadPast ? ws->readFile(pastJournal) : QString();
    // The page new habits go to and the checks' tag follow the notes' language (Polish: "Nawyki",
    // #nawyk; English: "Habits", #habit).
    QHash<QString, QString> oldPages;
    for (const QString &title : {u"Nawyki"_s, u"Habits"_s}) {
        const QString path = ws->newPagePath(title);
        oldPages.insert(path, ws->exists(path) ? ws->readFile(path) : QString());
    }
    const auto line = [](const QString &box) {
        return QRegularExpression(u"- \\["_s + QRegularExpression::escape(box)
                                  + u"\\] Rozciąganie samotestu #(nawyk|habit)"_s);
    };

    check(app->commands() && app->commands()->run(u"go.habits"_s), "the palette's command opens Habits");
    check(waitFor([&] { return app->currentView() == u"habits"; }, 3000), "the Habits view shows");
    QQuickItem *add = nullptr;
    check(waitFor([&] { return (add = named(u"habitAdd"_s)) != nullptr; }, 3000), "it has “Dodaj nawyk”");
    settle(400); // the view fades in
    QQuickItem *habitsView = nullptr;
    for (QQuickItem *up = add; up && !habitsView; up = up->parentItem()) {
        if (isA(up, "HabitsView"))
            habitsView = up;
    }
    QObject *dialog = habitsView ? habitsView->findChild<QObject *>(u"habitAddDialog"_s) : nullptr;
    check(dialog != nullptr, "the view has its add dialog");
    // (With a mouse its click as a signal: a pointer left hovering the button keeps its tooltip.)
    if (add && touch)
        tapOrClick(window, pointIn(add), touch);
    else if (add)
        QMetaObject::invokeMethod(add, "clicked");
    check(waitFor([&] { return dialog && dialog->property("opened").toBool(); }, 3000), "the add dialog opens");
    QQuickItem *nameField = named(u"habitAddName"_s);
    QQuickItem *scheduleField = named(u"habitAddSchedule"_s);
    check(nameField && scheduleField, "it asks for a name and a schedule");
    if (!dialog || !nameField || !scheduleField) {
        if (dialog)
            QMetaObject::invokeMethod(dialog, "close");
        app->goHome();
        settle(300);
        return;
    }
    nameField->setProperty("text", u"Rozciąganie samotestu"_s);
    scheduleField->setProperty("text", u"co 2 tygodnie w poniedziałek"_s);
    check(!dialog->property("preview").toString().isEmpty(), "the schedule is previewed in words");
    scheduleField->setProperty("text", u"co drugi wtorek"_s);
    check(!dialog->property("acceptEnabled").toBool(), "a schedule not understood cannot be added");
    // A word a phone's keyboard still composes (preedit text, outside `text`) counts: one word
    // being typed is previewed and can be added.
    scheduleField->setProperty("text", QString());
    scheduleField->forceActiveFocus();
    composeWord(scheduleField, u"codziennie"_s);
    check(dialog->property("acceptEnabled").toBool() && !dialog->property("preview").toString().isEmpty(),
          "a schedule still being composed is previewed and can be added");
    QInputMethodEvent composed; // (the composition dropped: the text is set below)
    QCoreApplication::sendEvent(scheduleField, &composed);
    scheduleField->setProperty("text", u"codziennie"_s);
    check(dialog->property("acceptEnabled").toBool(), "a daily habit can be added");
    QMetaObject::invokeMethod(dialog, "accept");
    const QString key = u"rozciąganie samotestu"_s;
    QQuickItem *toggle = nullptr;
    check(waitFor([&] { return (toggle = named(u"habitToggle_"_s + key)) != nullptr; }, 3000),
          "the new habit has a card");
    bool written = false;
    for (auto it = oldPages.cbegin(); it != oldPages.cend(); ++it)
        written |= ws->readFile(it.key()).contains(u"- Rozciąganie samotestu\n  habit:: codziennie"_s);
    check(written, "it is written as a habit:: block");

    // Today's check: a tap writes the line, another one opens it again (once the dialog's closing
    // transition, whose dimmed overlay takes the clicks, is over).
    waitFor([&] { return !dialog->property("visible").toBool(); }, 3000);
    settle(200);
    // Today's check: a tap writes the line, another one opens it again.
    if (toggle)
        tapOrClick(window, pointIn(toggle), touch);
    check(waitFor([&] { return ws->readFile(journal).contains(line(u"x"_s)); }, 3000),
          "a tap ticks the habit in today's journal");
    check(waitFor([&] { return toggle && toggle->property("mark").toString() == u"done"_s; }, 3000),
          "the card shows today done");
    if (toggle)
        tapOrClick(window, pointIn(toggle), touch);
    check(waitFor([&] { return ws->readFile(journal).contains(line(u" "_s)); }, 3000),
          "a second tap opens the check again (the line stays)");
    // A tick written in the journal itself counts.
    ws->writeFile(journal, ws->readFile(journal).replace(u"- [ ] Rozciąganie samotestu #"_s,
                                                         u"- [x] Rozciąganie samotestu #"_s));
    check(waitFor([&] { return toggle && toggle->property("mark").toString() == u"done"_s; }, 3000),
          "a tick in the journal shows in the card");

    // Yesterday in the month grid (when it is this month's).
    if (yesterday.left(7) == today.left(7)) {
        QQuickItem *day = named(u"habitDay_"_s + key + u'_' + yesterday);
        check(day != nullptr, "the month grid has yesterday");
        if (day)
            tapOrClick(window, pointIn(day), touch);
        check(waitFor([&] { return ws->readFile(pastJournal).contains(line(u"x"_s)); }, 3000),
              "a click on a past day ticks it in that day's journal");
    }

    // Leave the notebook as it was.
    if (hadJournal)
        ws->writeFile(journal, oldJournal);
    else
        ws->deletePage(journal);
    if (hadPast)
        ws->writeFile(pastJournal, oldPast);
    else if (ws->exists(pastJournal))
        ws->deletePage(pastJournal);
    for (auto it = oldPages.cbegin(); it != oldPages.cend(); ++it) {
        if (!it.value().isEmpty())
            ws->writeFile(it.key(), it.value());
        else if (ws->exists(it.key()))
            ws->deletePage(it.key());
    }
    closeMenus(window);
    app->goHome();
    settle(300);
}

/// Focus mode (F8 — the command's shortcut —, Esc, the Back key, its corner button), the word count in
/// the page's corner and typewriter scrolling (desktop), and one command's shortcut changed in
/// Settings → Keyboard shortcuts with real keys: another command's key is refused with the reason,
/// the new one works and the old one no longer does, the palette shows it, and back to the default.
void checkFocusAndShortcuts(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    AppSettings *settings = app->settings();
    ShortcutMap *shortcuts = app->shortcuts();
    const auto named = [&](const QString &name) { return shownNamed(window->contentItem(), name); };
    // A key as a keyboard gives it: the window's Shortcut objects first.
    const auto press = [&](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const bool handled = uidriver::deliverKey(window, {key, modifiers, {}}, uidriver::KeyDelivery::System);
        settle(80);
        return handled;
    };

    // A page long enough to scroll: 60 blocks of 6 words.
    const QString note = u"Próba skupienia.md"_s;
    QString text;
    for (int i = 1; i <= 60; ++i)
        text += u"- Zdanie numer %1 z kilkoma słowami\n"_s.arg(i);
    ws->writeFile(note, text);
    app->navigate({{u"view"_s, u"page"_s}, {u"path"_s, note}});
    QObject *controller = nullptr;
    QQuickItem *editor = nullptr;
    check(waitFor([&] {
              editor = named(u"pageEditor"_s);
              controller = editor ? editor->property("controller").value<QObject *>() : nullptr;
              return controller && controller->property("loaded").toBool()
                     && controller->property("path").toString() == note;
          }),
          "the page for focus mode is open");
    if (!controller || !editor)
        return;
    settle(300);
    QVariantMap stats;
    QMetaObject::invokeMethod(controller, "pageStats", Q_RETURN_ARG(QVariantMap, stats));
    check(stats.value(u"words"_s).toInt() == 360 && stats.value(u"readingMinutes"_s).toInt() == 2,
          "the page counts 360 words, 2 minutes of reading");

    // ---- Focus mode: F8 (the command's default shortcut) hides the bars, the text column centred.
    check(named(u"shellTopBar"_s) != nullptr && named(u"focusModeExit"_s) == nullptr,
          "outside focus mode the top bar is shown");
    press(Qt::Key_F8);
    check(waitFor([&] { return settings->focusMode() && named(u"shellTopBar"_s) == nullptr; }),
          "F8 turns focus mode on: no top bar");
    check(settings->value(u"ui/focusMode"_s, false).toBool(), "focus mode is kept as this device's setting");
    check(named(u"focusModeExit"_s) != nullptr && named(u"shellBottomNav"_s) == nullptr,
          "in focus mode only its corner button is there, no bottom navigation");
    // (Waited for: the bars hide and the layout follows over a few frames — slower on Windows.)
    check(waitFor([&] {
              const qreal contentX = editor->property("contentX").toReal();
              const qreal contentWidth = editor->property("contentWidth").toReal();
              const QPointF editorAt = editor->mapToScene(QPointF(0, 0));
              return editorAt.x() < 1 && qAbs(editor->width() - window->width()) < 1
                     && (touch || qAbs(contentX + contentWidth / 2 - editor->width() / 2) <= 1);
          }),
          "in focus mode the editor fills the window, its text column centred");
    // Esc: what the view does not need ends it (a selected block first, if any).
    for (int i = 0; i < 3 && settings->focusMode(); ++i)
        sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !settings->focusMode() && named(u"shellTopBar"_s) != nullptr; }),
          "Esc leaves focus mode and the top bar is back");
    check(app->currentView() == u"page", "leaving focus mode stays on the page");
    // The corner button; the Back key (phones) — both end focus mode, nothing else happens.
    settings->setFocusMode(true);
    QQuickItem *exit = nullptr;
    check(waitFor([&] { return (exit = named(u"focusModeExit"_s)) != nullptr; }),
          "focus mode's corner button is shown");
    if (exit)
        tapOrClick(window, pointIn(exit), touch);
    check(waitFor([&] { return !settings->focusMode(); }), "the corner button leaves focus mode");
    settings->setFocusMode(true);
    settle(100);
    check(sendKey(window, Qt::Key_Back) && waitFor([&] { return !settings->focusMode(); })
                  && app->currentView() == u"page",
          "the Back key leaves focus mode first and stays on the page");
    // Not on other views: the setting stays, their bars show.
    settings->setFocusMode(true);
    app->navigate({{u"view"_s, u"tasks"_s}});
    check(waitFor([&] { return named(u"shellTopBar"_s) != nullptr; }), "other views keep their bars in focus mode");
    settings->setFocusMode(false);
    app->back();
    check(waitFor([&] { return app->currentView() == u"page" && (editor = named(u"pageEditor"_s)) != nullptr; }),
          "back on the page");

    // ---- The word count in the status bar (wide windows) follows the typing, after a pause; the
    //      edited block is there too; the caret's line stays in the middle with typewriter
    //      scrolling.
    if (!touch && editor) {
        controller = editor->property("controller").value<QObject *>();
        QQuickItem *footer = nullptr;
        check(waitFor([&] {
                  footer = named(u"statusBarWords"_s);
                  return footer && footer->property("text").toString().contains(u"360"_s);
              }),
              "the status bar shows the page's word count");
        settings->setTypewriterScrolling(true);
        const int last = 59;
        uidriver::callQml(editor, "startEditing", {QVariant(last), QVariant(-1)});
        QQuickItem *edit = nullptr;
        check(waitFor([&] {
                  edit = editor->property("activeEditor").value<QQuickItem *>();
                  return edit && edit->hasActiveFocus();
              }),
              "the last block is being edited");
        if (edit) {
            for (const QChar ch : u" dwa trzy"_s)
                sendKey(window, 0, Qt::NoModifier, QString(ch));
            QQuickItem *list = editor->property("listView").value<QQuickItem *>();
            const auto caretOffset = [&] {
                const QRectF caret = edit->property("cursorRectangle").toRectF();
                const QPointF at = edit->mapToItem(list, caret.center());
                const qreal middle = (list->height() - list->property("bottomMargin").toReal()) / 2;
                return qAbs(at.y() - middle);
            };
            const qreal lineHeight = editor->property("lineHeight").toReal();
            check(list && waitFor([&] { return caretOffset() <= lineHeight; }),
                  "typewriter scrolling keeps the caret's line in the middle of the view");
            check(waitFor(
                          [&] {
                              footer = named(u"statusBarWords"_s);
                              return footer && footer->property("text").toString().contains(u"362"_s);
                          },
                          5000),
                  "the word count follows what is typed (after a pause)");
            QQuickItem *position = named(u"statusBarPosition"_s);
            check(position && position->property("text").toString().contains(u"60"_s),
                  "the status bar shows the edited block");
            sendKey(window, Qt::Key_Escape);
            sendKey(window, Qt::Key_Escape);
        }
        settings->setTypewriterScrolling(false);
    }

    // ---- A custom shortcut, recorded in Settings → Keyboard shortcuts.
    app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"shortcuts"_s}});
    QQuickItem *change = nullptr;
    check(waitFor([&] { return (change = named(u"shortcutChange_journal.today"_s)) != nullptr; }, 5000),
          "the shortcuts' settings list today's journal");
    if (change) {
        settle(300);
        uidriver::scrollIntoView(window, change);
        settle(100);
        tapOrClick(window, pointIn(change), touch);
        check(waitFor([&] {
                  const QQuickItem *focus = window->activeFocusItem();
                  return focus && focus->objectName() == u"shortcutRecorder";
              }),
              "“Zmień” waits for the new shortcut");
        // Another command's shortcut (the palette's Ctrl+K): refused with the reason, nothing opens.
        sendKey(window, Qt::Key_K, Qt::ControlModifier);
        QQuickItem *message = nullptr;
        check(waitFor([&] {
                  message = named(u"shortcutMessage"_s);
                  return message && message->property("text").toString().contains(shortcuts->title(u"palette"_s))
                         && named(u"shortcutTakeOver"_s) != nullptr;
              }),
              "another command's shortcut is refused, saying whose it is");
        check(shortcuts->sequence(u"journal.today"_s) == u"Ctrl+J" && shortcuts->sequence(u"palette"_s) == u"Ctrl+K",
              "the refused shortcut changes nothing");
        // A key the editor uses: refused too.
        sendKey(window, Qt::Key_C, Qt::ControlModifier);
        check(waitFor([&] {
                  message = named(u"shortcutMessage"_s);
                  return message && message->property("text").toString().contains(u"Ctrl+C"_s);
              }),
              "the editor's own keys cannot be a command's shortcut");
        sendKey(window, Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        check(waitFor([&] { return shortcuts->sequence(u"journal.today"_s) == u"Ctrl+Alt+J"; }),
              "Ctrl+Alt+J becomes today's journal's shortcut");
        check(settings->shortcutOverrides().value(u"journal.today"_s).toString() == u"Ctrl+Alt+J",
              "the custom shortcut is kept as this device's setting");
        QString paletteShortcut;
        for (const QVariant &v : app->commands()->commands()) {
            if (v.toMap().value(u"id"_s).toString() == u"journal.today")
                paletteShortcut = v.toMap().value(u"shortcut"_s).toString();
        }
        check(paletteShortcut == u"Ctrl+Alt+J", "the palette shows the custom shortcut");
        // The keys: the new one opens today's journal, the old one no longer does.
        app->navigate({{u"view"_s, u"tasks"_s}});
        settle(300);
        press(Qt::Key_J, Qt::ControlModifier);
        check(app->currentView() == u"tasks", "the old shortcut no longer opens today's journal");
        press(Qt::Key_J, Qt::ControlModifier | Qt::AltModifier);
        check(waitFor([&] { return app->currentView() == u"journals" || app->currentView() == u"journal"; }),
              "the new shortcut opens today's journal");
        // Back to the default from its row.
        app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"shortcuts"_s}});
        QQuickItem *reset = nullptr;
        check(waitFor([&] { return (reset = named(u"shortcutReset_journal.today"_s)) != nullptr; }, 5000),
              "a custom shortcut can be restored to its default");
        if (reset) {
            settle(300);
            uidriver::scrollIntoView(window, reset);
            settle(100);
            tapOrClick(window, pointIn(reset), touch);
        }
        check(waitFor([&] { return shortcuts->sequence(u"journal.today"_s) == u"Ctrl+J"; })
                      && !shortcuts->isCustom(u"journal.today"_s),
              "“Przywróć” gives today's journal Ctrl+J again");
    }
    shortcuts->resetAll();
    ws->deletePage(note);
    app->goHome();
    settle(300);
}

/// A page's icon and cover (docs/UI.md "Page icon and cover"): the page menu's "Add icon…"
/// opens the icon dialog, an emoji picked there is written into the page's properties (that line
/// only) and shown before the title and in the sidebar's tree; "Add cover…" takes a picture of
/// the notebook for a band at the top of the page; both removed from their dialogs, the file as it
/// was.
void checkPageLook(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const auto named = [&](const QString &name) { return shownNamed(window->contentItem(), name); };
    const QString page = u"Wygląd strony próba.md"_s;
    const QString text = u"---\ntags: [próba]\n---\n- Zażółć gęślą jaźń\n"_s;
    ws->writeFile(page, text);
    QImage picture(64, 32, QImage::Format_RGB32);
    picture.fill(Qt::darkCyan);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    const QString cover = ws->createAttachment(u"assets/Wygląd okładka.png"_s, buffer.data());
    check(!cover.isEmpty(), "a picture for the cover is in the notebook");
    app->openPage(page);
    const auto openMenu = [&]() {
        QQuickItem *menuButton = nullptr;
        waitFor(
                [&] {
                    QQuickItem *header = findShown(window->contentItem(), [&](QQuickItem *item) {
                        return isA(item, "ContentPageHeader") && item->property("path").toString() == page;
                    });
                    menuButton = header ? header->property("menuAnchor").value<QQuickItem *>() : nullptr;
                    return menuButton && menuButton->isVisible();
                },
                3000);
        if (menuButton)
            tapOrClick(window, pointIn(menuButton), touch);
    };
    const auto menuItem = [&](const QString &name, const char *what) {
        QQuickItem *item = nullptr;
        check(waitFor([&] { return (item = named(name)) != nullptr; }), what);
        if (item)
            tapOrClick(window, pointIn(item), touch);
        return item != nullptr;
    };
    settle(400); // the view fades in
    openMenu();
    if (menuItem(u"pageMenuIcon"_s, "the page menu offers “Dodaj ikonę…”")) {
        QQuickItem *emojis = nullptr;
        check(waitFor([&] { return (emojis = named(u"pageIconEmojis"_s)) != nullptr; }, 3000),
              "“Dodaj ikonę…” opens the icon dialog with its emoji");
        settle(300); // the dialog opens
        QQuickItem *first = nullptr;
        if (emojis) {
            for (QQuickItem *child : emojis->childItems()) {
                if (child->property("emoji").toString() == u"📝"_s)
                    first = child;
            }
        }
        // The choice draws its emoji (an icon that compiled to undefined left the grid blank).
        const auto *firstIcon = first ? first->property("contentItem").value<QQuickItem *>() : nullptr;
        check(firstIcon && firstIcon->property("emoji").toBool()
                      && firstIcon->property("_name").toString() == u"📝"_s,
              "the dialog's emoji are drawn (BPageIcon of 📝)");
        if (first)
            tapOrClick(window, pointIn(first), touch);
        check(first
                      && waitFor(
                              [&] {
                                  return ws->readFile(page)
                                         == u"---\ntags: [próba]\nicon: 📝\n---\n- Zażółć gęślą jaźń\n"_s;
                              },
                              5000),
              "a picked emoji is written as the page's `icon`, nothing else changes");
        check(waitFor([&] { return named(u"pageIconEmojis"_s) == nullptr; }, 3000), "the icon dialog closes");
        settle(400);
        check(waitFor([&] { return named(u"pageIconButton"_s) != nullptr; }, 3000), "the icon stands before the title");
        check(waitFor([&] { return ws->pageIconOf(page).value(u"name"_s).toString() == u"📝"_s; }, 3000),
              "the lists have it (Workspace.pageIconOf)");
        if (!touch) {
            check(waitFor(
                          [&] {
                              return findShown(window->contentItem(),
                                               [&](QQuickItem *item) {
                                                   return item->objectName() == u"treeRow"
                                                          && item->property("path").toString() == page
                                                          && item->property("iconEmoji").toString() == u"📝"_s;
                                               })
                                     != nullptr;
                          },
                          3000),
                  "the sidebar's tree shows the page's emoji");
        }
    }
    closeMenus(window);
    openMenu();
    if (menuItem(u"pageMenuCover"_s, "the page menu offers “Dodaj okładkę…”")) {
        QQuickItem *pictures = nullptr;
        QQuickItem *tile = nullptr;
        check(waitFor(
                      [&] {
                          pictures = named(u"pageCoverPictures"_s);
                          if (!pictures)
                              return false;
                          for (QQuickItem *child : pictures->childItems()) {
                              if (child->property("path").toString() == cover)
                                  tile = child;
                          }
                          return tile != nullptr;
                      },
                      3000),
              "the cover dialog shows the notebook's picture");
        settle(300);
        if (tile)
            tapOrClick(window, pointIn(tile), touch);
        check(waitFor(
                      [&] {
                          const QString now = ws->readFile(page);
                          return now.contains(u"\nbanner: \"![["_s) && now.contains(u"Wygląd okładka.png]]\"\n"_s);
                      },
                      5000),
              "the picture is written as the page's `banner`");
        QQuickItem *band = nullptr;
        check(waitFor(
                      [&] {
                          band = named(u"pageCover"_s);
                          QQuickItem *image = band ? shownNamed(band, u"pageCoverImage"_s) : nullptr;
                          return image && image->property("status").toInt() == 1; // Image.Ready
                      },
                      5000),
              "the cover is a band at the top of the page");
        waitFor([&] { return named(u"pageCoverPictures"_s) == nullptr; }, 3000);
        settle(400);
    }
    closeMenus(window);
    // Both removed from their dialogs: the file as it was.
    openMenu();
    if (menuItem(u"pageMenuIcon"_s, "“Zmień ikonę…”")) {
        QQuickItem *remove = nullptr;
        check(waitFor([&] { return (remove = named(u"pageIconRemove"_s)) != nullptr; }, 3000), "“Usuń ikonę”");
        settle(300);
        if (remove)
            tapOrClick(window, pointIn(remove), touch);
        check(waitFor([&] { return named(u"pageIconEmojis"_s) == nullptr; }, 3000), "the icon dialog closes");
        settle(400); // its exit transition (the modal overlay) is over
    }
    closeMenus(window);
    openMenu();
    if (menuItem(u"pageMenuCover"_s, "“Zmień okładkę…”")) {
        QQuickItem *remove = nullptr;
        check(waitFor([&] { return (remove = named(u"pageCoverRemove"_s)) != nullptr; }, 3000), "“Usuń okładkę”");
        settle(300);
        if (remove)
            tapOrClick(window, pointIn(remove), touch);
        waitFor([&] { return named(u"pageCoverPictures"_s) == nullptr; }, 3000);
        settle(400);
    }
    check(waitFor([&] { return ws->readFile(page) == text; }, 5000),
          "removing the icon and the cover leaves the file as it was");
    closeMenus(window);
    app->goHome();
    settle(300);
    ws->deletePage(page);
    ws->deletePage(cover);
}

void checkCleanup(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    const QString note = u"Sprzątanie stara notatka.md"_s;
    const QString picture = u"assets/Sprzątanie żółw.png"_s;
    const QString linkedPicture = u"assets/Sprzątanie w użyciu.png"_s;
    const auto offered = [ws](const QString &path) {
        const QVariantList files = ws->unreferencedFiles();
        return std::any_of(files.cbegin(), files.cend(),
                           [&](const QVariant &f) { return f.toMap().value(u"path"_s).toString() == path; });
    };
    ws->writeFile(note, u"- [ ] zadanie do zrobienia\n"_s);
    ws->writeFile(u"Sprzątanie z obrazem.md"_s, u"- ![[Sprzątanie w użyciu.png]]\n"_s);
    ws->createAttachment(picture, "png");
    ws->createAttachment(linkedPicture, "png");
    check(waitFor([&] { return offered(note) && offered(picture); }), "files nothing refers to are offered");
    check(!offered(linkedPicture), "an embedded picture is not offered");

    app->navigate({ { u"view"_s, u"cleanup"_s } });
    QQuickItem *list = nullptr;
    QQuickItem *view = nullptr;
    const auto tickedCount = [&] {
        return view ? view->property("shownTicked").toList().size() : -1;
    };
    check(waitFor([&] {
              list = shownNamed(window->contentItem(), u"cleanupList"_s);
              return list != nullptr && list->property("count").toInt() > 1;
          }),
          "the cleanup view lists the files");
    for (QQuickItem *up = list; up; up = up->parentItem()) {
        if (isA(up, "CleanupView")) {
            view = up;
            break;
        }
    }
    check(view && tickedCount() == 0, "nothing is ticked at first");
    QQuickItem *trashButton = shownNamed(window->contentItem(), u"cleanupTrash"_s);
    check(trashButton && !trashButton->isEnabled(), "with nothing ticked nothing can be moved to the trash");

    const auto rowOf = [&](const QString &title) -> QQuickItem * {
        const QVariantList shownFiles = view ? view->property("shown").toList() : QVariantList();
        for (int i = 0; i < shownFiles.size(); ++i) {
            if (shownFiles[i].toMap().value(u"title"_s).toString() == title)
                QMetaObject::invokeMethod(list, "positionViewAtIndex", i, 4 /* ListView.Contain */);
        }
        settle(100);
        return findShown(list, [&](QQuickItem *item) {
            return isA(item, "BListItem") && item->property("text").toString() == title;
        });
    };
    QQuickItem *noteRow = rowOf(u"Sprzątanie stara notatka"_s);
    QQuickItem *pictureRow = rowOf(u"Sprzątanie żółw.png"_s);
    check(noteRow && pictureRow, "both rows are in the list");
    if (!noteRow || !pictureRow || !view)
        return;
    check(noteRow->property("subtitle").toString().contains(u'1'), "the note's row says it holds an open task");
    tapOrClick(window, pointIn(pictureRow), touch);
    check(waitFor([&] { return tickedCount() == 1; }), "a tap on a row ticks it");
    tapOrClick(window, pointIn(rowOf(u"Sprzątanie stara notatka"_s)), touch);
    check(waitFor([&] { return tickedCount() == 2; }), "a second row is ticked too");

    // The row's button opens the file; Back returns with the ticks.
    noteRow = rowOf(u"Sprzątanie stara notatka"_s);
    QQuickItem *openButton = noteRow ? findShown(noteRow, [](QQuickItem *item) { return isA(item, "BIconButton"); }) : nullptr;
    check(openButton != nullptr, "the row has an open button");
    if (openButton)
        tapOrClick(window, pointIn(openButton), touch);
    check(waitFor([&] { return app->currentView() == u"page" && app->currentRoute().value(u"path"_s).toString() == note; }),
          "the row's button opens the file");
    app->back();
    view = nullptr;
    check(waitFor([&] {
              list = shownNamed(window->contentItem(), u"cleanupList"_s);
              for (QQuickItem *up = list; up && !view; up = up->parentItem()) {
                  if (isA(up, "CleanupView"))
                      view = up;
              }
              return view && tickedCount() == 2;
          }),
          "Back returns with the ticks kept");

    // Some ticked: "Select all" ticks all that is shown; pressed again it unticks them.
    QQuickItem *tickAll = shownNamed(window->contentItem(), u"cleanupTickAll"_s);
    const qsizetype all = view->property("shown").toList().size();
    check(tickAll && all > 2, "the view offers to tick everything shown");
    if (tickAll) {
        tapOrClick(window, pointIn(tickAll, 0.1), touch);
        check(waitFor([&] { return tickedCount() == all; }), "“Select all” from some ticked ticks every file shown");
        settle(500); // no double click
        tapOrClick(window, pointIn(tickAll, 0.1), touch);
        check(waitFor([&] { return tickedCount() == 0; }), "pressed again it unticks them");
        settle(500);
        tapOrClick(window, pointIn(rowOf(u"Sprzątanie stara notatka"_s)), touch);
        settle(500);
        tapOrClick(window, pointIn(rowOf(u"Sprzątanie żółw.png"_s)), touch);
        check(waitFor([&] { return tickedCount() == 2; }), "rows are ticked again");
    }

    // Only what is shown goes: attachments only, the note stays although ticked.
    settle(200); // the view made anew by Back lays out its segments
    QQuickItem *kind = shownNamed(window->contentItem(), u"cleanupKind"_s);
    check(kind != nullptr, "the kind filter is shown");
    const QString attachments = QCoreApplication::translate("CleanupView", "Attachments (%1)").section(u'(', 0, 0);
    QQuickItem *attachmentsTab = kind ? findShown(kind, [&](QQuickItem *item) {
        return item->property("text").toString().startsWith(attachments);
    }) : nullptr;
    check(attachmentsTab != nullptr, "the filter has a segment for attachments");
    if (attachmentsTab)
        tapOrClick(window, pointIn(attachmentsTab), touch);
    check(waitFor([&] { return view->property("kind").toString() == u"attachments" && tickedCount() == 1; }),
          "the filter shows attachments; a ticked note is not counted");
    trashButton = shownNamed(window->contentItem(), u"cleanupTrash"_s);
    check(trashButton && trashButton->isEnabled(), "something ticked can go to the trash");
    if (trashButton)
        tapOrClick(window, pointIn(trashButton), touch);
    const QString accept = QCoreApplication::translate("CleanupView", "Move to trash");
    QQuickItem *acceptButton = nullptr;
    check(waitFor([&] {
              QQuickItem *overlay = window->contentItem();
              acceptButton = findShown(overlay, [&](QQuickItem *item) {
                  return isA(item, "BButton") && item != trashButton && item->property("text").toString() == accept;
              });
              return acceptButton != nullptr;
          }),
          "the confirmation asks first");
    check(ws->exists(picture), "nothing is moved before the confirmation");
    settle(250);
    if (acceptButton)
        tapOrClick(window, pointIn(acceptButton), touch);
    check(waitFor([&] { return !ws->exists(picture); }), "the confirmed picture is in the trash");
    check(ws->exists(note), "the note filtered out stays");
    check(waitFor([&] { return !offered(picture); }), "the list no longer has the picture");

    // Undo from another view: the toast outlives the cleanup view.
    QQuickItem *undo = nullptr;
    check(waitFor([&] {
              undo = shownNamed(window->contentItem(), u"toastAction"_s);
              return undo != nullptr;
          }),
          "the toast offers Undo");
    app->navigate({ { u"view"_s, u"trash"_s } });
    settle(250);
    undo = shownNamed(window->contentItem(), u"toastAction"_s);
    if (undo)
        tapOrClick(window, pointIn(undo), touch);
    check(undo && waitFor([&] { return ws->exists(picture); }), "Undo restores the file after leaving the view");

    // Leave the sample notebook as it was for the checks after this one.
    for (const QString &path : { note, picture, linkedPicture, u"Sprzątanie z obrazem.md"_s })
        ws->deleteForever(ws->deletePage(path));
    closeMenus(window);
    app->goHome();
}

/// The sync center's conflicts: on a narrow window (the phone run) a conflict opened from the list
/// goes through the route, so Back returns to the list, not out of the sync center — also when
/// Conflicts was reached by the overview's "Merge" from a route with another tab —, and "Keep mine"
/// goes back to the list too. On a wide window the list stays beside the resolver (no route).
void checkSyncConflicts(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    WorkspaceSync *sync = ws->sync();
    const QString note = u"Zakupy na weekend.md"_s;
    const QString copy = u"Zakupy na weekend.sync-conflict-20261013-184500-LAPTOP7.md"_s;
    ws->writeFile(note, u"- mleko\n- chleb żytni\n"_s);
    ws->writeFile(copy, u"- mleko\n- chleb\n- ser żółty\n"_s);
    check(sync && waitFor([&] { return sync->conflictCopies().contains(copy); }, 5000),
          "a conflict copy waits in the sync center");
    if (!sync)
        return;
    const auto view = [app] { return app->currentView(); };
    const auto routeCopy = [app] { return app->currentRoute().value(u"copy"_s).toString(); };
    const auto shownType = [window](const char *type) {
        return findShown(window->contentItem(), [type](QQuickItem *item) { return isA(item, type); });
    };
    // A button shown with this text (maybe below the fold of a phone's page), and a press on it.
    const auto shownButton = [window](const QString &text) {
        return uidriver::topmost(window, [&text](QQuickItem *item) {
            return isA(item, "BButton") && item->property("text").toString() == text;
        });
    };
    const auto press = [&](QQuickItem *item) {
        uidriver::scrollIntoView(window, item);
        tapOrClick(window, pointIn(item), touch);
    };
    // A conflict's row in the list.
    const auto conflictRow = [&]() -> QQuickItem * {
        QQuickItem *list = shownType("SyncConflictList");
        return list ? findShown(list, [](QQuickItem *item) { return isA(item, "BListItem"); }) : nullptr;
    };
    const bool narrow = [&] {
        app->navigate({{u"view"_s, u"sync"_s}, {u"tab"_s, u"nextcloud"_s}});
        settle(300);
        app->navigate({{u"view"_s, u"sync"_s}, {u"tab"_s, u"overview"_s}});
        settle(400);
        QQuickItem *syncView = shownType("SyncView");
        return syncView && syncView->width() < 900;
    }();
    // The overview's "Merge" chooses the Conflicts tab here, not in the route.
    QQuickItem *merge = nullptr;
    check(waitFor([&] { return (merge = shownButton(QCoreApplication::translate("SyncView", "Merge"))) != nullptr; }),
          "the sync center's overview offers to merge the conflict");
    if (merge)
        press(merge);
    QQuickItem *row = nullptr;
    check(waitFor([&] { return (row = conflictRow()) != nullptr; }), "“Merge” shows the list of conflicts");
    if (row)
        tapOrClick(window, pointIn(row), touch);
    check(waitFor([&] { return shownType("SyncConflictResolver") != nullptr; }), "a tap on a conflict opens it");
    if (narrow) {
        check(routeCopy() == copy && conflictRow() == nullptr,
              "on a narrow window the conflict opens through the route, in place of the list");
        sendKey(window, Qt::Key_Back);
        check(waitFor([&] { return view() == u"sync" && routeCopy().isEmpty() && conflictRow() != nullptr; })
                      && shownType("SyncConflictResolver") == nullptr,
              "Back from a conflict returns to the list, in the sync center");
        if ((row = conflictRow()))
            tapOrClick(window, pointIn(row), touch);
        waitFor([&] { return routeCopy() == copy; });
    } else {
        check(routeCopy().isEmpty() && conflictRow() != nullptr, "on a wide window the list stays beside the conflict");
    }
    // "Keep mine": the copy goes to the trash; a narrow window goes back to the list.
    QQuickItem *keep = nullptr;
    check(waitFor([&] {
              return (keep = shownButton(QCoreApplication::translate("SyncConflictResolver", "Keep mine"))) != nullptr;
          }),
          "the conflict offers “Keep mine”");
    if (keep)
        press(keep);
    check(waitFor([&] { return !sync->conflictCopies().contains(copy); }) && waitFor([&] {
              return view() == u"sync" && routeCopy().isEmpty() && shownType("SyncConflictResolver") == nullptr;
          }),
          "a resolved conflict leaves the resolver for the list, in the sync center");

    // The notebook as it was.
    for (const QVariant &item : ws->trashItems()) {
        const QVariantMap entry = item.toMap();
        if (entry.value(u"original"_s).toString() == copy)
            ws->deleteForever(entry.value(u"trashPath"_s).toString());
    }
    ws->deleteForever(ws->deletePage(note));
    app->goHome();
}

/// "Backups": the settings' section with real input: a backup made with its button, the row of
/// the list, "Restore" through the confirmation with its counts, the note back; "Delete" asks first.
void checkBackups(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    Workspace *ws = app->workspace();
    WorkspaceBackups *backups = ws->backups();
    const QString note = u"Kopia żółwia.md"_s;
    ws->writeFile(note, u"- z kopii\n"_s);

    app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"backups"_s}});
    // The section may lie below the fold of the narrow layout's one column: brought into view.
    const auto reveal = [&](QQuickItem *item) {
        if (!item)
            return;
        for (QQuickItem *up = item->parentItem(); up; up = up->parentItem()) {
            if (!up->inherits("QQuickFlickable"))
                continue;
            const qreal y = item->mapToItem(up->property("contentItem").value<QQuickItem *>(), QPointF(0, 0)).y();
            const qreal maxY = qMax<qreal>(0, up->property("contentHeight").toReal() - up->height());
            up->setProperty("contentY", qBound<qreal>(0, y - up->height() / 3, maxY));
            break;
        }
        settle(100);
    };
    QQuickItem *create = nullptr;
    check(waitFor([&] {
              create = shownNamed(window->contentItem(), u"backupsCreateNow"_s);
              return create != nullptr;
          }),
          "the settings show the backups' section");
    if (!create)
        return;
    const qsizetype before = backups->backups().size();
    reveal(create);
    settle(1200); // the narrow layout's own scroll to the route's section has ended
    reveal(create);
    tapOrClick(window, pointIn(create), touch);
    check(waitFor([&] { return backups->backups().size() == before + 1 && !backups->running(); }),
          "“Create a backup now” adds a backup to the list");
    check(backups->status() == u"ok", "the status says it worked");

    QQuickItem *row = nullptr;
    check(waitFor([&] {
              row = shownNamed(window->contentItem(), u"backupRow"_s);
              return row != nullptr;
          }),
          "the backup has a row");
    QQuickItem *restore =
            row ? findShown(row, [](QQuickItem *i) { return i->objectName() == u"backupRestore"; }) : nullptr;
    check(restore != nullptr, "the row has “Restore”");
    if (!restore)
        return;

    ws->writeFile(note, u"- zmieniona po kopii\n"_s);
    ws->writeFile(u"Kopia nowa.md"_s, u"- po kopii\n"_s);
    reveal(restore);
    tapOrClick(window, pointIn(restore), touch);
    const QString accept = QCoreApplication::translate("ShellBackupSettings", "Restore");
    QQuickItem *acceptButton = nullptr;
    check(waitFor([&] {
              acceptButton = findShown(window->contentItem(), [&](QQuickItem *item) {
                  return isA(item, "BButton") && item->property("text").toString() == accept;
              });
              return acceptButton != nullptr;
          }),
          "a restore asks first, with its counts");
    check(ws->readFile(note) == u"- zmieniona po kopii\n", "nothing is restored before the confirmation");
    settle(250);
    if (acceptButton)
        tapOrClick(window, pointIn(acceptButton), touch);
    check(waitFor([&] { return ws->readFile(note) == u"- z kopii\n" && !backups->running(); }),
          "the confirmed restore brings the note back");
    check(!ws->exists(u"Kopia nowa.md"_s), "what the backup does not hold goes to the trash");
    check(waitFor([&] { return backups->backups().size() >= before + 2; }), "a safety backup was made first");
    closeMenus(window);

    // Delete asks first too.
    settle(300);
    const qsizetype listed = backups->backups().size();
    row = shownNamed(window->contentItem(), u"backupRow"_s);
    QQuickItem *remove =
            row ? findShown(row, [](QQuickItem *i) { return i->objectName() == u"backupDelete"; }) : nullptr;
    check(remove != nullptr, "the row has “Delete”");
    if (remove) {
        reveal(remove);
        tapOrClick(window, pointIn(remove), touch);
        const QString confirm = QCoreApplication::translate("ShellBackupSettings", "Delete");
        QQuickItem *confirmButton = nullptr;
        check(waitFor([&] {
                  confirmButton = findShown(window->contentItem(), [&](QQuickItem *item) {
                      return isA(item, "BButton") && item->property("text").toString() == confirm;
                  });
                  return confirmButton != nullptr;
              }),
              "deleting a backup asks first");
        check(backups->backups().size() == listed, "nothing is deleted before the confirmation");
        settle(250);
        if (confirmButton)
            tapOrClick(window, pointIn(confirmButton), touch);
        check(waitFor([&] { return backups->backups().size() == listed - 1; }), "the confirmed backup is deleted");
    }

    // Leave the sample notebook as it was for the checks after this one.
    ws->deleteForever(ws->deletePage(note));
    closeMenus(window);
    app->goHome();
}

void checkShare(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    QObject *dialog = window->findChild<QObject *>(u"shellShare"_s);
    if (!dialog) {
        check(false, "the share dialog exists");
        return;
    }
    Workspace *ws = app->workspace();
    const QString today = ws->journalPath(app->today(0));
    const QString journal = ws->readFile(today);
    const QString saveText = QCoreApplication::translate("ShellShareDialog", "Save");
    const auto save = [&] {
        // The dialog's own "Save" (its footer): a view behind it may have a button of that name.
        auto *footer = qvariant_cast<QQuickItem *>(dialog->property("footer"));
        QQuickItem *button = !footer ? nullptr : findShown(footer, [&](QQuickItem *item) {
            return isA(item, "BButton") && item->property("text").toString() == saveText;
        });
        if (!button || !button->isEnabled())
            return false;
        tapOrClick(window, button->mapToScene(QPointF(button->width() / 2, button->height() / 2)), touch);
        return true;
    };
    const auto opened = [&] { return waitFor([&] { return dialog->property("opened").toBool(); }); };
    const auto closed = [&] { return waitFor([&] { return !dialog->property("visible").toBool(); }); };

    // Text (a browser's page): the journal, as one block, without date parsing.
    app->receiveShare({{u"subject"_s, u"Przepis na żurek jutro"_s}, {u"text"_s, u"https://kuchnia.pl/żurek"_s}});
    check(opened(), "a share opens the share dialog");
    check(save() && closed(), "“Zapisz” saves the shared text and closes the dialog");
    const bool block = waitFor(
            [&] { return ws->readFile(today).endsWith(u"- [Przepis na żurek jutro](https://kuchnia.pl/żurek)\n"_s); });
    if (!block)
        std::fprintf(stderr, "selftest: today's journal (%s) ends with: %s\n", qPrintable(today),
                     qPrintable(ws->readFile(today).right(200)));
    // (The sample journal has reminders of its own: the block's line itself, exactly, has none.)
    check(block, "the text is a block of today's journal, no reminder");
    if (ws->readFile(today) != journal)
        ws->writeFile(today, journal);

    // A .md file: a new page named after it, its bytes kept, its copy removed.
    const QString dir = app->shareInboxPath() + u"/1792000000000-1"_s;
    const QByteArray bytes = "# Łódź\r\n\r\n- [ ] dworzec\r\n";
    QDir().mkpath(dir + u"/0"_s);
    QFile file(dir + u"/0/Łódź.md"_s);
    const bool written = file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    file.close();
    app->receiveShare(
            {{u"dir"_s, dir},
             {u"files"_s, QVariantList{QVariantMap{{u"path"_s, file.fileName()}, {u"name"_s, u"Łódź.md"_s}}}}});
    check(written && opened(), "a shared file opens the share dialog");
    QQuickItem *title = shownNamed(window->contentItem(), u"shareTitle"_s);
    check(title && title->property("text").toString() == u"Łódź"_s, "a new page titled after the file is offered");
    check(save() && closed(), "“Zapisz” saves the page");
    const QString page = app->currentRoute().value(u"path"_s).toString();
    check(page.endsWith(u"Łódź.md"_s), "the new page opens");
    QFile saved(QDir(ws->rootPath()).filePath(page));
    check(saved.open(QIODevice::ReadOnly) && saved.readAll() == bytes, "its bytes are the file's (CRLF too)");
    saved.close();
    check(!QFileInfo::exists(dir), "the shared file's copy is removed");
    if (page.endsWith(u"Łódź.md"_s))
        ws->deletePage(page);

    // A file opened with Brulion from outside the notebook ("Open with", --open): the
    // dialog says a copy is saved and the file stays; "Save" saves the page, the file is left
    // as it was.
    QTemporaryDir outside;
    const QString openedPath = outside.filePath(u"Przepis na pierogi.md"_s);
    const QByteArray openedBytes = "# Pierogi\r\n\r\n- mąka\r\n- ser\r\n";
    QFile openedFile(openedPath);
    const bool openedWritten = outside.isValid() && openedFile.open(QIODevice::WriteOnly)
                               && openedFile.write(openedBytes) == openedBytes.size();
    openedFile.close();
    app->receiveShare(
            notebook::sharedContentToJson(notebook::openedFilesShare({openedPath}, QString())).toVariantMap());
    check(openedWritten && opened(), "a file opened with Brulion opens the share dialog");
    const QString openedSubtitle =
            QCoreApplication::translate("ShellShareDialog", "%n file(s) to save in the notebook", "", 1);
    check(dialog->property("subtitle").toString() == openedSubtitle,
          "its subtitle says the file is saved in the notebook");
    check(shownNamed(window->contentItem(), u"shareCopyHint"_s) != nullptr,
          "the dialog says a copy is saved and the file stays");
    check(save() && closed(), "“Zapisz” saves the opened file as a page");
    const QString openedPage = app->currentRoute().value(u"path"_s).toString();
    check(openedPage.endsWith(u"Przepis na pierogi.md"_s), "the opened file's page opens");
    QFile openedCopy(QDir(ws->rootPath()).filePath(openedPage));
    check(openedCopy.open(QIODevice::ReadOnly) && openedCopy.readAll() == openedBytes, "the page has the file's bytes");
    openedCopy.close();
    openedFile.setFileName(openedPath);
    check(openedFile.open(QIODevice::ReadOnly) && openedFile.readAll() == openedBytes,
          "the opened file stays where it was, unchanged");
    openedFile.close();
    if (openedPage.endsWith(u"Przepis na pierogi.md"_s))
        ws->deletePage(openedPage);

    // Escape drops a share: nothing written.
    app->receiveShare({{u"text"_s, u"Nie zapisuj mnie"_s}});
    check(opened(), "another share opens the dialog");
    sendKey(window, Qt::Key_Escape);
    check(closed() && app->pendingShare().isEmpty() && !ws->readFile(today).contains(u"Nie zapisuj mnie"_s),
          "Escape drops the share and writes nothing");
    app->goHome();
    if (QQuickItem *toasts = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BToastHost"); }))
        QMetaObject::invokeMethod(toasts, "clear");
}

/// The tray calendar (ShellTrayCalendar, AppController::showTrayCalendar; desktop): a frameless
/// tool window without an owner, placed inside the work area next to the icon; today's date, a
/// day's tasks and reminders from the sample notebook; Escape, a second click on the icon and the
/// activation going elsewhere hide it, a click right after such a hide keeps it closed; "Open
/// journal" hides it and shows that day in the main window; with the main window hidden in the
/// tray, opening and closing it (also Alt+F4's close) never ends the app.
/// The page windows shown (ShellPageWindow, docs/UI.md "Tabs, split view and windows").
QList<QQuickWindow *> pageWindows()
{
    QList<QQuickWindow *> out;
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows) {
        if (w->objectName() == u"shellPageWindow" && w->isVisible())
            out.append(qobject_cast<QQuickWindow *>(w));
    }
    out.removeAll(nullptr);
    return out;
}

/// Tabs, the split view and pages in windows of their own (docs/UI.md "Tabs, split view and
/// windows"). Desktops: the shortcuts with real keys (Ctrl+T, Ctrl+Tab, Ctrl+Shift+Tab, Ctrl+1 /
/// Ctrl+9, Ctrl+W), each tab's own history, a middle click and a Ctrl+click on a page in the
/// sidebar, a middle click on a link in the editor and the link menu's "Open in new tab", a
/// tab dragged in the bar, a tab keeping where its page was scrolled, only the tabs shown having
/// an editor, the split view (a click focuses a pane; Ctrl+W on its tab closes it) and a page in a
/// window of its own that sees the main window's edits. Phones: no bar, the top bar's tab switcher.
void checkTabs(QQuickWindow *window, AppController *app, const Check &check)
{
    const bool narrow = window->width() < 900;
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const auto key = [window](int k, Qt::KeyboardModifiers modifiers = Qt::ControlModifier) {
        uidriver::deliverKey(window, {k, modifiers, {}}, uidriver::KeyDelivery::System);
        settle(120);
    };
    const auto named = [window](const QString &name) { return shownNamed(window->contentItem(), name); };
    const auto path = [app] { return app->currentRoute().value(u"path"_s).toString(); };
    const auto pageRoute = [](const QString &p) { return QVariantMap{{u"view"_s, u"page"_s}, {u"path"_s, p}}; };
    // The page controllers (an editor each) of `w` showing `p` — of views on screen: a view the
    // shell replaced waits for its deferred deletion, which never comes while the self-test runs.
    const auto controllers = [](QQuickWindow *w, const QString &p) {
        QList<QObject *> out;
        const QList<QObject *> found = w->findChildren<QObject *>(u"pageController"_s);
        for (QObject *c : found) {
            auto *view = qobject_cast<QQuickItem *>(c->parent());
            if (c->property("path").toString() == p && view && view->window() == w && view->isVisible())
                out.append(c);
        }
        return out;
    };
    const auto loadedIn = [&](QQuickWindow *w, const QString &p) {
        return waitFor([&] {
            const QList<QObject *> found = controllers(w, p);
            return !found.isEmpty() && found.first()->property("loaded").toBool();
        });
    };
    const QString guide = u"Witaj w Brulionie.md"_s;
    const QString renovation = u"Projekty/Remont mieszkania.md"_s;

    // One tab at home to start with.
    app->closeOtherTabs(app->focusedTab());
    app->closeSplit();
    app->goHome();
    settle(200);
    check(app->tabCount() == 1 && !named(u"shellTabBar"_s) && !named(u"topBarTabs"_s),
          "one tab: no tab bar and no tab switcher");

    if (narrow) {
        // ---- Phones: the tab switcher in the top bar.
        app->openPage(guide);
        const int first = app->focusedTab();
        app->openTab(pageRoute(renovation));
        QQuickItem *button = nullptr;
        check(waitFor([&] { return (button = named(u"topBarTabs"_s)) != nullptr; }) && !named(u"shellTabBar"_s),
              "phones: two tabs show the tab switcher in the top bar, no tab bar");
        if (button)
            tapOrClick(window, pointIn(button), touch);
        check(waitFor([&] { return openMenus(window) == 1; }), "the tab switcher opens its menu");
        QList<QQuickItem *> entries;
        waitFor([&] {
            entries.clear();
            for (QQuickItem *item : uidriver::allItems(window->contentItem())) {
                if (item->objectName() == u"tabSwitcherEntry" && shown(item))
                    entries.append(item);
            }
            return entries.size() == 2;
        });
        check(entries.size() == 2, "the tab switcher lists both tabs");
        if (!entries.isEmpty()) {
            std::sort(entries.begin(), entries.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->mapToScene(QPointF()).y() < b->mapToScene(QPointF()).y();
            });
            // The tab shown checked, as the notebook switcher checks the notebook open.
            check(entries.size() == 2 && !entries.first()->property("checked").toBool()
                          && entries.last()->property("checked").toBool(),
                  "the tab shown is the one checked in the switcher");
            tapOrClick(window, pointIn(entries.first()), touch);
        }
        check(waitFor([&] { return app->focusedTab() == first && path() == guide && openMenus(window) == 0; }),
              "a tap on a tab in the switcher shows it");
        // Each tab its own history: the back key returns within this one.
        app->navigate({{u"view"_s, u"tasks"_s}});
        const bool handled = sendKey(window, Qt::Key_Back);
        check(handled && app->focusedTab() == first && path() == guide,
              "the back key goes back in the tab's own history");
        if ((button = named(u"topBarTabs"_s)))
            tapOrClick(window, pointIn(button), touch);
        QQuickItem *closeOthers = nullptr;
        check(waitFor([&] { return (closeOthers = named(u"tabSwitcherCloseOthers"_s)) != nullptr; }),
              "the tab switcher offers “Zamknij pozostałe karty”");
        if (closeOthers)
            tapOrClick(window, pointIn(closeOthers), touch);
        check(waitFor([&] { return app->tabCount() == 1 && !named(u"topBarTabs"_s) && openMenus(window) == 0; }),
              "closing the other tabs hides the tab switcher");
        closeMenus(window);
        app->goHome();
        return;
    }

    // ---- Desktops: the shortcuts.
    window->requestActivate();
    waitFor([window] { return window->isActive(); }, 2000);
    const int first = app->focusedTab();
    key(Qt::Key_T);
    check(app->tabCount() == 2 && app->focusedTab() != first && app->atHome() && !app->canGoBack(),
          "Ctrl+T opens a new tab at home");
    check(waitFor([&] { return named(u"shellTabBar"_s) != nullptr; }), "two tabs show the tab bar");
    const int second = app->focusedTab();
    app->openPage(guide);
    check(loadedIn(window, guide), "the new tab shows a page");
    key(Qt::Key_Tab);
    check(app->focusedTab() == first && app->atHome() && !app->tabCanGoForward(first),
          "Ctrl+Tab shows the next tab (round past the last), with its own history");
    key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    check(app->focusedTab() == second && path() == guide && app->canGoBack(), "Ctrl+Shift+Tab shows the previous tab");
    key(Qt::Key_1);
    check(app->focusedTab() == first, "Ctrl+1 shows the first tab");
    key(Qt::Key_9);
    check(app->focusedTab() == second, "Ctrl+9 shows the last tab");
    check(loadedIn(window, guide), "back on the tab, its page is in the editor again");

    // ---- A middle click and a Ctrl+click on a page of the sidebar's tree.
    const auto treeRow = [&](const QString &p) -> QQuickItem * {
        QQuickItem *list = named(u"sidebarTree"_s);
        if (!list)
            return nullptr;
        if (auto *model = list->property("model").value<QObject *>()) {
            int row = -1;
            QMetaObject::invokeMethod(model, "indexOf", qReturnArg(row), p);
            if (row >= 0)
                QMetaObject::invokeMethod(list, "positionViewAtIndex", row, 4 /* ListView.Contain */);
        }
        settle(60);
        return findShown(list, [&](QQuickItem *item) {
            return item->property("isDir").isValid() && item->property("path").toString() == p;
        });
    };
    app->requestItemAction(u"reveal"_s, renovation);
    QQuickItem *row = nullptr;
    check(waitFor([&] { return (row = treeRow(renovation)) != nullptr; }), "the sidebar shows the renovation page");
    if (row)
        mouseClick(window, pointIn(row), Qt::MiddleButton);
    check(waitFor([&] { return app->tabCount() == 3 && path() == renovation; }),
          "a middle click on a page in the sidebar opens it in a new tab");
    key(Qt::Key_W);
    check(app->tabCount() == 2 && app->focusedTab() == second && path() == guide,
          "Ctrl+W closes the tab; its neighbour is shown");
    if ((row = treeRow(renovation)))
        mouseClick(window, pointIn(row), Qt::LeftButton, Qt::ControlModifier);
    check(waitFor([&] { return app->tabCount() == 3 && path() == renovation; }),
          "a Ctrl+click on a page in the sidebar opens it in a new tab");
    const int third = app->focusedTab();

    // ---- A link in the editor: a middle click, and the link menu's "Open in new tab".
    app->selectTab(second);
    QObject *page = nullptr;
    QQuickItem *editor = nullptr;
    const auto guideEditor = [&] {
        page = nullptr;
        editor = nullptr;
        waitFor([&] {
            const QList<QObject *> found = controllers(window, guide);
            page = found.value(0);
            return page && page->property("loaded").toBool();
        });
        for (QQuickItem *e : window->findChildren<QQuickItem *>(u"pageEditor"_s)) {
            if (page && e->property("controller").value<QObject *>() == page)
                editor = e;
        }
        return editor;
    };
    const auto linkOnScreen = [&]() -> QPointF {
        if (!guideEditor())
            return {-1, -1};
        const QStringList lines = app->workspace()->readFile(guide).split(u'\n');
        int linkRow = -1;
        for (qsizetype i = 0; i < lines.size() && linkRow < 0; ++i) {
            if (lines.at(i).startsWith(u"- Link do strony:"_s))
                QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(linkRow), int(i));
        }
        if (auto *list = editor->property("listView").value<QQuickItem *>(); list && linkRow >= 0)
            QMetaObject::invokeMethod(list, "positionViewAtIndex", linkRow, 1 /* ListView.Center */);
        settle(250);
        QQuickItem *text = renderedTextContaining(editor, u"page:Remont"_s);
        return text ? linkPoint(text, u"page:Remont"_s) : QPointF(-1, -1);
    };
    QPointF link = linkOnScreen();
    check(link.x() >= 0, "the guide shows its link to the renovation project");
    if (link.x() >= 0)
        mouseClick(window, link, Qt::MiddleButton);
    check(waitFor([&] { return app->tabCount() == 4 && path() == renovation; }),
          "a middle click on a link in the editor opens it in a new tab");
    app->closeTab(app->focusedTab());
    app->selectTab(second);
    link = linkOnScreen();
    if (link.x() >= 0)
        mouseClick(window, link, Qt::RightButton);
    QQuickItem *openTab = nullptr;
    check(waitFor([&] { return (openTab = named(u"linkMenuOpenTab"_s)) != nullptr; }),
          "the link menu offers “Otwórz w nowej karcie”");
    check(named(u"linkMenuOpenWindow"_s) != nullptr, "the link menu offers “Otwórz w nowym oknie”");
    if (openTab)
        mouseClick(window, pointIn(openTab));
    check(waitFor([&] { return app->tabCount() == 4 && path() == renovation && openMenus(window) == 0; }),
          "the link menu's “Otwórz w nowej karcie” opens the page in a new tab");
    app->closeTab(app->focusedTab());
    closeMenus(window);

    // ---- A tab dragged along the bar.
    app->selectTab(second);
    settle(200);
    const auto barTabs = [&] {
        QList<QQuickItem *> out;
        for (QQuickItem *item : uidriver::allItems(window->contentItem())) {
            if (item->objectName() == u"shellTab" && shown(item))
                out.append(item);
        }
        std::sort(out.begin(), out.end(), [](QQuickItem *a, QQuickItem *b) {
            return a->mapToScene(QPointF()).x() < b->mapToScene(QPointF()).x();
        });
        return out;
    };
    QList<QQuickItem *> tabItems;
    check(waitFor([&] { return (tabItems = barTabs()).size() == 3; }), "the bar shows the three tabs");
    if (tabItems.size() == 3) {
        mouseDrag(window, pointIn(tabItems.first()), pointIn(tabItems.last(), 0.7), 100);
        check(waitFor([&] { return app->tabs().last().toMap().value(u"id"_s).toInt() == first; }),
              "a tab dragged to the end of the bar moves there");
    }

    // ---- Where a page was scrolled: kept while the tab is away (its editor is not).
    if (guideEditor()) {
        auto *list = editor->property("listView").value<QQuickItem *>();
        if (list) {
            QMetaObject::invokeMethod(list, "positionViewAtIndex", 12, 0 /* ListView.Beginning */);
            list->setProperty("contentY", list->property("contentY").toReal() + 7);
            settle(150);
        }
        const qreal before = list ? list->property("contentY").toReal() : -1;
        app->selectTab(first); // the journals: another view, the page's editor goes
        check(waitFor([&] { return controllers(window, guide).isEmpty(); }),
              "a tab not shown keeps no editor of its page");
        check(controllers(window, renovation).isEmpty(), "nor does a tab in the background");
        app->selectTab(second);
        const bool back = guideEditor() != nullptr;
        auto *again = back ? editor->property("listView").value<QQuickItem *>() : nullptr;
        check(again && before > 50 && waitFor([&] { return qAbs(again->property("contentY").toReal() - before) <= 2; }),
              "back on its tab the page is where it was scrolled");
        if (again && qAbs(again->property("contentY").toReal() - before) > 2)
            std::fprintf(stderr, "selftest: scrolled to %.1f, back at %.1f\n", before,
                         again->property("contentY").toReal());
    }

    // ---- The split view.
    check(app->commands()->run(u"view.split"_s), "“Podziel widok” splits the view");
    QQuickItem *splitPane = nullptr;
    check(waitFor([&] { return (splitPane = named(u"shellSplitPane"_s)) != nullptr; }) && app->splitFocused()
                  && waitFor([&] { return controllers(window, guide).size() == 2; }),
          "the split pane shows a copy of the tab beside it, focused");
    app->navigate(pageRoute(renovation));
    check(loadedIn(window, renovation) && app->tabRoute(app->mainTab()).value(u"path"_s) == guide,
          "navigation goes to the focused pane");
    // A click into the main pane's page (it starts editing a block there): that pane is focused.
    if (QQuickItem *mainPane = named(u"shellMainPane"_s)) {
        QQuickItem *text = findShown(mainPane, [](QQuickItem *item) {
            return item->inherits("QQuickText") && !item->property("text").toString().isEmpty()
                   && hasAncestor(item, "BlockText");
        });
        if (text)
            mouseClick(window, text->mapToScene(QPointF(8, text->height() / 2)));
        check(text && waitFor([&] { return !app->splitFocused() && path() == guide; }),
              "a click in the main pane focuses it");
        if (QObject *c = controllers(window, guide).value(0))
            QMetaObject::invokeMethod(c, "stopEditing");
    }
    app->focusPane(true);
    key(Qt::Key_W);
    check(waitFor([&] { return app->splitTab() == 0 && !named(u"shellSplitPane"_s); }) && path() == guide,
          "Ctrl+W on the split pane's tab closes the split view");

    // ---- A page in a window of its own: the same notebook, edits seen in both windows.
    app->openPage(renovation);
    QObject *mainPage = nullptr;
    loadedIn(window, renovation);
    mainPage = controllers(window, renovation).value(0);
    const int windowTab = app->openWindow(pageRoute(renovation));
    QQuickWindow *pageWindow = nullptr;
    check(windowTab > 0 && waitFor([&] { return (pageWindow = pageWindows().value(0)) != nullptr; })
                  && loadedIn(pageWindow, renovation),
          "“Otwórz w nowym oknie” shows the page in a window of its own");
    check(app->tabCount() == 3 && app->windowTabs().size() == 1, "the window's tab is not in the bar");
    if (pageWindow && mainPage) {
        QObject *windowPage = controllers(pageWindow, renovation).value(0);
        int added = -1;
        QMetaObject::invokeMethod(mainPage, "insertBlockAfter", qReturnArg(added), 0,
                                  u"Zażółć gęślą jaźń z okna głównego"_s);
        QMetaObject::invokeMethod(mainPage, "save");
        check(added >= 0 && windowPage
                      && waitFor(
                              [&] {
                                  return windowPage->property("sourceText").toString().contains(u"z okna głównego"_s);
                              },
                              8000),
              "an edit in the main window shows in the page's window");
        // Undo the edit in the notebook (later checks read it).
        const QString text = app->workspace()->readFile(renovation);
        app->workspace()->writeFile(renovation, QString(text).remove(u"- Zażółć gęślą jaźń z okna głównego\n"_s));
    }
    app->closeTab(windowTab);
    check(waitFor([&] { return pageWindows().isEmpty(); }) && app->windowTabs().isEmpty(),
          "closing the window's tab closes the window");
    window->requestActivate();
    waitFor([window] { return window->isActive(); }, 2000);

    // A view's tab says what it shows: the event log's "Event log" (not "Brulion") with the
    // palette's icon of the log (not a page's).
    app->openTab({{u"view"_s, u"logs"_s}});
    const QString logTitle = QCoreApplication::translate("AppShell", "Event log");
    QQuickItem *logTab = nullptr;
    check(waitFor([&] {
              QQuickItem *list = named(u"shellTabList"_s);
              logTab = list ? findShown(list, [&](QQuickItem *item) {
                  return item->property("title").toString() == logTitle;
              })
                            : nullptr;
              return logTab && findShown(logTab, [](QQuickItem *item) {
                         return isA(item, "BIcon") && item->property("name").toString() == u"scroll-text";
                     });
          }),
          "the event log's tab is titled “Event log”, with the log's icon");

    // One tab again.
    app->closeOtherTabs(app->focusedTab());
    check(waitFor([&] { return app->tabCount() == 1 && !named(u"shellTabBar"_s); }), "one tab left: the bar goes");
    Q_UNUSED(third);
    app->goHome();
    closeMenus(window);
}

QQuickWindow *trayCalendarWindow()
{
    const QList<QWindow *> windows = QGuiApplication::topLevelWindows();
    for (QWindow *w : windows) {
        if (w->objectName() == u"trayCalendar")
            return qobject_cast<QQuickWindow *>(w);
    }
    return nullptr;
}

void checkTrayCalendar(QQuickWindow *window, AppController *app, const Check &check)
{
    QScreen *screen = window->screen() ? window->screen() : QGuiApplication::primaryScreen();
    const QRect area = screen->availableGeometry();
    // The icon in the work area's bottom-right corner (offscreen has no taskbar).
    const QRect icon(area.right() - 40, area.bottom() - 30, 24, 24);
    app->showTrayCalendar(icon, {});
    QQuickWindow *popup = nullptr;
    check(waitFor([&] {
              popup = trayCalendarWindow();
              return popup && popup->isVisible() && popup->isActive();
          }),
          "the tray calendar opens and takes the activation");
    if (!popup)
        return;
    const QRect frame(popup->position(), popup->size());
    if (!area.contains(frame))
        std::fprintf(stderr, "selftest: tray calendar at %d,%d %dx%d, work area %d,%d %dx%d\n", frame.x(), frame.y(),
                     frame.width(), frame.height(), area.x(), area.y(), area.width(), area.height());
    check(area.contains(frame) && frame.bottom() < icon.top() && frame.right() >= icon.left(),
          "it sits above the icon, inside the work area");
    check(popup->flags().testFlag(Qt::FramelessWindowHint) && popup->flags().testFlag(Qt::WindowStaysOnTopHint)
                  && (popup->flags() & Qt::WindowType_Mask) == Qt::Tool && !popup->transientParent(),
          "it is a frameless tool window on top, owned by no window");
    QQuickItem *todayText = shownNamed(popup->contentItem(), u"trayCalendarToday"_s);
    check(todayText && todayText->property("text").toString().contains(u"2026"_s), "its header shows today's date");

    // A day with entries (the sample notebook's tomorrow: reminders): listed under the month.
    QQuickItem *month = shownNamed(popup->contentItem(), u"trayCalendarMonth"_s);
    const QString tomorrow = app->today(1);
    QStringList titles;
    for (const QVariant &v : app->workspace()->calendarItems(tomorrow, tomorrow)) {
        if (v.toMap().value(u"kind"_s).toString() != u"journal")
            titles.append(v.toMap().value(u"title"_s).toString());
    }
    if (month)
        month->setProperty("selectedDate", tomorrow);
    const auto listed = [&](const QString &title) {
        for (QQuickItem *item : uidriver::allItems(popup->contentItem())) {
            if (isA(item, "BListItem") && item->isVisible() && item->property("text").toString() == title)
                return true;
        }
        return false;
    };
    check(month && !titles.isEmpty() && waitFor([&] { return std::all_of(titles.begin(), titles.end(), listed); }),
          "a day's tasks and reminders are listed");

    sendKey(popup, Qt::Key_Escape);
    check(waitFor([&] { return !popup->isVisible(); }), "Escape hides the tray calendar");
    app->showTrayCalendar(icon, {}, true);
    check(waitFor([&] { return popup->isVisible() && popup->isActive(); }) && month
                  && month->property("selectedDate").toString() == app->today(0),
          "a click on the icon opens it again, on today");
    app->showTrayCalendar(icon, {}, true);
    check(waitFor([&] { return !popup->isVisible(); }), "a second click on the icon closes it");

    // The activation going elsewhere (a click outside) hides it; the click on the icon that did
    // it arrives right after and must not reopen it.
    app->showTrayCalendar(icon, {}, true);
    waitFor([&] { return popup->isVisible() && popup->isActive(); });
    window->requestActivate();
    QElapsedTimer sinceHide;
    check(waitFor([&] { return !popup->isVisible(); }, 3000), "it hides when another window takes the activation");
    sinceHide.start();
    app->showTrayCalendar(icon, {}, true);
    settle(30);
    if (sinceHide.elapsed() < TrayPopupReopenGuardMs - 50)
        check(!popup->isVisible(), "a click on the icon right after that keeps it closed");
    settle(TrayPopupReopenGuardMs + 50);
    if (!popup->isVisible())
        app->showTrayCalendar(icon, {}, true);
    check(waitFor([&] { return popup->isVisible(); }), "a later click opens it again");
    // Windows: the press on the icon takes the activation, Trigger comes on the release — a
    // button held past the 300 ms guard is still that click (the cursor where the hide was).
    waitFor([&] { return popup->isActive(); });
    window->requestActivate();
    check(waitFor([&] { return !popup->isVisible(); }, 3000), "another window takes the activation again");
    settle(TrayPopupReopenGuardMs + 100);
    app->showTrayCalendar(icon, QCursor::pos(), true);
    settle(30);
    check(!popup->isVisible(), "a held click on the icon that took the activation keeps it closed");
    app->showTrayCalendar(icon, QCursor::pos(), true);
    check(waitFor([&] { return popup->isVisible(); }), "the next click opens it");

    // "Open journal" on a day: the popup goes, the main window shows that day.
    app->navigate({{u"view"_s, u"tasks"_s}});
    waitFor([&] { return popup->isActive(); });
    if (month)
        month->setProperty("selectedDate", tomorrow);
    settle(50);
    QQuickItem *openJournal = shownNamed(popup->contentItem(), u"miniCalendarOpenJournal"_s);
    if (openJournal)
        mouseClick(popup, pointIn(openJournal));
    check(openJournal && waitFor([&] {
              return !popup->isVisible() && window->isVisible() && app->currentView() == u"journal"
                     && app->currentRoute().value(u"date"_s).toString() == tomorrow;
          }),
          "“Open journal” hides the calendar and shows that day in the window");

    // The main window hidden in the tray: the calendar opens and closes, the app stays.
    bool lastClosed = false;
    const auto connection = QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, app,
                                             [&lastClosed] { lastClosed = true; });
    window->hide();
    app->showTrayCalendar(icon, {}, true);
    check(waitFor([&] { return popup->isVisible() && popup->isActive(); }), "it opens with the window hidden");
    sendKey(popup, Qt::Key_Escape);
    waitFor([&] { return !popup->isVisible(); });
    app->showTrayCalendar(icon, {});
    waitFor([&] { return popup->isVisible(); });
    popup->close(); // Alt+F4
    settle(200);
    check(!popup->isVisible() && !lastClosed && !app->quitting(),
          "closing it with the window hidden hides it and never ends the app");
    QObject::disconnect(connection);
    QObject *quickCapture = window->findChild<QObject *>(u"shellCapture"_s);
    app->showTrayCalendar(icon, {});
    waitFor([&] { return popup->isVisible() && popup->isActive(); });
    QQuickItem *capture = shownNamed(popup->contentItem(), u"trayCalendarCapture"_s);
    if (capture)
        mouseClick(popup, pointIn(capture));
    check(capture && quickCapture && waitFor([&] {
              return !popup->isVisible() && window->isVisible() && quickCapture->property("opened").toBool();
          }),
          "“Quick capture…” brings the window up with quick capture");
    if (quickCapture && quickCapture->property("visible").toBool()) {
        window->requestActivate();
        waitFor([window] { return window->isActive(); }, 2000);
        sendKey(window, Qt::Key_Escape);
        waitFor([&] { return !quickCapture->property("visible").toBool(); });
    }
    window->requestActivate();
    waitFor([window] { return window->isActive(); }, 2000);
    app->goHome();
}

/// 5f. The on-screen keyboard (phones; a simulated one, KeyboardInsets.simulated): the bottom
///     navigation makes way while it is up, the view ends at its top, toasts and a bottom sheet
///     (quick capture) sit above it. Editing with the keyboard hidden by the system: the bottom
///     navigation is back and the editing toolbar docks right on it (also with a system
///     navigation bar under it), toasts show at the top, away from the line being written, the
///     navigation's "+ Note" stays under the toolbar; the keyboard sliding in leaves the navigation under it until it
///     covers it (the toolbar never drops first), sliding away uncovers it again, the toolbar
///     following the keyboard's top down to the navigation; a keyboard shorter than the navigation
///     takes its place but leaves the toolbar where it was; a browser's keyboard (the page resized
///     for it: no overlap, still settling) takes the navigation away at once; a tap on a navigation
///     tab while editing opens that view and the editing ends, a tap on "+ Note" ends the
///     editing and opens quick capture. A settings field that gets the
///     focus is scrolled above the keyboard; on a wide touch window a page in the side panel docks
///     its toolbar on the keyboard too.
void checkKeyboard(QQmlApplicationEngine *engine, QQuickWindow *window, AppController *app, const Check &check)
{
    auto *insets = engine->singletonInstance<QObject *>("Brulion", "KeyboardInsets");
    auto *nav = window->findChild<QQuickItem *>(u"shellBottomNav"_s);
    auto *room = window->findChild<QQuickItem *>(u"shellKeyboardRoom"_s);
    auto *capture = window->findChild<QObject *>(u"shellCapture"_s);
    if (!insets || !nav || !room || !capture) {
        check(false, "the keyboard's room, the bottom navigation and quick capture exist");
        return;
    }
    const qreal keyboard = 300;
    const qreal top = window->height() - keyboard;
    const auto near = [](qreal a, qreal b) { return qAbs(a - b) <= 1; };
    app->goHome();
    check(waitFor([&] { return nav->isVisible(); }), "the bottom navigation shows without a keyboard");

    insets->setProperty("simulated", keyboard);
    check(waitFor([&] { return !nav->isVisible() && near(room->mapToScene(QPointF(0, 0)).y(), top); }),
          "a keyboard comes up: the bottom navigation makes way, the view ends at the keyboard's top");

    // A toast while the keyboard is up: above it.
    app->toast(u"Zapisano notatkę „Żółw”"_s, u"info"_s);
    QQuickItem *toast = nullptr;
    check(waitFor([&] {
              toast = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BToast_"); });
              return toast && toast->mapToScene(QPointF(0, toast->height())).y() <= top + 1;
          }),
          "a toast sits above the keyboard");

    // Quick capture (a bottom sheet on phones) sits on the keyboard.
    app->requestUiAction(u"capture"_s);
    check(waitFor([&] {
              return capture->property("opened").toBool()
                     && near(capture->property("y").toReal() + capture->property("height").toReal(), top);
          }),
          "quick capture's sheet sits on the keyboard");
    // A word the keyboard is still composing (preedit, outside the field's text) counts: "Add"
    // is enabled under it and adds it.
    Workspace *ws = app->workspace();
    const QString today = ws->journalPath(app->today(0));
    const QString journal = ws->readFile(today);
    QQuickItem *captureField = window->activeFocusItem();
    check(captureField && captureField->inherits("QQuickTextEdit"), "quick capture's field has the keys");
    composeWord(captureField, u"Żółw"_s);
    const QString addText = QCoreApplication::translate("ShellQuickCapture", "Add");
    QQuickItem *add = findShown(window->contentItem(), [&](QQuickItem *item) {
        return isA(item, "BButton") && item->property("text").toString() == addText;
    });
    check(add && add->isEnabled(), "a word still being composed enables “Dodaj”");
    if (add && add->isEnabled())
        tapOrClick(window, add->mapToScene(QPointF(add->width() / 2, add->height() / 2)), true);
    else
        sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !capture->property("visible").toBool(); })
                  && ws->readFile(today).endsWith(u"- Żółw\n"_s),
          "“Dodaj” adds the composed word to today's journal");
    if (ws->readFile(today) != journal)
        ws->writeFile(today, journal);
    // (Its toast with "Open" stays 7 s over the bottom rows of the views checked next.)
    if (QQuickItem *toasts = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BToastHost"); }))
        QMetaObject::invokeMethod(toasts, "clear");

    insets->setProperty("simulated", 0);
    check(waitFor([&] {
              return nav->isVisible() && near(nav->mapToScene(QPointF(0, nav->height())).y(), window->height());
          }),
          "the keyboard goes: the bottom navigation is back at the window's bottom");

    // Editing with the keyboard hidden by the system: the bottom navigation is back, the toolbar
    // docks right on it.
    const QString guide = u"Witaj w Brulionie.md"_s;
    QQuickItem *shellItem = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
    // Quick capture's + on the top bar (it was the bottom navigation's "+ Note" up to 0.13).
    QQuickItem *captureButton = window->findChild<QQuickItem *>(u"shellCaptureButton"_s);
    const auto captureOnTop = [&] {
        return captureButton && captureButton->isVisible()
               && captureButton->mapToScene(QPointF(0, captureButton->height())).y()
                          < nav->mapToScene(QPointF(0, 0)).y() / 4;
    };
    const auto navTop = [&] { return nav->mapToScene(QPointF(0, 0)).y(); };
    const auto navAtBottom = [&] { return nav->isVisible() && near(navTop() + nav->height(), window->height()); };
    QQuickItem *editor = nullptr;
    QObject *page = nullptr;
    const auto openGuide = [&] {
        editor = nullptr;
        page = nullptr;
        app->openPage(guide);
        return waitFor([&] {
            for (QQuickItem *e : window->findChildren<QQuickItem *>(u"pageEditor"_s)) {
                auto *c = e->property("controller").value<QObject *>();
                if (c && c->property("path").toString() == guide && c->property("loaded").toBool() && e->isVisible()) {
                    editor = e;
                    page = c;
                }
            }
            return editor != nullptr;
        });
    };
    const auto editLastRow = [&] {
        int row = -1;
        QMetaObject::invokeMethod(page, "lastVisibleRow", qReturnArg(row));
        uidriver::callQml(editor, "startEditing", {QVariant(row), QVariant(-1)});
    };
    check(openGuide(), "the guide opens");
    if (editor && page && shellItem) {
        editLastRow();
        check(waitFor([&] { return editor->property("toolbarVisible").toBool() && navAtBottom(); }),
              "editing with the keyboard hidden: the toolbar is docked and the bottom navigation is shown");
        QQuickItem *slot = toolbarSlotOf(window, editor);
        const auto slotBottom = [&] { return slot ? slot->mapToScene(QPointF(0, slot->height())).y() : -1.0; };
        // (Laid out with the shell's next polish.)
        check(waitFor([&] { return slot && near(slotBottom(), navTop()); }),
              "the docked toolbar sits right on the bottom navigation");
        check(waitFor([&] { return !room->isVisible(); }) && captureOnTop(),
              "no keyboard's room under the navigation; + is on the top bar");
        auto *theme = engine->singletonInstance<QObject *>("Brulion", "Theme");
        check(theme
                      && near(shellItem->property("toastClearance").toReal(),
                              nav->height() + theme->property("editToolbarHeight").toReal()),
              "toasts would clear the bottom navigation and the toolbar on it");
        std::fprintf(stderr,
                     "selftest: keyboard hidden: the toolbar ends at %.0f, the navigation starts at %.0f (window %d)\n",
                     slotBottom(), navTop(), window->height());
        // A toast while typing ("Scalono…", a sync): at the top, never over the line being
        // written (the caret is kept right above the toolbar, where the toasts used to be).
        if (QObject *toasts = window->findChild<QObject *>(u"toastHost"_s))
            QMetaObject::invokeMethod(toasts, "clear");
        app->toast(u"Scalono zmiany wprowadzone na innym urządzeniu"_s, u"info"_s);
        QQuickItem *typed = window->activeFocusItem();
        const bool caretKnown = typed && typed->inherits("QQuickTextEdit");
        QQuickItem *typingToast = nullptr;
        check(waitFor([&] {
                  typingToast = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BToast_"); });
                  if (!typingToast)
                      return false;
                  const QRectF box =
                          typingToast->mapRectToScene(QRectF(0, 0, typingToast->width(), typingToast->height()));
                  const QRectF caret =
                          caretKnown ? typed->mapRectToScene(typed->property("cursorRectangle").toRectF()) : QRectF();
                  return box.bottom() < window->height() / 4 && (!caretKnown || !box.intersects(caret));
              }),
              "a toast while typing shows at the top, not over the line being written");
        if (QObject *toasts = window->findChild<QObject *>(u"toastHost"_s))
            QMetaObject::invokeMethod(toasts, "clear");
        // A system navigation bar under the window's bottom edge (a phone's SafeArea margin, here
        // added to the shell's): the bottom navigation pads itself over it, the toolbar stays on
        // the navigation.
        const auto setSafeBottom = [&](int px) {
            QQmlExpression set(qmlContext(shellItem), shellItem, u"SafeArea.additionalMargins.bottom = %1"_s.arg(px));
            set.evaluate();
            if (set.hasError())
                std::fprintf(stderr, "selftest: %s\n", qPrintable(set.error().toString()));
            return !set.hasError();
        };
        const qreal navHeight = nav->height();
        if (slot && setSafeBottom(48)) {
            check(waitFor([&] {
                      return navAtBottom() && near(nav->height(), navHeight + 48) && near(slotBottom(), navTop())
                             && near(slotBottom(), window->height() - navHeight - 48) && !room->isVisible();
                  }),
                  "with a system navigation bar the toolbar still sits right on the bottom navigation above it");
            // Qt's SafeArea counting Android's keyboard in (336 px, a moment after it went): no
            // navigation bar is that tall — the bottom navigation keeps the last one's padding.
            setSafeBottom(336);
            settle(100);
            check(waitFor([&] {
                      return navAtBottom() && near(nav->height(), navHeight + 48) && near(slotBottom(), navTop());
                  }),
                  "a safe area as tall as a keyboard does not lift the bottom navigation");
            setSafeBottom(0);
            waitFor([&] { return near(slotBottom(), window->height() - navHeight); });
        } else {
            check(false, "the shell's SafeArea margins can be set");
        }

        // The keyboard comes back, sliding in frame by frame (Android 11+): the navigation stays
        // under it until it is covered, the toolbar never drops below where it was.
        auto *keyboardInsets = qobject_cast<brulion::ui::KeyboardInsets *>(insets);
        check(keyboardInsets != nullptr, "KeyboardInsets is the C++ type");
        if (!keyboardInsets)
            return;
        const qreal low = navHeight / 3;
        keyboardInsets->simulate(low, true);
        check(waitFor([&] { return nav->isVisible() && near(slotBottom(), window->height() - navHeight); }),
              "a keyboard sliding in below the navigation's top: the navigation stays, the toolbar on it");
        keyboardInsets->simulate(navHeight + 40, true);
        check(waitFor([&] { return !nav->isVisible() && near(slotBottom(), window->height() - navHeight - 40); }),
              "past the navigation's top: the navigation is covered, the toolbar rides on the keyboard");
        keyboardInsets->simulate(keyboard, false);
        check(waitFor([&] {
                  return !nav->isVisible() && near(slotBottom(), top) && near(room->mapToScene(QPointF(0, 0)).y(), top);
              }),
              "the keyboard up: the toolbar on it, the navigation away");
        // … and goes away again (Back): shown no more from its first frame, the navigation is
        // shown under it, the toolbar follows the keyboard's top down to the navigation.
        keyboardInsets->simulate(200, true, true);
        check(waitFor([&] { return nav->isVisible() && near(slotBottom(), window->height() - 200); }),
              "a keyboard sliding away: the navigation is back under it, the toolbar on the keyboard's top");
        keyboardInsets->simulate(low, true, true);
        check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()); }),
              "below the navigation's top: the toolbar rests on the navigation");
        insets->setProperty("simulated", 0);
        check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()) && !room->isVisible(); }),
              "the keyboard gone: the toolbar on the bottom navigation");

        // A keyboard that ends below the navigation's top (a suggestion strip over a hardware
        // keyboard): the navigation stays under it while it slides in and makes way once it has
        // landed, but the view keeps the navigation's room — the toolbar never drops onto the
        // short keyboard after it came —, nor moves while it goes.
        const qreal shortKeyboard = navHeight * 2 / 3;
        keyboardInsets->simulate(shortKeyboard / 2, true);
        check(waitFor([&] { return nav->isVisible() && near(slotBottom(), window->height() - navHeight); }),
              "a short keyboard sliding in: the navigation stays, the toolbar on it");
        keyboardInsets->simulate(shortKeyboard, false);
        check(waitFor([&] { return !nav->isVisible() && room->isVisible() && near(room->height(), navHeight); }),
              "a short keyboard landed: the navigation makes way, the view keeps its room");
        settle(100);
        check(near(slotBottom(), window->height() - navHeight),
              "a short keyboard landed: the toolbar stays where it was, it does not drop onto the keyboard");
        keyboardInsets->simulate(shortKeyboard / 2, true, true);
        check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()) && !room->isVisible(); }),
              "a short keyboard sliding away: the navigation back under it, the toolbar still on it");
        insets->setProperty("simulated", 0);
        check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()) && !room->isVisible(); }),
              "the short keyboard gone: the toolbar on the bottom navigation");

        // A browser's keyboard: the page resized for it (interactive-widget=resizes-content), so no
        // overlap, `shown` from the page's shrink while the readings settle — the navigation goes
        // at once (nothing would cover it); the page tall again brings it back.
        {
            insets->setProperty("simulated", -1);
            const qreal h = window->height();
            const auto browser = [&](qreal height, bool settled) {
                brulion::ui::ViewportSample sample;
                sample.innerWidth = window->width();
                sample.innerHeight = height;
                sample.height = height;
                sample.containerHeight = height;
                sample.touch = true;
                sample.editing = true;
                sample.settled = settled;
                sample.mode = u"content"_s;
                keyboardInsets->setViewportSample(sample);
            };
            browser(h, true);
            check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()); }),
                  "a browser without the keyboard: the toolbar on the bottom navigation");
            browser(h - keyboard, false);
            check(keyboardInsets->shown() && keyboardInsets->animating() && keyboardInsets->overlap() == 0
                          && !keyboardInsets->slidesOver()
                          && waitFor([&] { return !nav->isVisible() && near(slotBottom(), h); }),
                  "a browser's keyboard (the page shrank, still settling): the navigation goes at once");
            browser(h - keyboard, true);
            browser(h, false);
            check(waitFor([&] { return navAtBottom() && near(slotBottom(), navTop()); }),
                  "the browser's page tall again: the navigation is back under the toolbar");
            browser(h, true);
        }
        insets->setProperty("simulated", 0);

        QMetaObject::invokeMethod(page, "stopEditing");
        check(waitFor([&] { return !editor->property("toolbarVisible").toBool() && navAtBottom(); })
                      && captureOnTop(),
              "editing stops: the toolbar goes, the bottom navigation stays, + on the top bar");

        // The top bar's + while editing without the keyboard: the editing ends (nothing
        // written), quick capture opens.
        {
            const QString unchanged = app->workspace()->readFile(guide);
            editLastRow();
            check(waitFor([&] { return editor->property("toolbarVisible").toBool() && navAtBottom() && captureOnTop(); }),
                  "editing again, the keyboard hidden: + is on the top bar");
            if (captureButton)
                tapOrClick(window, pointIn(captureButton), QCoreApplication::arguments().contains(u"--touch"_s));
            check(waitFor([&] {
                      return capture->property("opened").toBool() && !editor->property("toolbarVisible").toBool()
                             && page->property("editingRow").toInt() < 0;
                  }),
                  "a tap on + while editing ends the editing and opens quick capture");
            sendKey(window, Qt::Key_Escape);
            check(waitFor([&] { return !capture->property("visible").toBool(); })
                          && app->workspace()->readFile(guide) == unchanged,
                  "quick capture closes, the guide is left as it was");
        }

        // A tap on a navigation tab while editing without the keyboard: that view opens, the
        // editing ends with the page (nothing written).
        const QString before = app->workspace()->readFile(guide);
        editLastRow();
        QQuickItem *calendarTab = nullptr;
        check(waitFor([&] {
                  calendarTab = shownNamed(window->contentItem(), u"bottomNav_calendar"_s);
                  return editor->property("toolbarVisible").toBool() && calendarTab && navAtBottom();
              }),
              "editing again, the keyboard hidden: Kalendarz is on the bottom navigation");
        if (calendarTab)
            tapOrClick(window, pointIn(calendarTab), QCoreApplication::arguments().contains(u"--touch"_s));
        check(waitFor([&] {
                  return app->currentRoute().value(u"view"_s).toString() == u"calendar"
                         && !findShown(window->contentItem(),
                                       [](QQuickItem *item) { return item->objectName() == u"editToolbarSlot"; })
                         && navAtBottom();
              }),
              "a tap on Kalendarz while editing opens the calendar: the editing and its toolbar end");
        check(app->workspace()->readFile(guide) == before, "the guide is left as it was");
    }

    // A form field low in the settings: the window is never panned to it, it is scrolled above
    // the keyboard when it gets the focus (BKeyboardFocusKeeper).
    app->navigate({{u"view"_s, u"settings"_s}});
    QQuickItem *field = nullptr;
    check(waitFor([&] {
              return (field = shownNamed(window->contentItem(), u"defaultReminderTimeField"_s)) != nullptr;
          }),
          "the settings show the default reminder time");
    QQuickItem *flick = enclosingFlickable(field);
    if (field && flick) {
        settle(300);
        // The field low on the screen, where the keyboard will come.
        const qreal low = window->height() - 100 - field->height();
        flick->setProperty("contentY", qMax<qreal>(0, flick->property("contentY").toReal()
                                                              + field->mapToScene(QPointF(0, 0)).y() - low));
        settle(100);
        const qreal before = field->mapToScene(QPointF(0, field->height())).y();
        insets->setProperty("simulated", keyboard);
        field->forceActiveFocus();
        const auto fieldShown = [&] {
            const qreal fieldTop = field->mapToScene(QPointF(0, 0)).y();
            const qreal fieldBottom = field->mapToScene(QPointF(0, field->height())).y();
            return field->hasActiveFocus() && fieldBottom <= top && fieldTop >= flick->mapToScene(QPointF(0, 0)).y();
        };
        check(before > top && waitFor(fieldShown),
              "a settings field that gets the focus is scrolled above the keyboard");
        std::fprintf(stderr, "selftest: the settings field: bottom %.0f, then %.0f (keyboard's top %.0f)\n", before,
                     field->mapToScene(QPointF(0, field->height())).y(), top);
        window->contentItem()->forceActiveFocus();
        insets->setProperty("simulated", 0);
    } else {
        check(false, "the settings field is in a Flickable");
    }
    app->goHome();

    // A wide touch window (a tablet, a phone in landscape): the side panel ends at the keyboard
    // too, so an editor in it docks its toolbar on the keyboard.
    const QSize phone = window->size();
    window->resize(1280, 800);
    check(waitFor([&] { return app->sidePanelAllowed() && !nav->isVisible(); }),
          "a wide window has room for the side panel");
    const QString recipes = u"Przepisy.md"_s;
    app->openPage(recipes, -1, true);
    QQuickItem *panel = nullptr;
    std::pair<QQuickItem *, QObject *> side{nullptr, nullptr};
    check(waitFor([&] {
              panel = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "ShellSidePanel"); });
              side = panel ? shownPageEditor(window, panel, recipes) : std::pair<QQuickItem *, QObject *>{};
              return side.first != nullptr;
          }),
          "a wide window shows a page in the side panel");
    if (side.first) {
        insets->setProperty("simulated", keyboard);
        int row = -1;
        QMetaObject::invokeMethod(side.second, "lastVisibleRow", qReturnArg(row));
        uidriver::callQml(side.first, "startEditing", {QVariant(row), QVariant(-1)});
        QQuickItem *slot = nullptr;
        check(waitFor([&] {
                  slot = toolbarSlotOf(window, side.first);
                  return slot && near(slot->mapToScene(QPointF(0, slot->height())).y(), window->height() - keyboard)
                         && near(panel->mapToScene(QPointF(0, panel->height())).y(), window->height() - keyboard);
              }),
              "the side panel ends at the keyboard: its editor's toolbar sits on the keyboard");
        if (slot)
            std::fprintf(stderr, "selftest: the side panel's toolbar ends at %.0f (keyboard's top %.0f)\n",
                         slot->mapToScene(QPointF(0, slot->height())).y(), window->height() - keyboard);
        QMetaObject::invokeMethod(side.second, "stopEditing");
    }
    insets->setProperty("simulated", -1);
    app->clearSidePanelRoute();
    app->setSidePanelVisible(false);
    window->resize(phone);
    waitFor([&] { return nav->isVisible(); });
    app->goHome();
}

/// 5h. Settings → Appearance (docs/UI.md "Look & feel"): the default look keeps the logo's colours;
///     a tap on an accent swatch recolours Theme at once and is saved, a custom colour from the
///     accent dialog too; an entry unticked under "Sidebar" leaves the sidebar (the drawer on
///     phones) and stays in the command palette; a bottom navigation slot takes another view from
///     its menu (phones: the bar shows it), and the view it already has stays checked there; the
///     quick note button's switch hides it; "Restore the default look" brings the logo's colours
///     back. Editor → Font → "Other…" opens the font picker; cancelled, the selection does not
///     move; a missing family names the serif or sans-serif font used instead. The settings are
///     put back at the end.
void checkAppearance(QQmlApplicationEngine *engine, QQuickWindow *window, AppController *app, const Check &check)
{
    const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
    const bool narrow = window->width() < 900;
    AppSettings *settings = app->settings();
    auto *theme = engine->singletonInstance<QObject *>("Brulion", "Theme");
    if (!theme) {
        check(false, "the Theme singleton exists");
        return;
    }
    const auto color = [theme](const char *name) { return theme->property(name).value<QColor>().name(); };
    const bool dark = theme->property("dark").toBool();
    check(color("accent") == (dark ? u"#ff7a66"_s : u"#c4422f"_s)
                  && color("fgOnAccent") == (dark ? u"#2b0a05"_s : u"#ffffff"_s)
                  && color("bg") == (dark ? u"#000000"_s : u"#fbf7ee"_s),
          "the default look is \"Nocny\" (coral on paper / OLED black)");
    // Visible, maybe scrolled out of the view (activate() scrolls it in).
    const auto shownItem = [window](const QString &name) {
        QQuickItem *found = nullptr;
        waitFor([&] {
            found = shownNamed(window->contentItem(), name);
            return found && shown(found);
        });
        return found && shown(found) ? found : nullptr;
    };
    const auto activate = [&](QQuickItem *item) {
        if (!item)
            return false;
        uidriver::scrollIntoView(window, item);
        settle(100);
        tapOrClick(window, pointIn(item), touch);
        return true;
    };

    app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"appearance"_s}});

    // The rows' shape (docs/UI.md "Look & feel", Settings rows): beside its text a control ends at
    // the row's right edge, centred on the title and description; compact windows stack only what
    // does not fit — under its text a segmented control fills the row. Phones (412 px): the two
    // background rows beside their text, Theme and Text size (three and four options) under
    // it; 360 px: each row decides alone, either shape. No option's label is cut, every target
    // is 44 px on touch UIs.
    {
        const auto rowOf = [](QQuickItem *item) {
            while (item && !isA(item, "ShellSettingRow"))
                item = item->parentItem();
            return item;
        };
        const auto sceneRect = [](QQuickItem *item) {
            return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        };
        // "" when the control's row has the shape asked for (`stacked`: -1 either, 0 beside, 1 under).
        const auto shapeProblem = [&](const QString &name, int stacked) -> QString {
            QQuickItem *control = shownNamed(window->contentItem(), name);
            QQuickItem *settingRow = rowOf(control);
            QQuickItem *text = settingRow ? settingRow->childItems().value(0) : nullptr;
            if (!control || !text || !shown(control))
                return name + u": not shown"_s;
            const bool under = settingRow->property("stacked").toBool();
            if (stacked >= 0 && under != (stacked == 1))
                return name + (under ? u": under its text"_s : u": beside its text"_s);
            const QRectF c = sceneRect(control), r = sceneRect(settingRow), t = sceneRect(text);
            if (qAbs(c.right() - r.right()) > 1)
                return name + u": ends at %1, its row at %2"_s.arg(c.right()).arg(r.right());
            if (under ? qAbs(c.left() - r.left()) > 1 || c.top() < t.bottom()
                      : qAbs(c.center().y() - t.center().y()) > 1 || c.left() < t.right())
                return name + u": not placed %1 its text"_s.arg(under ? u"under"_s : u"beside"_s);
            const QList<QQuickItem *> parts = uidriver::allItems(control);
            for (QQuickItem *part : parts) {
                if (part->property("truncated").toBool())
                    return name + u": a cut label (%1)"_s.arg(part->property("text").toString());
                if (touch && isA(part, "QQuickAbstractButton") && (part->height() < 44 || part->width() < 44))
                    return name + u": a target under 44 px"_s;
            }
            if (touch && c.height() < 44)
                return name + u": under 44 px high"_s;
            return {};
        };
        const auto rowsShaped = [&](bool compact, bool roomy, QString *problem) {
            // (The dark theme — the default — hides the light background's row, the light one the
            // dark background's.)
            QList<std::pair<QString, int>> expected{{u"themeModeSetting"_s, compact ? 1 : 0},
                                                    {u"fontScaleSetting"_s, compact ? 1 : 0}};
            if (settings->themeMode() != u"dark")
                expected.append({u"lightSurfaceSetting"_s, compact && !roomy ? -1 : 0});
            if (settings->themeMode() != u"light")
                expected.append({u"darkSurfaceSetting"_s, compact && !roomy ? -1 : 0});
            for (const auto &[name, stacked] : expected) {
                *problem = shapeProblem(name, stacked);
                if (!problem->isEmpty())
                    return false;
            }
            return true;
        };
        const bool compact = window->width() < 600;
        QString problem;
        const bool shaped = waitFor([&] { return rowsShaped(compact, true, &problem); });
        if (!shaped)
            std::fprintf(stderr, "selftest: settings rows: %s\n", qPrintable(problem));
        check(shaped, compact ? "phone settings: the background rows beside their text, Motyw and Rozmiar tekstu under it, filled"
                              : "settings rows: every control beside its text, at the row's right edge");
        if (compact) {
            const QSize phone = window->size();
            window->resize(360, 740);
            const bool narrowShaped = waitFor([&] { return window->width() == 360 && rowsShaped(true, false, &problem); });
            if (!narrowShaped)
                std::fprintf(stderr, "selftest: settings rows at 360 px: %s\n", qPrintable(problem));
            check(narrowShaped, "360 px settings: each row beside its text or under it, filled, no label cut");
            window->resize(phone);
            waitFor([&] { return window->size() == phone; });
            settle(200);
        }
    }

    check(activate(shownItem(u"accentSwatch_green"_s)), "the appearance settings show the accent swatches");
    check(waitFor([&] {
              return settings->accentColor() == u"green" && color("accent") != u"#ff7a66"
                     && color("accent") != u"#c4422f";
          }),
          "a tap on a swatch changes the accent at once and saves it");

    // A custom colour: the dialog's field, then "Use this colour".
    check(activate(shownItem(u"accentSwatch_custom"_s)), "the custom colour swatch exists");
    QQuickItem *field = shownItem(u"accentDialogField"_s);
    if (!field) {
        check(false, "the custom colour opens the accent dialog");
    } else {
        field->setProperty("text", u"#1A6B8F"_s);
        QMetaObject::invokeMethod(field, "textEdited");
        QObject *accentDialog = window->findChild<QObject *>(u"accentDialog"_s);
        if (accentDialog)
            QMetaObject::invokeMethod(accentDialog, "tryAccept");
        check(accentDialog
                      && waitFor([&] {
                             return settings->accentColor() == u"custom" && settings->customAccent() == u"#1a6b8f"
                                    && theme->property("customAccent").toString() == u"#1a6b8f";
                         }),
              "a colour typed in the accent dialog becomes the accent");
        // Its closing transition: the modal scrim takes taps until it is gone.
        if (accentDialog)
            waitFor([accentDialog] { return !accentDialog->property("visible").toBool(); });
        settle(100);
    }

    // Sidebar entries: "Graf" unticked.
    check(activate(shownItem(u"sidebarItem_graph"_s)), "the sidebar's entries can be ticked off");
    const bool hidden = waitFor([&] { return settings->sidebarHidden() == QStringList{u"graph"_s}; });
    if (!hidden)
        std::fprintf(stderr, "selftest: hidden in the sidebar: %s\n", qPrintable(settings->sidebarHidden().join(u", ")));
    check(hidden, "unticking an entry hides it (saved)");
    QObject *drawer = window->findChild<QObject *>(u"shellDrawer"_s);
    if (narrow && drawer)
        QMetaObject::invokeMethod(drawer, "open");
    check(shownItem(u"sidebarNav_calendar"_s) != nullptr
                  && !findShown(window->contentItem(),
                                [](QQuickItem *i) { return i->objectName() == u"sidebarNav_graph"; }),
          "the hidden entry is gone from the sidebar, the others stay");
    if (narrow && drawer) {
        QMetaObject::invokeMethod(drawer, "close");
        waitFor([drawer] { return !drawer->property("visible").toBool(); });
    }
    {
        CommandModel commands(app);
        const QVariantList all = commands.commands();
        check(std::any_of(all.cbegin(), all.cend(),
                          [](const QVariant &c) { return c.toMap().value(u"id"_s).toString() == u"go.graph"; }),
              "a view hidden in the sidebar stays in the command palette");
    }

    // The page sections' order (Layout → Order of the sections): "Pages" moved up twice goes first,
    // Favorites (a page starred) then come after the tree, in the list's footer.
    {
        const QStringList defaults{u"favorites"_s, u"recent"_s, u"pages"_s};
        check(settings->sidebarSections() == defaults, "the sidebar's sections start as Ulubione, Ostatnie, Strony");
        check(activate(shownItem(u"sidebarSectionUp_pages"_s))
                      && waitFor([&] {
                             return settings->sidebarSections()
                                    == QStringList{u"favorites"_s, u"pages"_s, u"recent"_s};
                         }),
              "a section's “up” button moves it one place up (saved)");
        // The rows are made anew for the new order: tap only once they are (a tap at the old
        // row's place would reach the section that moved down there).
        const auto rowY = [&](const QString &section) {
            QQuickItem *item = shownNamed(window->contentItem(), u"sidebarSectionUp_"_s + section);
            return item && shown(item) ? item->mapToScene(QPointF(0, 0)).y() : -1.0;
        };
        const auto rowsInOrder = [&](const QStringList &order) {
            qreal previous = -1.0;
            for (const QString &section : order) {
                const qreal y = rowY(section);
                if (y < 0 || y <= previous)
                    return false;
                previous = y;
            }
            return true;
        };
        waitFor([&] { return rowsInOrder({u"favorites"_s, u"pages"_s, u"recent"_s}); });
        // The pointer away from the rows: left where it clicked, it hovers the button of the
        // section that moved down there, whose tooltip (after 550 ms) covers the button above.
        mouseMove(window, QPointF(1, 1));
        activate(shownItem(u"sidebarSectionUp_pages"_s));
        check(waitFor([&] {
                  return settings->sidebarSections() == QStringList{u"pages"_s, u"favorites"_s, u"recent"_s};
              }),
              "and once more to the top");
        waitFor([&] { return rowsInOrder({u"pages"_s, u"favorites"_s, u"recent"_s}); });
        mouseMove(window, QPointF(1, 1));
        QQuickItem *up = shownItem(u"sidebarSectionUp_pages"_s);
        check(up && !up->isEnabled(), "the first section cannot move further up");
        Workspace *ws = app->workspace();
        const QString starred = u"Przepisy.md"_s;
        ws->setFavorite(starred, true);
        QObject *sectionDrawer = window->findChild<QObject *>(u"shellDrawer"_s);
        if (narrow && sectionDrawer)
            QMetaObject::invokeMethod(sectionDrawer, "open");
        const auto sceneY = [&](const QString &name) {
            QQuickItem *item = shownItem(name);
            return item ? item->mapToScene(QPointF(0, 0)).y() : -1.0;
        };
        check(waitFor([&] {
                  const qreal favorites = sceneY(u"sidebarFavoritesHeader"_s);
                  const qreal pages = sceneY(u"sidebarPagesHeader"_s);
                  return favorites >= 0 && pages >= 0 && favorites > pages;
              }),
              "with Strony first the favourites come after the folder tree");
        ws->setFavorite(starred, false);
        settings->setSidebarSections({});
        check(waitFor([&] {
                  return settings->sidebarSections() == defaults
                         && !shownNamed(window->contentItem(), u"sidebarFavoritesHeader"_s);
              }),
              "the default order comes back");
        if (narrow && sectionDrawer) {
            QMetaObject::invokeMethod(sectionDrawer, "close");
            waitFor([sectionDrawer] { return !sectionDrawer->property("visible").toBool(); });
        }
    }

    // Wide windows: the sidebar's button hides the page panel, the rail of views stays; the top
    // bar's search field opens the page switcher, ">" in it the command palette.
    if (!narrow) {
        QQuickItem *shellItem =
                findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
        if (shellItem)
            QMetaObject::invokeMethod(shellItem, "toggleSidebar");
        check(shellItem && waitFor([&] { return !shownNamed(window->contentItem(), u"sidebarTree"_s); })
                      && shownItem(u"shellRail"_s) && shownItem(u"sidebarNav_calendar"_s),
              "with the page panel hidden the rail of views stays");
        if (shellItem)
            QMetaObject::invokeMethod(shellItem, "toggleSidebar");
        check(waitFor([&] { return shownNamed(window->contentItem(), u"sidebarTree"_s) != nullptr; }),
              "the page panel comes back");

        QObject *switcher = window->findChild<QObject *>(u"shellSwitcher"_s);
        QObject *palette = window->findChild<QObject *>(u"shellPalette"_s);
        check(activate(shownItem(u"topBarSearchField"_s)) && switcher
                      && waitFor([&] { return switcher->property("opened").toBool(); }),
              "the top bar's search field opens the page switcher");
        sendKey(window, Qt::Key_Greater, Qt::ShiftModifier, u">"_s);
        check(palette && switcher && waitFor([&] {
                  return palette->property("opened").toBool() && !switcher->property("visible").toBool();
              }),
              "“>” in it turns to the command palette");
        if (palette)
            QMetaObject::invokeMethod(palette, "close");
        if (palette)
            waitFor([palette] { return !palette->property("visible").toBool(); });
    }

    // The bottom navigation's fourth slot: Graph instead of Tasks.
    check(shownItem(u"bottomNavSlot_4"_s) != nullptr && activate(shownItem(u"bottomNavSlot_3"_s)),
          "the bottom navigation's five slots are listed");
    check(activate(shownItem(u"bottomNavChoice_graph"_s)), "a slot's menu lists the views");
    check(waitFor([&] {
              return settings->bottomNavTabs()
                     == QStringList{u"journals"_s, u"allPages"_s, u"search"_s, u"graph"_s, u"calendar"_s};
          }),
          "a view picked in a slot's menu takes the slot");
    if (narrow)
        check(shownItem(u"bottomNav_graph"_s) != nullptr
                      && !findShown(window->contentItem(),
                                    [](QQuickItem *i) { return i->objectName() == u"bottomNav_tasks"; }),
              "the phone's bottom bar shows the view picked");
    // The view already in the slot, picked again: its menu still shows it checked.
    closeMenus(window);
    activate(shownItem(u"bottomNavSlot_3"_s));
    activate(shownItem(u"bottomNavChoice_graph"_s));
    closeMenus(window);
    activate(shownItem(u"bottomNavSlot_3"_s));
    {
        QQuickItem *again = shownItem(u"bottomNavChoice_graph"_s);
        check(again && again->property("checked").toBool(),
              "picking the slot's own view again keeps it checked in the menu");
    }
    closeMenus(window);

    // The quick note button.
    check(activate(shownItem(u"captureButtonSetting"_s)) && waitFor([&] { return !settings->captureButtonShown(); }),
          "the quick note button can be switched off");
    if (narrow) {
        app->navigate({{u"view"_s, u"journals"_s}});
        auto *capture = window->findChild<QQuickItem *>(u"shellCaptureButton"_s);
        check(capture && waitFor([&] { return app->currentView() == u"journals"; }) && !capture->isVisible(),
              "switched off, the quick note button is not shown");
        settings->setCaptureButtonShown(true);
        QQuickItem *topBar = window->findChild<QQuickItem *>(u"shellTopBar"_s);
        // (Laid out with the bar's next polish.)
        check(capture && topBar && waitFor([&] {
                  return capture->isVisible() && capture->width() > 0
                         && capture->mapToScene(QPointF(0, capture->height())).y()
                                    <= topBar->mapToScene(QPointF(0, topBar->height())).y() + 1;
              }),
              "switched on again, it is back on the top bar");
        // Larger text (Appearance → Text size: Larger): the five labels stay whole — a long one
        // ("Kalendarz") takes room from the short ones.
        QQuickItem *bar = window->findChild<QQuickItem *>(u"shellBottomNav"_s);
        const qreal scale = settings->fontScale();
        settings->setFontScale(1.3);
        QString cut;
        int labels = 0;
        const bool whole = bar && waitFor([&] {
            cut.clear();
            labels = 0;
            bool scaled = true;
            const QList<QQuickItem *> items = uidriver::allItems(bar);
            for (QQuickItem *item : items) {
                if (!item->inherits("QQuickText") || !shown(item))
                    continue;
                ++labels;
                scaled = scaled && item->property("font").value<QFont>().pixelSize() == qRound(12 * 1.3);
                if (item->property("truncated").toBool())
                    cut += item->property("text").toString() + u' ';
            }
            return labels == 5 && scaled && cut.isEmpty();
        });
        if (!whole)
            std::fprintf(stderr, "selftest: bottom bar labels %d, cut: %s\n", labels, qPrintable(cut));
        check(whole, "at a larger text size the bottom bar's five labels stay whole");
        settings->setFontScale(scale);
        app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"appearance"_s}});
    }

    // Back to the default look (the visibility of elements stays): the end of the Appearance card,
    // asked first (no undo), then the default coral.
    {
        QQuickItem *reset = shownItem(u"resetAppearance"_s);
        QQuickItem *card = reset;
        while (card && card->objectName() != u"settingsAppearance")
            card = card->parentItem();
        check(card != nullptr, "\"Restore the default look…\" is in the Appearance card");
        activate(reset);
        const QString restoreText = QCoreApplication::translate("SettingsView", "Restore");
        QQuickItem *restoreButton = nullptr;
        check(waitFor([&] {
                  restoreButton = findShown(window->contentItem(), [&](QQuickItem *item) {
                      return isA(item, "BButton") && item->property("text").toString() == restoreText;
                  });
                  return restoreButton != nullptr;
              }) && settings->accentColor() == u"custom",
              "\"Restore the default look…\" asks first (nothing restored yet)");
        settle(250);
        if (restoreButton)
            tapOrClick(window, pointIn(restoreButton), touch);
        check(waitFor([&] {
                  return settings->accentColor() == u"coral"
                         && (color("accent") == u"#ff7a66" || color("accent") == u"#c4422f");
              }),
              "its “Restore” brings the default coral back");
    }

    // Settings → Editor → Font → "Other…" opens the picker; cancelled, the selection stays on the
    // serif / sans-serif choice, and "Other…" opens the picker again. A missing family names the
    // font used instead.
    app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"editor"_s}});
    // The text width: Normal (680 px, about 85 characters) by default.
    {
        QQuickItem *width = shownItem(u"editorWidthSetting"_s);
        check(width && settings->editorWidth() == 680 && width->property("currentIndex").toInt() == 1
                      && width->property("currentValue").toInt() == 680,
              "Edytor → Szerokość tekstu: Normalna (680 px) by default");
    }
    if (QQuickItem *fontSetting = shownItem(u"editorFontSetting"_s)) {
        uidriver::scrollIntoView(window, fontSetting);
        // The picker of the settings view shown (a window can hold more than one settings view).
        const auto openPicker = [window]() -> QObject * {
            const QList<QObject *> popups = uidriver::openPopups(window);
            const auto it = std::find_if(popups.cbegin(), popups.cend(),
                                         [](QObject *p) { return p->objectName() == u"fontPickerDialog"; });
            return it != popups.cend() ? *it : nullptr;
        };
        QObject *fontPicker = nullptr;
        bool stays = true;
        for (int round = 0; round < 2 && stays; ++round) {
            uidriver::callQml(fontSetting, "select", {2});
            stays = waitFor([&] {
                fontPicker = openPicker();
                return fontPicker && fontPicker->property("opened").toBool();
            });
            if (fontPicker) {
                QMetaObject::invokeMethod(fontPicker, "close");
                stays = waitFor([&] { return !openPicker(); }) && stays;
            }
            stays = stays && fontSetting->property("currentValue").toString() == settings->editorFont();
        }
        check(stays, "\"Other…\" opens the font picker; cancelled, the font selection does not move");
        QQuickItem *fontRow = shownItem(u"editorFontRow"_s);
        settings->setEditorFont(u"serif"_s);
        settings->setEditorFontFamily(u"Łódź Nieobecna Ś"_s);
        const QString serifNote = fontRow ? fontRow->property("description").toString() : QString();
        settings->setEditorFont(u"sans"_s);
        const QString sansNote = fontRow ? fontRow->property("description").toString() : QString();
        check(fontRow && serifNote.contains(u"Łódź Nieobecna Ś"_s) && sansNote.contains(u"Łódź Nieobecna Ś"_s)
                      && serifNote != sansNote && fontSetting->property("currentValue").toString() == u"other",
              "a missing editor font names the serif or sans-serif font used instead");
        settings->setEditorFontFamily(QString());
        settings->setEditorFont(u"serif"_s);
        check(fontSetting->property("currentValue").toString() == u"serif", "the font selection follows the settings");
    } else {
        check(false, "the editor settings show the font choice");
    }

    settings->setSidebarHidden({});
    settings->setBottomNavTabs({});
    settings->setCaptureButtonShown(true);
    settings->setCustomAccent(QString());
    closeMenus(window);
    app->goHome();
}

/// 3k. A name written with markup (a synced or imported file can be named anything without a "/",
///     so its tags stay open) is quoted as written by the dialogs: "Move …"'s title and the trash
///     confirmation show it as plain text, never as styled text (BDialog, BConfirmDialog). Windows
///     allows no "<" or ">" in file names: there entities stand for the markup.
void checkNamesWithMarkup(QQuickWindow *window, AppController *app, const Check &check)
{
#ifdef Q_OS_WIN
    const QString name = u"&lt;b&gt;Ważne &lt;i&gt;notatki"_s;
#else
    const QString name = u"<b>Ważne <i>notatki"_s;
#endif
    const QString path = name + u".md"_s;
    Workspace *ws = app->workspace();
    check(ws->writeFile(path, u"Treść.\n"_s) && waitFor([&] { return ws->exists(path); }),
          "a page named with markup is added to the notebook");
    // The shown text item that reads `text` exactly — in plain text (Text.PlainText is 0).
    const auto plainTextShown = [&](const QString &text) {
        QQuickItem *label = nullptr;
        waitFor([&] {
            label = findShown(window->contentItem(), [&](QQuickItem *item) {
                return item->inherits("QQuickText") && item->property("text").toString() == text;
            });
            return label != nullptr;
        });
        return label && label->property("textFormat").toInt() == 0;
    };
    const auto closeDialog = [&](QObject *dialog) {
        if (dialog)
            QMetaObject::invokeMethod(dialog, "close");
        waitFor([&] { return !dialog || !dialog->property("visible").toBool(); });
    };

    app->requestItemAction(u"move"_s, path);
    QObject *moveDialog = window->findChild<QObject *>(u"shellMoveDialog"_s);
    check(moveDialog && waitFor([&] { return moveDialog->property("opened").toBool(); })
                  && plainTextShown(QCoreApplication::translate("ShellMoveDialog", "Move “%1”").arg(name)),
          "the move dialog's title quotes a name with markup as written");
    closeDialog(moveDialog);

    app->requestItemAction(u"delete"_s, path);
    QObject *deleteDialog = window->findChild<QObject *>(u"shellDeleteDialog"_s);
    check(deleteDialog && waitFor([&] { return deleteDialog->property("opened").toBool(); })
                  && plainTextShown(QCoreApplication::translate(
                                            "AppShell", "“%1” will be moved to the trash. You can restore it from there.")
                                            .arg(name)),
          "the trash confirmation quotes a name with markup as written");
    closeDialog(deleteDialog);
    check(ws->exists(path) && ws->vault()->removePermanently(path), "the page named with markup is removed again");
    closeMenus(window);
    app->goHome();
}

/// 3l. A language switched at run time reaches the frame's dates: the tree's daily notes are
///     labelled in the new language (their binding reads DatesHelper.locale: a method call alone is
///     not tracked, and the days kept the old language). Desktop: the sidebar's tree. The language
///     is put back.
void checkLanguageFollows(QQuickWindow *window, AppController *app, const Check &check)
{
    if (window->width() < 900)
        return;
    QQuickItem *shellItem = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
    if (!shellItem) {
        check(false, "the shell is shown");
        return;
    }
    const auto evaluate = [&](const QString &js) {
        QQmlExpression expression(qmlContext(shellItem), shellItem, js);
        const QVariant value = expression.evaluate();
        if (expression.hasError())
            std::fprintf(stderr, "selftest: %s\n", qPrintable(expression.error().toString()));
        return value;
    };
    const QString journal = app->workspace()->journalPath(app->today(0));
    app->requestItemAction(u"reveal"_s, journal);
    const auto dayRow = [&]() -> QQuickItem * {
        QQuickItem *list = shownNamed(window->contentItem(), u"sidebarTree"_s);
        return list ? findShown(list,
                                [&](QQuickItem *item) {
                                    return item->property("isDir").isValid()
                                           && item->property("path").toString() == journal;
                                })
                    : nullptr;
    };
    QQuickItem *row = nullptr;
    QString before;
    check(waitFor([&] {
              row = dayRow();
              before = row ? row->property("text").toString() : QString();
              return !before.isEmpty();
          }),
          "today's daily note is in the tree");
    const QString language = evaluate(u"DevTools.language"_s).toString();
    const QString other = language == u"en" ? u"pl"_s : u"en"_s;
    evaluate(u"DevTools.setLanguage(\"%1\")"_s.arg(other));
    QString after;
    const bool switched = waitFor([&] {
        row = dayRow();
        after = row ? row->property("text").toString() : QString();
        return !after.isEmpty() && after != before;
    });
    if (!switched || !after.contains(other == u"en" ? u"Oct"_s : u"paź"_s))
        std::fprintf(stderr, "selftest: the day's label: %s, then %s\n", qPrintable(before), qPrintable(after));
    check(switched && after.contains(other == u"en" ? u"Oct"_s : u"paź"_s),
          "the tree's daily notes are labelled in the language switched to");
    evaluate(u"DevTools.setLanguage(\"%1\")"_s.arg(language));
    check(waitFor([&] { return (row = dayRow()) && row->property("text").toString() == before; }),
          "and in the first language again");
    app->goHome();
}

int runChecks(QQmlApplicationEngine *engine, AppController *app)
{
    int failures = 0;
    const auto check = [&failures](bool ok, const char *what) {
        std::fprintf(stderr, "selftest: %s %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok)
            ++failures;
    };
    const auto view = [app] { return app->currentView(); };

    if (!waitFor([app] { return app->status() == u"ready"; }, 20000)) {
        check(false, "the notebook opens");
        return failures;
    }
    auto *window = qobject_cast<QQuickWindow *>(engine->rootObjects().value(0));
    if (!window) {
        check(false, "the main window exists");
        return failures;
    }
    window->requestActivate();
    waitFor([window] { return window->isActive(); }, 2000);
    const auto find = [window](const char *name) { return window->findChild<QObject *>(QString::fromLatin1(name)); };
    QObject *drawer = find("shellDrawer");
    QObject *palette = find("shellPalette");
    QObject *capture = find("shellCapture");
    if (!drawer || !palette || !capture) {
        check(false, "the drawer, the command palette and quick capture exist");
        return failures;
    }
    const auto isOpen = [](QObject *popup) { return popup->property("visible").toBool(); };

    // 0. Started with --capture (a launcher action, or the tray while the window was hidden):
    //    quick capture opens by itself; Escape closes it and the keys reach the shell again.
    if (QCoreApplication::arguments().contains(u"--capture"_s)) {
        check(waitFor([&] { return capture->property("opened").toBool(); }), "--capture opens quick capture");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(capture); }), "Escape closes quick capture");
    }

    // 1. The back key walks the history and is handled; at home it is left to the system.
    app->navigate({ { u"view"_s, u"tasks"_s } });
    bool handled = sendKey(window, Qt::Key_Back);
    check(handled && view() == u"journals", "back goes back through the history");
    handled = sendKey(window, Qt::Key_Back);
    check(!handled && view() == u"journals", "back at home is not handled (Android may close the app)");

    // 2. + 3. The drawer (narrow windows; wide ones have the sidebar instead).
    if (window->width() < 900) {
        // Escape closes the drawer — and nothing else happens.
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor([&] { return drawer->property("opened").toBool(); }), "the drawer opens");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(drawer); }) && view() == u"journals",
              "Escape closes the drawer and stays on the view");

        // An open drawer owns the keys: back/Escape never act on the view underneath it.
        app->navigate({ { u"view"_s, u"tasks"_s } });
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor([&] { return drawer->property("opened").toBool(); }), "the drawer opens again");
        sendKey(window, Qt::Key_Back);
        check(view() == u"tasks", "back with the drawer open does not navigate underneath it");
        sendKey(window, Qt::Key_Escape);
        check(waitFor([&] { return !isOpen(drawer); }) && view() == u"tasks", "Escape closes the drawer");
        handled = sendKey(window, Qt::Key_Back);
        check(handled && view() == u"journals", "after the drawer closed, back reaches the shell again");
    }

    // 3b. Rows with context menus: taps / clicks act, long presses / right-clicks open the menu.
    checkRowsAndMenus(window, app, check);
    // 3c. The task status menus (Tasks, backlinks, the agenda, a journal day) and "/" commands.
    checkTaskStatusMenus(window, app, check);
    closeMenus(window); // whatever a failed check left open
    // 3d. Tags name pages: a tag opens its page (journal, Tasks with Ctrl+click / long press, the
    //     tag list), the tag overview stays a tap away.
    checkTagLinks(window, app, check);
    closeMenus(window);
    // 3e. Folders: reveal, the row menu's "Move to…", a root folder, drag and drop (desktop),
    //     All pages as a folder browser.
    checkFolders(window, app, check);
    closeMenus(window);
    // 3e'. "Clean up": what nothing refers to, picked with real input, to the trash and back.
    checkCleanup(window, app, check);
    closeMenus(window);
    // 3e''. "Backups": a backup, a restore through its confirmation and a delete.
    checkBackups(window, app, check);
    closeMenus(window);
    // 3f. "Share → Brulion": the share dialog, to the journal and to a new page.
    checkShare(window, app, check);
    if (drawer->property("visible").toBool()) {
        QMetaObject::invokeMethod(drawer, "close");
        waitFor([&] { return !isOpen(drawer); });
    }
    app->goHome();
    // 3g. Find and replace: the bar in a note and over the journal's days, the notebook-wide view.
    checkFindAndReplace(window, app, check);
    closeMenus(window);
    // 3h. Weekly and monthly notes: opened from the shortcut's action, the page's arrows, the calendar.
    checkPeriodicNotes(window, app, check);
    closeMenus(window);
    // 3h'. Habits: added in the view, today's check ticked and opened again, a tick from the journal.
    checkHabits(window, app, check);
    closeMenus(window);
    // 3i. Focus mode, the word count, typewriter scrolling and a custom shortcut.
    checkFocusAndShortcuts(window, app, check);
    // 3j. A page's icon and cover: the page menu, their dialogs, the header, the sidebar's tree.
    checkPageLook(window, app, check);
    closeMenus(window);
    // 3k. Names with markup in the dialogs that quote them (Move, the trash confirmation).
    checkNamesWithMarkup(window, app, check);
    // 3l. A language switched at run time: the tree's daily notes follow.
    checkLanguageFollows(window, app, check);

    // 4. The command palette: Escape closes it; the focus returns to the shell.
    app->navigate({ { u"view"_s, u"tasks"_s } });
    app->requestUiAction(u"palette"_s);
    check(waitFor([&] { return palette->property("opened").toBool(); }), "the command palette opens");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !isOpen(palette); }) && view() == u"tasks", "Escape closes the palette");
    handled = sendKey(window, Qt::Key_Back);
    check(handled && view() == u"journals", "after the palette closed, back reaches the shell again");

    // 4b. The rows of the palette and the quick switcher (docs/UI.md "Shared components",
    //     BPalette): floating (wide windows) every row is one line high (Theme.listItemHeight), a
    //     subtitle on the title's line after it; full screen (phones) a subtitled row has two lines,
    //     the subtitle under the title. The query shows subtitled rows ("New folder…" — "At the
    //     notebook root", the page "Remont mieszkania" in Projekty).
    {
        auto *theme = engine->singletonInstance<QObject *>("Brulion", "Theme");
        const auto rowsLaidOut = [theme](QObject *popup, QString *detail) {
            auto *content = popup->property("contentItem").value<QQuickItem *>();
            if (!content || !theme) {
                *detail = u"no list"_s;
                return false;
            }
            const bool floating = !popup->property("fullScreen").toBool();
            const qreal oneLine = theme->property("listItemHeight").toReal();
            const qreal twoLine = theme->property("listItemHeightTwoLine").toReal();
            int rows = 0;
            int subtitled = 0;
            const QList<QQuickItem *> items = uidriver::allItems(content);
            for (QQuickItem *row : items) {
                if (!isA(row, "BListItem") || !shown(row))
                    continue;
                ++rows;
                const QString title = row->property("text").toString();
                const QString subtitle = row->property("subtitle").toString();
                const qreal expected = floating || subtitle.isEmpty() ? oneLine : twoLine;
                if (qAbs(row->height() - expected) > 0.5) {
                    *detail = u"“%1” is %2 px high, not %3"_s.arg(title).arg(row->height()).arg(expected);
                    return false;
                }
                if (subtitle.isEmpty())
                    continue;
                QQuickItem *sub = nullptr;
                const QList<QQuickItem *> parts = uidriver::allItems(row);
                for (QQuickItem *part : parts) {
                    if (part->inherits("QQuickText") && part->property("text").toString() == subtitle)
                        sub = part;
                }
                QQuickItem *titleText = sub && sub->parentItem() ? sub->parentItem()->childItems().value(0) : nullptr;
                if (!sub || !titleText || !shown(sub)) {
                    *detail = u"“%1” does not show its subtitle"_s.arg(title);
                    return false;
                }
                const QRectF t = titleText->mapRectToScene(QRectF(0, 0, titleText->width(), titleText->height()));
                const QRectF u = sub->mapRectToScene(QRectF(0, 0, sub->width(), sub->height()));
                const bool sameLine = qAbs(u.center().y() - t.center().y()) < t.height() / 2 && u.left() >= t.right();
                const bool below = u.top() >= t.bottom() - 0.5;
                if (floating ? !sameLine : !below) {
                    *detail = u"“%1”: the subtitle is not %2 the title"_s.arg(title, floating ? u"beside"_s : u"under"_s);
                    return false;
                }
                ++subtitled;
            }
            *detail = u"%1 rows, %2 with a subtitle"_s.arg(rows).arg(subtitled);
            return rows > 0 && subtitled > 0;
        };
        // Every command (no query): a floating list fills its room with whole rows — it once stayed
        // one row high when every row had the same height (nothing changed contentHeight to measure
        // it again); full screen it fills the screen.
        if (!palette->property("fullScreen").toBool()) {
            QMetaObject::invokeMethod(palette, "open");
            QQuickItem *list = nullptr;
            const auto filled = [&] {
                auto *content = palette->property("contentItem").value<QQuickItem *>();
                const QList<QQuickItem *> items = content ? uidriver::allItems(content) : QList<QQuickItem *>();
                const auto it = std::find_if(items.cbegin(), items.cend(),
                                             [](QQuickItem *item) { return item->inherits("QQuickListView"); });
                list = it != items.cend() ? *it : nullptr;
                if (!list || !theme)
                    return false;
                const qreal room = palette->property("_listRoom").toReal();
                const qreal all = list->property("contentHeight").toReal() + 12;
                const qreal row = theme->property("listItemHeight").toReal();
                return list->height() + row > std::min(all, room) && list->height() <= room + 0.5;
            };
            const bool full = waitFor([&] { return palette->property("opened").toBool() && filled(); });
            if (!full && list)
                std::fprintf(stderr, "selftest: palette list %g px high, room %g, content %g\n", list->height(),
                             palette->property("_listRoom").toReal(), list->property("contentHeight").toReal());
            check(full, "the floating command palette shows as many whole rows as fit, not one");
            // Scrolled to its end (keys, the wheel), far past the first rows, which the view then
            // releases: the list keeps its height — it once shrank to its margins, no row shown.
            if (list)
                QMetaObject::invokeMethod(list, "positionViewAtEnd");
            const qreal room = palette->property("_listRoom").toReal();
            const bool stillFull = waitFor([&] {
                return list && list->property("contentY").toReal() > room && filled();
            });
            if (!stillFull && list)
                std::fprintf(stderr, "selftest: scrolled palette list %g px high, room %g, contentY %g\n", list->height(),
                             room, list->property("contentY").toReal());
            check(stillFull, "the floating command palette keeps its rows when scrolled down");
            QMetaObject::invokeMethod(palette, "close");
            waitFor([&] { return !isOpen(palette); });
        }
        QObject *switcher = find("shellSwitcher");
        const QList<std::pair<QObject *, QString>> popups{{palette, u"folder"_s}, {switcher, u"rem"_s}};
        for (const auto &[popup, query] : popups) {
            if (!popup) {
                check(false, "the quick switcher exists");
                continue;
            }
            const bool isPalette = popup == palette;
            QMetaObject::invokeMethod(popup, "open");
            const bool opened = waitFor([&] { return popup->property("opened").toBool(); });
            uidriver::callQml(popup, "typeQuery", {query});
            QString detail;
            const bool laidOut = opened && waitFor([&] { return rowsLaidOut(popup, &detail); });
            if (!laidOut)
                std::fprintf(stderr, "selftest: %s rows: %s\n", isPalette ? "palette" : "switcher", qPrintable(detail));
            check(laidOut, isPalette ? "the command palette's rows: one line when floating (a subtitle beside the title), two on a phone"
                                     : "the quick switcher's rows: one line when floating (the folder beside the title), two on a phone");
            QMetaObject::invokeMethod(popup, "close");
            waitFor([&] { return !isOpen(popup); });
        }
    }

    // 5. The block editor in the shell: real keys reach the edited block and are saved, Ctrl+E
    //    and the "Markdown source" command switch source mode, a link to a missing page opens
    //    that page ready to type (and typing creates it).
    {
        const QString guide = u"Witaj w Brulionie.md"_s;
        // The controller of the page view shown for `path`: views a Loader has let go of (no parent
        // item) may still be around with their controllers until they are deleted.
        const auto controllerFor = [window](const QString &path) -> QObject * {
            const QList<QObject *> found = window->findChildren<QObject *>(u"pageController"_s);
            for (QObject *c : found) {
                const auto *shownView = qobject_cast<QQuickItem *>(c->parent());
                if (c->property("path").toString() == path && (!shownView || shownView->parentItem()))
                    return c;
            }
            return nullptr;
        };
        const auto editorOf = [window](QObject *controller) -> QQuickItem * {
            const QList<QQuickItem *> found = window->findChildren<QQuickItem *>(u"pageEditor"_s);
            for (QQuickItem *e : found) {
                if (e->property("controller").value<QObject *>() == controller)
                    return e;
            }
            return nullptr;
        };
        const auto source = [](QObject *c) { return c ? c->property("sourceText").toString() : QString(); };
        // The block's text editor has the keyboard (its delegate is created a frame later).
        const auto editorFocused = [window] {
            return waitFor([window] {
                const QQuickItem *f = window->activeFocusItem();
                return f && QByteArray(f->metaObject()->className()).contains("InlineEditor");
            });
        };
        const auto typeText = [window](const QString &text) {
            for (const QChar ch : text)
                sendKey(window, 0, Qt::NoModifier, QString(ch));
        };

        app->openPage(guide);
        QObject *page = nullptr;
        check(waitFor([&] {
                  page = controllerFor(guide);
                  return page && page->property("loaded").toBool();
              }),
              "the guide page opens in the editor");
        QQuickItem *editor = page ? editorOf(page) : nullptr;
        if (page && editor) {
            // Mouse clicks on a block's rendered rich text: a click on words starts editing that
            // block, a click on a link follows it. (A TapHandler on the rich Text itself lost every
            // mouse press to the editor's ListView: nothing happened on click.)
            // Phones (--touch) tap with a finger, desktops click with the mouse.
            const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
            settle(300);
            // The guide's first list block (under "Konspekt", the Polish guide's outline section),
            // scrolled into view (on a phone it is below the first screen).
            {
                const QStringList guideLines = app->workspace()->readFile(guide).split(u'\n');
                const qsizetype blockLine =
                        guideLines.indexOf(u"- Każdy punkt listy to **blok**. Kliknij go, żeby edytować."_s);
                int blockRow = -1;
                QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(blockRow), int(blockLine));
                if (auto *list = editor->property("listView").value<QQuickItem *>(); list && blockRow >= 0)
                    QMetaObject::invokeMethod(list, "positionViewAtIndex", blockRow, 1 /* ListView.Center */);
                settle(200);
            }
            if (QQuickItem *text = renderedTextContaining(editor, u"Kliknij go"_s)) {
                // A quick tap (30 ms, shorter than the lists' 90 ms press delay on touch UIs).
                if (touch)
                    touchTap(window, text->mapToScene(QPointF(8, text->height() / 2)), 30);
                else
                    mouseClick(window, text->mapToScene(QPointF(8, text->height() / 2)));
                check(waitFor([&] { return page->property("editingRow").toInt() >= 0; }) && editorFocused(),
                      "a click or tap on a block's text starts editing it");
                QMetaObject::invokeMethod(page, "stopEditing");
                settle(100);
                // Scrolling with a finger that starts on text scrolls and never starts editing.
                if (touch) {
                    auto *list = editor->property("listView").value<QQuickItem *>();
                    const qreal before = list ? list->property("contentY").toReal() : 0;
                    touchDrag(window, text->mapToScene(QPointF(text->width() / 2, text->height() / 2)), QPointF(0, -160));
                    check(list && list->property("contentY").toReal() > before + 40
                              && page->property("editingRow").toInt() < 0,
                          "a finger dragged over block text scrolls the page and does not start editing");
                    QMetaObject::invokeMethod(list, "positionViewAtBeginning");
                    settle(200);
                }
            } else {
                check(false, "the guide shows its first outline block");
            }
            // The guide's "Link do strony: [[Remont mieszkania]]" block, scrolled to the middle.
            {
                const QStringList guideLines = app->workspace()->readFile(guide).split(u'\n');
                qsizetype linkLine = -1;
                for (qsizetype i = 0; i < guideLines.size() && linkLine < 0; ++i) {
                    if (guideLines.at(i).startsWith(u"- Link do strony:"_s))
                        linkLine = i;
                }
                int linkRow = -1;
                QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(linkRow), int(linkLine));
                if (auto *list = editor->property("listView").value<QQuickItem *>(); list && linkRow >= 0)
                    QMetaObject::invokeMethod(list, "positionViewAtIndex", linkRow, 1 /* ListView.Center */);
                settle(200);
            }
            if (QQuickItem *text = renderedTextContaining(editor, u"page:Remont"_s)) {
                const QPointF at = linkPoint(text, u"page:Remont"_s);
                check(at.x() >= 0, "the guide shows a link to the renovation project");
                tapOrClick(window, at, touch);
                check(waitFor([app] { return app->currentRoute().value(u"path"_s).toString() == u"Projekty/Remont mieszkania.md"; }),
                      "a click or tap on a link opens the linked page");
                app->back();
                waitFor([&] {
                    page = controllerFor(guide);
                    return app->currentRoute().value(u"path"_s).toString() == guide && page
                           && page->property("loaded").toBool();
                });
                editor = page ? editorOf(page) : nullptr;
            } else {
                check(false, "the guide shows its link to the renovation project");
            }
        }
        if (page && editor) {
            const QStringList lines = app->workspace()->readFile(guide).split(u'\n');
            const qsizetype line = lines.indexOf(u"- Każdy punkt listy to **blok**. Kliknij go, żeby edytować."_s);
            int row = -1;
            QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(row), int(line));
            check(line >= 0 && row >= 0, "the guide has its first outline block");
            uidriver::callQml(editor, "startEditing", {QVariant(row), QVariant(-1)});
            check(waitFor([&] { return page->property("editingRow").toInt() == row; }) && editorFocused(),
                  "a block starts editing and has the keyboard");
            typeText(u" Żółw ąę"_s);
            check(source(page).contains(u"żeby edytować. Żółw ąę"_s), "typed keys reach the block (Polish letters)");
            // Another program (an agent's brulion-cli, Syncthing) changes the page while the block
            // is being edited and no toast is shown: the typing goes on in the block — neither the
            // "merged" toast (the first one of its list) nor the editor list's current row takes
            // the keyboard (the keys after it went nowhere).
            if (QObject *toasts = window->findChild<QObject *>(u"toastHost"_s))
                QMetaObject::invokeMethod(toasts, "clear");
            settle(200);
            typeText(u" i"_s);
            {
                QFile file(app->workspace()->rootPath() + u'/' + guide);
                if (file.open(QIODevice::Append))
                    file.write("\n- Dopisek innego programu: żółw\n");
            }
            check(waitFor([&] { return source(page).contains(u"Dopisek innego programu: żółw"_s); }),
                  "another program's change reaches the page being edited");
            settle(300);
            check(editorFocused() && page->property("editingRow").toInt() == row,
                  "the edited block keeps the keyboard after another program's change");
            typeText(u" jeż"_s);
            check(source(page).contains(u"Żółw ąę i jeż"_s), "keys typed after that change reach the block");
            // Any toast arriving while typing (a reminder, a sync) leaves the keyboard there too.
            if (QObject *toasts = window->findChild<QObject *>(u"toastHost"_s))
                QMetaObject::invokeMethod(toasts, "clear");
            settle(200);
            app->toast(u"Zsynchronizowano: żółw"_s, u"info"_s);
            settle(300);
            check(editorFocused(), "a toast shown while typing leaves the keyboard in the edited block");
            // Ctrl+K is the command palette, but Qt binds it to "delete to the end of the line" in
            // text editors on Linux: the edited block must leave it to the shell's shortcut (the
            // shortcut map asks the focused item with a ShortcutOverride event first).
            if (QQuickItem *focused = window->activeFocusItem()) {
                QKeyEvent override(QEvent::ShortcutOverride, Qt::Key_K, Qt::ControlModifier);
                override.ignore();
                QCoreApplication::sendEvent(focused, &override);
                check(!override.isAccepted(), "a block being edited leaves Ctrl+K to the command palette");
            }
            sendKey(window, Qt::Key_E, Qt::ControlModifier);
            check(page->property("sourceMode").toBool(), "Ctrl+E in a block switches to Markdown source");
            check(waitFor([&] { return app->sourceModeShown(); }),
                  "Markdown source turns the palette's block link commands off (AppController.sourceModeShown)");
            // Phones: a finger dragged over the source scrolls it (a TextEdit that selects by
            // dragging kept the finger: the source never moved) and selects nothing; a tap still
            // puts the cursor where it lands.
            if (QCoreApplication::arguments().contains(u"--touch"_s)) {
                QQuickItem *sourceView = nullptr;
                QQuickItem *sourceEdit = nullptr;
                check(waitFor([&] {
                          sourceView = findShown(editor, [](QQuickItem *item) { return isA(item, "SourceEditor"); });
                          // (The whole file: its middle lies far below the screen.)
                          const QList<QQuickItem *> edits =
                                  sourceView ? sourceView->findChildren<QQuickItem *>() : QList<QQuickItem *>();
                          const auto it = std::find_if(edits.cbegin(), edits.cend(), [](QQuickItem *item) {
                              return item->inherits("QQuickTextEdit") && item->isVisible();
                          });
                          sourceEdit = it != edits.cend() ? *it : nullptr;
                          return sourceEdit
                                 && sourceView->property("contentHeight").toReal() > sourceView->height() + 400;
                      }),
                      "the guide's Markdown source is longer than the screen");
                if (sourceView && sourceEdit) {
                    const qreal before = sourceView->property("contentY").toReal();
                    touchDrag(window, pointIn(sourceView, 0.5, 0.7), QPointF(0, -300));
                    check(waitFor([&] { return sourceView->property("contentY").toReal() > before + 100; })
                                  && sourceEdit->property("selectedText").toString().isEmpty(),
                          "a finger dragged 300 px over the Markdown source scrolls it and selects nothing");
                    // (A tap on a moving list only stops it.)
                    waitFor([&] { return !sourceView->property("moving").toBool(); });
                    const int cursorBefore = sourceEdit->property("cursorPosition").toInt();
                    // The finger lands in the middle of the line at the view's centre, on a whole
                    // pixel as touch points are (a point near a line's edge would be rounded onto
                    // the next line). The text under it is read before the tap: the tap brings up
                    // the touch toolbar, and the view keeps the cursor above it.
                    QPointF at = pointIn(sourceView, 0.5, 0.5);
                    {
                        const QPointF centre = sourceEdit->mapFromScene(at);
                        int pos = -1;
                        QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(pos), centre.x(), centre.y());
                        QRectF lineRect;
                        QMetaObject::invokeMethod(sourceEdit, "positionToRectangle", qReturnArg(lineRect), pos);
                        at = QPointF(sourceEdit->mapToScene(QPointF(centre.x(), lineRect.center().y())).toPoint());
                    }
                    const QPointF local = sourceEdit->mapFromScene(at);
                    int expected = -1;
                    QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(expected), local.x(), local.y());
                    const qreal contentBefore = sourceView->property("contentY").toReal();
                    touchTap(window, at);
                    const int cursor = sourceEdit->property("cursorPosition").toInt();
                    std::fprintf(stderr,
                                 "selftest: the tap in the source at (%.1f, %.1f): cursor %d (before %d, under the "
                                 "finger %d), contentY %.1f -> %.1f\n",
                                 at.x(), at.y(), cursor, cursorBefore, expected, contentBefore,
                                 sourceView->property("contentY").toReal());
                    check(cursor != cursorBefore && qAbs(cursor - expected) <= 1
                                  && sourceEdit->property("selectedText").toString().isEmpty(),
                          "a tap in the Markdown source puts the cursor there");
                    // A long press selects the word under the finger (where the platform has
                    // selection handles, they take it on from there). First the view settles
                    // after the tap: the toolbar it brought up and the keyboard shrink the view and
                    // keep the cursor above them. A view that moved during the hold took the text
                    // away from under the finger (the press is cancelled), which on a slow runner
                    // (Windows) happened when only 150 ms of stillness were waited for.
                    qreal settledY = -1;
                    qreal settledHeight = -1;
                    QElapsedTimer stillFor;
                    waitFor([&] {
                        const qreal y = sourceView->property("contentY").toReal();
                        const qreal height = sourceView->height();
                        if (y != settledY || height != settledHeight || sourceView->property("moving").toBool()
                            || sourceView->property("keyboardSliding").toBool()) {
                            settledY = y;
                            settledHeight = height;
                            stillFor.start();
                            return false;
                        }
                        return stillFor.elapsed() >= 400;
                    });
                    const qreal scrolled = sourceView->property("contentY").toReal();
                    const auto textAt = [&](int from, int to) {
                        QString text;
                        QMetaObject::invokeMethod(sourceEdit, "getText", qReturnArg(text), from, to);
                        return text;
                    };
                    // A point inside a word, read where the finger lands: on a whole pixel (touch
                    // points are) in the middle of its line. A fractional point checked here was
                    // rounded onto the next line by the touch event when a line's edge lay between
                    // them (with the source scrolled to 504 px on Windows runners: the finger landed
                    // after the end of a short wrapped line, "listę.", where there is no word).
                    QPointF onWord = QPointF(pointIn(sourceView, 0.5, 0.4).toPoint());
                    for (qreal fy = 0.3; fy < 0.7; fy += 0.05) {
                        const QPointF p = QPointF(pointIn(sourceView, 0.35, fy).toPoint());
                        const QPointF l = sourceEdit->mapFromScene(p);
                        int pos = -1;
                        QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(pos), l.x(), l.y());
                        QRectF lineRect;
                        QMetaObject::invokeMethod(sourceEdit, "positionToRectangle", qReturnArg(lineRect), pos);
                        const QPointF centred =
                                QPointF(sourceEdit->mapToScene(QPointF(l.x(), lineRect.center().y())).toPoint());
                        const QPointF lc = sourceEdit->mapFromScene(centred);
                        QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(pos), lc.x(), lc.y());
                        const QString around = textAt(pos - 1, pos + 1);
                        if (around.size() == 2 && around.at(0).isLetter() && around.at(1).isLetter()) {
                            onWord = centred;
                            break;
                        }
                    }
                    // The finger stays down until the long press has selected (or the handler let
                    // go of it), not for a fixed time. What happened is logged: when the handler's
                    // long press moved the cursor (onLongPressed puts it under the finger, then
                    // selects the word), the handler's state, the event loop's longest pause.
                    QObject *const handler = sourceView->findChild<QObject *>(u"sourceTouchTap"_s);
                    const bool pressedBefore = handler && handler->property("pressed").toBool();
                    int underFinger = -1;
                    {
                        const QPointF l = sourceEdit->mapFromScene(onWord);
                        QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(underFinger), l.x(), l.y());
                    }
                    const int cursorBeforeHold = sourceEdit->property("cursorPosition").toInt();
                    QElapsedTimer held;
                    qint64 lastPoll = -1;
                    qint64 longestGap = 0;
                    qint64 selectedAfter = -1;
                    qint64 letGoAfter = -1;
                    qint64 cursorMovedAfter = -1;
                    qreal timeHeld = 0;
                    held.start();
                    uidriver::touchHoldNow(
                            window, onWord,
                            [&] {
                                const qint64 now = held.elapsed();
                                longestGap = qMax(longestGap, now - qMax<qint64>(lastPoll, 0));
                                lastPoll = now;
                                if (handler)
                                    timeHeld = qMax(timeHeld, handler->property("timeHeld").toReal());
                                if (cursorMovedAfter < 0
                                    && sourceEdit->property("cursorPosition").toInt() != cursorBeforeHold)
                                    cursorMovedAfter = now;
                                if (!sourceEdit->property("selectedText").toString().isEmpty()) {
                                    selectedAfter = now;
                                    return true;
                                }
                                if (handler && !handler->property("pressed").toBool()) {
                                    letGoAfter = now;
                                    return true;
                                }
                                return false;
                            },
                            10000);
                    const QString word = sourceEdit->property("selectedText").toString();
                    std::fprintf(
                            stderr,
                            "selftest: a long press in the source at (%.1f, %.1f) selected \"%s\" after %lld ms "
                            "(handler %s, pressed before %d, held %.2f s by its count, let go after %lld ms; cursor "
                            "%d -> %d after %lld ms, under the finger %d; the event loop's longest pause %lld ms), "
                            "contentY %.1f -> %.1f, view height %.1f -> %.1f\n",
                            onWord.x(), onWord.y(), qPrintable(word), qlonglong(selectedAfter),
                            handler ? "found" : "missing", int(pressedBefore), timeHeld, qlonglong(letGoAfter),
                            cursorBeforeHold, sourceEdit->property("cursorPosition").toInt(),
                            qlonglong(cursorMovedAfter), underFinger, qlonglong(longestGap), scrolled,
                            sourceView->property("contentY").toReal(), settledHeight, sourceView->height());
                    check(!word.isEmpty() && !word.contains(u' ') && !word.contains(u'\n')
                                  && qAbs(sourceView->property("contentY").toReal() - scrolled) < 1,
                          "a long press in the Markdown source selects the word under the finger");
                    QMetaObject::invokeMethod(sourceEdit, "deselect");
                }
            }
            app->requestUiAction(u"toggleSourceMode"_s);
            check(waitFor([&] { return !page->property("sourceMode").toBool(); }),
                  "the \"Markdown source\" command switches back");
            check(waitFor([&] { return !app->sourceModeShown(); }), "back to blocks: the block commands are on again");
            bool saved = false;
            QMetaObject::invokeMethod(page, "save", qReturnArg(saved));
            check(saved && app->workspace()->readFile(guide).contains(u"żeby edytować. Żółw ąę"_s), "the edit is saved to the file");

            // The note mode: the guide is a document; the header's toggle (a real click or tap; on
            // a small header — a phone — the page menu's item instead: the toggle is not among the
            // page actions there) makes it an outline, the "Toggle note mode" command a document
            // again — each switch rewrites only the `brulion:` line of the file.
            {
                const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
                QMetaObject::invokeMethod(page, "stopEditing");
                if (auto *list = editor->property("listView").value<QQuickItem *>())
                    QMetaObject::invokeMethod(list, "positionViewAtBeginning");
                settle(300);
                QQuickItem *toggle = nullptr;
                for (QQuickItem *t : window->findChildren<QQuickItem *>(u"noteModeToggle"_s)) {
                    if (t->isVisible() && editor->isAncestorOf(t))
                        toggle = t;
                }
                QQuickItem *pageHeader = findShown(window->contentItem(), [&](QQuickItem *item) {
                    return isA(item, "ContentPageHeader") && item->property("path").toString() == guide;
                });
                const bool smallHeader = pageHeader && pageHeader->property("small").toBool();
                const QString before = app->workspace()->readFile(guide);
                const auto withKey = [&before](const QString &mode) {
                    return QString(before).replace(u"\nbrulion: document\n"_s, u"\nbrulion: "_s + mode + u'\n');
                };
                check(page->property("mode").toString() == u"document" && pageHeader != nullptr
                              && (smallHeader ? toggle == nullptr : toggle != nullptr),
                      smallHeader ? "the guide is a document; a small header leaves the mode toggle to the page menu"
                                  : "the guide is a document with a mode toggle in its header");
                if (smallHeader) {
                    auto *menuButton = pageHeader->property("menuAnchor").value<QQuickItem *>();
                    if (menuButton)
                        tapOrClick(window, pointIn(menuButton), touch);
                    QQuickItem *modeItem = nullptr;
                    check(waitFor([&] {
                              modeItem = findShown(window->contentItem(), [](QQuickItem *item) {
                                  return item->objectName() == u"pageMenuNoteMode";
                              });
                              return modeItem != nullptr;
                          }),
                          "the page menu offers the note mode");
                    if (modeItem) {
                        tapOrClick(window, pointIn(modeItem), touch);
                        check(waitFor([&] { return page->property("mode").toString() == u"outline"; })
                                      && app->workspace()->readFile(guide) == withKey(u"outline"_s),
                              "the page menu's item switches to an outline, writing only the mode");
                    } else {
                        closeMenus(window);
                    }
                } else if (toggle) {
                    tapOrClick(window, toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)), touch);
                    check(waitFor([&] { return page->property("mode").toString() == u"outline"; })
                                  && app->workspace()->readFile(guide) == withKey(u"outline"_s),
                          "the header's toggle switches to an outline, writing only the mode");
                }
                app->requestUiAction(u"toggleNoteMode"_s);
                check(waitFor([&] { return page->property("mode").toString() == u"document"; })
                              && app->workspace()->readFile(guide) == before,
                      "the \"Toggle note mode\" command switches back");

                // The top bar's More menu: its Mode switch shows the note's mode and changes it —
                // Outline (only the mode written), Source (the Markdown source), Document again.
                const QString more = QCoreApplication::translate("ShellTopBar", "More");
                const auto openMore = [&] {
                    closeMenus(window);
                    QQuickItem *button = findShown(window->contentItem(), [&](QQuickItem *item) {
                        return isA(item, "BIconButton") && item->property("label").toString() == more;
                    });
                    if (button)
                        tapOrClick(window, pointIn(button), touch);
                    QQuickItem *mode = nullptr;
                    waitFor([&] {
                        mode = findShown(window->contentItem(),
                                         [](QQuickItem *item) { return item->objectName() == u"menuNoteMode"; });
                        return mode != nullptr;
                    });
                    settle(150); // the menu's opening transition
                    return mode;
                };
                const auto segment = [&](QQuickItem *mode, const char *text) {
                    const QString wanted = QCoreApplication::translate("ShellTopBar", text);
                    return mode ? findShown(mode,
                                            [&](QQuickItem *item) {
                                                return item->inherits("QQuickAbstractButton")
                                                       && item->property("text").toString() == wanted;
                                            })
                                : nullptr;
                };
                QQuickItem *mode = openMore();
                QQuickItem *segmented = mode ? findShown(mode, [](QQuickItem *item) { return isA(item, "BSegmented"); })
                                             : nullptr;
                check(segmented && segmented->property("currentValue").toString() == u"document",
                      "the More menu's Mode switch shows the document");
                if (QQuickItem *outline = segment(mode, "Outline")) {
                    tapOrClick(window, pointIn(outline), touch);
                    check(waitFor([&] { return page->property("mode").toString() == u"outline"; })
                                  && app->workspace()->readFile(guide) == withKey(u"outline"_s),
                          "its Outline makes the note an outline, writing only the mode");
                } else {
                    check(false, "the Mode switch has Outline");
                }
                mode = openMore();
                if (QQuickItem *sourceSegment = segment(mode, "Source")) {
                    tapOrClick(window, pointIn(sourceSegment), touch);
                    check(waitFor([&] { return page->property("sourceMode").toBool(); }),
                          "its Source shows the Markdown source");
                } else {
                    check(false, "the Mode switch has Source");
                }
                mode = openMore();
                if (QQuickItem *document = segment(mode, "Document")) {
                    tapOrClick(window, pointIn(document), touch);
                    check(waitFor([&] {
                              return !page->property("sourceMode").toBool()
                                     && page->property("mode").toString() == u"document";
                          }) && app->workspace()->readFile(guide) == before,
                          "its Document leaves the source and makes the note a document again");
                } else {
                    check(false, "the Mode switch has Document");
                }
                closeMenus(window);
            }

            // Page properties (docs/UI.md "Page properties"): the guide's tag chip in the header —
            // a right-click, a long press — "Remove from this page" writes only that property (its
            // last tag: the `tags:` line goes), undo brings it back; the header's "+" → "Add alias"
            // and an alias typed in the row's field: `aliases:` gets it; the palette's "Add page
            // property" opens the properties panel's add row.
            {
                const bool touch = QCoreApplication::arguments().contains(u"--touch"_s);
                const auto saveNow = [page] {
                    bool ok = false;
                    QMetaObject::invokeMethod(page, "save", qReturnArg(ok));
                    return ok;
                };
                const auto undoNow = [page] { QMetaObject::invokeMethod(page, "undo"); };
                // Waits for it: a menu counts as open before its items are laid out (on a slow
                // machine its rows show a little later).
                const auto shownItem = [&](const QString &name) {
                    QQuickItem *found = nullptr;
                    waitFor([&] {
                        found = uidriver::topmost(window, [&name](QQuickItem *i) { return i->objectName() == name; });
                        return found != nullptr;
                    });
                    return found;
                };
                if (auto *list = editor->property("listView").value<QQuickItem *>())
                    QMetaObject::invokeMethod(list, "positionViewAtBeginning");
                settle(300);
                const QString before = app->workspace()->readFile(guide);
                QQuickItem *chip = shownNamed(editor, u"headerTag_przewodnik"_s);
                check(chip != nullptr && before.contains(u"\ntags: [przewodnik]\n"_s),
                      "the guide's tag chip is in its header");
                if (chip) {
                    if (touch)
                        touchTap(window, pointIn(chip), 1000);
                    else
                        mouseClick(window, pointIn(chip), Qt::RightButton);
                    check(waitFor([&] { return openMenus(window) == 1; }),
                          touch ? "a long press on the tag chip opens its menu"
                                : "a right-click on the tag chip opens its menu");
                    if (QQuickItem *remove = shownItem(u"propertyChipRemove"_s)) {
                        tapOrClick(window, pointIn(remove), touch);
                        check(waitFor([&] {
                                  return saveNow()
                                         && app->workspace()->readFile(guide)
                                                    == QString(before).replace(u"tags: [przewodnik]\n"_s, QString());
                              }),
                              "“Usuń z tej strony” removes the tag: only that line changes");
                        check(waitFor([&] { return !shownNamed(editor, u"headerTag_przewodnik"_s); }),
                              "the chip is gone from the header");
                        undoNow();
                        check(waitFor([&] { return saveNow() && app->workspace()->readFile(guide) == before; }),
                              "undo brings the tag back");
                    } else {
                        check(false, "the chip's menu has “Usuń z tej strony”");
                    }
                }
                closeMenus(window);
                QQuickItem *plus = shownNamed(editor, u"pageHeaderAdd"_s);
                check(plus != nullptr, "the header's chips end with “+”");
                if (plus) {
                    tapOrClick(window, pointIn(plus), touch);
                    check(waitFor([&] { return openMenus(window) == 1; }), "“+” opens its menu");
                    if (QQuickItem *alias = shownItem(u"headerAddAlias"_s)) {
                        tapOrClick(window, pointIn(alias), touch);
                        check(waitFor([&] {
                                  const QQuickItem *f = window->activeFocusItem();
                                  return f && f->objectName() == u"pageHeaderField";
                              }),
                              "“Dodaj alias” opens a field in the header");
                        typeText(u"Żółw"_s);
                        sendKey(window, Qt::Key_Return);
                        check(waitFor([&] {
                                  return saveNow()
                                         && app->workspace()->readFile(guide)
                                                    == QString(before).replace(u"aliases: [Przewodnik, Start]"_s,
                                                                               u"aliases: [Przewodnik, Start, Żółw]"_s);
                              }),
                              "the alias typed there is written into `aliases:`");
                        sendKey(window, Qt::Key_Escape);
                        undoNow();
                        check(waitFor([&] { return saveNow() && app->workspace()->readFile(guide) == before; }),
                              "undo takes the alias away");
                    } else {
                        check(false, "the menu has “Dodaj alias”");
                    }
                }
                closeMenus(window);
                // An alias no link can use, then a tap elsewhere: nothing written, the field closes
                // and a toast says why (Enter says it under the field).
                plus = shownNamed(editor, u"pageHeaderAdd"_s);
                if (plus) {
                    tapOrClick(window, pointIn(plus), touch);
                    check(waitFor([&] { return openMenus(window) == 1; }), "“+” opens its menu again");
                    if (QQuickItem *alias = shownItem(u"headerAddAlias"_s)) {
                        tapOrClick(window, pointIn(alias), touch);
                        check(waitFor([&] {
                                  const QQuickItem *f = window->activeFocusItem();
                                  return f && f->objectName() == u"pageHeaderField";
                              }),
                              "“Dodaj alias” opens the field again");
                        QString toast;
                        const QMetaObject::Connection toasts =
                                QObject::connect(app, &AppController::toastRequested, app,
                                                 [&toast](const QString &text) { toast = text; });
                        typeText(u"Dom|ż"_s);
                        // The keyboard goes elsewhere (outside the editor: its focus scope would
                        // give it back to the field).
                        auto *elsewhere = new QQuickItem;
                        elsewhere->setParentItem(window->contentItem());
                        elsewhere->forceActiveFocus();
                        const QString reason =
                                QCoreApplication::translate("brulion::editor::PageController",
                                                            "An alias with this character cannot be used in a link");
                        check(waitFor([&] { return toast == reason; }),
                              "an alias no link can use, left by a tap elsewhere: a toast says why");
                        QObject::disconnect(toasts);
                        check(waitFor([&] { return !shownNamed(editor, u"pageHeaderField"_s); }), "the field closed");
                        check(saveNow() && app->workspace()->readFile(guide) == before, "nothing was written");
                        delete elsewhere;
                    } else {
                        check(false, "the menu has “Dodaj alias” again");
                    }
                }
                closeMenus(window);
                app->requestUiAction(u"addProperty"_s);
                check(waitFor([&] {
                          const QQuickItem *f = window->activeFocusItem();
                          return f && f->objectName() == u"propertiesKeyField";
                      }),
                      "the palette's “Dodaj właściwość strony” opens the properties panel's add row");
                sendKey(window, Qt::Key_Escape);
                check(app->workspace()->readFile(guide) == before, "nothing is written until a name is chosen");
                app->settings()->setPropertiesExpanded(false);
                settle(100);
            }

            // Phones: a block at the bottom of the screen that starts editing is scrolled above
            // the editing toolbar (which sits on the keyboard) instead of staying under it.
            if (window->width() < 900) {
                const qsizetype lowLine = lines.indexOf(u"- Ctrl+. zwija i rozwija dzieci bloku w konspekcie."_s);
                int low = -1;
                QMetaObject::invokeMethod(page, "rowOfLine", qReturnArg(low), int(lowLine));
                auto *list = editor->property("listView").value<QQuickItem *>();
                if (list && low >= 0) {
                    QMetaObject::invokeMethod(page, "stopEditing");
                    QMetaObject::invokeMethod(list, "positionViewAtIndex", low, 2 /* ListView.End */);
                    settle(100);
                    uidriver::callQml(editor, "startEditing", {QVariant(low), QVariant(-1)});
                    const auto caretBottom = [editor]() -> qreal {
                        auto *edit = editor->property("activeEditor").value<QQuickItem *>();
                        if (!edit)
                            return 1e9;
                        const QRectF r = edit->property("cursorRectangle").toRectF();
                        return edit->mapToItem(editor, r.bottomLeft()).y();
                    };
                    const auto visibleBottom = [editor] {
                        double bottom = 0;
                        QMetaObject::invokeMethod(editor, "visibleBottom", qReturnArg(bottom));
                        return bottom;
                    };
                    check(editorFocused() && waitFor([&] { return caretBottom() <= visibleBottom() + 0.5; }),
                          "a block edited at the bottom of a phone screen is not hidden under the toolbar");
                    QMetaObject::invokeMethod(page, "stopEditing");
                } else {
                    check(false, "the editor's list and the low block exist");
                }
            }

            // [[Pomysły na prezenty]] on the guide: the page does not exist yet.
            const QString missing = u"Pomysły na prezenty.md"_s;
            QVariantMap result;
            QMetaObject::invokeMethod(page, "activateLink", qReturnArg(result), row, u"page:Pomys%C5%82y%20na%20prezenty"_s, false);
            QObject *newPage = nullptr;
            check(waitFor([&] {
                      newPage = controllerFor(missing);
                      return app->currentRoute().value(u"path"_s).toString() == missing && newPage
                             && newPage->property("editingRow").toInt() >= 0;
                  }),
                  "a link to a missing page opens it with a block to type into");
            check(editorFocused(), "the new page's block has the keyboard");
            check(!app->workspace()->exists(missing), "opening a missing page does not create a file");
            if (newPage) {
                typeText(u"Rower dla Oli"_s);
                QMetaObject::invokeMethod(newPage, "save", qReturnArg(saved));
                check(app->workspace()->readFile(missing).contains(u"Rower dla Oli"_s), "typing creates the new page");

                // Renamed with unsaved typing (F2 right after typing): the editor follows the
                // file, the old name never comes back as a duplicate and nothing typed is lost.
                typeText(u" i Kuby"_s);
                const QString renamed = app->workspace()->renamePage(missing, u"Prezenty dla dzieci"_s);
                check(renamed == u"Prezenty dla dzieci.md"_s, "the open page is renamed");
                check(waitFor([&] {
                          return newPage->property("path").toString() == renamed
                                 && app->currentRoute().value(u"path"_s).toString() == renamed;
                      }),
                      "the editor and the route follow the rename");
                QMetaObject::invokeMethod(newPage, "save", qReturnArg(saved));
                settle(100);
                check(!app->workspace()->exists(missing), "the old name does not come back");
                check(app->workspace()->readFile(renamed).contains(u"Rower dla Oli i Kuby"_s),
                      "the text typed before the rename is saved under the new name");
            }

            // 5g. Links to blocks and headings: followed, missing, copied from the palette.
            checkBlockLinks(window, app, check, controllerFor);
            // 5k. The path parts of a link to a page in a folder.
            checkLinkParts(window, app, check, controllerFor);
            // 5l. Link previews; rounded tags in linked references.
            checkLinkPreviews(window, app, check, controllerFor);
            // 5b. A picture opens as a picture (checkPictureView).
            checkPictureView(window, app, check, controllerFor);
            // 5k. PDFs: the thumbnail, the viewer, drawing on a page (checkPdfViewer).
            checkPdfViewer(window, app, check, controllerFor);
            // 5i. A drawing: opened from its embed, drawn with real input, saved by itself.
            checkDrawing(window, app, check);
            // 5j. A drawing another program wrote: its embed's frame never moves.
            checkDrawingFrame(window, app, check);
            // 5e. The default reminder time set on a clock in the settings.
            checkSettingsClock(window, app, check);
            // 5f. The on-screen keyboard (phones).
            if (window->width() < 900)
                checkKeyboard(engine, window, app, check);
        } else {
            check(false, "the page view has an editor");
        }
        app->goHome();
    }

    // 5m. Tabs, the split view, a page in a window of its own (phones: the tab switcher).
    checkTabs(window, app, check);
    closeMenus(window);

    // 5c. The event log page and a sync run's details.
    checkLogsAndSyncDetails(window, app, check);
    closeMenus(window);
    app->goHome();
    // 5c'. The sync center's conflicts: Back on a phone returns to the list.
    checkSyncConflicts(window, app, check);
    closeMenus(window);

    // 5d. The notebook switcher: to the documentation and back with one tap / click each.
    checkNotebookSwitcher(window, app, check);
    closeMenus(window);

    // 5h. Settings → Appearance: accent, sidebar entries, bottom navigation, quick note button.
    checkAppearance(engine, window, app, check);

    // 5j. The tray calendar (desktop).
    if (!QCoreApplication::arguments().contains(u"--touch"_s)) {
        checkTrayCalendar(window, app, check);
        closeMenus(window);
    }

    // 5i. Settings → Application → "Start when you sign in": Windows only, and disabled in a
    // separate profile (the tests' BRULION_PROFILE: a Run entry would start the default one).
    {
        app->navigate({{u"view"_s, u"settings"_s}, {u"section"_s, u"app"_s}});
        check(waitFor([&] { return shownNamed(window->contentItem(), u"settingsDetailedLog"_s) != nullptr; }),
              "the application settings are shown");
        QQuickItem *startAtLogin = nullptr;
        for (QQuickItem *item : uidriver::allItems(window->contentItem())) {
            if (item->objectName() == u"settingsStartAtLogin")
                startAtLogin = item;
        }
#ifdef Q_OS_WIN
        const bool separateProfile = app->autostartAvailability() == u"profile";
        check(startAtLogin && startAtLogin->isVisible() && startAtLogin->isEnabled() == !separateProfile
                      && startAtLogin->property("checked").toBool() == app->startAtLogin(),
              "Windows offers to start at sign-in (disabled in a separate profile)");
#else
        check(startAtLogin && !startAtLogin->isVisible(), "starting at sign-in is offered on Windows only");
#endif
        // What a click on the tray icon does: desktops only, while there is a tray.
        QQuickItem *trayClick = nullptr;
        for (QQuickItem *item : uidriver::allItems(window->contentItem())) {
            if (item->objectName() == u"settingsTrayClick")
                trayClick = item;
        }
        check(trayClick && trayClick->isVisible() == app->isDesktop()
                      && (!app->isDesktop() || trayClick->isEnabled() == app->trayAvailable()),
              "the tray click setting is shown on desktops (enabled with a tray)");
        app->goHome();
    }

    // 6. Closing the notebook from the UI (as a click would: from inside an event).
    QTimer::singleShot(0, app, [app] { app->closeVault(); });
    check(waitFor([app] { return app->status() == u"onboarding" && !app->workspace()->isOpen(); }),
          "closing the notebook shows onboarding and closes the vault");

    // 6b. The tray calendar without a notebook: the month and a hint, no entries.
    if (!QCoreApplication::arguments().contains(u"--touch"_s)) {
        app->showTrayCalendar(QRect(), {});
        QQuickWindow *popup = nullptr;
        check(waitFor([&] {
                  popup = trayCalendarWindow();
                  return popup && popup->isVisible();
              }),
              "the tray calendar opens without a notebook");
        const QString hint = QCoreApplication::translate("ShellTrayCalendar",
                                                         "Open a notebook to see its tasks and reminders.");
        QQuickItem *month = popup ? shownNamed(popup->contentItem(), u"trayCalendarMonth"_s) : nullptr;
        check(month && month->property("placeholder").toString() == hint
                      && renderedTextContaining(popup->contentItem(), hint) != nullptr
                      && !shownNamed(popup->contentItem(), u"miniCalendarOpenJournal"_s),
              "without a notebook it shows the month and asks for a notebook");
        // While a notebook opens (started hidden at login, the tray clicked during indexing):
        // it says so and keeps quick capture, which waits for the notebook.
        app->openDemoVault();
        const QString opening = QCoreApplication::translate("ShellTrayCalendar", "Opening the notebook…");
        QQuickItem *captureButton = popup ? shownNamed(popup->contentItem(), u"trayCalendarCapture"_s) : nullptr;
        check(app->status() == u"loading" && month && month->property("placeholder").toString() == opening
                      && captureButton && captureButton->isVisible(),
              "while a notebook opens it says so and keeps “Quick capture…”");
        check(waitFor([&] { return app->status() == u"ready"; }, 10000) && month
                      && month->property("placeholder").toString().isEmpty(),
              "once it is open the hint goes");
        if (popup)
            popup->hide();
    }
    return failures;
}

} // namespace

void startShellSelfTest(QQmlApplicationEngine *engine, AppController *app)
{
    QTimer::singleShot(0, app, [engine, app] {
        const int failures = runChecks(engine, app);
        std::fprintf(stderr, "selftest: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::shell
