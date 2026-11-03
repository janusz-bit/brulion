// A layered (Sugiyama-style) layout of a directed graph, top to bottom: cycles broken by
// reversing DFS back edges, ranks by longest path (sources pulled down to their successors),
// links spanning ranks split by bend points ("dummy" nodes; a label is the middle one, with its
// size), crossings reduced by barycentre sweeps (a bounded number, the best order kept), and
// coordinates from weighted isotonic regression (pool adjacent violators): each node as near the
// mean of its neighbours as the spacing allows — balanced, centred, long links straight.
// Ranks are doubled (every link spans two at least): a label sits in the rank between its ends.
// The caller turns the result for other directions (flowchart_layout.cpp).
#pragma once

#include <QList>
#include <QPointF>
#include <QSizeF>

#include <optional>

namespace brulion::diagram::detail {

struct LayerNode {
    qreal width = 0;      ///< across the ranks
    qreal height = 0;     ///< along the ranks
    qreal extraAfter = 0; ///< room kept right of it (self-loops)
};

struct LayerEdge {
    int from = 0, to = 0; ///< different nodes
    int minLength = 1;    ///< ranks (before doubling)
    QSizeF label;         ///< empty: no label
};

struct LayerOptions {
    qreal nodeSep = 40;
    qreal edgeSep = 16;
    qreal rankSep = 48;
    int maxSweeps = 16;
    int maxNodes = 30000; ///< nodes plus bends; more: nullopt
};

struct LayerResult {
    QSizeF size;
    QList<QPointF> centers;               ///< per node
    QList<QList<QPointF>> bends;          ///< per edge: from `from` to `to`, bend points between
    QList<std::optional<QPointF>> labels; ///< per edge: the label's centre
    QList<int> ranks;                     ///< per node
};

std::optional<LayerResult> layered(const QList<LayerNode> &nodes, const QList<LayerEdge> &edges,
                                   const LayerOptions &options = {});

} // namespace brulion::diagram::detail
