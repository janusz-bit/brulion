#include "shelljourney.h"

#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <qtestsupport_gui.h>

#include <algorithm>
#include <cstdio>
#include <functional>

QT_BEGIN_NAMESPACE
// QtGui's entry point for synthetic mouse input (what QtTest's mouse functions call): the events
// go through QGuiApplication like real ones, so pointer handlers (TapHandler) see whole clicks.
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

/// The text a QML file shows for `source` (qsTr() in <context>.qml), in the current language.
QString ui(const char *context, const char *source)
{
    return QCoreApplication::translate(context, source);
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

bool hasAncestor(const QQuickItem *item, const char *prefix)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (isA(p, prefix))
            return true;
    }
    return false;
}

void collect(QQuickItem *item, QList<QQuickItem *> &out)
{
    out.append(item);
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collect(child, out);
}

QString labelOf(const QQuickItem *item)
{
    for (const char *name : { "text", "label", "title" }) {
        const QVariant v = item->property(name);
        if (v.metaType() == QMetaType::fromType<QString>() && !v.toString().isEmpty())
            return v.toString();
    }
    return {};
}

class Journey {
public:
    Journey(QQmlApplicationEngine *engine, AppController *app, const QString &outDir)
        : m_engine(engine), m_app(app), m_outDir(outDir)
    {
    }

    int run();

private:
    // ---- Reporting -------------------------------------------------------------------------------
    void log(const QString &text) const { std::fprintf(stderr, "journey: %s\n", qPrintable(text)); }
    void fail(const QString &what)
    {
        ++m_failures;
        log(u"FAIL "_s + what);
    }
    void fallback(const QString &what) { log(u"FALLBACK "_s + what); }
    void shot(const QString &name, int settleMs = 700)
    {
        settle(settleMs);
        const QImage image = m_window->grabWindow();
        const QString file = QDir(m_outDir).filePath(QString::asprintf("%02d-", ++m_shot) + name + u".png"_s);
        if (image.isNull() || !image.save(file))
            fail(u"screenshot "_s + name);
        else
            log(u"shot "_s + file);
    }

    // ---- Finding items -------------------------------------------------------------------------
    QList<QQuickItem *> items() const
    {
        QList<QQuickItem *> out;
        collect(m_window->contentItem(), out);
        return out;
    }
    /// Visible (effectively, opacity included) and not empty.
    static bool visibleTree(const QQuickItem *item)
    {
        if (item->width() < 1 || item->height() < 1)
            return false;
        for (const QQuickItem *p = item; p; p = p->parentItem()) {
            if (!p->isVisible() || p->opacity() <= 0.01)
                return false;
        }
        return true;
    }
    /// The item's centre is inside the window and every clipping ancestor.
    bool onScreen(const QQuickItem *item) const
    {
        const QPointF c = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        if (!QRectF(0, 0, m_window->width(), m_window->height()).contains(c))
            return false;
        for (const QQuickItem *p = item->parentItem(); p; p = p->parentItem()) {
            if (p->clip() && !p->mapRectToScene(QRectF(0, 0, p->width(), p->height())).contains(c))
                return false;
        }
        return true;
    }
    /// The topmost visible item matching `pred` (popups first, then the last one painted).
    QQuickItem *find(const std::function<bool(QQuickItem *)> &pred) const
    {
        QQuickItem *best = nullptr;
        int bestScore = -1;
        for (QQuickItem *item : items()) {
            if (!pred(item) || !visibleTree(item))
                continue;
            const int score = (hasAncestor(item, "QQuickOverlay") ? 2 : 0) + (onScreen(item) ? 1 : 0);
            if (score >= bestScore) {
                best = item;
                bestScore = score;
            }
        }
        return best;
    }
    QQuickItem *findText(const QString &text, const char *within = nullptr, int timeoutMs = 3000) const
    {
        QQuickItem *found = nullptr;
        waitFor(
            [&] {
                found = find([&](QQuickItem *item) {
                    return labelOf(item) == text && (!within || hasAncestor(item, within));
                });
                return found != nullptr;
            },
            timeoutMs);
        return found;
    }
    QQuickItem *findType(const char *type, const std::function<bool(QQuickItem *)> &pred = {}) const
    {
        return find([&](QQuickItem *item) { return isA(item, type) && (!pred || pred(item)); });
    }

