#include "pdfrenderpool.h"

#include <QCoreApplication>
#include <QThread>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_PDF_THREADS 1
#endif

namespace brulion::pdfview {

#ifdef BRULION_PDF_THREADS
namespace {

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

} // namespace
#endif

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

PdfReaderPool::Lease::Lease(Lease &&other) noexcept
    : m_pool(other.m_pool), m_file(std::move(other.m_file)), m_version(std::move(other.m_version)),
      m_reader(std::move(other.m_reader))
{
    other.m_pool = nullptr;
}

PdfReaderPool::Lease &PdfReaderPool::Lease::operator=(Lease &&other) noexcept
{
    if (this != &other) {
        release();
        m_pool = other.m_pool;
        m_file = std::move(other.m_file);
        m_version = std::move(other.m_version);
        m_reader = std::move(other.m_reader);
        other.m_pool = nullptr;
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
    m_pool->giveBack(m_file, m_version, std::move(m_reader));
    m_pool = nullptr;
}

PdfReaderPool::Lease PdfReaderPool::acquire(const QString &file, const QString &version, platform::PdfInfo *info)
{
    Lease lease;
    lease.m_pool = this;
    lease.m_file = file;
    lease.m_version = version;
    {
        const std::lock_guard lock(m_lock);
        for (auto it = m_idle.begin(); it != m_idle.end(); ++it) {
            if (it->file == file && it->version == version) {
                lease.m_reader = std::move(it->reader);
                m_idle.erase(it);
                break;
            }
        }
    }
    if (lease.m_reader) {
        if (info) {
            *info = platform::PdfInfo();
            info->pages = lease.m_reader->pageCount();
            info->pagePoints = {lease.m_reader->pageSize(1)};
        }
    } else {
        lease.m_reader = platform::PdfReader::open(file, info);
        if (!lease.m_reader)
            return {};
        const std::lock_guard lock(m_lock);
        ++m_opened;
    }
    lease.m_reader->attachToCurrentThread();
    return lease;
}

void PdfReaderPool::giveBack(const QString &file, const QString &version, std::unique_ptr<platform::PdfReader> reader)
{
    std::list<Idle> closed; // closed outside the lock
    {
        const std::lock_guard lock(m_lock);
        // A reader of an older version of the file is not kept.
        for (auto it = m_idle.begin(); it != m_idle.end();) {
            if (it->file == file && it->version != version)
                closed.splice(closed.end(), m_idle, it++);
            else
                ++it;
        }
        Idle idle;
        idle.file = file;
        idle.version = version;
        idle.reader = std::move(reader);
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
    QMetaObject::invokeMethod(this, [this] {
        if (!m_closer.isActive())
            m_closer.start();
    }, Qt::QueuedConnection);
}

void PdfReaderPool::closeExpired()
{
    std::list<Idle> closed;
    {
        const std::lock_guard lock(m_lock);
        for (auto it = m_idle.begin(); it != m_idle.end();) {
            if (it->since.elapsed() >= IdleCloseMs)
                closed.splice(closed.end(), m_idle, it++);
            else
                ++it;
        }
        if (m_idle.empty())
            m_closer.stop();
    }
}

void PdfReaderPool::forget(const QString &file)
{
    std::list<Idle> closed;
    const std::lock_guard lock(m_lock);
    for (auto it = m_idle.begin(); it != m_idle.end();) {
        if (it->file == file)
            closed.splice(closed.end(), m_idle, it++);
        else
            ++it;
    }
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
    QMetaObject::invokeMethod(this, [this, file, version, page] { Q_EMIT pageFailed(file, version, page); },
                              Qt::QueuedConnection);
}

} // namespace brulion::pdfview
