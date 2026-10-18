// GraphModel — the link graph of the vault for GraphView.qml / GraphItem: nodes (pages, daily
// notes, #tags, pages that do not exist yet) and undirected edges from VaultIndex::graph(), laid
// out by ForceLayout on a timer.
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
// * The simulation runs on a timer (tickInterval ms) while `active` and stops when it has
//   cooled down (`running`). With reducedMotion it cools down fast and is computed in bursts
//   without intermediate frames, so nothing moves on screen.
// * Slow devices (`timeBoxed`): when the first steps of a run, together with painting the view
//   (GraphItem reports its paint time), cost more than a 30 fps frame, the rest of the run cools
//   down in about 100 steps instead of 300, computed in bursts of several steps per timer event;
//   the view repaints only now and then while it moves (GraphItem) and shows the frames of at
//   most the first two seconds — then the rest is computed without frames and shown when done.
//   The decision is measured on the device (phones, the browser, an old laptop alike) and taken
//   at a fixed step, so the layout of a slow run is deterministic too.
// * Index changes are applied after `refreshDelay` ms: nodes keep their positions, new ones
//   start next to a neighbour, and the simulation is reheated a little.
// * Deterministic: the same index, settings and `seed` give the same layout (see ForceLayout).
//
// The C++ side (GraphItem) reads nodes(), edges(), adjacency() and layout() directly.
#pragma once

#include "forcelayout.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>

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
    /// Milliseconds between simulation steps (default 16; phones/browser 33).
    Q_PROPERTY(int tickInterval READ tickInterval WRITE setTickInterval NOTIFY tickIntervalChanged FINAL)
    /// Debounce of rebuilds after index changes, ms (default 300).
    Q_PROPERTY(int refreshDelay READ refreshDelay WRITE setRefreshDelay NOTIFY refreshDelayChanged FINAL)
    /// The current run is time-boxed because this device is slow (see above).
    Q_PROPERTY(bool timeBoxed READ timeBoxed NOTIFY timeBoxedChanged FINAL)

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
    int tickInterval() const { return m_tickInterval; }
    void setTickInterval(int msec);
    int refreshDelay() const { return m_refreshTimer.interval(); }
    void setRefreshDelay(int msec);
    bool timeBoxed() const { return m_timeBoxed; }
    /// Frame cost (one step + one paint, ms) above which a run is time-boxed (default 33; tests:
    /// 0 = always, a huge value = never).
    double slowFrameMs() const { return m_slowFrameMs; }
    void setSlowFrameMs(double ms) { m_slowFrameMs = ms; }
    /// How long a time-boxed run shows intermediate frames, ms (default 2000).
    void setTimeBoxMs(int ms) { m_timeBoxMs = ms; }
    /// GraphItem reports how long painting the graph took (any thread; the latest paint of the
    /// current run counts).
    void reportPaintCost(double ms) { m_paintMs.store(ms, std::memory_order_relaxed); }
    /// Average cost of the simulation steps of the current run's first steps, ms.
    double tickCostMs() const { return m_tickMs; }

    bool running() const { return m_running; }
    double alpha() const { return m_layout.alpha(); }
    int nodeCount() const { return int(m_nodes.size()); }
    int edgeCount() const { return m_layout.edgeCount(); }
    int totalNodeCount() const { return m_totalNodes; }
    bool truncated() const { return m_truncated; }
    bool focusMissing() const { return m_focusMissing; }
    int pageCount() const { return m_pageCount; }

    const QList<Node> &nodes() const { return m_nodes; }
    const std::vector<std::pair<int, int>> &edges() const { return m_layout.edges(); }
    const QList<QList<int>> &adjacency() const { return m_adjacency; }
    const ForceLayout &layout() const { return m_layout; }
    ForceLayout &layout() { return m_layout; }
    /// Node index of an id, -1 when not in the graph.
    Q_INVOKABLE int indexOf(const QString &id) const { return m_indexOf.value(id, -1); }
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
    /// Run `ticks` simulation steps now (tests, warm-up).
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
    void tickIntervalChanged();
    void refreshDelayChanged();
    void runningChanged();
    void timeBoxedChanged();
    /// The nodes or edges changed (positions too).
    void structureChanged();
    /// Positions changed (a simulation step, a drag).
    void positionsChanged();

private:
    void scheduleRebuild();
    void rebuild();
    void onTick();
    void updateRunning();
    void settingsEdited();
    /// A (re)started simulation: the normal cooling, a fresh decision about time-boxing.
    void beginRun();

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
    int m_tickInterval = 16;

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
    // Time-boxing on slow devices (see the header comment).
    double m_slowFrameMs = 33;
    int m_timeBoxMs = 2000;
    std::atomic<double> m_paintMs{0};
    double m_tickMs = 0;
    int m_runTicks = 0; ///< timer-driven steps of the current run
    bool m_timeBoxed = false;
    QElapsedTimer m_runClock;

    QTimer m_timer;
    QTimer m_refreshTimer;
};

} // namespace brulion::planning
