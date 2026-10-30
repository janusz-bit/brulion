#include "graphsimulation.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QTimer>

#ifdef BRULION_GRAPH_THREADS
#include <QThread>
#endif

#include <algorithm>
#include <climits>
#include <cmath>

namespace brulion::planning {

namespace {

/// Steps of a run measured before the fast-cooling decision (a fixed step: deterministic).
constexpr int kDecisionTicks = 3;
/// Fast cooling: ~100 steps instead of ~300.
constexpr double kFastAlphaDecay = 0.067;

bool &inlineFlag()
{
    static bool inlineRuns = false;
    return inlineRuns;
}

} // namespace

/// The worker: owns the layout of the current run on its thread (or inline on the owner's).
class GraphSimWorker : public QObject {
public:
    GraphSimWorker(GraphSimulation *sim, std::shared_ptr<GraphSimulation::Shared> shared, bool threaded)
        : m_sim(sim), m_shared(std::move(shared)), m_threaded(threaded), m_timer(this)
    {
        m_timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { step(); });
    }

    /// Commands arrived (queued from the owner).
    void process()
    {
        m_shared->processPending.store(false);
        const bool changed = applyCommands();
        schedule(changed);
    }

    /// Before the thread quits (or the owner deletes an inline worker).
    void shutdown()
    {
        m_timer.stop();
        m_has = false;
#ifdef BRULION_GRAPH_THREADS
        if (m_threaded)
            QThread::currentThread()->quit();
#endif
    }

private:
    /// Drains the command queue; true when the layout's state changed.
    bool applyCommands()
    {
        std::vector<std::variant<SimRun, SimCommand>> items;
        {
            std::lock_guard lock(m_shared->commandMutex);
            items.swap(m_shared->commands);
            m_shared->interrupt.store(false);
        }
        bool changed = false;
        for (auto &item : items) {
            if (auto *run = std::get_if<SimRun>(&item)) {
                m_run = std::move(*run);
                m_has = true;
                m_paused = false;
                m_runTicks = 0;
                beginSegment();
                m_timer.setInterval(std::clamp(m_run.frameIntervalMs, 1, 1000));
                changed = true;
                continue;
            }
            const SimCommand &c = std::get<SimCommand>(item);
            switch (c.kind) {
            case SimCommand::Cancel:
                m_has = false;
                m_paused = false;
                m_timer.stop();
                break;
            case SimCommand::Pause:
                if (m_has && !m_paused) {
                    m_paused = true;
                    m_activeNs += m_clock.nsecsElapsed();
                    m_timer.stop();
                }
                break;
            case SimCommand::Resume:
                if (m_has && m_paused) {
                    m_paused = false;
                    m_clock.start();
                }
                break;
            case SimCommand::Fix:
                if (m_has) {
                    m_run.layout.fix(c.node, c.x, c.y);
                    changed = true;
                }
                break;
            case SimCommand::Unfix:
                if (m_has) {
                    m_run.layout.unfix(c.node);
                    changed = true;
                }
                break;
            case SimCommand::AlphaTarget:
                if (m_has) {
                    const bool wasDragging = m_run.layout.alphaTarget() > 0;
                    m_run.layout.setAlphaTarget(c.value);
                    if (wasDragging && !(c.value > 0))
                        beginSegment(); // the drag ended: the rest cools like a new run
                    changed = true;
                }
                break;
            case SimCommand::ReheatTo:
                if (m_has) {
                    m_run.layout.setAlpha(std::max(c.value, m_run.layout.alpha()));
                    beginSegment();
                    changed = true;
                }
                break;
            }
        }
        return changed;
    }

    /// After commands: step on (timer), or tell the owner the (idle) state.
    void schedule(bool changed)
    {
        if (!m_has) {
            m_timer.stop();
            return;
        }
        if (m_paused)
            return;
        if (m_run.layout.running()) {
            if (!m_timer.isActive())
                m_timer.start();
        } else if (changed) {
            publish(true);
        }
    }

    /// A new run segment: the normal cooling, a fresh decision, a fresh pacing clock.
    void beginSegment()
    {
        m_segTicks = 0;
        m_segTickMs = 0;
        m_fast = false;
        m_run.layout.params().alphaDecay = m_run.alphaDecay;
        m_expected = m_run.layout.expectedTicks();
        m_activeNs = 0;
        m_clock.start();
    }

    void step()
    {
        applyCommands(); // commands that came between two timer events
        if (!m_has || m_paused) {
            m_timer.stop();
            return;
        }
        ForceLayout &layout = m_run.layout;
        if (!layout.running()) {
            m_timer.stop();
            publish(true);
            return;
        }
        const bool dragging = layout.alphaTarget() > 0;
        long long allowed = LLONG_MAX; // reduced motion: as many as the budget allows
        if (dragging) {
            allowed = m_segTicks + 1;
        } else if (!m_run.reducedMotion && m_run.minRunMs > 0 && m_expected >= 0) {
            const double elapsedMs = double(m_activeNs + m_clock.nsecsElapsed()) / 1e6;
            const auto paced = static_cast<long long>(std::ceil(double(m_expected) * elapsedMs / double(m_run.minRunMs)));
            allowed = std::max<long long>(m_segTicks + 1, paced);
        }
        const double budgetMs = m_threaded ? double(std::max(1, m_run.frameIntervalMs))
                                           : std::clamp(0.5 * m_run.frameIntervalMs, 8.0, 25.0);
        QElapsedTimer batch;
        batch.start();
        int steps = 0;
        while (layout.running() && m_segTicks < allowed) {
            if (m_shared->interrupt.load(std::memory_order_relaxed))
                break;
            if (steps > 0 && double(batch.nsecsElapsed()) / 1e6 >= budgetMs)
                break;
            QElapsedTimer t;
            t.start();
            layout.tick();
            const double ms = double(t.nsecsElapsed()) / 1e6;
            ++steps;
            ++m_segTicks;
            ++m_runTicks;
            if (m_segTicks <= kDecisionTicks) {
                m_segTickMs += (ms - m_segTickMs) / double(m_segTicks);
                if (m_segTicks == kDecisionTicks && !dragging && !m_run.reducedMotion && m_segTickMs > m_run.slowTickMs) {
                    // Too slow to unfold within the pacing: cool down in ~100 steps.
                    m_fast = true;
                    layout.params().alphaDecay = kFastAlphaDecay;
                    const int rest = layout.expectedTicks();
                    m_expected = rest < 0 ? -1 : m_segTicks + rest;
                }
            }
        }
        if (!layout.running()) {
            m_timer.stop();
            publish(true);
        } else if (steps > 0 && !m_run.reducedMotion) {
            publish(false);
        }
    }

