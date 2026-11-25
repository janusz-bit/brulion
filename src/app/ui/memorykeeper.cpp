#include "memorykeeper.h"

#include "nearview.h"
#include "processmemory.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <utility>

namespace brulion::ui {

namespace {

Q_LOGGING_CATEGORY(lcMemory, "brulion.memory", QtInfoMsg)

} // namespace

MemoryKeeper *MemoryKeeper::instance()
{
    static QPointer<MemoryKeeper> keeper;
    if (!keeper && QCoreApplication::instance()) {
        keeper = new MemoryKeeper(QCoreApplication::instance());
        // Pictures out of reach of the view let go of their pixels.
        NearView::setReleased([] {
            if (keeper)
                keeper->released();
        });
        // In the background (a phone's home screen, another app in front): what is free goes back
        // at once — the system counts it against the app.
        if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
            connect(gui, &QGuiApplication::applicationStateChanged, keeper, [](Qt::ApplicationState state) {
                if (keeper && state != Qt::ApplicationActive)
                    keeper->releaseNow();
            });
        }
    }
    return keeper;
}

MemoryKeeper::MemoryKeeper(QObject *parent) : QObject(parent)
{
    m_quiet.setSingleShot(true);
    m_quiet.setTimerType(Qt::CoarseTimer);
    connect(&m_quiet, &QTimer::timeout, this, &MemoryKeeper::run);
}

MemoryKeeper::~MemoryKeeper() = default;

bool MemoryKeeper::switchedOff()
{
    static const bool off = qEnvironmentVariable("BRULION_MEMORY_KEEPER") == QLatin1String("0");
    return off;
}

void MemoryKeeper::released()
{
    // (Android's allocator gives back by itself: it is only asked in the background, releaseNow().)
    if (!platform::keepsFreedMemory() || switchedOff())
        return;
    if (m_running) {
        m_again = true;
        return;
    }
    if (!m_since.isValid())
        m_since.start();
    // Quiet for quietMs, but never later than latestMs after the first release.
    const qint64 left = std::max<qint64>(0, m_latestMs - m_since.elapsed());
    m_quiet.start(int(std::min<qint64>(m_quietMs, left)));
}

void MemoryKeeper::releaseNow()
{
    if (!platform::canReleaseFreeMemory() || switchedOff())
        return;
    if (m_running) {
        m_again = true;
        return;
    }
    m_quiet.stop();
    run();
}

void MemoryKeeper::releaseAll()
{
    static const bool keep = qEnvironmentVariable("BRULION_PROGRAM_PAGES") == QLatin1String("0");
    if (keep || !platform::canReleaseProgramPages() || switchedOff()) {
        releaseNow();
        return;
    }
    if (m_running) {
        m_again = true;
        m_againProgramPages = true;
        return;
    }
    m_programPages = true;
    m_quiet.stop();
    run();
}

void MemoryKeeper::run()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || m_running)
        return;
    m_running = true;
    m_since.invalidate();
    m_again = false;
    const bool programPages = std::exchange(m_programPages, false);
    // On a worker: the allocator walks its heaps under their locks. The worker never touches the
    // keeper, which may be gone by then: the result comes back through the application (it waits
    // for the pool's threads before it goes).
    QThreadPool::globalInstance()->start([app, programPages, self = QPointer<MemoryKeeper>(this)] {
        QElapsedTimer took;
        took.start();
        const qint64 before = platform::residentBytes();
        platform::releaseFreeMemory();
        const qint64 programBytes = programPages ? platform::releaseProgramPages() : -1;
        const qint64 after = platform::residentBytes();
        const qint64 ms = took.elapsed();
        QMetaObject::invokeMethod(
                app,
                [self, before, after, ms, programBytes] {
                    if (self)
                        self->ran(before, after, ms, programBytes);
                },
                Qt::QueuedConnection);
    });
}

void MemoryKeeper::ran(qint64 before, qint64 after, qint64 ms, qint64 programBytes)
{
    m_running = false;
    ++m_runs;
    if (programBytes >= 0) {
        qCDebug(lcMemory) << "The program's own pages let go of:" << programBytes / (1024 * 1024) << "MB";
        Q_EMIT programPagesReleased(programBytes);
    }
    if (before >= 0 && after >= 0) {
        // (Worth a line in the log from 32 MB; every run with brulion.memory.debug=true.)
        const QString text = QStringLiteral("Memory given back to the system: %1 MB (%2 → %3 MB, %4 ms)")
                                                    .arg((before - after) / (1024 * 1024))
                                                    .arg(before / (1024 * 1024))
                                                    .arg(after / (1024 * 1024))
                                                    .arg(ms);
        if (before - after >= 32 * 1024 * 1024)
            qCInfo(lcMemory).noquote() << text;
        else
            qCDebug(lcMemory).noquote() << text;
    }
    Q_EMIT gaveBack(before, after, ms);
    if (std::exchange(m_againProgramPages, false))
        releaseAll();
    else if (m_again)
        released();
}

} // namespace brulion::ui
