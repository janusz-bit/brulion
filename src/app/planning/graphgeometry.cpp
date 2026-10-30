#include "graphgeometry.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace brulion::planning::geometry {

namespace {

constexpr double kLabelCell = 64;
constexpr int kCornerSegments = 4;

bool finite(QPointF p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}

/// The premultiplied colour with its coverage scaled by `factor` (0–1).
QRgb scaled(QRgb c, double factor)
{
    if (factor >= 1)
        return c;
    const auto s = [factor](int v) { return int(std::lround(v * std::max(0.0, factor))); };
    return qRgba(s(qRed(c)), s(qGreen(c)), s(qBlue(c)), s(qAlpha(c)));
}

/// Offsets across a band of `width` with a fringe: 0 (transparent) | 1 | 2 | 3 (transparent),
/// and the colour factor of the inner rows (a band thinner than the fringe fades).
struct Band {
    double o[4];
    double factor;
};

Band band(double width, double fringe)
{
    const double half = width / 2;
    if (width >= fringe)
        return {{-half - fringe / 2, -half + fringe / 2, half - fringe / 2, half + fringe / 2}, 1.0};
    const double outer = (width + fringe) / 2;
    return {{-outer, 0, 0, outer}, 2 * width / (width + fringe)};
}

void quad(Mesh &m, quint16 a, quint16 b, quint16 c, quint16 d)
{
    m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
}

constexpr int kMaxSegments = 64;

/// cos / sin of the k-th of `segments` equal steps around the circle (tables built once).
const std::vector<QPointF> &unitCircle(int segments)
{
    static const auto tables = [] {
        std::array<std::vector<QPointF>, kMaxSegments + 1> t;
        for (int n = 3; n <= kMaxSegments; ++n) {
            t[size_t(n)].reserve(size_t(n));
            for (int k = 0; k < n; ++k) {
                const double a = 2 * std::numbers::pi * k / n;
                t[size_t(n)].emplace_back(std::cos(a), std::sin(a));
            }
        }
        return t;
    }();
    return tables[size_t(std::clamp(segments, 3, kMaxSegments))];
}

} // namespace

Mesh &MeshChunks::room(int vertices)
{
    if (m_used == 0 || m_chunks[m_used - 1].vertices.size() + size_t(vertices) > size_t(MaxVertices)) {
        if (m_used == m_chunks.size())
            m_chunks.emplace_back();
        Mesh &m = m_chunks[m_used++];
        m.vertices.clear();
        m.indices.clear();
        return m;
    }
    return m_chunks[m_used - 1];
}

int MeshChunks::vertexCount() const
{
    int n = 0;
    for (const Mesh &m : chunks())
        n += int(m.vertices.size());
    return n;
}

int MeshChunks::indexCount() const
{
    int n = 0;
    for (const Mesh &m : chunks())
        n += int(m.indices.size());
    return n;
}

void MeshChunks::clear()
{
    for (size_t i = 0; i < m_used; ++i) {
        m_chunks[i].vertices.clear();
        m_chunks[i].indices.clear();
    }
    m_used = 0;
}

QRgb premultiplied(const QColor &c, double alphaFactor)
{
    const QColor rgb = c.toRgb();
    const int a = int(std::lround(std::clamp(rgb.alphaF() * alphaFactor, 0.0, 1.0) * 255));
    return qPremultiply(qRgba(rgb.red(), rgb.green(), rgb.blue(), a));
}

Vertex vertex(QPointF p, QRgb c)
{
    return {float(p.x()), float(p.y()), quint8(qRed(c)), quint8(qGreen(c)), quint8(qBlue(c)), quint8(qAlpha(c))};
}

int discSegments(double screenRadius)
{
    if (!std::isfinite(screenRadius) || screenRadius <= 0)
        return 8;
    return std::clamp(int(std::ceil(2 * std::numbers::pi * screenRadius / 3.0)), 8, 64);
}

