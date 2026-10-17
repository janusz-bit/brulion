#include "touchflick.h"

#include <QCoreApplication>
#include <QList>
#include <QLoggingCategory>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTouchEvent>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <utility>

namespace brulion::ui {

namespace {

Q_LOGGING_CATEGORY(lcTouchFlick, "brulion.touchflick", QtWarningMsg)

// The largest correction applied: Qt's damping is 1/0.7 = 1.43; more only when the finger
// slowed down within the last samples (the estimate spans more of them than Flickable's 3).
constexpr qreal MaxFactor = 2;

/// The touch strokes of the whole application, seen before Qt Quick delivers them (an
/// application event filter: it also sees events that another filter on the window handles
/// itself, such as the scroll probe's). One finger at a time; costs a switch per event.
class TouchTracker : public QObject {
public:
    /// The tracker, installed on first use (null without an application).
    static TouchTracker *instance()
    {
        QPointer<TouchTracker> &tracker = slot();
        if (!tracker && QCoreApplication::instance()) {
            tracker = new TouchTracker(QCoreApplication::instance());
            QCoreApplication::instance()->installEventFilter(tracker);
        }
        return tracker;
    }
    /// The tracker if there is one (never creates it: destructors at shutdown).
    static TouchTracker *existing() { return slot(); }

    void add(TouchFlick *flick)
    {
        if (!m_flicks.contains(flick))
            m_flicks.append(flick);
    }
    void remove(TouchFlick *flick)
    {
        m_flicks.removeAll(flick);
        m_catching.removeAll(flick);
    }

    /// The stroke whose release is being delivered in `window` right now (single finger).
    const TouchVelocityEstimator *releasing(const QWindow *window) const
    {
        return m_releasing && window && m_window == window ? &m_samples : nullptr;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            if (watched->isWindowType())
                track(static_cast<QWindow *>(watched), static_cast<const QTouchEvent *>(event));
            break;
        default: break;
        }
        return false;
    }

private:
    explicit TouchTracker(QObject *parent) : QObject(parent) { }

    void track(QWindow *window, const QTouchEvent *event)
    {
        if (event->type() == QEvent::TouchCancel) {
            m_active = false;
            endCatches();
            return;
        }
        const QPointingDevice *device = event->pointingDevice();
        if (!device || device->type() != QInputDevice::DeviceType::TouchScreen
            || device->capabilities().testFlag(QInputDevice::Capability::Velocity) || event->points().isEmpty())
            return;
        const QEventPoint &point = event->points().constFirst();
        // At the window, position() is in the window's (= the Qt Quick scene's) coordinates.
        const QPointF pos = point.position();
        const auto t = qint64(event->timestamp());
        switch (event->type()) {
        case QEvent::TouchBegin: {
            m_active = true;
            m_releasing = false;
            m_single = event->pointCount() == 1;
            m_window = window;
            m_device = device;
            m_pointId = point.id();
            m_samples.press(t, pos);
            m_catching.clear();
            for (const QPointer<TouchFlick> &flick : listeners()) {
                if (flick && flick->touchPressed(window, pos) && m_single)
                    m_catching.append(flick);
            }
            break;
        }
        case QEvent::TouchUpdate:
            if (!m_active || window != m_window || device != m_device)
                return;
            if (event->pointCount() != 1 || point.id() != m_pointId) {
                m_single = false;
                endCatches();
                return;
            }
            if (point.state() != QEventPoint::State::Updated)
                return;
            m_samples.move(t, pos, point.velocity());
            // Every move until each catch is over: with touch compression (Qt Quick's default)
            // the moves that arrive within one frame reach Flickable as one, so the move it skips
            // can be the sum of several (a slow frame while the fling ran into new rows).
            if (!m_catching.isEmpty()) {
                const QList<QPointer<TouchFlick>> catching = m_catching;
                for (const QPointer<TouchFlick> &flick : catching) {
                    if (!flick || !flick->touchMoved(window, pos))
                        m_catching.removeAll(flick);
                }
            }
            break;
        case QEvent::TouchEnd:
            if (!m_active || window != m_window || device != m_device)
                return;
            m_active = false;
            endCatches();
            if (!m_single || event->pointCount() != 1 || point.id() != m_pointId)
                return;
            m_samples.release(t, pos);
            // Flickable starts its flick while this release is delivered, right after this filter.
            m_releasing = true;
            QMetaObject::invokeMethod(this, [this] { m_releasing = false; }, Qt::QueuedConnection);
            break;
        default: break;
        }
    }

