#include "canvas/edit.h"

#include "util/paths.h"
#include "util/strings.h"

#include <QHash>
#include <QSizeF>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

namespace {

double whole(double v)
{
    return std::isfinite(v) ? std::round(std::clamp(v, -CoordinateLimit, CoordinateLimit)) : 0;
}

void placeNode(Node &n, const QRectF &r)
{
    n.x = whole(r.x());
    n.y = whole(r.y());
    n.width = std::max(MinNodeSize, whole(r.width()));
    n.height = std::max(MinNodeSize, whole(r.height()));
}

/// A unique id: from `ids`, again until no node or edge of `c` (or one handed out now) has it.
QString freshId(const Canvas &c, const IdSource &ids, QSet<QString> &taken)
{
    for (int i = 0; i < 1000; ++i) {
        QString id = ids ? ids() : newId();
        if (!id.isEmpty() && !taken.contains(id) && !c.node(id) && !c.edge(id)) {
            taken.insert(id);
            return id;
        }
    }
    QString id = newId();
    taken.insert(id);
    return id;
}

bool isPictureFile(QStringView file)
{
    return paths::isPicturePath(file.toString());
}

} // namespace

QString newId()
{
    return strings::randomText(u"0123456789abcdef", 16);
}

QSizeF defaultSize(QStringView type, QStringView file)
{
    if (type == TextType)
        return {250, 60};
    if (type == LinkType)
        return {400, 200};
    if (type == GroupType)
        return {400, 300};
    if (type == FileType) {
        if (file.endsWith(u".md", Qt::CaseInsensitive) || file.endsWith(u".canvas", Qt::CaseInsensitive))
            return {400, 400};
        if (isPictureFile(file) || file.endsWith(u".pdf", Qt::CaseInsensitive))
            return {400, 300};
        return {400, 100};
    }
    return {250, 60};
}

Node makeNode(QStringView type, const QRectF &rect, const IdSource &ids)
{
    Node n;
    n.id = ids ? ids() : newId();
    n.type = type.toString();
    placeNode(n, rect);
    return n;
}

Node makeTextNode(const QString &text, const QRectF &rect, const IdSource &ids)
{
    Node n = makeNode(TextType, rect, ids);
    n.text = text;
    return n;
}

Node makeFileNode(const QString &path, const QString &subpath, const QRectF &rect, const IdSource &ids)
{
    Node n = makeNode(FileType, rect, ids);
    n.file = path;
    n.subpath = subpath.isEmpty() || subpath.startsWith(u'#') ? subpath : u'#' + subpath;
    return n;
}

Node makeLinkNode(const QString &url, const QRectF &rect, const IdSource &ids)
{
    Node n = makeNode(LinkType, rect, ids);
    n.url = url;
    return n;
}

Canvas addNode(Canvas c, Node node)
{
    if (node.id.isEmpty() || c.node(node.id))
        node.id = newId();
    c.nodes.append(std::move(node));
    return c;
}

Canvas connectNodes(Canvas c, const QString &from, const QString &fromSide, const QString &to, const QString &toSide,
                    QString *edgeId, const IdSource &ids)
{
    if (!c.node(from) || !c.node(to) || from == to)
        return c;
    QSet<QString> taken;
    Edge e;
    e.id = freshId(c, ids, taken);
    e.fromNode = from;
    e.toNode = to;
    e.fromSide = sideFrom(fromSide) ? fromSide : QString();
    e.toSide = sideFrom(toSide) ? toSide : QString();
    if (edgeId)
        *edgeId = e.id;
    c.edges.append(std::move(e));
    return c;
}

Canvas removeItems(Canvas c, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds)
{
    c.nodes.removeIf([&](const Node &n) { return nodeIds.contains(n.id); });
    c.edges.removeIf([&](const Edge &e) {
        return edgeIds.contains(e.id) || nodeIds.contains(e.fromNode) || nodeIds.contains(e.toNode);
    });
    return c;
}

Canvas moveNodes(Canvas c, const QSet<QString> &ids, const QPointF &delta)
{
    if (delta.isNull())
        return c;
    for (Node &n : c.nodes) {
        if (!ids.contains(n.id))
            continue;
        // (An axis it does not move along keeps its number as written: 12.5 stays.)
        if (delta.x() != 0)
            n.x = whole(n.x + delta.x());
        if (delta.y() != 0)
            n.y = whole(n.y + delta.y());
    }
    return c;
}

Canvas setNodeRect(Canvas c, const QString &id, const QRectF &rect)
{
    if (Node *n = c.node(id))
        placeNode(*n, rect.normalized());
    return c;
}

Canvas setNodeText(Canvas c, const QString &id, const QString &text)
{
    if (Node *n = c.node(id))
        n->text = text;
    return c;
}

Canvas setNodeFile(Canvas c, const QString &id, const QString &file, const QString &subpath)
{
    if (Node *n = c.node(id)) {
        n->file = file;
        n->subpath = subpath.isEmpty() || subpath.startsWith(u'#') ? subpath : u'#' + subpath;
    }
    return c;
}

