#include "smoothscroll.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointer>
#include <QQuickItem>
#include <QStyleHints>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

qreal real(const QObject *object, const char *name)
{
    return object->property(name).toReal();
}

double sign(double v)
{
    return v < 0 ? -1.0 : 1.0;
}

/// Where the content position of `flickable` may go on one axis: Flickable's extents
/// (-minYExtent … -maxYExtent: originY − topMargin … the end, never above the start). ListView
/// estimates its content's size from the rows it has made; Qt's flick stops at its own extents
/// anyway, these only keep a target from running away past them.
std::pair<double, double> bounds(const QQuickItem *flickable, Qt::Orientation orientation)
{
    const bool vertical = orientation == Qt::Vertical;
    const double origin = real(flickable, vertical ? "originY" : "originX");
    const double startMargin = real(flickable, vertical ? "topMargin" : "leftMargin");
    const double endMargin = real(flickable, vertical ? "bottomMargin" : "rightMargin");
    const double content = real(flickable, vertical ? "contentHeight" : "contentWidth");
    const double view = vertical ? flickable->height() : flickable->width();
    const double lowest = origin - startMargin;
    return {lowest, std::max(lowest, origin + content + endMargin - view)};
}

/// Whether `flickable` scrolls along `orientation` at all (QQuickFlickable::yflick / xflick).
bool scrolls(const QQuickItem *flickable, Qt::Orientation orientation)
{
    const bool vertical = orientation == Qt::Vertical;
    // Flickable.FlickableDirection: AutoFlickDirection 0, HorizontalFlick 1, VerticalFlick 2,
    // HorizontalAndVerticalFlick 3, AutoFlickIfNeeded 0xc.
    const int direction = flickable->property("flickableDirection").toInt();
    const int content = int(real(flickable, vertical ? "contentHeight" : "contentWidth")
                            + real(flickable, vertical ? "topMargin" : "leftMargin")
                            + real(flickable, vertical ? "bottomMargin" : "rightMargin"));
    const double view = vertical ? flickable->height() : flickable->width();
    if ((direction & 0xc) && content > view)
        return true;
    if (direction == 0)
        return std::floor(std::abs(content - view)) != 0;
    return direction & (vertical ? 2 : 1);
}

/// Whether the view is at its end towards which `delta` scrolls (atYBeginning / atYEnd…).
bool atEnd(const QQuickItem *flickable, Qt::Orientation orientation, double delta)
{
    const bool vertical = orientation == Qt::Vertical;
    if (delta > 0)
        return flickable->property(vertical ? "atYEnd" : "atXEnd").toBool();
    return flickable->property(vertical ? "atYBeginning" : "atXBeginning").toBool();
}

/// ListView / GridView that snap compute where a flick ends from its velocity themselves.
bool snaps(const QQuickItem *flickable)
{
    // snapMode NoSnap = 0, highlightRangeMode StrictlyEnforceRange = 2 (a plain Flickable has
    // neither: invalid, 0).
    return flickable->property("snapMode").toInt() != 0 || flickable->property("highlightRangeMode").toInt() == 2;
}

bool inBrowser()
{
    static const bool browser = QGuiApplication::platformName() == u"wasm"_s;
    return browser;
}

} // namespace

// ---- SmoothScrollAxis ----------------------------------------------------------------------------

double SmoothScrollAxis::velocityAt(qint64 nowMs) const
{
    if (!m_active)
        return 0;
    const double elapsed = double(nowMs - m_startMs) / 1000.0;
    const double speed = std::abs(m_velocity) - m_deceleration * elapsed;
    return speed > 0 ? sign(m_velocity) * speed : 0;
}

double SmoothScrollAxis::durationFor(qint64 nowMs)
{
    // Firefox (ScrollAnimationBezierPhysics::ComputeDuration): from rest the history says the
    // events came MaxDurationMs / IntervalRatio apart, so the first one takes MaxDurationMs.
    if (!m_active) {
        const auto step = qint64(MaxDurationMs / IntervalRatio);
        m_previous = {nowMs - step, nowMs - 2 * step, nowMs - 3 * step};
    }
    const double interval = double(nowMs - m_previous[2]) / 3.0;
    m_previous = {nowMs, m_previous[0], m_previous[1]};
    return std::clamp(interval * IntervalRatio, MinDurationMs, MaxDurationMs);
}

