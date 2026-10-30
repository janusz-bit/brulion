// GraphModel — the link graph of the vault for GraphView.qml / GraphItem: nodes (pages, daily
// notes, #tags, pages that do not exist yet) and undirected edges from VaultIndex::graph(), laid
// out by ForceLayout on a worker thread (GraphSimulation).
//
//   GraphModel { vaultIndex: PlanningContext.vaultIndex; showTags: true; focusPath: route.path; depth: 1 }
//
// * Toggles: showTags, showMissing (links to pages that do not exist), showJournals,
//   showOrphans (nodes without edges), showTemplates (pages in the vault's templatesFolder, off by
//   default: templates link to everything they will be used for; tags and missing pages only
//   templates link to go with them). The local graph's centre is shown even when it is a template.
// * Local graph: with `focusPath` set (a node id: a page path) only the nodes within `depth`
//   (1 or 2) links of it are kept.
// * `maxNodes` caps the graph (the best connected nodes win, the focus always stays):
//   `truncated` / `totalNodeCount` tell the view. Phones and the browser use a lower cap.
// * The simulation runs on a worker thread while `active` (GraphSimulation; inline on the GUI
//   thread in slices in the browser, which has no threads) and stops when it has cooled down
//   (`running`). The GUI thread never runs a step: it keeps a mirror of the newest positions
//   (x(), y(), position(), bounds(), what GraphItem draws and hit-tests) that the worker's frames
//   update, at most one per `frameInterval` ms. A run unfolds over about a second (pacing); a run
//   whose first steps are slow cools down in ~100 steps instead of ~300 (`fastCooling`). Hidden
//   (`active` false) the run pauses and resumes where it was.
// * With reducedMotion the run cools down fast and only its final layout is shown; a layout that
//   starts from fresh positions is `settling` until then (the view shows a spinner instead of a
//   spiral that jumps).
// * Dragging (beginDrag / dragTo / endDrag): the dragged node follows the pointer at once (the
//   mirror), the worker pins it and keeps the simulation warm.
// * Index changes are applied after `refreshDelay` ms: nodes keep their positions, new ones
//   start next to a neighbour, and the simulation is reheated a little.
// * Deterministic: the same index, settings and `seed` give the same layout (see ForceLayout),
//   threaded or not, whatever the frame timing.
//
// The C++ side (GraphItem) reads nodes(), edges(), adjacency() and the positions directly.
// layout() is the authoritative ForceLayout while no run is on the worker (runActive() false: an
// inactive model, tests stepping it with step()); during a run it holds the run's start state.
#pragma once

#include "forcelayout.h"
#include "graphsimulation.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <vector>

namespace brulion {
class VaultIndex;
}

namespace brulion::planning {

class GraphModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QObject *vaultIndex READ vaultIndexObject WRITE setVaultIndexObject NOTIFY vaultIndexChanged FINAL)
    Q_PROPERTY(bool showTags READ showTags WRITE setShowTags NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showMissing READ showMissing WRITE setShowMissing NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showJournals READ showJournals WRITE setShowJournals NOTIFY settingsChanged FINAL)
    Q_PROPERTY(bool showOrphans READ showOrphans WRITE setShowOrphans NOTIFY settingsChanged FINAL)
    /// Pages in the vault's templates folder (VaultConfig::templatesFolder); default false.
    Q_PROPERTY(bool showTemplates READ showTemplates WRITE setShowTemplates NOTIFY settingsChanged FINAL)
    /// Local graph centre (a node id — a page path); "" = the whole graph.
    Q_PROPERTY(QString focusPath READ focusPath WRITE setFocusPath NOTIFY settingsChanged FINAL)
    /// Local graph depth (1–3).
    Q_PROPERTY(int depth READ depth WRITE setDepth NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int seed READ seed WRITE setSeed NOTIFY settingsChanged FINAL)
    /// "pl" (default) | "en": daily notes are labelled with their date ("wt 13 paź").
    Q_PROPERTY(QString locale READ locale WRITE setLocale NOTIFY settingsChanged FINAL)
    Q_PROPERTY(int maxNodes READ maxNodes WRITE setMaxNodes NOTIFY settingsChanged FINAL)
    /// Run the simulation (bind to the view's visibility).
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged FINAL)
    /// Least milliseconds between two frames of a moving layout (default 16; the browser 33).
    Q_PROPERTY(int frameInterval READ frameInterval WRITE setFrameInterval NOTIFY frameIntervalChanged FINAL)
    /// The current run cools down in ~100 steps: its first steps were slow (see above).
    Q_PROPERTY(bool fastCooling READ fastCooling NOTIFY fastCoolingChanged FINAL)
    /// Reduced motion and a layout from fresh positions: nothing to show until it has settled.
    Q_PROPERTY(bool settling READ settling NOTIFY settlingChanged FINAL)
    /// The simulation runs on a thread of its own (false in the browser; diagnostics).
    Q_PROPERTY(bool threaded READ threaded CONSTANT FINAL)
    /// Debounce of rebuilds after index changes, ms (default 300).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)

    Q_PROPERTY(bool running READ running NOTIFY runningChanged FINAL)
    Q_PROPERTY(double alpha READ alpha NOTIFY positionsChanged FINAL)
    Q_PROPERTY(int nodeCount READ nodeCount NOTIFY structureChanged FINAL)
    Q_PROPERTY(int edgeCount READ edgeCount NOTIFY structureChanged FINAL)
    /// Nodes before the cap (and before the local-graph cut).
    Q_PROPERTY(int totalNodeCount READ totalNodeCount NOTIFY structureChanged FINAL)
    Q_PROPERTY(bool truncated READ truncated NOTIFY structureChanged FINAL)
    /// focusPath is set but not part of the graph (deleted page, filtered out).
    Q_PROPERTY(bool focusMissing READ focusMissing NOTIFY structureChanged FINAL)
    /// Notes in the vault (0 = an empty vault: the view shows its empty state).
    Q_PROPERTY(int pageCount READ pageCount NOTIFY structureChanged FINAL)

