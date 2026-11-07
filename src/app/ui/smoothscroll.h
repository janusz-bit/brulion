// SmoothScroll — the mouse wheel and the scroll bar's paging move every Flickable (ListView,
// GridView…) the way a web browser's smooth scrolling does, instead of Qt 6.11's wheel flicks.
// docs/UI.md "Wheel and scroll bar: SmoothScroll"; tests: tests/app/smoothscroll.
//
// Qt 6.11 turns each notch of a wheel into a short flick (about 100 ms) whose velocity is averaged
// from the time between the notches and capped (QQuickFlickable::wheelEvent): how far the content
// goes depends on how fast the wheel turns — 6 notches 150 ms apart move 420 px, 40 ms apart
// 284 px, 10 ms apart 118 px instead of 6 × 72 (tst_smooth_scroll qtWheelLosesNotches). A browser (Firefox's
// ScrollAnimationBezierPhysics, Chromium's wheel animations) moves the same distance for every
// notch, eases into it within 50–200 ms, adds the next notch to where the running animation is
// heading (no distance is lost or gained however fast the wheel turns) and stops there.
//
// SmoothScroll is an application event filter (installed once by main()): a wheel event that Qt
// Quick delivers to an interactive Flickable — after the items above it declined it, as before:
// a zoom on Ctrl+wheel, a WheelHandler — moves that Flickable through its own flick() with the
// velocity and deceleration of the planned motion (SmoothScrollAxis). So the view is `moving`
// and `flicking` as with Qt's wheel (the scroll bar shows, movementStarted / movementEnded, a
// ListView fills its cache ahead), a press, a drag or a new position (positionViewAtIndex, the
// scroll bar dragged) stops it as it stops any flick, and it never runs past the content's ends.
// Left to Qt: touchpad scrolling (scroll phases: it follows the fingers 1:1, as in a browser),
// Ctrl / Alt / Meta + wheel, views that snap (snapMode, StrictlyEnforceRange) and a view already
// at its end in that direction (the event goes on to the Flickable around it, as with Qt).
// Shift+wheel scrolls sideways where the view can, as in a browser.
//
// One notch (120 eighths of a degree) is wheelScrollLines() (the system's setting, 3 by default)
// lines of LinePx: 57 px, Firefox's 3 lines of a 16 px font and Chromium's 53 px on Linux. In the
// browser build the browser's own distance (the event's pixelDelta) is used.
//
// BRULION_SMOOTHSCROLL=0 in the environment turns it off (Qt's wheel flicks, A/B comparisons).
#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <optional>

class QQuickItem;
class QWheelEvent;

namespace brulion::ui {

/// One axis of a smooth scroll: where it is heading, and the flick that gets it there (pure).
/// Positions are content positions (contentY / contentX: + scrolls towards the content's end),
/// times in ms, velocities in px/s.
///
/// The motion of a flick decelerates evenly to a stop at the target (an ease-out, as Firefox's
/// bezier(0, 0, 0.6, 1) at rest). Its duration follows the rate of the events, as Firefox's: twice
/// the mean of the last three intervals, within [MinDurationMs, MaxDurationMs] — a single notch
/// takes 200 ms, a fast-turning wheel gets snappier. A notch during a flick adds to its target and
/// never slows it down (the new flick starts at least as fast as the running one).
class SmoothScrollAxis {
public:
    static constexpr double MinDurationMs = 50;
    static constexpr double MaxDurationMs = 200;
    static constexpr double IntervalRatio = 2;

    struct Flick {
        double velocity = 0;     // signed: + moves the position up
        double deceleration = 0; // > 0
        double distance = 0;     // signed, target − position
        double durationMs = 0;
    };

    /// Adds `delta` (px) at `nowMs` to the scroll of a view at `position` whose position stays
    /// within [lowest, highest]. `running`: the flick of the last add() still moves the view (else
    /// the scroll starts from `position`, at rest). nullopt: nothing to move (already there).
    std::optional<Flick> add(qint64 nowMs, double position, double delta, double lowest, double highest,
                             bool running);
    /// The view stopped (or something else moved it): the next add() starts at rest.
    void stop() { m_active = false; }

    bool isActive() const { return m_active; }
    double target() const { return m_target; }
    /// The velocity of the planned motion at `nowMs` (signed px/s; 0 when not active or stopped).
    double velocityAt(qint64 nowMs) const;

private:
    double durationFor(qint64 nowMs);

    bool m_active = false;
    double m_target = 0;
    qint64 m_startMs = 0;
    double m_velocity = 0;
    double m_deceleration = 0;
    std::array<qint64, 3> m_previous{}; // the last events' times, newest first
};

/// The filter (one per application) and the QML singleton "SmoothScroll" (src/app/ui/qmlforeign.h).
class SmoothScroll : public QObject {
    Q_OBJECT

    /// Wheel events it moved a view for (diagnostics, tests).
    Q_PROPERTY(int wheelEvents READ wheelEvents NOTIFY wheelEventsChanged FINAL)

public:
    /// The scroll distance of one line: a notch scrolls QStyleHints::wheelScrollLines() of them.
    static constexpr double LinePx = 19;

    /// The instance, installed on the application on first use (null without an application).
    static SmoothScroll *instance();
    /// BRULION_SMOOTHSCROLL=0 in the environment: the filter lets every wheel event through.
    static bool switchedOff();

    /// Scrolls `flickable` by (dx, dy) px, smoothly as the wheel does (the scroll bar's paging),
    /// within its content. False when it is no Flickable or nothing moved (already at that end).
    Q_INVOKABLE bool scrollBy(QQuickItem *flickable, qreal dx, qreal dy);

    int wheelEvents() const { return m_wheelEvents; }

Q_SIGNALS:
    void wheelEventsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private Q_SLOTS:
    void onFlickStarted();
    void onMovementEnded();

private:
    explicit SmoothScroll(QObject *parent);

    struct Axes {
        SmoothScrollAxis x;
        SmoothScrollAxis y;
    };

    bool wheel(QQuickItem *flickable, QWheelEvent *event);
    /// Adds `delta` on one axis; true when the view moves.
    bool scroll(QQuickItem *flickable, Qt::Orientation orientation, double delta);
    Axes &axes(QQuickItem *flickable);
    void stopAll(QQuickItem *flickable);

    QHash<QQuickItem *, Axes> m_axes;
    QElapsedTimer m_clock;
    bool m_flicking = false; // inside its own flick() call
    int m_wheelEvents = 0;
};

} // namespace brulion::ui
