// processmemory — asking the process's allocator to give freed memory back to the system
// (docs/PERFORMANCE.md "Pictures and memory"), and the process to let go of the program's own
// pages it no longer uses (releaseProgramPages(); docs/PERFORMANCE.md "Memory: only what is in use").
//
// Linux with glibc (the desktop, the AppImage, the Nix package): its malloc keeps what was freed.
// A block over its mmap threshold is mapped on its own and unmapped when freed — but the threshold
// moves up to the largest such block freed (at most 32 MiB), so after the first decoded photo was
// freed every later one came from a heap, and a heap gives pages back by itself only from its top.
// Opening a page of forty photos and leaving it left 1.4–1.7 GB resident with 320 MB in use (measured
// 2026-11-24). releaseFreeMemory() gives back the free pages inside the heaps of every thread
// (malloc_trim): the app calls it once it is quiet after something large was let go
// (ui/memorykeeper.h).
//
// (Not done: a fixed threshold, mallopt(M_MMAP_THRESHOLD), so that large blocks are always unmapped
// the moment they are freed. Measured with 512 KiB it left 2 % less resident than the call above
// alone, and every large block — a long page's text, its block list — then starts as fresh pages
// the kernel has to map and zero, each time one is allocated.)
//
// Android (Scudo) gives freed pages back by itself, a moment after they were freed;
// releaseFreeMemory() makes it purge what it still caches (mallopt M_PURGE*) — worth it when the
// app leaves the foreground, not after every page (what was purged is mapped again when it is
// needed next). Windows and the browser: nothing — Windows' heap unmaps large blocks itself, a
// browser's linear memory never shrinks.
#pragma once

#include <QtGlobal>

namespace brulion::platform {

/// Whether releaseFreeMemory() does anything on this platform (glibc, Android).
bool canReleaseFreeMemory();

/// The allocator keeps what was freed until it is asked (glibc): asking is worth it whenever the
/// app is quiet after something large was let go, not only when it leaves the foreground.
bool keepsFreedMemory();

/// Gives the allocator's free memory back to the system. Thread-safe; it takes milliseconds
/// (0–7 ms with a process of 500–800 MB, measured) and holds each thread's heap in turn while it
/// runs — call it from a worker thread, when the app is quiet (app: ui/memorykeeper.h). False
/// where the platform has no such call.
bool releaseFreeMemory();

/// The memory the process holds resident, in bytes (Linux and Android: /proc/self/statm; -1
/// elsewhere).
qint64 residentBytes();

/// Whether releaseProgramPages() does anything on this platform (Linux with glibc).
bool canReleaseProgramPages();

/// Lets go of the pages of the program and of its libraries that the process has mapped — their
/// code and read-only data, 160–190 MB after a start, most of it read once by the start itself
/// (relocation and symbol tables, initialisers, the code only a start runs). They are clean pages
/// of files: let go of, they stay in the system's page cache, where they are the first thing
/// given to whoever needs memory, and one the process touches again is mapped again without any
/// reading while it is still cached (a minor fault: microseconds). Worth it when the start is
/// over and whenever the app is away (app: ui/memorykeeper.h); not while it is used — what is in
/// use only comes back, a fault at a time.
///
/// Only what cannot hold anything of the process's own: the segments of the loaded ELF objects
/// that are not writable (the loader's lock is held meanwhile: none can go), and of those only
/// the mappings the kernel reports as file-backed, private, not writable, not locked and without
/// a single anonymous or swapped-out page (/proc/self/smaps: a debugger's breakpoint or a text
/// relocation makes one) — madvise(MADV_DONTNEED) would throw such a page away. The libraries OpenGL calls go
/// through (libGL*, libEGL*, libOpenGL*, libglapi*, libnvidia*) are left alone: a driver may
/// rewrite their entry points in place at any moment.
///
/// Thread-safe; milliseconds (it reads /proc/self/smaps); call it from a worker thread. Returns
/// the resident bytes of the ranges let go (0: nothing, or not on this platform).
qint64 releaseProgramPages();

} // namespace brulion::platform