    static QPointer<TouchTracker> &slot()
    {
        static QPointer<TouchTracker> tracker;
        return tracker;
    }

    void endCatches()
    {
        const QList<QPointer<TouchFlick>> catching = std::exchange(m_catching, {});
        for (const QPointer<TouchFlick> &flick : catching) {
            if (flick)
                flick->touchCatchEnded();
        }
    }

    // A copy: a listener's reaction (moving the content) may create or destroy others.
    QList<QPointer<TouchFlick>> listeners() const { return {m_flicks.cbegin(), m_flicks.cend()}; }

    TouchVelocityEstimator m_samples;
    QPointer<QWindow> m_window;
    const QPointingDevice *m_device = nullptr;
    int m_pointId = -1;
    bool m_active = false;
    bool m_single = true;
    bool m_releasing = false;
    QList<TouchFlick *> m_flicks;
    QList<QPointer<TouchFlick>> m_catching; // presses of this stroke that caught a fling
};

qreal real(const QObject *object, const char *name)
{
    return object->property(name).toReal();
}

} // namespace

// ---- TouchVelocityEstimator -------------------------------------------------------------------------

void TouchVelocityEstimator::append(const Sample &s)
{
    if (m_count == Capacity) {
        m_head = (m_head + 1) % Capacity;
        --m_count;
    }
    m_samples[size_t((m_head + m_count) % Capacity)] = s;
    ++m_count;
}

void TouchVelocityEstimator::press(qint64 timeMs, QPointF pos)
{
    clear();
    append({timeMs, pos, {}, {}, false});
}

void TouchVelocityEstimator::move(qint64 timeMs, QPointF pos, QVector2D reported)
{
    Sample s{timeMs, pos, reported, {}, true};
    if (m_count > 0) {
        Sample &last = at(m_count - 1);
        if (timeMs < last.t)
            return; // out of order
        if (timeMs == last.t) {
            // Qt computes no velocity for an event with the previous one's timestamp either.
            last.pos = pos;
            last.reported = reported;
            last.instant = {};
            last.move = true;
            return;
        }
        s.instant = QVector2D(pos - last.pos) * (1000.0f / float(timeMs - last.t));
    }
    append(s);
}

void TouchVelocityEstimator::release(qint64 timeMs, QPointF pos)
{
    if (m_count == 0)
        return;
    const Sample &last = at(m_count - 1);
    if (timeMs <= last.t || (pos - last.pos).manhattanLength() < 0.01)
        return;
    append({timeMs, pos, {}, QVector2D(pos - last.pos) * (1000.0f / float(timeMs - last.t)), false});
}

std::optional<QVector2D> TouchVelocityEstimator::fingerVelocity() const
{
    if (m_count < 2)
        return std::nullopt;
    const Sample &last = at(m_count - 1);
    int ref = -1;
    for (int i = m_count - 2; i >= 0; --i) {
        const qint64 span = last.t - at(i).t;
        if (span > MaxSpanMs)
            break;
        ref = i;
        if (span >= MinSpanMs && m_count - 1 - i >= MinSegments)
            break;
    }
    if (ref < 0 || last.t <= at(ref).t)
        return std::nullopt;
    return QVector2D(last.pos - at(ref).pos) * (1000.0f / float(last.t - at(ref).t));
}

std::optional<QVector2D> TouchVelocityEstimator::reportedVelocity(qreal maxVelocity) const
{
    QVector2D sum;
    int n = 0;
    const auto clamp = [maxVelocity](float v) {
        return maxVelocity > 0 ? std::clamp(v, -float(maxVelocity), float(maxVelocity)) : v;
    };
    for (int i = m_count - 1; i >= 0 && n < QtSamples; --i) {
        const Sample &s = at(i);
        if (!s.move)
            continue;
        sum += QVector2D(clamp(s.reported.x()), clamp(s.reported.y()));
        ++n;
    }
    if (n == 0)
        return std::nullopt;
    return sum / float(n);
}

