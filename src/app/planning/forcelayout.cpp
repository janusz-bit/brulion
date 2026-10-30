#include "forcelayout.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace brulion::planning {

namespace {

constexpr int kMaxDepth = 40;

/// Stateless hash of (seed, i) to [-1, 1) — jitter of the start positions does not depend on the
/// order in which nodes are added.
double hashUnit(std::uint32_t seed, std::uint32_t i, std::uint32_t salt)
{
    std::uint64_t z = (std::uint64_t(seed) << 32) ^ (std::uint64_t(i) * 0x9E3779B97F4A7C15ull) ^ (std::uint64_t(salt) << 7);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) / double(1ull << 53) * 2.0 - 1.0;
}

} // namespace

double ForceLayout::random()
{
    // d3-force's LCG (a = 1664525, c = 1013904223, m = 2^32): exact in 32-bit arithmetic.
    m_random = 1664525u * m_random + 1013904223u;
    return double(m_random) / 4294967296.0;
}

double ForceLayout::jiggle()
{
    return (random() - 0.5) * 1e-6;
}

std::pair<double, double> ForceLayout::startPosition(int i) const
{
    // d3's phyllotaxis arrangement (initialRadius 10, golden angle), rotated by the seed, with a
    // small seeded jitter.
    const double golden = std::numbers::pi * (3.0 - std::sqrt(5.0));
    const double r = 10.0 * std::sqrt(0.5 + i);
    const double a = i * golden + m_rotation;
    const double jx = hashUnit(m_seed, std::uint32_t(i), 1) * 2.0;
    const double jy = hashUnit(m_seed, std::uint32_t(i), 2) * 2.0;
    return {r * std::cos(a) + jx, r * std::sin(a) + jy};
}

void ForceLayout::reset(int nodeCount, const QList<std::pair<int, int>> &edges, std::uint32_t seed)
{
    reset(nodeCount, edges, seed, {}, {}, {});
}

void ForceLayout::reset(int nodeCount, const QList<std::pair<int, int>> &edges, std::uint32_t seed,
                        const std::vector<double> &xs, const std::vector<double> &ys, const std::vector<bool> &known)
{
    const size_t n = size_t(std::max(0, nodeCount));
    m_seed = seed == 0 ? 1 : seed;
    m_random = m_seed;
    m_rotation = (hashUnit(m_seed, 0x5eed, 3) + 1.0) * std::numbers::pi;
    m_x.assign(n, 0);
    m_y.assign(n, 0);
    m_vx.assign(n, 0);
    m_vy.assign(n, 0);
    m_fx.assign(n, 0);
    m_fy.assign(n, 0);
    m_fixed.assign(n, 0);
    for (size_t i = 0; i < n; ++i) {
        if (i < known.size() && known[i] && i < xs.size() && i < ys.size()) {
            m_x[i] = xs[i];
            m_y[i] = ys[i];
        } else {
            const auto [x, y] = startPosition(int(i));
            m_x[i] = x;
            m_y[i] = y;
        }
    }

    // Undirected, deduplicated, sorted (deterministic order whatever the input order).
    m_edges.clear();
    m_edges.reserve(size_t(edges.size()));
    for (const auto &[a, b] : edges) {
        if (a == b || a < 0 || b < 0 || size_t(a) >= n || size_t(b) >= n)
            continue;
        m_edges.emplace_back(std::min(a, b), std::max(a, b));
    }
    std::sort(m_edges.begin(), m_edges.end());
    m_edges.erase(std::unique(m_edges.begin(), m_edges.end()), m_edges.end());

    std::vector<int> count(n, 0);
    for (const auto &[a, b] : m_edges) {
        ++count[size_t(a)];
        ++count[size_t(b)];
    }
    m_linkStrength.resize(m_edges.size());
    m_linkBias.resize(m_edges.size());
    for (size_t k = 0; k < m_edges.size(); ++k) {
        const int cs = count[size_t(m_edges[k].first)];
        const int ct = count[size_t(m_edges[k].second)];
        m_linkStrength[k] = 1.0 / std::max(1, std::min(cs, ct));
        m_linkBias[k] = double(cs) / double(cs + ct);
    }
    m_gravityScale.assign(n, 1.0);
    for (size_t i = 0; i < n; ++i) {
        if (count[i] == 0)
            m_gravityScale[i] = m_params.orphanGravity;
    }
    m_nextPoint.assign(n, -1);
    m_alpha = 1;
    m_alphaTarget = 0;
}

