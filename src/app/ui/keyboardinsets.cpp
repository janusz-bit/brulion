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
    const bool first = !m_hasPlatform;
    m_hasPlatform = true;
    m_platform = {clampOverlap(overlap), shown, animating};
    if (first)
        qCInfo(brulion::logging::lcPlatform) << "On-screen keyboard: reported by the platform";
    update();
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
    // Clamp what is known already (a smaller window after a rotation).
    m_platform.overlap = clampOverlap(m_platform.overlap);
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