public:
    enum class Kind { Page, Journal, Tag, Missing };

    struct Node {
        QString id;    ///< note path, "#tag", or the missing page's name
        QString title;
        QString path;  ///< notes: the path; missing pages: "" (see id); tags: ""
        Kind kind = Kind::Page;
        int degree = 0;
        double radius = 4; ///< world units
    };

    explicit GraphModel(QObject *parent = nullptr);
    ~GraphModel() override;

    VaultIndex *vaultIndex() const;
    void setVaultIndex(VaultIndex *index);
    QObject *vaultIndexObject() const;
    void setVaultIndexObject(QObject *index);

    bool showTags() const { return m_showTags; }
    void setShowTags(bool on);
    bool showMissing() const { return m_showMissing; }
    void setShowMissing(bool on);
    bool showJournals() const { return m_showJournals; }
    void setShowJournals(bool on);
    bool showOrphans() const { return m_showOrphans; }
    void setShowOrphans(bool on);
    bool showTemplates() const { return m_showTemplates; }
    void setShowTemplates(bool on);
    QString focusPath() const { return m_focus; }
    void setFocusPath(const QString &path);
    int depth() const { return m_depth; }
    void setDepth(int depth);
    int seed() const { return int(m_seed); }
    void setSeed(int seed);
    QString locale() const { return m_locale; }
    void setLocale(const QString &locale);
    int maxNodes() const { return m_maxNodes; }
    void setMaxNodes(int max);
    bool active() const { return m_active; }
    void setActive(bool active);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool on);
    int frameInterval() const { return m_frameInterval; }
    void setFrameInterval(int msec);
    int refreshDelay() const { return m_refreshTimer.interval(); }
    void setRefreshDelay(int msec);
    bool fastCooling() const { return m_fastCooling; }
    bool settling() const { return m_settling; }
    bool threaded() const { return m_sim.threaded(); }
    /// Average step cost (ms) above which a run cools down fast (default 5; tests: 0 = always,
    /// a huge value = never). Applies to runs started afterwards.
    double slowTickMs() const { return m_slowTickMs; }
    void setSlowTickMs(double ms) { m_slowTickMs = ms; }
    /// A normal run unfolds over at least this long, ms (default 1000; 0 = as fast as it can).
    int minRunMs() const { return m_minRunMs; }
    void setMinRunMs(int ms) { m_minRunMs = std::max(0, ms); }
    /// Average cost of the current run's first steps, ms (measured by the worker).
    double tickCostMs() const { return m_tickMs; }
    /// A run is on the worker (the mirror holds the positions; layout() its start state).
    bool runActive() const { return m_simOwned; }
    /// Steps of the current run as of the newest frame (diagnostics, tests).
    int runTicks() const { return m_runTicks; }
    /// Frames applied / dropped because they belonged to an older run (tests).
    int framesApplied() const { return m_framesApplied; }
    int staleFramesDropped() const { return m_staleFrames; }

    bool running() const { return m_running; }
    double alpha() const { return m_simOwned ? m_alpha : m_layout.alpha(); }
    int nodeCount() const { return int(m_nodes.size()); }
    int edgeCount() const { return m_layout.edgeCount(); }
    int totalNodeCount() const { return m_totalNodes; }
    bool truncated() const { return m_truncated; }
    bool focusMissing() const { return m_focusMissing; }
    int pageCount() const { return m_pageCount; }

    const QList<Node> &nodes() const { return m_nodes; }
    const std::vector<std::pair<int, int>> &edges() const { return m_layout.edges(); }
    const QList<QList<int>> &adjacency() const { return m_adjacency; }
    /// The authoritative layout while runActive() is false (see the header comment).
    const ForceLayout &layout() const { return m_layout; }
    ForceLayout &layout() { return m_layout; }
    /// Node index of an id, -1 when not in the graph.
    Q_INVOKABLE int indexOf(const QString &id) const { return m_indexOf.value(id, -1); }
    /// Positions as drawn (world units): the worker's newest frame during a run, else layout().
    const std::vector<double> &xs() const { return m_simOwned ? m_x : m_layout.xs(); }
    const std::vector<double> &ys() const { return m_simOwned ? m_y : m_layout.ys(); }
    QPointF position(int node) const;

    /// Nodes whose title (or path) matches `query` (case- and diacritic-insensitive): exact,
    /// then prefix, then word prefix, then substring; best connected first. [{ id, title,
    /// path, kind, degree }].
    Q_INVOKABLE QVariantList find(const QString &query, int limit = 8) const;
    /// { id, title, path, kind ("page" | "journal" | "tag" | "missing"), degree, exists } or {}.
    Q_INVOKABLE QVariantMap nodeInfo(const QString &id) const;
    /// Ids of the neighbours of a node.
    Q_INVOKABLE QStringList neighbors(const QString &id) const;
    /// Warm the simulation up again (alpha at least `alpha`).
    Q_INVOKABLE void reheat(double alpha = 0.3);
    /// Start over from fresh positions.
    Q_INVOKABLE void relayout();
    /// Run `ticks` simulation steps now, on this thread (tests, warm-up): a run on the worker is
    /// cancelled and the steps continue from its newest positions (velocities 0).
    Q_INVOKABLE void step(int ticks = 1);
    /// Rebuild the graph from the index now.
    Q_INVOKABLE void refresh();
    /// World bounds of the nodes (radii included).
    Q_INVOKABLE QRectF bounds() const;

    /// Dragging a node (GraphItem): pin it, move it, release it.
    void beginDrag(int node);
    void dragTo(int node, QPointF world);
    void endDrag(int node);

    static QString kindName(Kind kind);