    // ---- Input ----------------------------------------------------------------------------------
    /// Scroll the nearest flickable ancestors so that `item` is on screen.
    void scrollIntoView(QQuickItem *item)
    {
        for (QQuickItem *p = item->parentItem(); p && !onScreen(item); p = p->parentItem()) {
            if (!p->inherits("QQuickFlickable"))
                continue;
            auto *content = p->property("contentItem").value<QQuickItem *>();
            if (!content)
                continue;
            const QRectF r = item->mapRectToItem(content, QRectF(0, 0, item->width(), item->height()));
            const qreal h = p->height();
            const qreal originY = p->property("originY").toReal();
            const qreal maxY = originY + std::max<qreal>(0, p->property("contentHeight").toReal() - h);
            const qreal y = std::clamp<qreal>(r.center().y() - h / 2, originY, maxY);
            p->setProperty("contentY", y);
            settle(150);
        }
    }
    /// The chain of items under a scene point, topmost first (debugging clicks that do nothing).
    QString itemsAt(QPointF scenePos) const
    {
        QStringList chain;
        QQuickItem *item = m_window->contentItem();
        while (item) {
            QList<QQuickItem *> children = item->childItems();
            std::stable_sort(children.begin(), children.end(),
                             [](const QQuickItem *a, const QQuickItem *b) { return a->z() < b->z(); });
            QQuickItem *next = nullptr;
            for (auto it = children.crbegin(); it != children.crend(); ++it) {
                QQuickItem *c = *it;
                if (!c->isVisible() || c->opacity() <= 0.01)
                    continue;
                if (c->contains(c->mapFromScene(scenePos))) {
                    next = c;
                    break;
                }
            }
            if (next)
                chain.append(QString::fromLatin1(next->metaObject()->className()) + u'/' + next->objectName());
            item = next;
        }
        return chain.join(u" > "_s);
    }
    /// A click with the mouse, or on touch UIs (--touch, the phone runs) a tap with a finger.
    void clickAt(QPointF scenePos)
    {
        if (qEnvironmentVariableIsSet("BRULION_JOURNEY_DEBUG"))
            log(u"click %1,%2: %3"_s.arg(scenePos.x()).arg(scenePos.y()).arg(itemsAt(scenePos)));
        if (m_touch) {
            // QTouchEventSequence's constructor is protected (QtTest's touchEvent() subclasses it).
            struct Sequence : QTest::QTouchEventSequence {
                Sequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) {}
            };
            static QPointingDevice *device = QTest::createTouchDevice();
            Sequence sequence(m_window, device);
            sequence.press(0, scenePos.toPoint(), m_window).commit();
            settle(50); // a quick tap
            sequence.release(0, scenePos.toPoint(), m_window).commit();
            settle(250);
            return;
        }
        const QPointF global = m_window->mapToGlobal(scenePos);
        m_timestamp += 1000; // never a double click with the previous one
        qt_handleMouseEvent(m_window, scenePos, global, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier,
                            m_timestamp);
        settle(20);
        qt_handleMouseEvent(m_window, scenePos, global, Qt::LeftButton, Qt::LeftButton, QEvent::MouseButtonPress,
                            Qt::NoModifier, m_timestamp += 20);
        settle(40);
        qt_handleMouseEvent(m_window, scenePos, global, Qt::NoButton, Qt::LeftButton, QEvent::MouseButtonRelease,
                            Qt::NoModifier, m_timestamp += 80);
        settle(250);
    }
    bool click(QQuickItem *item, qreal fx = 0.5, qreal fy = 0.5)
    {
        if (!item)
            return false;
        scrollIntoView(item);
        if (!onScreen(item))
            log(u"warning: clicking an item that is not on screen: "_s + labelOf(item));
        clickAt(item->mapToScene(QPointF(item->width() * fx, item->height() * fy)));
        return true;
    }
    bool clickText(const QString &text, const char *within = nullptr)
    {
        QQuickItem *item = findText(text, within);
        if (!item) {
            log(u"no visible item with the text \""_s + text + u'"');
            return false;
        }
        return click(item);
    }
    bool key(int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier, const QString &text = QString())
    {
        QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
        press.ignore();
        QCoreApplication::sendEvent(m_window, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers, text);
        release.ignore();
        QCoreApplication::sendEvent(m_window, &release);
        settle(30);
        return press.isAccepted() || release.isAccepted();
    }
    void type(const QString &text)
    {
        for (const QChar ch : text) {
            const char16_t u = ch.unicode();
            int code = 0;
            if (u == u' ')
                code = Qt::Key_Space;
            else if (u < 0x80 && ch.isPrint())
                code = ch.toUpper().unicode(); // Qt::Key_A … Key_Z, digits and ASCII punctuation
            key(code, Qt::NoModifier, QString(ch));
        }
        settle(150);
    }

    // ---- The app ----------------------------------------------------------------------------------
    QString view() const { return m_app->currentView(); }
    bool narrow() const { return m_window->width() < 900; }
    QObject *drawer() const { return m_window->findChild<QObject *>(u"shellDrawer"_s); }
    bool editorHasKeyboard() const
    {
        return waitFor([this] {
            const QQuickItem *f = m_window->activeFocusItem();
            return f && isA(f, "InlineEditor");
        });
    }
    QQuickItem *today() const
    {
        return findType("ContentJournalDay", [](QQuickItem *day) { return day->property("isToday").toBool(); });
    }
    QString todayPath() const
    {
        return m_app->workspace()->journalPath(m_app->today());
    }
    bool fileHas(const QString &path, const QString &text) const
    {
        return m_app->workspace()->readFile(path).contains(text);
    }
    void closePopups()
    {
        if (QObject *d = drawer(); d && d->property("visible").toBool()) {
            QMetaObject::invokeMethod(d, "close");
            waitFor([d] { return !d->property("visible").toBool(); }, 1500);
        }
    }
    /// Open a view the way a user does: the sidebar (wide), the bottom bar or the drawer (phone).
    void go(const QString &viewName, const char *sidebarText, const char *bottomText = nullptr)
    {
        closePopups();
        bool clicked = false;
        if (narrow() && bottomText)
            clicked = clickText(ui("ShellBottomNav", bottomText), "ShellBottomNav");
        if (!clicked && narrow() && sidebarText) {
            if (clickText(ui("ShellBottomNav", "Menu"), "ShellBottomNav")) {
                waitFor([this] { return drawer() && drawer()->property("opened").toBool(); }, 2000);
                if (!m_drawerShot) {
                    m_drawerShot = true;
                    shot(u"drawer"_s, 500);
                }
                clicked = clickText(ui("ShellSidebar", sidebarText), "ShellSidebar");
            }
        }
        if (!clicked && !narrow() && sidebarText)
            clicked = clickText(ui("ShellSidebar", sidebarText), "ShellSidebar");
        if (!clicked) {
            fallback(u"navigate to "_s + viewName);
            m_app->navigate({ { u"view"_s, viewName } });
        }
        if (!waitFor([&] { return view() == viewName; }, 3000))
            fail(u"view "_s + viewName + u" opens (now: "_s + view() + u')');
        closePopups();
        settle(300);
    }
    /// A top bar "More" menu item (`shotName`: a screenshot of the open menu first).
    bool moreMenu(const char *itemText, const QString &shotName = QString())
    {
        closePopups();
        if (!clickText(ui("ShellTopBar", "More"), "ShellTopBar"))
            return false;
        settle(300);
        if (!shotName.isEmpty())
            shot(shotName, 300);
        if (clickText(ui("ShellTopBar", itemText)))
            return true;
        key(Qt::Key_Escape); // close the menu again
        settle(200);
        return false;
    }

    /// Click the first link whose URL contains `needle` in the rendered texts under `root`
    /// (found with Text.linkAt(), clicked in the middle of its line). False when none is shown.
    bool clickLink(QQuickItem *root, const QString &needle)
    {
        QList<QQuickItem *> inside;
        collect(root, inside);
        for (QQuickItem *item : inside) {
            if (!visibleTree(item) || !onScreen(item) || item->metaObject()->indexOfMethod("linkAt(double,double)") < 0)
                continue;
            for (qreal y = 2; y < item->height(); y += 4) {
                for (qreal x = 2; x < item->width(); x += 6) {
                    QString link;
                    QMetaObject::invokeMethod(item, "linkAt", qReturnArg(link), x, y);
                    if (!link.contains(needle))
                        continue;
                    qreal bottom = y;
                    for (QString below = link; below == link && bottom < item->height(); bottom += 1)
                        QMetaObject::invokeMethod(item, "linkAt", qReturnArg(below), x + 4, bottom + 1);
                    const QPointF point = item->mapToScene(QPointF(x + 6, (y + bottom) / 2));
                    if (qEnvironmentVariableIsSet("BRULION_JOURNEY_DEBUG"))
                        log(u"link %1 at %2,%3"_s.arg(link).arg(point.x()).arg(point.y()));
                    clickAt(point);
                    return true;
                }
            }
        }
        return false;
    }

    void emptyNotebook();
    void longTitles();
    void onboarding();
    void writeInToday();
    void calendar();
    void tasks();
    void pageThroughLink();
    void search();
    void graph();
    void sync();
    void importExport();
    void history();
    void settings();

    QQmlApplicationEngine *m_engine;
    AppController *m_app;
    QString m_outDir;
    QQuickWindow *m_window = nullptr;
    int m_shot = 0;
    int m_failures = 0;
    int m_timestamp = 0;
    bool m_drawerShot = false;
    const bool m_touch = QCoreApplication::arguments().contains(u"--touch"_s);
};

