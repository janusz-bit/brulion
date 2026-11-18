#include "graphmodel.h"

#include "dates/format.h"
#include "index/vaultindex.h"
#include "planningutil.h"
#include "text/text.h"
#include "util/paths.h"

#include <QSet>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <utility>

namespace brulion::planning {

using namespace Qt::StringLiterals;
using namespace std::chrono_literals;

namespace {

constexpr double kNormalAlphaDecay = 0.0228;  // ~300 ticks
constexpr double kReducedAlphaDecay = 0.109;  // ~60 ticks

double nodeRadius(GraphModel::Kind kind, int degree)
{
    const double r = 4.0 + std::sqrt(double(degree)) * 1.7;
    switch (kind) {
    case GraphModel::Kind::Tag: return std::min(18.0, r * 0.9);
    case GraphModel::Kind::Missing: return std::min(10.0, 3.0 + std::sqrt(double(degree)));
    default: return std::min(22.0, r);
    }
}

/// The local graph of a note read from the index around it (GraphModel's local build): the nodes
/// within `depth` links of the focus and the edges among them, exactly those of graph() (without
/// missing pages) that GraphModel's own cut keeps — pages hidden by the filters (templates,
/// journals) are not passed through, as GraphModel drops them before it walks the graph.
class Neighbourhood {
public:
    Neighbourhood(const VaultIndex &index, const QString &focus, const QString &templatesPrefix, bool showTags,
                  bool showJournals, bool showTemplates)
        : m_index(index)
        , m_focus(focus)
        , m_templatesPrefix(templatesPrefix)
        , m_showTags(showTags)
        , m_showJournals(showJournals)
        , m_showTemplates(showTemplates)
    { }

    struct Result {
        GraphData graph;
        QSet<QString> ids;   ///< every node reached (the focus included)
        QSet<QString> inner; ///< those closer than `depth` to the focus
        /// More nodes than the cap: every node's degree in the whole (filtered) graph, which
        /// decides which of them stay.
        QHash<QString, int> degree;
    };

    Result build(int depth, int maxNodes)
    {
        Result r;
        QHash<QString, int> dist{{m_focus, 0}};
        QStringList queue{m_focus};
        for (qsizetype qi = 0; qi < queue.size(); ++qi) {
            const QString u = queue.at(qi);
            const int d = dist.value(u);
            if (d >= depth)
                continue;
            r.inner.insert(u);
            for (const QStringList next = neighbours(u); const QString &v : next) {
                if (!dist.contains(v)) {
                    dist.insert(v, d + 1);
                    queue.append(v);
                }
            }
        }
        r.ids = QSet<QString>(queue.cbegin(), queue.cend());
        // Nodes in graph()'s order: notes by path, then #tags as first met (the first note by
        // path carrying the tag, the tag's place in it).
        QStringList pages;
        QList<std::pair<std::pair<QString, qsizetype>, QString>> tags;
        for (const QString &id : std::as_const(queue)) {
            if (m_index.note(id)) {
                pages.append(id);
            } else {
                const QString tag = id.sliced(1);
                const QList<VaultPath> carriers = tagNotes(tag);
                const ParsedNote *first = carriers.isEmpty() ? nullptr : m_index.note(carriers.first());
                tags.append({{carriers.isEmpty() ? QString() : carriers.first(), first ? first->tags.indexOf(tag) : 0},
                             id});
            }
        }
        std::ranges::sort(pages);
        std::ranges::sort(tags);
        for (const QString &path : std::as_const(pages))
            r.graph.nodes.append(GraphNode{path, m_index.note(path)->title, 0, false, false});
        for (const auto &tag : std::as_const(tags))
            r.graph.nodes.append(GraphNode{tag.second, tag.second, 0, true, false});
        for (const QString &path : std::as_const(pages)) {
            for (const QString &target : outOf(path)) {
                if (r.ids.contains(target))
                    r.graph.edges.append(GraphEdge{path, target});
            }
        }
        if (queue.size() > maxNodes) {
            for (const QString &id : std::as_const(queue))
                r.degree.insert(id, int(neighbours(id).size()));
        }
        return r;
    }

private:
    /// A node GraphModel keeps before it walks the graph: a note (not a hidden template or daily
    /// note) or a #tag node.
    bool visible(const QString &id) const
    {
        const ParsedNote *note = m_index.note(id);
        if (!note)
            return m_showTags && id.startsWith(u'#');
        if (!m_showTemplates && !m_templatesPrefix.isEmpty() && id != m_focus
            && id.startsWith(m_templatesPrefix, Qt::CaseInsensitive))
            return false;
        return m_showJournals || !note->journalDate;
    }

