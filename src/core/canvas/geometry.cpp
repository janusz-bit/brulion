#include "canvas/geometry.h"

#include <QLineF>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

namespace {

double clampCoordinate(double v)
{
    if (!std::isfinite(v))
        return 0;
    return std::clamp(v, -CoordinateLimit, CoordinateLimit);
}

double clampSize(double v)
{
    if (!std::isfinite(v))
        return 1;
    return std::clamp(v, 1.0, CoordinateLimit);
}

QPointF normalized(const QPointF &v)
{
    const double len = std::hypot(v.x(), v.y());
    return len > 1e-9 ? v / len : QPointF(1, 0);
}

QList<QPointF> arrowAt(const QPointF &tip, const QPointF &direction)
{
    const QPointF d = normalized(direction);
    const QPointF n(-d.y(), d.x());
    const QPointF base = tip - d * ArrowLength;
    return {tip, base + n * (ArrowWidth / 2), base - n * (ArrowWidth / 2)};
}

double distanceToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    double t = len2 > 0 ? QPointF::dotProduct(p - a, ab) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

/// The sides two rectangles connect by when the edge names none: the pair facing each other.
std::pair<Side, Side> facingSides(const QRectF &from, const QRectF &to)
{
    const QPointF d = to.center() - from.center();
    // Prefer the axis with the larger gap between the rectangles.
    const double gapX = std::max(to.left() - from.right(), from.left() - to.right());
    const double gapY = std::max(to.top() - from.bottom(), from.top() - to.bottom());
    const bool horizontal = gapX > gapY || (gapX == gapY && std::abs(d.x()) >= std::abs(d.y()));
    if (horizontal)
        return d.x() >= 0 ? std::pair{Side::Right, Side::Left} : std::pair{Side::Left, Side::Right};
    return d.y() >= 0 ? std::pair{Side::Bottom, Side::Top} : std::pair{Side::Top, Side::Bottom};
}

EdgeShape shapeBetween(const QPointF &a, const QPointF &dirA, const QPointF &b, const QPointF &dirB, bool arrowStart,
                       bool arrowEnd)
{
    EdgeShape s;
    s.valid = true;
    const double dist = std::hypot(b.x() - a.x(), b.y() - a.y());
    const double bend = std::clamp(dist / 2, 40.0, 300.0);
    QPointF start = a;
    QPointF end = b;
    if (arrowStart) {
        s.startArrow = arrowAt(a, -dirA);
        start = a + dirA * (ArrowLength * 0.8);
    }
    if (arrowEnd) {
        s.endArrow = arrowAt(b, -dirB);
        end = b + dirB * (ArrowLength * 0.8);
    }
    s.start = start;
    s.end = end;
    s.c1 = a + dirA * bend;
    s.c2 = b + dirB * bend;
    s.labelPoint = bezierPoint(s, 0.5);
    return s;
}

} // namespace

QRectF nodeRect(const Node &n)
{
    return QRectF(clampCoordinate(n.x), clampCoordinate(n.y), clampSize(n.width), clampSize(n.height));
}

QRectF bounds(const Canvas &c)
{
    QRectF r;
    for (const Node &n : c.nodes)
        r = r.isNull() ? nodeRect(n) : r.united(nodeRect(n));
    return r;
}

QRectF bounds(const Canvas &c, const QSet<QString> &nodeIds)
{
    QRectF r;
    for (const Node &n : c.nodes) {
        if (nodeIds.contains(n.id))
            r = r.isNull() ? nodeRect(n) : r.united(nodeRect(n));
    }
    return r;
}

std::optional<Side> sideFrom(QStringView name)
{
    if (name == u"top")
        return Side::Top;
    if (name == u"right")
        return Side::Right;
    if (name == u"bottom")
        return Side::Bottom;
    if (name == u"left")
        return Side::Left;
    return std::nullopt;
}

QString sideName(Side side)
{
    switch (side) {
    case Side::Top: return u"top"_s;
    case Side::Right: return u"right"_s;
    case Side::Bottom: return u"bottom"_s;
    case Side::Left: return u"left"_s;
    }
    return u"right"_s;
}

