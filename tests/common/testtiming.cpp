// The calling thread's CPU time for tests/common/testtiming.h (linked into every test by
// brulion_add_test, tests/CMakeLists.txt).
#include "testtiming.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <ctime>
#endif

namespace brulion::test {

qint64 threadCpuNs()
{
#ifdef Q_OS_WIN
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user))
        return 0;
    const auto hundredNs = [](const FILETIME &t) {
        return qint64((quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime);
    };
    return (hundredNs(kernel) + hundredNs(user)) * 100;
#else
    timespec ts{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return qint64(ts.tv_sec) * 1000000000 + ts.tv_nsec;
#endif
}

} // namespace brulion::test