void appendDisc(MeshChunks &out, QPointF centre, double radius, QRgb colour, double fringe, int segments)
{
    if (!finite(centre) || !std::isfinite(radius) || !std::isfinite(fringe) || radius <= 0)
        return;
    segments = std::clamp(segments, 3, kMaxSegments);
    const double inner = std::max(0.0, radius - fringe / 2);
    const double outer = radius + fringe / 2;
    const QRgb fill = scaled(colour, fringe > 0 && 2 * radius < fringe ? 2 * radius / fringe : 1.0);
    const std::vector<QPointF> &circle = unitCircle(segments);
    Mesh &m = out.room(1 + 2 * segments);
    const auto base = quint16(m.vertices.size());
    m.vertices.push_back(vertex(centre, fill));
    for (const QPointF &u : circle)
        m.vertices.push_back(vertex(centre + u * inner, fill));
    for (const QPointF &u : circle)
        m.vertices.push_back(vertex(centre + u * outer, 0));
    const auto n = quint16(segments);
    for (quint16 k = 0; k < n; ++k) {
        const auto k1 = quint16((k + 1) % n);
        m.indices.insert(m.indices.end(), {base, quint16(base + 1 + k), quint16(base + 1 + k1)});
        quad(m, quint16(base + 1 + k), quint16(base + 1 + n + k), quint16(base + 1 + n + k1), quint16(base + 1 + k1));
    }
}

void appendRing(MeshChunks &out, QPointF centre, double radius, double width, QRgb colour, double fringe, int segments)
{
    if (!finite(centre) || !std::isfinite(radius) || !std::isfinite(width) || !std::isfinite(fringe) || radius <= 0
        || width <= 0)
        return;
    segments = std::clamp(segments, 3, kMaxSegments);
    const Band b = band(width, fringe);
    const QRgb c = scaled(colour, b.factor);
    const QRgb rowColour[4] = {0, c, c, 0};
    const std::vector<QPointF> &circle = unitCircle(segments);
    Mesh &m = out.room(4 * segments);
    const auto base = quint16(m.vertices.size());
    for (int row = 0; row < 4; ++row) {
        const double r = std::max(0.0, radius + b.o[row]);
        for (const QPointF &u : circle)
            m.vertices.push_back(vertex(centre + u * r, rowColour[row]));
    }
    const auto n = quint16(segments);
    for (quint16 row = 0; row < 3; ++row) {
        for (quint16 k = 0; k < n; ++k) {
            const auto k1 = quint16((k + 1) % n);
            quad(m, quint16(base + row * n + k), quint16(base + (row + 1) * n + k), quint16(base + (row + 1) * n + k1),
                 quint16(base + row * n + k1));
        }
    }
}

void appendSegment(MeshChunks &out, QPointF a, QPointF b, double width, QRgb colour, double fringe)
{
    if (!finite(a) || !finite(b) || !std::isfinite(width) || !std::isfinite(fringe) || width <= 0)
        return;
    const QPointF d = b - a;
    const double length = std::hypot(d.x(), d.y());
    if (!(length > 0) || !std::isfinite(length))
        return;
    const QPointF normal(-d.y() / length, d.x() / length);
    const Band band_ = band(width, fringe);
    const QRgb c = scaled(colour, band_.factor);
    const QRgb rowColour[4] = {0, c, c, 0};
    Mesh &m = out.room(8);
    const auto base = quint16(m.vertices.size());
    for (int row = 0; row < 4; ++row) {
        m.vertices.push_back(vertex(a + normal * band_.o[row], rowColour[row]));
        m.vertices.push_back(vertex(b + normal * band_.o[row], rowColour[row]));
    }
    for (quint16 row = 0; row < 3; ++row)
        quad(m, quint16(base + 2 * row), quint16(base + 2 * row + 1), quint16(base + 2 * row + 3), quint16(base + 2 * row + 2));
}

