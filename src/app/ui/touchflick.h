// TouchFlick — corrects two Qt 6.11 touch-scrolling faults of Flickable / ListView on touch
// screens that do not report velocities (Android's, QInputDevice::Capability::Velocity unset).
// docs/UI.md "Scrolling performance"; tests: tests/app/touch.
//
//   ListView { TouchFlick.enabled: true }
//
// 1. Fling at 0.7x the finger's speed. For such devices QGuiApplication copies the platform's
//    velocity (0) into the persistent touch point and then filters 0.7 * new + 0.3 * that 0
//    (QMutableEventPoint::update / setTimestamp, qeventpoint.cpp); Flickable averages those
//    samples, so every touch fling starts at exactly 0.7x the finger's speed (half the
//    distance). TouchFlick records the finger's own samples and, when the release starts a
//    flick, restarts it (Flickable::flick) scaled by finger speed / Qt's estimate. Only when the
//    samples show that signature (each sample exactly 0.7x its instantaneous value), so it does
//    nothing for mouse, wheel, touchpad, devices that report velocities or a fixed Qt; Qt's own
//    decisions (whether to flick, the multi-flick boost, halving at the ends) stay, and
//    maximumFlickVelocity still caps the result.
// 2. Jump when a drag starts during a fling ("catching" it) on a child item (a block's text, a
//    row with a TapHandler or MouseArea): Flickable skips the first move it gets and applies it
//    together with the second one. TouchFlick moves the content with the finger itself (before
//    Qt Quick sees each move) until Flickable has handled a move — it is dragging then; its
//    second move puts the content where Flickable computes it from the press anyway, so nothing
//    else changes. Every raw move, not only the first: with touch compression (Qt Quick's
//    default) the moves that arrive within one frame reach Flickable as one, which it skips
//    whole. Where Flickable does move with its first move (a press on the Flickable itself
//    during a fast fling), it sets the same position in the same event.
//
// Side effects of 2 (moving the content with Flickable's contentY while it is pressed): Flickable
// emits flickEnded at the first move (QQuickFlickable::setContentY ends the movement it thinks is
// running), and once more at the release when no new fling follows; and contentY changes while
// neither dragging nor flicking (as Qt's own catch on the Flickable itself does). Nothing in the
// app listens to flickStarted/flickEnded; the cache refills on "contentY decreasing while not
// dragging" (BlockEditor, JournalsView) may run once more per upward catch.
// Not supported: ListView/GridView with a snapMode or StrictlyEnforceRange — they compute where a
// flick ends when it starts, so a restarted flick would not go further (2 still applies).
#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVector2D>
#include <QtQml/qqmlregistration.h>

#include <array>
#include <optional>

class QQuickItem;
class QWindow;

namespace brulion::ui {

/// The samples of one finger stroke and the velocities derived from them (positions in scene
/// pixels, times in ms, velocities in px/s). Pure; a small ring of the newest samples.
class TouchVelocityEstimator {
public:
    /// The finger's velocity is the time-weighted mean over the newest samples spanning at least
    /// MinSegments segments and MinSpanMs (60 Hz: the 3 that Flickable averages; faster screens
    /// and jittery timestamps: more), at most MaxSpanMs.
    static constexpr int MinSegments = 3;
    static constexpr qint64 MinSpanMs = 40;
    static constexpr qint64 MaxSpanMs = 100;
    /// Flickable averages the velocities of the last 3 moves (QML_FLICK_SAMPLEBUFFER).
    static constexpr int QtSamples = 3;
    /// Qt 6.11's filter for devices without velocities: reported = Gain * instantaneous.
    static constexpr float QtGain = 0.7f;

    void press(qint64 timeMs, QPointF pos);
    /// A move with the velocity Qt computed for it (QEventPoint::velocity()).
    void move(qint64 timeMs, QPointF pos, QVector2D reported);
    /// The release: a sample only where the finger moved since the last one (a release at the
    /// same position carries no new information, it would only add a zero-velocity segment).
    void release(qint64 timeMs, QPointF pos);
    void clear() { m_count = 0; }

