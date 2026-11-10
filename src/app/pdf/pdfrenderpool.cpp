#include "pdfrenderpool.h"

#include <QCoreApplication>
#include <QThread>

#include <algorithm>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_PDF_THREADS 1
#endif

namespace brulion::pdfview {

namespace {

#ifdef BRULION_PDF_THREADS
QThreadPool *makePool()
{
    auto *pool = new QThreadPool;
    pool->setMaxThreadCount(PdfRenderPool::MaxThreads);
    pool->setObjectName(QStringLiteral("PdfRenderPool"));
    // Threads go after 30 s without work.
    pool->setExpiryTimeout(30'000);
    if (QCoreApplication *app = QCoreApplication::instance()) {
        // Nothing runs after the app: queued tasks dropped, the running ones finished.
        QObject::connect(app, &QCoreApplication::aboutToQuit, app, [pool] {
            pool->clear();
            pool->waitForDone(5000);
            PdfReaderPool::instance().closeIdle();
        });
    }
    return pool;
}
#endif

/// The elements of `from` that `pick` chooses moved to the end of `to` (the idle readers are closed
/// when `to` goes, outside the lock).
template<typename List, typename Pick>
void moveIf(List &from, List &to, Pick pick)
{
    for (auto it = from.begin(); it != from.end();) {
        if (pick(*it))
            to.splice(to.end(), from, it++);
        else
            ++it;
    }
}

} // namespace

QThreadPool *PdfRenderPool::pool()
{
#ifdef BRULION_PDF_THREADS
    // Never deleted: tasks may still post to it while the process ends.
    static QThreadPool *const instance = makePool();
    return instance;
#else
    return nullptr;
#endif
}

void PdfRenderPool::start(std::function<void()> task, int priority)
{
#ifdef BRULION_PDF_THREADS
    pool()->start(std::move(task), priority);
#else
    Q_UNUSED(priority);
    task();
#endif
}

// ---- PdfReaderPool --------------------------------------------------------------------------

PdfReaderPool &PdfReaderPool::instance()
{
    // Created on first use, on the GUI thread's side (its timer lives there); never deleted —
    // closeIdle() at the app's end closes the files.
    static PdfReaderPool *const pool = [] {
        auto *p = new PdfReaderPool;
        if (QCoreApplication *app = QCoreApplication::instance())
            p->moveToThread(app->thread());
        return p;
    }();
    return *pool;
}

PdfReaderPool::PdfReaderPool() : m_closer(this)
{
    m_closer.setInterval(IdleCloseMs / 4);
    connect(&m_closer, &QTimer::timeout, this, &PdfReaderPool::closeExpired);
}

PdfReaderPool::Lease &PdfReaderPool::Lease::operator=(Lease &&other) noexcept
{
    if (this != &other) {
        release();
        m_pool = other.m_pool;
        m_file = std::move(other.m_file);
        m_version = std::move(other.m_version);
        m_password = std::move(other.m_password);
        m_reader = std::move(other.m_reader);
    }
    return *this;
}

PdfReaderPool::Lease::~Lease()
{
    release();
}

void PdfReaderPool::Lease::release()
{
    if (!m_reader || !m_pool)
        return;
    m_reader->detachFromThread();
    m_pool->giveBack(m_file, m_version, m_password, std::move(m_reader));
}

PdfReaderPool::Lease PdfReaderPool::acquire(const QString &file, const QString &version, platform::PdfInfo *info,
                                            const QString &password)
{
    Lease lease;
    lease.m_pool = this;
    lease.m_file = file;
    lease.m_version = version;
    lease.m_password = password;
    {
        const std::lock_guard lock(m_lock);
        const auto it = std::ranges::find_if(m_idle, [&](const Idle &idle) {
            return idle.file == file && idle.version == version && idle.password == password;
        });
        if (it != m_idle.end()) {
            lease.m_reader = std::move(it->reader);
            m_idle.erase(it);
        }
    }
    const bool reused = lease.m_reader != nullptr;
    if (!reused) {
        lease.m_reader = platform::PdfReader::open(file, info, password);
        if (!lease.m_reader)
            return {};
        const std::lock_guard lock(m_lock);
        ++m_opened;
    }
    // (Before the reader is used on this thread: Qt PDF pulls its document here.)
    lease.m_reader->attachToCurrentThread();
    if (reused && info) {
        *info = platform::PdfInfo();
        info->pages = lease.m_reader->pageCount();
        info->pagePoints = {lease.m_reader->pageSize(1)};
    }
    return lease;
}

void PdfReaderPool::giveBack(const QString &file, const QString &version, const QString &password,
                             std::unique_ptr<platform::PdfReader> reader)
{
    if (reader->lost())
        return;             // (closed here: never given to the next thread)
    std::list<Idle> closed; // closed outside the lock
    {
        const std::lock_guard lock(m_lock);
        // (Checked under the lock: once suspendIdle() has closed the idle readers, none comes back.)
        if (m_suspended.load() > 0)
            return; // (closed here: kept open while a sync runs)
        if (!password.isEmpty() && m_passwords.value(file).password != password)
            return; // (the viewer let it go: an unlocked PDF is not kept open)
        // A reader of an older version of the file is not kept.
        moveIf(m_idle, closed, [&](const Idle &idle) { return idle.file == file && idle.version != version; });
        Idle idle{.file = file, .version = version, .password = password, .reader = std::move(reader), .since = {}};
        idle.since.start();
        m_idle.push_front(std::move(idle));
        int perFile = 0;
        int all = 0;
        for (auto it = m_idle.begin(); it != m_idle.end();) {
            ++all;
            const bool same = it->file == file && ++perFile > MaxIdlePerFile;
            if (same || all > MaxIdle) {
                closed.splice(closed.end(), m_idle, it++);
                --all;
            } else {
                ++it;
            }
        }
    }
    // The closer runs on the GUI thread (a timer is started on its own thread).
    QMetaObject::invokeMethod(
            this,
            [this] {
                if (!m_closer.isActive())
                    m_closer.start();
            },
            Qt::QueuedConnection);
}

void PdfReaderPool::closeExpired()
{
    std::list<Idle> closed;
    const std::lock_guard lock(m_lock);
    moveIf(m_idle, closed, [](const Idle &idle) { return idle.since.elapsed() >= IdleCloseMs; });
    if (m_idle.empty())
        m_closer.stop();
}

void PdfReaderPool::forget(const QString &file)
{
    std::list<Idle> closed;
    const std::lock_guard lock(m_lock);
    moveIf(m_idle, closed, [&](const Idle &idle) { return idle.file == file; });
}

void PdfReaderPool::holdPassword(const QString &file, const QString &password)
{
    if (file.isEmpty() || password.isEmpty())
        return;
    const std::lock_guard lock(m_lock);
    Held &held = m_passwords[file];
    held.password = password;
    ++held.holders;
}

void PdfReaderPool::releasePassword(const QString &file)
{
    std::list<Idle> closed;
    const std::lock_guard lock(m_lock);
    const auto it = m_passwords.find(file);
    if (it == m_passwords.end())
        return;
    if (--it->holders > 0)
        return;
    m_passwords.erase(it);
    moveIf(m_idle, closed, [&](const Idle &idle) { return idle.file == file && !idle.password.isEmpty(); });
}

QString PdfReaderPool::password(const QString &file) const
{
    const std::lock_guard lock(m_lock);
    return m_passwords.value(file).password;
}

void PdfReaderPool::suspendIdle()
{
    ++m_suspended;
    closeIdle();
}

void PdfReaderPool::resumeIdle()
{
    if (m_suspended.load() > 0)
        --m_suspended;
}

void PdfReaderPool::closeIdle()
{
    std::list<Idle> closed;
    const std::lock_guard lock(m_lock);
    closed.splice(closed.end(), m_idle);
}

int PdfReaderPool::idleCount() const
{
    const std::lock_guard lock(m_lock);
    return int(m_idle.size());
}

int PdfReaderPool::openedCount() const
{
    const std::lock_guard lock(m_lock);
    return m_opened;
}

void PdfReaderPool::reportFailure(const QString &file, const QString &version, int page)
{
    QMetaObject::invokeMethod(
            this, [this, file, version, page] { Q_EMIT pageFailed(file, version, page); }, Qt::QueuedConnection);
}

} // namespace brulion::pdfview
