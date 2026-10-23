#include "uidriver.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHash>
#include <QKeyEvent>
#include <QKeySequence>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>
#include <qtestsupport_gui.h>

#include <algorithm>
#include <cmath>
#include <memory>

QT_BEGIN_NAMESPACE
// QtGui's entry points for synthetic input (what QtTest's functions call): the events go through
// QGuiApplication and the delivery agent like real ones, so pointer handlers (TapHandler) see
// whole clicks, and shortcuts (QShortcutMap: QML Shortcut items) see keys first.
Q_GUI_EXPORT void qt_handleMouseEvent(QWindow *window, const QPointF &local, const QPointF &global,
                                      Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                                      Qt::KeyboardModifiers mods, int timestamp);
Q_GUI_EXPORT bool qt_sendShortcutOverrideEvent(QObject *o, ulong timestamp, int k, Qt::KeyboardModifiers mods,
                                               const QString &text, bool autorep, ushort count);
QT_END_NAMESPACE

using namespace Qt::StringLiterals;

namespace brulion::uidriver {

// ---- Waiting ----------------------------------------------------------------------------------

void settle(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
}

bool waitFor(const std::function<bool()> &condition, int timeoutMs)
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

// ---- Items ------------------------------------------------------------------------------------

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

namespace {

QString cleanClassName(const char *className)
{
    QString name = QString::fromLatin1(className);
    for (const QString &marker : {u"_QMLTYPE_"_s, u"_QML_"_s}) {
        if (const qsizetype at = name.indexOf(marker); at > 0)
            name.truncate(at);
    }
    return name;
}

} // namespace

QString typeName(const QObject *object)
{
    return object ? cleanClassName(object->metaObject()->className()) : QString();
}

bool shown(const QQuickItem *item)
{
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (!p->isVisible() || p->opacity() <= 0.01)
            return false;
    }
    return item->width() >= 1 && item->height() >= 1;
}

bool onScreen(const QQuickWindow *window, const QQuickItem *item)
{
    const QPointF c = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    if (!QRectF(0, 0, window->width(), window->height()).contains(c))
        return false;
    for (const QQuickItem *p = item->parentItem(); p; p = p->parentItem()) {
        if (p->clip() && !p->mapRectToScene(QRectF(0, 0, p->width(), p->height())).contains(c))
            return false;
    }
    return true;
}

QPointF pointIn(const QQuickItem *item, qreal fx, qreal fy)
{
    return item->mapToScene(QPointF(item->width() * fx, item->height() * fy));
}

QString labelOf(const QQuickItem *item)
{
    for (const char *name : {"text", "label", "title"}) {
        const QVariant v = item->property(name);
        if (v.metaType() == QMetaType::fromType<QString>() && !v.toString().isEmpty())
            return v.toString();
    }
    return {};
}

QList<QQuickItem *> allItems(QQuickItem *root)
{
    QList<QQuickItem *> out;
    if (!root)
        return out;
    const std::function<void(QQuickItem *)> collect = [&](QQuickItem *item) {
        out.append(item);
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            collect(child);
    };
    collect(root);
    return out;
}

QList<QQuickItem *> matching(QQuickWindow *window, const std::function<bool(QQuickItem *)> &pred, bool shownOnly)
{
    struct Ranked {
        QQuickItem *item;
        int score;
        qsizetype order;
    };
    QList<Ranked> ranked;
    const QList<QQuickItem *> items = allItems(window->contentItem());
    for (qsizetype i = 0; i < items.size(); ++i) {
        QQuickItem *item = items.at(i);
        if (!pred(item))
            continue;
        const bool visible = shown(item);
        if (shownOnly && !visible)
            continue;
        const int score =
                (visible ? 4 : 0) + (hasAncestor(item, "QQuickOverlay") ? 2 : 0) + (onScreen(window, item) ? 1 : 0);
        ranked.append({item, score, i});
    }
    // Popups first, then on screen, then the last one painted.
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked &a, const Ranked &b) {
        return a.score != b.score ? a.score > b.score : a.order > b.order;
    });
    QList<QQuickItem *> out;
    out.reserve(ranked.size());
    for (const Ranked &r : std::as_const(ranked))
        out.append(r.item);
    return out;
}