    int size() const { return m_count; }
    /// The finger's velocity at the end of the stroke; nullopt with too few or too old samples.
    std::optional<QVector2D> fingerVelocity() const;
    /// The mean of the velocities Qt reported for the last QtSamples moves, each clamped to
    /// ±maxVelocity like Flickable does (maxVelocity <= 0: no clamp); nullopt without moves.
    std::optional<QVector2D> reportedVelocity(qreal maxVelocity) const;
    /// True when the newest moves (at least 2, up to QtSamples) were each reported at exactly
    /// QtGain x their instantaneous velocity: Qt's filter restarting from 0 on every event.
    bool hasQtDampedSamples() const;

private:
    struct Sample {
        qint64 t = 0;
        QPointF pos;
        QVector2D reported;
        QVector2D instant; // from the previous sample (moves only)
        bool move = false;
    };
    static constexpr int Capacity = 24;
    const Sample &at(int i) const { return m_samples[size_t((m_head + i) % Capacity)]; }
    Sample &at(int i) { return m_samples[size_t((m_head + i) % Capacity)]; }
    void append(const Sample &s);

    std::array<Sample, Capacity> m_samples{};
    int m_head = 0;
    int m_count = 0;
};

/// Attached to a Flickable (ListView, GridView…): `TouchFlick.enabled: true`. Not a Flickable:
/// does nothing (warns once).
class TouchFlick : public QObject {
    Q_OBJECT
    // The attached type is resolved on this class (QQmlPrivate::QmlAttached of the registered
    // type), so it carries the macro; the app registers it as "TouchFlick" (src/app/ui/qmlforeign.h).
    QML_ATTACHED(brulion::ui::TouchFlick)

    /// Corrects this Flickable's touch flicks (default true).
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY enabledChanged FINAL)
    /// Flicks restarted with the corrected velocity (diagnostics, tests).
    Q_PROPERTY(int corrections READ corrections NOTIFY correctionsChanged FINAL)
    /// The factor of the latest correction (1: none yet).
    Q_PROPERTY(qreal lastFactor READ lastFactor NOTIFY correctionsChanged FINAL)
    /// Drags started during a fling whose first moves TouchFlick applied (diagnostics, tests).
    Q_PROPERTY(int catches READ catches NOTIFY catchesChanged FINAL)

public:
    explicit TouchFlick(QObject *flickable);
    ~TouchFlick() override;

    static TouchFlick *qmlAttachedProperties(QObject *object);
    /// BRULION_TOUCHFLICK=0 in the environment turns every TouchFlick off (A/B measurements;
    /// Android: the debuggable APK's extraenvvars).
    static bool switchedOff();

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    int corrections() const { return m_corrections; }
    qreal lastFactor() const { return m_lastFactor; }
    int catches() const { return m_catches; }

    // Called by the touch tracker (an application event filter, before Qt Quick delivers the
    // event). touchPressed: true when the press caught a fling of this view; then touchMoved for
    // each move of the stroke while it returns true; touchCatchEnded when the stroke ends.
    bool touchPressed(const QWindow *window, QPointF scenePos);
    bool touchMoved(const QWindow *window, QPointF scenePos);
    void touchCatchEnded();

Q_SIGNALS:
    void enabledChanged();
    void correctionsChanged();
    void catchesChanged();

private Q_SLOTS:
    void onFlickStarted();

private:
    bool isFlickable() const;
    bool snaps() const;
    QVector2D toLocal(QVector2D sceneVector) const;

    QPointer<QQuickItem> m_flickable;
    bool m_enabled = true;
    int m_corrections = 0;
    qreal m_lastFactor = 1;
    int m_catches = 0;
    // A press that caught a fling: where, the content position when it came, and the position
    // TouchFlick last gave the content (counted once per stroke in `catches`).
    bool m_catchPending = false;
    bool m_catchCounted = false;
    QPointF m_catchPress;
    qreal m_catchContentY = 0;
    qreal m_catchApplied = 0;
};

} // namespace brulion::ui
