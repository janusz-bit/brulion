#include "shellselftest.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "commandmodel.h"
#include "demovault.h"
#include "uidriver.h"
#include "workspace.h"
#include "workspacesync.h"

#include "log/categories.h"
#include "log/logger.h"
#include "log/redact.h"
#include "sync/synchistory.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputDevice>
#include <QInputMethodEvent>
#include <QJSValue>
#include <QKeyEvent>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

// Input and item finding: uidriver.h (shared with the user journey and the remote control).
using uidriver::closeMenus;
using uidriver::enclosingFlickable;
using uidriver::findShown;
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
///      length was read after Qt had reset it). ☰ still opens the drawer, a swipe on it closes it
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
///     "/zrobione" typed into a journal day completes its block. The agenda shows a task written
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
    // The keyboard: the Menu key opens a row's menu, the arrows reach "Otwórz" (above the
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
        const auto focusName = [window] {
            const QQuickItem *focus = window->activeFocusItem();
            return focus ? focus->objectName() : QString();
        };
        const QQuickItem *rowBox = named(row, u"tasksRowTaskBox"_s);
        const QString status = rowBox ? rowBox->property("status").toString() : QString();
        check(opened && focusName() == u"taskStatus_"_s + status,
              "the Menu key opens a task's menu in Tasks with its status highlighted");
        for (int i = 0; i < 8 && opened && focusName() != u"taskStatusOpen"; ++i)
            sendKey(window, Qt::Key_Up);
        const bool reached = focusName() == u"taskStatusOpen";
        sendKey(window, Qt::Key_Return);
        check(opened && reached && waitFor([&] { return app->currentView() != u"tasks" && openMenus(window) == 0; }),
              "in a task's menu in Tasks Up reaches “Otwórz” and Enter opens the note");
        closeMenus(window);
    } else {
        check(false, "the tasks view lists tasks for the keyboard");
    }
    restore();

    // ---- Tasks: a due date's chip has the date menu (PlanningTokenMenu); "Dzień później" writes it.
    app->navigate({{u"view"_s, u"tasks"_s}});
    settle(800);
    QQuickItem *dueChip = shownWhere(window->contentItem(), [](QQuickItem *item) {
        return isA(item, "BChip") && item->property("tokenKind").toString() == u"due";
    });
    check(dueChip != nullptr, "a task in Tasks shows its due date as a chip");
    if (dueChip) {
        QQuickItem *row = dueChip;
        while (row && !isA(row, "TasksRow"))
            row = row->parentItem();
        const QObject *task = row ? row->property("task").value<QObject *>() : nullptr;
        const QString path = task ? task->property("path").toString() : QString();
        const QString before = path.isEmpty() ? QString() : ws->readFile(path);
        if (touch)
            touchTap(window, pointIn(dueChip), 1000);
        else
            mouseClick(window, pointIn(dueChip), Qt::RightButton);
        QQuickItem *later = nullptr;
        check(waitFor([&] { return openMenus(window) == 1 && (later = named(window->contentItem(), u"dateChipDayLater"_s)); }),
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

    // ---- A day of the week view: its day panel, beside the week on wide windows and a bottom
    //      sheet (a modal dialog) on phones, where the menu opens over the sheet and the sheet
    //      stays open after the choice. The day in two days has dated tasks.
    app->navigate({{u"view"_s, u"calendar"_s}, {u"mode"_s, u"week"_s}});
    settle(600);
    QQuickItem *calendarView =
            shownWhere(window->contentItem(), [](QQuickItem *item) { return isA(item, "CalendarView"); });
    QObject *daySheet = calendarView ? calendarView->findChild<QObject *>(u"calendarDaySheet"_s) : nullptr;
    if (calendarView)
        QMetaObject::invokeMethod(calendarView, "showDay", Q_ARG(QVariant, QVariant(app->today(2))));
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
        QMetaObject::invokeMethod(dayEditor, "startEditing", QVariant(row), QVariant(-1));
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

/// 5c. The event log ("Rejestr zdarzeń") and the details of a sync run, with real taps / clicks:
///     lines logged anywhere reach the page (a secret never does), the search ignores diacritics,
///     an area chip filters, the detailed mode switches the Logger; the sync center's history
///     opens a run's details, whose "Show in the event log" opens the page at the run's time.
/// 3d. Tags name pages (docs/SYNTAX.md §3), with real input: a tap / click on a #tag in a journal
///     day opens the page the tag names — one not written yet opens empty, with the tagged blocks
///     among its references and the tag strip in its header, ready to type on a desktop, waiting
///     for a tap on a touch screen — and back returns; in Tasks a click
///     on a tag filters the list while a Ctrl+click (desktop) or a long press (--touch) opens the
///     tag's page; a row of the tag list opens the tag's page, its trailing button the overview,
///     whose "Utwórz stronę" opens the page not written yet; the sidebar's rename dialog counts
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
              "the page header has the tag strip (tag overview, tasks with the tag)");
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
            // A long press: the tag's menu (LinkMenu), "Filtruj po tagu" first; its "Otwórz stronę tagu".
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

    // ---- A page renamed (in its header) to a tag in use gets the tag's strip with the rename;
    //      renamed again — the tags naming it rewritten in other notes — it keeps it.
    {
        const QString draft = u"Szkic pomysłów.md"_s;
        app->workspace()->writeFile(draft, u"- szkic\n"_s);
        app->openPage(draft);
        const auto strip = [window] {
            return findShown(window->contentItem(),
                             [](QQuickItem *item) { return item->objectName() == u"pageTagStrip"; });
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
        check(header && !strip(), "a page no tag names has no tag strip");
        renameInHeader(header, u"pomysł"_s);
        check(strip() != nullptr && app->workspace()->exists(tagPage),
              "renamed to a tag in use, the page has the tag's strip at once");
        renameInHeader(headerOf(tagPage), u"Pomysły kuchenne"_s);
        const QString renamed = u"Pomysły kuchenne.md"_s;
        // (No waiting: the rename is done when the call returns. The strip used to come only with
        // some later change — its binding had lost its dependency on Workspace.revision.)
        check(app->workspace()->exists(renamed) && strip() != nullptr && !app->workspace()->exists(tagPage),
              "renamed again, the tags naming it rewritten in other notes after it moved: the strip stays");
        app->workspace()->deletePage(app->workspace()->exists(renamed) ? renamed : draft);
        app->goHome();
        settle(200);
    }

    // ---- Renaming from the sidebar says how many links and tags change.
    QObject *rename = window->findChild<QObject *>(u"shellRename"_s);
    check(rename != nullptr, "the shell has its rename dialog");
    if (rename) {
        QMetaObject::invokeMethod(rename, "openFor", Q_ARG(QVariant, QVariant(u"Witaj w Brulionie.md"_s)),
                                  Q_ARG(QVariant, QVariant(false)));
        const int changes = rename->property("changes").toInt();
        check(waitFor([&] { return rename->property("opened").toBool(); }) && changes > 0
                      && rename->property("subtitle").toString().contains(QString::number(changes)),
              "the sidebar's rename dialog counts the links and tags naming the page");
        QMetaObject::invokeMethod(rename, "close");
        waitFor([&] { return !rename->property("opened").toBool(); });
        // A "/" moves the page, here renamed too: the toast names the folder and the new name.
        const QString probe = u"Próba przenosin.md"_s;
        app->workspace()->writeFile(probe, u"- żółw\n"_s);
        QString toast;
        const QMetaObject::Connection toasts = QObject::connect(app, &AppController::toastRequested, app,
                                                                [&toast](const QString &text) { toast = text; });
        QMetaObject::invokeMethod(rename, "openFor", Q_ARG(QVariant, QVariant(probe)),
                                  Q_ARG(QVariant, QVariant(false)));
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
            // A fresh shell after each switch: its drawer, found through its bottom bar.
            QQuickItem *menuTab = shownItem(named(u"bottomNav_menu"_s));
            if (!menuTab)
                return false;
            activate(menuTab);
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
                             && app->currentRoute().value(u"path"_s).toString() == docsWelcomePage();
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
/// strip while blocks are selected or the keyboard is up; a swipe still closes the drawer). Runs
/// on a page (Remont mieszkania) with the drawer closed and stays there, without navigating: a
/// view the shell leaves waits for its deferred deletion, which never comes while the self-test
/// runs, and later findChild() calls would find its items.
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

    // The on-screen keyboard up (simulated): no strip either, and the drawer opened by its button
    // closes with a swipe.
    if (run(u"KeyboardInsets.simulated = 300"_s)) {
        check(waitFor([&] { return margin() <= 0; }), "while the keyboard is up there is no edge strip");
        QMetaObject::invokeMethod(drawer, "open");
        check(waitFor(opened, 3000), "the drawer opens while the keyboard is up");
        check(swipeClose(), "with the keyboard up a swipe to the left closes the drawer");
        run(u"KeyboardInsets.simulated = -1"_s);
        check(waitFor([&] { return qAbs(margin() - plain) <= 1; }), "the keyboard gone, the edge strip is back");
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
    // The top bar's menu button lies in the strip: a tap there is the button's (it opens the
    // drawer — by a tap, not a swipe).
    QQuickItem *menuButton =
            findShown(window->contentItem(), [](QQuickItem *item) { return item->objectName() == u"topBarMenu"; });
    const QPointF inStrip = menuButton ? QPointF(margin() / 2, pointIn(menuButton).y()) : QPointF();
    if (menuButton && menuButton->contains(menuButton->mapFromScene(inStrip))) {
        touchTap(window, inStrip);
        check(waitFor(opened, 3000), "a tap in the edge strip reaches the button under it");
        sendKey(window, Qt::Key_Escape);
        waitFor(closed);
    } else {
        check(false, "the top bar's menu button lies in the edge strip");
    }

    check(app->currentView() == u"page" && closed(), "the page is still shown, the drawer closed");
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

    // ---- The page tree: the drawer on phones (opened from the bottom bar and the top bar),
    //      the sidebar on desktops.
    if (narrow) {
        QQuickItem *menuTab = findNamed(u"bottomNav_menu"_s);
        check(menuTab != nullptr, "the bottom bar has a Menu tab");
        if (!menuTab)
            return;
        activate(menuTab);
        check(waitFor(drawerOpen), "the bottom bar's Menu opens the drawer");
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
    activate(treeRow(page));
    check(waitFor([&] { return view() == u"page" && routePath() == page; }) && openMenus(window) == 0,
          touch ? "a tap on a page in the drawer opens it, without a menu"
                : "a click on a page in the sidebar opens it, without a menu");
    if (narrow) {
        check(waitFor(drawerClosed), "opening a page from the drawer closes it");
        // Through the top bar's menu button: a long press opens the row's context menu.
        QQuickItem *menuButton = findNamed(u"topBarMenu"_s);
        check(menuButton != nullptr, "the top bar has a menu button");
        if (!menuButton)
            return;
        activate(menuButton);
        check(waitFor(drawerOpen), "the top bar's menu button opens the drawer");
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
}

/// 3e. Folders, with real input: the page opened is revealed and highlighted in the tree; a row's
///     menu (the "⋯" button on touch screens, a right-click on desktops) has "Przenieś do…", whose
///     dialog moves the page into the folder chosen; the "Strony" header's "+" menu creates a root
///     folder (named in the dialog, typed with real keys) and a deleted empty folder leaves the
///     tree; on desktops a page dragged with the mouse onto a folder moves into it, onto the
///     "Strony" header back to the root, a closed folder hovered over opens and Escape cancels
///     the drag, the target follows the rows the list scrolls under a still pointer and the
///     header scrolled out of the list is none; a collapsed "Strony" stays collapsed when another
///     page opens, "Pokaż w drzewie" opens it; a "/" typed into the page header's title stays in
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

    // ---- The page opened elsewhere (a link, search, the switcher) shows in the tree.
    app->goHome();
    settle(200);
    app->openPage(remont);
    openDrawer();
    QQuickItem *row = nullptr;
    check(waitFor([&] { return (row = treeRow(remont)) && row->property("selected").toBool(); }),
          "the page opened is revealed and highlighted in the tree");

    // ---- A row's menu: the "⋯" button (touch) or a right-click; "Przenieś do…".
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

    // ---- A root folder from the "Strony" header's "+" menu.
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
        if (touch) {
            // A phone's keyboard still composes the word when "Utwórz" is tapped: preedit text,
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
        check(waitFor([&] { return treeRow(u"Zażółć"_s) != nullptr; }), "the new folder shows in the tree");
        // An empty folder deleted leaves the tree at once (folders have no file changes).
        check(!ws->deletePage(u"Zażółć"_s).isEmpty() && waitFor([&] { return treeRow(u"Zażółć"_s) == nullptr; }),
              "a deleted empty folder leaves the tree");
    }
    restore();

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
    //      the "Strony" header scrolled out above the list is no target (desktop drags, driven
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
            QMetaObject::invokeMethod(sidebar, "beginDrag", QVariant(remont), QVariant(false),
                                      QVariant(u"Remont mieszkania"_s));
            const QPointF still = list->mapToScene(QPointF(list->width() / 2, list->height() - 12));
            QMetaObject::invokeMethod(sidebar, "dragTo", QVariant(still));
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
            QMetaObject::invokeMethod(sidebar, "dragTo", QVariant(pointIn(pagesHeader, 0.5)));
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
            QMetaObject::invokeMethod(sidebar, "dragTo", QVariant(above));
            check(rootShown && overHeader && !sidebar->property("dropOnRoot").toBool()
                          && !sidebar->property("dropValid").toBool(),
                  "the “Strony” header is a drop target while it shows, not once scrolled out of the list");
            QMetaObject::invokeMethod(sidebar, "cancelDrag");
        }
    }
    restore();

    // ---- A collapsed "Strony" stays collapsed (the setting too) when another page opens;
    //      "Pokaż w drzewie" opens it and shows the page.
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

/// 5g. Links to blocks and headings (docs/SYNTAX.md §2 "Copied links"), with real input: a link
///     to a block of this page whose block was moved to another page opens that page with the
///     block selected; a heading the page lacks is said (followed from the text and as a route),
///     its name as plain text in the toast even when written with tags;
///     the palette's "Kopiuj odwołanie do bloku" copies the edited block's ((id)) — read back from
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
    // The palette's "Kopiuj odwołanie do bloku" on the edited block.
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

/// 5b. A picture attachment opens as a picture (ContentPictureView): its bytes are not a note and
/// never reach the editor — the older view read them as text there, mojibake for a binary picture,
/// and a typed character was saved over it. "Pokaż jako tekst" (a real tap / click) and Ctrl+E
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
    // readers and in its tooltip) the file name keeps its room — the toggle's "Pokaż naturalny
    // rozmiar" left it none on a phone — and nothing runs off the window's edge. The toggle works.
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

    // "Pokaż jako tekst" in the picture's bar: the file's text, read-only; the editor stays
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
    // it can is loaded (these bytes are no picture: "Nie można wczytać").
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

        // Embedded in a note it says so too, with its name (the embed only said "Nie można
        // wczytać łąka.webp").
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

/// 5e. The default reminder time in the notebook's settings, set on the clock: saved at once, and
/// the field keeps following the notebook's config afterwards — a value synced from another
/// device shows (assigning the picked text once ended that binding: the field kept the old value
/// and wrote it back with the next edit).
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
    QMetaObject::invokeMethod(picker, "commitTime", Q_ARG(QVariant, 7), Q_ARG(QVariant, 35));
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

/// 5f. The on-screen keyboard (phones; a simulated one, KeyboardInsets.simulated): the bottom
///     navigation makes way while it is up, the view ends at its top, toasts and a bottom sheet
///     (quick capture) sit above it; the editing toolbar docked after the system hid the keyboard
///     keeps the navigation away until the editing stops, above the system's navigation bar, and
///     toasts then show at the top, away from the line being written; a
///     settings field that gets the focus is scrolled above the keyboard; on a wide touch window
///     a page in the side panel docks its toolbar on the keyboard too.
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
    // A word the keyboard is still composing (preedit, outside the field's text) counts: "Dodaj"
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
    // (Its toast with "Otwórz" stays 7 s over the bottom rows of the views checked next.)
    if (QQuickItem *toasts = findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "BToastHost"); }))
        QMetaObject::invokeMethod(toasts, "clear");

    insets->setProperty("simulated", 0);
    check(waitFor([&] {
              return nav->isVisible() && near(nav->mapToScene(QPointF(0, nav->height())).y(), window->height());
          }),
          "the keyboard goes: the bottom navigation is back at the window's bottom");

    // Editing with the keyboard hidden by the system: the toolbar stays docked, no navigation.
    const QString guide = u"Witaj w Brulionie.md"_s;
    app->openPage(guide);
    QQuickItem *editor = nullptr;
    QObject *page = nullptr;
    check(waitFor([&] {
              for (QQuickItem *e : window->findChildren<QQuickItem *>(u"pageEditor"_s)) {
                  auto *c = e->property("controller").value<QObject *>();
                  if (c && c->property("path").toString() == guide && c->property("loaded").toBool()
                      && e->isVisible()) {
                      editor = e;
                      page = c;
                  }
              }
              return editor != nullptr;
          }),
          "the guide opens");
    if (editor && page) {
        int row = -1;
        QMetaObject::invokeMethod(page, "lastVisibleRow", qReturnArg(row));
        QMetaObject::invokeMethod(editor, "startEditing", QVariant(row), QVariant(-1));
        check(waitFor([&] { return editor->property("toolbarVisible").toBool() && !nav->isVisible(); }),
              "editing with the keyboard hidden: the toolbar is docked and the bottom navigation stays away");
        // (No keyboard and no navigation bar here: the view, and the toolbar, end at the window's
        // bottom.)
        QQuickItem *slot = toolbarSlotOf(window, editor);
        // (Laid out with the shell's next polish.)
        check(waitFor([&] { return slot && near(slot->mapToScene(QPointF(0, slot->height())).y(), window->height()); }),
              "the docked toolbar sits on the bottom edge of the view");
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
        // added to the shell's): the docked toolbar sits right above it.
        QQuickItem *shellItem =
                findShown(window->contentItem(), [](QQuickItem *item) { return isA(item, "AppShell"); });
        const auto setSafeBottom = [&](int px) {
            QQmlExpression set(qmlContext(shellItem), shellItem, u"SafeArea.additionalMargins.bottom = %1"_s.arg(px));
            set.evaluate();
            if (set.hasError())
                std::fprintf(stderr, "selftest: %s\n", qPrintable(set.error().toString()));
            return !set.hasError();
        };
        if (shellItem && slot && setSafeBottom(48)) {
            check(waitFor([&] {
                      return near(slot->mapToScene(QPointF(0, slot->height())).y(), window->height() - 48)
                             && near(room->height(), 48) && room->isVisible() && !nav->isVisible();
                  }),
                  "with the keyboard hidden the docked toolbar sits right above the system's navigation bar");
            setSafeBottom(0);
            waitFor([&] { return near(slot->mapToScene(QPointF(0, slot->height())).y(), window->height()); });
        } else {
            check(false, "the shell's SafeArea margins can be set");
        }
        QMetaObject::invokeMethod(page, "stopEditing");
        check(waitFor([&] { return nav->isVisible(); }), "editing stops: the bottom navigation is back");
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
        QMetaObject::invokeMethod(side.first, "startEditing", QVariant(row), QVariant(-1));
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
    // 3e. Folders: reveal, the row menu's "Przenieś do…", a root folder, drag and drop (desktop),
    //     All pages as a folder browser.
    checkFolders(window, app, check);
    closeMenus(window);
    if (drawer->property("visible").toBool()) {
        QMetaObject::invokeMethod(drawer, "close");
        waitFor([&] { return !isOpen(drawer); });
    }
    app->goHome();

    // 4. The command palette: Escape closes it; the focus returns to the shell.
    app->navigate({ { u"view"_s, u"tasks"_s } });
    app->requestUiAction(u"palette"_s);
    check(waitFor([&] { return palette->property("opened").toBool(); }), "the command palette opens");
    sendKey(window, Qt::Key_Escape);
    check(waitFor([&] { return !isOpen(palette); }) && view() == u"tasks", "Escape closes the palette");
    handled = sendKey(window, Qt::Key_Back);
    check(handled && view() == u"journals", "after the palette closed, back reaches the shell again");

    // 5. The block editor in the shell: real keys reach the edited block and are saved, Ctrl+E
    //    and the "Markdown source" command switch source mode, a link to a missing page opens
    //    that page ready to type (and typing creates it).
    {
        const QString guide = u"Witaj w Brulionie.md"_s;
        const auto controllerFor = [window](const QString &path) -> QObject * {
            const QList<QObject *> found = window->findChildren<QObject *>(u"pageController"_s);
            for (QObject *c : found) {
                if (c->property("path").toString() == path)
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
            // The guide's first list block ("Konspekt"), scrolled into view (on a phone it is
            // below the first screen).
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
            QMetaObject::invokeMethod(editor, "startEditing", QVariant(row), QVariant(-1));
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
                    // after the tap (the toolbar it brought up keeps the cursor above it).
                    qreal settledY = -1;
                    QElapsedTimer stillFor;
                    waitFor([&] {
                        const qreal y = sourceView->property("contentY").toReal();
                        if (y != settledY || sourceView->property("moving").toBool()) {
                            settledY = y;
                            stillFor.start();
                            return false;
                        }
                        return stillFor.elapsed() >= 150;
                    });
                    const qreal scrolled = sourceView->property("contentY").toReal();
                    const auto textAt = [&](int from, int to) {
                        QString text;
                        QMetaObject::invokeMethod(sourceEdit, "getText", qReturnArg(text), from, to);
                        return text;
                    };
                    QPointF onWord = pointIn(sourceView, 0.5, 0.4);
                    for (qreal fy = 0.3; fy < 0.7; fy += 0.05) {
                        const QPointF p = pointIn(sourceView, 0.35, fy);
                        const QPointF l = sourceEdit->mapFromScene(p);
                        int pos = -1;
                        QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(pos), l.x(), l.y());
                        const QString around = textAt(pos - 1, pos + 1);
                        if (around.size() == 2 && around.at(0).isLetter() && around.at(1).isLetter()) {
                            onWord = p;
                            break;
                        }
                    }
                    touchTap(window, onWord, 1000);
                    const QString word = sourceEdit->property("selectedText").toString();
                    std::fprintf(stderr, "selftest: a long press in the source selected \"%s\"\n", qPrintable(word));
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

            // The note mode: the guide is a document; the header's toggle (a real click or tap)
            // makes it an outline, the "Toggle note mode" command a document again — each switch
            // rewrites only the `brulion:` line of the file.
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
                const QString before = app->workspace()->readFile(guide);
                const auto withKey = [&before](const QString &mode) {
                    return QString(before).replace(u"\nbrulion: document\n"_s, u"\nbrulion: "_s + mode + u'\n');
                };
                check(page->property("mode").toString() == u"document" && toggle != nullptr,
                      "the guide is a document with a mode toggle in its header");
                if (toggle) {
                    tapOrClick(window, toggle->mapToScene(QPointF(toggle->width() / 2, toggle->height() / 2)), touch);
                    check(waitFor([&] { return page->property("mode").toString() == u"outline"; })
                                  && app->workspace()->readFile(guide) == withKey(u"outline"_s),
                          "the header's toggle switches to an outline, writing only the mode");
                }
                app->requestUiAction(u"toggleNoteMode"_s);
                check(waitFor([&] { return page->property("mode").toString() == u"document"; })
                              && app->workspace()->readFile(guide) == before,
                      "the \"Toggle note mode\" command switches back");
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
                    QMetaObject::invokeMethod(editor, "startEditing", QVariant(low), QVariant(-1));
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
            // 5b. A picture opens as a picture (checkPictureView).
            checkPictureView(window, app, check, controllerFor);
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

    // 5c. The event log page and a sync run's details.
    checkLogsAndSyncDetails(window, app, check);
    closeMenus(window);
    app->goHome();

    // 5d. The notebook switcher: to the documentation and back with one tap / click each.
    checkNotebookSwitcher(window, app, check);

    // 6. Closing the notebook from the UI (as a click would: from inside an event).
    QTimer::singleShot(0, app, [app] { app->closeVault(); });
    check(waitFor([app] { return app->status() == u"onboarding" && !app->workspace()->isOpen(); }),
          "closing the notebook shows onboarding and closes the vault");
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