void ForceLayout::setPosition(int i, double x, double y)
{
    if (i < 0 || i >= nodeCount())
        return;
    m_x[size_t(i)] = x;
    m_y[size_t(i)] = y;
    m_vx[size_t(i)] = 0;
    m_vy[size_t(i)] = 0;
    if (m_fixed[size_t(i)]) {
        m_fx[size_t(i)] = x;
        m_fy[size_t(i)] = y;
    }
}

void ForceLayout::setPositions(const std::vector<double> &xs, const std::vector<double> &ys)
{
    if (xs.size() != m_x.size() || ys.size() != m_y.size())
        return;
    m_x = xs;
    m_y = ys;
    std::fill(m_vx.begin(), m_vx.end(), 0.0);
    std::fill(m_vy.begin(), m_vy.end(), 0.0);
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (m_fixed[i]) {
            m_fx[i] = m_x[i];
            m_fy[i] = m_y[i];
        }
    }
}

int ForceLayout::ticksToCool(double alpha, double alphaMin, double alphaDecay)
{
    if (!(alpha >= alphaMin))
        return 0;
    if (!(alphaDecay > 0) || alphaDecay >= 1 || !(alphaMin > 0))
        return alphaDecay >= 1 ? 1 : -1;
    // tick() multiplies alpha by (1 - decay) before each step; it runs while alpha >= alphaMin.
    // Counted rather than computed with logarithms: the same rounding as tick() itself (capped
    // for absurdly slow cooling).
    int ticks = 0;
    while (alpha >= alphaMin && ticks < 1000000) {
        alpha += (0 - alpha) * alphaDecay;
        ++ticks;
    }
    return ticks;
}

int ForceLayout::expectedTicks() const
{
    if (m_alphaTarget > 0)
        return -1;
    return ticksToCool(m_alpha, m_params.alphaMin, m_params.alphaDecay);
}

void ForceLayout::fix(int i, double x, double y)
{
    if (i < 0 || i >= nodeCount())
        return;
    m_fixed[size_t(i)] = 1;
    m_fx[size_t(i)] = x;
    m_fy[size_t(i)] = y;
    m_x[size_t(i)] = x;
    m_y[size_t(i)] = y;
    m_vx[size_t(i)] = 0;
    m_vy[size_t(i)] = 0;
}

void ForceLayout::unfix(int i)
{
    if (i >= 0 && i < nodeCount())
        m_fixed[size_t(i)] = 0;
}

void ForceLayout::tick(int count)
{
    for (int k = 0; k < count; ++k)
        tick();
}

void ForceLayout::tick()
{
    m_alpha += (m_alphaTarget - m_alpha) * m_params.alphaDecay;
    if (m_x.empty())
        return;
    applyLinks();
    if (m_exact)
        applyChargeExact();
    else
        applyCharge();
    applyGravity();
    const double keep = 1.0 - m_params.velocityDecay;
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (m_fixed[i]) {
            m_x[i] = m_fx[i];
            m_y[i] = m_fy[i];
            m_vx[i] = 0;
            m_vy[i] = 0;
            continue;
        }
        m_vx[i] *= keep;
        m_vy[i] *= keep;
        m_x[i] += m_vx[i];
        m_y[i] += m_vy[i];
    }
}

void ForceLayout::applyLinks()
{
    const double alpha = m_alpha;
    const double distance = m_params.linkDistance;
    const double scale = m_params.linkStrengthScale;
    for (size_t k = 0; k < m_edges.size(); ++k) {
        const size_t s = size_t(m_edges[k].first);
        const size_t t = size_t(m_edges[k].second);
        double x = m_x[t] + m_vx[t] - m_x[s] - m_vx[s];
        double y = m_y[t] + m_vy[t] - m_y[s] - m_vy[s];
        if (x == 0)
            x = jiggle();
        if (y == 0)
            y = jiggle();
        double l = std::sqrt(x * x + y * y);
        l = (l - distance) / l * alpha * m_linkStrength[k] * scale;
        x *= l;
        y *= l;
        const double b = m_linkBias[k];
        m_vx[t] -= x * b;
        m_vy[t] -= y * b;
        m_vx[s] += x * (1 - b);
        m_vy[s] += y * (1 - b);
    }
}

