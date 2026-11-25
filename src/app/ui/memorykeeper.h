// MemoryKeeper — gives memory the app has freed back to the system once it is quiet
// (docs/PERFORMANCE.md "Pictures and memory"; the allocator's side: platform/processmemory.h).
//
// A page left, pictures out of reach of the view (NearView), the index built on the worker threads,
// the app sent to the background: each frees tens to hundreds of megabytes that the allocator keeps
// in its heaps — the process then looks as large as at its peak for as long as it runs. Each of
// those calls released(); once nothing more was released for `quietMs` (at the latest `latestMs`
// after the first call) the allocator's free pages are handed back — platform::releaseFreeMemory(),
// on a worker thread: it walks every heap (milliseconds on a large one). Nothing runs while
// nothing is released. Where the allocator gives back by itself (Android) it is only asked when
// the app leaves the foreground; where the platform has no such call (Windows, the browser)
// nothing ever runs.
//
//
// releaseAll() also lets go of the pages of the program and its libraries that the process has
// mapped (platform::releaseProgramPages(): clean pages of files, mapped again when touched) —
// MemoryPolicy asks for it once the start is over and whenever the app is away.
// BRULION_PROGRAM_PAGES=0 in the environment: never (A/B measurements).
//
//   brulion::ui::MemoryKeeper::instance()->released();
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

namespace brulion::ui {

class MemoryKeeper : public QObject {
    Q_OBJECT

public:
    /// The application's keeper (made on first use, a child of the application object; null
    /// without one). It follows NearView's releases and the application's state by itself.
    static MemoryKeeper *instance();

    explicit MemoryKeeper(QObject *parent = nullptr);
    ~MemoryKeeper() override;

    /// BRULION_MEMORY_KEEPER=0 in the environment: nothing is given back (A/B measurements).
    static bool switchedOff();

    /// The app must have been quiet this long before memory is given back (default 2000 ms).
    int quietMs() const { return m_quietMs; }
    void setQuietMs(int ms) { m_quietMs = ms; }
    /// … and it is given back at the latest this long after the first release (default 10 000 ms).
    int latestMs() const { return m_latestMs; }
    void setLatestMs(int ms) { m_latestMs = ms; }

    /// How many times memory was given back (tests).
    int runs() const { return m_runs; }
    /// A run is waiting for the app to be quiet, or under way.
    bool pending() const { return m_quiet.isActive() || m_running; }

public Q_SLOTS:
    /// Something large was let go: see above. Cheap (a timer restarted).
    void released();
    /// Now, without waiting to be quiet (the app goes to the background).
    void releaseNow();
    /// Now, and the program's own pages too (the start is over; the app is away).
    void releaseAll();

Q_SIGNALS:
    /// A run ended: the process's resident bytes before and after (-1: unknown), how long it took.
    void gaveBack(qint64 residentBefore, qint64 residentAfter, qint64 tookMs);
    /// … one that let go of the program's own pages too: their resident bytes.
    void programPagesReleased(qint64 bytes);

private:
    void run();
    void ran(qint64 before, qint64 after, qint64 ms, qint64 programBytes);

    QTimer m_quiet;
    QElapsedTimer m_since; ///< running since the first release not yet given back
    int m_quietMs = 2000;
    int m_latestMs = 10000;
    int m_runs = 0;
    bool m_again = false;   ///< released() came while a run was under way
    bool m_running = false; ///< a run is under way on a worker
    bool m_programPages = false;      ///< the next run lets go of the program's own pages too
    bool m_againProgramPages = false; ///< releaseAll() came while a run was under way
};

} // namespace brulion::ui
