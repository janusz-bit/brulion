#include "scrollprobe.h"
#include "rowscroll.h"
#include "touchflick.h"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QMutex>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScreen>
#include <QSet>
#include <QStyleHints>
#include <QTimer>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QtQml/qqml.h>
#include <qtestsupport_gui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

QString valueAfter(const QStringList &args, const QString &option)
{
    const qsizetype i = args.indexOf(option);
    if (i >= 0 && i + 1 < args.size())
        return args.at(i + 1);
    const QString prefix = option + u'=';
    for (const QString &arg : args) {
        if (arg.startsWith(prefix))
            return arg.mid(prefix.size());
    }
    return {};
}

int intAfter(const QStringList &args, const QString &option, int fallback)
{
    bool ok = false;
    const int v = valueAfter(args, option).toInt(&ok);
    return ok ? v : fallback;
}

/// True when the item's QML type (or one it is based on) starts with `prefix` ("BlockDelegate"
/// matches "BlockDelegate_QMLTYPE_12").
bool isA(const QObject *object, const char *prefix)
{
    for (const QMetaObject *m = object->metaObject(); m; m = m->superClass()) {
        if (qstrncmp(m->className(), prefix, qstrlen(prefix)) == 0)
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

bool visibleTree(const QQuickItem *item)
{
    if (item->width() < 1 || item->height() < 1)
        return false;
    for (const QQuickItem *p = item; p; p = p->parentItem()) {
        if (!p->isVisible() || p->opacity() <= 0.01)
            return false;
    }
    return true;
}

void collect(QQuickItem *item, QList<QQuickItem *> &out)
{
    out.append(item);
    const QList<QQuickItem *> children = item->childItems();
    for (QQuickItem *child : children)
        collect(child, out);
}

double num(const QObject *object, const char *name)
{
    return object->property(name).toDouble();
}

/// The least-squares slope dy/dt of (t, y) points (NaN with fewer than 2 or no time between them).
double slope(const QList<QPointF> &points)
{
    if (points.size() < 2)
        return std::numeric_limits<double>::quiet_NaN();
    double mt = 0, my = 0;
    for (const QPointF &p : points) {
        mt += p.x();
        my += p.y();
    }
    mt /= double(points.size());
    my /= double(points.size());
    double num = 0, den = 0;
    for (const QPointF &p : points) {
        num += (p.x() - mt) * (p.y() - my);
        den += (p.x() - mt) * (p.x() - mt);
    }
    return den > 1 ? num / den : std::numeric_limits<double>::quiet_NaN();
}

double percentile(QList<double> values, double p)
{
    if (values.isEmpty())
        return 0;
    std::sort(values.begin(), values.end());
    const auto i = qsizetype(std::clamp(p * double(values.size() - 1), 0.0, double(values.size() - 1)));
    return values.at(i);
}

/// The thumb of the view's BRowScrollBar (RowScroll) jumped between two frames of a scroll: it
/// moved against the content (by more than 1 px of the view's height) or by more than 3 px while
/// the content stood still. Only while the view moves in both frames (the thumb's size, which
/// changes only when it stops, is the same) and away from its ends.
template<typename F>
bool rowThumbJump(const F &prev, const F &f)
{
    if (std::isnan(f.rowThumb) || std::isnan(prev.rowThumb) || !f.moving || !prev.moving || f.atBound || prev.atBound
        || f.rowSize != prev.rowSize)
        return false;
    const double thumb = (f.rowThumb - prev.rowThumb) * f.height;
    const double dy = f.contentY - prev.contentY;
    if (std::abs(dy) <= 0.5)
        return std::abs(thumb) > 3;
    return dy > 0 ? thumb < -1 : thumb > 1;
}

struct DelegateState {
    int index = -1;
    double y = 0;
    double height = 0;
};

struct Frame {
    int n = 0;
    double t = 0;        // ms since the first stroke
    double interval = 0; // ms since the previous frame
    double gap = 0;      // ms of GUI-thread work between the previous frame and this one
    double polish = 0;   // ms from the frame's update request to afterAnimating (polish)
    double gui = 0;      // ms the previous frame's update request took on the GUI thread
    double contentY = 0, originY = 0, contentHeight = 0, height = 0;
    double visPos = 0, visRatio = 0;
    double rowThumb = kNaN, rowSize = kNaN; // the view's BRowScrollBar (position, size), if it has one
    bool moving = false, dragging = false, flicking = false, atBound = false;
    double velocity = 0;
    int inputEvents = 0;
    double inputScroll = 0; // the scroll the input asked for (contentY += inputScroll)
    double dyInput = 0;     // contentY changes while input was delivered
    double dyFrame = 0;     // … during frames (animations, polish)
    double dyAnim = 0;      // … from timers while the view flicks (animation driver)
    double dyOther = 0;     // … anywhere else (timers, queued calls, model signals)
    int delegates = 0, created = 0, destroyed = 0;
    int blocks = 0, blocksCreated = 0;
    double anchorShift = kNaN;  // the anchor's movement inside the content (layout shift)
    double anchorScreen = kNaN; // the anchor's movement on screen
    double unexplained = kNaN;  // screen movement the input / the running animation does not explain
    double held = kNaN;         // direct input that did not move the content (drag threshold, an end)
    bool jump = false;
    QString phase;
    QString events;
    QString layout; // the delegates (row@y+height, by y) when they changed
    double probeMs = 0;
    // Real touch input (observe): what arrived since the previous frame.
    int touchMoves = 0;     // touch moves
    double touchDy = 0;     // the finger's movement (scene px, + = down)
    double touchLag = kNaN; // ms from the newest move's arrival to this frame's start
    bool pressed = false;   // a finger is down
};

/// One finger stroke of real input (observe): press, moves, release, and the fling after it.
struct Stroke {
    double tDown = 0, tUp = kNaN;
    double yDown = 0, yUp = 0;
    int moves = 0;
    double releaseSpeed = kNaN; // the finger's speed over the last ~80 ms before the release (px/s)
    double velocity = kNaN;     // the view's verticalVelocity in the first frame after the release
    double contentYUp = kNaN, contentYStop = kNaN, tStop = kNaN;
    bool caught = false;                                  // a new press stopped the fling
    bool atBound = false;                                 // the fling ended at an end of the view
    int dragFrames = 0, stillFrames = 0, multiFrames = 0; // frames while dragging: all, no move, 2+ moves
    QList<QPointF> recent;                                // (event timestamp, y) of the newest moves
};

enum class Context { Other, Input, Frame };

class ScrollProbe : public QObject {
    Q_OBJECT

public:
    ScrollProbe(QQmlApplicationEngine *engine, const QStringList &args) : QObject(engine), m_engine(engine)
    {
        m_targetSpec = valueAfter(args, u"--scroll-probe"_s);
        if (m_targetSpec.isEmpty() || m_targetSpec.startsWith(u"--"_s))
            m_targetSpec = u"auto"_s;
        m_script = valueAfter(args, u"--scroll-script"_s);
        if (m_script.isEmpty())
            m_script = u"drag"_s;
        m_outFile = valueAfter(args, u"--probe-out"_s);
        m_passes = std::max(1, intAfter(args, u"--probe-passes"_s, 1));
        m_waitMs = std::max(0, intAfter(args, u"--probe-wait"_s, 1500));
        m_maxMs = std::max(1000, intAfter(args, u"--probe-max"_s, 60000));
        m_speed = std::max(50, intAfter(args, u"--probe-speed"_s, 1500));
        m_checkTolerance = valueAfter(args, u"--probe-check"_s).toDouble();
        m_cache = intAfter(args, u"--probe-cache"_s, -1);
        m_observe = m_script == u"observe";
        m_direct = m_script == u"drag" || m_script == u"touchpad";
        m_input.setTimerType(Qt::PreciseTimer);
        m_input.setInterval(8); // a touch screen's / touchpad's report rate (~120 Hz)
        connect(&m_input, &QTimer::timeout, this, &ScrollProbe::tick);
        m_clock.start();
        QTimer::singleShot(200, this, &ScrollProbe::waitForTarget);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched != m_window)
            return QObject::eventFilter(watched, event);
        if (!m_observe && isInput(event->type()) && m_target) {
            // Synthetic input (deliver()): the same touch statistics as for real input.
            recordInput(event, false);
            return QObject::eventFilter(watched, event);
        }
        if (m_observe && isInput(event->type())) {
            // Real input: deliver it here to attribute the scroll it causes (touch moves only
            // queue: Qt Quick delivers the newest one with the next frame).
            recordInput(event, true);
            const Context outer = m_context;
            m_context = Context::Input;
            static_cast<QObject *>(m_window.data())->event(event);
            m_context = outer;
            return true;
        }
        if (event->type() != QEvent::UpdateRequest)
            return QObject::eventFilter(watched, event);
        // Handle the frame here to time it as a whole (polish, sync, animations, incubation).
        m_frameBegin = now();
        m_gapMs = m_lastFrameEnd > 0 ? m_frameBegin - m_lastFrameEnd : 0;
        const Context outer = m_context;
        m_context = Context::Frame;
        static_cast<QObject *>(m_window.data())->event(event);
        m_context = outer;
        m_lastFrameEnd = now();
        m_lastGuiMs = m_lastFrameEnd - m_frameBegin;
        return true;
    }

private Q_SLOTS:
    void onContentYChanged()
    {
        if (!m_target)
            return;
        const double y = num(m_target, "contentY");
        const double dy = y - m_lastContentY;
        m_lastContentY = y;
        switch (m_context) {
        case Context::Input: m_dyInput += dy; break;
        case Context::Frame: m_dyFrame += dy; break;
        case Context::Other:
            // Timer-driven animations (fling, wheel, return to bounds) run outside the frames.
            if (m_target->property("moving").toBool() && !m_target->property("dragging").toBool())
                m_dyAnim += dy;
            else
                m_dyOther += dy;
            break;
        }
    }

private:
    double now() const { return double(m_clock.nsecsElapsed()) / 1e6; }
    void log(const QString &text) const
    {
        std::fprintf(stderr, "scroll-probe: %s\n", qPrintable(text));
        m_logLines.append(text);
        // Android: stderr goes nowhere, logcat shows qInfo.
        if (QGuiApplication::platformName() == u"android")
            qInfo().noquote() << "scroll-probe:" << text;
    }

    static bool isInput(QEvent::Type type)
    {
        switch (type) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
        case QEvent::MouseButtonPress:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease:
        case QEvent::Wheel: return true;
        default: return false;
        }
    }

    /// Observe: count real input, follow the finger, and keep the strokes.
    void recordInput(const QEvent *event, bool count)
    {
        const double t = now();
        if (count)
            ++m_inputEvents;
        const QEvent::Type type = event->type();
        if (type != QEvent::TouchBegin && type != QEvent::TouchUpdate && type != QEvent::TouchEnd
            && type != QEvent::TouchCancel)
            return;
        const auto *touch = static_cast<const QTouchEvent *>(event);
        if (touch->points().isEmpty())
            return;
        const double y = touch->points().constFirst().scenePosition().y();
        // The finger's speed from the events' own timestamps (the platform's: Android's sample
        // times), not from when they arrived here.
        const auto te = double(touch->timestamp());
        if (type == QEvent::TouchBegin) {
            closeFling(t, true);
            Stroke s;
            s.tDown = t;
            s.yDown = y;
            s.recent = {QPointF(te, y)};
            m_strokes.append(s);
            m_fingerY = y;
            m_pressed = true;
            return;
        }
        m_touchDy += y - m_fingerY;
        m_fingerY = y;
        if (type == QEvent::TouchUpdate) {
            ++m_touchMoves;
            m_lastMoveAt = t;
        }
        if (m_strokes.isEmpty())
            return;
        Stroke &s = m_strokes.last();
        if (type == QEvent::TouchUpdate)
            ++s.moves;
        // A release at the position of the last move adds nothing (Android reports one).
        if (type == QEvent::TouchUpdate || s.recent.isEmpty() || std::abs(s.recent.constLast().y() - y) > 0.01)
            s.recent.append(QPointF(te, y));
        while (s.recent.size() > 3 && te - s.recent.constFirst().x() > 80)
            s.recent.removeFirst();
        if (type == QEvent::TouchEnd || type == QEvent::TouchCancel) {
            s.tUp = t;
            s.yUp = y;
            s.releaseSpeed = slope(s.recent) * 1000;
            s.contentYUp = m_target ? num(m_target, "contentY") : kNaN;
            m_pressed = false;
        }
    }

    /// The fling after the newest stroke's release ended (or a new stroke stopped it).
    void closeFling(double t, bool byPress = false)
    {
        if (m_strokes.isEmpty())
            return;
        Stroke &s = m_strokes.last();
        if (!std::isnan(s.tUp) && std::isnan(s.tStop)) {
            s.tStop = t;
            s.contentYStop = m_target ? num(m_target, "contentY") : kNaN;
            s.caught = byPress && m_target && m_target->property("moving").toBool();
            s.atBound =
                    m_target && (m_target->property("atYBeginning").toBool() || m_target->property("atYEnd").toBool());
        }
    }

    // ---- The target -----------------------------------------------------------------------------
    QQuickItem *findTarget() const
    {
        if (!m_window)
            return nullptr;
        QList<QQuickItem *> all;
        collect(m_window->contentItem(), all);
        QQuickItem *best = nullptr;
        double bestArea = 0;
        const QString spec = m_targetSpec;
        for (QQuickItem *item : std::as_const(all)) {
            if (!item->inherits("QQuickFlickable") || !visibleTree(item))
                continue;
            if (spec.startsWith(u"name:"_s)) {
                if (item->objectName() == spec.mid(5))
                    return item;
                continue;
            }
            if (!item->property("interactive").toBool())
                continue;
            if (spec == u"sidebar" && !hasAncestor(item, "ShellSidebar"))
                continue;
            if (spec.startsWith(u"type:"_s) && !hasAncestor(item, spec.mid(5).toLatin1().constData()))
                continue;
            if (spec == u"auto"
                && (hasAncestor(item, "ShellSidebar") || hasAncestor(item, "QQuickOverlay")
                    || hasAncestor(item, "ShellSidePanel")))
                continue;
            // Only vertical scrollers.
            if (item->property("contentHeight").toDouble() <= item->height() + 1
                && item->property("flickableDirection").toInt() == 1 /* HorizontalFlick */)
                continue;
            const double area = item->width() * item->height();
            if (area > bestArea) {
                bestArea = area;
                best = item;
            }
        }
        return best;
    }

    void waitForTarget()
    {
        if (!m_window) {
            const QList<QObject *> roots = m_engine->rootObjects();
            for (QObject *root : roots) {
                if (auto *w = qobject_cast<QQuickWindow *>(root)) {
                    m_window = w;
                    break;
                }
            }
        }
        QQuickItem *target = findTarget();
        const bool scrollable = target && num(target, "contentHeight") > target->height() + 1;
        if (!scrollable) {
            if (m_clock.elapsed() > (m_observe ? std::max(20000, m_maxMs) : 20000)) {
                log(target ? u"the target does not scroll (content %1 px, view %2 px)"_s
                                     .arg(num(target, "contentHeight"))
                                     .arg(target->height())
                           : u"no target Flickable for '%1'"_s.arg(m_targetSpec));
                QCoreApplication::exit(2);
                return;
            }
            QTimer::singleShot(100, this, &ScrollProbe::waitForTarget);
            return;
        }
        m_target = target;
        QTimer::singleShot(m_waitMs, this, &ScrollProbe::begin);
    }

    void begin()
    {
        if (!m_target || !m_window) {
            log(u"the target went away"_s);
            QCoreApplication::exit(2);
            return;
        }
        if (m_observe) {
            // The view shown now (the user may have navigated while the probe waited).
            if (QQuickItem *shown = findTarget())
                m_target = shown;
        }
        attachTarget();
        if (m_cache >= 0 && m_target->property("cacheBuffer").isValid()) {
            // --probe-cache: every row created at the edge of the view (0), as on a phone too slow
            // for the cache to keep ahead — a row growing after the list placed it shows every time.
            m_target->setProperty("cacheBuffer", m_cache);
        }
        m_window->installEventFilter(this);
        connect(m_window, &QQuickWindow::afterAnimating, this, &ScrollProbe::onFrame, Qt::DirectConnection);
        connect(
                m_window, &QQuickWindow::frameSwapped, this,
                [this] {
                    const QMutexLocker lock(&m_swapMutex);
                    m_swaps.append(now());
                },
                Qt::DirectConnection);
        const QSGRendererInterface *ri = m_window->rendererInterface();
        const QString api = ri ? QString::number(int(ri->graphicsApi())) : u"?"_s;
        log(u"target %1 '%2' in %3, %4x%5 at %6,%7; window %8x%9 dpr %10; QPA %11; graphics api %12; render loop %13; "
            u"screen %14 Hz; script %15, %16 pass(es), speed %17 px/s"_s
                    .arg(QString::fromLatin1(m_target->metaObject()->className()), m_target->objectName(),
                         parentChain(m_target))
                    .arg(m_target->width())
                    .arg(m_target->height())
                    .arg(m_target->mapToScene(QPointF(0, 0)).x())
                    .arg(m_target->mapToScene(QPointF(0, 0)).y())
                    .arg(m_window->width())
                    .arg(m_window->height())
                    .arg(m_window->devicePixelRatio())
                    .arg(QGuiApplication::platformName(), api,
                         qEnvironmentVariableIsSet("QSG_RENDER_LOOP") ? qEnvironmentVariable("QSG_RENDER_LOOP")
                                                                      : u"default"_s)
                    .arg(m_window->screen() ? m_window->screen()->refreshRate() : 0)
                    .arg(m_script)
                    .arg(m_passes)
                    .arg(m_speed));
        logTuning();
        logInventory();
        m_startMs = now();
        if (m_observe) {
            log(u"observing real input for %1 ms"_s.arg(m_maxMs));
            QTimer::singleShot(m_maxMs, this, &ScrollProbe::finish);
            m_window->update();
            return;
        }
        m_phaseIndex = 0;
        startPhase();
        m_input.start();
        m_window->update();
    }

    /// Follow the target's scrolling (begin(), and observe's retargeting after navigation).
    void attachTarget()
    {
        m_contentItem = m_target->property("contentItem").value<QQuickItem *>();
        m_hasItemAtIndex = m_target->metaObject()->indexOfMethod("itemAtIndex(int)") >= 0;
        m_visibleArea = m_target->property("visibleArea").value<QObject *>();
        // A BRowScrollBar over the view (RowScroll, rowscroll.h): its thumb is checked too.
        m_rowScroll = nullptr;
        const QList<QQuickItem *> children = m_target->childItems();
        for (QQuickItem *child : children) {
            if (child->objectName() == u"rowScrollBar"_s)
                m_rowScroll = child->findChild<brulion::ui::RowScroll *>();
        }
        m_lastContentY = num(m_target, "contentY");
        connect(m_target.data(), SIGNAL(contentYChanged()), this, SLOT(onContentYChanged()));
        m_delegates.clear();
        m_blocks.clear();
        m_anchor = nullptr;
    }

    /// Observe: the view went away (navigation) — follow the one shown now.
    bool retarget()
    {
        if (m_target && visibleTree(m_target))
            return true;
        QQuickItem *next = findTarget();
        if (!next || next == m_target)
            return m_target && visibleTree(m_target);
        if (m_target)
            disconnect(m_target.data(), SIGNAL(contentYChanged()), this, SLOT(onContentYChanged()));
        m_target = next;
        attachTarget();
        log(u"t=%1 retarget %2 '%3' in %4, %5x%6, content %7 px"_s.arg(now() - m_startMs, 0, 'f', 0)
                    .arg(QString::fromLatin1(next->metaObject()->className()), next->objectName(), parentChain(next))
                    .arg(next->width())
                    .arg(next->height())
                    .arg(num(next, "contentHeight")));
        return true;
    }

    /// The scrolling physics in effect: Flickable's, the style hints', the screen's.
    void logTuning() const
    {
        const QStyleHints *hints = QGuiApplication::styleHints();
        const QScreen *screen = m_window->screen();
        log(u"tuning: flickDeceleration %1, maximumFlickVelocity %2, pressDelay %3, cacheBuffer %4, "
            u"boundsBehavior %5, synchronousDrag %6; startDragDistance %7, wheelScrollLines %8, "
            u"mouseDoubleClickDistance %9; screen %10x%11 logical, %12 dpi physical, %13 Hz"_s
                    .arg(num(m_target, "flickDeceleration"))
                    .arg(num(m_target, "maximumFlickVelocity"))
                    .arg(num(m_target, "pressDelay"))
                    .arg(m_target->property("cacheBuffer").isValid() ? num(m_target, "cacheBuffer") : -1)
                    .arg(m_target->property("boundsBehavior").toInt())
                    .arg(m_target->property("synchronousDrag").toBool())
                    .arg(hints->startDragDistance())
                    .arg(hints->wheelScrollLines())
                    .arg(hints->mouseDoubleClickDistance())
                    .arg(screen ? screen->size().width() : 0)
                    .arg(screen ? screen->size().height() : 0)
                    .arg(screen ? screen->physicalDotsPerInch() : 0, 0, 'f', 0)
                    .arg(screen ? screen->refreshRate() : 0, 0, 'f', 1));
    }

    /// What the window holds: items, blocks, icons (an IconImage without a source looks its
    /// empty name up in the icon theme, on the file system), menus — per block.
    void logInventory() const
    {
        QList<QQuickItem *> all;
        collect(m_window->contentItem(), all);
        int blocks = 0, icons = 0, emptyIcons = 0, inTarget = 0;
        QStringList overlay; // Qt Quick Controls' overlay: while it is visible (a popup is open), it
                             // hit-tests every wheel event against the whole window first
        for (const QQuickItem *item : std::as_const(all)) {
            if (item->inherits("QQuickOverlay")) {
                overlay.append(item->isVisible() ? u"visible"_s : u"hidden"_s);
                const QList<QQuickItem *> popups = item->childItems();
                for (const QQuickItem *p : popups) {
                    if (p->isVisible())
                        overlay.append(QString::fromLatin1(p->parent() ? p->parent()->metaObject()->className()
                                                                       : p->metaObject()->className()));
                }
            }
            if (isA(item, "BlockDelegate"))
                ++blocks;
            if (item->inherits("QQuickIconImage")) {
                ++icons;
                if (item->property("source").toUrl().isEmpty() && item->property("name").toString().isEmpty())
                    ++emptyIcons;
            }
            if (m_contentItem && m_contentItem->isAncestorOf(item))
                ++inTarget;
        }
        // Popups (menus) are QObjects owned by the items that declare them; their content is
        // not in the window's item tree until they open.
        int menus = 0, menuItems = 0, menuIcons = 0, menuEmptyIcons = 0, menuObjects = 0;
        QSet<const QObject *> seen;
        for (const QQuickItem *item : std::as_const(all)) {
            const QList<QObject *> owned = item->findChildren<QObject *>(Qt::FindDirectChildrenOnly);
            for (const QObject *o : owned) {
                if (!o->inherits("QQuickMenu") || seen.contains(o))
                    continue;
                seen.insert(o);
                ++menus;
                const QList<QObject *> inside = o->findChildren<QObject *>();
                menuObjects += int(inside.size());
                for (const QObject *c : inside) {
                    if (c->inherits("QQuickMenuItem"))
                        ++menuItems;
                    if (c->inherits("QQuickIconImage")) {
                        ++menuIcons;
                        if (c->property("source").toUrl().isEmpty() && c->property("name").toString().isEmpty())
                            ++menuEmptyIcons;
                    }
                }
            }
        }
        log(u"inventory: overlay %1"_s.arg(overlay.join(u' ')));
        log(u"inventory: %1 items (%2 in the target), %3 BlockDelegates, %4 IconImages (%5 without source or name); "
            u"%6 menus holding %7 objects, %8 menu items, %9 IconImages (%10 without source or name)"_s.arg(all.size())
                    .arg(inTarget)
                    .arg(blocks)
                    .arg(icons)
                    .arg(emptyIcons)
                    .arg(menus)
                    .arg(menuObjects)
                    .arg(menuItems)
                    .arg(menuIcons)
                    .arg(menuEmptyIcons));
    }

    QString parentChain(const QQuickItem *item) const
    {
        QStringList names;
        for (const QQuickItem *p = item->parentItem(); p && names.size() < 12; p = p->parentItem()) {
            const QString cls = QString::fromLatin1(p->metaObject()->className());
            if (cls.contains(u"_QML"_s))
                names.append(cls.section(u'_', 0, 0));
        }
        return names.join(u'<');
    }

    // ---- Input ----------------------------------------------------------------------------------
    struct TouchSequence : QTest::QTouchEventSequence {
        TouchSequence(QWindow *w, QPointingDevice *d) : QTest::QTouchEventSequence(w, d, false) { }
    };
    QPointingDevice *touchDevice()
    {
        static QPointingDevice *device = QTest::createTouchDevice();
        return device;
    }

    /// Scene point of the strokes: the target's centre column, `fy` down its height.
    QPointF strokePoint(double fy) const
    {
        return m_target->mapToScene(QPointF(m_target->width() * 0.5, m_target->height() * fy));
    }
    bool down() const { return m_phaseIndex % 2 == 0; }
    QString phaseName() const
    {
        if (m_phaseIndex >= 2 * m_passes)
            return u"end"_s;
        return (down() ? u"down"_s : u"up"_s) + QString::number(m_phaseIndex / 2 + 1);
    }

    void startPhase()
    {
        m_phaseStart = now();
        m_stroke = 0;
        m_strokeStart = -1;
        m_restUntil = now() + 300;
        log(u"phase %1 at contentY %2"_s.arg(phaseName()).arg(num(m_target, "contentY")));
    }

    bool atPhaseEnd() const
    {
        const bool moving = m_target->property("moving").toBool();
        return !moving && m_target->property(down() ? "atYEnd" : "atYBeginning").toBool();
    }

    void deliver(const std::function<void()> &send, double scroll)
    {
        const Context outer = m_context;
        m_context = Context::Input;
        send();
        m_context = outer;
        ++m_inputEvents;
        m_inputScroll += scroll;
    }

    void touch(int type, QPointF p, double scroll)
    {
        deliver(
                [&] {
                    TouchSequence seq(m_window, touchDevice());
                    if (type == 0)
                        seq.press(0, p.toPoint(), m_window);
                    else if (type == 1)
                        seq.move(0, p.toPoint(), m_window);
                    else
                        seq.release(0, p.toPoint(), m_window);
                    seq.commit(false);
                },
                scroll);
    }

    void wheel(QPointF p, QPoint pixel, QPoint angle, Qt::ScrollPhase phase, Qt::MouseEventSource source, double scroll)
    {
        deliver(
                [&] {
                    QWheelEvent ev(p, m_window->mapToGlobal(p), pixel, angle, Qt::NoButton, Qt::NoModifier, phase,
                                   false, source);
                    ev.setTimestamp(ulong(m_clock.elapsed()));
                    QCoreApplication::sendEvent(m_window, &ev);
                },
                scroll);
    }

    /// One input step (every 8 ms): the current stroke of the script, then a pause, until the
    /// view reached the end of this direction.
    void tick()
    {
        if (!m_target || !m_window) {
            finish();
            return;
        }
        const double t = now();
        if (m_strokeStart < 0) {
            if (t < m_restUntil)
                return;
            if (atPhaseEnd() || t - m_phaseStart > m_maxMs) {
                if (!atPhaseEnd())
                    log(u"phase %1: time limit"_s.arg(phaseName()));
                ++m_phaseIndex;
                if (m_phaseIndex >= 2 * m_passes) {
                    m_input.stop();
                    QTimer::singleShot(600, this, &ScrollProbe::finish);
                    return;
                }
                startPhase();
                return;
            }
            if (m_target->property("moving").toBool() && t - m_strokeEnd < 3000)
                return; // a fling still running: the next stroke when it ends
            m_strokeStart = t;
            m_strokeStep = 0;
            m_lastFinger = 0;
            ++m_stroke;
        }
        const double sign = down() ? 1 : -1; // + scrolls down (content moves up)
        const double elapsed = t - m_strokeStart;
        const double h = m_target->height();
        if (m_script == u"drag" || m_script == u"flick") {
            const bool flick = m_script == u"flick";
            const double distance = h * (flick ? 0.4 : 0.55);
            const double duration = flick ? 100 : distance / m_speed * 1000;
            const double fy0 = down() ? 0.78 : 0.22;
            const QPointF start = strokePoint(fy0);
            if (m_strokeStep == 0) {
                touch(0, start, 0);
                m_strokeStep = 1;
                return;
            }
            const double moved = std::min(1.0, elapsed / duration) * distance;
            if (m_strokeStep == 1) {
                if (moved > m_lastFinger + 0.5) {
                    touch(1, start - QPointF(0, sign * moved), sign * (moved - m_lastFinger));
                    m_lastFinger = moved;
                }
                if (elapsed >= duration)
                    m_strokeStep = 2;
                return;
            }
            // Drag: hold the finger still before lifting it (no fling); flick: lift at once.
            if (!flick && elapsed < duration + 120)
                return;
            touch(2, start - QPointF(0, sign * distance), 0);
            endStroke(flick ? 100 : 150);
            return;
        }
        if (m_script == u"wheel") {
            // Notches of a mouse wheel, 50 ms apart, 6 per stroke.
            if (elapsed >= m_strokeStep * 50.0) {
                const int step = std::lround(QGuiApplication::styleHints()->wheelScrollLines() * 24.0);
                wheel(strokePoint(0.5), QPoint(), QPoint(0, int(-sign * 120)), Qt::NoScrollPhase,
                      Qt::MouseEventNotSynthesized, sign * step);
                if (++m_strokeStep >= 6)
                    endStroke(150);
            }
            return;
        }
        if (m_script == u"touchpad") {
            // Two-finger scrolling: pixel deltas at the touchpad's rate for 500 ms, with phases.
            const QPointF p = strokePoint(0.5);
            if (m_strokeStep == 0) {
                wheel(p, QPoint(), QPoint(), Qt::ScrollBegin, Qt::MouseEventSynthesizedBySystem, 0);
                m_strokeStep = 1;
                m_lastFinger = 0;
                return;
            }
            const double target = std::min(500.0, elapsed) * m_speed / 1000.0;
            const int dy = int(std::lround(target - m_lastFinger));
            if (dy != 0) {
                wheel(p, QPoint(0, int(-sign * dy)), QPoint(0, int(-sign * dy)), Qt::ScrollUpdate,
                      Qt::MouseEventSynthesizedBySystem, sign * dy);
                m_lastFinger += dy;
            }
            if (elapsed >= 500) {
                wheel(p, QPoint(), QPoint(), Qt::ScrollEnd, Qt::MouseEventSynthesizedBySystem, 0);
                endStroke(150);
            }
            return;
        }
        log(u"unknown script '%1'"_s.arg(m_script));
        QCoreApplication::exit(2);
    }

    void endStroke(double restMs)
    {
        m_strokeStart = -1;
        m_strokeEnd = now();
        m_restUntil = m_strokeEnd + restMs;
    }

    // ---- Frames ---------------------------------------------------------------------------------
    /// The deepest visible item at the viewport's centre (in the target's content).
    /// The child of the content item (the view's delegate) that holds `item`.
    QQuickItem *delegateOf(QQuickItem *item) const
    {
        while (item && item->parentItem() != m_contentItem)
            item = item->parentItem();
        return item;
    }

    /// The view no longer has the anchor's row: the delegate was released, or reused for another
    /// row (reuseItems).
    bool anchorRowGone() const
    {
        QQuickItem *d = m_anchorDelegate.data();
        if (!d)
            return true;
        if (m_anchorRow < 0)
            return false; // not one of the view's rows (no index)
        const QVariant row = d->property("index");
        return !row.isValid() || row.toInt() != m_anchorRow || !placed(d, m_anchorRow);
    }

    QQuickItem *anchorAt(QPointF targetPoint) const
    {
        QQuickItem *item = m_contentItem;
        QQuickItem *found = nullptr;
        while (item) {
            QList<QQuickItem *> children = item->childItems();
            std::stable_sort(children.begin(), children.end(),
                             [](const QQuickItem *a, const QQuickItem *b) { return a->z() < b->z(); });
            QQuickItem *next = nullptr;
            for (auto it = children.crbegin(); it != children.crend(); ++it) {
                QQuickItem *c = *it;
                if (!c->isVisible() || c->height() < 1 || c->width() < 1)
                    continue;
                if (item == m_contentItem) {
                    // Only rows the view has placed (not one still incubating, or kept for reuse).
                    const QVariant row = c->property("index");
                    if (row.isValid() && !placed(c, row.toInt()))
                        continue;
                }
                if (c->contains(c->mapFromItem(m_target, targetPoint))) {
                    next = c;
                    break;
                }
            }
            if (next)
                found = next;
            item = next;
        }
        return found;
    }

    /// Whether the target view has placed `item` as its row `index` (ListView / GridView:
    /// itemAtIndex() knows only the rows it has laid out); other Flickables: always.
    bool placed(const QQuickItem *item, int index) const
    {
        if (!m_hasItemAtIndex)
            return true;
        QQuickItem *row = nullptr;
        QMetaObject::invokeMethod(m_target.data(), "itemAtIndex", Qt::DirectConnection, qReturnArg(row), index);
        return row == item;
    }

    /// "(query)", "(text+embeds)", "(text+transclusions)": a block delegate's kind (BlockDelegate's
    /// view, and whether it shows pictures or embedded pages / blocks); "" for other delegates.
    static QString blockKind(const QQuickItem *item)
    {
        const QString view = item->property("view").toString();
        if (view.isEmpty())
            return {};
        QString kind = view;
        if (item->property("hasEmbeds").toBool())
            kind += u"+embeds"_s;
        if (item->property("hasTransclusions").toBool())
            kind += u"+transclusions"_s;
        return u'(' + kind + u')';
    }

    void countBlocks(QQuickItem *item, QSet<const QQuickItem *> &out) const
    {
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children) {
            if (isA(child, "BlockDelegate")) {
                out.insert(child);
                continue;
            }
            countBlocks(child, out);
        }
    }

    void onFrame()
    {
        if (m_observe && !retarget())
            return;
        if (!m_target || !m_contentItem)
            return;
        const double t0 = now();
        Frame f;
        f.n = int(m_frames.size());
        f.t = t0 - m_startMs;
        f.interval = m_lastFrameAt > 0 ? t0 - m_lastFrameAt : 0;
        m_lastFrameAt = t0;
        f.gap = m_gapMs;
        f.polish = m_frameBegin > 0 ? t0 - m_frameBegin : 0;
        f.gui = m_lastGuiMs;
        f.contentY = num(m_target, "contentY");
        f.originY = num(m_target, "originY");
        f.contentHeight = num(m_target, "contentHeight");
        f.height = m_target->height();
        if (m_visibleArea) {
            f.visPos = num(m_visibleArea, "yPosition");
            f.visRatio = num(m_visibleArea, "heightRatio");
        }
        if (m_rowScroll) {
            // Now, not when its own refresh comes (this frame, before or after this handler).
            m_rowScroll->refresh();
            f.rowThumb = m_rowScroll->position();
            f.rowSize = m_rowScroll->size();
        }
        f.moving = m_target->property("moving").toBool();
        f.dragging = m_target->property("dragging").toBool();
        f.flicking = m_target->property("flicking").toBool();
        f.atBound = m_target->property("atYBeginning").toBool() || m_target->property("atYEnd").toBool();
        f.velocity = num(m_target, "verticalVelocity");
        f.inputEvents = m_inputEvents;
        // Observe: the content follows the finger (finger up = contentY grows).
        f.inputScroll = m_observe ? -m_touchDy : m_inputScroll;
        f.touchMoves = m_touchMoves;
        f.touchDy = m_touchDy;
        f.touchLag = m_touchMoves > 0 ? m_frameBegin - m_lastMoveAt : kNaN;
        f.pressed = m_pressed;
        m_touchMoves = 0;
        m_touchDy = 0;
        f.dyInput = m_dyInput;
        f.dyFrame = m_dyFrame;
        f.dyAnim = m_dyAnim;
        f.dyOther = m_dyOther;
        m_inputEvents = 0;
        m_inputScroll = 0;
        m_dyInput = m_dyFrame = m_dyAnim = m_dyOther = 0;
        f.phase = m_observe ? u"observe"_s : m_phaseIndex < 2 * m_passes ? phaseName() : u"end"_s;

        // Delegates of the target: created, destroyed, resized (and where: above / in / below).
        const auto *header = m_target->property("headerItem").value<QQuickItem *>();
        const auto *footer = m_target->property("footerItem").value<QQuickItem *>();
        QHash<const QQuickItem *, DelegateState> current;
        QStringList events;
        const Frame *prev = m_frames.isEmpty() ? nullptr : &m_frames.constLast();
        const double viewTop = prev ? prev->contentY : f.contentY;
        const double viewBottom = viewTop + f.height;
        QStringList moves; // delegates whose y changed (in content coordinates)
        double topmostY = std::numeric_limits<double>::max();
        const QQuickItem *topmost = nullptr;
        const QList<QQuickItem *> children = m_contentItem->childItems();
        for (const QQuickItem *child : children) {
            if (child == header || child == footer || !child->isVisible())
                continue;
            // Delegates with a `required property int index` say which row they are.
            const QVariant index = child->property("index");
            // A delegate the view has not placed yet (still incubating, or complete and waiting for
            // the view's next refill: at y 0, not drawn) is not counted until it is placed.
            if (index.isValid() && !placed(child, index.toInt()))
                continue;
            DelegateState s{index.isValid() ? index.toInt() : -1, child->y(), child->height()};
            current.insert(child, s);
            const auto it = m_delegates.constFind(child);
            if (it == m_delegates.constEnd()) {
                ++f.created;
                events.append(u"+%1:%2%3"_s.arg(s.index).arg(s.height, 0, 'f', 0).arg(blockKind(child)));
            } else {
                if (std::abs(s.y - it->y) > 0.5)
                    moves.append(u"m%1:%2"_s.arg(s.index).arg(s.y - it->y, 0, 'f', 0));
                if (it->y < topmostY) {
                    topmostY = it->y;
                    topmost = child;
                }
                if (std::abs(it->height - s.height) > 0.5) {
                    const bool above = it->y + it->height <= viewTop + 0.5;
                    const bool below = it->y >= viewBottom - 0.5;
                    // Blocks (BlockDelegate.view): which kind of block changed its height.
                    events.append(u"r%1:%2%3%4"_s.arg(s.index)
                                          .arg(s.height - it->height, 0, 'f', 0)
                                          .arg(above   ? u"a"_s
                                               : below ? u"b"_s
                                                       : u"v"_s)
                                          .arg(blockKind(child)));
                }
            }
        }
        if (topmost) {
            // Mark the topmost delegate's resize: ListView keeps the content in place only for that one.
            const int idx = current.value(topmost).index;
            for (QString &e : events) {
                if (e.startsWith(u"r%1:"_s.arg(idx)))
                    e.append(u'F');
            }
        }
        for (auto it = m_delegates.constBegin(); it != m_delegates.constEnd(); ++it) {
            if (!current.contains(it.key())) {
                ++f.destroyed;
                events.append(u"-%1"_s.arg(it->index));
            }
        }
        m_delegates = current;
        f.delegates = int(current.size());
        const bool layoutChanged =
                !moves.isEmpty() || f.created > 0 || f.destroyed > 0
                || std::any_of(events.cbegin(), events.cend(), [](const QString &e) { return e.startsWith(u'r'); });
        if (moves.size() > 8)
            moves = moves.mid(0, 8) << u"m…(%1)"_s.arg(moves.size());
        events += moves;
        if (layoutChanged && current.size() <= 40) {
            QList<DelegateState> order = current.values();
            std::sort(order.begin(), order.end(),
                      [](const DelegateState &a, const DelegateState &b) { return a.y < b.y; });
            QStringList parts;
            for (const DelegateState &d : std::as_const(order))
                parts.append(u"%1@%2+%3"_s.arg(d.index).arg(d.y, 0, 'f', 0).arg(d.height, 0, 'f', 0));
            f.layout = parts.join(u' ');
        }
        if (prev) {
            if (std::abs(f.originY - prev->originY) > 0.5)
                events.append(u"O%1"_s.arg(f.originY - prev->originY, 0, 'f', 0));
            if (std::abs(f.contentHeight - prev->contentHeight) > 0.5)
                events.append(u"H%1"_s.arg(f.contentHeight - prev->contentHeight, 0, 'f', 0));
        }

        // Nested blocks (the journal's days, or the page's own delegates).
        QSet<const QQuickItem *> blocks;
        countBlocks(m_contentItem, blocks);
        f.blocks = int(blocks.size());
        for (const QQuickItem *b : std::as_const(blocks)) {
            if (!m_blocks.contains(b))
                ++f.blocksCreated;
        }
        m_blocks = blocks;

        // The anchor: what the user looks at. Its movement on screen against the scroll asked for.
        const QPointF centre(m_target->width() * 0.5, m_target->height() * 0.5);
        if (m_anchor && !anchorRowGone() && delegateOf(m_anchor.data()) != m_anchorDelegate.data()) {
            // The anchor left its row (the row's content was rebuilt): only a new anchor.
            m_anchor = nullptr;
        }
        if (m_anchor && anchorRowGone()) {
            // The anchor's row was released, or its delegate reused for another row (reuseItems):
            // the row left the view (and its cache). To get there from where it was on screen it
            // moved at least to the nearer edge; what this frame's scrolling does not explain of
            // that is a layout shift — a row above it grew (by at least that much) and pushed it
            // out. A new anchor below.
            if (prev && m_anchorScreenY >= 0 && m_anchorScreenY <= m_target->height()) {
                const double scrolled = std::abs(f.contentY - prev->contentY);
                const double toEdge = std::min(m_anchorScreenY + m_anchorHeight, m_target->height() - m_anchorScreenY);
                if (toEdge - scrolled > 0.5) {
                    f.anchorShift = toEdge - scrolled;
                    events.append(u"A!%1"_s.arg(toEdge - scrolled, 0, 'f', 0));
                }
            }
            m_anchor = nullptr;
            events.append(u"A-"_s);
        }
        if (m_anchor && m_anchor->isVisible() && m_anchor->window()) {
            const double contentPos = m_anchor->mapToItem(m_contentItem, QPointF(0, 0)).y();
            const double screenPos = m_anchor->mapToItem(m_target, QPointF(0, 0)).y();
            f.anchorShift = contentPos - m_anchorContentY;
            f.anchorScreen = screenPos - m_anchorScreenY;
            // Touchpad: frames with scroll events (after the gesture Flickable may still animate).
            // Observe: touch drags only — a mouse or a stylus (Qt's mouse / tablet path) moves
            // the content too, but the probe follows fingers only: those frames are not judged.
            const bool following = prev
                                   && (m_script == u"touchpad" ? f.inputEvents > 0 && std::abs(f.dyAnim) < 0.5
                                                               : f.dragging && prev->dragging
                                                                         && (!m_observe || f.pressed || prev->pressed));
            // Observe: frames of a drag follow the finger; the rest are animations or rest.
            const bool direct = m_observe ? following : m_direct;
            if (direct && following) {
                // The content follows the finger / touchpad: it moves by -inputScroll on screen. Less
                // in the same direction is input held back (the drag threshold, StopAtBounds at an
                // end); more, or the other way, is a jump.
                const double expected = -f.inputScroll;
                const double moved = f.anchorScreen;
                const bool against = expected * moved < 0 && std::abs(moved) > 2;
                if (std::abs(moved) > std::abs(expected) + 2 || against) {
                    f.unexplained = moved - expected;
                    f.jump = true;
                } else if (std::abs(moved - expected) > 2) {
                    f.held = expected - moved;
                } else {
                    f.unexplained = moved - expected;
                }
            } else if ((m_direct || m_observe) && prev && !f.moving && !prev->moving && f.inputEvents == 0
                       && std::abs(f.dyAnim) < 0.5) {
                f.unexplained = f.anchorScreen; // at rest: nothing may move
                f.jump = std::abs(f.unexplained) > 2;
            } else if (!direct && prev && !std::isnan(prev->anchorScreen) && f.inputEvents == 0
                       && prev->inputEvents == 0) {
                if (f.moving && prev->moving && !f.atBound && !prev->atBound && prev->interval > 0) {
                    // An animation (fling, wheel): the displacement changes smoothly from frame to
                    // frame — Qt's animation driver advances one vsync interval per frame, or the
                    // time since the last frame when frames are late (its timer mode).
                    const double perFrame = f.anchorScreen - prev->anchorScreen;
                    const double perTime = f.anchorScreen - prev->anchorScreen / prev->interval * f.interval;
                    f.unexplained = std::abs(perFrame) < std::abs(perTime) ? perFrame : perTime;
                    // An animation starting after a frame without movement (a fling after the still
                    // frame at the lift-off, restarted at the finger's speed by TouchFlick) may move
                    // at once by up to the fastest fling over the time since it started: at most
                    // since the start of the previous frame.
                    const double sinceStart = f.interval + prev->interval;
                    const double limit =
                            std::abs(prev->anchorScreen) < 1
                                    ? std::max(6.0, num(m_target, "maximumFlickVelocity") * sinceStart / 1000 + 2)
                                    : std::max(6.0, 0.5 * std::abs(prev->anchorScreen));
                    f.jump = std::abs(f.unexplained) > limit;
                } else if (!f.moving && !prev->moving) {
                    f.unexplained = f.anchorScreen;
                    f.jump = std::abs(f.unexplained) > 2;
                }
            }
            const bool onScreen = screenPos > -m_anchor->height() && screenPos < m_target->height();
            if (!onScreen)
                m_anchor = nullptr;
        }
        if (!m_anchor || !m_anchor->window()) {
            m_anchor = anchorAt(centre);
            m_anchorDelegate = m_anchor ? delegateOf(m_anchor) : nullptr;
            const QVariant row = m_anchorDelegate ? m_anchorDelegate->property("index") : QVariant();
            m_anchorRow = row.isValid() ? row.toInt() : -1;
            if (m_anchor) {
                // A<row>: the anchor's delegate.
                events.append(u"A"_s + (row.isValid() ? QString::number(m_anchorRow) : QString()));
            }
        }
        if (m_anchor) {
            m_anchorContentY = m_anchor->mapToItem(m_contentItem, QPointF(0, 0)).y();
            m_anchorScreenY = m_anchor->mapToItem(m_target, QPointF(0, 0)).y();
            m_anchorHeight = m_anchor->height();
        }
        f.events = events.join(u' ');
        if (!m_strokes.isEmpty()) {
            Stroke &s = m_strokes.last();
            if (f.dragging && prev && prev->dragging) {
                ++s.dragFrames;
                if (f.touchMoves == 0)
                    ++s.stillFrames;
                else if (f.touchMoves > 1)
                    ++s.multiFrames;
            }
            if (!std::isnan(s.tUp) && std::isnan(s.velocity))
                s.velocity = f.velocity;
            if (!std::isnan(s.tUp) && std::isnan(s.tStop) && !f.moving)
                closeFling(t0);
        }
        f.probeMs = now() - t0;
        m_frames.append(f);
        if (m_frames.size() > 200000)
            finish();
    }

    // ---- Report ---------------------------------------------------------------------------------
    static QString fmt(double v, int prec = 1) { return std::isnan(v) ? QString() : QString::number(v, 'f', prec); }

    void writeCsv() const
    {
        if (m_outFile.isEmpty())
            return;
        QFile file(m_outFile);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            log(u"cannot write "_s + m_outFile);
            return;
        }
        file.write("n,t_ms,interval_ms,gap_ms,polish_ms,prev_frame_gui_ms,contentY,originY,contentHeight,height,"
                   "vis_pos,vis_ratio,moving,dragging,flicking,velocity,input_events,input_scroll,dy_input,dy_frame,"
                   "dy_anim,dy_other,delegates,created,destroyed,blocks,blocks_created,anchor_shift,anchor_screen,"
                   "unexplained,held,jump,phase,events,probe_ms,touch_moves,touch_dy,touch_lag_ms,pressed,layout\n");
        for (const Frame &f : m_frames) {
            const QStringList cols{
                    QString::number(f.n),
                    fmt(f.t),
                    fmt(f.interval, 2),
                    fmt(f.gap, 2),
                    fmt(f.polish, 2),
                    fmt(f.gui, 2),
                    fmt(f.contentY, 1),
                    fmt(f.originY, 1),
                    fmt(f.contentHeight, 1),
                    fmt(f.height, 0),
                    fmt(f.visPos, 4),
                    fmt(f.visRatio, 4),
                    QString::number(f.moving),
                    QString::number(f.dragging),
                    QString::number(f.flicking),
                    fmt(f.velocity, 0),
                    QString::number(f.inputEvents),
                    fmt(f.inputScroll, 1),
                    fmt(f.dyInput, 1),
                    fmt(f.dyFrame, 1),
                    fmt(f.dyAnim, 1),
                    fmt(f.dyOther, 1),
                    QString::number(f.delegates),
                    QString::number(f.created),
                    QString::number(f.destroyed),
                    QString::number(f.blocks),
                    QString::number(f.blocksCreated),
                    fmt(f.anchorShift, 1),
                    fmt(f.anchorScreen, 1),
                    fmt(f.unexplained, 1),
                    fmt(f.held, 1),
                    QString::number(f.jump),
                    f.phase,
                    f.events,
                    fmt(f.probeMs, 3),
                    QString::number(f.touchMoves),
                    fmt(f.touchDy, 1),
                    fmt(f.touchLag, 2),
                    QString::number(f.pressed),
                    f.layout,
            };
            file.write(cols.join(u',').toUtf8());
            file.write("\n");
        }
        log(u"wrote %1 frames to %2"_s.arg(m_frames.size()).arg(m_outFile));
    }

    void summarize() const
    {
        // Frames while scrolling: in a phase, and the view moving or input arriving.
        QList<double> intervals;
        QList<double> probe;
        QList<const Frame *> stutter;
        QList<const Frame *> jumps;
        QList<const Frame *> other;
        int frames = 0, createdFrames = 0, created = 0, blocksCreated = 0, originChanges = 0, heightChanges = 0;
        int thumbJumps = 0, rowThumbJumps = 0;
        double originTotal = 0, jumpTotal = 0, jumpMax = 0, otherTotal = 0, frameDyDirect = 0, shiftTotal = 0;
        double animTotal = 0;
        int shifts = 0, heldFrames = 0;
        double heldTotal = 0;
        const Frame *prev = nullptr;
        for (const Frame &f : m_frames) {
            // Scrolling: the view moved since a frame in which it was already moving (a long
            // interval before such a frame is a visible hitch; idle time between strokes is not).
            const bool active = f.phase != u"end" && prev && prev->moving && f.moving
                                && std::abs(f.contentY - prev->contentY) > 0.5;
            probe.append(f.probeMs);
            created += f.created;
            blocksCreated += f.blocksCreated;
            if (f.created > 0 || f.blocksCreated > 0)
                ++createdFrames;
            if (prev) {
                if (std::abs(f.originY - prev->originY) > 0.5) {
                    ++originChanges;
                    originTotal += std::abs(f.originY - prev->originY);
                }
                if (std::abs(f.contentHeight - prev->contentHeight) > 0.5 && (f.moving || prev->moving))
                    ++heightChanges;
                // The scroll bar's thumb: where it is against where the scroll alone would put it.
                if (f.contentHeight > 0 && prev->contentHeight > 0 && (f.moving || prev->moving)) {
                    const double thumb = f.visPos * f.height;
                    const double expected =
                            prev->visPos * f.height + (f.contentY - prev->contentY) / f.contentHeight * f.height;
                    if (std::abs(thumb - expected) > 3)
                        ++thumbJumps;
                }
                if (rowThumbJump(*prev, f))
                    ++rowThumbJumps;
            }
            if (active && f.interval > 0) {
                ++frames;
                intervals.append(f.interval);
                if (f.interval > 33.4)
                    stutter.append(&f);
            }
            if (!std::isnan(f.held)) {
                ++heldFrames;
                heldTotal += std::abs(f.held);
            }
            if (f.jump) {
                jumps.append(&f);
                jumpTotal += std::abs(f.unexplained);
                jumpMax = std::max(jumpMax, std::abs(f.unexplained));
            }
            if (!std::isnan(f.anchorShift) && std::abs(f.anchorShift) > 1.0) {
                ++shifts;
                shiftTotal += std::abs(f.anchorShift);
            }
            if (std::abs(f.dyOther) > 0.5) {
                other.append(&f);
                otherTotal += std::abs(f.dyOther);
            }
            frameDyDirect += std::abs(f.dyFrame);
            animTotal += std::abs(f.dyAnim);
            prev = &f;
        }
        const auto line = [this](const QString &s) { log(s); };
        QList<double> swapIntervals;
        {
            const QMutexLocker lock(&m_swapMutex);
            for (qsizetype i = 1; i < m_swaps.size(); ++i)
                swapIntervals.append(m_swaps.at(i) - m_swaps.at(i - 1));
        }
        line(u"SUMMARY script=%1 target=%2 frames=%3 scrolling-frames=%4 duration=%5s"_s.arg(m_script, m_targetSpec)
                     .arg(m_frames.size())
                     .arg(frames)
                     .arg(m_frames.isEmpty() ? 0 : m_frames.constLast().t / 1000, 0, 'f', 1));
        line(u"FRAMES interval ms p50=%1 p90=%2 p99=%3 max=%4; >33ms=%5 >50ms=%6 >100ms=%7; swap p50=%8 p99=%9 max=%10"_s
                     .arg(percentile(intervals, 0.5), 0, 'f', 1)
                     .arg(percentile(intervals, 0.9), 0, 'f', 1)
                     .arg(percentile(intervals, 0.99), 0, 'f', 1)
                     .arg(percentile(intervals, 1), 0, 'f', 1)
                     .arg(std::count_if(intervals.cbegin(), intervals.cend(), [](double v) { return v > 33.4; }))
                     .arg(std::count_if(intervals.cbegin(), intervals.cend(), [](double v) { return v > 50; }))
                     .arg(std::count_if(intervals.cbegin(), intervals.cend(), [](double v) { return v > 100; }))
                     .arg(percentile(swapIntervals, 0.5), 0, 'f', 1)
                     .arg(percentile(swapIntervals, 0.99), 0, 'f', 1)
                     .arg(percentile(swapIntervals, 1), 0, 'f', 1));
        line(u"JUMPS %1 frames (%2), total %3 px, max %4 px; layout shifts of the anchor: %5 frames, %6 px; "
             u"input held back (drag threshold, an end): %7 frames, %8 px"_s.arg(jumps.size())
                     .arg(m_observe ? u"drags: content moved more than the finger, or against it; flings: "
                                      u"displacement off the animation's by > max(6 px, 50%)"_s
                          : m_direct ? u"content moved more than the input, or against it"_s
                                     : u"displacement off the animation's by > max(6 px, 50%)"_s)
                     .arg(jumpTotal, 0, 'f', 0)
                     .arg(jumpMax, 0, 'f', 0)
                     .arg(shifts)
                     .arg(shiftTotal, 0, 'f', 0)
                     .arg(heldFrames)
                     .arg(heldTotal, 0, 'f', 0));
        line(u"SCROLL not from input or animation (timers, queued calls, model signals): %1 frames, %2 px; contentY "
             u"changes inside frames (polish, touch moves Qt delivers with the frame): %3 px; animations: %4 px"_s
                     .arg(other.size())
                     .arg(otherTotal, 0, 'f', 0)
                     .arg(frameDyDirect, 0, 'f', 0)
                     .arg(animTotal, 0, 'f', 0));
        line(u"VIEW originY changes %1 (total %2 px), contentHeight changes while moving %3, scroll bar thumb "
             u"jumps > 3 px %4 (visibleArea, an attached bar's), %5"_s.arg(originChanges)
                     .arg(originTotal, 0, 'f', 0)
                     .arg(heightChanges)
                     .arg(thumbJumps)
                     .arg(m_rowScroll ? u"row scroll bar thumb jumps %1"_s.arg(rowThumbJumps)
                                      : u"no row scroll bar"_s));
        line(u"DELEGATES created %1 (in %2 frames), nested blocks created %3; probe cost per frame p50 %4 ms, max %5 ms"_s
                     .arg(created)
                     .arg(createdFrames)
                     .arg(blocksCreated)
                     .arg(percentile(probe, 0.5), 0, 'f', 3)
                     .arg(percentile(probe, 1), 0, 'f', 3));
        const auto describe = [](const Frame *f) {
            return u"#%1 t=%2 %3 interval=%4 gap=%5 polish=%6 prevgui=%7 dy(in/frame/anim/other)=%8/%9/%10/%11 "
                   u"unexplained=%12 "
                   u"shift=%13 created=%14 blocks+=%15 y=%16 origin=%17 h=%18 [%19]"_s.arg(f->n)
                           .arg(f->t, 0, 'f', 0)
                           .arg(f->phase)
                           .arg(f->interval, 0, 'f', 1)
                           .arg(f->gap, 0, 'f', 1)
                           .arg(f->polish, 0, 'f', 1)
                           .arg(f->gui, 0, 'f', 1)
                           .arg(f->dyInput, 0, 'f', 0)
                           .arg(f->dyFrame, 0, 'f', 0)
                           .arg(f->dyAnim, 0, 'f', 0)
                           .arg(f->dyOther, 0, 'f', 0)
                           .arg(fmt(f->unexplained, 0))
                           .arg(fmt(f->anchorShift, 0))
                           .arg(f->created)
                           .arg(f->blocksCreated)
                           .arg(f->contentY, 0, 'f', 0)
                           .arg(f->originY, 0, 'f', 0)
                           .arg(f->contentHeight, 0, 'f', 0)
                           .arg(f->events);
        };
        QList<const Frame *> worstJumps = jumps;
        std::sort(worstJumps.begin(), worstJumps.end(),
                  [](const Frame *a, const Frame *b) { return std::abs(a->unexplained) > std::abs(b->unexplained); });
        for (qsizetype i = 0; i < std::min<qsizetype>(12, worstJumps.size()); ++i)
            line(u"  jump "_s + describe(worstJumps.at(i)));
        QList<const Frame *> worstStutter = stutter;
        std::sort(worstStutter.begin(), worstStutter.end(),
                  [](const Frame *a, const Frame *b) { return a->interval > b->interval; });
        for (qsizetype i = 0; i < std::min<qsizetype>(12, worstStutter.size()); ++i)
            line(u"  stutter "_s + describe(worstStutter.at(i)));
        for (qsizetype i = 0; i < std::min<qsizetype>(8, other.size()); ++i)
            line(u"  other-scroll "_s + describe(other.at(i)));
        if (!m_strokes.isEmpty())
            summarizeTouch();
        if (m_checkTolerance > 0)
            check();
    }

    /// --probe-check <px>: the content may not jump — move on screen more than the input explains
    /// (direct scripts: drag, touchpad) or shift inside the list (the anchor's layout shift, any
    /// script) — by more than <px>. Geometry only, no timing: also valid in slow (sanitizer) runs.
    /// At an end of the list (in this frame or the one before) only layout shifts count: there the
    /// view clamps the content back against the input (StopAtBounds), and ListView's estimate of
    /// the rows it has not created moves that end whenever a row at the edge comes and goes (with
    /// --probe-cache 0 a row at the edge is released and created again as the input pushes past
    /// the end).
    void check() const
    {
        int jumps = 0, shifts = 0, thumbJumps = 0;
        double worst = 0;
        const Frame *prev = nullptr;
        for (const Frame &f : m_frames) {
            const bool atBound = f.atBound || (prev && prev->atBound);
            const Frame *before = prev;
            prev = &f;
            if (f.phase == u"end")
                continue;
            if (before && rowThumbJump(*before, f)) {
                ++thumbJumps;
                log(u"THUMB JUMP #%1: row scroll bar %2 -> %3 while contentY %4 -> %5"_s.arg(f.n)
                            .arg(before->rowThumb, 0, 'f', 5)
                            .arg(f.rowThumb, 0, 'f', 5)
                            .arg(before->contentY, 0, 'f', 1)
                            .arg(f.contentY, 0, 'f', 1));
            }
            if (m_direct && f.jump && !atBound && std::abs(f.unexplained) > m_checkTolerance) {
                ++jumps;
                worst = std::max(worst, std::abs(f.unexplained));
            }
            if (!std::isnan(f.anchorShift) && std::abs(f.anchorShift) > m_checkTolerance) {
                ++shifts;
                worst = std::max(worst, std::abs(f.anchorShift));
            }
        }
        m_checkFailed = jumps > 0 || shifts > 0 || thumbJumps > 0 || m_frames.size() < 10;
        log(u"CHECK %1: %2 jumps and %3 layout shifts over %4 px (worst %5 px), %6 row scroll bar thumb jumps in %7 frames"_s
                    .arg(m_checkFailed ? u"FAIL"_s : u"PASS"_s)
                    .arg(jumps)
                    .arg(shifts)
                    .arg(m_checkTolerance)
                    .arg(worst, 0, 'f', 0)
                    .arg(thumbJumps)
                    .arg(m_frames.size()));
    }

    /// Observe: how the real touch input reached the frames, and the flings.
    void summarizeTouch() const
    {
        // Frames of a drag (dragging in this frame and the previous one).
        int drag = 0, still = 0, one = 0, multi = 0;
        QList<double> moved, lag, ratio;
        const Frame *prev = nullptr;
        for (const Frame &f : m_frames) {
            if (prev && f.dragging && prev->dragging) {
                ++drag;
                if (f.touchMoves == 0)
                    ++still;
                else if (f.touchMoves == 1)
                    ++one;
                else
                    ++multi;
                if (!std::isnan(f.anchorScreen))
                    moved.append(std::abs(f.anchorScreen));
                if (!std::isnan(f.touchLag))
                    lag.append(f.touchLag);
            }
            prev = &f;
        }
        // Per-frame movement against its median: a steady finger moves the content evenly.
        const double median = percentile(moved, 0.5);
        for (double m : std::as_const(moved)) {
            if (median > 0.5)
                ratio.append(m / median);
        }
        const auto share = [drag](int n) { return drag ? 100.0 * n / drag : 0.0; };
        log(u"TOUCH drag frames %1: no new move %2 (%3%), 1 move %4 (%5%), 2+ moves %6 (%7%); content movement per "
            u"frame px p10=%8 p50=%9 p90=%10 max=%11, frames < 0.25x median %12, > 1.75x median %13; last move "
            u"arrived before the frame ms p10=%14 p50=%15 p90=%16"_s.arg(drag)
                    .arg(still)
                    .arg(share(still), 0, 'f', 0)
                    .arg(one)
                    .arg(share(one), 0, 'f', 0)
                    .arg(multi)
                    .arg(share(multi), 0, 'f', 0)
                    .arg(percentile(moved, 0.1), 0, 'f', 1)
                    .arg(median, 0, 'f', 1)
                    .arg(percentile(moved, 0.9), 0, 'f', 1)
                    .arg(percentile(moved, 1), 0, 'f', 1)
                    .arg(std::count_if(ratio.cbegin(), ratio.cend(), [](double r) { return r < 0.25; }))
                    .arg(std::count_if(ratio.cbegin(), ratio.cend(), [](double r) { return r > 1.75; }))
                    .arg(percentile(lag, 0.1), 0, 'f', 1)
                    .arg(percentile(lag, 0.5), 0, 'f', 1)
                    .arg(percentile(lag, 0.9), 0, 'f', 1));
        // TouchFlick (touchflick.h) restarts a damped flick with the finger's speed; the view's
        // velocity in the first frame after the release is still Qt's damped one (Flickable updates
        // it with the flick's first animation step), so the start velocity is taken from how far a
        // fling went by itself: v = sqrt(2 * flickDeceleration * distance).
        const auto *touchFlick =
                m_target ? qobject_cast<brulion::ui::TouchFlick *>(
                                   qmlAttachedPropertiesObject<brulion::ui::TouchFlick>(m_target, false))
                         : nullptr;
        if (touchFlick)
            log(u"TouchFlick %1: %2 flicks corrected (last x%3), %4 drags caught a fling"_s
                        .arg(brulion::ui::TouchFlick::switchedOff() ? u"off (BRULION_TOUCHFLICK=0)"_s
                             : touchFlick->isEnabled()              ? u"on"_s
                                                                    : u"off"_s)
                        .arg(touchFlick->corrections())
                        .arg(touchFlick->lastFactor(), 0, 'f', 3)
                        .arg(touchFlick->catches()));
        else
            log(u"TouchFlick: not attached to the view"_s);
        const double deceleration = m_target ? num(m_target, "flickDeceleration") : 0;
        QList<double> startRatios; // strokes of 300 ms or more, flung freely: steadier speeds
        for (const Stroke &s : m_strokes) {
            const double duration = (std::isnan(s.tUp) ? s.tDown : s.tUp) - s.tDown;
            const double flung = s.contentYStop - s.contentYUp;
            const double start = s.caught || s.atBound || std::isnan(flung) || std::abs(flung) < 1
                                         ? kNaN
                                         : std::sqrt(2 * deceleration * std::abs(flung));
            log(u"  stroke t=%1 %2 ms, finger %3 px, %4 moves (%5/s); drag frames %6, no new move %7, 2+ moves %8; "
                u"release speed %9 px/s, view velocity after %10 px/s, fling %11 px in %12 ms%13, start velocity "
                u"from its distance %14 px/s (x%15 the release speed)"_s.arg(s.tDown - m_startMs, 0, 'f', 0)
                        .arg(duration, 0, 'f', 0)
                        .arg(s.yUp - s.yDown, 0, 'f', 0)
                        .arg(s.moves)
                        .arg(duration > 0 ? s.moves / duration * 1000 : 0, 0, 'f', 0)
                        .arg(s.dragFrames)
                        .arg(s.stillFrames)
                        .arg(s.multiFrames)
                        .arg(fmt(s.releaseSpeed, 0))
                        .arg(fmt(s.velocity, 0))
                        .arg(fmt(flung, 0))
                        .arg(fmt(s.tStop - s.tUp, 0))
                        .arg(s.caught    ? u" (caught)"_s
                             : s.atBound ? u" (to an end)"_s
                                         : QString())
                        .arg(fmt(start, 0))
                        .arg(fmt(start / std::abs(s.releaseSpeed), 2)));
            if (duration >= 300 && !std::isnan(start) && std::abs(s.releaseSpeed) > 100)
                startRatios.append(start / std::abs(s.releaseSpeed));
        }
        // Per stroke the ratio is noisy (the release speed of a few samples); compare the same
        // strokes with BRULION_TOUCHFLICK=0 by their fling distances for the exact factor.
        if (!startRatios.isEmpty())
            log(u"TOUCH fling start velocity / release speed, strokes of 300 ms or more: p50 x%1 (%2 strokes; Qt "
                u"6.11 alone: x0.70)"_s.arg(percentile(startRatios, 0.5), 0, 'f', 2)
                        .arg(startRatios.size()));
    }

    void writeLog() const
    {
        if (m_outFile.isEmpty())
            return;
        QFile file(m_outFile + u".log"_s);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            return;
        for (const QString &l : std::as_const(m_logLines))
            file.write((u"scroll-probe: "_s + l + u'\n').toUtf8());
    }

    void finish()
    {
        if (m_finished)
            return;
        m_finished = true;
        m_input.stop();
        if (m_window)
            m_window->removeEventFilter(this);
        writeCsv();
        summarize();
        writeLog();
        QCoreApplication::exit(m_checkFailed ? 1 : 0);
    }

    QQmlApplicationEngine *m_engine = nullptr;
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_target;
    QPointer<QQuickItem> m_contentItem;
    QPointer<QObject> m_visibleArea;
    QPointer<brulion::ui::RowScroll> m_rowScroll;
    QString m_targetSpec;
    QString m_script;
    QString m_outFile;
    int m_passes = 1;
    int m_waitMs = 1500;
    int m_maxMs = 60000;
    int m_speed = 1500;
    bool m_direct = true;
    bool m_observe = false;
    bool m_finished = false;
    double m_checkTolerance = 0;   // --probe-check: px (0: no check)
    int m_cache = -1;              // --probe-cache: the target's cacheBuffer (-1: its own)
    bool m_hasItemAtIndex = false; // the target is an item view (ListView, GridView)
    mutable bool m_checkFailed = false;
    mutable QStringList m_logLines;

    QElapsedTimer m_clock;
    QTimer m_input;
    double m_startMs = 0;
    int m_phaseIndex = 0;
    double m_phaseStart = 0;
    int m_stroke = 0;
    double m_strokeStart = -1;
    double m_strokeEnd = 0;
    double m_restUntil = 0;
    int m_strokeStep = 0;
    double m_lastFinger = 0;

    Context m_context = Context::Other;
    int m_inputEvents = 0;
    double m_inputScroll = 0;
    double m_lastContentY = 0;
    double m_dyInput = 0, m_dyFrame = 0, m_dyAnim = 0, m_dyOther = 0;
    double m_frameBegin = 0;
    double m_lastFrameEnd = 0;
    double m_gapMs = 0;
    double m_lastGuiMs = 0;
    double m_lastFrameAt = 0;

    QHash<const QQuickItem *, DelegateState> m_delegates;
    QSet<const QQuickItem *> m_blocks;
    QPointer<QQuickItem> m_anchor;
    QPointer<QQuickItem> m_anchorDelegate; // the view's delegate holding the anchor
    int m_anchorRow = -1;                  // … and its row when the anchor was chosen
    double m_anchorContentY = 0;
    double m_anchorScreenY = 0;
    double m_anchorHeight = 0;
    QList<Frame> m_frames;
    // Observe: real touch input.
    QList<Stroke> m_strokes;
    int m_touchMoves = 0;
    double m_touchDy = 0;
    double m_fingerY = 0;
    double m_lastMoveAt = 0;
    bool m_pressed = false;
    mutable QMutex m_swapMutex;
    QList<double> m_swaps;
};

} // namespace

void startScrollProbe(QQmlApplicationEngine *engine, const QStringList &arguments)
{
    const bool wanted = std::any_of(arguments.cbegin(), arguments.cend(),
                                    [](const QString &arg) { return arg.startsWith(u"--scroll-probe"_s); });
    if (!engine || !wanted)
        return;
    new ScrollProbe(engine, arguments); // owned by the engine; quits the app when done
}

} // namespace brulion::ui

#include "scrollprobe.moc"