void ForceLayout::applyGravity()
{
    const double g = m_params.gravity * m_alpha;
    if (g == 0)
        return;
    for (size_t i = 0; i < m_x.size(); ++i) {
        const double gi = g * m_gravityScale[i];
        m_vx[i] -= m_x[i] * gi;
        m_vy[i] -= m_y[i] * gi;
    }
}

void ForceLayout::buildTree()
{
    m_cells.clear();
    double x0 = m_x[0], y0 = m_y[0], x1 = m_x[0], y1 = m_y[0];
    for (size_t i = 1; i < m_x.size(); ++i) {
        x0 = std::min(x0, m_x[i]);
        y0 = std::min(y0, m_y[i]);
        x1 = std::max(x1, m_x[i]);
        y1 = std::max(y1, m_y[i]);
    }
    const double size = std::max({x1 - x0, y1 - y0, 1.0}) * 1.0001 + 1e-9;
    Cell root;
    root.x0 = x0;
    root.y0 = y0;
    root.size = size;
    m_cells.push_back(root);
    for (size_t i = 0; i < m_x.size(); ++i) {
        if (std::isfinite(m_x[i]) && std::isfinite(m_y[i]))
            insert(0, int(i), 0);
    }
    accumulate(0);
}

void ForceLayout::insert(int cell, int point, int depth)
{
    const size_t p = size_t(point);
    if (m_cells[size_t(cell)].leaf) {
        const int q = m_cells[size_t(cell)].point;
        if (q < 0) {
            m_cells[size_t(cell)].point = point;
            m_nextPoint[p] = -1;
            return;
        }
        if (depth >= kMaxDepth || (m_x[size_t(q)] == m_x[p] && m_y[size_t(q)] == m_y[p])) {
            m_nextPoint[p] = q;
            m_cells[size_t(cell)].point = point;
            return;
        }
        // Split: the existing (coincident) points go one level down.
        m_cells[size_t(cell)].leaf = false;
        m_cells[size_t(cell)].point = -1;
        const Cell parent = m_cells[size_t(cell)];
        const double half = parent.size / 2;
        const int quadrant = (m_x[size_t(q)] >= parent.x0 + half ? 1 : 0) | (m_y[size_t(q)] >= parent.y0 + half ? 2 : 0);
        Cell child;
        child.x0 = parent.x0 + ((quadrant & 1) ? half : 0);
        child.y0 = parent.y0 + ((quadrant & 2) ? half : 0);
        child.size = half;
        child.point = q; // keeps its m_nextPoint chain
        m_cells.push_back(child);
        m_cells[size_t(cell)].child[quadrant] = int(m_cells.size()) - 1;
    }
    const Cell parent = m_cells[size_t(cell)];
    const double half = parent.size / 2;
    const int quadrant = (m_x[p] >= parent.x0 + half ? 1 : 0) | (m_y[p] >= parent.y0 + half ? 2 : 0);
    int child = parent.child[quadrant];
    if (child < 0) {
        Cell c;
        c.x0 = parent.x0 + ((quadrant & 1) ? half : 0);
        c.y0 = parent.y0 + ((quadrant & 2) ? half : 0);
        c.size = half;
        m_cells.push_back(c);
        child = int(m_cells.size()) - 1;
        m_cells[size_t(cell)].child[quadrant] = child;
    }
    insert(child, point, depth + 1);
}

void ForceLayout::accumulate(int cell)
{
    const double charge = m_params.charge;
    Cell &c0 = m_cells[size_t(cell)];
    if (c0.leaf) {
        double sx = 0, sy = 0;
        int n = 0;
        for (int p = c0.point; p >= 0; p = m_nextPoint[size_t(p)]) {
            sx += m_x[size_t(p)];
            sy += m_y[size_t(p)];
            ++n;
        }
        c0.charge = charge * n;
        c0.cx = n > 0 ? sx / n : 0;
        c0.cy = n > 0 ? sy / n : 0;
        return;
    }
    double sx = 0, sy = 0, weight = 0, total = 0;
    for (int q = 0; q < 4; ++q) {
        const int child = m_cells[size_t(cell)].child[q];
        if (child < 0)
            continue;
        accumulate(child);
        const Cell &c = m_cells[size_t(child)];
        const double w = std::abs(c.charge);
        sx += c.cx * w;
        sy += c.cy * w;
        weight += w;
        total += c.charge;
    }
    Cell &c = m_cells[size_t(cell)];
    c.charge = total;
    c.cx = weight > 0 ? sx / weight : 0;
    c.cy = weight > 0 ? sy / weight : 0;
}

