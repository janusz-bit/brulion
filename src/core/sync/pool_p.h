// Internal: a tiny pool running asynchronous jobs with bounded concurrency.
#pragma once

#include <QList>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

namespace brulion::sync::detail {

/// Runs queued asynchronous jobs with at most `limit` in flight. A job receives a `done`
/// callback it must call exactly once (synchronously or later; extra calls are ignored). Jobs
/// may add() more jobs. After a job finishes, the next ones start from `context`'s event loop,
/// so a long queue of synchronous jobs neither recurses nor blocks the loop for long. Nothing
/// runs once `context` is destroyed. Create with std::make_shared (jobs keep it alive).
class JobPool : public std::enable_shared_from_this<JobPool> {
public:
    using Done = std::function<void()>;
    using Job = std::function<void(Done)>;

    JobPool(int limit, QObject *context);

    void add(Job job);
    /// Start running jobs; `idle` runs once when no job is running and none is left to start
    /// (possibly synchronously, e.g. for an empty queue).
    void start(std::function<void()> idle);
    /// Start no further jobs; the running ones finish, then `idle` follows.
    void stop();
    bool isStopped() const { return m_stopped; }
    int running() const { return m_running; }

private:
    void pump();
    void schedulePump();
    void finishOne();

    QPointer<QObject> m_context;
    QList<Job> m_queue;
    std::function<void()> m_idle;
    int m_limit;
    int m_running = 0;
    bool m_started = false;
    bool m_stopped = false;
    bool m_pumpScheduled = false;
};

} // namespace brulion::sync::detail
