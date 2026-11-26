#include "ui/touch.h"

#include "ui/div.h"
#include "ui/window.h"

#include <algorithm>
#include <cmath>

namespace brulion::atrament {

// --- VelocityTracker

namespace {

constexpr double kHorizon = 0.100; // s: the samples a velocity is fitted to
constexpr double kAssumeStopped = 0.040; // s: a pause this long means the finger rested

// The slope at t = 0 of the least-squares quadratic through (t[i], x[i]) — t in ms, ≤ 0 — or of
// the line when a quadratic is not determined (two samples, or all at nearly the same times).
double fittedSlope(const double *t, const double *x, int n)
{
    double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, q0 = 0, q1 = 0, q2 = 0;
    for (int i = 0; i < n; ++i) {
        const double ti = t[i];
        const double t2 = ti * ti;
        s0 += 1;
        s1 += ti;
        s2 += t2;
        s3 += t2 * ti;
        s4 += t2 * t2;
        q0 += x[i];
        q1 += x[i] * ti;
        q2 += x[i] * t2;
    }
    if (n >= 3) {
        const double det = s0 * (s2 * s4 - s3 * s3) - s1 * (s1 * s4 - s3 * s2) + s2 * (s1 * s3 - s2 * s2);
        if (std::abs(det) > 1e-9 * s0 * s2 * s4) {
            // Cramer's rule for the linear coefficient.
            const double d1 = s0 * (q1 * s4 - s3 * q2) - q0 * (s1 * s4 - s3 * s2) + s2 * (s1 * q2 - q1 * s2);
            return d1 / det;
        }
    }
    const double den = s0 * s2 - s1 * s1;
    if (std::abs(den) < 1e-9)
        return 0;
    return (s0 * q1 - s1 * q0) / den;
}

} // namespace

void VelocityTracker::add(double seconds, Point position)
{
    m_samples[std::size_t(m_head)] = Sample { seconds, position };
    m_head = (m_head + 1) % kCapacity;
    m_count = std::min(m_count + 1, kCapacity);
}

Point VelocityTracker::velocity() const
{
    if (m_count < 2)
        return {};
    // From the newest back: within the horizon, and no pause between two samples.
    const Sample &newest = at(0);
    double t[kCapacity];
    double x[kCapacity];
    double y[kCapacity];
    int n = 0;
    for (int i = 0; i < m_count; ++i) {
        const Sample &s = at(i);
        if (newest.t - s.t > kHorizon)
            break;
        if (i > 0 && at(i - 1).t - s.t > kAssumeStopped)
            break;
        t[n] = (s.t - newest.t) * 1000.0; // ms: better conditioned sums
        x[n] = double(s.p.x - newest.p.x);
        y[n] = double(s.p.y - newest.p.y);
        ++n;
    }
    if (n < 2)
        return {};
    return { float(fittedSlope(t, x, n) * 1000.0), float(fittedSlope(t, y, n) * 1000.0) };
}

Point VelocityTracker::velocityAtRelease(double seconds) const
{
    if (m_count == 0 || seconds - at(0).t > kAssumeStopped)
        return {};
    return velocity();
}

// --- FlingCurve (the constants of Android's OverScroller)

namespace {

constexpr double kInflexion = 0.35; // where the spline's tension lines cross
constexpr double kStartTension = 0.5;
constexpr double kEndTension = 1.0;
constexpr double kP1 = kStartTension * kInflexion;
constexpr double kP2 = 1.0 - kEndTension * (1.0 - kInflexion);
constexpr double kDecelerationRate = 2.358201815; // ln(0.78) / ln(0.9)
constexpr double kFriction = 0.015; // ViewConfiguration.getScrollFriction()
// Gravity in px/s² at Android's 160 px per inch, as OverScroller computes it for a density of 1.
constexpr double kPhysicalCoeff = 9.80665 * 39.37 * 160.0 * 0.84;

double bezier(double s, double c1, double c2)
{
    const double r = 1 - s;
    return 3 * r * s * (r * c1 + s * c2) + s * s * s;
}

double bezierSlope(double s, double c1, double c2)
{
    const double r = 1 - s;
    return 3 * r * r * c1 + 6 * r * s * (c2 - c1) + 3 * s * s * (1 - c2);
}

// The curve's parameter at the fraction `u` of the duration (its time coordinate is monotonic).
double parameterAt(double u)
{
    double lo = 0;
    double hi = 1;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2;
        if (bezier(mid, kP1, kP2) < u)
            lo = mid;
        else
            hi = mid;
    }
    return (lo + hi) / 2;
}

} // namespace