    /// graph()'s edges from note `path`: the notes its links and block references resolve to,
    /// the notes its tags name, its other tags as #tag nodes (with showTags).
    const QStringList &outOf(const QString &path)
    {
        if (const auto it = m_out.constFind(path); it != m_out.cend())
            return *it;
        QStringList targets;
        if (const ParsedNote *note = m_index.note(path)) {
            for (const QList<ResolvedLink> links = m_index.outgoingLinks(path); const ResolvedLink &l : links) {
                if (!l.target.isEmpty() && l.target != path && m_index.note(l.target))
                    targets.append(l.target);
            }
            for (const QString &tag : note->tags) {
                if (const std::optional<VaultPath> page = m_index.resolveTag(tag)) {
                    if (*page != path)
                        targets.append(*page);
                } else if (m_showTags) {
                    targets.append(u'#' + tag);
                }
            }
        }
        return *m_out.insert(path, targets);
    }

    /// The notes carrying `tag` (VaultIndex::tags()), sorted.
    QList<VaultPath> tagNotes(const QString &tag)
    {
        if (!m_tagsRead) {
            for (const QList<TagCount> all = m_index.tags(); const TagCount &t : all)
                m_tagNotes.insert(t.tag, t.paths);
            m_tagsRead = true;
        }
        return m_tagNotes.value(tag);
    }

    /// The visible nodes sharing an edge with `id` (either way), without `id` itself.
    QStringList neighbours(const QString &id)
    {
        QStringList out;
        QSet<QString> seen{id};
        const auto add = [&](const QString &v) {
            if (!seen.contains(v)) {
                seen.insert(v);
                if (visible(v))
                    out.append(v);
            }
        };
        const bool note = m_index.note(id) != nullptr;
        if (note) {
            for (const QString &v : outOf(id))
                add(v);
        }
        // Notes with an edge to `id`: candidates from the backlinks and the tags naming it (or,
        // for a #tag node, the notes carrying it), each confirmed by its own edges.
        QList<VaultPath> sources;
        if (note) {
            for (const QList<Backlink> links = m_index.backlinks(id); const Backlink &b : links)
                sources.append(b.path);
            sources.append(m_index.tagsTo(id));
        } else if (id.startsWith(u'#')) {
            sources = tagNotes(id.sliced(1));
        }
        for (const VaultPath &v : std::as_const(sources)) {
            if (seen.contains(v) || !visible(v))
                continue;
            if (outOf(v).contains(id))
                add(v);
        }
        return out;
    }

    const VaultIndex &m_index;
    QString m_focus;
    QString m_templatesPrefix;
    bool m_showTags;
    bool m_showJournals;
    bool m_showTemplates;
    QHash<QString, QStringList> m_out;
    QHash<QString, QList<VaultPath>> m_tagNotes;
    bool m_tagsRead = false;
};

/// Small deterministic offset for a node placed next to a neighbour.
std::pair<double, double> offsetFor(const QString &id, std::uint32_t seed)
{
    const quint64 h = text::fnv1a64(id.toUtf8()) ^ (quint64(seed) * 0x9E3779B97F4A7C15ull);
    const double a = double(h % 6283) / 1000.0;
    const double r = 18.0 + double((h >> 16) % 14);
    return {std::cos(a) * r, std::sin(a) * r};
}

} // namespace

GraphModel::GraphModel(QObject *parent) : QObject(parent)
{
    connect(&m_sim, &GraphSimulation::frameReady, this, &GraphModel::onFrameReady);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300ms);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GraphModel::rebuildFromIndex);
}

GraphModel::~GraphModel() = default;

QString GraphModel::kindName(Kind kind)
{
    switch (kind) {
    case Kind::Page: return u"page"_s;
    case Kind::Journal: return u"journal"_s;
    case Kind::Tag: return u"tag"_s;
    case Kind::Missing: return u"missing"_s;
    }
    return u"page"_s;
}

VaultIndex *GraphModel::vaultIndex() const
{
    return m_index.data();
}

void GraphModel::setVaultIndex(VaultIndex *index)
{
    if (m_index == index)
        return;
    if (m_indexConnection)
        disconnect(m_indexConnection);
    m_index = index;
    if (index)
        m_indexConnection = connect(index, &VaultIndex::changed, this, &GraphModel::scheduleRebuild);
    Q_EMIT vaultIndexChanged();
    rebuild();
}

