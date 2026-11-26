#include "terminalregistry.h"

#include "terminalsession.h"
#include "terminalthread.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::terminal {

namespace {

/// Threads without terminals remembered (their "closed by the user"), at most.
constexpr int MaxEmptyThreads = 2 * TerminalRegistry::MaxThreads;

/// Layouts are written this long after a change (and when the app quits).
constexpr int SaveDelay = 2000;

QPointer<TerminalRegistry> &theInstance()
{
    static QPointer<TerminalRegistry> registry;
    return registry;
}

} // namespace

TerminalRegistry::TerminalRegistry(QObject *parent) : QObject(parent)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(SaveDelay);
    connect(&m_saveTimer, &QTimer::timeout, this, &TerminalRegistry::saveLayouts);
    connect(this, &TerminalRegistry::threadsChanged, this, &TerminalRegistry::layoutChanged);
}

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
        // Quitting keeps the layouts and hangs up on every program at once, before the window goes;
        // the application's end deletes the registry with its threads, which reaps them.
        if (QCoreApplication::instance()) {
            TerminalRegistry *r = registry.data();
            connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, r, [r] {
                r->saveLayouts();
                r->hangUpAll();
            });
        }
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
        connect(thread, &TerminalThread::currentChanged, this, &TerminalRegistry::layoutChanged);
        m_threads.insert(key, thread);
        m_order.prepend(key);
        if (!folder.isEmpty())
            thread->setFolder(folder);
        // What it had before the app was restarted.
        if (m_saved.contains(key)) {
            const QJsonObject layout = m_saved.take(key);
            m_savedOrder.removeAll(key);
            restore(thread, layout);
        }
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
    if (m_saved.contains(oldPath) && !m_saved.contains(newPath)) {
        QJsonObject layout = m_saved.take(oldPath);
        layout.insert(u"key"_s, newPath);
        m_saved.insert(newPath, layout);
        const qsizetype at = m_savedOrder.indexOf(oldPath);
        if (at >= 0)
            m_savedOrder[at] = newPath;
        layoutChanged();
    }
}

void TerminalRegistry::pathRemoved(const QString &path)
{
    close(path);
    if (m_saved.remove(path) > 0) {
        m_savedOrder.removeAll(path);
        layoutChanged();
    }
}

void TerminalRegistry::archived(const QString &path, bool on)
{
    if (!on)
        return;
    closeIdle(path);
    if (m_saved.remove(path) > 0) {
        m_savedOrder.removeAll(path);
        layoutChanged();
    }
}

// ---- Layouts kept over a restart --------------------------------------------------------------

void TerminalRegistry::setLayoutStorage(std::function<QByteArray()> load,
                                        std::function<void(const QByteArray &)> save)
{
    m_save = std::move(save);
    if (load)
        setLayouts(QJsonDocument::fromJson(load()).object());
}

void TerminalRegistry::layoutChanged()
{
    if (m_save)
        m_saveTimer.start();
}

void TerminalRegistry::saveLayouts()
{
    m_saveTimer.stop();
    if (m_save)
        m_save(QJsonDocument(layouts()).toJson(QJsonDocument::Compact));
}