FlingCurve::FlingCurve(float velocity)
{
    const float speed = std::min(std::abs(velocity), kMaxFlingVelocity);
    if (!(speed >= kMinFlingVelocity))
        return;
    const float sign = velocity < 0 ? -1.0f : 1.0f;
    const double l = std::log(kInflexion * double(speed) / (kFriction * kPhysicalCoeff));
    m_duration = std::exp(l / (kDecelerationRate - 1.0));
    m_distance = sign * float(kFriction * kPhysicalCoeff * std::exp(kDecelerationRate / (kDecelerationRate - 1.0) * l));
    m_velocity = sign * speed;
}

float FlingCurve::positionAt(double seconds) const
{
    if (isEmpty() || seconds <= 0)
        return 0;
    if (seconds >= m_duration)
        return m_distance;
    const double s = parameterAt(seconds / m_duration);
    return float(double(m_distance) * bezier(s, kStartTension, 1.0));
}

float FlingCurve::velocityAt(double seconds) const
{
    if (isEmpty() || seconds >= m_duration)
        return 0;
    if (seconds <= 0)
        return m_velocity;
    const double s = parameterAt(seconds / m_duration);
    const double dt = bezierSlope(s, kP1, kP2);
    if (dt <= 0)
        return 0;
    return float(bezierSlope(s, kStartTension, 1.0) / dt * double(m_distance) / m_duration);
}

// --- Window: the touch gesture
//
// A finger (or a pen) down: inside a container that can scroll, the press waits (kTapTimeout) —
// a drag past the slop before that is a scroll the elements never hear of, a release is a tap
// (the press and the release at once), a finger that stays gives them the press. Outside, they
// have it at once. Past the slop the innermost container under the press that can still move in
// the drag's direction takes the drag (vertical when it is at least as vertical as horizontal),
// for the rest of it — latched, as browsers and Android's lists do: an inner list at its end leaves
// the drag to the container around it only when the drag starts. The press the elements had is
// cancelled (a release over nothing: no click, pressed looks end), the hover goes. The content
// follows the finger from where it passed the slop on; the release flings it with the finger's
// velocity. A finger on a flinging container stops it and drags it on at once, a press no element
// sees. A drag past the slop nothing scrolls stays the elements' for good (a slider's thumb, a
// sideways drag over a vertical list). A long press in the focused field — where Android's input
// method selects a word — makes the finger scroll nothing more.

namespace {

constexpr float kScrollEpsilon = 0.5f; // less is no room to scroll

} // namespace

bool Window::isTouchInput(const PlatformInput &input) const
{
    if (const auto *d = std::get_if<MouseDownEvent>(&input))
        return d->pointer != PointerKind::Mouse && d->button == MouseButton::Left;
    if (const auto *u = std::get_if<MouseUpEvent>(&input))
        return u->pointer != PointerKind::Mouse && u->button == MouseButton::Left;
    if (const auto *m = std::get_if<MouseMoveEvent>(&input)) {
        // A finger is over nothing unless it touches (no hover from a touch screen); a pen above
        // the screen hovers as a mouse does.
        return m->pointer == PointerKind::Touch
                || (m->pointer == PointerKind::Pen && m_touch.phase != TouchPhaseState::None);
    }
    return false;
}