    void publish(bool final)
    {
        SimFrame frame;
        frame.generation = m_run.generation;
        frame.x = m_run.layout.xs();
        frame.y = m_run.layout.ys();
        frame.alpha = m_run.layout.alpha();
        frame.running = !final;
        frame.fastCooling = m_fast;
        frame.tickMs = m_segTickMs;
        frame.ticks = m_runTicks;
        if (final)
            frame.state = m_run.layout;
        bool notify = false;
        {
            std::lock_guard lock(m_shared->frameMutex);
            m_shared->latest = std::move(frame);
            notify = !m_shared->framePending;
            m_shared->framePending = true;
        }
        if (notify)
            QMetaObject::invokeMethod(m_sim, &GraphSimulation::frameReady, Qt::QueuedConnection);
    }

    GraphSimulation *m_sim;
    std::shared_ptr<GraphSimulation::Shared> m_shared;
    bool m_threaded;
    QTimer m_timer;

    bool m_has = false;
    bool m_paused = false;
    SimRun m_run;
    int m_runTicks = 0;
    // The current segment (a run, a reheat, the rest after a drag).
    int m_segTicks = 0;
    double m_segTickMs = 0;
    bool m_fast = false;
    int m_expected = 0;
    qint64 m_activeNs = 0;
    QElapsedTimer m_clock;
};

GraphSimulation::GraphSimulation(QObject *parent) : QObject(parent)
{
#ifdef BRULION_GRAPH_THREADS
    m_threaded = !inlineFlag();
#endif
}

GraphSimulation::~GraphSimulation()
{
    if (!m_worker)
        return;
    {
        std::lock_guard lock(m_shared->commandMutex);
        m_shared->commands.clear();
        m_shared->interrupt.store(true);
    }
#ifdef BRULION_GRAPH_THREADS
    if (m_thread) {
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->shutdown(); }, Qt::QueuedConnection);
        m_thread->wait();
        delete m_worker;
        delete m_thread;
        return;
    }
#endif
    m_worker->shutdown();
    delete m_worker;
}

void GraphSimulation::setInlineForTests(bool inlineRuns)
{
    inlineFlag() = inlineRuns;
}

bool GraphSimulation::inlineForTests()
{
    return inlineFlag();
}

void GraphSimulation::ensureWorker()
{
    if (m_worker)
        return;
    m_worker = new GraphSimWorker(this, m_shared, m_threaded);
#ifdef BRULION_GRAPH_THREADS
    if (m_threaded) {
        m_thread = new QThread;
        m_thread->setObjectName(QStringLiteral("brulion-graph"));
        m_worker->moveToThread(m_thread);
        m_thread->start();
    }
#endif
}

void GraphSimulation::wake()
{
    if (!m_shared->processPending.exchange(true))
        QMetaObject::invokeMethod(m_worker, [w = m_worker] { w->process(); }, Qt::QueuedConnection);
}

void GraphSimulation::start(SimRun run)
{
    ensureWorker();
    {
        std::lock_guard lock(m_shared->frameMutex);
        m_shared->latest.reset(); // frames of the replaced run
    }
    {
        std::lock_guard lock(m_shared->commandMutex);
        m_shared->commands.emplace_back(std::move(run));
        m_shared->interrupt.store(true);
    }
    wake();
}

void GraphSimulation::command(const SimCommand &command)
{
    if (!m_worker)
        return; // nothing runs: nothing to command
    {
        std::lock_guard lock(m_shared->commandMutex);
        auto &queue = m_shared->commands;
        // A drag sends a Fix per pointer move: only the newest position of a node counts.
        if (command.kind == SimCommand::Fix && !queue.empty()) {
            if (auto *last = std::get_if<SimCommand>(&queue.back()); last && last->kind == SimCommand::Fix && last->node == command.node) {
                *last = command;
                return; // already woken for the replaced one
            }
        }
        queue.emplace_back(command);
        if (command.kind == SimCommand::Cancel || command.kind == SimCommand::Pause)
            m_shared->interrupt.store(true);
    }
    if (command.kind == SimCommand::Cancel) {
        std::lock_guard lock(m_shared->frameMutex);
        m_shared->latest.reset();
    }
    wake();
}

bool GraphSimulation::takeFrame(SimFrame &out)
{
    std::lock_guard lock(m_shared->frameMutex);
    m_shared->framePending = false;
    if (!m_shared->latest)
        return false;
    out = std::move(*m_shared->latest);
    m_shared->latest.reset();
    return true;
}

} // namespace brulion::planning
