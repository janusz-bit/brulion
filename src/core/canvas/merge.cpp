#include "canvas/merge.h"

#include <QHash>
#include <QSet>

using namespace Qt::StringLiterals;

namespace brulion::canvas {

namespace {

template<typename Item>
QHash<QString, const Item *> byId(const QList<Item> &items)
{
    QHash<QString, const Item *> out;
    out.reserve(items.size());
    for (const Item &i : items)
        out.tryEmplace(i.id, &i);
    return out;
}

/// Field by field: ours where ours changed it since the base, else theirs.
Node mergeNode(const Node &base, const Node &ours, Node theirs)
{
    const auto pick = [](const auto &b, const auto &o, auto &t) {
        if (o != b)
            t = o;
    };
    pick(base.type, ours.type, theirs.type);
    pick(base.x, ours.x, theirs.x);
    pick(base.y, ours.y, theirs.y);
    pick(base.width, ours.width, theirs.width);
    pick(base.height, ours.height, theirs.height);
    pick(base.color, ours.color, theirs.color);
    pick(base.text, ours.text, theirs.text);
    pick(base.file, ours.file, theirs.file);
    pick(base.subpath, ours.subpath, theirs.subpath);
    pick(base.url, ours.url, theirs.url);
    pick(base.label, ours.label, theirs.label);
    pick(base.background, ours.background, theirs.background);
    pick(base.backgroundStyle, ours.backgroundStyle, theirs.backgroundStyle);
    return theirs;
}

Edge mergeEdge(const Edge &base, const Edge &ours, Edge theirs)
{
    const auto pick = [](const QString &b, const QString &o, QString &t) {
        if (o != b)
            t = o;
    };
    pick(base.fromNode, ours.fromNode, theirs.fromNode);
    pick(base.toNode, ours.toNode, theirs.toNode);
    pick(base.fromSide, ours.fromSide, theirs.fromSide);
    pick(base.toSide, ours.toSide, theirs.toSide);
    pick(base.fromEnd, ours.fromEnd, theirs.fromEnd);
    pick(base.toEnd, ours.toEnd, theirs.toEnd);
    pick(base.color, ours.color, theirs.color);
    pick(base.label, ours.label, theirs.label);
    return theirs;
}

/// theirs' items with ours' changes (`known`: a base), ours' new items on top.
template<typename Item, typename MergeOne>
QList<Item> mergeItems(const QList<Item> *base, const QList<Item> &ours, const QList<Item> &theirs,
                       const MergeOne &mergeOne)
{
    const QHash<QString, const Item *> was = base ? byId(*base) : QHash<QString, const Item *>();
    const QHash<QString, const Item *> mine = byId(ours);
    const QHash<QString, const Item *> other = byId(theirs);
    QList<Item> out;
    out.reserve(theirs.size() + ours.size());
    for (const Item &t : theirs) {
        const Item *o = mine.value(t.id);
        const Item *b = was.value(t.id);
        if (!o) {
            // Deleted here (it was in the base), or new there.
            if (base && b)
                continue;
            out.append(t);
            continue;
        }
        if (!base) {
            out.append(t); // a union: theirs where both have it
            continue;
        }
        if (!b) {
            // New on both sides under one id (a copy of the same paste): ours where it differs.
            out.append(o->sameAs(t) ? t : mergeOne(t, *o, t));
            continue;
        }
        out.append(mergeOne(*b, *o, t));
    }
    for (const Item &o : ours) {
        if (other.contains(o.id))
            continue;
        if (base && was.contains(o.id))
            continue; // deleted there: stays deleted
        out.append(o); // (as our file wrote it, when it did not change since)
    }
    return out;
}

} // namespace

Canvas merge(const Canvas *base, const Canvas &ours, const Canvas &theirs)
{
    Canvas out;
    out.layout = theirs.layout;
    out.nodes = mergeItems<Node>(base ? &base->nodes : nullptr, ours.nodes, theirs.nodes,
                                 [](const Node &b, const Node &o, const Node &t) { return mergeNode(b, o, t); });
    out.edges = mergeItems<Edge>(base ? &base->edges : nullptr, ours.edges, theirs.edges,
                                 [](const Edge &b, const Edge &o, const Edge &t) { return mergeEdge(b, o, t); });
    // A node the merge may have duplicated by id (new on both sides): the first kept.
    QSet<QString> seen;
    out.nodes.removeIf([&seen](const Node &n) {
        if (seen.contains(n.id))
            return true;
        seen.insert(n.id);
        return false;
    });
    // Edges to nodes no longer there go with them.
    out.edges.removeIf([&seen](const Edge &e) { return !seen.contains(e.fromNode) || !seen.contains(e.toNode); });
    return out;
}

MergeCounts mergeCounts(const Canvas &ours, const Canvas &theirs, const Canvas &merged)
{
    MergeCounts m;
    m.ours = int(ours.nodes.size() + ours.edges.size());
    m.theirs = int(theirs.nodes.size() + theirs.edges.size());
    m.merged = int(merged.nodes.size() + merged.edges.size());
    const auto count = [&m](const auto &a, const auto &b) {
        const auto other = byId(b);
        for (const auto &item : a) {
            const auto *t = other.value(item.id);
            if (t) {
                ++m.common;
                if (!item.sameAs(*t))
                    ++m.changed;
            } else {
                ++m.onlyOurs;
            }
        }
        const auto mine = byId(a);
        for (const auto &item : b) {
            if (!mine.contains(item.id))
                ++m.onlyTheirs;
        }
    };
    count(ours.nodes, theirs.nodes);
    count(ours.edges, theirs.edges);
    return m;
}

std::optional<QString> mergeCanvasFiles(QStringView base, QStringView local, QStringView remote)
{
    const ReadResult b = readCanvas(base);
    const ReadResult l = readCanvas(local);
    const ReadResult r = readCanvas(remote);
    if (!b.canvas || !l.canvas || !r.canvas)
        return std::nullopt;
    const Canvas merged = merge(&*b.canvas, *l.canvas, *r.canvas);
    QString text = writeCanvas(merged);
    // The merge must read back (it always should: a guard against writing a broken file).
    if (!readCanvas(text).canvas)
        return std::nullopt;
    return text;
}

} // namespace brulion::canvas