QQuickItem *topmost(QQuickWindow *window, const std::function<bool(QQuickItem *)> &pred)
{
    return matching(window, pred, true).value(0);
}

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

QQuickItem *shownNamed(QQuickItem *root, const QString &name)
{
    if (!root || !root->isVisible())
        return nullptr;
    if (root->objectName() == name)
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = shownNamed(child, name))
            return found;
    }
    return nullptr;
}

QQuickItem *enclosingFlickable(QQuickItem *item)
{
    for (QQuickItem *p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
        if (p->inherits("QQuickFlickable"))
            return p;
    }
    return nullptr;
}

QQuickItem *renderedTextContaining(QQuickItem *root, const QString &needle)
{
    const QRectF area = root->mapRectToScene(QRectF(0, 0, root->width(), root->height()));
    QList<QQuickItem *> stack{root};
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
    return {-1, -1};
}

QString itemsAt(QQuickWindow *window, QPointF scenePos)
{
    QStringList chain;
    QQuickItem *item = window->contentItem();
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

namespace {

/// Visit the visible items under the window's content item; `popupItem(item, popup)` for each
/// popup's item (its parent is the QQuickPopup).
void visitPopupItems(QQuickWindow *window, const std::function<void(QQuickItem *, QObject *)> &popupItem)
{
    const std::function<void(QQuickItem *)> visit = [&](QQuickItem *item) {
        if (!item->isVisible())
            return;
        if (item->inherits("QQuickPopupItem") && item->parent())
            popupItem(item, item->parent());
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            visit(child);
    };
    visit(window->contentItem());
}

} // namespace

int openMenus(QQuickWindow *window)
{
    int open = 0;
    visitPopupItems(window, [&](QQuickItem *, QObject *popup) {
        if (popup->inherits("QQuickMenu"))
            ++open;
    });
    return open;
}

void closeMenus(QQuickWindow *window)
{
    visitPopupItems(window, [](QQuickItem *, QObject *popup) {
        if (popup->inherits("QQuickMenu"))
            QMetaObject::invokeMethod(popup, "close");
    });
    waitFor([window] { return openMenus(window) == 0; });
}

QList<QObject *> openPopups(QQuickWindow *window)
{
    QList<QObject *> out;
    visitPopupItems(window, [&](QQuickItem *, QObject *popup) {
        if (popup->inherits("QQuickPopup") && !out.contains(popup))
            out.append(popup);
    });
    return out;
}

bool scrollIntoView(QQuickWindow *window, QQuickItem *item, const std::function<void()> &afterEach)
{
    for (QQuickItem *p = item->parentItem(); p && !onScreen(window, item); p = p->parentItem()) {
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
        // Sideways too: a row of tools wider than the screen (the phone's editing toolbar) — not a
        // flickable that only flicks up and down (wider content there is not to be scrolled to).
        constexpr int verticalFlickOnly = 0x2; // Flickable.VerticalFlick
        const qreal w = p->width();
        const qreal originX = p->property("originX").toReal();
        const qreal maxX = originX + std::max<qreal>(0, p->property("contentWidth").toReal() - w);
        if (maxX > originX && p->property("flickableDirection").toInt() != verticalFlickOnly)
            p->setProperty("contentX", std::clamp<qreal>(r.center().x() - w / 2, originX, maxX));
        if (afterEach)
            afterEach();
    }
    return onScreen(window, item);
}

namespace {

/// Does `item` take presses itself: touch, mouse buttons, or a pointer handler that does?
bool takesPresses(const QQuickItem *item)
{
    // The popups' overlay covers the window and accepts presses, but lets them through while no
    // popup is open in it (items of the app's own shown there — toasts — do not count).
    if (item->inherits("QQuickOverlay")) {
        const QList<QQuickItem *> children = item->childItems();
        return std::any_of(children.cbegin(), children.cend(),
                           [](const QQuickItem *c) { return c->isVisible() && c->inherits("QQuickPopupItem"); });
    }
    if (item->acceptTouchEvents() || item->acceptedMouseButtons() != Qt::NoButton)
        return true;
    const QObjectList children = item->children();
    for (const QObject *child : children) {
        if (child->inherits("QQuickPointerHandler") && !child->inherits("QQuickHoverHandler")
            && !child->inherits("QQuickWheelHandler") && !child->inherits("QQuickPointHandler")
            && child->property("enabled").toBool())
            return true;
    }
    return false;
}

QQuickItem *pressTargetUnder(QQuickItem *item, QPointF scenePos)
{
    // As Qt Quick delivers a press: visible, enabled items (opacity does not matter), in a
    // clipping item only where it is, the children painted last first.
    if (!item->isVisible() || !item->isEnabled())
        return nullptr;
    const QPointF local = item->mapFromScene(scenePos);
    if (item->clip() && !QRectF(0, 0, item->width(), item->height()).contains(local))
        return nullptr; // nothing of it there, its children clipped too
    QList<QQuickItem *> children = item->childItems();
    std::stable_sort(children.begin(), children.end(),
                     [](const QQuickItem *a, const QQuickItem *b) { return a->z() < b->z(); });
    for (auto it = children.crbegin(); it != children.crend(); ++it) {
        if (QQuickItem *found = pressTargetUnder(*it, scenePos))
            return found;
    }
    if (item->contains(local) && (takesPresses(item) || item->inherits("QQuickFlickable")))
        return item;
    return nullptr;
}

} // namespace

QQuickItem *pressTargetAt(QQuickWindow *window, QPointF scenePos)
{
    return window ? pressTargetUnder(window->contentItem(), scenePos) : nullptr;
}

QList<QQuickItem *> flickablesAt(QQuickWindow *window, QPointF scenePos)
{
    QList<QQuickItem *> out;
    for (QQuickItem *p = pressTargetAt(window, scenePos); p; p = p->parentItem()) {
        if (p->inherits("QQuickFlickable"))
            out.append(p);
    }
    return out;
}

bool canScroll(const QQuickItem *flickable, QPointF delta)
{
    if (!flickable || !flickable->property("interactive").toBool() || !flickable->isEnabled())
        return false;
    // Flickable.FlickableDirection: AutoFlickDirection 0, HorizontalFlick 1, VerticalFlick 2,
    // HorizontalAndVerticalFlick 3, AutoFlickIfNeeded 0xc; BoundsBehavior: DragOverBounds 1.
    const int direction = flickable->property("flickableDirection").toInt();
    const bool dragOverBounds = flickable->property("boundsBehavior").toInt() & 0x1;
    // The content's size as Flickable reads it: its content item's (contentWidth -1 is the view's).
    const auto *contentItem = flickable->property("contentItem").value<QQuickItem *>();
    const auto axis = [&](qreal d, bool vertical) {
        if (d == 0 || !contentItem)
            return false;
        const qreal margins = flickable->property(vertical ? "topMargin" : "leftMargin").toReal()
                              + flickable->property(vertical ? "bottomMargin" : "rightMargin").toReal();
        const qreal content = (vertical ? contentItem->height() : contentItem->width()) + margins;
        const qreal view = vertical ? flickable->height() : flickable->width();
        const int flag = vertical ? 0x2 : 0x1;
        bool flicks = false;
        if (direction == 0)
            flicks = std::floor(std::abs(content - view)) > 0;
        else if (direction & 0xc)
            flicks = (direction & flag) || content > view;
        else
            flicks = direction & flag;
        if (!flicks)
            return false;
        if (dragOverBounds)
            return true;
        const qreal origin = flickable->property(vertical ? "originY" : "originX").toReal();
        const qreal at = flickable->property(vertical ? "contentY" : "contentX").toReal();
        const qreal start = origin - flickable->property(vertical ? "topMargin" : "leftMargin").toReal();
        const qreal end = start + std::max<qreal>(0, content - view);
        return d > 0 ? at < end - 0.5 : at > start + 0.5;
    };
    return axis(delta.y(), true) || axis(delta.x(), false);
}

// ---- Playing gestures ---------------------------------------------------------------------------

void run(const Gesture &gesture)
{
    for (const Step &step : gesture) {
        if (step.act)
            step.act();
        if (step.flush)
            QCoreApplication::processEvents();
        if (step.pauseMs > 0)
            settle(step.pauseMs);
    }
}

namespace {

/// Plays a gesture from timers: each step, then back to the event loop for its pause.
class Player : public QObject {
public:
    Player(QObject *owner, QQuickWindow *window, Gesture gesture, std::function<void(bool)> done,
           std::function<bool()> keepGoing)
        : QObject(owner)
        , m_window(window)
        , m_steps(std::move(gesture))
        , m_done(std::move(done))
        , m_keepGoing(std::move(keepGoing))
    { }

    void start()
    {
        QTimer::singleShot(0, this, [this] { next(); });
    }

private:
    void next()
    {
        while (m_index < m_steps.size()) {
            if (!m_window || (m_keepGoing && !m_keepGoing())) {
                finish(false);
                return;
            }
            const Step &step = m_steps.at(m_index++);
            if (step.act)
                step.act();
            if (step.flush || step.pauseMs > 0) {
                QTimer::singleShot(step.pauseMs, Qt::PreciseTimer, this, [this] { next(); });
                return;
            }
        }
        finish(bool(m_window));
    }
    void finish(bool completed)
    {
        std::function<void(bool)> done = std::move(m_done);
        deleteLater();
        if (done)
            done(completed);
    }

    QPointer<QQuickWindow> m_window;
    Gesture m_steps;
    qsizetype m_index = 0;
    std::function<void(bool)> m_done;
    std::function<bool()> m_keepGoing;
};

/// QTouchEventSequence's constructor is protected (QtTest's touchEvent() subclasses it too).
struct TouchSequence : QTest::QTouchEventSequence {
    TouchSequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) { }
};

QPointingDevice *touchDevice()
{
    static QPointingDevice *device = QTest::createTouchDevice();
    return device;
}

int g_mouseTimestamp = 0;

Step mouseEvent(QQuickWindow *window, QPointF pos, Qt::MouseButtons state, Qt::MouseButton button, QEvent::Type type,
                Qt::KeyboardModifiers modifiers, int timestamp, int pauseMs)
{
    return Step{[window, pos, state, button, type, modifiers, timestamp] {
                    qt_handleMouseEvent(window, pos, window->mapToGlobal(pos), state, button, type, modifiers,
                                        timestamp);
                },
                pauseMs, false};
}

} // namespace

