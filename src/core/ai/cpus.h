#pragma once
// Which processor cores the model on the device computes on (ai/llamarunner.h). A phone's cores
// are not alike: a few fast ones and some slow, efficient ones ("big.LITTLE": the Pixel 9 Pro's
// Tensor G4 has 1 + 3 fast cores and 4 slow ones). llama.cpp splits every step evenly among its
// threads and waits for the last one, so one thread on a slow core holds all the others back —
// the model computes on the fast cores only, one thread each.
//
// Linux (Android too) tells a core's speed in sysfs: `cpu_capacity` (the fastest core is 1024;
// on a PC 1024 for each, where the kernel has it at all), the highest clock in
// `cpufreq/cpuinfo_max_freq`, and which logical processors share a physical core
// (`topology/core_cpus_list`): a PC's SMT sibling computes nothing a thread on its core does not,
// it only takes half the core's time — one thread per physical core. Elsewhere — an older kernel
// without `cpu_capacity`, Windows, macOS — nothing is chosen and the runner keeps its own rule
// (half the logical processors: their SMT siblings and efficiency cores left over).
//
//   const QList<int> cores = ai::computeCores(ai::cpuCores());   // {4, 5, 6, 7} on a Pixel 9 Pro,
//                                                                 // {0, 2, 4, 6, 8, 10} on an i5-13450HX

#include <QList>
#include <QString>

namespace brulion::ai {

/// A processor core as the system describes it.
struct CpuCore {
    int index = 0;     ///< the system's number for it (cpu4: 4)
    int capacity = 0;  ///< how fast it is next to the fastest (1024); 0: not told
    qint64 maxKHz = 0; ///< its highest clock; 0: not told
    /// The first logical processor of its physical core (SMT: two of a PC's logical processors
    /// share one core — cpu0 and cpu1 are both 0); -1: not told.
    int firstSibling = -1;
    friend bool operator==(const CpuCore &, const CpuCore &) = default;
};

/// The cores that are online, read from `root` (Linux's /sys/devices/system/cpu). Empty where
/// there is no such folder.
QList<CpuCore> cpuCores(const QString &root = QStringLiteral("/sys/devices/system/cpu"));

/// The most threads the model computes with: more seldom helps on one device.
inline constexpr int MaxComputeThreads = 8;

/// The cores a model should compute on, in ascending order: one logical processor per physical
/// core, without the slow cores of a processor that has some (the cluster below the widest gap of
/// speeds, where that gap is a real one), at most MaxComputeThreads, the fastest first. Empty when
/// the cores do not tell their capacity (an older kernel): the caller decides then.
QList<int> computeCores(const QList<CpuCore> &cores);

/// "4-7", "0,2,4-5": cores as the system writes them.
QString coreList(const QList<int> &cores);

} // namespace brulion::ai
