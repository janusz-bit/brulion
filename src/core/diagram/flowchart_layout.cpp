// The flowchart's layout and scene: node shapes sized around their text, subgraphs laid out from
// the inside out (each one a layered graph of its own, then a box in the graph around it, in its
// own `direction`), links between the levels drawn from shape to shape through their bends.
#include "diagram/flowchart_p.h"
#include "diagram/layered_p.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace Qt::StringLiterals;

namespace brulion::diagram::detail {

namespace {

constexpr qreal Margin = 8;
constexpr qreal ClusterPad = 14;

qreal cylinderRy(qreal width, qreal height)
{
    return std::min(6 + width * 0.04, height * 0.16);
}

QList<QPointF> flatten(const QList<PathOp> &path)
{
    QList<QPointF> out;
    QPointF cur;
    for (const PathOp &op : path) {
        switch (op.type) {
        case PathOp::Type::Move:
            if (!out.isEmpty())
                return out; // the first subpath only
            out.append(op.p);
            cur = op.p;
            break;
        case PathOp::Type::Line:
            out.append(op.p);
            cur = op.p;
            break;
        case PathOp::Type::Cubic:
            for (int i = 1; i <= 8; ++i) {
                const qreal t = i / 8.0, u = 1 - t;
                out.append(cur * (u * u * u) + op.c1 * (3 * u * u * t) + op.c2 * (3 * u * t * t) + op.p * (t * t * t));
            }
            cur = op.p;
            break;
        case PathOp::Type::Close: return out;
        }
    }
    return out;
}

bool isHorizontal(Direction d)
{
    return d == Direction::LR || d == Direction::RL;
}

/// A curve through `pts` bending along the main axis (vertical: TB/BT).
QList<PathOp> bumpCurve(const QList<QPointF> &pts, bool vertical)
{
    QList<PathOp> p;
    p.append({PathOp::Type::Move, pts.front(), {}, {}});
    for (qsizetype i = 0; i + 1 < pts.size(); ++i) {
        const QPointF a = pts[i], b = pts[i + 1];
        QPointF c1, c2;
        if (vertical) {
            const qreal my = (a.y() + b.y()) / 2;
            c1 = QPointF(a.x(), my);
            c2 = QPointF(b.x(), my);
        } else {
            const qreal mx = (a.x() + b.x()) / 2;
            c1 = QPointF(mx, a.y());
            c2 = QPointF(mx, b.y());
        }
        p.append({PathOp::Type::Cubic, b, c1, c2});
    }
    return p;
}

qreal lineWidthOf(const FlowEdge &e)
{
    if (e.style.strokeWidth >= 0)
        return std::max<qreal>(0.5, e.style.strokeWidth);
    return e.stroke == LinkStroke::Thick ? 3.2 : 1.5;
}

} // namespace

QSizeF shapeSize(NodeShape shape, const QSizeF &text, qreal fs)
{
    const qreal padX = fs * 1.1, padY = fs * 0.65;
    const qreal tw = text.width(), th = text.height();
    const qreal w = tw + 2 * padX, h = th + 2 * padY;
    switch (shape) {
    case NodeShape::Rect:
    case NodeShape::Rounded: return {w, h};
    case NodeShape::Stadium: return {w + h * 0.5, h};
    case NodeShape::Subroutine: return {w + 16, h};
    case NodeShape::Cylinder: {
        const qreal ry = 6 + w * 0.04;
        const qreal total = h + 2 * ry;
        return ry <= total * 0.16 ? QSizeF(w, total) : QSizeF(w, h / (1 - 2 * 0.16));
    }
    case NodeShape::Circle: {
        const qreal d = std::max(tw, th) + 2 * padY * 1.4;
        return {d, d};
    }
    case NodeShape::DoubleCircle: {
        const qreal d = std::max(tw, th) + 2 * padY * 1.4 + 10;
        return {d, d};
    }
    case NodeShape::Asymmetric: return {w + h * 0.25, h};
    case NodeShape::Rhombus: {
        const qreal s = tw + th + fs * 2.2;
        return {s, s};
    }
    case NodeShape::Hexagon:
    case NodeShape::Parallelogram:
    case NodeShape::ParallelogramAlt:
    case NodeShape::Trapezoid:
    case NodeShape::TrapezoidAlt: return {w + h * 0.5, h};
    }
    return {w, h};
}

QList<PathOp> shapePath(NodeShape shape, const QRectF &r, QList<PathOp> *decorations)
{
    constexpr qreal pi = std::numbers::pi;
    const qreal l = r.left(), t = r.top(), rt = r.right(), b = r.bottom();
    const qreal cx = r.center().x(), cy = r.center().y(), h = r.height();
    switch (shape) {
    case NodeShape::Rect: return rectPath(r);
    case NodeShape::Rounded: return roundedRectPath(r, std::min<qreal>(6, h / 4));
    case NodeShape::Stadium: return roundedRectPath(r, h / 2);
    case NodeShape::Subroutine:
        if (decorations) {
            decorations->append({PathOp::Type::Move, QPointF(l + 8, t), {}, {}});
            decorations->append({PathOp::Type::Line, QPointF(l + 8, b), {}, {}});
            decorations->append({PathOp::Type::Move, QPointF(rt - 8, t), {}, {}});
            decorations->append({PathOp::Type::Line, QPointF(rt - 8, b), {}, {}});
        }
        return rectPath(r);
    case NodeShape::Cylinder: {
        const qreal rx = r.width() / 2, ry = cylinderRy(r.width(), h);
        QList<PathOp> p;
        p.append({PathOp::Type::Move, QPointF(l, t + ry), {}, {}});
        p.append({PathOp::Type::Line, QPointF(l, b - ry), {}, {}});
        appendArc(p, QPointF(cx, b - ry), rx, ry, pi, 0);
        p.append({PathOp::Type::Line, QPointF(rt, t + ry), {}, {}});
        appendArc(p, QPointF(cx, t + ry), rx, ry, 0, -pi);
        p.append({PathOp::Type::Close, {}, {}, {}});
        if (decorations) {
            decorations->append({PathOp::Type::Move, QPointF(l, t + ry), {}, {}});
            appendArc(*decorations, QPointF(cx, t + ry), rx, ry, pi, 0);
        }
        return p;
    }
    case NodeShape::Circle: return ellipsePath(r.center(), r.width() / 2, h / 2);
    case NodeShape::DoubleCircle:
        if (decorations)
            decorations->append(ellipsePath(r.center(), r.width() / 2 - 5, h / 2 - 5));
        return ellipsePath(r.center(), r.width() / 2, h / 2);
    case NodeShape::Asymmetric: {
        const qreal n = h * 0.25;
        return polygonPath({{l, t}, {rt, t}, {rt, b}, {l, b}, {l + n, cy}});
    }
    case NodeShape::Rhombus: return polygonPath({{cx, t}, {rt, cy}, {cx, b}, {l, cy}});
    case NodeShape::Hexagon: {
        const qreal m = h * 0.25;
        return polygonPath({{l, cy}, {l + m, t}, {rt - m, t}, {rt, cy}, {rt - m, b}, {l + m, b}});
    }
    case NodeShape::Parallelogram: {
        const qreal k = h * 0.5;
        return polygonPath({{l + k, t}, {rt, t}, {rt - k, b}, {l, b}});
    }
    case NodeShape::ParallelogramAlt: {
        const qreal k = h * 0.5;
        return polygonPath({{l, t}, {rt - k, t}, {rt, b}, {l + k, b}});
    }
    case NodeShape::Trapezoid: {
        const qreal k = h * 0.5;
        return polygonPath({{l + k, t}, {rt - k, t}, {rt, b}, {l, b}});
    }
    case NodeShape::TrapezoidAlt: {
        const qreal k = h * 0.5;
        return polygonPath({{l, t}, {rt, t}, {rt - k, b}, {l + k, b}});
    }
    }
    return rectPath(r);
}

QList<QPointF> shapeOutline(NodeShape shape, const QRectF &r)
{
    return flatten(shapePath(shape, r));
}

QPointF outlineExit(const QList<QPointF> &outline, const QPointF &inside, const QPointF &outside)
{
    const QPointF d = outside - inside;
    qreal best = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i < outline.size(); ++i) {
        const QPointF a = outline[i], b = outline[(i + 1) % outline.size()];
        const QPointF e = b - a;
        const qreal den = d.x() * e.y() - d.y() * e.x();
        if (std::abs(den) < 1e-12)
            continue;
        const QPointF w = a - inside;
        const qreal t = (w.x() * e.y() - w.y() * e.x()) / den;
        const qreal u = (w.x() * d.y() - w.y() * d.x()) / den;
        if (t > 1e-9 && u >= -1e-9 && u <= 1 + 1e-9)
            best = std::min(best, t);
    }
    return best == std::numeric_limits<qreal>::max() ? inside : inside + d * best;
}

