// Time in tests that a slow or loaded machine cannot fail. CI runners stall (another job on the
// host, a virus scanner on Windows, the sanitizer build's ~25x slowdown) and ctest runs tests side
// by side, so a wall clock (QElapsedTimer) read around a piece of work measures the machine as
// much as the work:
//
//   * a limit on how long work takes reads the calling thread's own CPU time (CpuTimer): it stands
//     still while the thread waits for a core or the whole process is stopped;
//   * a comparison of two ways of doing the same thing takes the fastest of several runs of each
//     (fastestNs()) or the median of many samples (medianOf()), which a stall in one run does not
//     move;
//   * a wall-clock budget that the code under test enforces, where the test is not about that
//     budget, is kRoomyMs.
//
// Limits stay multiplied by BRULION_TEST_SLOWDOWN (tests/CMakeLists.txt) for the sanitizer build.
#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace brulion::test {

/// A wall-clock budget for work the test is not about (an extension's time limit, a wait for a
/// worker): far beyond any stall of a CI runner, still below ctest's own timeout outside the
/// sanitizer build. A test of a time limit itself sets that limit instead.
inline constexpr int kRoomyMs = 60000 * BRULION_TEST_SLOWDOWN;

/// The CPU time the calling thread has run (user and kernel), in nanoseconds (testtiming.cpp:
/// <windows.h> stays out of the tests, whose `near` and `far` it would define away). Windows
/// counts it in clock ticks (15.6 ms): use it for limits of a few hundred milliseconds and more.
qint64 threadCpuNs();

/// QElapsedTimer's interface over the calling thread's CPU time: for a limit on work that runs on
/// that thread (not for work handed to other threads, which it does not see).
class CpuTimer
{
public:
    void start() { m_start = threadCpuNs(); }
    /// The milliseconds since start(), and starts again.
    qint64 restart()
    {
        const qint64 now = threadCpuNs();
        const qint64 ms = (now - m_start) / 1000000;
        m_start = now;
        return ms;
    }
    qint64 nsecsElapsed() const { return threadCpuNs() - m_start; }
    qint64 elapsed() const { return nsecsElapsed() / 1000000; }

private:
    qint64 m_start = 0;
};

/// The shortest wall-clock time of `runs` runs of `work`, in nanoseconds: a stall makes a run
/// longer, never shorter, so the fastest run is the work's own time.
template <typename Work>
qint64 fastestNs(int runs, Work &&work)
{
    qint64 best = std::numeric_limits<qint64>::max();
    QElapsedTimer clock;
    for (int i = 0; i < runs; ++i) {
        clock.start();
        work();
        best = std::min(best, clock.nsecsElapsed());
    }
    return best;
}

/// The median of `samples` (0 when there are none): a stall in fewer than half of them does not
/// move it.
inline qint64 medianOf(QList<qint64> samples)
{
    if (samples.isEmpty())
        return 0;
    std::sort(samples.begin(), samples.end());
    return samples.at(samples.size() / 2);
}

} // namespace brulion::test
