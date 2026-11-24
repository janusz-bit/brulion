// processmemory — asking the process's allocator to give freed memory back to the system
// (docs/PERFORMANCE.md "Pictures and memory").
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

} // namespace brulion::platform