QPointF anchor(const QRectF &r, Side side)
{
    switch (side) {
    case Side::Top: return {r.center().x(), r.top()};
    case Side::Right: return {r.right(), r.center().y()};
    case Side::Bottom: return {r.center().x(), r.bottom()};
    case Side::Left: return {r.left(), r.center().y()};
    }
    return r.center();
}

QPointF outward(Side side)
{
    switch (side) {
    case Side::Top: return {0, -1};
    case Side::Right: return {1, 0};
    case Side::Bottom: return {0, 1};
    case Side::Left: return {-1, 0};
    }
    return {1, 0};
}

Side nearestSide(const QRectF &r, const QPointF &p)
{
    const std::array<std::pair<double, Side>, 4> d{{
            {std::abs(p.y() - r.top()), Side::Top},
            {std::abs(p.x() - r.right()), Side::Right},
            {std::abs(p.y() - r.bottom()), Side::Bottom},
            {std::abs(p.x() - r.left()), Side::Left},
    }};
    return std::ranges::min_element(d, {}, &std::pair<double, Side>::first)->second;
}

EdgeShape edgeShape(const Edge &e, const QRectF &from, const QRectF &to)
{
    const auto [autoFrom, autoTo] = facingSides(from, to);
    const Side fs = sideFrom(e.fromSide).value_or(autoFrom);
    const Side ts = sideFrom(e.toSide).value_or(autoTo);
    EdgeShape s = shapeBetween(anchor(from, fs), outward(fs), anchor(to, ts), outward(ts), e.arrowAtStart(),
                               e.arrowAtEnd());
    s.fromSide = fs;
    s.toSide = ts;
    return s;
}

EdgeShape edgeShape(const Canvas &c, const Edge &e)
{
    const Node *from = c.node(e.fromNode);
    const Node *to = c.node(e.toNode);
    if (!from || !to)
        return {};
    return edgeShape(e, nodeRect(*from), nodeRect(*to));
}

EdgeShape looseShape(const QRectF &from, Side fromSide, const QPointF &to)
{
    const QPointF a = anchor(from, fromSide);
    const QPointF dir = outward(fromSide);
    // The loose end comes in from where it is, as if along the line to the start.
    const QPointF back = normalized(a - to);
    EdgeShape s = shapeBetween(a, dir, to, back, false, true);
    s.fromSide = fromSide;
    return s;
}

QPointF bezierPoint(const EdgeShape &s, double t)
{
    const double u = 1 - t;
    return s.start * (u * u * u) + s.c1 * (3 * u * u * t) + s.c2 * (3 * u * t * t) + s.end * (t * t * t);
}

QList<QPointF> polyline(const EdgeShape &s, int segments)
{
    QList<QPointF> out;
    segments = std::max(1, segments);
    out.reserve(segments + 1);
    for (int i = 0; i <= segments; ++i)
        out.append(bezierPoint(s, double(i) / segments));
    return out;
}

double distanceTo(const EdgeShape &s, const QPointF &p)
{
    if (!s.valid)
        return HUGE_VAL;
    const QList<QPointF> pts = polyline(s, 32);
    double best = HUGE_VAL;
    for (qsizetype i = 1; i < pts.size(); ++i)
        best = std::min(best, distanceToSegment(p, pts.at(i - 1), pts.at(i)));
    return best;
}

QString svgPath(const EdgeShape &s)
{
    const auto n = [](double v) { return QString::number(std::round(v * 10) / 10, 'f', 1); };
    const auto pt = [&n](const QPointF &p) { return n(p.x()) + u' ' + n(p.y()); };
    return u"M "_s + pt(s.start) + u" C "_s + pt(s.c1) + u", "_s + pt(s.c2) + u", "_s + pt(s.end);
}

qsizetype nodeAt(const Canvas &c, const QPointF &p, bool groupsByFrame, double frame)
{
    qsizetype group = -1;
    for (qsizetype i = c.nodes.size() - 1; i >= 0; --i) {
        const Node &n = c.nodes.at(i);
        const QRectF r = nodeRect(n);
        if (n.isGroup()) {
            if (group >= 0)
                continue;
            const QRectF label(r.left(), r.top() - GroupLabelHeight, std::max(r.width(), 1.0), GroupLabelHeight);
            const bool onLabel = !n.label.isEmpty() && label.contains(p);
            if (!r.contains(p) && !onLabel)
                continue;
            if (groupsByFrame && !onLabel && r.adjusted(frame, frame, -frame, -frame).contains(p))
                continue;
            group = i;
            continue;
        }
        if (r.contains(p))
            return i;
    }
    return group;
}