void ForceLayout::applyCharge()
{
    buildTree();
    const double alpha = m_alpha;
    const double theta2 = m_params.theta * m_params.theta;
    const double dMin2 = m_params.distanceMin * m_params.distanceMin;
    const double dMax2 = m_params.distanceMax * m_params.distanceMax;
    const double charge = m_params.charge;
    for (size_t i = 0; i < m_x.size(); ++i) {
        const double xi = m_x[i];
        const double yi = m_y[i];
        double fx = 0, fy = 0;
        m_stack.clear();
        m_stack.push_back(0);
        while (!m_stack.empty()) {
            const Cell &c = m_cells[size_t(m_stack.back())];
            m_stack.pop_back();
            if (c.charge == 0)
                continue;
            double dx = c.cx - xi;
            double dy = c.cy - yi;
            double l = dx * dx + dy * dy;
            const double w = c.size;
            if (w * w / theta2 < l) {
                // Far enough: the whole cell acts as one charge.
                if (l < dMax2) {
                    if (dx == 0)
                        dx = jiggle(), l += dx * dx;
                    if (dy == 0)
                        dy = jiggle(), l += dy * dy;
                    if (l < dMin2)
                        l = std::sqrt(dMin2 * l);
                    fx += dx * c.charge * alpha / l;
                    fy += dy * c.charge * alpha / l;
                }
                continue;
            }
            if (!c.leaf) {
                for (int q = 3; q >= 0; --q) {
                    if (c.child[q] >= 0)
                        m_stack.push_back(c.child[q]);
                }
                continue;
            }
            if (l >= dMax2)
                continue;
            // A leaf near the node: its points one by one (they share a position).
            const bool self = c.point == int(i) && m_nextPoint[size_t(c.point)] < 0;
            if (self)
                continue;
            if (dx == 0)
                dx = jiggle(), l += dx * dx;
            if (dy == 0)
                dy = jiggle(), l += dy * dy;
            if (l < dMin2)
                l = std::sqrt(dMin2 * l);
            for (int p = c.point; p >= 0; p = m_nextPoint[size_t(p)]) {
                if (p == int(i))
                    continue;
                const double f = charge * alpha / l;
                fx += dx * f;
                fy += dy * f;
            }
        }
        m_vx[i] += fx;
        m_vy[i] += fy;
    }
}

void ForceLayout::applyChargeExact()
{
    const double alpha = m_alpha;
    const double dMin2 = m_params.distanceMin * m_params.distanceMin;
    const double dMax2 = m_params.distanceMax * m_params.distanceMax;
    const double charge = m_params.charge;
    for (size_t i = 0; i < m_x.size(); ++i) {
        double fx = 0, fy = 0;
        for (size_t j = 0; j < m_x.size(); ++j) {
            if (i == j)
                continue;
            double dx = m_x[j] - m_x[i];
            double dy = m_y[j] - m_y[i];
            double l = dx * dx + dy * dy;
            if (l >= dMax2)
                continue;
            if (dx == 0)
                dx = jiggle(), l += dx * dx;
            if (dy == 0)
                dy = jiggle(), l += dy * dy;
            if (l < dMin2)
                l = std::sqrt(dMin2 * l);
            fx += dx * charge * alpha / l;
            fy += dy * charge * alpha / l;
        }
        m_vx[i] += fx;
        m_vy[i] += fy;
    }
}

QRectF ForceLayout::bounds() const
{
    if (m_x.empty())
        return {};
    double x0 = m_x[0], y0 = m_y[0], x1 = m_x[0], y1 = m_y[0];
    for (size_t i = 1; i < m_x.size(); ++i) {
        x0 = std::min(x0, m_x[i]);
        y0 = std::min(y0, m_y[i]);
        x1 = std::max(x1, m_x[i]);
        y1 = std::max(y1, m_y[i]);
    }
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

} // namespace brulion::planning
