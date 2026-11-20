#include "applock/applock.h"

#include "crypto/secrethash.h"

#include <array>

namespace brulion::applock {

namespace {

constexpr qint64 Minute = 60 * 1000;
constexpr int FreeAttempts = 4;

} // namespace

qint64 retryDelayMs(int failures)
{
    static constexpr std::array<qint64, 5> Waits = {30 * 1000, 1 * Minute, 2 * Minute, 5 * Minute, 15 * Minute};
    if (failures <= FreeAttempts)
        return 0;
    const qsizetype step = qMin<qsizetype>(failures - FreeAttempts - 1, Waits.size() - 1);
    return Waits[std::size_t(step)];
}

void LockState::setPolicy(const Policy &policy)
{
    m_policy = policy;
    if (!m_policy.enabled)
        m_locked = false;
}

void LockState::started(qint64 now)
{
    m_inBackground = false;
    m_lastActivity = now;
    m_locked = m_policy.enabled && m_policy.lockOnStart;
}

void LockState::wentToBackground(qint64 now)
{
    if (m_inBackground)
        return;
    m_inBackground = true;
    m_backgroundSince = now;
}

bool LockState::cameToForeground(qint64 now)
{
    if (!m_inBackground)
        return false;
    m_inBackground = false;
    m_lastActivity = now;
    if (!m_policy.enabled || m_locked)
        return false;
    const qint64 away = now - m_backgroundSince;
    // (A clock that went back — negative — counts as away: lock.)
    if (away < 0 || away >= qint64(qMax(0, m_policy.backgroundMinutes)) * Minute) {
        m_locked = true;
        return true;
    }
    return false;
}

void LockState::activity(qint64 now)
{
    m_lastActivity = now;
}

bool LockState::tick(qint64 now)
{
    const qint64 deadline = nextDeadline();
    if (deadline < 0 || now < deadline)
        return false;
    m_locked = true;
    return true;
}

qint64 LockState::nextDeadline() const
{
    if (!m_policy.enabled || m_locked || m_inBackground || m_policy.idleMinutes <= 0)
        return -1;
    return m_lastActivity + qint64(m_policy.idleMinutes) * Minute;
}

void LockState::lockNow()
{
    if (m_policy.enabled)
        m_locked = true;
}

void LockState::unlocked(qint64 now)
{
    m_locked = false;
    m_lastActivity = now;
    m_attempts = {};
}

void LockState::disable()
{
    m_policy.enabled = false;
    m_locked = false;
    m_attempts = {};
}

void LockState::failedAttempt(qint64 wallNow)
{
    ++m_attempts.failures;
    m_attempts.lastFailure = wallNow;
}

bool LockState::attemptAllowed(qint64 wallNow) const
{
    const qint64 at = retryAt();
    return at == 0 || wallNow >= at || wallNow < m_attempts.lastFailure;
}

qint64 LockState::retryAt() const
{
    const qint64 delay = retryDelayMs(m_attempts.failures);
    return delay == 0 ? 0 : m_attempts.lastFailure + delay;
}

bool isValidPin(QStringView pin)
{
    if (pin.size() < 4 || pin.size() > 16)
        return false;
    for (const QChar c : pin) {
        if (c < u'0' || c > u'9')
            return false;
    }
    return true;
}

QString hashPin(QStringView pin)
{
    return isValidPin(pin) ? crypto::hashSecret(pin) : QString();
}

bool verifyPin(QStringView stored, QStringView pin)
{
    return isValidPin(pin) && crypto::verifySecret(stored, pin);
}

} // namespace brulion::applock