void Journey::onboarding()
{
    if (!waitFor([this] { return m_app->status() == u"onboarding" || m_app->status() == u"ready"; }, 20000)) {
        fail(u"the app starts (status: "_s + m_app->status() + u')');
        return;
    }
    if (m_app->status() == u"onboarding") {
        shot(u"onboarding"_s, 1200);
        if (!clickText(ui("OnboardingView", "Try the sample notebook"))) {
            fallback(u"open the sample notebook"_s);
            m_app->openDemoVault();
        }
    }
    if (!waitFor([this] { return m_app->status() == u"ready" && m_app->workspace()->ready(); }, 30000))
        fail(u"the sample notebook opens"_s);
    // The sample notebook opens on its guide page; the journal is one click away.
    waitFor([this] { return !view().isEmpty(); }, 5000);
    shot(u"sample-guide"_s, 1500);
    go(u"journals"_s, "Journals", "Journal");
    if (!waitFor([this] { return today() != nullptr; }, 5000))
        fail(u"the journals show today"_s);
    shot(u"journals"_s, 1500);
}

void Journey::writeInToday()
{
    QQuickItem *day = today();
    if (!day) {
        fail(u"today's journal is shown"_s);
        return;
    }
    // A click at the end of the day's last block (ContentJournalDay.begin(), what a click on the
    // day's prompt or below its blocks does).
    QMetaObject::invokeMethod(day, "begin");
    if (!editorHasKeyboard()) {
        fail(u"today's journal starts editing"_s);
        return;
    }
    auto *editor = day->property("editor").value<QQuickItem *>();
    // The editor creates its suggestion popup the first time there is something to complete.
    const auto suggesting = [&] {
        return waitFor(
                [&] {
                    const auto *suggestions = editor ? editor->property("suggestions").value<QObject *>() : nullptr;
                    return suggestions && suggestions->property("visible").toBool();
                },
                2000);
    };

    key(Qt::Key_End);
    key(Qt::Key_Return);
    type(u"Plan na wieczór"_s);
    key(Qt::Key_Return);
    key(Qt::Key_Tab);
    type(u"Nakarmić żółwia Franka"_s);
    key(Qt::Key_Return);
    type(u"Zobacz też [[Remo"_s);
    if (!suggesting())
        fail(u"[[ shows page suggestions"_s);
    shot(u"journal-link-suggestions"_s, 400);
    key(Qt::Key_Return);
    settle(200);
    key(Qt::Key_End);
    key(Qt::Key_Return);
    shot(u"journal-outline"_s, 300);
    type(u"/zad"_s);
    if (!suggesting())
        fail(u"/ shows the slash menu"_s);
    key(Qt::Key_Return);
    settle(200);
    type(u"Kupić bilety do kina /przyp"_s);
    if (!suggesting())
        fail(u"/przyp shows the slash menu"_s);
    shot(u"journal-slash-menu"_s, 400);
    key(Qt::Key_Return);
    if (!findText(ui("BReminderDialog", "New reminder"))) {
        fail(u"the slash command opens the reminder dialog"_s);
    } else {
        shot(u"reminder-dialog"_s, 600);
        if (!clickText(ui("BReminderDialog", "tomorrow 8:00")))
            fail(u"the quick pick 'tomorrow 8:00'"_s);
        shot(u"reminder-dialog-picked"_s, 400);
        if (!clickText(ui("BReminderDialog", "Save")))
            fail(u"the reminder dialog saves"_s);
    }
    const QString tomorrow = m_app->today(1);
    if (!waitFor([&] { return fileHas(todayPath(), u"Kupić bilety do kina ⏰ "_s + tomorrow + u" 08:00"_s); }, 3000))
        fail(u"the task with its reminder is written: "_s + m_app->workspace()->readFile(todayPath()).right(200));
    if (!fileHas(todayPath(), u"\t- Nakarmić żółwia Franka"_s) && !fileHas(todayPath(), u"  - Nakarmić żółwia Franka"_s))
        fail(u"Tab indented the block"_s);
    key(Qt::Key_Escape);
    key(Qt::Key_Escape);
    shot(u"journal-written"_s, 900);
}

