#include "keyboardinsets.h"

#include "log/categories.h"

#include <QtMath>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

KeyboardInsets *g_instance = nullptr;

bool finite(qreal v)
{
    return std::isfinite(v);
}

/// A finite number above 0.
bool positive(qreal v)
{
    return finite(v) && v > 0;
}

/// A device pixel ratio; an invalid one (not finite, not above 0) counts as 1.
qreal pixelRatio(qreal dpr)
{
    return positive(dpr) ? dpr : 1;
}

// A pinch-zoomed page shrinks the visual viewport too (a desktop touchpad, two fingers on a
// phone): no keyboard. A little above 1: browsers round the scale.
constexpr qreal kMaxScale = 1.01;

/// A sample with a visual viewport (height and scale above 0) of a page that is not pinch-zoomed.
bool usableViewport(const ViewportSample &sample)
{
    return positive(sample.height) && positive(sample.scale) && sample.scale <= kMaxScale;
}

// Two readings of the viewports' geometry the browser build tells apart (it reports changes of
// half a pixel and more).
bool sameGeometry(const ViewportSample &a, const ViewportSample &b)
{
    const auto same = [](qreal x, qreal y) {
        return finite(x) && finite(y) ? std::abs(x - y) < 0.5 : finite(x) == finite(y);
    };
    return same(a.innerWidth, b.innerWidth) && same(a.innerHeight, b.innerHeight) && same(a.height, b.height)
           && same(a.offsetTop, b.offsetTop) && same(a.scale, b.scale) && same(a.containerHeight, b.containerHeight);
}

// One tallest height per width (rounded; malformed widths share one).
int widthKey(qreal innerWidth)
{
    return qRound(finite(innerWidth) ? std::clamp<qreal>(innerWidth, 0, 1e6) : 0);
}

} // namespace

qreal keyboardOverlap(const QRectF &keyboard, bool nativePixels, qreal dpr, const QRect &window, bool visible)
{
    if (!visible || window.isEmpty())
        return 0;
    if (!finite(keyboard.x()) || !finite(keyboard.y()) || keyboard.y() < 0 || !positive(keyboard.width())
        || !positive(keyboard.height()))
        return 0;
    const qreal ratio = nativePixels ? pixelRatio(dpr) : 1;
    const QRectF kb(keyboard.x() / ratio, keyboard.y() / ratio, keyboard.width() / ratio, keyboard.height() / ratio);
    const QRectF win(window); // its right() and bottom() are the edges (QRect's are one pixel short)
    if (kb.right() <= win.left() || kb.left() >= win.right() || kb.bottom() <= win.top()) // beside or above it
        return 0;
    if (kb.top() >= win.bottom()) // below it (the window was resized above the keyboard)
        return 0;
    // Everything from the keyboard's top down counts (also below a keyboard that ends above the
    // window's bottom: what sits on the keyboard goes above its top).
    return std::clamp<qreal>(win.bottom() - std::max(kb.top(), win.top()), 0, win.height());
}

qreal viewportOverlap(const ViewportSample &sample, qreal windowHeight)
{
    if (!usableViewport(sample) || !finite(sample.offsetTop))
        return 0;
    // The container's height: its own, else Qt's window's, else innerHeight.
    const qreal heights[] = {sample.containerHeight, windowHeight, sample.innerHeight};
    const auto container = std::ranges::find_if(heights, positive);
    if (container == std::end(heights))
        return 0;
    const qreal top = std::max<qreal>(0, sample.offsetTop);
    const qreal overlap = std::clamp<qreal>(*container - (top + sample.height), 0, *container);
    return overlap < ViewportKeyboard::MinOverlap ? 0 : overlap;
}

ViewportKeyboard::State ViewportKeyboard::update(const ViewportSample &sample, qreal windowHeight)
{
    // The page the browser shrank itself (interactive-widget=resizes-content): the visual
    // viewport is the whole page, so the keyboard shows as a shrink against the tallest seen.
    bool shrunk = false;
    if (usableViewport(sample)) {
        qreal &tallest = m_tallest[widthKey(sample.innerWidth)];
        tallest = std::max(tallest, sample.height);
        // Qt's <input> focused starts it; a focus passing between Qt's elements (Enter: the next
        // block) does not end it while the page stays shrunk and the samples have not settled. A
        // settled sample without the focus does (the editing is over; a split screen keeps the
        // page shrunk with no keyboard).
        shrunk = sample.touch && (sample.editing || (m_shrunk && !sample.settled))
                 && tallest - sample.height >= MinKeyboard;
    }
    m_shrunk = shrunk;
    if (sample.settled) {
        m_settled = sample;
        m_hasSettled = true;
        m_moving = false;
    } else if (!m_hasSettled || !sameGeometry(sample, m_settled)) {
        m_moving = true;
    }
    const qreal overlap = viewportOverlap(sample, windowHeight);
    return {.overlap = overlap, .shown = shrunk || overlap > 0, .animating = m_moving && !sample.settled};
}

qreal ViewportKeyboard::tallest(qreal innerWidth) const
{
    return m_tallest.value(widthKey(innerWidth), 0);
}

KeyboardInsets::KeyboardInsets(QObject *parent) : QObject(parent)
{
    if (!g_instance)
        g_instance = this;
}

KeyboardInsets::~KeyboardInsets()
{
    if (g_instance == this)
        g_instance = nullptr;
}

KeyboardInsets *KeyboardInsets::instance()
{
    return g_instance;
}