QObject *GraphModel::vaultIndexObject() const
{
    return m_index.data();
}

void GraphModel::setVaultIndexObject(QObject *index)
{
    setVaultIndex(qobject_cast<VaultIndex *>(index));
}

void GraphModel::settingsEdited()
{
    Q_EMIT settingsChanged();
    rebuild();
}

void GraphModel::setShowTags(bool on)
{
    if (std::exchange(m_showTags, on) != on)
        settingsEdited();
}

void GraphModel::setShowMissing(bool on)
{
    if (std::exchange(m_showMissing, on) != on)
        settingsEdited();
}

void GraphModel::setShowJournals(bool on)
{
    if (std::exchange(m_showJournals, on) != on)
        settingsEdited();
}

void GraphModel::setShowOrphans(bool on)
{
    if (std::exchange(m_showOrphans, on) != on)
        settingsEdited();
}

void GraphModel::setShowTemplates(bool on)
{
    if (std::exchange(m_showTemplates, on) != on)
        settingsEdited();
}

void GraphModel::setFocusPath(const QString &path)
{
    if (m_focus == path)
        return;
    m_focus = path;
    settingsEdited();
}

void GraphModel::setDepth(int depth)
{
    depth = std::clamp(depth, 1, 3);
    if (std::exchange(m_depth, depth) == depth)
        return;
    if (m_focus.isEmpty())
        Q_EMIT settingsChanged(); // the whole graph has no depth: nothing to rebuild
    else
        settingsEdited();
}

void GraphModel::setSeed(int seed)
{
    const auto s = std::uint32_t(seed);
    if (std::exchange(m_seed, s) == s)
        return;
    Q_EMIT settingsChanged();
    relayout();
}

void GraphModel::setLocale(const QString &locale)
{
    if (m_locale == locale)
        return;
    m_locale = locale;
    settingsEdited();
}

void GraphModel::setMaxNodes(int max)
{
    max = std::max(1, max);
    if (std::exchange(m_maxNodes, max) != max)
        settingsEdited();
}

void GraphModel::setActive(bool active)
{
    if (std::exchange(m_active, active) == active)
        return;
    Q_EMIT activeChanged();
    if (m_simOwned)
        m_sim.command({active ? SimCommand::Resume : SimCommand::Pause});
    else if (active)
        ensureRunning();
    updateRunning();
}

void GraphModel::setShown(bool shown)
{
    if (std::exchange(m_shown, shown) == shown)
        return;
    Q_EMIT shownChanged();
    if (!m_shown) {
        // A rebuild on its way waits for the graph to be shown.
        if (m_refreshTimer.isActive()) {
            m_refreshTimer.stop();
            m_stale = true;
        }
        return;
    }
    if (m_stale)
        rebuildFromIndex();
}

void GraphModel::setReducedMotion(bool on)
{
    if (std::exchange(m_reducedMotion, on) == on)
        return;
    // The run starts over with the other cooling (and the other way of showing it).
    takeBack();
    m_layout.params().alphaDecay = alphaDecay();
    Q_EMIT reducedMotionChanged();
    ensureRunning();
    updateRunning();
}

void GraphModel::setFrameInterval(int msec)
{
    msec = std::clamp(msec, 1, 1000);
    if (std::exchange(m_frameInterval, msec) == msec)
        return;
    // Also for the run in progress (the view sets it after its first build has started one).
    if (m_simOwned)
        m_sim.command({.kind = SimCommand::FrameInterval, .value = double(msec)});
    Q_EMIT frameIntervalChanged();
}

void GraphModel::setRefreshDelay(int msec)
{
    msec = std::max(0, msec);
    if (m_refreshTimer.interval() == msec)
        return;
    m_refreshTimer.setInterval(msec);
    Q_EMIT refreshDelayChanged();
}

QPointF GraphModel::position(int node) const
{
    const auto &x = xs();
    const auto &y = ys();
    if (node < 0 || size_t(node) >= x.size() || size_t(node) >= y.size())
        return {};
    return {x[size_t(node)], y[size_t(node)]};
}

// ---- queries -------------------------------------------------------------------------------