std::optional<SmoothScrollAxis::Flick> SmoothScrollAxis::add(qint64 nowMs, double position, double delta,
                                                             double lowest, double highest, bool running)
{
    if (!running)
        m_active = false;
    const double current = velocityAt(nowMs);
    const double from = m_active ? m_target : position;
    const double target = std::clamp(from + delta, lowest, std::max(lowest, highest));
    const double durationMs = durationFor(nowMs);
    const double distance = target - position;
    if (std::abs(distance) < 0.5) {
        m_active = false;
        return std::nullopt;
    }
    // Even deceleration to a stop after `distance`: v = 2 d / T, a = v² / 2d.
    double speed = 2 * std::abs(distance) / (durationMs / 1000.0);
    if (sign(current) == sign(distance))
        speed = std::max(speed, std::abs(current));
    Flick flick;
    flick.velocity = sign(distance) * speed;
    flick.deceleration = speed * speed / (2 * std::abs(distance));
    flick.distance = distance;
    flick.durationMs = 2 * std::abs(distance) / speed * 1000.0;
    m_active = true;
    m_target = target;
    m_startMs = nowMs;
    m_velocity = flick.velocity;
    m_deceleration = flick.deceleration;
    return flick;
}

// ---- SmoothScroll --------------------------------------------------------------------------------

SmoothScroll::SmoothScroll(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

SmoothScroll *SmoothScroll::instance()
{
    static QPointer<SmoothScroll> filter;
    if (!filter && QCoreApplication::instance()) {
        filter = new SmoothScroll(QCoreApplication::instance());
        QCoreApplication::instance()->installEventFilter(filter);
    }
    return filter;
}

bool SmoothScroll::switchedOff()
{
    static const bool off = qEnvironmentVariableIsSet("BRULION_SMOOTHSCROLL")
                            && qEnvironmentVariableIntValue("BRULION_SMOOTHSCROLL") == 0;
    return off;
}

bool SmoothScroll::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() != QEvent::Wheel || switchedOff())
        return false;
    auto *item = qobject_cast<QQuickItem *>(watched);
    if (!item || !item->inherits("QQuickFlickable"))
        return false;
    return wheel(item, static_cast<QWheelEvent *>(event));
}

bool SmoothScroll::wheel(QQuickItem *flickable, QWheelEvent *event)
{
    // A touchpad (scroll phases) drags the content with the fingers: Qt's, as in a browser; it
    // also takes over from a smooth scroll still running.
    if (event->phase() != Qt::NoScrollPhase) {
        stopAll(flickable);
        return false;
    }
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
    if (modifiers != Qt::NoModifier && modifiers != Qt::ShiftModifier)
        return false;
    if (!flickable->property("interactive").toBool() || snaps(flickable))
        return false;

    // Content pixels, + towards the content's end (a wheel turned towards the user: angleDelta < 0).
    QPointF delta;
    if (inBrowser() && !event->pixelDelta().isNull()) {
        delta = -QPointF(event->pixelDelta());
    } else {
        if (event->angleDelta().isNull())
            return false;
        const double notch = QGuiApplication::styleHints()->wheelScrollLines() * LinePx;
        delta = -QPointF(event->angleDelta()) / 120.0 * notch;
    }
    // Shift+wheel: sideways, where the view scrolls sideways (a browser's).
    if ((modifiers & Qt::ShiftModifier) && delta.x() == 0 && scrolls(flickable, Qt::Horizontal))
        delta = QPointF(delta.y(), 0);

    bool moved = false;
    for (const Qt::Orientation orientation : {Qt::Vertical, Qt::Horizontal}) {
        const double d = orientation == Qt::Vertical ? delta.y() : delta.x();
        if (d == 0 || !scrolls(flickable, orientation) || atEnd(flickable, orientation, d))
            continue;
        moved |= scroll(flickable, orientation, d);
    }
    if (!moved)
        return false; // Qt's: at an end it passes the event on to the Flickable around this one
    event->accept();
    ++m_wheelEvents;
    Q_EMIT wheelEventsChanged();
    return true;
}