void Journey::calendar()
{
    go(u"calendar"_s, "Calendar", "Calendar");
    shot(u"calendar"_s, 1200);
    // The next day (keyboard: → in the month grid), where the reminder is.
    key(Qt::Key_Right);
    shot(u"calendar-tomorrow"_s, 800);
}

void Journey::tasks()
{
    go(u"tasks"_s, "Tasks", "Tasks");
    shot(u"tasks"_s, 1200);
    QQuickItem *row = findType("TasksRow", [](QQuickItem *r) {
        // `task` is the delegate's model object (roles as properties) or a map.
        const QVariant task = r->property("task");
        const QString text = task.canConvert<QObject *>() && task.value<QObject *>()
                                 ? task.value<QObject *>()->property("text").toString()
                                 : task.toMap().value(u"text"_s).toString();
        return text.contains(u"Kupić bilety do kina"_s);
    });
    QQuickItem *box = nullptr;
    if (row) {
        QList<QQuickItem *> inside;
        collect(row, inside);
        for (QQuickItem *item : inside) {
            if (isA(item, "TaskBox") && visibleTree(item)) {
                box = item;
                break;
            }
        }
    }
    if (!box) {
        fail(u"the new task is listed with a checkbox"_s);
        return;
    }
    click(box);
    if (!waitFor([&] { return fileHas(todayPath(), u"- [x] Kupić bilety do kina"_s); }, 3000))
        fail(u"ticking the task off writes [x]"_s);
    shot(u"tasks-done"_s, 600);
}

