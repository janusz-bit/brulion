// TaskRunner — one long piece of work, a step at a time, on a notebook::WorkerPool's thread (the
// whole loop) or without a pool in slices on the owner's thread (WebAssembly). Architecture
// principle 8: core starts no threads. Internal to the backup module.
#pragma once

#include "notebook/indexloader.h"

#include <QObject>
#include <QTimer>

#include <functional>
#include <memory>
#include <mutex>

namespace brulion::backup::detail {

/// Work that can stop between steps. The object is used by one thread at a time.
class Task {
public:
    virtual ~Task() = default;
    /// Work for about `budgetMs` (a single file may take longer). True when finished.
    virtual bool step(int budgetMs) = 0;
};

class TaskRunner : public QObject {
public:
    explicit TaskRunner(notebook::WorkerPool pool, QObject *parent = nullptr);
    /// `done` is never called after this; a pool job still running finishes on its own (it holds
    /// the task, which holds no reference to the owner).
    ~TaskRunner() override;

    /// Run `task`; `done` follows on the owner's thread, on a later turn of its event loop.
    void start(std::shared_ptr<Task> task, std::function<void()> done);
    bool isInline() const { return m_pool.isInline(); }

private:
    struct Shared {
        std::mutex mutex;
        TaskRunner *receiver = nullptr;
    };
    void slice();

    notebook::WorkerPool m_pool;
    std::shared_ptr<Shared> m_shared;
    QTimer m_slices;
    std::shared_ptr<Task> m_task;
    std::function<void()> m_done;
};

} // namespace brulion::backup::detail