bool Window::dispatchTouch(const PlatformInput &input)
{
    if (const auto *d = std::get_if<MouseDownEvent>(&input))
        return touchDown(*d);
    if (const auto *m = std::get_if<MouseMoveEvent>(&input))
        return touchMove(*m);
    if (const auto *u = std::get_if<MouseUpEvent>(&input))
        return touchUp(*u);
    return false;
}

double Window::touchSeconds(EventTime time) const
{
    if (time.count() != 0)
        return double(time.count()) / 1000.0;
    return std::chrono::duration<double>(m_app.platform().now().time_since_epoch()).count();
}

std::vector<Window::ScrollTarget> Window::scrollTargetsAt(Point position) const
{
    std::vector<ScrollTarget> out;
    const Frame &f = rendered();
    for (HitboxId h : hitsAt(f, position)) {
        for (const ScrollTarget &t : f.scrollTargets) {
            if (t.hitbox == h)
                out.push_back(t);
        }
    }
    return out;
}

ScrollState *Window::liveScrollTarget(ScrollState *state) const
{
    // Still in the frame on screen: element state of a container not drawn any more is gone.
    if (!state)
        return nullptr;
    for (const ScrollTarget &t : rendered().scrollTargets) {
        if (t.state == state)
            return state;
    }
    return nullptr;
}

bool Window::touchDown(const MouseDownEvent &down)
{
    if (m_touch.phase != TouchPhaseState::None) {
        // A press without the release before it (the platform lost one): that one ends here.
        cancelTouchPress();
        endTouch();
    }
    ScrollState *caught = nullptr;
    bool caughtX = false;
    bool caughtY = false;
    if (m_fling.active) {
        ScrollState *flung = liveScrollTarget(m_fling.target);
        for (const ScrollTarget &t : scrollTargetsAt(down.position)) {
            if (t.state == flung) {
                caught = flung;
                caughtX = t.x;
                caughtY = t.y;
                break;
            }
        }
        stopFling();
    }
    m_touch.down = down;
    m_touch.last = down.position;
    m_touch.velocity.add(touchSeconds(down.time), down.position);
    m_touchScrolled = false;
    if (caught) {
        m_touch.phase = TouchPhaseState::Scrolling;
        m_touch.decided = true;
        m_touch.target = caught;
        m_touch.x = caughtX;
        m_touch.y = caughtY;
        m_touchScrolled = true;
        return true;
    }
    m_touch.longPressTimer
            = m_app.setTimeout(m_app.platform().longPressInterval(), [this](App &) { touchLongPressed(); });
    bool scrollable = false;
    for (const ScrollTarget &t : scrollTargetsAt(down.position)) {
        const Point max = t.state->maxOffset();
        scrollable = scrollable || (t.x && max.x > kScrollEpsilon) || (t.y && max.y > kScrollEpsilon);
    }
    if (scrollable) {
        m_touch.phase = TouchPhaseState::Waiting;
        m_touch.pressTimer = m_app.setTimeout(kTapTimeout, [this](App &) { deliverTouchPress(); });
        return true;
    }
    m_touch.phase = TouchPhaseState::Pressed;
    dispatchMouse(down);
    return true;
}

void Window::deliverTouchPress()
{
    if (m_touch.phase != TouchPhaseState::Waiting)
        return;
    m_touch.phase = TouchPhaseState::Pressed;
    m_touch.pressTimer = {};
    m_defaultPrevented = false;
    dispatchMouse(m_touch.down);
}

void Window::touchLongPressed()
{
    if (m_touch.phase != TouchPhaseState::Waiting && m_touch.phase != TouchPhaseState::Pressed)
        return;
    deliverTouchPress();
    // Android's input context selects the word under a long press in the focused field (the press
    // focused it): the finger moving on must not scroll the selection and its handles away.
    InputHandler *handler = inputHandler();
    const std::optional<Rect> field = handler ? handler->inputBounds() : std::nullopt;
    if (field && field->contains(m_touch.down.position)) {
        m_touch.phase = TouchPhaseState::Held;
        m_touch.decided = true;
    }
}