Outcome<FlowLayout> layoutFlowchart(const Flowchart &chart, const Metrics &m)
{
    const qreal fs = m.fontSize;
    const qreal labelMax = fs * 15;
    FlowLayout L;
    const int N = int(chart.nodes.size()), C = int(chart.clusters.size()), E = int(chart.edges.size());
    QList<QSizeF> nodeSize(N);
    L.nodeText.reserve(N);
    for (int n = 0; n < N; ++n) {
        L.nodeText.append(layoutText(m, chart.nodes[n].label, fs, chart.nodes[n].style.bold, labelMax));
        nodeSize[n] = shapeSize(chart.nodes[n].shape, L.nodeText[n].size, fs);
    }
    for (int c = 0; c < C; ++c)
        L.clusterText.append(layoutText(m, chart.clusters[c].title, fs, false, labelMax * 1.5));
    const auto titleSpace = [&](int c) {
        return chart.clusters[c].title.isEmpty() ? 0.0 : L.clusterText[c].size.height() + 4;
    };

    // The items of each level (index c + 1; 0 the whole chart), in the order they are written.
    struct Child {
        bool cluster;
        int index;
        int line;
    };
    QList<QList<Child>> children(C + 1);
    for (int n = 0; n < N; ++n)
        children[chart.nodes[n].cluster + 1].append({false, n, chart.nodes[n].line});
    for (int c = 0; c < C; ++c)
        children[chart.clusters[c].parent + 1].append({true, c, chart.clusters[c].line});
    for (auto &list : children)
        std::ranges::stable_sort(list, [](const Child &a, const Child &b) { return a.line < b.line; });
    QList<int> depth(C, 0);
    for (int c = 0; c < C; ++c)
        for (int p = chart.clusters[c].parent; p >= 0; p = chart.clusters[p].parent)
            ++depth[c];
    const auto directionOf = [&](int c) {
        for (; c >= 0; c = chart.clusters[c].parent)
            if (chart.clusters[c].direction)
                return *chart.clusters[c].direction;
        return chart.direction;
    };

    // Each link's level (the innermost subgraph holding both ends) and the items it joins there.
    const auto chainOf = [&](const FlowEnd &end) {
        QList<int> chain;
        for (int c = end.cluster ? end.index : chart.nodes[end.index].cluster; c >= 0; c = chart.clusters[c].parent)
            chain.prepend(c);
        return chain;
    };
    struct Link {
        bool valid = false;
        int level = -1;
        Child from{}, to{};
    };
    QList<Link> links(E);
    QList<QList<int>> loops(N);
    for (int e = 0; e < E; ++e) {
        const FlowEdge &edge = chart.edges[e];
        const QList<int> a = chainOf(edge.from), b = chainOf(edge.to);
        qsizetype p = 0;
        while (p < a.size() && p < b.size() && a[p] == b[p])
            ++p;
        Link &k = links[e];
        k.level = p > 0 ? a[p - 1] : -1;
        const auto item = [&](const FlowEnd &end, const QList<int> &chain) -> std::optional<Child> {
            if (p < chain.size())
                return Child{true, chain[p], 0};
            if (end.cluster)
                return std::nullopt; // a subgraph linked to something inside it
            return Child{false, end.index, 0};
        };
        const auto f = item(edge.from, a), t = item(edge.to, b);
        if (!f || !t)
            continue;
        if (!edge.from.cluster && !edge.to.cluster && edge.from.index == edge.to.index) {
            loops[edge.from.index].append(e);
            continue;
        }
        k.valid = true;
        k.from = *f;
        k.to = *t;
    }
    L.edges.resize(E);
    for (int e = 0; e < E; ++e) {
        if (!chart.edges[e].label.isEmpty())
            L.edges[e].labelText = layoutText(m, chart.edges[e].label, fs * 0.93, false, labelMax);
    }
    const auto labelBox = [&](int e) {
        const QSizeF s = L.edges[e].labelText.size;
        return QSizeF(s.width() + 10, s.height() + 4);
    };
    // Room for self-loops beside their node (right of it; below it in LR / RL levels).
    const auto loopRoom = [&](int n, bool horizontal) {
        if (loops[n].isEmpty())
            return 0.0;
        qreal label = 0;
        for (const int e : std::as_const(loops[n]))
            if (!chart.edges[e].label.isEmpty())
                label = std::max(label, horizontal ? labelBox(e).height() : labelBox(e).width());
        return 22 + 8.0 * (loops[n].size() - 1) + label + 4;
    };

    // Levels from the innermost out.
    QList<QPointF> nodeRel(N), clusterRel(C);
    QList<QSizeF> clusterSize(C), contentSize(C + 1);
    QList<QList<QPointF>> bendsRel(E);
    QList<std::optional<QPointF>> labelRel(E);
    QList<int> levels;
    for (int c = 0; c < C; ++c)
        levels.append(c);
    std::ranges::stable_sort(levels, [&](int a, int b) { return depth[a] > depth[b]; });
    levels.append(-1);
    int layoutNodes = 0;
    for (const int c : std::as_const(levels)) {
        const QList<Child> &items = children[c + 1];
        const Direction dir = directionOf(c);
        const bool horizontal = isHorizontal(dir);
        QList<LayerNode> ln;
        QHash<qint64, int> local; // (cluster?, index) → layer node
        const auto key = [](const Child &k) { return (qint64(k.cluster) << 32) | quint32(k.index); };
        for (const Child &k : items) {
            const QSizeF s = k.cluster ? clusterSize[k.index] : nodeSize[k.index];
            LayerNode n;
            n.width = horizontal ? s.height() : s.width();
            n.height = horizontal ? s.width() : s.height();
            n.extraAfter = k.cluster ? 0 : loopRoom(k.index, horizontal);
            // A loop's label beside the node stays within the node's rank.
            if (!k.cluster)
                for (const int e : std::as_const(loops[k.index]))
                    if (!chart.edges[e].label.isEmpty())
                        n.height = std::max(n.height, (horizontal ? labelBox(e).width() : labelBox(e).height()) + 4);
            local.insert(key(k), int(ln.size()));
            ln.append(n);
        }
        QList<LayerEdge> le;
        QList<int> leEdge;
        for (int e = 0; e < E; ++e) {
            if (!links[e].valid || links[e].level != c)
                continue;
            LayerEdge x;
            x.from = local.value(key(links[e].from), -1);
            x.to = local.value(key(links[e].to), -1);
            if (x.from < 0 || x.to < 0 || x.from == x.to) {
                links[e].valid = false;
                continue;
            }
            x.minLength = chart.edges[e].length;
            if (!chart.edges[e].label.isEmpty()) {
                const QSizeF s = labelBox(e);
                x.label = horizontal ? s.transposed() : s;
            }
            le.append(x);
            leEdge.append(e);
        }
        LayerOptions options;
        options.maxNodes = limits::MaxLayoutNodes - layoutNodes;
        if (horizontal) {
            options.nodeSep = 28;
            options.rankSep = 56;
        }
        const auto result = layered(ln, le, options);
        if (!result)
            return makeError(Error::Code::TooLarge, 0, u"layout"_s);
        layoutNodes += int(ln.size());
        for (const auto &bends : result->bends)
            layoutNodes += int(bends.size());
        const qreal W = result->size.width(), H = result->size.height();
        const auto turn = [&](const QPointF &p) {
            switch (dir) {
            case Direction::TB: return p;
            case Direction::BT: return QPointF(p.x(), H - p.y());
            case Direction::LR: return QPointF(p.y(), p.x());
            case Direction::RL: return QPointF(H - p.y(), p.x());
            }
            return p;
        };
        const QSizeF content = items.isEmpty() ? QSizeF(0, 0) : horizontal ? QSizeF(H, W) : QSizeF(W, H);
        contentSize[c + 1] = content;
        for (qsizetype i = 0; i < items.size(); ++i) {
            const QPointF p = turn(result->centers[i]);
            if (items[i].cluster)
                clusterRel[items[i].index] = p;
            else
                nodeRel[items[i].index] = p;
        }
        for (qsizetype i = 0; i < le.size(); ++i) {
            const int e = leEdge[i];
            for (const QPointF &p : result->bends[i])
                bendsRel[e].append(turn(p));
            if (result->labels[i])
                labelRel[e] = turn(*result->labels[i]);
        }
        if (c >= 0) {
            const qreal w = std::max(content.width(), L.clusterText[c].size.width()) + 2 * ClusterPad;
            const qreal h = content.height() + 2 * ClusterPad + titleSpace(c);
            clusterSize[c] = QSizeF(std::max(w, 2 * ClusterPad + 20), std::max(h, 2 * ClusterPad + 10));
        }
    }

    // Absolute positions, from the outside in.
    QString chartTitle = chart.title;
    const TextBlock title = layoutText(m, chartTitle, fs * 1.15, true, 0);
    const qreal titleH = chartTitle.isEmpty() ? 0 : title.size.height() + 8;
    const QSizeF root = contentSize[0];
    const qreal width = std::max(root.width(), chartTitle.isEmpty() ? 0 : title.size.width()) + 2 * Margin;
    QList<QPointF> origin(C + 1);
    origin[0] = QPointF((width - root.width()) / 2, Margin + titleH);
    L.clusters.resize(C);
    QList<int> outward = levels;
    outward.removeLast();
    std::ranges::reverse(outward);
    for (const int c : std::as_const(outward)) {
        const QPointF o = origin[chart.clusters[c].parent + 1];
        const QSizeF s = clusterSize[c];
        const QRectF r(o + clusterRel[c] - QPointF(s.width() / 2, s.height() / 2), s);
        L.clusters[c] = r;
        origin[c + 1] =
                QPointF(r.left() + (r.width() - contentSize[c + 1].width()) / 2, r.top() + ClusterPad + titleSpace(c));
    }
    L.nodes.resize(N);
    for (int n = 0; n < N; ++n) {
        const QSizeF s = nodeSize[n];
        L.nodes[n] =
                QRectF(origin[chart.nodes[n].cluster + 1] + nodeRel[n] - QPointF(s.width() / 2, s.height() / 2), s);
    }
    L.size = QSizeF(width, root.height() + 2 * Margin + titleH);

    // Links.
    const auto outlineOf = [&](const FlowEnd &end) {
        return end.cluster ? flatten(rectPath(L.clusters[end.index]))
                           : shapeOutline(chart.nodes[end.index].shape, L.nodes[end.index]);
    };
    const auto centerOf = [&](const FlowEnd &end) {
        return end.cluster ? L.clusters[end.index].center() : L.nodes[end.index].center();
    };
    for (int e = 0; e < E; ++e) {
        const FlowEdge &edge = chart.edges[e];
        FlowLayout::Edge &g = L.edges[e];
        g.width = lineWidthOf(edge);
        if (!links[e].valid)
            continue;
        g.drawn = edge.stroke != LinkStroke::Invisible;
        const QPointF o = origin[links[e].level + 1];
        QList<QPointF> pts;
        const QPointF a = centerOf(edge.from), b = centerOf(edge.to);
        pts.append(a);
        for (const QPointF &p : std::as_const(bendsRel[e]))
            pts.append(o + p);
        pts.append(b);
        pts.front() = outlineExit(outlineOf(edge.from), a, pts[1]);
        pts.back() = outlineExit(outlineOf(edge.to), b, pts[pts.size() - 2]);
        const bool vertical = !isHorizontal(directionOf(links[e].level));
        g.path = bumpCurve(pts, vertical);
        g.start = pts.front();
        g.end = pts.back();
        PathOp &first = g.path[1];
        PathOp &last = g.path.last();
        g.startDir = unitVector(first.c1, g.start, unitVector(pts[1], pts[0]));
        g.endDir = unitVector(last.c2, g.end, unitVector(pts[pts.size() - 2], pts.back()));
        g.lineStart = g.start - g.startDir * headInset(edge.startHead, g.width);
        g.lineEnd = g.end - g.endDir * headInset(edge.endHead, g.width);
        g.path.front().p = g.lineStart;
        last.p = g.lineEnd;
        if (labelRel[e]) {
            const QSizeF s = labelBox(e);
            g.label = QRectF(o + *labelRel[e] - QPointF(s.width() / 2, s.height() / 2), s);
        }
    }
    // Self-loops beside their node.
    for (int n = 0; n < N; ++n) {
        const bool horizontal = isHorizontal(directionOf(chart.nodes[n].cluster));
        const QRectF r = L.nodes[n];
        const QList<QPointF> outline = shapeOutline(chart.nodes[n].shape, r);
        for (qsizetype k = 0; k < loops[n].size(); ++k) {
            const int e = loops[n][k];
            const FlowEdge &edge = chart.edges[e];
            FlowLayout::Edge &g = L.edges[e];
            g.drawn = edge.stroke != LinkStroke::Invisible;
            const qreal reach = 20 + 8.0 * k;
            const QPointF c = r.center();
            QPointF p0, p1, c1, c2;
            if (!horizontal) {
                const qreal dy = std::min<qreal>(r.height() / 4, 10);
                p0 = outlineExit(outline, c, c + QPointF(r.width(), -dy * 2));
                p1 = outlineExit(outline, c, c + QPointF(r.width(), dy * 2));
                const qreal x = std::max(p0.x(), p1.x()) + reach;
                c1 = QPointF(x, p0.y() - reach * 0.4);
                c2 = QPointF(x, p1.y() + reach * 0.4);
            } else {
                const qreal dx = std::min<qreal>(r.width() / 4, 10);
                p0 = outlineExit(outline, c, c + QPointF(-dx * 2, r.height()));
                p1 = outlineExit(outline, c, c + QPointF(dx * 2, r.height()));
                const qreal y = std::max(p0.y(), p1.y()) + reach;
                c1 = QPointF(p0.x() - reach * 0.4, y);
                c2 = QPointF(p1.x() + reach * 0.4, y);
            }
            g.start = p0;
            g.end = p1;
            g.startDir = unitVector(c1, p0);
            g.endDir = unitVector(c2, p1);
            g.lineStart = p0 - g.startDir * headInset(edge.startHead, g.width);
            g.lineEnd = p1 - g.endDir * headInset(edge.endHead, g.width);
            g.path = {{PathOp::Type::Move, g.lineStart, {}, {}}, {PathOp::Type::Cubic, g.lineEnd, c1, c2}};
            if (!edge.label.isEmpty()) {
                const QSizeF s = labelBox(e);
                const QPointF at = horizontal ? QPointF(c.x() - s.width() / 2, c1.y() - reach * 0.25 + 2)
                                              : QPointF(c1.x() - reach * 0.25 + 2, c.y() - s.height() / 2);
                g.label = QRectF(at, s);
            }
        }
    }
    return L;
}