QJsonObject TerminalRegistry::layouts() const
{
    QJsonArray threads;
    int sessions = 0;
    for (const QString &key : m_order) {
        const TerminalThread *thread = m_threads.value(key);
        if (!thread || thread->sessionCount() == 0)
            continue;
        if (threads.size() >= m_maxThreads)
            break;
        QJsonArray tabs;
        for (int g = 0; g < thread->count(); ++g) {
            const TerminalGroup *group = thread->group(g);
            QJsonArray terminals;
            for (const TerminalSession *session : group->sessions()) {
                if (sessions >= m_maxSessions)
                    break;
                terminals.append(QJsonObject{
                        {u"folder"_s, session->isRunning() ? session->currentDirectory() : session->folder()},
                        {u"label"_s, session->label()},
                        {u"command"_s, session->command()}});
                ++sessions;
            }
            if (terminals.isEmpty())
                continue;
            tabs.append(QJsonObject{{u"split"_s, group->orientation() == Qt::Vertical ? u"down"_s : u"right"_s},
                                    {u"focus"_s, group->focusIndex()},
                                    {u"terminals"_s, terminals}});
        }
        if (!tabs.isEmpty())
            threads.append(QJsonObject{{u"key"_s, key}, {u"current"_s, thread->currentIndex()}, {u"tabs"_s, tabs}});
    }
    // Those from before whose threads were not shown since: kept, after them.
    for (const QString &key : m_savedOrder) {
        if (threads.size() >= m_maxThreads)
            break;
        const TerminalThread *live = m_threads.value(key);
        if (live && live->sessionCount() > 0)
            continue;
        const QJsonObject layout = m_saved.value(key);
        int count = 0;
        for (const QJsonValue &tab : layout.value(u"tabs"_s).toArray())
            count += int(tab.toObject().value(u"terminals"_s).toArray().size());
        if (sessions + count > m_maxSessions)
            continue;
        sessions += count;
        threads.append(layout);
    }
    return QJsonObject{{u"threads"_s, threads}};
}

void TerminalRegistry::setLayouts(const QJsonObject &layouts)
{
    m_saved.clear();
    m_savedOrder.clear();
    int sessions = 0;
    for (const QJsonValue &value : layouts.value(u"threads"_s).toArray()) {
        const QJsonObject thread = value.toObject();
        const QString key = thread.value(u"key"_s).toString();
        if (key.isEmpty() || m_saved.contains(key) || m_savedOrder.size() >= m_maxThreads)
            continue;
        // Only what a layout is made of, checked: the settings come from anywhere.
        QJsonArray tabs;
        for (const QJsonValue &tabValue : thread.value(u"tabs"_s).toArray()) {
            const QJsonObject tab = tabValue.toObject();
            QJsonArray terminals;
            for (const QJsonValue &t : tab.value(u"terminals"_s).toArray()) {
                const QJsonObject terminal = t.toObject();
                if (!t.isObject() || terminals.size() >= TerminalGroup::MaxSessions || sessions >= m_maxSessions)
                    continue;
                terminals.append(QJsonObject{{u"folder"_s, terminal.value(u"folder"_s).toString()},
                                             {u"label"_s, terminal.value(u"label"_s).toString().left(200)},
                                             {u"command"_s, terminal.value(u"command"_s).toString()}});
                ++sessions;
            }
            if (terminals.isEmpty())
                continue;
            tabs.append(QJsonObject{
                    {u"split"_s, tab.value(u"split"_s).toString() == u"down" ? u"down"_s : u"right"_s},
                    {u"focus"_s, std::clamp(tab.value(u"focus"_s).toInt(), 0, int(terminals.size()) - 1)},
                    {u"terminals"_s, terminals}});
        }
        if (tabs.isEmpty())
            continue;
        m_saved.insert(key, QJsonObject{{u"key"_s, key},
                                        {u"current"_s, std::clamp(thread.value(u"current"_s).toInt(), 0,
                                                                  int(tabs.size()) - 1)},
                                        {u"tabs"_s, tabs}});
        m_savedOrder.append(key);
    }
}

void TerminalRegistry::restore(TerminalThread *thread, const QJsonObject &layout)
{
    for (const QJsonValue &tabValue : layout.value(u"tabs"_s).toArray()) {
        const QJsonObject tab = tabValue.toObject();
        QList<TerminalThread::Restored> terminals;
        for (const QJsonValue &t : tab.value(u"terminals"_s).toArray()) {
            const QJsonObject terminal = t.toObject();
            terminals.append({terminal.value(u"folder"_s).toString(), terminal.value(u"label"_s).toString(),
                              terminal.value(u"command"_s).toString()});
        }
        if (!thread->restoreTab(terminals, tab.value(u"split"_s).toString() == u"down" ? Qt::Vertical : Qt::Horizontal,
                                tab.value(u"focus"_s).toInt()))
            break; // (no room: the rest neither)
    }
    thread->setCurrentIndex(layout.value(u"current"_s).toInt());
}

} // namespace brulion::terminal
