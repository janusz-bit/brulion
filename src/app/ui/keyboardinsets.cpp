#include "keyboardinsets.h"

#include "log/categories.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

KeyboardInsets *g_instance = nullptr;

bool finite(qreal v)
{
    return std::isfinite(v);
}

// A pinch-zoomed page shrinks the visual viewport too (a desktop touchpad, two fingers on a
// phone): no keyboard. A little above 1: browsers round the scale.
constexpr qreal kMaxScale = 1.01;

// Two readings of the viewports' geometry the browser build tells apart (it reports changes of
// half a pixel and more).
bool sameGeometry(const ViewportSample &a, const ViewportSample &b)
{
    const auto same = [](qreal x, qreal y) { return finite(x) && finite(y) ? std::abs(x - y) < 0.5 : finite(x) == finite(y); };
    return same(a.innerWidth, b.innerWidth) && same(a.innerHeight, b.innerHeight) && same(a.height, b.height)
        && same(a.offsetTop, b.offsetTop) && same(a.scale, b.scale) && same(a.containerHeight, b.containerHeight);
}

} // namespace

qreal keyboardOverlap(const QRectF &keyboard, bool nativePixels, qreal dpr, const QRect &window, bool visible)
{
    if (!visible || window.isEmpty())
        return 0;
    if (!finite(keyboard.x()) || !finite(keyboard.y()) || !finite(keyboard.width()) || !finite(keyboard.height()))
        return 0;
    if (keyboard.width() <= 0 || keyboard.height() <= 0 || keyboard.y() < 0)
        return 0;
    const qreal ratio = nativePixels && finite(dpr) && dpr > 0 ? dpr : 1;
    const QRectF kb = nativePixels ? QRectF(keyboard.x() / ratio, keyboard.y() / ratio, keyboard.width() / ratio,
                                            keyboard.height() / ratio)
                                   : keyboard;
    // QRect's right()/bottom() are one pixel short of the edge: use x + width, y + height.
    const qreal left = window.x();
    const qreal right = window.x() + window.width();
    const qreal top = window.y();
    const qreal bottom = window.y() + window.height();
    if (kb.right() <= left || kb.left() >= right || kb.bottom() <= top) // beside or above the window
        return 0;
    if (kb.top() >= bottom) // below it (the window was resized above the keyboard)
        return 0;
    // Everything from the keyboard's top down counts (also below a keyboard that ends above the
    // window's bottom: what sits on the keyboard goes above its top).
    return std::clamp<qreal>(bottom - std::max(kb.top(), top), 0, window.height());
}

qreal viewportOverlap(const ViewportSample &sample, qreal windowHeight)
{
    if (!finite(sample.height) || sample.height <= 0 || !finite(sample.offsetTop) || !finite(sample.scale)
        || sample.scale <= 0 || sample.scale > kMaxScale)
        return 0;
    const qreal container = finite(sample.containerHeight) && sample.containerHeight > 0 ? sample.containerHeight
        : finite(windowHeight) && windowHeight > 0                                        ? windowHeight
        : finite(sample.innerHeight) && sample.innerHeight > 0                            ? sample.innerHeight
                                                                                          : 0;
    if (container <= 0)
        return 0;
    const qreal top = std::max<qreal>(0, sample.offsetTop);
    const qreal overlap = std::clamp<qreal>(container - (top + sample.height), 0, container);
    return overlap < ViewportKeyboard::MinOverlap ? 0 : overlap;
}