Scene drawFlowchart(const Flowchart &chart, const FlowLayout &L, const Metrics &m)
{
    Scene scene;
    scene.size = L.size;
    scene.title = chart.title;
    QList<Item> &items = scene.items;
    const qreal fs = m.fontSize;
    if (!chart.title.isEmpty()) {
        const TextBlock t = layoutText(m, chart.title, fs * 1.15, true, 0);
        items.append(textItem(t, QRectF(0, Margin, L.size.width(), t.size.height()), Paint{Role::Title, {}}));
    }
    // Subgraphs, the outer ones first.
    QList<int> order;
    for (qsizetype c = 0; c < chart.clusters.size(); ++c)
        order.append(int(c));
    const auto depthOf = [&](int c) {
        int d = 0;
        for (int p = chart.clusters[c].parent; p >= 0; p = chart.clusters[p].parent)
            ++d;
        return d;
    };
    std::ranges::stable_sort(order, [&](int a, int b) { return depthOf(a) < depthOf(b); });
    QList<Item> titles;
    for (const int c : std::as_const(order)) {
        const FlowCluster &cl = chart.clusters[c];
        const QRectF r = L.clusters[c];
        const Paint fill = cl.style.fill.isEmpty() ? Paint{Role::ClusterFill, {}} : Paint{Role::None, cl.style.fill};
        const Paint stroke =
                cl.style.stroke.isEmpty() ? Paint{Role::ClusterStroke, {}} : Paint{Role::None, cl.style.stroke};
        items.append(pathItem(roundedRectPath(r, 4), fill, stroke, cl.style.strokeWidth >= 0 ? cl.style.strokeWidth : 1,
                              cl.style.dash.value_or(Dash::Solid)));
        if (!cl.title.isEmpty()) {
            const TextBlock &t = L.clusterText[c];
            Paint color{Role::ClusterText, {}};
            if (!cl.style.color.isEmpty())
                color = {Role::None, cl.style.color};
            else if (!cl.style.fill.isEmpty())
                color = {Role::None, textOn(cl.style.fill, u"#1f2430"_s, u"#f3f1ec"_s)};
            // Over the links entering the subgraph, on its own fill.
            const qreal w = std::min(t.size.width() + 8, r.width() - 2);
            Item title =
                    textItem(t, QRectF(r.center().x() - w / 2, r.top() + ClusterPad - 4, w, t.size.height()), color);
            title.background = fill;
            titles.append(title);
        }
    }
    // Links, then their labels.
    for (qsizetype e = 0; e < chart.edges.size(); ++e) {
        const FlowEdge &edge = chart.edges[e];
        const FlowLayout::Edge &g = L.edges[e];
        if (!g.drawn)
            continue;
        const Paint color = edge.style.stroke.isEmpty() ? Paint{Role::Line, {}} : Paint{Role::None, edge.style.stroke};
        const Dash dash = edge.style.dash.value_or(edge.stroke == LinkStroke::Dotted ? Dash::Dotted : Dash::Solid);
        items.append(pathItem(g.path, Paint{}, color, g.width, dash));
        addHead(items, edge.endHead, g.end, g.endDir, color, g.width);
        addHead(items, edge.startHead, g.start, g.startDir, color, g.width);
    }
    for (qsizetype e = 0; e < chart.edges.size(); ++e) {
        const FlowEdge &edge = chart.edges[e];
        const FlowLayout::Edge &g = L.edges[e];
        if (!g.drawn || g.label.isEmpty())
            continue;
        Item t = textItem(g.labelText, g.label,
                          edge.style.color.isEmpty() ? Paint{Role::LineText, {}} : Paint{Role::None, edge.style.color});
        t.background = Paint{Role::LabelBackground, {}};
        items.append(t);
    }
    items.append(titles);
    // Nodes.
    for (qsizetype n = 0; n < chart.nodes.size(); ++n) {
        const FlowNode &node = chart.nodes[n];
        const QRectF r = L.nodes[n];
        const Style &st = node.style;
        const Paint fill = st.fill.isEmpty() ? Paint{Role::NodeFill, {}} : Paint{Role::None, st.fill};
        const Paint stroke = st.stroke.isEmpty() ? Paint{Role::NodeStroke, {}} : Paint{Role::None, st.stroke};
        const qreal width = st.strokeWidth >= 0 ? st.strokeWidth : 1.2;
        QList<PathOp> deco;
        items.append(pathItem(shapePath(node.shape, r, &deco), fill, stroke, width, st.dash.value_or(Dash::Solid)));
        if (!deco.isEmpty())
            items.append(pathItem(deco, Paint{}, stroke, width));
        Paint color{Role::NodeText, {}};
        if (!st.color.isEmpty())
            color = {Role::None, st.color};
        else if (!st.fill.isEmpty())
            color = {Role::None, textOn(st.fill, u"#1f2430"_s, u"#f3f1ec"_s)};
        QRectF box = r;
        if (node.shape == NodeShape::Cylinder)
            box.setTop(r.top() + cylinderRy(r.width(), r.height()));
        else if (node.shape == NodeShape::Asymmetric)
            box.setLeft(r.left() + r.height() * 0.25);
        items.append(textItem(L.nodeText[n], box, color));
    }
    return scene;
}

} // namespace brulion::diagram::detail