QString KeyboardInsets::sourceName() const
{
    switch (m_source) {
    case Source::InputMethod: return u"inputMethod"_s;
    case Source::Platform: return u"platform"_s;
    case Source::Simulated: return u"simulated"_s;
    case Source::None: break;
    }
    return u"none"_s;
}

qreal KeyboardInsets::toLogical(qreal nativePixels, qreal dpr)
{
    return positive(nativePixels) ? nativePixels / pixelRatio(dpr) : 0;
}

qreal KeyboardInsets::clampOverlap(qreal overlap) const
{
    if (!positive(overlap))
        return 0;
    return m_windowHeight > 0 ? std::min(overlap, m_windowHeight) : overlap;
}

void KeyboardInsets::setSimulated(qreal overlap)
{
    simulate(overlap, false);
}

void KeyboardInsets::simulate(qreal overlap, bool animating, bool hiding)
{
    const qreal value = finite(overlap) && overlap >= 0 ? overlap : -1;
    const bool moving = value >= 0 && animating;
    const bool going = moving && hiding;
    if (value == m_simulated && moving == m_simulatedAnimating && going == m_simulatedHiding)
        return;
    const bool changed = value != m_simulated;
    m_simulated = value;
    m_simulatedAnimating = moving;
    m_simulatedHiding = going;
    if (changed)
        Q_EMIT simulatedChanged();
    update();
}

void KeyboardInsets::setPlatformState(qreal overlap, bool shown, bool animating)
{
    m_hasViewportSample = false;
    m_platformSlides = true;
    applyPlatform(overlap, shown, animating, QString());
}

void KeyboardInsets::setViewportSample(const ViewportSample &sample)
{
    m_viewportSample = sample;
    m_hasViewportSample = true;
    m_platformSlides = false;
    const ViewportKeyboard::State state = m_viewportKeyboard.update(sample, m_windowHeight);
    applyPlatform(state.overlap, state.shown, state.animating,
                  sample.mode.isEmpty() ? u"browser"_s : u"browser, interactive-widget=resizes-%1"_s.arg(sample.mode));
}

void KeyboardInsets::applyPlatform(qreal overlap, bool shown, bool animating, const QString &how)
{
    const bool first = !std::exchange(m_hasPlatform, true);
    m_platform = {clampOverlap(overlap), shown, animating};
    if (first) {
        qCInfo(brulion::logging::lcPlatform).noquote()
                << u"On-screen keyboard: reported by the platform"_s
                           + (how.isEmpty() ? QString() : u" ("_s + how + u")"_s);
    }
    update();
}

void KeyboardInsets::setMarked(bool marked)
{
    if (marked == m_marked)
        return;
    m_marked = marked;
    Q_EMIT markedChanged();
}

void KeyboardInsets::setInputMethodState(qreal overlap, bool shown)
{
    m_inputMethod = {clampOverlap(overlap), shown, false};
    update();
}

void KeyboardInsets::setWindowHeight(qreal height)
{
    const qreal h = positive(height) ? height : 0;
    if (h == m_windowHeight)
        return;
    m_windowHeight = h;
    // The browser's last sample again (without a container height the window is the container);
    // else clamp what is known already (a smaller window after a rotation).
    if (m_hasViewportSample) {
        const ViewportKeyboard::State state = m_viewportKeyboard.update(m_viewportSample, m_windowHeight);
        m_platform = {clampOverlap(state.overlap), state.shown, state.animating};
    } else {
        m_platform.overlap = clampOverlap(m_platform.overlap);
    }
    m_inputMethod.overlap = clampOverlap(m_inputMethod.overlap);
    update();
}

void KeyboardInsets::update()
{
    State next;
    Source source = Source::None;
    bool slides = false;
    if (m_simulated >= 0) {
        next = {clampOverlap(m_simulated), m_simulated > 0 && !m_simulatedHiding, m_simulatedAnimating};
        source = Source::Simulated;
        slides = true;
    } else if (m_hasPlatform) {
        next = m_platform;
        source = Source::Platform;
        slides = m_platformSlides;
    } else if (m_inputMethod.shown || m_inputMethod.overlap > 0) {
        next = m_inputMethod;
        source = Source::InputMethod;
    }

    const State previous = std::exchange(m_current, next);
    const Source previousSource = std::exchange(m_source, source);
    const bool previousSlides = std::exchange(m_slidesOver, slides);
    if (!qFuzzyCompare(1 + previous.overlap, 1 + next.overlap))
        Q_EMIT overlapChanged();
    if (previous.shown != next.shown)
        Q_EMIT shownChanged();
    if (previous.animating != next.animating)
        Q_EMIT animatingChanged();
    if (previousSource != source)
        Q_EMIT sourceChanged();
    if (previousSlides != slides)
        Q_EMIT slidesOverChanged();

    // The log: settled values only (a slide reports every frame).
    if (!next.animating && (std::abs(next.overlap - m_loggedOverlap) >= 1 || next.shown != m_loggedShown)) {
        m_loggedOverlap = next.overlap;
        m_loggedShown = next.shown;
        qCDebug(brulion::logging::lcPlatform).noquote()
                << u"On-screen keyboard %1, overlap %2 px (%3)"_s.arg(next.shown ? u"shown"_s : u"hidden"_s)
                           .arg(qRound(next.overlap))
                           .arg(sourceName());
    }
}

} // namespace brulion::ui