ViewportKeyboard::State ViewportKeyboard::update(const ViewportSample &sample, qreal windowHeight)
{
    State state;
    state.overlap = viewportOverlap(sample, windowHeight);
    // The page the browser shrank itself (interactive-widget=resizes-content): the visual
    // viewport is the whole page, so the keyboard shows as a shrink against the tallest seen.
    const bool usable = finite(sample.height) && sample.height > 0 && finite(sample.scale) && sample.scale > 0
        && sample.scale <= kMaxScale;
    if (usable) {
        qreal &tallest = m_tallest[qRound(finite(sample.innerWidth) ? std::clamp<qreal>(sample.innerWidth, 0, 1e6) : 0)];
        tallest = std::max(tallest, sample.height);
        if (sample.touch && sample.editing && tallest - sample.height >= MinKeyboard)
            state.shown = true;
    }
    if (state.overlap > 0)
        state.shown = true;
    if (sample.settled) {
        m_settled = sample;
        m_hasSettled = true;
        m_moving = false;
    } else if (!m_hasSettled || !sameGeometry(sample, m_settled)) {
        m_moving = true;
    }
    state.animating = m_moving && !sample.settled;
    return state;
}

qreal ViewportKeyboard::tallest(qreal innerWidth) const
{
    return m_tallest.value(qRound(finite(innerWidth) ? std::clamp<qreal>(innerWidth, 0, 1e6) : 0), 0);
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
    if (!finite(nativePixels) || nativePixels <= 0)
        return 0;
    const qreal ratio = finite(dpr) && dpr > 0 ? dpr : 1;
    return nativePixels / ratio;
}

qreal KeyboardInsets::clampOverlap(qreal overlap) const
{
    if (!finite(overlap) || overlap <= 0)
        return 0;
    return m_windowHeight > 0 ? std::min(overlap, m_windowHeight) : overlap;
}

void KeyboardInsets::setSimulated(qreal overlap)
{
    simulate(overlap, false);
}

void KeyboardInsets::simulate(qreal overlap, bool animating)
{
    const qreal value = finite(overlap) && overlap >= 0 ? overlap : -1;
    const bool moving = value >= 0 && animating;
    if (value == m_simulated && moving == m_simulatedAnimating)
        return;
    const bool changed = value != m_simulated;
    m_simulated = value;
    m_simulatedAnimating = moving;
    if (changed)
        Q_EMIT simulatedChanged();
    update();
}

void KeyboardInsets::setPlatformState(qreal overlap, bool shown, bool animating)
{
    m_hasViewportSample = false;
    applyPlatform(overlap, shown, animating, QString());
}

void KeyboardInsets::setViewportSample(const ViewportSample &sample)
{
    m_viewportSample = sample;
    m_hasViewportSample = true;
    const ViewportKeyboard::State state = m_viewportKeyboard.update(sample, m_windowHeight);
    applyPlatform(state.overlap, state.shown, state.animating,
                  sample.mode.isEmpty() ? u"browser"_s : u"browser, interactive-widget=resizes-%1"_s.arg(sample.mode));
}

void KeyboardInsets::applyPlatform(qreal overlap, bool shown, bool animating, const QString &how)
{
    const bool first = !m_hasPlatform;
    m_hasPlatform = true;
    m_platform = {clampOverlap(overlap), shown, animating};
    if (first) {
        qCInfo(brulion::logging::lcPlatform).noquote()
                << u"On-screen keyboard: reported by the platform"_s + (how.isEmpty() ? QString() : u" ("_s + how + u")"_s);
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
    const qreal h = finite(height) && height > 0 ? height : 0;
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
    if (m_simulated >= 0) {
        next = {clampOverlap(m_simulated), m_simulated > 0, m_simulatedAnimating};
        source = Source::Simulated;
    } else if (m_hasPlatform) {
        next = m_platform;
        source = Source::Platform;
    } else if (m_inputMethod.shown || m_inputMethod.overlap > 0) {
        next = m_inputMethod;
        source = Source::InputMethod;
    }

    const State previous = m_current;
    const Source previousSource = m_source;
    m_current = next;
    m_source = source;
    if (!qFuzzyCompare(1 + previous.overlap, 1 + next.overlap))
        Q_EMIT overlapChanged();
    if (previous.shown != next.shown)
        Q_EMIT shownChanged();
    if (previous.animating != next.animating)
        Q_EMIT animatingChanged();
    if (previousSource != source)
        Q_EMIT sourceChanged();

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
