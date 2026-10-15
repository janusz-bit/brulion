// ForceLayout — a deterministic force-directed graph layout (the d3-force model): many-body
// repulsion approximated with a Barnes–Hut quadtree (O(n log n) per tick), link springs with
// degree-based strength and bias, a weak pull towards the origin (keeps components and orphans
// together), velocity decay and a cooling `alpha`. GUI-free and seedable: the same graph, seed
// and number of ticks give bit-identical positions on every platform (own LCG, no
// std::*_distribution, fixed iteration order).
//
//   ForceLayout layout;
//   layout.reset(nodeCount, edges, seed);       // phyllotaxis start + seeded jitter, alpha = 1
//   while (layout.running()) layout.tick();
//   layout.x(i), layout.y(i)
//
// Dragging: fix(i, x, y) pins a node (velocity 0), unfix(i) releases it; setAlphaTarget(0.3)
// while dragging keeps the simulation warm (like d3's drag example).
#pragma once

#include <QList>
#include <QRectF>

#include <cstdint>
#include <utility>
#include <vector>

namespace brulion::planning {

class ForceLayout {
public:
    struct Params {
        double charge = -110;         ///< many-body strength per node (negative = repulsion)
        double theta = 0.9;           ///< Barnes–Hut accuracy (smaller = exact, slower)
        double distanceMin = 1;       ///< below this, forces are clamped (no singularities)
        double distanceMax = 900;     ///< repulsion is ignored beyond this distance
        double linkDistance = 42;     ///< rest length of the springs
        double linkStrengthScale = 1; ///< multiplies 1 / min(degree(source), degree(target))
        double gravity = 0.03;        ///< pull towards (0, 0), per tick and alpha
        double orphanGravity = 4;     ///< gravity factor for nodes without links (they stay near)
        double velocityDecay = 0.4;   ///< fraction of velocity lost per tick
        double alphaMin = 0.001;      ///< the simulation stops below this
        double alphaDecay = 0.0228;   ///< 1 - 0.001^(1/300): about 300 ticks from 1 to alphaMin
    };

    ForceLayout() = default;

    Params &params() { return m_params; }
    const Params &params() const { return m_params; }

    /// New graph: `nodeCount` nodes, undirected `edges` (pairs of node indices; self-loops and
    /// out-of-range pairs are ignored). Positions start on a phyllotaxis spiral rotated and
    /// jittered by `seed`; velocities 0; alpha 1.
    void reset(int nodeCount, const QList<std::pair<int, int>> &edges, std::uint32_t seed = 1);
    /// Same nodes, keep positions where given: `positions[i]` (when `known[i]`) or the start
    /// position of node i. Used after the graph changed: carried-over nodes keep their place.
    void reset(int nodeCount, const QList<std::pair<int, int>> &edges, std::uint32_t seed,
               const std::vector<double> &xs, const std::vector<double> &ys, const std::vector<bool> &known);

    int nodeCount() const { return int(m_x.size()); }
    int edgeCount() const { return int(m_edges.size()); }
    const std::vector<std::pair<int, int>> &edges() const { return m_edges; }

    double x(int i) const { return m_x[size_t(i)]; }
    double y(int i) const { return m_y[size_t(i)]; }
    double vx(int i) const { return m_vx[size_t(i)]; }
    double vy(int i) const { return m_vy[size_t(i)]; }
    void setPosition(int i, double x, double y);
    void fix(int i, double x, double y);
    void unfix(int i);
    bool isFixed(int i) const { return m_fixed[size_t(i)] != 0; }

    double alpha() const { return m_alpha; }
    void setAlpha(double alpha) { m_alpha = alpha; }
    double alphaTarget() const { return m_alphaTarget; }
    void setAlphaTarget(double target) { m_alphaTarget = target; }
    /// alpha above alphaMin, or a positive alpha target.
    bool running() const { return m_alpha >= m_params.alphaMin || m_alphaTarget > 0; }

    /// One simulation step (cools alpha first, like d3).
    void tick();
    /// `count` steps.
    void tick(int count);

    /// Bounding box of all nodes (empty rect at the origin without nodes).
    QRectF bounds() const;
    /// The start position of node `i` for the current seed (phyllotaxis + jitter).
    std::pair<double, double> startPosition(int i) const;

    /// Deterministic pseudo-random number in [0, 1) (d3's LCG).
    double random();

    /// Many-body force computed exactly (O(n²)) instead of the quadtree — tests compare both.
    void setExactRepulsion(bool exact) { m_exact = exact; }

private:
    struct Cell {
        double x0 = 0, y0 = 0, size = 0; ///< square bounds
        double cx = 0, cy = 0;           ///< centre of the charges
        double charge = 0;               ///< sum of the charges
        int child[4] = {-1, -1, -1, -1};
        int point = -1; ///< leaf: first point (others via m_nextPoint), -1 when empty / internal
        bool leaf = true;
    };

    void applyLinks();
    void applyCharge();
    void applyChargeExact();
    void applyGravity();
    void buildTree();
    void insert(int cell, int point, int depth);
    void accumulate(int cell);
    double jiggle();

    Params m_params;
    std::vector<double> m_x, m_y, m_vx, m_vy, m_fx, m_fy;
    std::vector<char> m_fixed;
    std::vector<std::pair<int, int>> m_edges;
    std::vector<double> m_linkStrength, m_linkBias;
    std::vector<double> m_gravityScale;
    std::vector<Cell> m_cells;
    std::vector<int> m_nextPoint;
    std::vector<int> m_stack;
    double m_alpha = 1;
    double m_alphaTarget = 0;
    std::uint32_t m_seed = 1;
    std::uint32_t m_random = 1;
    double m_rotation = 0;
    bool m_exact = false;
};

} // namespace brulion::planning