void Journey::pageThroughLink()
{
    go(u"journals"_s, "Journals", "Journal");
    QQuickItem *day = nullptr;
    waitFor([&] { return (day = today()) != nullptr; });
    if (!day) {
        fail(u"today's journal is shown again"_s);
        return;
    }
    // Where today's heading is right after the view opened and a moment later: the list must
    // not jump under the user's pointer.
    const qreal earlyY = day->mapToScene(QPointF(0, 0)).y();
    shot(u"journals-again-early"_s, 30);
    settle(900);
    const qreal lateY = day->mapToScene(QPointF(0, 0)).y();
    if (std::abs(lateY - earlyY) > 2)
        fail(u"the journal list jumps after opening: today's heading moved from y=%1 to y=%2"_s.arg(earlyY).arg(lateY));
    // A [[Remont mieszkania]] link in today's rendered text.
    if (!clickLink(day, u"Remont"_s)) {
        fallback(u"open Remont mieszkania (no link found on screen)"_s);
        m_app->openPage(u"Projekty/Remont mieszkania.md"_s);
    }
    if (!waitFor([this] { return view() == u"page" && m_app->currentRoute().value(u"path"_s).toString().contains(u"Remont"_s); })) {
        fail(u"a link in the journal opens its page (now: "_s + view() + u')');
        m_app->openPage(u"Projekty/Remont mieszkania.md"_s);
    }
    shot(u"page"_s, 1200);
    // Down to its references (under the blocks).
    if (QQuickItem *editor = findType("BlockEditor", [](QQuickItem *e) { return e->objectName() == u"pageEditor"; })) {
        if (auto *list = editor->property("listView").value<QQuickItem *>()) {
            QMetaObject::invokeMethod(list, "positionViewAtEnd");
            shot(u"page-references"_s, 900);
        }
    } else {
        fail(u"the page view has its editor"_s);
    }
}

