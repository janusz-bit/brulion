#include "shellselftest.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "demovault.h"
#include "workspace.h"
#include "workspacesync.h"

#include "log/categories.h"
#include "log/logger.h"
#include "log/redact.h"
#include "sync/synchistory.h"
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
#include <QKeyEvent>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <qtestsupport_gui.h>

#include <algorithm>
#include <cstdio>
#include <functional>

QT_BEGIN_NAMESPACE
// QtGui's entry point for synthetic mouse input (what QtTest's mouse functions call): the click
// goes through QGuiApplication and the delivery agent like a real one.
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);
QT_END_NAMESPACE

using namespace Qt::StringLiterals;

namespace brulion::shell {

namespace {

/// Let the event loop run for `ms` (gestures, transitions, queued calls). Not scaled by
/// BRULION_TEST_SLOWDOWN: these are real input timings (a longer pause turns a tap into a
/// long press), unlike waitFor()'s deadlines.
void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs = 3000)
{
    // Sanitizer test runs are ~25x slower: ctest passes BRULION_TEST_SLOWDOWN (tests/CMakeLists.txt).
    static const int slowdown = qMax(1, qEnvironmentVariableIntValue("BRULION_TEST_SLOWDOWN"));
    timeoutMs *= slowdown;
    QElapsedTimer timer;
    timer.start();
    while (!condition()) {
        if (timer.elapsed() > timeoutMs)
            return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return true;
}

/// Press and release `key` in the window. True when an item handled it (accepted the press or
/// the release) — an unhandled back key is what lets Android close the app.
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
    // Straight to the focused item (window Shortcut objects are not involved).
    QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
    press.ignore();
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
    release.ignore();
    QCoreApplication::sendEvent(window, &release);
    settle(50); // let bindings, transitions and queued calls settle
    return press.isAccepted() || release.isAccepted();
}

/// A tap with one finger at `scenePos`, as a touch screen sends it (press, `holdMs`, release).
void touchTap(QQuickWindow *window, QPointF scenePos, int holdMs = 110)
{
    // QTouchEventSequence's constructor is protected; QtTest's touchEvent() is a subclass too.
    struct Sequence : QTest::QTouchEventSequence {
        Sequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) {}
    };
    static QPointingDevice *device = QTest::createTouchDevice();
    Sequence sequence(window, device);
    sequence.press(0, scenePos.toPoint(), window).commit();
    settle(holdMs);
    sequence.release(0, scenePos.toPoint(), window).commit();
    settle(100);
}

/// A finger dragged from `from` by `delta` (scene coordinates) in small steps, then lifted.
void touchDrag(QQuickWindow *window, QPointF from, QPointF delta)
{
    struct Sequence : QTest::QTouchEventSequence {
        Sequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) {}
    };
    static QPointingDevice *device = QTest::createTouchDevice();
    Sequence sequence(window, device);
    sequence.press(0, from.toPoint(), window).commit();
    for (int step = 1; step <= 12; ++step) {
        settle(16);
        sequence.move(0, (from + delta * step / 12.0).toPoint(), window).commit();
    }
    sequence.release(0, (from + delta).toPoint(), window).commit();
    settle(300);
}

/// A tap on touch UIs (the phone run), a left click otherwise.
void tapOrClick(QQuickWindow *window, QPointF scenePos, bool touch);

/// A click (left by default) at `scenePos` of the window, as a real mouse sends it.
void mouseClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button = Qt::LeftButton)
{
    static int timestamp = 0;
    timestamp += 1000; // never a double click
    const QPointF global = window->mapToGlobal(scenePos);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier,
                        timestamp);
    qt_handleMouseEvent(window, scenePos, global, button, button, QEvent::MouseButtonPress, Qt::NoModifier,
                        timestamp += 20);
    qt_handleMouseEvent(window, scenePos, global, Qt::NoButton, button, QEvent::MouseButtonRelease, Qt::NoModifier,
                        timestamp += 80);
    settle(100);
}

void tapOrClick(QQuickWindow *window, QPointF scenePos, bool touch)
{
    if (touch)
        touchTap(window, scenePos);
    else
        mouseClick(window, scenePos);
}

/// The visible rendered text (a Text with linkAt()) under `root` whose text contains `needle`
/// and whose middle is on screen inside `root` (lists keep delegates beyond their edges too).
QQuickItem *renderedTextContaining(QQuickItem *root, const QString &needle)
{
    const QRectF area = root->mapRectToScene(QRectF(0, 0, root->width(), root->height()));
    QList<QQuickItem *> stack{ root };
    while (!stack.isEmpty()) {
        QQuickItem *item = stack.takeLast();
        if (!item->isVisible())
            continue;
        if (item->metaObject()->indexOfMethod("linkAt(double,double)") >= 0
            && item->property("text").toString().contains(needle)
            && area.contains(item->mapToScene(QPointF(item->width() / 2, item->height() / 2))))
            return item;
        stack.append(item->childItems());
    }
    return nullptr;
}

/// A point of `text` (scene coordinates) on its link whose URL contains `url`; (-1, -1) if none.
QPointF linkPoint(QQuickItem *text, const QString &url)
{
    for (qreal y = 2; y < text->height(); y += 3) {
        for (qreal x = 2; x < text->width(); x += 4) {
            QString link;
            QMetaObject::invokeMethod(text, "linkAt", qReturnArg(link), x, y);
            if (link.contains(url))
                return text->mapToScene(QPointF(x + 4, y + 4));
        }
    }
    return { -1, -1 };
}