bool TouchVelocityEstimator::hasQtDampedSamples() const
{
    int matched = 0;
    for (int i = m_count - 1; i >= 1 && matched < QtSamples; --i) {
        const Sample &s = at(i);
        if (!s.move)
            continue;
        const QVector2D expected = s.instant * QtGain;
        if ((s.reported - expected).length() > 0.01f * expected.length() + 0.5f)
            return false;
        ++matched;
    }
    return matched >= 2;
}

// ---- TouchFlick ---------------------------------------------------------------------------------

TouchFlick::TouchFlick(QObject *flickable) : QObject(flickable), m_flickable(qobject_cast<QQuickItem *>(flickable))
{
    if (!isFlickable()) {
        qCWarning(lcTouchFlick) << "TouchFlick is attached to" << flickable << "which is not a Flickable";
        m_flickable = nullptr;
        return;
    }
    // QQuickFlickable is private API: its signal and method by name.
    connect(m_flickable, SIGNAL(flickStarted()), this, SLOT(onFlickStarted()));
    if (TouchTracker *tracker = TouchTracker::instance())
        tracker->add(this);
}

TouchFlick::~TouchFlick()
{
    if (TouchTracker *tracker = TouchTracker::existing())
        tracker->remove(this);
}

TouchFlick *TouchFlick::qmlAttachedProperties(QObject *object)
{
    return new TouchFlick(object);
}

bool TouchFlick::switchedOff()
{
    static const bool off =
            qEnvironmentVariableIsSet("BRULION_TOUCHFLICK") && qEnvironmentVariableIntValue("BRULION_TOUCHFLICK") == 0;
    return off;
}

void TouchFlick::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_catchPending = false;
    Q_EMIT enabledChanged();
}

bool TouchFlick::isFlickable() const
{
    return m_flickable && m_flickable->inherits("QQuickFlickable");
}

QVector2D TouchFlick::toLocal(QVector2D sceneVector) const
{
    return QVector2D(m_flickable->mapFromScene(sceneVector.toPointF()) - m_flickable->mapFromScene(QPointF()));
}

bool TouchFlick::snaps() const
{
    // ListView / GridView: snapMode NoSnap = 0, highlightRangeMode StrictlyEnforceRange = 2 (a
    // plain Flickable has neither: invalid, 0).
    return m_flickable->property("snapMode").toInt() != 0 || m_flickable->property("highlightRangeMode").toInt() == 2;
}

