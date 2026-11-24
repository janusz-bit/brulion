#include "processmemory.h"

#include <QFile>
#include <QList>

#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
#include <unistd.h>
#if __has_include(<malloc.h>)
#include <malloc.h>
#endif
#endif

// The sanitizers bring their own allocator: glibc's call would trim a heap that nothing uses.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define BRULION_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#define BRULION_SANITIZED 1
#endif
#endif

#if defined(__GLIBC__) && !defined(Q_OS_ANDROID) && !defined(BRULION_SANITIZED)
#define BRULION_GLIBC_MALLOC 1
#endif

namespace brulion::platform {

bool canReleaseFreeMemory()
{
#if defined(BRULION_GLIBC_MALLOC)
    return true;
#elif defined(Q_OS_ANDROID) && (defined(M_PURGE_ALL) || defined(M_PURGE))
    return true;
#else
    return false;
#endif
}

bool keepsFreedMemory()
{
#if defined(BRULION_GLIBC_MALLOC)
    return true;
#else
    return false;
#endif
}

bool releaseFreeMemory()
{
#if defined(BRULION_GLIBC_MALLOC)
    malloc_trim(0);
    return true;
#elif defined(Q_OS_ANDROID) && (defined(M_PURGE_ALL) || defined(M_PURGE))
    // (Scudo: M_PURGE_ALL also empties the per-thread caches; it is newer — API 34 — than M_PURGE,
    // API 28: a system that refuses the one gets the other.)
#if defined(M_PURGE_ALL)
    if (mallopt(M_PURGE_ALL, 0) == 1)
        return true;
#endif
#if defined(M_PURGE)
    return mallopt(M_PURGE, 0) == 1;
#else
    return false;
#endif
#else
    return false;
#endif
}

qint64 residentBytes()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // "size resident shared text lib data dt", in pages.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> fields = statm.readAll().split(' ');
    if (fields.size() < 2)
        return -1;
    bool ok = false;
    const qint64 pages = fields.at(1).toLongLong(&ok);
    const long pageSize = sysconf(_SC_PAGESIZE);
    return ok && pageSize > 0 ? pages * pageSize : -1;
#else
    return -1;
#endif
}

} // namespace brulion::platform
