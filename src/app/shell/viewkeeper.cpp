#include "viewkeeper.h"

#include "memorypolicy.h"

#include <QVariant>

#include <utility>

namespace brulion::shell {

ViewKeeper::ViewKeeper(QObject *parent) : QObject(parent)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, &ViewKeeper::letGoHidden);
    // The app is away: nothing hidden is worth its memory.
    if (ui::MemoryPolicy *policy = ui::MemoryPolicy::instance())
        connect(policy, &ui::MemoryPolicy::letGo, this, &ViewKeeper::letGoHidden);
}

void ViewKeeper::setLimit(int limit)
{
    if (m_limit == limit)
        return;
    m_limit = limit;
    Q_EMIT limitChanged();
    trim();
}

void ViewKeeper::setIdleMs(int ms)
{
    if (m_idleMs == ms)
        return;
    m_idleMs = ms;
    Q_EMIT idleMsChanged();
    if (m_idleMs <= 0)
        m_idle.stop();
    else if (!m_hidden.isEmpty())
        startIdle();
}

void ViewKeeper::startIdle()
{
    // (Whole seconds are precise enough for minutes, and wake the processor least.)
    m_idle.setTimerType(m_idleMs >= 5000 ? Qt::VeryCoarseTimer : Qt::CoarseTimer);
    m_idle.start(m_idleMs);
}

void ViewKeeper::add(const QString &key, QObject *loader)
{
    if (!key.isEmpty() && loader)
        m_loaders.insert(key, loader);
}

void ViewKeeper::show(const QString &key)
{
    const QString now = m_loaders.contains(key) ? key : QString();
    const QStringList before = m_hidden;
    if (now != m_shown) {
        if (!m_shown.isEmpty()) {
            m_hidden.removeAll(m_shown);
            m_hidden.prepend(m_shown);
        }
        m_shown = now;
    }
    m_hidden.removeAll(now);
    trim();
    if (m_hidden != before)
        Q_EMIT hiddenChanged();
    // (Every call: the user is switching views, the hidden ones are in use.)
    if (m_idleMs > 0 && !m_hidden.isEmpty())
        startIdle();
    else
        m_idle.stop();
}

void ViewKeeper::built(const QString &key)
{
    if (!m_loaders.contains(key) || key == m_shown || m_hidden.contains(key))
        return;
    m_hidden.prepend(key);
    trim();
    Q_EMIT hiddenChanged();
    if (m_idleMs > 0 && !m_idle.isActive() && !m_hidden.isEmpty())
        startIdle();
}

void ViewKeeper::letGoHidden()
{
    m_idle.stop();
    if (m_hidden.isEmpty())
        return;
    const QStringList gone = std::exchange(m_hidden, {});
    for (const QString &key : gone)
        drop(key);
    Q_EMIT hiddenChanged();
}

void ViewKeeper::clear()
{
    m_idle.stop();
    m_shown.clear();
    if (m_hidden.isEmpty())
        return;
    m_hidden.clear();
    Q_EMIT hiddenChanged();
}

void ViewKeeper::trim()
{
    if (m_limit < 0)
        return;
    bool changed = false;
    while (m_hidden.size() > m_limit) {
        drop(m_hidden.takeLast());
        changed = true;
    }
    if (changed)
        Q_EMIT hiddenChanged();
}

void ViewKeeper::drop(const QString &key)
{
    if (QObject *loader = m_loaders.value(key))
        loader->setProperty("kept", false);
    Q_EMIT letGo(key);
}

} // namespace brulion::shell
