#include "processmemory.h"

#include <QFile>
#include <QList>

#include <cctype>
#include <utility>

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
// (The same builds: where it was tried. A sanitizer keeps its own view of the mappings.)
#define BRULION_PROGRAM_PAGES 1
#endif

#if defined(BRULION_PROGRAM_PAGES)
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <link.h>
#include <string>
#include <sys/mman.h>
#include <vector>
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

#if defined(BRULION_PROGRAM_PAGES)
namespace {

/// A mapping of the process that holds nothing of its own (see releaseProgramPages()).
struct CleanMapping {
    std::uintptr_t start = 0;
    std::uintptr_t end = 0;
    qint64 residentKb = 0;
};

/// /proc/self/smaps, whole. Plain system calls: this runs inside dl_iterate_phdr()'s callback.
std::string readSmaps()
{
    std::string text;
    const int fd = ::open("/proc/self/smaps", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return text;
    char buffer[65536];
    for (;;) {
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n <= 0)
            break;
        text.append(buffer, size_t(n));
    }
    ::close(fd);
    return text;
}

/// The value of a "Name:   123 kB" line, -1 when `line` is not that field.
qint64 smapsField(const char *line, const char *name)
{
    const size_t length = std::strlen(name);
    if (std::strncmp(line, name, length) != 0 || line[length] != ':')
        return -1;
    return std::strtoll(line + length + 1, nullptr, 10);
}

/// The mappings of files that are private, not writable, not locked, and have no page of their
/// own (no anonymous page: nothing was ever written into them), sorted by address.
std::vector<CleanMapping> cleanMappings(const std::string &smaps)
{
    std::vector<CleanMapping> out;
    CleanMapping current;
    bool candidate = false;
    const auto finish = [&] {
        if (candidate)
            out.push_back(current);
        candidate = false;
    };
    size_t at = 0;
    while (at < smaps.size()) {
        size_t eol = smaps.find('\n', at);
        if (eol == std::string::npos)
            eol = smaps.size();
        const char *line = smaps.c_str() + at;
        const size_t length = eol - at;
        at = eol + 1;
        // A mapping's first line: "start-end perms offset dev inode [path]" (lower-case hex first;
        // every field line starts with a capital letter).
        if (length > 0 && (std::isdigit(static_cast<unsigned char>(line[0])) || (line[0] >= 'a' && line[0] <= 'f'))) {
            finish();
            char *rest = nullptr;
            current = {};
            current.start = std::strtoull(line, &rest, 16);
            if (!rest || *rest != '-')
                continue;
            current.end = std::strtoull(rest + 1, &rest, 16);
            if (!rest || *rest != ' ' || std::strlen(rest) < 5)
                continue;
            const char *perms = rest + 1; // "r-xp"
            const char *path = static_cast<const char *>(std::memchr(line, '/', length));
            candidate = perms[0] == 'r' && perms[1] == '-' && perms[3] == 'p' && path != nullptr
                        && current.end > current.start;
            continue;
        }
        if (!candidate)
            continue;
        if (const qint64 kb = smapsField(line, "Rss"); kb >= 0) {
            current.residentKb = kb;
        } else if (const qint64 own = smapsField(line, "Anonymous"); own > 0) {
            candidate = false;
        } else if (const qint64 dirty = smapsField(line, "Private_Dirty"); dirty > 0) {
            candidate = false;
        } else if (const qint64 locked = smapsField(line, "Locked"); locked > 0) {
            candidate = false;
        } else if (std::strncmp(line, "VmFlags:", 8) == 0) {
            // mw would be fine (mprotect may make it writable later); lo: locked, pf / io: no pages
            // of a file, wr: writable after all.
            const std::string flags(line + 8, length - 8);
            for (const char *bad : {" wr", " lo", " pf", " io", " sh"}) {
                if (flags.find(bad) != std::string::npos)
                    candidate = false;
            }
        }
    }
    finish();
    return out;
}

struct ProgramSweep {
    std::vector<CleanMapping> mappings;
    std::uintptr_t pageSize = 4096;
    qint64 bytes = 0;
    bool read = false;
    bool first = true;
};

/// One loaded object: the pages of its segments that are not writable, where the kernel agrees
/// that they hold nothing of the process's own.
int sweepObject(struct dl_phdr_info *info, size_t, void *data)
{
    auto *sweep = static_cast<ProgramSweep *>(data);
    const bool program = std::exchange(sweep->first, false);
    // (The kernel's own object — the vDSO — has no file behind it; the program's name is empty.)
    if (!program && (!info->dlpi_name || !std::strchr(info->dlpi_name, '/')))
        return 0;
    // The libraries OpenGL calls go through stay as they are: a vendor's driver may rewrite their
    // entry points in place when a context is made or goes (libglvnd's patching), from the
    // render thread, at any moment — also between the look at the mappings and the call below.
    if (!program) {
        const char *base = std::strrchr(info->dlpi_name, '/') + 1;
        for (const char *family : {"libGL", "libEGL", "libOpenGL", "libglapi", "libnvidia"}) {
            if (std::strncmp(base, family, std::strlen(family)) == 0)
                return 0;
        }
    }
    if (!sweep->read) {
        // Read under the loader's lock: no object is unloaded between this and the calls below.
        sweep->mappings = cleanMappings(readSmaps());
        sweep->read = true;
    }
    const std::uintptr_t mask = sweep->pageSize - 1;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) &segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD || (segment.p_flags & PF_W) || segment.p_filesz == 0)
            continue;
        const std::uintptr_t begin = (info->dlpi_addr + segment.p_vaddr) & ~mask;
        const std::uintptr_t end = (info->dlpi_addr + segment.p_vaddr + segment.p_filesz + mask) & ~mask;
        for (const CleanMapping &mapping : sweep->mappings) {
            if (mapping.end <= begin)
                continue;
            if (mapping.start >= end)
                break;
            // Whole mappings only: a segment is one, and its resident size is the mapping's.
            if (mapping.start < begin || mapping.end > end || mapping.residentKb == 0)
                continue;
            if (::madvise(reinterpret_cast<void *>(mapping.start), mapping.end - mapping.start, MADV_DONTNEED) == 0)
                sweep->bytes += mapping.residentKb * 1024;
        }
    }
    return 0;
}

} // namespace
#endif

bool canReleaseProgramPages()
{
#if defined(BRULION_PROGRAM_PAGES)
    return true;
#else
    return false;
#endif
}

qint64 releaseProgramPages()
{
#if defined(BRULION_PROGRAM_PAGES)
    ProgramSweep sweep;
    if (const long size = sysconf(_SC_PAGESIZE); size > 0)
        sweep.pageSize = std::uintptr_t(size);
    dl_iterate_phdr(&sweepObject, &sweep);
    return sweep.bytes;
#else
    return 0;
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