Q_SIGNALS:
    void vaultIndexChanged();
    void settingsChanged();
    void activeChanged();
    void reducedMotionChanged();
    void frameIntervalChanged();
    void refreshDelayChanged();
    void runningChanged();
    void fastCoolingChanged();
    void settlingChanged();
    /// The nodes or edges changed (positions too).
    void structureChanged();
    /// Positions changed (a simulation step, a drag).
    void positionsChanged();

private:
    void scheduleRebuild();
    void rebuild();
    void updateRunning();
    void settingsEdited();
    double alphaDecay() const;
    /// Hands m_layout to the worker when active, not empty and not cooled down yet.
    void ensureRunning();
    void startRun();
    /// Ends the worker's run; m_layout continues from the newest frame (velocities 0).
    void takeBack();
    void onFrameReady();
    void setFastCooling(bool on);
    void setSettling(bool on);

    QPointer<VaultIndex> m_index;
    QMetaObject::Connection m_indexConnection;
    bool m_showTags = false;
    bool m_showMissing = false;
    bool m_showJournals = true;
    bool m_showOrphans = true;
    bool m_showTemplates = false;
    QString m_focus;
    int m_depth = 1;
    std::uint32_t m_seed = 1;
    QString m_locale = QStringLiteral("pl");
    int m_maxNodes = 4000;
    bool m_active = true;
    bool m_reducedMotion = false;
    int m_frameInterval = 16;

    QList<Node> m_nodes;
    QHash<QString, int> m_indexOf;
    QList<QList<int>> m_adjacency;
    ForceLayout m_layout;
    int m_totalNodes = 0;
    bool m_truncated = false;
    bool m_focusMissing = false;
    int m_pageCount = 0;
    bool m_running = false;
    int m_dragging = -1;
    QPointF m_dragPos;
    /// The layout starts from fresh positions (no carried ones): `settling` with reduced motion.
    bool m_fresh = false;

    // The run on the worker.
    GraphSimulation m_sim;
    quint64 m_generation = 0;     ///< id of the current run (frames of other runs are dropped)
    bool m_simOwned = false;      ///< the worker owns the layout (startRun … takeBack / cancel)
    bool m_simRunning = false;    ///< it is stepping (the newest frame, or a command that heats it)
    std::vector<double> m_x, m_y; ///< the mirror: positions of the newest frame
    double m_alpha = 0;
    bool m_fastCooling = false;
    bool m_settling = false;
    double m_tickMs = 0;
    double m_slowTickMs = 5;
    int m_minRunMs = 1000;
    int m_runTicks = 0;
    int m_framesApplied = 0;
    int m_staleFrames = 0;

    QTimer m_refreshTimer;
};

} // namespace brulion::planning