QVariantList GraphModel::find(const QString &query, int limit) const
{
    QVariantList out;
    const QString q = text::foldKey(query.trimmed());
    if (q.isEmpty() || limit <= 0)
        return out;
    struct Hit {
        int node;
        int rank;
    };
    QList<Hit> hits;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const Node &n = m_nodes.at(i);
        QString title = text::foldKey(n.title);
        if (n.kind == Kind::Tag && title.startsWith(u'#') && !q.startsWith(u'#'))
            title.remove(0, 1);
        int rank = -1;
        if (title == q)
            rank = 0;
        else if (title.startsWith(q))
            rank = 1;
        else {
            const qsizetype at = title.indexOf(q);
            if (at > 0 && !text::isWordChar(title.at(at - 1)))
                rank = 2;
            else if (at > 0)
                rank = 3;
            else if (!n.path.isEmpty() && text::foldKey(n.path).contains(q))
                rank = 4;
        }
        if (rank >= 0)
            hits.append({i, rank});
    }
    std::ranges::stable_sort(hits, [this](Hit a, Hit b) {
        if (a.rank != b.rank)
            return a.rank < b.rank;
        return m_nodes.at(a.node).degree > m_nodes.at(b.node).degree;
    });
    for (const Hit &h : std::as_const(hits)) {
        if (out.size() >= limit)
            break;
        const Node &n = m_nodes.at(h.node);
        out.append(QVariantMap{{u"id"_s, n.id}, {u"title"_s, n.title}, {u"path"_s, n.path}, {u"kind"_s, kindName(n.kind)},
                               {u"degree"_s, n.degree}});
    }
    return out;
}

QVariantMap GraphModel::nodeInfo(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return {};
    const Node &n = m_nodes.at(i);
    return {{u"id"_s, n.id},
            {u"title"_s, n.title},
            {u"path"_s, n.path},
            {u"kind"_s, kindName(n.kind)},
            {u"degree"_s, n.degree},
            {u"exists"_s, n.kind == Kind::Page || n.kind == Kind::Journal}};
}

QStringList GraphModel::neighbors(const QString &id) const
{
    QStringList out;
    const int i = indexOf(id);
    if (i < 0)
        return out;
    for (const int j : m_adjacency.at(i))
        out.append(m_nodes.at(j).id);
    return out;
}