void play(QObject *owner, QQuickWindow *window, const Gesture &gesture, std::function<void(bool)> done,
          std::function<bool()> keepGoing)
{
    auto *player = new Player(owner, window, gesture, std::move(done), std::move(keepGoing));
    player->start();
}

int mouseTimestamp(int advance)
{
    return g_mouseTimestamp += advance;
}

// ---- Mouse ------------------------------------------------------------------------------------

Gesture mouseMove(QQuickWindow *window, QPointF scenePos, int pauseMs)
{
    return {mouseEvent(window, scenePos, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, Qt::NoModifier,
                       mouseTimestamp(1000), pauseMs)};
}

Gesture mouseClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers,
                   const ClickTiming &timing)
{
    const int t = mouseTimestamp(1000); // never a double click with the previous one
    const int press = mouseTimestamp(timing.pressGap);
    const int release = mouseTimestamp(timing.releaseGap);
    return {mouseEvent(window, scenePos, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, modifiers, t, timing.afterMove),
            mouseEvent(window, scenePos, button, button, QEvent::MouseButtonPress, modifiers, press, timing.afterPress),
            mouseEvent(window, scenePos, Qt::NoButton, button, QEvent::MouseButtonRelease, modifiers, release,
                       timing.afterRelease)};
}

Gesture mouseDoubleClick(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button,
                         Qt::KeyboardModifiers modifiers)
{
    const int t = mouseTimestamp(1000);
    Gesture g{mouseEvent(window, scenePos, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, modifiers, t, 20)};
    // Both presses within the double-click interval (event timestamps decide it).
    for (int i = 0; i < 2; ++i) {
        g.append(mouseEvent(window, scenePos, button, button, QEvent::MouseButtonPress, modifiers, mouseTimestamp(40),
                            30));
        g.append(mouseEvent(window, scenePos, Qt::NoButton, button, QEvent::MouseButtonRelease, modifiers,
                            mouseTimestamp(40), i == 0 ? 30 : 200));
    }
    return g;
}