/// True when the item's QML type (or one it is based on) is `prefix` ("TasksRow" matches the
/// class "TasksRow_QMLTYPE_12").
bool isA(const QObject *object, const char *prefix)
{
    for (const QMetaObject *m = object->metaObject(); m; m = m->superClass()) {
        if (QByteArray(m->className()).startsWith(prefix))
            return true;
    }
    return false;
}

/// The item and all its ancestors are visible.
bool shown(const QQuickItem *item)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (!p->isVisible() || p->opacity() <= 0.01)
            return false;
    }
    return item->width() >= 1 && item->height() >= 1;
}

QPointF pointIn(const QQuickItem *item, qreal fx = 0.5, qreal fy = 0.5)
{
    return item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
}

/// The first shown item under `root` (depth first, in child order) for which `pred` holds and
/// whose middle lies inside `root` (lists keep delegates beyond their edges, pools hide some).
QQuickItem *findShown(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred)
{
    if (!root)
        return nullptr;
    const QRectF area = root->mapRectToScene(QRectF(0, 0, root->width(), root->height()));
    std::function<QQuickItem *(QQuickItem *)> visit = [&](QQuickItem *item) -> QQuickItem * {
        if (!item->isVisible())
            return nullptr;
        if (pred(item) && shown(item) && area.contains(pointIn(item)))
            return item;
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (QQuickItem *found = visit(child))
                return found;
        }
        return nullptr;
    };
    return visit(root);
}

/// The menus (BMenu) open in the window: their popup items in the overlay (the visual tree also
/// reaches menus inside list delegates, which the QObject tree of the window does not).
int openMenus(QQuickWindow *window)
{
    int open = 0;
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickPopupItem") && item->parent() && item->parent()->inherits("QQuickMenu"))
            ++open;
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            visit(child);
    };
    visit(window->contentItem());
    return open;
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

/// Close the menus open in the window (a failed check must not leave a modal menu behind).
void closeMenus(QQuickWindow *window)
{
    std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickPopupItem") && item->parent() && item->parent()->inherits("QQuickMenu"))
            QMetaObject::invokeMethod(item->parent(), "close");
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            visit(child);
    };
    visit(window->contentItem());
    waitFor([window] { return openMenus(window) == 0; });
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

/// 5c. The event log ("Rejestr zdarzeń") and the details of a sync run, with real taps / clicks:
///     lines logged anywhere reach the page (a secret never does), the search ignores diacritics,
///     an area chip filters, the detailed mode switches the Logger; the sync center's history
///     opens a run's details, whose "Show in the event log" opens the page at the run's time.
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
    check(app->workspace()->createFolder(empty), "an empty folder is created");
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
        check(closeMenu(), "Escape closes the block menu");
        activate(bullet());
        check(waitFor([&] { return controller->property("zoomBlock").toInt() >= 0; }) && openMenus(window) == 0,
              "a tap or click on a bullet zooms into the block, without a menu");
        QMetaObject::invokeMethod(controller, "zoomOut");
    }
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
            std::fprintf(stderr,
                         "selftest: picture bar: name %.0f of %.0f px, size toggle %.0f–%.0f, text button %.0f–%.0f\n",
                         name->width(), name->property("implicitWidth").toReal(), left(size), right(size),
                         left(textButton), right(textButton));
            check(name->width() >= qMin<qreal>(name->property("implicitWidth").toReal(), 80)
                          && right(name) <= left(size) + 0.5 && right(size) <= left(textButton) + 0.5
                          && right(textButton) <= window->width() + 0.5
                          && nameOf(size) == QCoreApplication::translate("ContentPictureView", "Zoom to actual size"),
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

    // A file name that looks like markup is shown as written (one line, not a line break).
    {
        const QString odd = u"assets/a<br>b <i>szkic.svg"_s;
        check(workspace->writeFile(odd, bytes), "a picture with markup in its name is added to the notebook");
        app->openPage(odd);
        check(waitFor([&] { return opened(odd) && pictureShown(); }), "the picture with markup in its name opens");
        QQuickItem *name = child(pictureView(), "pictureName");
        check(name && name->property("lineCount").toInt() == 1
                      && name->property("text").toString() == u"a<br>b <i>szkic.svg"_s
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
    workspace->setConfig({{key, u"10:05"_s}});
    check(waitFor([&] { return field->property("text").toString() == u"10:05"; }),
          "after a clock pick the field still follows the notebook's config (a synced change shows)");
    workspace->setConfig({{key, original}});
    check(waitFor([&] { return field->property("text").toString() == original; }) && configValue() == original,
          "the default reminder time is back");
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
    closeMenus(window); // whatever a failed check left open
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
                    const QPointF at = pointIn(sourceView, 0.5, 0.5);
                    touchTap(window, at);
                    const QPointF local = sourceEdit->mapFromScene(at);
                    int expected = -1;
                    QMetaObject::invokeMethod(sourceEdit, "positionAt", qReturnArg(expected), local.x(), local.y());
                    const int cursor = sourceEdit->property("cursorPosition").toInt();
                    std::fprintf(stderr,
                                 "selftest: the tap in the source: cursor %d (before %d, under the finger %d)\n",
                                 cursor, cursorBefore, expected);
                    check(cursor != cursorBefore && qAbs(cursor - expected) <= 1
                                  && sourceEdit->property("selectedText").toString().isEmpty(),
                          "a tap in the Markdown source puts the cursor there");
                    // A long press selects the word under the finger (where the platform has
                    // selection handles, they take it on from there).
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
                    check(editorFocused() && waitFor([&] { return caretBottom() <= editor->property("toolbarTop").toReal() + 0.5; }),
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

            // 5b. A picture opens as a picture (checkPictureView).
            checkPictureView(window, app, check, controllerFor);
            // 5e. The default reminder time set on a clock in the settings.
            checkSettingsClock(window, app, check);
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