void appendRoundedRect(MeshChunks &out, const QRectF &rect, double radius, QRgb colour, double fringe)
{
    if (!finite(rect.topLeft()) || !finite(rect.bottomRight()) || !std::isfinite(radius) || !std::isfinite(fringe)
        || rect.width() <= 0 || rect.height() <= 0)
        return;
    const double rr = std::clamp(radius, 0.0, std::min(rect.width(), rect.height()) / 2);
    // Corners clockwise on screen (y down): top-left, top-right, bottom-right, bottom-left.
    const QPointF centres[4] = {{rect.left() + rr, rect.top() + rr},
                                {rect.right() - rr, rect.top() + rr},
                                {rect.right() - rr, rect.bottom() - rr},
                                {rect.left() + rr, rect.bottom() - rr}};
    const double starts[4] = {std::numbers::pi, 1.5 * std::numbers::pi, 0, 0.5 * std::numbers::pi};
    constexpr int points = 4 * (kCornerSegments + 1);
    Mesh &m = out.room(1 + 2 * points);
    const auto base = quint16(m.vertices.size());
    m.vertices.push_back(vertex(rect.center(), colour));
    QPointF outline[points];
    QPointF normals[points];
    for (int c = 0, i = 0; c < 4; ++c) {
        for (int k = 0; k <= kCornerSegments; ++k, ++i) {
            const double a = starts[c] + 0.5 * std::numbers::pi * k / kCornerSegments;
            normals[i] = QPointF(std::cos(a), std::sin(a));
            outline[i] = centres[c] + normals[i] * rr;
        }
    }
    for (int i = 0; i < points; ++i)
        m.vertices.push_back(vertex(outline[i] - normals[i] * (fringe / 2), colour));
    for (int i = 0; i < points; ++i)
        m.vertices.push_back(vertex(outline[i] + normals[i] * (fringe / 2), 0));
    const auto n = quint16(points);
    for (quint16 k = 0; k < n; ++k) {
        const auto k1 = quint16((k + 1) % n);
        m.indices.insert(m.indices.end(), {base, quint16(base + 1 + k), quint16(base + 1 + k1)});
        quad(m, quint16(base + 1 + k), quint16(base + 1 + n + k), quint16(base + 1 + n + k1), quint16(base + 1 + k1));
    }
}

QList<QRectF> placeLabels(const std::vector<LabelCandidate> &ordered, int maxLabels)
{
    QList<QRectF> out(qsizetype(ordered.size()));
    QHash<quint64, QList<QRectF>> grid;
    const auto cellKey = [](int cx, int cy) { return (quint64(quint32(cx)) << 32) | quint32(cy); };
    const auto cells = [](const QRectF &rect, auto &&f) {
        const int c0 = int(std::floor(rect.left() / kLabelCell)), c1 = int(std::floor(rect.right() / kLabelCell));
        const int r0 = int(std::floor(rect.top() / kLabelCell)), r1 = int(std::floor(rect.bottom() / kLabelCell));
        for (int cx = c0; cx <= c1; ++cx) {
            for (int cy = r0; cy <= r1; ++cy) {
                if (f(cx, cy))
                    return true;
            }
        }
        return false;
    };
    const auto overlaps = [&](const QRectF &rect) {
        return cells(rect, [&](int cx, int cy) {
            const auto it = grid.constFind(cellKey(cx, cy));
            if (it == grid.cend())
                return false;
            for (const QRectF &o : it.value()) {
                if (o.intersects(rect))
                    return true;
            }
            return false;
        });
    };
    std::vector<char> placed;
    int drawn = 0;
    for (size_t k = 0; k < ordered.size(); ++k) {
        if (drawn >= maxLabels)
            break;
        const LabelCandidate &c = ordered[k];
        if (c.node < 0 || !finite(c.centre) || !std::isfinite(c.radius) || !c.text.isValid())
            continue;
        if (size_t(c.node) < placed.size() && placed[size_t(c.node)])
            continue;
        const QPointF p = c.centre;
        const double r = c.radius;
        const double lw = c.text.width() + 6;
        const double lh = c.text.height() + 2;
        // Below the node; if taken, above, right, left.
        const QRectF options[] = {QRectF(p.x() - lw / 2, p.y() + r + 2, lw, lh), QRectF(p.x() - lw / 2, p.y() - r - 2 - lh, lw, lh),
                                  QRectF(p.x() + r + 3, p.y() - lh / 2, lw, lh), QRectF(p.x() - r - 3 - lw, p.y() - lh / 2, lw, lh)};
        QRectF rect;
        for (const QRectF &option : options) {
            if (c.always || !overlaps(option)) {
                rect = option;
                break;
            }
        }
        if (rect.isNull())
            continue;
        cells(rect, [&](int cx, int cy) {
            grid[cellKey(cx, cy)].append(rect);
            return false;
        });
        if (size_t(c.node) >= placed.size())
            placed.resize(size_t(c.node) + 1, 0);
        placed[size_t(c.node)] = 1;
        out[qsizetype(k)] = rect;
        ++drawn;
    }
    return out;
}

} // namespace brulion::planning::geometry
