#include "gestureinsets.h"

#include "log/categories.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::ui {

namespace {

GestureInsets *g_instance = nullptr;

/// Logical pixels: 0 for anything negative or not finite.
qreal sanitized(qreal v)
{
    return std::isfinite(v) && v > 0 ? v : 0;
}

bool off(qreal v)
{
    return !std::isfinite(v) || v < 0;
}

} // namespace

GestureInsets::GestureInsets(QObject *parent) : QObject(parent)
{
    if (!g_instance)
        g_instance = this;
}

GestureInsets::~GestureInsets()
{
    if (g_instance == this)
        g_instance = nullptr;
}

GestureInsets *GestureInsets::instance()
{
    return g_instance;
}

void GestureInsets::setPlatformInsets(qreal left, qreal right)
{
    const qreal oldLeft = this->left();
    const qreal oldRight = this->right();
    const qreal l = sanitized(left);
    const qreal r = sanitized(right);
    if (l != m_platformLeft || r != m_platformRight) {
        qCDebug(brulion::logging::lcPlatform).noquote()
                << u"System gesture insets: left %1 px, right %2 px"_s.arg(qRound(l)).arg(qRound(r));
    }
    m_platformLeft = l;
    m_platformRight = r;
    apply(oldLeft, oldRight, m_simulated);
}

void GestureInsets::simulate(qreal left, qreal right)
{
    const qreal oldLeft = this->left();
    const qreal oldRight = this->right();
    const bool oldSimulated = m_simulated;
    m_simulated = !(off(left) && off(right));
    m_simulatedLeft = m_simulated ? sanitized(left) : 0;
    m_simulatedRight = m_simulated ? sanitized(right) : 0;
    apply(oldLeft, oldRight, oldSimulated);
}

void GestureInsets::apply(qreal oldLeft, qreal oldRight, bool oldSimulated)
{
    if (oldLeft != left() || oldRight != right() || oldSimulated != m_simulated)
        Q_EMIT changed();
}

} // namespace brulion::ui
