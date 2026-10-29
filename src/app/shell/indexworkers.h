// indexWorkerPool — the threads the app gives the index loader (notebook::IndexLoader, L1 of
// docs/PERFORMANCE.md): a dedicated QThreadPool (the import/export jobs of the global pool never
// wait for it, nor it for them), clamp(idealThreadCount − 1, 1, 4) parse workers plus one for
// the full-text index, at low priority so the GUI thread always comes first. Single-threaded
// builds (WebAssembly) get no pool: the loader works in slices on the GUI thread, as before.
// BRULION_INDEX_THREADS=<n> overrides the number of workers (0 = in slices on the GUI thread),
// for measurements and tests.
#pragma once

#include "notebook/indexloader.h"

#include <QThread>
#include <QtGlobal>

#include <algorithm>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#include <QThreadPool>
#define BRULION_INDEX_THREADS 1
#endif

namespace brulion::shell {

/// The number of parse workers for this machine (BRULION_INDEX_THREADS overrides).
inline int indexWorkerCount()
{
#ifdef BRULION_INDEX_THREADS
    bool ok = false;
    const int forced = qEnvironmentVariableIntValue("BRULION_INDEX_THREADS", &ok);
    if (ok)
        return std::clamp(forced, 0, 64);
    return std::clamp(QThread::idealThreadCount() - 1, 1, 4);
#else
    return 0;
#endif
}

#ifdef BRULION_INDEX_THREADS
/// A worker pool on `pool` (owned by the caller, outliving every loader that uses it).
inline notebook::WorkerPool indexWorkerPool(QThreadPool *pool)
{
    notebook::WorkerPool out;
    out.threads = indexWorkerCount();
    if (out.threads <= 0 || !pool)
        return {};
    pool->setMaxThreadCount(out.threads + 1);
    pool->setThreadPriority(QThread::LowPriority);
    out.start = [pool](std::function<void()> job) { pool->start(std::move(job)); };
    return out;
}
#endif

} // namespace brulion::shell
