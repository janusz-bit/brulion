#pragma once
// Which processor cores the model on the device computes on (ai/llamarunner.h). A phone's cores
// are not alike: a few fast ones and some slow, efficient ones ("big.LITTLE": the Pixel 9 Pro's
// Tensor G4 has 1 + 3 fast cores and 4 slow ones). llama.cpp splits every step evenly among its
// threads and waits for the last one, so one thread on a slow core holds all the others back —
// the model computes on the fast cores only, one thread each.
//
// Linux (Android too) tells a core's speed in sysfs: `cpu_capacity` (the fastest core is 1024)
// where the kernel knows it (ARM), the highest clock in `cpufreq/cpuinfo_max_freq`. Elsewhere —
// a PC's processor without `cpu_capacity`, Windows, macOS — nothing is chosen and the runner keeps
// its own rule (half the logical processors: their SMT siblings and efficiency cores left over).
//
//   const QList<int> cores = ai::computeCores(ai::cpuCores());   // {4, 5, 6, 7} on a Pixel 9 Pro

#include <QList>
#include <QString>

namespace brulion::ai {

/// A processor core as the system describes it.
struct CpuCore {
    int index = 0;     ///< the system's number for it (cpu4: 4)
    int capacity = 0;  ///< how fast it is next to the fastest (1024); 0: not told
    qint64 maxKHz = 0; ///< its highest clock; 0: not told
    friend bool operator==(const CpuCore &, const CpuCore &) = default;
};

/// The cores that are online, read from `root` (Linux's /sys/devices/system/cpu). Empty where
/// there is no such folder.
QList<CpuCore> cpuCores(const QString &root = QStringLiteral("/sys/devices/system/cpu"));

/// The most threads the model computes with: more seldom helps on one device.
inline constexpr int MaxComputeThreads = 8;

/// The cores a model should compute on, in ascending order: without the slow cores of a processor
/// that has some (the cluster below the widest gap of speeds, where that gap is a real one), at
/// most MaxComputeThreads, the fastest first. Empty when the cores do not tell their capacity — a
/// PC's processor: the caller decides then.
QList<int> computeCores(const QList<CpuCore> &cores);

/// "4-7", "0,2,4-5": cores as the system writes them.
QString coreList(const QList<int> &cores);

} // namespace brulion::ai
