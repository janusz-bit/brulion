#include "terminalregistry.h"

#include "terminalsession.h"
#include "terminalthread.h"

#include <QCoreApplication>

#include <algorithm>

namespace brulion::terminal {

namespace {

/// Threads without terminals remembered (their "closed by the user"), at most.
constexpr int MaxEmptyThreads = 2 * TerminalRegistry::MaxThreads;

QPointer<TerminalRegistry> &theInstance()
{
    static QPointer<TerminalRegistry> registry;
    return registry;
}

} // namespace

TerminalRegistry::TerminalRegistry(QObject *parent) : QObject(parent) { }

TerminalRegistry::~TerminalRegistry()
{
    for (TerminalThread *thread : std::as_const(m_threads))
        disconnect(thread, nullptr, this, nullptr);
    // Every program hung up on at once; deleting the threads (children) reaps them.
    hangUpAll();
}

TerminalRegistry *TerminalRegistry::instance()
{
    QPointer<TerminalRegistry> &registry = theInstance();
    if (!registry) {
        registry = new TerminalRegistry(QCoreApplication::instance());
        // Quitting hangs up on every program at once, before the window goes; the application's
        // end deletes the registry with its threads, which reaps them.
        if (QCoreApplication::instance())
            connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, registry.data(),
                    &TerminalRegistry::hangUpAll);
    }
    return registry;
}

bool TerminalRegistry::available()
{
    return TerminalSession::available();
}

QStringList TerminalRegistry::keys() const
{
    QStringList out;
    for (const QString &key : m_order) {
        const TerminalThread *thread = m_threads.value(key);
        if (thread && thread->sessionCount() > 0)
            out.append(key);
    }
    return out;
}

int TerminalRegistry::sessionCount() const
{
    int n = 0;
    for (const TerminalThread *thread : m_threads)
        n += thread->sessionCount();
    return n;
}

TerminalThread *TerminalRegistry::thread(const QString &key, const QString &folder)
{
    if (key.isEmpty())
        return nullptr;
    TerminalThread *thread = m_threads.value(key);
    if (!thread) {
        thread = new TerminalThread(key, this);
        connect(thread, &TerminalThread::busyChanged, this, [this, thread] { updateBusy(thread); });
        connect(thread, &TerminalThread::sessionsChanged, this, &TerminalRegistry::threadsChanged);
        m_threads.insert(key, thread);
        m_order.prepend(key);
        Q_EMIT threadsChanged();
    }
    if (!folder.isEmpty())
        thread->setFolder(folder);
    return thread;
}

TerminalThread *TerminalRegistry::find(const QString &key) const
{
    return m_threads.value(key);
}

void TerminalRegistry::touch(const QString &key)
{
    if (!m_threads.contains(key))
        return;
    m_order.removeAll(key);
    m_order.prepend(key);
}

void TerminalRegistry::shown(const QString &key)
{
    m_shown = key;
    touch(key);
    enforceThreadLimit();
    prune();
    Q_EMIT threadsChanged();
}

QList<TerminalThread *> TerminalRegistry::leastRecentFirst(const TerminalThread *except) const
{
    QList<TerminalThread *> out;
    for (auto it = m_order.crbegin(); it != m_order.crend(); ++it) {
        TerminalThread *thread = m_threads.value(*it);
        if (thread && thread != except && thread->key() != m_shown)
            out.append(thread);
    }
    return out;
}

void TerminalRegistry::enforceThreadLimit()
{
    auto withTerminals = [this] {
        return int(std::ranges::count_if(m_threads, [](const TerminalThread *t) { return t->sessionCount() > 0; }));
    };
    if (withTerminals() <= m_maxThreads)
        return;
    for (TerminalThread *thread : leastRecentFirst(nullptr)) {
        if (thread->sessionCount() > 0)
            thread->closeIdle();
        if (withTerminals() <= m_maxThreads)
            return;
    }
}

bool TerminalRegistry::makeRoom(const TerminalThread *asking)
{
    if (sessionCount() < m_maxSessions)
        return true;
    for (TerminalThread *thread : leastRecentFirst(asking)) {
        for (TerminalSession *session : thread->sessions()) {
            if (session->isBusy())
                continue;
            thread->closeSession(session, false);
            if (sessionCount() < m_maxSessions)
                return true;
        }
    }
    return sessionCount() < m_maxSessions;
}