bool SmoothScroll::scrollBy(QQuickItem *flickable, qreal dx, qreal dy)
{
    if (!flickable || !flickable->inherits("QQuickFlickable"))
        return false;
    bool moved = false;
    if (switchedOff() || snaps(flickable)) {
        // A jump, as Qt's scroll bar does.
        for (const Qt::Orientation orientation : {Qt::Vertical, Qt::Horizontal}) {
            const bool vertical = orientation == Qt::Vertical;
            const double d = vertical ? dy : dx;
            if (d == 0 || !scrolls(flickable, orientation))
                continue;
            const char *name = vertical ? "contentY" : "contentX";
            const auto [lowest, highest] = bounds(flickable, orientation);
            const double from = real(flickable, name);
            const double to = std::clamp(from + d, lowest, highest);
            flickable->setProperty(name, to);
            moved |= std::abs(to - from) >= 0.5;
        }
        return moved;
    }
    if (dy != 0 && scrolls(flickable, Qt::Vertical))
        moved |= scroll(flickable, Qt::Vertical, dy);
    if (dx != 0 && scrolls(flickable, Qt::Horizontal))
        moved |= scroll(flickable, Qt::Horizontal, dx);
    return moved;
}

SmoothScroll::Axes &SmoothScroll::axes(QQuickItem *flickable)
{
    auto it = m_axes.find(flickable);
    if (it != m_axes.end())
        return *it;
    // Anything else that flicks the view (a finger, Qt's own wheel, code) or ends its movement
    // leaves the planned motion behind: the next notch starts from where the view is.
    connect(flickable, &QObject::destroyed, this, [this, flickable] { m_axes.remove(flickable); });
    connect(flickable, SIGNAL(flickStarted()), this, SLOT(onFlickStarted()));
    connect(flickable, SIGNAL(movementEnded()), this, SLOT(onMovementEnded()));
    return m_axes[flickable];
}

void SmoothScroll::onFlickStarted()
{
    if (!m_flicking)
        stopAll(qobject_cast<QQuickItem *>(sender()));
}

void SmoothScroll::onMovementEnded()
{
    stopAll(qobject_cast<QQuickItem *>(sender()));
}

void SmoothScroll::stopAll(QQuickItem *flickable)
{
    auto it = m_axes.find(flickable);
    if (it == m_axes.end())
        return;
    it->x.stop();
    it->y.stop();
}

bool SmoothScroll::scroll(QQuickItem *flickable, Qt::Orientation orientation, double delta)
{
    const bool vertical = orientation == Qt::Vertical;
    SmoothScrollAxis &axis = vertical ? axes(flickable).y : axes(flickable).x;
    const qint64 now = m_clock.elapsed();
    const bool running = axis.isActive()
                         && flickable->property(vertical ? "flickingVertically" : "flickingHorizontally").toBool();
    const auto [lowest, highest] = bounds(flickable, orientation);
    const std::optional<SmoothScrollAxis::Flick> flick =
            axis.add(now, real(flickable, vertical ? "contentY" : "contentX"), delta, lowest, highest, running);
    if (!flick)
        return false;
    // Flickable::flick() decelerates by flickDeceleration and caps the velocity at
    // maximumFlickVelocity when it starts (QQuickFlickablePrivate::flick); both are restored at
    // once, the running flick keeps the values it started with. A positive velocity there moves
    // the content towards its beginning.
    const qreal deceleration = real(flickable, "flickDeceleration");
    const qreal maximum = real(flickable, "maximumFlickVelocity");
    const qreal velocity = -flick->velocity;
    flickable->setProperty("flickDeceleration", flick->deceleration);
    if (maximum > 0 && std::abs(velocity) > maximum)
        flickable->setProperty("maximumFlickVelocity", std::abs(velocity) + 1);
    m_flicking = true;
    QMetaObject::invokeMethod(flickable, "flick", Q_ARG(qreal, vertical ? 0 : velocity),
                              Q_ARG(qreal, vertical ? velocity : 0));
    m_flicking = false;
    flickable->setProperty("flickDeceleration", deceleration);
    flickable->setProperty("maximumFlickVelocity", maximum);
    return true;
}

} // namespace brulion::ui
