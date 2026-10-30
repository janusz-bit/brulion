// GraphSimulation — runs a ForceLayout off the GUI thread for GraphModel (docs/PERFORMANCE.md,
// L16). One dedicated QThread ("brulion-graph") per simulation, started with the first run, with a
// worker object, its own event loop and a pacing timer; only value copies cross threads (a run in,
// frames out), one mutex for the newest frame and one for the command queue.
//
//   GraphSimulation sim;
//   connect(&sim, &GraphSimulation::frameReady, …);    // queued to the owner's thread
//   sim.start(SimRun{generation, layout, …});          // the worker owns the layout from now on
//   sim.command({SimCommand::Fix, node, x, y});        // drag, reheat, pause, …
//   SimFrame frame; if (sim.takeFrame(frame)) …         // the newest frame (older ones coalesce)
//
// * Pacing: a normal run unfolds over at least `minRunMs` (several steps per frame, at most one
//   frame per `frameIntervalMs`); a run that cannot keep up runs flat out. While a node is dragged
//   (alpha target > 0) exactly one step per frame, like d3's drag behaviour.
// * Fast cooling (replaces N5's time-boxing): after the first 3 steps of a run (a fixed step, so the
//   result is deterministic for a given decision) a run whose steps cost more than `slowTickMs` on
//   average cools in ~100 steps instead of ~300. Not while dragging, not with reduced motion.
// * Reduced motion: the run is computed without intermediate frames; only the final one is
//   published.
// * The worker keeps the layout after a run has cooled down, so commands that arrive late (a drag
//   that starts just as the run ends) still apply: it runs again and publishes new frames. Every
//   command batch that leaves it idle publishes a final frame (with the full state).
// * Where the build has no threads (WebAssembly) — or when a test asks for it — the same worker
//   lives on the owner's thread and runs in slices of at most `clamp(frameIntervalMs / 2, 8, 25)`
//   ms per timer event (never a 0 ms timer: docs/PERFORMANCE.md N13).
// * Deterministic: the frames of a run are the states after whole steps of the same sequence of
//   ForceLayout steps whatever the timing, pauses or coalescing (commands change it, of course).
#pragma once

#include "forcelayout.h"

#include <QObject>
#include <QPointer>
#include <QtGlobal>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>
#include <vector>

#if QT_CONFIG(thread) && !defined(Q_OS_WASM)
#define BRULION_GRAPH_THREADS 1
#endif

class QThread;

namespace brulion::planning {

/// One run of the force layout as the owner hands it to the worker.
struct SimRun {
    quint64 generation = 0;      ///< the owner's run id (frames of other runs are dropped)
    ForceLayout layout;          ///< the start state (the worker owns its copy)
    bool reducedMotion = false;  ///< compute to the end, publish only the final frame
    double alphaDecay = 0.0228;  ///< normal cooling (~300 steps)
    double slowTickMs = 5.0;     ///< fast cooling when the first steps cost more than this, ms
    int frameIntervalMs = 16;    ///< at most one frame per interval
    int minRunMs = 1000;         ///< a normal run unfolds over at least this long (animation)
};

/// What the worker publishes (the newest one wins; an unread older frame is replaced).
struct SimFrame {
    quint64 generation = 0;
    std::vector<double> x, y;        ///< node positions (world units)
    double alpha = 0;
    bool running = true;             ///< false: the layout has cooled down — `state` holds it
    bool fastCooling = false;
    double tickMs = 0;               ///< average cost of the run's first steps, ms
    int ticks = 0;                   ///< steps of the current run so far (diagnostics, tests)
    std::optional<ForceLayout> state; ///< the full state (velocities included), final frames only
};

/// Commands from the owner, applied by the worker before its next step, in order.
struct SimCommand {
    enum Kind {
        Fix,         ///< pin `node` at (x, y)
        Unfix,       ///< release `node`
        AlphaTarget, ///< alpha target = value (0.3 while dragging; back to 0 starts a new run segment)
        ReheatTo,    ///< alpha = max(value, alpha), a new run segment (pacing, cooling decision)
        Pause,       ///< stop stepping, keep the state (the view is hidden)
        Resume,
        Cancel,      ///< drop the run (no more frames)
    };
    Kind kind = Fix;
    int node = -1;
    double x = 0;
    double y = 0;
    double value = 0;
};

class GraphSimWorker;

class GraphSimulation : public QObject {
    Q_OBJECT
public:
    explicit GraphSimulation(QObject *parent = nullptr);
    /// Cancels the run, quits and joins the thread (waits for at most the step in progress).
    ~GraphSimulation() override;

    /// Replaces any current run (its unread frames are dropped).
    void start(SimRun run);
    void command(const SimCommand &command);
    /// The newest unread frame (owner's thread); false: none.
    bool takeFrame(SimFrame &out);
    /// Runs on a thread of its own (false in the browser and under setInlineForTests(true)).
    bool threaded() const { return m_threaded; }

    /// Tests: simulations created afterwards run on the calling thread even where threads exist.
    static void setInlineForTests(bool inlineRuns);
    static bool inlineForTests();

    // ---- worker side (GraphSimWorker) ----------------------------------------------------------
    struct Shared {
        std::mutex commandMutex;
        std::vector<std::variant<SimRun, SimCommand>> commands;
        std::atomic<bool> processPending{false};
        /// Set when a command must stop the step loop early (a new run, cancel, pause, shutdown).
        std::atomic<bool> interrupt{false};
        std::mutex frameMutex;
        std::optional<SimFrame> latest;
        bool framePending = false; ///< frameReady queued and not taken yet (under frameMutex)
    };

Q_SIGNALS:
    /// A new frame can be taken (queued to the owner's thread; at most one pending).
    void frameReady();

private:
    void ensureWorker();
    void wake();

    std::shared_ptr<Shared> m_shared = std::make_shared<Shared>();
    GraphSimWorker *m_worker = nullptr;
    QThread *m_thread = nullptr;
    bool m_threaded = false;
};

} // namespace brulion::planning
