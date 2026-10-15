#include "graphmodel.h"

#include "dates/format.h"
#include "index/vaultindex.h"
#include "planningutil.h"
#include "text/text.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace brulion::planning {

using namespace Qt::StringLiterals;

namespace {

constexpr double kNormalAlphaDecay = 0.0228;  // ~300 ticks
constexpr double kReducedAlphaDecay = 0.109;  // ~60 ticks
constexpr qint64 kReducedBurstMs = 30;
constexpr qint64 kReducedInitialBudgetMs = 150;

double nodeRadius(GraphModel::Kind kind, int degree)
{
    const double r = 4.0 + std::sqrt(double(degree)) * 1.7;
    switch (kind) {
    case GraphModel::Kind::Tag: return std::min(18.0, r * 0.9);
    case GraphModel::Kind::Missing: return std::min(10.0, 3.0 + std::sqrt(double(degree)));
    default: return std::min(22.0, r);
    }
}

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
    m_timer.setInterval(m_tickInterval);
    connect(&m_timer, &QTimer::timeout, this, &GraphModel::onTick);
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(300);
    connect(&m_refreshTimer, &QTimer::timeout, this, &GraphModel::rebuild);
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
    if (m_showTags == on)
        return;
    m_showTags = on;
    settingsEdited();
}

void GraphModel::setShowMissing(bool on)
{
    if (m_showMissing == on)
        return;
    m_showMissing = on;
    settingsEdited();
}

void GraphModel::setShowJournals(bool on)
{
    if (m_showJournals == on)
        return;
    m_showJournals = on;
    settingsEdited();
}

void GraphModel::setShowOrphans(bool on)
{
    if (m_showOrphans == on)
        return;
    m_showOrphans = on;
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
    if (m_depth == depth)
        return;
    m_depth = depth;
    if (m_focus.isEmpty()) {
        Q_EMIT settingsChanged();
        return;
    }
    settingsEdited();
}

void GraphModel::setSeed(int seed)
{
    const auto s = std::uint32_t(seed);
    if (m_seed == s)
        return;
    m_seed = s;
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
    if (m_maxNodes == max)
        return;
    m_maxNodes = max;
    settingsEdited();
}

void GraphModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    Q_EMIT activeChanged();
    updateRunning();
}

void GraphModel::setReducedMotion(bool on)
{
    if (m_reducedMotion == on)
        return;
    m_reducedMotion = on;
    m_layout.params().alphaDecay = on ? kReducedAlphaDecay : kNormalAlphaDecay;
    Q_EMIT reducedMotionChanged();
    updateRunning();
}

