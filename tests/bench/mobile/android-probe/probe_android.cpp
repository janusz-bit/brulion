// Android shim of the app benchmark probe (tests/bench/app/probe/benchprobe.cpp).
//
// On the desktop the probe is LD_PRELOADed and the runner passes BENCH_T0 (the launch time). On
// Android the probe is packaged as an extra native library of a *measurement copy* of the app
// (tests/bench/mobile/android-probe/measure-copy.sh: QT_ANDROID_EXTRA_LIBS, debuggable manifest)
// and Qt's loader System.load()s it before the app's main library, so its constructors run
// before main() and its Q_COREAPP_STARTUP_FUNCTION runs in the QGuiApplication constructor.
//
// This file only prepares the environment before benchprobe.cpp's own constructor reads it
// (constructor priority 101 runs before the default priority within the same library):
//   * BENCH_T0 = the process start (the zygote fork, /proc/self/stat field 22, 10 ms
//     resolution) on CLOCK_MONOTONIC, so every mark is "ms since the process exists";
//   * BENCH_TARGET = "libbrulion": the probe only attaches to the program it was meant for, and
//     on Android QCoreApplication::applicationFilePath() is the main library
//     (libbrulion_<abi>.so).
// Everything else (BENCH_SCENARIO, BENCH_OUT, BENCH_KEEP, …) comes from the launching intent:
// `am start … --es extraenvvars_BENCH_OUT /data/user/0/<pkg>/files/run.jsonl` (debuggable APKs).
#include <QCoreApplication>
#include <QFileInfo>

#include <android/log.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

namespace {

long long nsOf(clockid_t clock)
{
    timespec ts{};
    clock_gettime(clock, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

/// Field 22 of /proc/self/stat: the start time in clock ticks since boot (CLOCK_BOOTTIME).
long long processStartTicks()
{
    FILE *f = std::fopen("/proc/self/stat", "r");
    if (!f)
        return -1;
    char buf[2048];
    const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    buf[n] = 0;
    // The command name (field 2) may contain spaces: count from the last ')'.
    const char *p = std::strrchr(buf, ')');
    if (!p)
        return -1;
    p += 2; // field 3 starts here
    for (int field = 3; field < 22 && *p; ++field) {
        p = std::strchr(p, ' ');
        if (!p)
            return -1;
        ++p;
    }
    return std::strtoll(p, nullptr, 10);
}

} // namespace

__attribute__((constructor(101))) static void benchProbeAndroidEnvironment()
{
    __android_log_print(ANDROID_LOG_INFO, "benchprobe", "library loaded");
    if (!std::getenv("BENCH_T0")) {
        const long long ticks = processStartTicks();
        const long hz = sysconf(_SC_CLK_TCK);
        if (ticks > 0 && hz > 0) {
            const long long startBoot = ticks * (1000000000LL / hz);
            // BOOTTIME and MONOTONIC differ by the time spent suspended (0 on an emulator).
            const long long startMono = nsOf(CLOCK_MONOTONIC) - (nsOf(CLOCK_BOOTTIME) - startBoot);
            char value[32];
            std::snprintf(value, sizeof value, "%lld", startMono);
            setenv("BENCH_T0", value, 1);
        }
    }
    // On Android QCoreApplication::applicationFilePath() is the app's main library
    // (libbrulion_<abi>.so), not /proc/self/exe (app_process64).
    if (!std::getenv("BENCH_TARGET"))
        setenv("BENCH_TARGET", "libbrulion", 1);
}

namespace {

// Says in logcat (tag "benchprobe") that the probe is in the process and what it was given.
void benchProbeAndroidReport()
{
    const char *out = std::getenv("BENCH_OUT");
    const char *scenario = std::getenv("BENCH_SCENARIO");
    __android_log_print(ANDROID_LOG_INFO, "benchprobe",
                        "QCoreApplication up: exe=%s target=%s out=%s scenario=%s t0=%s",
                        qPrintable(QFileInfo(QCoreApplication::applicationFilePath()).fileName()),
                        std::getenv("BENCH_TARGET") ? std::getenv("BENCH_TARGET") : "-", out ? out : "-",
                        scenario ? scenario : "-", std::getenv("BENCH_T0") ? std::getenv("BENCH_T0") : "-");
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(benchProbeAndroidReport)
