// processmemory.h: asking the allocator to give freed memory back. With glibc (not under a
// sanitizer, which brings its own allocator): what freed blocks leave behind in the heap — small
// ones, and large ones once glibc has moved its mmap threshold over them — stays with the process
// until releaseFreeMemory(), measured by the process's resident size. Elsewhere the calls say
// that there is nothing to do.
#include "processmemory.h"

#include <QTest>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(Q_OS_UNIX)
#include <unistd.h>
#endif

namespace platform = brulion::platform;

namespace {

constexpr qint64 MiB = 1024 * 1024;

// (Through a pointer the compiler cannot see through: a block allocated, filled and freed is not
// optimised away.)
void *(*volatile g_allocate)(std::size_t) = std::malloc;

/// A block of `bytes`, every page of it written to (resident).
void *block(std::size_t bytes)
{
    void *memory = g_allocate(bytes);
    if (memory)
        std::memset(memory, 0x5a, bytes);
    return memory;
}

QByteArray megabytes(const char *what, qint64 from, qint64 to)
{
    return QByteArray(what) + ": " + QByteArray::number(from / MiB) + " MB → " + QByteArray::number(to / MiB) + " MB";
}

#if defined(__GLIBC__) && !defined(Q_OS_ANDROID)
constexpr bool Glibc = true;
#else
constexpr bool Glibc = false;
#endif

long pageSize()
{
#if defined(Q_OS_UNIX)
    return sysconf(_SC_PAGESIZE);
#else
    return 4096;
#endif
}

} // namespace

class TestProcessMemory : public QObject {
    Q_OBJECT

private:
    /// `count` blocks of `bytes` with a small block that stays after each (the heap cannot give
    /// them back from its top), freed again: what the process held with them, after freeing them
    /// and after releaseFreeMemory().
    struct Sizes {
        qint64 held = 0, freed = 0, released = 0;
        bool called = false;
    };
    static Sizes heldFreedReleased(int count, std::size_t bytes)
    {
        std::vector<void *> blocks;
        std::vector<void *> pins;
        for (int i = 0; i < count; ++i) {
            blocks.push_back(block(bytes));
            pins.push_back(block(64));
        }
        Sizes sizes;
        sizes.held = platform::residentBytes();
        for (void *memory : blocks)
            std::free(memory);
        sizes.freed = platform::residentBytes();
        sizes.called = platform::releaseFreeMemory();
        sizes.released = platform::residentBytes();
        for (void *memory : pins)
            std::free(memory);
        return sizes;
    }

private Q_SLOTS:
    void whereItCan()
    {
        if (platform::canReleaseFreeMemory())
            return; // (the tests below)
        QVERIFY(!platform::releaseFreeMemory());
    }

    void residentSize()
    {
#if defined(Q_OS_LINUX)
        const qint64 before = platform::residentBytes();
        QVERIFY(before > 0);
        void *memory = block(std::size_t(64 * MiB));
        QVERIFY(memory);
        const qint64 with = platform::residentBytes();
        std::free(memory);
        QVERIFY2(with - before > 48 * MiB, megabytes("64 MB held", before, with).constData());
#else
        QCOMPARE(platform::residentBytes(), qint64(-1));
#endif
    }

    /// What many small blocks leave behind (a page's rows, an index being built).
    void smallBlocks()
    {
        if (!platform::canReleaseFreeMemory() || !Glibc)
            QSKIP("Not glibc's allocator (or a sanitizer's)");
        // Blocks of four pages (16 KiB where a page is 4 KiB), 64 MiB of them.
        const auto page = std::size_t(std::max<long>(4096, pageSize()));
        const Sizes sizes = heldFreedReleased(int(64 * MiB / qint64(4 * page)), 4 * page);
        QVERIFY(sizes.called);
        // Freed, yet still the process's …
        QVERIFY2(sizes.held - sizes.freed < 16 * MiB,
                 megabytes("64 MB of small blocks freed", sizes.held, sizes.freed).constData());
        // … until given back: the whole pages of every free block (3 of its 4).
        QVERIFY2(sizes.freed - sizes.released > 32 * MiB,
                 megabytes("given back", sizes.freed, sizes.released).constData());
    }

    /// Decoded pictures: megabytes each. The first large block freed moves glibc's mmap threshold
    /// over its size; the next ones come from the heap and stay there, pinned by what was
    /// allocated in between — the app looked as large as at its peak for as long as it ran.
    void largeBlocksOnceTheThresholdMoved()
    {
        if (!platform::canReleaseFreeMemory() || !Glibc)
            QSKIP("Not glibc's allocator (or a sanitizer's)");
        std::free(block(std::size_t(8 * MiB))); // moves the threshold to 8 MiB
        const Sizes sizes = heldFreedReleased(64, std::size_t(MiB));
        QVERIFY(sizes.called);
        QVERIFY2(sizes.held - sizes.freed < 16 * MiB,
                 megabytes("64 blocks of 1 MB freed", sizes.held, sizes.freed).constData());
        QVERIFY2(sizes.freed - sizes.released > 48 * MiB,
                 megabytes("given back", sizes.freed, sizes.released).constData());
    }

    /// Other allocators: the call is there and harmless.
    void elsewhere()
    {
        if (Glibc || !platform::canReleaseFreeMemory())
            QSKIP("glibc's allocator, or one with nothing to give back");
        const Sizes sizes = heldFreedReleased(256, 64 * 1024);
        QVERIFY(sizes.called);
        QVERIFY(sizes.released <= sizes.held);
    }
};

QTEST_GUILESS_MAIN(TestProcessMemory)
#include "tst_processmemory.moc"