void TerminalRegistry::setLimits(int maxThreads, int maxSessions)
{
    m_maxThreads = std::max(1, maxThreads);
    m_maxSessions = std::max(1, maxSessions);
}

void TerminalRegistry::prune()
{
    // Threads without terminals: forgotten unless shown — those whose last terminal the user closed
    // are remembered a while (no shell opens by itself there).
    int kept = 0;
    const QStringList order = m_order;
    for (const QString &key : order) {
        TerminalThread *thread = m_threads.value(key);
        if (!thread || thread->sessionCount() > 0 || key == m_shown)
            continue;
        if (!thread->autoOpen() && kept < MaxEmptyThreads) {
            ++kept;
            continue;
        }
        forget(thread);
    }
}

void TerminalRegistry::forget(TerminalThread *thread)
{
    const QString key = thread->key();
    disconnect(thread, nullptr, this, nullptr);
    thread->closeAll();
    m_threads.remove(key);
    m_order.removeAll(key);
    if (m_busy.removeAll(key) > 0) {
        Q_EMIT busyKeysChanged();
        Q_EMIT busyChanged(key, false);
    }
    // Later: QML may still hold it for the rest of this event.
    thread->deleteLater();
}

bool TerminalRegistry::rekey(const QString &from, const QString &to)
{
    TerminalThread *thread = m_threads.value(from);
    if (!thread || to.isEmpty() || from == to)
        return false;
    if (TerminalThread *other = m_threads.value(to)) {
        if (other->sessionCount() > 0)
            return false;
        forget(other);
    }
    m_threads.remove(from);
    m_threads.insert(to, thread);
    thread->setKey(to);
    const qsizetype at = m_order.indexOf(from);
    if (at >= 0)
        m_order[at] = to;
    else
        m_order.prepend(to);
    if (m_shown == from)
        m_shown = to;
    if (m_busy.removeAll(from) > 0) {
        m_busy.append(to);
        Q_EMIT busyKeysChanged();
        Q_EMIT busyChanged(from, false);
        Q_EMIT busyChanged(to, true);
    }
    Q_EMIT threadsChanged();
    return true;
}

void TerminalRegistry::close(const QString &key)
{
    TerminalThread *thread = m_threads.value(key);
    if (!thread)
        return;
    thread->closeAll();
    // (The panel showing it keeps the object until it shows another thread.)
    if (key != m_shown)
        forget(thread);
    Q_EMIT threadsChanged();
}

void TerminalRegistry::closeIdle(const QString &key)
{
    if (TerminalThread *thread = m_threads.value(key)) {
        thread->closeIdle();
        Q_EMIT threadsChanged();
    }
}

bool TerminalRegistry::isBusy(const QString &key) const
{
    return m_busy.contains(key);
}

void TerminalRegistry::hangUpAll()
{
    for (const TerminalThread *thread : std::as_const(m_threads)) {
        for (TerminalSession *session : thread->sessions())
            session->stop();
    }
}

void TerminalRegistry::closeAll()
{
    // Every program hung up on first, so that the waits for their ends overlap.
    hangUpAll();
    const QList<TerminalThread *> threads = m_threads.values();
    for (TerminalThread *thread : threads)
        thread->closeAll();
}

void TerminalRegistry::updateBusy(TerminalThread *thread)
{
    const QString key = thread->key();
    const bool busy = thread->isBusy();
    if (busy == m_busy.contains(key))
        return;
    if (busy)
        m_busy.append(key);
    else
        m_busy.removeAll(key);
    Q_EMIT busyKeysChanged();
    Q_EMIT busyChanged(key, busy);
}

void TerminalRegistry::pathRenamed(const QString &oldPath, const QString &newPath)
{
    if (m_threads.contains(oldPath))
        rekey(oldPath, newPath);
}

void TerminalRegistry::pathRemoved(const QString &path)
{
    close(path);
}

void TerminalRegistry::archived(const QString &path, bool on)
{
    if (on)
        closeIdle(path);
}

} // namespace brulion::terminal
