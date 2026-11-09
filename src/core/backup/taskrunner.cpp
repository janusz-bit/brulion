#include "backup/taskrunner_p.h"

#include <QMetaObject>

#include <chrono>
#include <utility>

namespace brulion::backup::detail {

using namespace std::chrono_literals;

namespace {
constexpr int InlineSliceMs = 8;
constexpr int PoolStepMs = 100;
} // namespace

TaskRunner::TaskRunner(notebook::WorkerPool pool, QObject *parent)
    : QObject(parent), m_pool(std::move(pool)), m_shared(std::make_shared<Shared>())
{
    m_shared->receiver = this;
    // 1 ms, not 0: a repeating 0 ms timer stalls Qt's timers in the browser (docs/WEB.md).
    m_slices.setInterval(1ms);
    connect(&m_slices, &QTimer::timeout, this, &TaskRunner::slice);
}

TaskRunner::~TaskRunner()
{
    std::lock_guard lock(m_shared->mutex);
    m_shared->receiver = nullptr;
}

void TaskRunner::start(std::shared_ptr<Task> task, std::function<void()> done)
{
    if (m_pool.isInline()) {
        m_task = std::move(task);
        m_done = std::move(done);
        m_slices.start();
        return;
    }
    m_pool.start([shared = m_shared, task = std::move(task), done = std::move(done)]() mutable {
        while (!task->step(PoolStepMs)) { }
        std::lock_guard lock(shared->mutex);
        if (shared->receiver)
            QMetaObject::invokeMethod(shared->receiver, std::move(done), Qt::QueuedConnection);
    });
}

void TaskRunner::slice()
{
    if (m_task && !m_task->step(InlineSliceMs))
        return;
    m_slices.stop();
    m_task.reset();
    if (const auto done = std::exchange(m_done, {}))
        done();
}

} // namespace brulion::backup::detail