void Journey::search()
{
    go(u"search"_s, "Search", "Search");
    settle(400);
    const QQuickItem *focus = m_window->activeFocusItem();
    if (!focus || !focus->inherits("QQuickTextInput")) {
        if (QQuickItem *field = findType("BSearchField"))
            click(field);
        else
            fail(u"the search field"_s);
    }
    type(u"zolw"_s);
    settle(900);
    const bool found = find([](QQuickItem *item) {
                           return labelOf(item).contains(u"żółw"_s, Qt::CaseInsensitive) && hasAncestor(item, "SearchView");
                       }) != nullptr;
    if (!found)
        fail(u"searching \"zolw\" shows \"żółw\""_s);
    shot(u"search-zolw"_s, 300);
}

void Journey::graph()
{
    go(u"graph"_s, "Graph");
    shot(u"graph"_s, 2500);
}

void Journey::sync()
{
    closePopups();
    bool clicked = false;
    if (narrow() && clickText(ui("ShellBottomNav", "Menu"), "ShellBottomNav"))
        waitFor([this] { return drawer() && drawer()->property("opened").toBool(); }, 2000);
    if (QQuickItem *footer = findType("ShellSyncFooter"))
        clicked = click(footer, 0.25, 0.5);
    if (!clicked || !waitFor([this] { return view() == u"sync"; })) {
        fallback(u"open the sync center (the sidebar's sync footer)"_s);
        m_app->navigate({ { u"view"_s, u"sync"_s } });
    }
    closePopups();
    shot(u"sync"_s, 1000);
    if (clickText(ui("SyncView", "Nextcloud"), "BTabButton"))
        shot(u"sync-nextcloud"_s, 800);
    else
        fail(u"the Nextcloud tab"_s);
}