bool Window::touchMove(const MouseMoveEvent &move)
{
    if (m_touch.phase == TouchPhaseState::None)
        return false;
    const Point previous = m_touch.last;
    m_touch.last = move.position;
    m_touch.velocity.add(touchSeconds(move.time), move.position);
    if (m_touch.phase == TouchPhaseState::Scrolling) {
        // The content follows the finger, 1:1.
        if (ScrollState *st = liveScrollTarget(m_touch.target)) {
            Point d = previous - move.position;
            if (!m_touch.x)
                d.x = 0;
            if (!m_touch.y)
                d.y = 0;
            if (st->scrollBy(d))
                refresh();
        }
        return true;
    }
    if (m_touch.phase == TouchPhaseState::Held)
        return dispatchMouse(move);
    if (!m_touch.decided) {
        const Point dragged = move.position - m_touch.down.position;
        if (std::max(std::abs(dragged.x), std::abs(dragged.y)) > m_app.platform().touchSlop()) {
            m_touch.decided = true;
            if (beginTouchScroll(dragged))
                return true;
            deliverTouchPress(); // nothing scrolls that way: the elements' drag
        }
    }
    if (m_touch.phase == TouchPhaseState::Waiting)
        return true; // no element has the press to drag yet
    return dispatchMouse(move);
}

bool Window::beginTouchScroll(Point dragged)
{
    const bool vertical = std::abs(dragged.y) >= std::abs(dragged.x);
    for (const ScrollTarget &t : scrollTargetsAt(m_touch.down.position)) {
        const Point offset = t.state->offset;
        const Point max = t.state->maxOffset();
        // The content moves against the finger: a finger going up scrolls towards the end.
        const bool takes = vertical
                ? t.y && (dragged.y < 0 ? max.y - offset.y > kScrollEpsilon : offset.y > kScrollEpsilon)
                : t.x && (dragged.x < 0 ? max.x - offset.x > kScrollEpsilon : offset.x > kScrollEpsilon);
        if (!takes)
            continue;
        cancelTouchPress();
        m_touch.phase = TouchPhaseState::Scrolling;
        m_touch.target = t.state;
        m_touch.x = t.x;
        m_touch.y = t.y;
        m_touch.pressTimer = {};
        m_touch.longPressTimer = {};
        m_touchScrolled = true;
        touchHoverOff();
        // From where the drag passed the slop on: the content does not jump by the slop.
        const float slop = m_app.platform().touchSlop();
        const float travelled = std::max(std::abs(dragged.x), std::abs(dragged.y));
        Point d = dragged * (-(travelled - slop) / travelled);
        if (!t.x)
            d.x = 0;
        if (!t.y)
            d.y = 0;
        if (t.state->scrollBy(d))
            refresh();
        if (m_touchTrace)
            m_touchTrace(TouchTrace { TouchTrace::Kind::Scroll, m_touch.last, t.state->offset, {}, {}, 0, 0 });
        return true;
    }
    return false;
}

void Window::cancelTouchPress()
{
    // Waiting: the elements never had the press.
    if (m_touch.phase != TouchPhaseState::Pressed && m_touch.phase != TouchPhaseState::Held)
        return;
    MouseUpEvent cancel;
    cancel.position = m_touch.last;
    cancel.modifiers = m_touch.down.modifiers;
    cancel.pointer = m_touch.down.pointer;
    cancel.cancelled = true;
    m_defaultPrevented = false;
    dispatchMouse(cancel);
}

void Window::touchHoverOff()
{
    // A finger is over nothing once it scrolls or lifts: no hover look follows the content under
    // it, or stays behind after a tap.
    m_defaultPrevented = false;
    dispatchMouse(MouseExitEvent { m_touch.last, m_modifiers });
}

void Window::endTouch()
{
    m_touch = TouchGesture {};
}

