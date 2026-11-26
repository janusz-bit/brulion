#pragma once

// Touch scrolling's physics (docs/ATRAMENT.md "Touch"): how fast a finger moved when it let go,
// and the fling after it, on the curve of Android's OverScroller, so that a list glides as the
// platform's own lists do. The gesture itself — slop, which container takes the drag, taps that
// wait, the frames of a fling — is the window's (Window::dispatchTouch, ui/touch.cpp).

#include "base/geometry.h"

#include <array>
#include <chrono>

namespace brulion::atrament {

// Android's ViewConfiguration, in logical px (dp there): flings slower than the first do not
// start, faster ones are capped at the second.
inline constexpr float kMinFlingVelocity = 50; // px/s
inline constexpr float kMaxFlingVelocity = 8000; // px/s
// A press inside a container that can scroll waits this long before the elements see it (Android's
// tap timeout): a drag that starts sooner is a scroll they never hear of, a tap is a press and a
// release at once.
inline constexpr std::chrono::milliseconds kTapTimeout { 100 };

// The velocity of a finger from its last positions: a least-squares quadratic over the last
// 100 ms, its slope at the newest sample (Android's and Flutter's VelocityTracker), zero once the
// finger rested 40 ms (between two samples, or before letting go).
class VelocityTracker {
public:
    void reset() { m_count = 0; }
    // A position at `seconds` (any epoch; samples come in time order).
    void add(double seconds, Point position);
    // px/s at the newest sample.
    Point velocity() const;
    // The velocity when the finger lifts at `seconds`: zero if it rested since the last move.
    Point velocityAtRelease(double seconds) const;
    int count() const { return m_count; }

private:
    struct Sample {
        double t = 0;
        Point p;
    };
    static constexpr int kCapacity = 20;
    const Sample &at(int i) const { return m_samples[std::size_t((m_head + kCapacity - 1 - i) % kCapacity)]; }
    std::array<Sample, kCapacity> m_samples {};
    int m_head = 0; // where the next sample goes
    int m_count = 0;
};

// One axis of a fling (Android's OverScroller.SplineOverScroller): the release velocity gives a
// distance and a duration — both growing faster than the velocity, so a hard flick crosses a long
// list — and the position follows Android's spline from that velocity down to rest, the cubic
// Bézier with time (0, 0.175, 0.35, 1) and position (0, 0.5, 1, 1).
class FlingCurve {
public:
    FlingCurve() = default;
    explicit FlingCurve(float velocity); // px/s, signed; |v| < kMinFlingVelocity gives no fling
    bool isEmpty() const { return m_duration <= 0; }
    float distance() const { return m_distance; } // signed px
    double duration() const { return m_duration; } // seconds
    // The signed distance travelled after `seconds` (the whole distance from duration() on).
    float positionAt(double seconds) const;
    // px/s after `seconds`: the release velocity at 0, zero at the end.
    float velocityAt(double seconds) const;

private:
    float m_velocity = 0;
    float m_distance = 0;
    double m_duration = 0;
};

} // namespace brulion::atrament
