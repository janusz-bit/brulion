#include "sync/pool_p.h"

#include "sync/util_p.h"

#include <utility>

namespace brulion::sync::detail {

JobPool::JobPool(int limit, QObject *context)
    : m_context(context)
    , m_limit(qMax(1, limit))
{
}

void JobPool::add(Job job)
{
    m_queue.append(std::move(job));
    if (m_started)
        schedulePump();
}

void JobPool::start(std::function<void()> idle)
{
    m_idle = std::move(idle);
    m_started = true;
    pump();
}

void JobPool::stop()
{
    if (m_stopped)
        return;
    m_stopped = true;
    if (m_started)
        schedulePump(); // reports idle once the running jobs are done
}

void JobPool::pump()
{
    if (!m_context)
        return;
    const std::shared_ptr<JobPool> keepAlive = shared_from_this(); // `idle` may drop the last owner
    // At most `limit` starts per call: jobs that finish synchronously post the next pump, so the
    // event loop keeps running between batches.
    for (int started = 0; started < m_limit && !m_stopped && m_running < m_limit && !m_queue.isEmpty(); ++started) {
        Job job = m_queue.takeFirst();
        ++m_running;
        auto called = std::make_shared<bool>(false);
        job([self = shared_from_this(), called] {
            if (std::exchange(*called, true))
                return;
            self->finishOne();
        });
    }
    if (m_running == 0 && (m_queue.isEmpty() || m_stopped) && m_idle) {
        const std::function<void()> idle = std::exchange(m_idle, nullptr);
        idle();
    }
}

void JobPool::schedulePump()
{
    if (m_pumpScheduled || !m_context)
        return;
    m_pumpScheduled = true;
    post(m_context, [self = shared_from_this()] {
        self->m_pumpScheduled = false;
        self->pump();
    });
}

void JobPool::finishOne()
{
    --m_running;
    schedulePump();
}

} // namespace brulion::sync::detail