qsizetype edgeAt(const Canvas &c, const QPointF &p, double tolerance)
{
    qsizetype best = -1;
    double bestDistance = tolerance;
    for (qsizetype i = c.edges.size() - 1; i >= 0; --i) {
        const EdgeShape s = edgeShape(c, c.edges.at(i));
        if (!s.valid)
            continue;
        // Cheap rejection by the curve's bounding box.
        const QRectF box = QRectF(s.start, s.end).normalized().united(QRectF(s.c1, s.c2).normalized());
        if (!box.adjusted(-tolerance - ArrowLength, -tolerance - ArrowLength, tolerance + ArrowLength,
                          tolerance + ArrowLength)
                     .contains(p))
            continue;
        const double d = distanceTo(s, p);
        if (d < bestDistance) {
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

QSet<QString> nodesIn(const Canvas &c, const QRectF &r, bool contained)
{
    QSet<QString> out;
    const QRectF band = r.normalized();
    for (const Node &n : c.nodes) {
        const QRectF nr = nodeRect(n);
        if (contained ? band.contains(nr) : band.intersects(nr))
            out.insert(n.id);
    }
    return out;
}

QSet<QString> groupContents(const Canvas &c, const QString &groupId)
{
    QSet<QString> out;
    const Node *g = c.node(groupId);
    if (!g || !g->isGroup())
        return out;
    const QRectF gr = nodeRect(*g);
    for (const Node &n : c.nodes) {
        if (n.id != groupId && gr.contains(nodeRect(n)))
            out.insert(n.id);
    }
    return out;
}

QSet<QString> withGroupContents(const Canvas &c, const QSet<QString> &ids)
{
    QSet<QString> out = ids;
    for (const Node &n : c.nodes) {
        if (n.isGroup() && ids.contains(n.id))
            out.unite(groupContents(c, n.id));
    }
    return out;
}

namespace {

struct Preset {
    const char16_t *light;
    const char16_t *dark;
};

// Obsidian's --color-red … --color-purple (light and dark themes).
constexpr std::array<Preset, 6> Presets{{
        {u"#e93147", u"#fb464c"},
        {u"#ec7500", u"#e9973f"},
        {u"#e0ac00", u"#e0de71"},
        {u"#08b94e", u"#44cf6e"},
        {u"#00bfbc", u"#53dfdd"},
        {u"#7852ee", u"#a882ff"},
}};

bool isHexDigit(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'0' && u <= u'9') || (u >= u'a' && u <= u'f') || (u >= u'A' && u <= u'F');
}

} // namespace

int presetIndex(QStringView canvasColor)
{
    const QStringView c = canvasColor.trimmed();
    if (c.size() == 1 && c[0] >= u'1' && c[0] <= u'6')
        return c[0].unicode() - u'0';
    return 0;
}

QString normalizeColor(QStringView color)
{
    const QStringView c = color.trimmed();
    if (presetIndex(c) > 0)
        return c.toString();
    if (!c.startsWith(u'#'))
        return {};
    const QStringView hex = c.sliced(1);
    if (!std::ranges::all_of(hex, isHexDigit))
        return {};
    if (hex.size() == 6)
        return c.toString().toLower();
    if (hex.size() == 3) {
        QString out = u"#"_s;
        for (const QChar h : hex)
            out += QString(2, h);
        return out.toLower();
    }
    return {};
}

QString colorHex(QStringView canvasColor, bool dark)
{
    if (const int i = presetIndex(canvasColor); i > 0)
        return QString::fromUtf16(dark ? Presets[i - 1].dark : Presets[i - 1].light);
    const QString hex = normalizeColor(canvasColor);
    return hex.startsWith(u'#') ? hex : QString();
}

} // namespace brulion::canvas
