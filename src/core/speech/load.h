#pragma once
// How many threads speech recognition computes with (docs/PERFORMANCE.md "Speech recognition on a
// busy processor"). ggml splits every step of Whisper's computation among its threads and waits at
// the end of the step for the last one — spinning, whatever its thread pool's poll level says
// (ggml_barrier) —, so a thread the system took off its core holds up all the others: with the
// processor busy with other programs, four threads took 225 s for what one idle core hears in a
// few. The recognizer takes as many threads as the fast cores (ai::computeCores, at most four) and
// no more than the processor has free from other programs now: what they kept busy since the last
// look (Linux and Android's /proc/stat), the recognizer's own process left out (/proc/self/stat —
// its own threads of a window heard a moment ago are not "others"). Where the system tells none
// of it (Windows, macOS, an Android that hides /proc/stat) the fast cores alone decide.
//
//   speech::CpuSample before = speech::sampleCpu();
//   … (at least 100 ms later)
//   const double busy = speech::othersBusy(before, speech::sampleCpu());   // cores; -1 unknown
//   const int threads = speech::speechThreads(fastCores, logicalCpus, busy);

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>

namespace brulion::speech {

/// The most threads Whisper computes with (whisper.cpp's own default): more seldom make it faster.
inline constexpr int MaxSpeechThreads = 4;

/// The processor's time so far, in the system's clock ticks (all of them: every core).
struct CpuTimes {
    qint64 busy = 0;  ///< user, nice, system, irq, softirq, steal
    qint64 total = 0; ///< busy and idle, iowait
    friend bool operator==(const CpuTimes &, const CpuTimes &) = default;
};

/// The first line of Linux's /proc/stat ("cpu  4705 356 584 3699 23 23 0 0 0 0"); nullopt when it
/// is not one.
std::optional<CpuTimes> parseProcStat(QByteArrayView text);

/// The time a process computed (its user and system time, clock ticks) from its /proc/<pid>/stat
/// line — fields 14 and 15, after the name in parentheses (which may hold spaces and parentheses);
/// -1 when it is not one.
qint64 parseProcessTime(QByteArrayView text);

/// One look at the processor: the time of all of it and of this process, when.
struct CpuSample {
    std::optional<CpuTimes> all;
    qint64 self = -1;
    qint64 atMs = 0; ///< a monotonic clock
    bool isValid() const { return all.has_value() && self >= 0; }
};

/// Now (files: /proc/stat, /proc/self/stat; tests give others).
CpuSample
sampleCpu(const QString &statFile = QStringLiteral("/proc/stat"),
                                                   const QString &selfFile = QStringLiteral("/proc/self/stat"));

/// How many cores other programs kept busy between two samples, on average (`cpus`: the logical
/// processors — /proc/stat's line sums them); -1 when either sample is invalid or no time passed.
double othersBusy(const CpuSample &before, const CpuSample &after, int cpus);

/// The threads to compute with: `fastCores` (one per fast physical core; ≤ 0: half of `cpus`), at
/// most MaxSpeechThreads, and no more than the logical processors `cpus` minus the ones others keep
/// busy (`busy` cores, rounded; < 0: not known) — at least one.
int speechThreads(int fastCores, int cpus, double busy);

} // namespace brulion::speech