Canvas setNodeUrl(Canvas c, const QString &id, const QString &url)
{
    if (Node *n = c.node(id))
        n->url = url;
    return c;
}

Canvas setGroupLabel(Canvas c, const QString &id, const QString &label)
{
    if (Node *n = c.node(id))
        n->label = label;
    return c;
}

Canvas setGroupBackground(Canvas c, const QString &id, const QString &path, const QString &style)
{
    if (Node *n = c.node(id)) {
        n->background = path;
        n->backgroundStyle = path.isEmpty()                                                 ? QString()
                             : style == u"cover" || style == u"ratio" || style == u"repeat" ? style
                                                                                            : QString();
    }
    return c;
}

Canvas setColor(Canvas c, const QSet<QString> &nodeIds, const QSet<QString> &edgeIds, const QString &color)
{
    const QString normalized = normalizeColor(color);
    if (!color.isEmpty() && normalized.isEmpty())
        return c;
    for (Node &n : c.nodes) {
        if (nodeIds.contains(n.id))
            n.color = normalized;
    }
    for (Edge &e : c.edges) {
        if (edgeIds.contains(e.id))
            e.color = normalized;
    }
    return c;
}

Canvas setEdgeLabel(Canvas c, const QString &id, const QString &label)
{
    const qsizetype i = c.indexOfEdge(id);
    if (i >= 0)
        c.edges[i].label = label;
    return c;
}

Canvas setEdgeEnds(Canvas c, const QSet<QString> &ids, bool arrowAtStart, bool arrowAtEnd)
{
    for (Edge &e : c.edges) {
        if (!ids.contains(e.id))
            continue;
        // A default stays unwritten: the spec's (none at the start, an arrow at the end).
        if (e.arrowAtStart() != arrowAtStart)
            e.fromEnd = arrowAtStart ? u"arrow"_s : (e.fromEnd.isEmpty() ? QString() : u"none"_s);
        if (e.arrowAtEnd() != arrowAtEnd)
            e.toEnd = arrowAtEnd ? (e.toEnd.isEmpty() ? QString() : u"arrow"_s) : u"none"_s;
    }
    return c;
}

Canvas setEdgeSides(Canvas c, const QString &id, const QString &fromSide, const QString &toSide)
{
    const qsizetype i = c.indexOfEdge(id);
    if (i < 0)
        return c;
    Edge &e = c.edges[i];
    e.fromSide = sideFrom(fromSide) ? fromSide : QString();
    e.toSide = sideFrom(toSide) ? toSide : QString();
    return c;
}

Canvas reverseEdges(Canvas c, const QSet<QString> &ids)
{
    for (Edge &e : c.edges) {
        if (!ids.contains(e.id))
            continue;
        // The ends keep their arrowheads: an arrow now points the other way.
        std::swap(e.fromNode, e.toNode);
        std::swap(e.fromSide, e.toSide);
    }
    return c;
}

Canvas duplicate(Canvas c, const QSet<QString> &ids, const QPointF &offset, QStringList *newIds, const IdSource &ids_)
{
    QSet<QString> taken;
    QHash<QString, QString> renamed;
    QList<Node> copies;
    for (const Node &n : std::as_const(c.nodes)) {
        if (!ids.contains(n.id))
            continue;
        Node copy = n;
        copy.origin.reset();
        copy.id = freshId(c, ids_, taken);
        copy.x = whole(n.x + offset.x());
        copy.y = whole(n.y + offset.y());
        renamed.insert(n.id, copy.id);
        copies.append(std::move(copy));
    }
    QList<Edge> edges;
    for (const Edge &e : std::as_const(c.edges)) {
        if (!renamed.contains(e.fromNode) || !renamed.contains(e.toNode))
            continue;
        Edge copy = e;
        copy.origin.reset();
        copy.id = freshId(c, ids_, taken);
        copy.fromNode = renamed.value(e.fromNode);
        copy.toNode = renamed.value(e.toNode);
        edges.append(std::move(copy));
    }
    if (newIds) {
        newIds->clear();
        for (const Node &n : std::as_const(copies))
            newIds->append(n.id);
    }
    c.nodes.append(copies);
    c.edges.append(edges);
    return c;
}

Canvas paste(Canvas c, const Canvas &fragment, const QPointF &at, QStringList *newIds, const IdSource &ids)
{
    const QRectF b = bounds(fragment);
    const QPointF offset = b.isNull() ? QPointF() : at - b.topLeft();
    Canvas staged = c;
    for (const Node &n : fragment.nodes) {
        Node copy = n;
        copy.origin.reset();
        staged.nodes.append(copy);
    }
    QSet<QString> all;
    for (const Node &n : fragment.nodes)
        all.insert(n.id);
    // Copies of the fragment's nodes: new ids, the fragment's edges between them.
    QSet<QString> taken;
    QHash<QString, QString> renamed;
    for (const Node &n : fragment.nodes) {
        if (renamed.contains(n.id))
            continue;
        Node copy = n;
        copy.origin.reset();
        copy.id = freshId(staged, ids, taken);
        copy.x = whole(n.x + offset.x());
        copy.y = whole(n.y + offset.y());
        renamed.insert(n.id, copy.id);
        c.nodes.append(copy);
        if (newIds)
            newIds->append(copy.id);
    }
    for (const Edge &e : fragment.edges) {
        if (!renamed.contains(e.fromNode) || !renamed.contains(e.toNode))
            continue;
        Edge copy = e;
        copy.origin.reset();
        copy.id = freshId(staged, ids, taken);
        copy.fromNode = renamed.value(e.fromNode);
        copy.toNode = renamed.value(e.toNode);
        c.edges.append(copy);
    }
    return c;
}