QRectF GraphModel::bounds() const
{
    const auto &xv = xs();
    const auto &yv = ys();
    const qsizetype n = std::min({m_nodes.size(), qsizetype(xv.size()), qsizetype(yv.size())});
    constexpr double inf = std::numeric_limits<double>::infinity();
    bool any = false;
    double x0 = inf, y0 = inf, x1 = -inf, y1 = -inf;
    for (qsizetype i = 0; i < n; ++i) {
        const double x = xv[size_t(i)], y = yv[size_t(i)];
        if (!std::isfinite(x) || !std::isfinite(y))
            continue;
        const double r = m_nodes.at(i).radius;
        x0 = std::min(x0, x - r);
        y0 = std::min(y0, y - r);
        x1 = std::max(x1, x + r);
        y1 = std::max(y1, y + r);
        any = true;
    }
    if (!any)
        return {};
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

// ---- simulation ----------------------------------------------------------------------------

double GraphModel::alphaDecay() const
{
    return m_reducedMotion ? kReducedAlphaDecay : kNormalAlphaDecay;
}

void GraphModel::ensureRunning()
{
    if (m_active && !m_simOwned && !m_nodes.isEmpty() && m_layout.running())
        startRun();
}

void GraphModel::startRun()
{
    ++m_generation;
    m_layout.params().alphaDecay = alphaDecay();
    m_x = m_layout.xs();
    m_y = m_layout.ys();
    m_alpha = m_layout.alpha();
    m_simOwned = true;
    m_simRunning = true;
    m_tickMs = 0;
    m_runTicks = 0;
    setFastCooling(false);
    setSettling(m_reducedMotion && m_fresh);
    m_sim.start({.generation = m_generation,
                 .layout = m_layout,
                 .reducedMotion = m_reducedMotion,
                 .alphaDecay = alphaDecay(),
                 .slowTickMs = m_slowTickMs,
                 .frameIntervalMs = m_frameInterval,
                 .minRunMs = m_minRunMs});
}

void GraphModel::takeBack()
{
    if (!m_simOwned)
        return;
    m_sim.command({SimCommand::Cancel});
    ++m_generation; // frames still on their way belong to no run
    m_simOwned = false;
    if (m_simRunning) {
        // Mid-run: continue from the newest frame (a final frame already handed the full state).
        m_layout.setPositions(m_x, m_y);
        m_layout.setAlpha(m_alpha);
    }
    m_simRunning = false;
    setSettling(false);
}

void GraphModel::onFrameReady()
{
    SimFrame frame;
    if (!m_sim.takeFrame(frame))
        return;
    if (!m_simOwned || frame.generation != m_generation || frame.x.size() != size_t(m_nodes.size())
        || frame.y.size() != frame.x.size()) {
        ++m_staleFrames;
        return;
    }
    ++m_framesApplied;
    m_fresh = false; // these positions have moved (and are shown)
    m_x = std::move(frame.x);
    m_y = std::move(frame.y);
    m_alpha = frame.alpha;
    m_tickMs = frame.tickMs;
    m_runTicks = frame.ticks;
    if (m_dragging >= 0 && size_t(m_dragging) < m_x.size()) {
        // The dragged node is where the pointer is, also in frames computed before its last move.
        m_x[size_t(m_dragging)] = m_dragPos.x();
        m_y[size_t(m_dragging)] = m_dragPos.y();
    }
    m_simRunning = frame.running;
    if (frame.state) {
        m_layout = std::move(*frame.state);
        if (m_dragging >= 0) {
            m_layout.fix(m_dragging, m_dragPos.x(), m_dragPos.y());
            m_layout.setAlphaTarget(m_reducedMotion ? 0 : 0.3);
        }
    }
    if (!frame.running)
        setSettling(false);
    setFastCooling(frame.fastCooling);
    Q_EMIT positionsChanged();
    updateRunning();
}

void GraphModel::setFastCooling(bool on)
{
    if (std::exchange(m_fastCooling, on) != on)
        Q_EMIT fastCoolingChanged();
}

void GraphModel::setSettling(bool on)
{
    if (std::exchange(m_settling, on) != on)
        Q_EMIT settlingChanged();
}

void GraphModel::reheat(double alpha)
{
    if (m_simOwned) {
        m_sim.command({.kind = SimCommand::ReheatTo, .value = alpha});
        m_layout.setAlpha(std::max(m_layout.alpha(), alpha));
        m_alpha = std::max(m_alpha, alpha); // what the worker makes of it
        if (m_alpha >= m_layout.params().alphaMin)
            m_simRunning = true;
        setFastCooling(false); // a new run decides again
    } else {
        m_layout.setAlpha(std::max(m_layout.alpha(), alpha));
        m_layout.params().alphaDecay = alphaDecay();
        ensureRunning();
    }
    updateRunning();
}

void GraphModel::relayout()
{
    takeBack();
    const QList<std::pair<int, int>> edges(m_layout.edges().cbegin(), m_layout.edges().cend());
    m_layout.reset(int(m_nodes.size()), edges, m_seed);
    m_layout.params().alphaDecay = alphaDecay();
    m_dragging = -1;
    m_fresh = !m_nodes.isEmpty();
    ensureRunning();
    Q_EMIT positionsChanged();
    updateRunning();
}

void GraphModel::step(int ticks)
{
    takeBack();
    m_fresh = false;
    for (int i = 0; i < ticks; ++i)
        m_layout.tick();
    ensureRunning();
    Q_EMIT positionsChanged();
    updateRunning();
}

void GraphModel::beginDrag(int node)
{
    if (node < 0 || node >= m_nodes.size())
        return;
    m_dragging = node;
    m_dragPos = position(node);
    const double target = m_reducedMotion ? 0 : 0.3;
    m_layout.fix(node, m_dragPos.x(), m_dragPos.y());
    m_layout.setAlphaTarget(target);
    if (m_simOwned) {
        m_sim.command({SimCommand::Fix, node, m_dragPos.x(), m_dragPos.y()});
        m_sim.command({.kind = SimCommand::AlphaTarget, .value = target});
        if (target > 0)
            m_simRunning = true;
    } else {
        ensureRunning();
    }
    updateRunning();
}

void GraphModel::dragTo(int node, QPointF world)
{
    if (node < 0 || node >= m_nodes.size())
        return;
    if (node == m_dragging)
        m_dragPos = world;
    m_layout.fix(node, world.x(), world.y());
    if (m_simOwned) {
        // The node follows the pointer now, not when the worker's next frame comes.
        if (size_t(node) < m_x.size()) {
            m_x[size_t(node)] = world.x();
            m_y[size_t(node)] = world.y();
        }
        m_sim.command({SimCommand::Fix, node, world.x(), world.y()});
    }
    Q_EMIT positionsChanged();
}

void GraphModel::endDrag(int node)
{
    const bool valid = node >= 0 && node < m_nodes.size();
    if (valid)
        m_layout.unfix(node);
    m_dragging = -1;
    m_layout.setAlphaTarget(0);
    if (m_simOwned) {
        if (valid)
            m_sim.command({SimCommand::Unfix, node});
        m_sim.command({.kind = SimCommand::AlphaTarget, .value = 0});
        if (m_reducedMotion) {
            // Settle the neighbours without animating them: one final frame.
            m_sim.command({.kind = SimCommand::ReheatTo, .value = 0.1});
            m_alpha = std::max(m_alpha, 0.1);
            m_simRunning = true;
        } else {
            m_simRunning = m_alpha >= m_layout.params().alphaMin;
        }
    } else {
        if (m_reducedMotion)
            m_layout.setAlpha(std::max(m_layout.alpha(), 0.1));
        m_layout.params().alphaDecay = alphaDecay();
        ensureRunning();
    }
    updateRunning();
}

void GraphModel::updateRunning()
{
    const bool run = m_active && m_simOwned && m_simRunning;
    if (m_running != run) {
        m_running = run;
        Q_EMIT runningChanged();
    }
}

// ---- building ------------------------------------------------------------------------------

void GraphModel::refresh()
{
    m_refreshTimer.stop();
    rebuild();
}

void GraphModel::scheduleRebuild()
{
    if (!m_shown) {
        m_stale = true;
        return;
    }
    if (m_refreshTimer.interval() == 0)
        rebuildFromIndex();
    else
        m_refreshTimer.start();
}

void GraphModel::rebuildFromIndex()
{
    m_refreshTimer.stop();
    m_stale = false;
    if (!unaffectedByIndexChanges())
        rebuild();
}

bool GraphModel::unaffectedByIndexChanges()
{
    if (!m_localBuild || !m_index || m_builtSerial != m_index->serial() || m_builtVersion < 0)
        return false;
    if (m_builtVersion == m_index->version())
        return true;
    // Cut by the node cap: which nodes stay depends on their degrees in the whole graph, which a
    // note outside the neighbourhood changes too (a link to a leaf taken out) — built again.
    if (m_localCapped)
        return false;
    const IndexChanges changes = m_index->changesSince(m_builtVersion);
    if (!changes.complete || changes.names)
        return false;
    for (const VaultPath &path : changes.notes) {
        if (m_localIds.contains(path))
            return false; // one of its notes: its links or title may differ
        const ParsedNote *note = m_index->note(path);
        if (!note)
            continue;
        // A note outside it joins it with an edge to one of its inner nodes.
        for (const QList<ResolvedLink> links = m_index->outgoingLinks(path); const ResolvedLink &l : links) {
            if (m_joinIds.contains(l.target))
                return false;
        }
        for (const QString &tag : note->tags) {
            const std::optional<VaultPath> page = m_index->resolveTag(tag);
            if (m_joinIds.contains(page ? *page : u'#' + tag))
                return false;
        }
    }
    m_builtVersion = m_index->version();
    return true;
}

void GraphModel::rebuild()
{
    m_refreshTimer.stop();
    m_stale = false;
    ++m_rebuilds;
    // Positions to carry over (the newest ones: the worker's frame during a run).
    QHash<QString, QPointF> oldPos;
    oldPos.reserve(m_nodes.size());
    {
        const auto &x = xs();
        const auto &y = ys();
        for (int i = 0; i < m_nodes.size() && size_t(i) < x.size() && size_t(i) < y.size(); ++i)
            oldPos.insert(m_nodes.at(i).id, QPointF(x[size_t(i)], y[size_t(i)]));
    }
    const std::vector<std::pair<int, int>> oldEdges = m_layout.edges();
    const double oldAlpha = alpha();
    // The run on the worker belongs to the old graph.
    if (m_simOwned) {
        m_sim.command({SimCommand::Cancel});
        ++m_generation;
        m_simOwned = false;
        m_simRunning = false;
    }

    // The templates folder ("templates/…", case-insensitive like the journals folder).
    const QString templatesPrefix = [this] {
        const QString folder = m_index ? paths::normalize(m_index->config().templatesFolder) : QString();
        return folder.isEmpty() ? QString() : folder + u'/';
    }();
    GraphData g;
    QHash<QString, int> wholeDegree; ///< a local build's degrees in the whole graph (only when capped)
    m_pageCount = 0;
    m_localBuild = false;
    m_localCapped = false;
    m_localIds.clear();
    m_joinIds.clear();
    m_builtSerial = m_index ? m_index->serial() : 0;
    m_builtVersion = m_index ? m_index->version() : -1;
    if (m_index) {
        // A note's local graph without missing pages: only its neighbourhood (a daily note
        // hidden with the journals shows the whole graph, as graph() cut by step 1 would).
        const ParsedNote *focusNote = m_focus.isEmpty() ? nullptr : m_index->note(m_focus);
        if (focusNote && !m_showMissing && (m_showJournals || !focusNote->journalDate)) {
            Neighbourhood around(*m_index, m_focus, templatesPrefix, m_showTags, m_showJournals, m_showTemplates);
            Neighbourhood::Result local = around.build(m_depth, m_maxNodes);
            g = std::move(local.graph);
            wholeDegree = std::move(local.degree);
            m_localCapped = !wholeDegree.isEmpty();
            m_joinIds = wholeDegree.isEmpty() ? std::move(local.inner) : local.ids;
            m_localIds = std::move(local.ids);
            m_localBuild = true;
        } else {
            g = m_index->graph(m_showTags, m_showMissing);
        }
        m_pageCount = int(m_index->paths().size());
    }

    // 1. Candidate nodes (kind, journals and templates filtered).
    QList<Node> all;
    QHash<QString, int> allIndex;
    all.reserve(g.nodes.size());
    const auto locale = uiLocale(m_locale);
    const int thisYear = QDate::currentDate().year();
    QSet<QString> templateIds; ///< template pages left out
    for (const GraphNode &gn : std::as_const(g.nodes)) {
        if (!m_showTemplates && !templatesPrefix.isEmpty() && !gn.isTag && !gn.isMissing && gn.id != m_focus
            && gn.id.startsWith(templatesPrefix, Qt::CaseInsensitive)) {
            templateIds.insert(gn.id);
            continue;
        }
        Node n;
        n.id = gn.id;
        n.title = gn.title;
        if (gn.isTag) {
            n.kind = Kind::Tag;
        } else if (gn.isMissing) {
            n.kind = Kind::Missing;
        } else {
            n.path = gn.id;
            const ParsedNote *note = m_index ? m_index->note(gn.id) : nullptr;
            n.kind = note && note->journalDate ? Kind::Journal : Kind::Page;
            if (n.kind == Kind::Journal)
                n.title = dates::formatDateShort(*note->journalDate, locale, thisYear);
        }
        if (n.kind == Kind::Journal && !m_showJournals)
            continue;
        allIndex.insert(n.id, int(all.size()));
        all.append(n);
    }
    // 2. Undirected edges among them.
    QList<QList<int>> adj(all.size());
    QList<bool> linkedFromTemplate(all.size(), false);
    {
        QSet<quint64> seen;
        for (const GraphEdge &e : std::as_const(g.edges)) {
            const int a = allIndex.value(e.source, -1);
            const int b = allIndex.value(e.target, -1);
            if (a >= 0 && b < 0 && templateIds.contains(e.target))
                linkedFromTemplate[a] = true;
            if (b >= 0 && a < 0 && templateIds.contains(e.source))
                linkedFromTemplate[b] = true;
            if (a < 0 || b < 0 || a == b)
                continue;
            const quint64 key = (quint64(std::min(a, b)) << 32) | quint64(std::max(a, b));
            if (seen.contains(key))
                continue;
            seen.insert(key);
            adj[a].append(b);
            adj[b].append(a);
        }
    }
    QList<bool> keep(all.size(), true);
    // Tags and missing pages that only hidden templates link to ("{{title}}", the template's
    // #tags) go with them.
    for (int i = 0; i < all.size(); ++i) {
        const bool tagOrMissing = all.at(i).kind == Kind::Tag || all.at(i).kind == Kind::Missing;
        if (linkedFromTemplate[i] && adj.at(i).isEmpty() && tagOrMissing)
            keep[i] = false;
    }
    // 3. Local graph.
    const int focus = m_focus.isEmpty() ? -1 : allIndex.value(m_focus, -1);
    m_focusMissing = !m_focus.isEmpty() && focus < 0;
    if (focus >= 0) {
        QList<int> dist(all.size(), -1);
        QList<int> queue{focus};
        dist[focus] = 0;
        for (qsizetype qi = 0; qi < queue.size(); ++qi) {
            const int u = queue.at(qi);
            if (dist[u] >= m_depth)
                continue;
            for (const int v : adj.at(u)) {
                if (dist[v] < 0) {
                    dist[v] = dist[u] + 1;
                    queue.append(v);
                }
            }
        }
        for (int i = 0; i < all.size(); ++i)
            keep[i] = dist[i] >= 0;
    }
    // 4. Orphans.
    if (!m_showOrphans) {
        for (int i = 0; i < all.size(); ++i) {
            if (keep[i] && i != focus && adj.at(i).isEmpty())
                keep[i] = false;
        }
    }
    m_totalNodes = int(std::count(keep.cbegin(), keep.cend(), true));
    // 5. Cap: the best connected nodes stay (and the focus).
    m_truncated = m_totalNodes > m_maxNodes;
    if (m_truncated) {
        QList<int> order;
        for (int i = 0; i < all.size(); ++i) {
            if (keep[i])
                order.append(i);
        }
        const auto degree = [&](int i) {
            return wholeDegree.isEmpty() ? int(adj.at(i).size())
                                         : wholeDegree.value(all.at(i).id, int(adj.at(i).size()));
        };
        std::ranges::stable_sort(order, [&](int a, int b) {
            if ((a == focus) != (b == focus))
                return a == focus;
            return degree(a) > degree(b);
        });
        for (qsizetype k = m_maxNodes; k < order.size(); ++k)
            keep[order.at(k)] = false;
    }

    // 6. Final nodes in index order, edges among them.
    QList<Node> nodes;
    QHash<QString, int> indexOf;
    QList<int> newIndex(all.size(), -1);
    for (int i = 0; i < all.size(); ++i) {
        if (!keep[i])
            continue;
        newIndex[i] = int(nodes.size());
        indexOf.insert(all.at(i).id, int(nodes.size()));
        nodes.append(all.at(i));
    }
    QList<std::pair<int, int>> edges;
    QList<QList<int>> adjacency(nodes.size());
    for (int i = 0; i < all.size(); ++i) {
        if (newIndex[i] < 0)
            continue;
        for (const int j : adj.at(i)) {
            if (j <= i || newIndex[j] < 0)
                continue;
            edges.append({newIndex[i], newIndex[j]});
            adjacency[newIndex[i]].append(newIndex[j]);
            adjacency[newIndex[j]].append(newIndex[i]);
        }
    }
    for (int i = 0; i < nodes.size(); ++i) {
        nodes[i].degree = int(adjacency.at(i).size());
        nodes[i].radius = nodeRadius(nodes.at(i).kind, nodes.at(i).degree);
    }

    // 7. Positions: carried over, next to a known neighbour, or fresh.
    std::vector<double> startX(size_t(nodes.size()), 0), startY(size_t(nodes.size()), 0);
    std::vector<bool> known(size_t(nodes.size()), false);
    int carried = 0;
    for (int i = 0; i < nodes.size(); ++i) {
        const auto it = oldPos.constFind(nodes.at(i).id);
        if (it != oldPos.cend()) {
            startX[size_t(i)] = it->x();
            startY[size_t(i)] = it->y();
            known[size_t(i)] = true;
            ++carried;
        }
    }
    if (carried > 0) {
        for (int i = 0; i < nodes.size(); ++i) {
            if (known[size_t(i)])
                continue;
            for (const int j : adjacency.at(i)) {
                if (known[size_t(j)]) {
                    const auto [dx, dy] = offsetFor(nodes.at(i).id, m_seed);
                    startX[size_t(i)] = startX[size_t(j)] + dx;
                    startY[size_t(i)] = startY[size_t(j)] + dy;
                    known[size_t(i)] = true;
                    break;
                }
            }
        }
    }

    const bool sameIds = std::ranges::equal(std::as_const(nodes), std::as_const(m_nodes), {}, &Node::id, &Node::id);
    m_nodes = nodes;
    m_indexOf = indexOf;
    m_adjacency = adjacency;
    m_dragging = -1;
    m_layout.reset(int(nodes.size()), edges, m_seed, startX, startY, known);
    m_layout.params().alphaDecay = alphaDecay();
    const bool changed = !sameIds || m_layout.edges() != oldEdges;
    if (carried > 0)
        m_layout.setAlpha(changed ? std::max(oldAlpha, carried == nodes.size() ? 0.15 : 0.35) : oldAlpha);
    // Fresh start positions, or carried ones that never moved yet (the spiral of a build whose
    // run has not published a frame: the bindings of a view that opens set several settings one
    // by one, an index change or a filter while a reduced-motion layout settles).
    m_fresh = !nodes.isEmpty() && (carried == 0 || m_fresh);
    setSettling(false);
    ensureRunning();
    Q_EMIT structureChanged();
    Q_EMIT positionsChanged();
    updateRunning();
}

} // namespace brulion::planning