void GraphModel::setTickInterval(int msec)
{
    msec = std::clamp(msec, 1, 1000);
    if (m_tickInterval == msec)
        return;
    m_tickInterval = msec;
    m_timer.setInterval(msec);
    Q_EMIT tickIntervalChanged();
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
    if (node < 0 || node >= m_layout.nodeCount())
        return {};
    return {m_layout.x(node), m_layout.y(node)};
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
    std::stable_sort(hits.begin(), hits.end(), [this](const Hit &a, const Hit &b) {
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
    if (m_nodes.isEmpty())
        return {};
    double x0 = m_layout.x(0), y0 = m_layout.y(0), x1 = x0, y1 = y0;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const double r = m_nodes.at(i).radius;
        x0 = std::min(x0, m_layout.x(i) - r);
        y0 = std::min(y0, m_layout.y(i) - r);
        x1 = std::max(x1, m_layout.x(i) + r);
        y1 = std::max(y1, m_layout.y(i) + r);
    }
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

// ---- simulation ----------------------------------------------------------------------------

void GraphModel::reheat(double alpha)
{
    m_layout.setAlpha(std::max(m_layout.alpha(), alpha));
    updateRunning();
}

void GraphModel::relayout()
{
    QList<std::pair<int, int>> edges;
    edges.reserve(qsizetype(m_layout.edges().size()));
    for (const auto &e : m_layout.edges())
        edges.append(e);
    m_layout.reset(int(m_nodes.size()), edges, m_seed);
    m_dragging = -1;
    if (m_reducedMotion) {
        QElapsedTimer t;
        t.start();
        while (m_layout.running() && t.elapsed() < kReducedInitialBudgetMs)
            m_layout.tick();
    }
    Q_EMIT positionsChanged();
    updateRunning();
}

void GraphModel::step(int ticks)
{
    for (int i = 0; i < ticks; ++i)
        m_layout.tick();
    Q_EMIT positionsChanged();
    updateRunning();
}

void GraphModel::beginDrag(int node)
{
    if (node < 0 || node >= m_nodes.size())
        return;
    m_dragging = node;
    m_layout.fix(node, m_layout.x(node), m_layout.y(node));
    m_layout.setAlphaTarget(m_reducedMotion ? 0 : 0.3);
    updateRunning();
}

void GraphModel::dragTo(int node, QPointF world)
{
    if (node < 0 || node >= m_nodes.size())
        return;
    m_layout.fix(node, world.x(), world.y());
    if (m_reducedMotion || !m_active || !m_timer.isActive())
        Q_EMIT positionsChanged();
}

void GraphModel::endDrag(int node)
{
    if (node >= 0 && node < m_nodes.size())
        m_layout.unfix(node);
    m_dragging = -1;
    m_layout.setAlphaTarget(0);
    if (m_reducedMotion) {
        // Settle the neighbours at once instead of animating them.
        m_layout.setAlpha(std::max(m_layout.alpha(), 0.1));
        QElapsedTimer t;
        t.start();
        while (m_layout.running() && t.elapsed() < kReducedInitialBudgetMs)
            m_layout.tick();
        Q_EMIT positionsChanged();
    }
    updateRunning();
}

void GraphModel::onTick()
{
    if (!m_layout.running()) {
        updateRunning();
        return;
    }
    if (m_reducedMotion) {
        QElapsedTimer t;
        t.start();
        while (m_layout.running() && t.elapsed() < kReducedBurstMs)
            m_layout.tick();
        if (!m_layout.running())
            Q_EMIT positionsChanged();
    } else {
        m_layout.tick();
        Q_EMIT positionsChanged();
    }
    updateRunning();
}

void GraphModel::updateRunning()
{
    const bool run = m_active && !m_nodes.isEmpty() && m_layout.running();
    if (run && !m_timer.isActive())
        m_timer.start();
    else if (!run && m_timer.isActive())
        m_timer.stop();
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
    if (m_refreshTimer.interval() == 0)
        rebuild();
    else
        m_refreshTimer.start();
}

void GraphModel::rebuild()
{
    m_refreshTimer.stop();
    // Positions to carry over.
    QHash<QString, QPointF> oldPos;
    oldPos.reserve(m_nodes.size());
    for (int i = 0; i < m_nodes.size() && i < m_layout.nodeCount(); ++i)
        oldPos.insert(m_nodes.at(i).id, QPointF(m_layout.x(i), m_layout.y(i)));
    QStringList oldIds;
    for (const Node &n : std::as_const(m_nodes))
        oldIds.append(n.id);
    const std::vector<std::pair<int, int>> oldEdges = m_layout.edges();
    const double oldAlpha = m_layout.alpha();

    GraphData g;
    m_pageCount = 0;
    if (m_index) {
        g = m_index->graph(m_showTags, m_showMissing);
        m_pageCount = int(m_index->paths().size());
    }

    // 1. Candidate nodes (kind, journals filtered).
    QList<Node> all;
    QHash<QString, int> allIndex;
    all.reserve(g.nodes.size());
    for (const GraphNode &gn : std::as_const(g.nodes)) {
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
                n.title = dates::formatDateShort(*note->journalDate, uiLocale(m_locale), QDate::currentDate().year());
        }
        if (n.kind == Kind::Journal && !m_showJournals)
            continue;
        allIndex.insert(n.id, int(all.size()));
        all.append(n);
    }
    // 2. Undirected edges among them.
    QList<QList<int>> adj(all.size());
    {
        QSet<quint64> seen;
        for (const GraphEdge &e : std::as_const(g.edges)) {
            const int a = allIndex.value(e.source, -1);
            const int b = allIndex.value(e.target, -1);
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
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            if ((a == focus) != (b == focus))
                return a == focus;
            return adj.at(a).size() > adj.at(b).size();
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
    std::vector<double> xs(size_t(nodes.size()), 0), ys(size_t(nodes.size()), 0);
    std::vector<bool> known(size_t(nodes.size()), false);
    int carried = 0;
    for (int i = 0; i < nodes.size(); ++i) {
        const auto it = oldPos.constFind(nodes.at(i).id);
        if (it != oldPos.cend()) {
            xs[size_t(i)] = it->x();
            ys[size_t(i)] = it->y();
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
                    xs[size_t(i)] = xs[size_t(j)] + dx;
                    ys[size_t(i)] = ys[size_t(j)] + dy;
                    known[size_t(i)] = true;
                    break;
                }
            }
        }
    }
    QStringList newIds;
    for (const Node &n : std::as_const(nodes))
        newIds.append(n.id);

    m_nodes = nodes;
    m_indexOf = indexOf;
    m_adjacency = adjacency;
    m_dragging = -1;
    m_layout.params().alphaDecay = m_reducedMotion ? kReducedAlphaDecay : kNormalAlphaDecay;
    m_layout.reset(int(nodes.size()), edges, m_seed, xs, ys, known);
    const bool changed = newIds != oldIds || m_layout.edges() != oldEdges;
    if (carried > 0)
        m_layout.setAlpha(changed ? std::max(oldAlpha, carried == nodes.size() ? 0.15 : 0.35) : oldAlpha);
    if (m_reducedMotion) {
        QElapsedTimer t;
        t.start();
        while (m_layout.running() && t.elapsed() < kReducedInitialBudgetMs)
            m_layout.tick();
    }
    Q_EMIT structureChanged();
    Q_EMIT positionsChanged();
    updateRunning();
}

} // namespace brulion::planning