Canvas group(Canvas c, const QSet<QString> &ids, const QString &label, QString *groupId, const IdSource &idSource)
{
    const QRectF b = bounds(c, ids);
    if (b.isNull())
        return c;
    qsizetype lowest = c.nodes.size();
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        if (ids.contains(c.nodes.at(i).id)) {
            lowest = i;
            break;
        }
    }
    QSet<QString> taken;
    Node g = makeNode(GroupType, b.adjusted(-GroupPadding, -GroupPadding, GroupPadding, GroupPadding),
                      [&] { return freshId(c, idSource, taken); });
    g.label = label;
    if (groupId)
        *groupId = g.id;
    c.nodes.insert(lowest, std::move(g));
    return c;
}

Canvas ungroup(Canvas c, const QSet<QString> &groupIds)
{
    QSet<QString> groups;
    for (const Node &n : std::as_const(c.nodes)) {
        if (n.isGroup() && groupIds.contains(n.id))
            groups.insert(n.id);
    }
    return removeItems(std::move(c), groups, {});
}

Canvas align(Canvas c, const QSet<QString> &ids, Align how)
{
    const QRectF b = bounds(c, ids);
    if (b.isNull())
        return c;
    for (Node &n : c.nodes) {
        if (!ids.contains(n.id))
            continue;
        const QRectF r = nodeRect(n);
        switch (how) {
        case Align::Left: n.x = whole(b.left()); break;
        case Align::HCenter: n.x = whole(b.center().x() - r.width() / 2); break;
        case Align::Right: n.x = whole(b.right() - r.width()); break;
        case Align::Top: n.y = whole(b.top()); break;
        case Align::VCenter: n.y = whole(b.center().y() - r.height() / 2); break;
        case Align::Bottom: n.y = whole(b.bottom() - r.height()); break;
        }
    }
    return c;
}

Canvas distribute(Canvas c, const QSet<QString> &ids, bool horizontal)
{
    QList<qsizetype> picked;
    for (qsizetype i = 0; i < c.nodes.size(); ++i) {
        if (ids.contains(c.nodes.at(i).id))
            picked.append(i);
    }
    if (picked.size() < 3)
        return c;
    const auto start = [&](qsizetype i) { return horizontal ? c.nodes.at(i).x : c.nodes.at(i).y; };
    const auto extent = [&](qsizetype i) {
        const QRectF r = nodeRect(c.nodes.at(i));
        return horizontal ? r.width() : r.height();
    };
    std::ranges::stable_sort(picked, {}, start);
    const double first = start(picked.constFirst());
    const double last = start(picked.constLast()) + extent(picked.constLast());
    double total = 0;
    for (const qsizetype i : std::as_const(picked))
        total += extent(i);
    const double gap = (last - first - total) / double(picked.size() - 1);
    double at = first;
    for (const qsizetype i : std::as_const(picked)) {
        Node &n = c.nodes[i];
        (horizontal ? n.x : n.y) = whole(at);
        at += extent(i) + gap;
    }
    return c;
}

Canvas reorder(Canvas c, const QSet<QString> &ids, Order order)
{
    QList<Node> moved;
    QList<Node> rest;
    for (const Node &n : std::as_const(c.nodes))
        (ids.contains(n.id) ? moved : rest).append(n);
    if (moved.isEmpty())
        return c;
    switch (order) {
    case Order::Front: c.nodes = rest + moved; break;
    case Order::Back: c.nodes = moved + rest; break;
    case Order::Forward:
    case Order::Backward: {
        // One step: each moved node swaps with its neighbour that is not moved.
        QList<Node> nodes = c.nodes;
        if (order == Order::Forward) {
            for (qsizetype i = nodes.size() - 2; i >= 0; --i) {
                if (ids.contains(nodes.at(i).id) && !ids.contains(nodes.at(i + 1).id))
                    nodes.swapItemsAt(i, i + 1);
            }
        } else {
            for (qsizetype i = 1; i < nodes.size(); ++i) {
                if (ids.contains(nodes.at(i).id) && !ids.contains(nodes.at(i - 1).id))
                    nodes.swapItemsAt(i, i - 1);
            }
        }
        c.nodes = nodes;
        break;
    }
    }
    return c;
}

} // namespace brulion::canvas