void TouchFlick::onFlickStarted()
{
    if (!m_enabled || !m_flickable || switchedOff())
        return;
    // A view that snaps computed where the flick ends from Qt's velocity when it started, and a
    // restarted flick keeps that end (QQuickListViewPrivate::flick): nothing to correct there.
    if (snaps())
        return;
    TouchTracker *tracker = TouchTracker::existing();
    const TouchVelocityEstimator *samples = tracker ? tracker->releasing(m_flickable->window()) : nullptr;
    // Not a touch release (wheel, flick() from code), or Qt's estimate is not the damped one.
    if (!samples || !samples->hasQtDampedSamples())
        return;
    const std::optional<QVector2D> finger = samples->fingerVelocity();
    const std::optional<QVector2D> reported = samples->reportedVelocity(real(m_flickable, "maximumFlickVelocity"));
    if (!finger || !reported)
        return;
    const QVector2D fingerLocal = toLocal(*finger);
    const QVector2D reportedLocal = toLocal(*reported);
    // The flick Qt just started, per axis (Flickable's own sign: + moves the content down/right;
    // verticalVelocity was set to minus it): scaled by how much Qt's estimate fell short, which
    // keeps its multi-flick boost and the halving at an end.
    qreal factor = 1;
    const auto corrected = [&factor](qreal qtVelocity, float fingerVelocity, float reportedVelocity) {
        if (qtVelocity == 0 || std::abs(reportedVelocity) < 1 || (fingerVelocity > 0) != (reportedVelocity > 0))
            return qtVelocity;
        const qreal f = std::clamp(qreal(fingerVelocity) / qreal(reportedVelocity), qreal(1), MaxFactor);
        factor = std::max(factor, f);
        return qtVelocity * f;
    };
    const qreal qtY = m_flickable->property("flickingVertically").toBool() ? -real(m_flickable, "verticalVelocity") : 0;
    const qreal qtX =
            m_flickable->property("flickingHorizontally").toBool() ? -real(m_flickable, "horizontalVelocity") : 0;
    const qreal vy = corrected(qtY, fingerLocal.y(), reportedLocal.y());
    const qreal vx = corrected(qtX, fingerLocal.x(), reportedLocal.x());
    if (factor < 1.02)
        return;
    qCDebug(lcTouchFlick) << "flick" << qtX << qtY << "->" << vx << vy << "finger" << fingerLocal << "Qt's samples"
                          << reportedLocal;
    // Restarts the flick from where it is (no time has passed): Flickable::flick() caps the
    // velocity at maximumFlickVelocity and emits nothing new while it is already flicking.
    QMetaObject::invokeMethod(m_flickable, "flick", Q_ARG(qreal, vx), Q_ARG(qreal, vy));
    m_lastFactor = factor;
    ++m_corrections;
    Q_EMIT correctionsChanged();
}

bool TouchFlick::touchPressed(const QWindow *window, QPointF scenePos)
{
    m_catchPending = false;
    if (!m_enabled || !m_flickable || m_flickable->window() != window || switchedOff())
        return false;
    QQuickItem *view = m_flickable;
    if (!view->isVisible() || !view->isEnabled() || !view->property("interactive").toBool()
        || !view->property("flickingVertically").toBool() || real(view, "verticalOvershoot") != 0
        || !view->contains(view->mapFromScene(scenePos)))
        return false;
    m_catchPending = true;
    m_catchCounted = false;
    m_catchPress = scenePos;
    m_catchContentY = real(view, "contentY");
    m_catchApplied = m_catchContentY;
    return true;
}

bool TouchFlick::touchMoved(const QWindow *window, QPointF scenePos)
{
    if (!m_catchPending)
        return false;
    QQuickItem *view = m_flickable;
    // Qt took the press as catching the fling: it stopped the fling, the view still counts as
    // moving; until Flickable has handled a move itself (it is dragging then), the content stays
    // where the press stopped it — or where the moves before put it. Anything else moved it: over.
    if (!m_enabled || !view || view->window() != window || view->property("flicking").toBool()
        || !view->property("moving").toBool() || view->property("dragging").toBool()
        || std::abs(real(view, "contentY") - m_catchApplied) > 0.5) {
        m_catchPending = false;
        return false;
    }
    const qreal dy = view->mapFromScene(scenePos).y() - view->mapFromScene(m_catchPress).y();
    if (std::abs(dy) < 0.5)
        return true;
    // Where Flickable puts the content with its next move (from the press, no drag threshold).
    // When it handles this move itself (a press on the Flickable during a fast fling), it puts it
    // there now, in the same event. Inside the bounds only: at an end its own rules decide.
    const qreal y = m_catchContentY - dy;
    const qreal top = real(view, "originY") - real(view, "topMargin");
    const qreal bottom =
            real(view, "originY") + real(view, "contentHeight") + real(view, "bottomMargin") - view->height();
    if (y < top || y > std::max(top, bottom)) {
        m_catchPending = false;
        return false;
    }
    qCDebug(lcTouchFlick) << "catch: move" << dy << "applied";
    view->setProperty("contentY", y);
    m_catchApplied = real(view, "contentY");
    if (!m_catchCounted) {
        m_catchCounted = true;
        ++m_catches;
        Q_EMIT catchesChanged();
    }
    return true;
}

void TouchFlick::touchCatchEnded()
{
    m_catchPending = false;
}

} // namespace brulion::ui