Gesture mouseLongPress(QQuickWindow *window, QPointF scenePos, int holdMs, Qt::MouseButton button)
{
    ClickTiming timing;
    timing.afterMove = 20;
    timing.afterPress = holdMs;
    timing.releaseGap = holdMs;
    timing.afterRelease = 250;
    return mouseClick(window, scenePos, button, Qt::NoModifier, timing);
}

Gesture mouseDrag(QQuickWindow *window, QPointF from, QPointF to, int holdMs, bool escape, Qt::MouseButton button,
                  Qt::KeyboardModifiers modifiers)
{
    const int t = mouseTimestamp(1000);
    Gesture g{mouseEvent(window, from, Qt::NoButton, Qt::NoButton, QEvent::MouseMove, modifiers, t, 0),
              mouseEvent(window, from, button, button, QEvent::MouseButtonPress, modifiers, mouseTimestamp(20), 40)};
    constexpr int steps = 16;
    for (int step = 1; step <= steps; ++step) {
        const QPointF p = from + (to - from) * step / double(steps);
        g.append(mouseEvent(window, p, button, Qt::NoButton, QEvent::MouseMove, modifiers, mouseTimestamp(16),
                            step == steps ? 16 + holdMs : 16));
    }
    if (escape) {
        g.append(Step{[window] {
                          QKeyEvent press(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
                          QCoreApplication::sendEvent(window, &press);
                          QKeyEvent release(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
                          QCoreApplication::sendEvent(window, &release);
                      },
                      50, false});
    }
    g.append(mouseEvent(window, to, Qt::NoButton, button, QEvent::MouseButtonRelease, modifiers, mouseTimestamp(20),
                        200));
    return g;
}

// ---- Touch ------------------------------------------------------------------------------------

Gesture touchTap(QQuickWindow *window, QPointF scenePos, int holdMs, int afterMs)
{
    auto sequence = std::make_shared<TouchSequence>(window, touchDevice());
    const QPoint p = scenePos.toPoint();
    return {Step{[sequence, window, p] { sequence->press(0, p, window).commit(false); }, holdMs, true},
            Step{[sequence, window, p] { sequence->release(0, p, window).commit(false); }, afterMs, true}};
}

Gesture touchDoubleTap(QQuickWindow *window, QPointF scenePos)
{
    return touchTap(window, scenePos, 40, 60) + touchTap(window, scenePos, 40, 250);
}

Gesture touchDrag(QQuickWindow *window, QPointF from, QPointF delta, int steps, int stepMs, int holdMs, int afterMs)
{
    auto sequence = std::make_shared<TouchSequence>(window, touchDevice());
    steps = qMax(1, steps);
    Gesture g{Step{[sequence, window, p = from.toPoint()] { sequence->press(0, p, window).commit(false); }, stepMs,
                   true}};
    for (int step = 1; step <= steps; ++step) {
        const QPoint p = (from + delta * step / double(steps)).toPoint();
        g.append(Step{[sequence, window, p] { sequence->move(0, p, window).commit(false); },
                      step == steps ? holdMs : stepMs, true});
    }
    g.append(Step{[sequence, window, p = (from + delta).toPoint()] { sequence->release(0, p, window).commit(false); },
                  afterMs, true});
    return g;
}

Gesture touchScroll(QQuickWindow *window, QPointF scenePos, QPointF delta, bool crossThreshold)
{
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length <= 0)
        return {};
    // The finger moves against the way the content is to go (the content follows it).
    const QPointF unit = -delta / length;
    QList<qreal> stops;
    if (crossThreshold) {
        // Flickable measures the threshold per axis: the dominant one crosses it.
        const qreal along = std::max(std::abs(unit.x()), std::abs(unit.y()));
        const qreal threshold = QGuiApplication::styleHints()->startDragDistance();
        stops = {(threshold + 1) / along, (threshold + 2) / along};
    }
    const qreal start = stops.isEmpty() ? 0 : stops.last();
    const int steps = qMax(1, int(std::ceil(length / 12.0)));
    for (int step = 1; step <= steps; ++step)
        stops.append(start + length * step / steps);
    auto sequence = std::make_shared<TouchSequence>(window, touchDevice());
    Gesture g{Step{[sequence, window, p = scenePos.toPoint()] { sequence->press(0, p, window).commit(false); }, 16,
                   true}};
    for (qsizetype i = 0; i < stops.size(); ++i) {
        const QPoint p = (scenePos + unit * stops.at(i)).toPoint();
        g.append(Step{[sequence, window, p] { sequence->move(0, p, window).commit(false); },
                      i + 1 == stops.size() ? 120 : 16, true});
    }
    g.append(Step{[sequence, window, p = (scenePos + unit * stops.last()).toPoint()] {
                      sequence->release(0, p, window).commit(false);
                  },
                  300, true});
    return g;
}

// ---- Wheel ------------------------------------------------------------------------------------

Gesture wheel(QQuickWindow *window, QPointF scenePos, QPointF delta, Qt::KeyboardModifiers modifiers)
{
    const auto send = [window, scenePos, modifiers](QPoint pixels, Qt::ScrollPhase phase) {
        return [window, scenePos, modifiers, pixels, phase] {
            // Flickable reads the speed from the timestamps (events without time are dropped).
            static QElapsedTimer clock;
            if (!clock.isValid())
                clock.start();
            QWheelEvent event(scenePos, window->mapToGlobal(scenePos), pixels, pixels, Qt::NoButton, modifiers, phase,
                              false, Qt::MouseEventSynthesizedBySystem);
            event.setTimestamp(ulong(clock.elapsed()) + 1);
            QCoreApplication::sendEvent(window, &event);
        };
    };
    Gesture g{Step{send(QPoint(), Qt::ScrollBegin), 8, false}};
    // Flickable starts its drag with the first two reports and moves the content from the third:
    // two of a pixel first, so the content then moves by `delta` exactly.
    const QPoint lead(delta.x() > 0 ? -1 : delta.x() < 0 ? 1 : 0, delta.y() > 0 ? -1 : delta.y() < 0 ? 1 : 0);
    g.append(Step{send(lead, Qt::ScrollUpdate), 8, false});
    g.append(Step{send(lead, Qt::ScrollUpdate), 8, false});
    // Two-finger scrolling: the content follows the fingers, at most 40 px per report.
    const qreal length = std::hypot(delta.x(), delta.y());
    const int steps = qMax(1, int(std::ceil(length / 40.0)));
    QPoint sent;
    for (int step = 1; step <= steps; ++step) {
        const QPoint total = (delta * step / double(steps)).toPoint();
        const QPoint pixels = -(total - sent);
        sent = total;
        g.append(Step{send(pixels, Qt::ScrollUpdate), 8, false});
    }
    g.append(Step{send(QPoint(), Qt::ScrollEnd), 150, false});
    return g;
}

// ---- Keys -------------------------------------------------------------------------------------

bool deliverKey(QQuickWindow *window, const KeyStroke &stroke, KeyDelivery delivery)
{
    if (delivery == KeyDelivery::System) {
        static QElapsedTimer clock;
        if (!clock.isValid())
            clock.start();
        // Shortcuts first, as QGuiApplication does with a keyboard's key (the focused item may
        // claim the key with a ShortcutOverride event: a text field keeps Ctrl+A).
        if (qt_sendShortcutOverrideEvent(window, ulong(clock.elapsed()), stroke.key, stroke.modifiers, stroke.text,
                                         false, 1))
            return true;
    }
    QKeyEvent press(QEvent::KeyPress, stroke.key, stroke.modifiers, stroke.text);
    press.ignore();
    QCoreApplication::sendEvent(window, &press);
    QKeyEvent release(QEvent::KeyRelease, stroke.key, stroke.modifiers, stroke.text);
    release.ignore();
    QCoreApplication::sendEvent(window, &release);
    return press.isAccepted() || release.isAccepted();
}

bool sendKey(QQuickWindow *window, int key, Qt::KeyboardModifiers modifiers, const QString &text, int pauseMs)
{
    const bool handled = deliverKey(window, KeyStroke{key, modifiers, text}, KeyDelivery::FocusItem);
    settle(pauseMs); // let bindings, transitions and queued calls settle
    return handled;
}

namespace {

/// The text a key gives without Ctrl, Alt or Meta held.
QString keyText(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return {};
    switch (key) {
    case Qt::Key_Space: return u" "_s;
    case Qt::Key_Return:
    case Qt::Key_Enter: return u"\r"_s;
    case Qt::Key_Tab: return u"\t"_s;
    default: break;
    }
    if (key >= 0x20 && key < 0x110000) {
        const char32_t code = char32_t(key);
        const QString text = QString::fromUcs4(&code, 1);
        if (!text.isEmpty() && text.at(0).isPrint())
            return modifiers & Qt::ShiftModifier ? text.toUpper() : text.toLower();
    }
    return {};
}

/// Names people write that QKeySequence spells differently.
QString canonicalKeyName(const QString &name)
{
    static const QHash<QString, QString> aliases{
            {u"escape"_s, u"Esc"_s},       {u"delete"_s, u"Del"_s},       {u"insert"_s, u"Ins"_s},
            {u"pageup"_s, u"PgUp"_s},      {u"pagedown"_s, u"PgDown"_s},  {u"page up"_s, u"PgUp"_s},
            {u"page down"_s, u"PgDown"_s}, {u"arrowup"_s, u"Up"_s},       {u"arrowdown"_s, u"Down"_s},
            {u"arrowleft"_s, u"Left"_s},   {u"arrowright"_s, u"Right"_s}, {u"bksp"_s, u"Backspace"_s},
            {u"spacebar"_s, u"Space"_s},   {u"menu"_s, u"Menu"_s},        {u"back"_s, u"Back"_s},
            {u"plus"_s, u"+"_s},           {u"comma"_s, u","_s},
    };
    return aliases.value(name.toLower(), name);
}

} // namespace

std::optional<QList<KeyStroke>> parseKeySequence(const QString &sequence, QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<QList<KeyStroke>> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    // Chords separated by commas; a comma right after '+' (or alone) is the comma key.
    QStringList chords;
    QString current;
    for (const QChar c : sequence) {
        if (c == u',' && !current.trimmed().isEmpty() && !current.trimmed().endsWith(u'+')) {
            chords.append(current.trimmed());
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.trimmed().isEmpty())
        chords.append(current.trimmed());
    if (chords.isEmpty())
        return fail(u"No keys named"_s);
    QList<KeyStroke> out;
    for (const QString &chord : std::as_const(chords)) {
        // "Ctrl++" ends with the plus key.
        QStringList parts;
        QString rest = chord;
        QString keyName;
        if (rest.endsWith(u"++")) {
            keyName = u"+"_s;
            rest.chop(2);
        } else if (rest == u"+") {
            keyName = u"+"_s;
            rest.clear();
        }
        parts = rest.split(u'+', Qt::SkipEmptyParts);
        if (keyName.isEmpty()) {
            if (parts.isEmpty())
                return fail(u"No key in \"%1\""_s.arg(chord));
            keyName = parts.takeLast().trimmed();
        }
        Qt::KeyboardModifiers modifiers;
        for (const QString &raw : std::as_const(parts)) {
            const QString m = raw.trimmed().toLower();
            if (m == u"ctrl" || m == u"control")
                modifiers |= Qt::ControlModifier;
            else if (m == u"shift")
                modifiers |= Qt::ShiftModifier;
            else if (m == u"alt" || m == u"option")
                modifiers |= Qt::AltModifier;
            else if (m == u"meta" || m == u"super" || m == u"win" || m == u"cmd")
                modifiers |= Qt::MetaModifier;
            else
                return fail(u"Unknown modifier \"%1\" in \"%2\" (Ctrl, Shift, Alt, Meta)"_s.arg(raw.trimmed(), chord));
        }
        keyName = canonicalKeyName(keyName);
        int key = 0;
        const QList<uint> ucs4 = keyName.toUcs4();
        if (ucs4.size() == 1) {
            key = int(QChar::toUpper(char32_t(ucs4.first())));
        } else {
            const QKeySequence parsed = QKeySequence::fromString(keyName, QKeySequence::PortableText);
            if (parsed.count() != 1 || parsed[0].key() == Qt::Key_unknown
                || parsed[0].keyboardModifiers() != Qt::NoModifier)
                return fail(u"Unknown key \"%1\" in \"%2\""_s.arg(keyName, chord));
            key = int(parsed[0].key());
        }
        // A keyboard's Shift+Tab is Backtab (what the editor's and the shell's keys look for).
        if (key == Qt::Key_Tab && (modifiers & Qt::ShiftModifier))
            key = Qt::Key_Backtab;
        out.append(KeyStroke{key, modifiers, keyText(key, modifiers)});
    }
    return out;
}

QList<KeyStroke> strokesFor(const QString &text)
{
    QList<KeyStroke> out;
    const QList<uint> codes = text.toUcs4();
    for (qsizetype i = 0; i < codes.size(); ++i) {
        const char32_t c = char32_t(codes.at(i));
        const QString chars = QString::fromUcs4(&c, 1);
        if (c == U'\r') {
            if (i + 1 < codes.size() && codes.at(i + 1) == U'\n')
                continue; // "\r\n": one Return
            out.append(KeyStroke{Qt::Key_Return, {}, u"\r"_s});
        } else if (c == U'\n') {
            out.append(KeyStroke{Qt::Key_Return, {}, u"\r"_s});
        } else if (c == U'\t') {
            out.append(KeyStroke{Qt::Key_Tab, {}, u"\t"_s});
        } else if (c == U' ') {
            out.append(KeyStroke{Qt::Key_Space, {}, u" "_s});
        } else if (c > 0xffff) {
            out.append(KeyStroke{0, {}, chars}); // an emoji: text without a key of its own
        } else if (QChar(char16_t(c)).isPrint()) {
            const QChar ch{char16_t(c)};
            // Qt's key code of a character key is the code point of its capital (Key_A … Key_Z
            // for ASCII letters, 0x104 for "ą" and "Ą"); a capital comes with Shift held.
            out.append(
                    KeyStroke{int(ch.toUpper().unicode()), ch.isUpper() ? Qt::ShiftModifier : Qt::NoModifier, chars});
        }
    }
    return out;
}

Gesture keyStrokes(QQuickWindow *window, const QList<KeyStroke> &strokes, KeyDelivery delivery, int pauseMs)
{
    Gesture g;
    for (const KeyStroke &stroke : strokes)
        g.append(Step{[window, stroke, delivery] { deliverKey(window, stroke, delivery); }, pauseMs, true});
    return g;
}

Gesture typeText(QQuickWindow *window, const QString &text, KeyDelivery delivery, int pauseMs, int endMs)
{
    Gesture g = keyStrokes(window, strokesFor(text), delivery, pauseMs);
    g.append(Step{{}, endMs, false});
    return g;
}

// ---- Synchronous helpers ----------------------------------------------------------------------

void touchTapNow(QQuickWindow *window, QPointF scenePos, int holdMs)
{
    run(touchTap(window, scenePos, holdMs, 100));
}

void touchDragNow(QQuickWindow *window, QPointF from, QPointF delta)
{
    run(touchDrag(window, from, delta, 12, 16, 0, 300));
}

void mouseMoveNow(QQuickWindow *window, QPointF scenePos)
{
    run(mouseMove(window, scenePos, 50));
}

void mouseClickNow(QQuickWindow *window, QPointF scenePos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers)
{
    run(mouseClick(window, scenePos, button, modifiers));
}

void mouseDragNow(QQuickWindow *window, QPointF from, QPointF to, int holdMs, bool escape)
{
    run(mouseDrag(window, from, to, holdMs, escape));
}

void tapOrClickNow(QQuickWindow *window, QPointF scenePos, bool touch)
{
    if (touch)
        touchTapNow(window, scenePos);
    else
        mouseClickNow(window, scenePos);
}

} // namespace brulion::uidriver