void Journey::importExport()
{
    closePopups();
    bool opened = false;
    if (!narrow())
        opened = clickText(ui("ShellTopBar", "Command palette"), "ShellTopBar");
    else
        opened = moreMenu("Command palette");
    if (opened && waitFor([this] {
            QObject *palette = m_window->findChild<QObject *>(u"shellPalette"_s);
            return palette && palette->property("opened").toBool();
        })) {
        type(u"import"_s);
        shot(u"palette-import"_s, 400);
        key(Qt::Key_Return);
    } else {
        fallback(u"open import and export (command palette)"_s);
        m_app->navigate({ { u"view"_s, u"importExport"_s } });
    }
    if (!waitFor([this] { return view() == u"importExport"; }))
        fail(u"import and export opens"_s);
    shot(u"import"_s, 1000);
    if (clickText(ui("ImportExportView", "Export"), "BTabButton"))
        shot(u"export"_s, 800);
    else
        fail(u"the Export tab"_s);
}

void Journey::history()
{
    go(u"journals"_s, "Journals", "Journal");
    QQuickItem *day = nullptr;
    waitFor([&] { return (day = today()) != nullptr; });
    // Today's own button (every day has one).
    const auto insideToday = [day](const QQuickItem *item) {
        for (const QQuickItem *p = item; p; p = p->parentItem()) {
            if (p == day)
                return true;
        }
        return false;
    };
    QQuickItem *open = day ? find([&](QQuickItem *item) {
        return labelOf(item) == ui("ContentJournalDay", "Open as page") && item->isEnabled()
               && (isA(item, "BButton") || isA(item, "BIconButton")) && insideToday(item);
    })
                           : nullptr;
    if (!open || !click(open) || !waitFor([this] { return view() == u"journal"; })) {
        fallback(u"open today as a page"_s);
        m_app->openJournal(m_app->today());
    }
    settle(600);
    if (!moreMenu("Page history") || !waitFor([this] { return view() == u"history"; })) {
        fallback(u"open the page history (top bar menu)"_s);
        m_app->navigate({ { u"view"_s, u"history"_s }, { u"path"_s, todayPath() } });
    }
    shot(u"history"_s, 1200);
}

void Journey::longTitles()
{
    // Long Polish names: the top bar, the page header, the sidebar and All pages.
    const QString path = u"Projekty/Konstantynopolitańczykowianeczka i niedźwiedź — notatki z wyprawy do Szczebrzeszyna.md"_s;
    m_app->workspace()->writeFile(path, u"- Chrząszcz brzmi w trzcinie w Szczebrzeszynie [[Remont mieszkania]]\n"_s
                                        u"- Najdłuższe polskie słowo: dziewięćsetdziewięćdziesięciodziewięcioletni\n"_s);
    settle(500);
    m_app->openPage(path);
    waitFor([&] { return m_app->currentRoute().value(u"path"_s).toString() == path; });
    shot(u"long-title"_s, 1200);
    go(u"allPages"_s, "All pages");
    shot(u"all-pages"_s, 1000);
}

void Journey::settings()
{
    if (!moreMenu("Settings", u"more-menu"_s) || !waitFor([this] { return view() == u"settings"; })) {
        fallback(u"open the settings (top bar menu)"_s);
        m_app->navigate({ { u"view"_s, u"settings"_s } });
    }
    shot(u"settings"_s, 1000);
    const bool dark = m_app->settings()->themeMode() == u"dark";
    if (!clickText(u"English"_s))
        fail(u"the language switch"_s);
    if (!waitFor([this] { return m_app->settings()->language() == u"en"; }))
        fail(u"switching to English"_s);
    shot(u"settings-english"_s, 900);
    // Now in English: the theme the run did not start with.
    if (!clickText(dark ? u"Light"_s : u"Dark"_s, "SettingsView"))
        fail(u"the theme switch"_s);
    shot(u"settings-theme"_s, 900);
    go(u"journals"_s, "Journals", "Journal");
    shot(u"journals-after-settings"_s, 1200);
}