bool Window::touchUp(const MouseUpEvent &up)
{
    if (m_touch.phase == TouchPhaseState::None)
        return false;
    m_touch.pressTimer = {};
    m_touch.longPressTimer = {};
    const double t = touchSeconds(up.time);
    const Point previous = m_touch.last;
    if (!(up.position == previous)) // a last stretch the moves did not show
        m_touch.velocity.add(t, up.position);
    m_touch.last = up.position;
    if (up.cancelled) {
        cancelTouchPress();
    } else if (m_touch.phase == TouchPhaseState::Scrolling) {
        if (ScrollState *st = liveScrollTarget(m_touch.target)) {
            Point d = previous - up.position;
            if (!m_touch.x)
                d.x = 0;
            if (!m_touch.y)
                d.y = 0;
            if (st->scrollBy(d))
                refresh();
            const Point v = m_touch.velocity.velocityAtRelease(t);
            startFling(st, m_touch.x, m_touch.y, -v);
            if (m_touchTrace) {
                TouchTrace trace { TouchTrace::Kind::Release, up.position, st->offset, v, {}, 0, 0 };
                if (m_fling.active) {
                    trace.distance = { m_fling.x.distance(), m_fling.y.distance() };
                    trace.seconds = std::max(m_fling.x.duration(), m_fling.y.duration());
                }
                m_touchTrace(trace);
            }
        }
    } else {
        if (m_touch.phase == TouchPhaseState::Waiting) {
            // A tap: the press and the release at once.
            m_touch.phase = TouchPhaseState::Pressed;
            m_defaultPrevented = false;
            dispatchMouse(m_touch.down);
        }
        m_defaultPrevented = false;
        dispatchMouse(up);
    }
    touchHoverOff();
    endTouch();
    return true;
}

void Window::startFling(ScrollState *state, bool x, bool y, Point velocity)
{
    m_fling = {};
    const FlingCurve fx = x ? FlingCurve(velocity.x) : FlingCurve();
    const FlingCurve fy = y ? FlingCurve(velocity.y) : FlingCurve();
    if (fx.isEmpty() && fy.isEmpty())
        return;
    m_fling.active = true;
    m_fling.target = state;
    m_fling.x = fx;
    m_fling.y = fy;
    m_fling.start = m_app.platform().now();
    if (m_platform)
        m_platform->requestAnimationFrame();
}

void Window::stopFling()
{
    if (m_fling.active && m_touchTrace) {
        const ScrollState *st = liveScrollTarget(m_fling.target);
        const double ran = std::chrono::duration<double>(m_app.platform().now() - m_fling.start).count();
        m_touchTrace(TouchTrace { TouchTrace::Kind::FlingEnd, m_touch.last, st ? st->offset : Point {}, {}, {}, ran,
                m_fling.frames });
    }
    m_fling = {};
}

void Window::stepFling()
{
    ScrollState *st = liveScrollTarget(m_fling.target);
    if (!st) {
        stopFling();
        return;
    }
    const double t = std::chrono::duration<double>(m_app.platform().now() - m_fling.start).count();
    const Point want { m_fling.x.positionAt(t), m_fling.y.positionAt(t) };
    // By what the curve moved since the last frame: the container may have moved otherwise too
    // (the wheel, a revealed caret).
    const Point delta = want - m_fling.travelled;
    const Point before = st->offset;
    st->scrollBy(delta);
    const Point applied = st->offset - before;
    m_fling.travelled = want;
    ++m_fling.frames;
    // An edge stops it on its axis: no bounce, no glow.
    if (std::abs(applied.x - delta.x) > 0.01f)
        m_fling.x = {};
    if (std::abs(applied.y - delta.y) > 0.01f)
        m_fling.y = {};
    const bool running = (!m_fling.x.isEmpty() && t < m_fling.x.duration())
            || (!m_fling.y.isEmpty() && t < m_fling.y.duration());
    if (!running)
        stopFling();
}

} // namespace brulion::atrament