int Journey::run()
{
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().value(0));
    if (!m_window) {
        fail(u"the main window exists"_s);
        return m_failures;
    }
    QDir().mkpath(m_outDir);
    // --journey-theme light|dark: the theme a user chose (not --theme, which pins it for the
    // run: the settings step switches it).
    const QStringList args = QCoreApplication::arguments();
    if (const qsizetype i = args.indexOf(u"--journey-theme"_s); i >= 0 && i + 1 < args.size())
        m_app->settings()->setThemeMode(args.at(i + 1));
    m_window->requestActivate();
    waitFor([this] { return m_window->isActive(); }, 2000);

    if (args.contains(u"--journey-empty"_s)) {
        emptyNotebook();
        return m_failures;
    }
    onboarding();
    writeInToday();
    calendar();
    tasks();
    pageThroughLink();
    search();
    graph();
    sync();
    importExport();
    history();
    longTitles();
    settings();
    return m_failures;
}

void Journey::emptyNotebook()
{
    // A new, empty notebook in the suggested folder: what every view shows before the first note.
    if (!waitFor([this] { return m_app->status() == u"onboarding"; }, 20000)) {
        fail(u"onboarding shows"_s);
        return;
    }
    if (!clickText(ui("OnboardingView", "Open notebook"))) {
        fallback(u"open an empty notebook"_s);
        m_app->openVault(m_app->defaultVaultPath());
    }
    if (!waitFor([this] { return m_app->status() == u"ready" && m_app->workspace()->ready(); }, 30000))
        fail(u"the empty notebook opens"_s);
    waitFor([this] { return today() != nullptr; }, 5000);
    shot(u"empty-journals"_s, 1500);
    go(u"calendar"_s, "Calendar", "Calendar");
    shot(u"empty-calendar"_s, 1000);
    go(u"tasks"_s, "Tasks", "Tasks");
    shot(u"empty-tasks"_s, 1000);
    go(u"allPages"_s, "All pages");
    shot(u"empty-all-pages"_s, 1000);
    go(u"tags"_s, "Tags");
    shot(u"empty-tags"_s, 1000);
    go(u"graph"_s, "Graph");
    shot(u"empty-graph"_s, 1200);
    go(u"search"_s, "Search", "Search");
    if (const QQuickItem *focus = m_window->activeFocusItem(); !focus || !focus->inherits("QQuickTextInput")) {
        if (QQuickItem *field = findType("BSearchField"))
            click(field);
    }
    type(u"żółw"_s);
    shot(u"empty-search"_s, 900);
    if (!moreMenu("Trash") || !waitFor([this] { return view() == u"trash"; })) {
        fallback(u"open the trash (top bar menu)"_s);
        m_app->navigate({ { u"view"_s, u"trash"_s } });
    }
    shot(u"empty-trash"_s, 900);
    // The first note: a click on today's prompt and a few words.
    go(u"journals"_s, "Journals", "Journal");
    if (QQuickItem *prompt = findText(ui("ContentJournalDay", "What's on today? Click to start writing.")))
        click(prompt);
    else
        fail(u"today's prompt"_s);
    if (!editorHasKeyboard())
        fail(u"a click on today's prompt starts writing"_s);
    type(u"Pierwsza notatka — zażółć gęślą jaźń"_s);
    shot(u"empty-first-note"_s, 600);
    if (!waitFor([this] { return fileHas(todayPath(), u"Pierwsza notatka"_s); }, 3000))
        fail(u"the first note is saved"_s);
}

} // namespace

void startShellJourney(QQmlApplicationEngine *engine, AppController *app, const QString &outDir)
{
    QTimer::singleShot(0, app, [engine, app, outDir] {
        Journey journey(engine, app, outDir);
        const int failures = journey.run();
        std::fprintf(stderr, "journey: %d failure(s)\n", failures);
        QCoreApplication::exit(failures);
    });
}

} // namespace brulion::shell
