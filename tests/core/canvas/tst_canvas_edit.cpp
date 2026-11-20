// Edits of a canvas (canvas/edit.h) and its geometry (canvas/geometry.h): adding, connecting,
// removing, moving, resizing, colours, edge ends, duplicating, pasting, groups, alignment,
// z-order; edges' curves, sides and arrowheads, hit tests, groups' contents, preset colours.
#include "canvas/edit.h"
#include "canvas/geometry.h"
#include "fixtures.h"

#include <QTest>

using namespace brulion;
using namespace brulion::canvas;
using namespace Qt::StringLiterals;

namespace {

Canvas read(const QString &text)
{
    ReadResult r = readCanvas(text);
    if (!r.canvas)
        qFatal("not a canvas");
    return *r.canvas;
}

/// Ids "id1", "id2", … for a test.
IdSource counter(const QString &prefix = u"id"_s)
{
    auto n = std::make_shared<int>(0);
    return [n, prefix] { return prefix + QString::number(++*n); };
}

Canvas threeCards()
{
    Canvas c;
    const IdSource ids = counter(u"n"_s);
    c = addNode(c, makeTextNode(u"Pierwsza"_s, QRectF(0, 0, 100, 50), ids));
    c = addNode(c, makeTextNode(u"Druga"_s, QRectF(300, 40, 120, 80), ids));
    c = addNode(c, makeTextNode(u"Trzecia"_s, QRectF(100, 300, 80, 60), ids));
    return c;
}

QStringList nodeIds(const Canvas &c)
{
    QStringList out;
    for (const Node &n : c.nodes)
        out.append(n.id);
    return out;
}

} // namespace

class TestCanvasEdit : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void newIds()
    {
        const QString a = newId();
        QCOMPARE(a.size(), 16);
        QVERIFY(std::ranges::all_of(a, [](QChar c) { return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f'); }));
        QVERIFY(newId() != a);
    }

    void newNodes()
    {
        const Node text = makeTextNode(u"Ćma"_s, QRectF(1.4, 2.6, 249.5, 30), counter());
        QCOMPARE(text.id, u"id1"_s);
        QCOMPARE(text.type, u"text"_s);
        QCOMPARE(text.x, 1.0);
        QCOMPARE(text.y, 3.0);
        QCOMPARE(text.width, 250.0);
        QCOMPARE(text.height, MinNodeSize); // at least the smallest card
        const Node file = makeFileNode(u"Dom.md"_s, u"Ogród"_s, QRectF(0, 0, 400, 400));
        QCOMPARE(file.subpath, u"#Ogród"_s);
        QCOMPARE(defaultSize(FileType, u"Dom.md"), QSizeF(400, 400));
        QCOMPARE(defaultSize(FileType, u"zdjęcie.PNG"), QSizeF(400, 300));
        QCOMPARE(defaultSize(FileType, u"plik.zip"), QSizeF(400, 100));
        QCOMPARE(defaultSize(TextType), QSizeF(250, 60));
        QCOMPARE(defaultSize(LinkType), QSizeF(400, 200));
    }

    void connectAndRemove()
    {
        Canvas c = threeCards();
        QString edge;
        c = connectNodes(c, u"n1"_s, u"right"_s, u"n2"_s, u"left"_s, &edge, counter(u"e"_s));
        QCOMPARE(edge, u"e1"_s);
        c = connectNodes(c, u"n2"_s, u"diagonal"_s, u"n3"_s, QString(), nullptr, counter(u"f"_s));
        QCOMPARE(c.edges.size(), 2);
        QCOMPARE(c.edges.at(1).fromSide, QString()); // an unknown side: chosen when drawn
        // No edge to a node that does not exist, none from a node to itself.
        QCOMPARE(connectNodes(c, u"n1"_s, {}, u"x"_s, {}).edges.size(), 2);
        QCOMPARE(connectNodes(c, u"n1"_s, {}, u"n1"_s, {}).edges.size(), 2);
        // Removing a node takes its edges.
        const Canvas without = removeItems(c, {u"n2"_s}, {});
        QCOMPARE(nodeIds(without), (QStringList{u"n1"_s, u"n3"_s}));
        QVERIFY(without.edges.isEmpty());
        const Canvas oneEdge = removeItems(c, {}, {u"e1"_s});
        QCOMPARE(oneEdge.edges.size(), 1);
        QCOMPARE(oneEdge.nodes.size(), 3);
    }

    void moveAndResize()
    {
        Canvas c = threeCards();
        c = moveNodes(c, {u"n1"_s, u"n3"_s}, {10.6, -0.4});
        QCOMPARE(c.node(u"n1")->x, 11.0);
        QCOMPARE(c.node(u"n1")->y, 0.0);
        QCOMPARE(c.node(u"n2")->x, 300.0);
        QCOMPARE(c.node(u"n3")->x, 111.0);
        c = setNodeRect(c, u"n2"_s, QRectF(500, 500, -200, 10)); // normalised, at least MinNodeSize
        QCOMPARE(c.node(u"n2")->x, 300.0);
        QCOMPARE(c.node(u"n2")->width, 200.0);
        QCOMPARE(c.node(u"n2")->height, MinNodeSize);
        // Far beyond reach: clamped.
        c = moveNodes(c, {u"n1"_s}, {1e300, 0});
        QCOMPARE(c.node(u"n1")->x, CoordinateLimit);
    }

    void colours_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<QString>("stored");
        QTest::newRow("preset") << u"3"_s << u"3"_s;
        QTest::newRow("hex") << u"#FF8800"_s << u"#ff8800"_s;
        QTest::newRow("short hex") << u"#AbC"_s << u"#aabbcc"_s;
        QTest::newRow("none") << QString() << QString();
        QTest::newRow("css name") << u"red"_s << u"keep"_s;
        QTest::newRow("css injection") << u"#fff;background:url(x)"_s << u"keep"_s;
        QTest::newRow("preset 7") << u"7"_s << u"keep"_s;
    }
    void colours()
    {
        QFETCH(QString, color);
        QFETCH(QString, stored);
        Canvas c = threeCards();
        c = setColor(c, {u"n1"_s}, {}, u"2"_s);
        c = setColor(c, {u"n1"_s}, {}, color);
        QCOMPARE(c.node(u"n1")->color, stored == u"keep" ? u"2"_s : stored);
    }

    void presetColours()
    {
        QCOMPARE(colorHex(u"1", false), u"#e93147"_s);
        QCOMPARE(colorHex(u"1", true), u"#fb464c"_s);
        QCOMPARE(colorHex(u"6", false), u"#7852ee"_s);
        QCOMPARE(colorHex(u"#ABCDEF", false), u"#abcdef"_s);
        QCOMPARE(colorHex(u"rgb(0,0,0)", false), QString());
        QCOMPARE(colorHex(u"", false), QString());
        QCOMPARE(presetIndex(u"4"), 4);
        QCOMPARE(presetIndex(u"#123456"), 0);
    }

    void edgeEnds()
    {
        Canvas c = threeCards();
        c = connectNodes(c, u"n1"_s, {}, u"n2"_s, {}, nullptr, counter(u"e"_s));
        QVERIFY(!c.edges.at(0).arrowAtStart());
        QVERIFY(c.edges.at(0).arrowAtEnd());
        c = setEdgeEnds(c, {u"e1"_s}, true, true);
        QCOMPARE(c.edges.at(0).fromEnd, u"arrow"_s);
        QCOMPARE(c.edges.at(0).toEnd, QString()); // the spec's default stays unwritten
        c = setEdgeEnds(c, {u"e1"_s}, false, false);
        QCOMPARE(c.edges.at(0).toEnd, u"none"_s);
        QVERIFY(!c.edges.at(0).arrowAtEnd());
        QVERIFY(!c.edges.at(0).arrowAtStart());
        c = setEdgeEnds(c, {u"e1"_s}, false, true);
        QCOMPARE(c.edges.at(0).toEnd, u"arrow"_s); // written once it was named
        c = reverseEdges(c, {u"e1"_s});
        QCOMPARE(c.edges.at(0).fromNode, u"n2"_s);
        QCOMPARE(c.edges.at(0).toNode, u"n1"_s);
        c = setEdgeLabel(c, u"e1"_s, u"zależy od"_s);
        QCOMPARE(c.edges.at(0).label, u"zależy od"_s);
        c = setEdgeSides(c, u"e1"_s, u"bottom"_s, u"nowhere"_s);
        QCOMPARE(c.edges.at(0).fromSide, u"bottom"_s);
        QCOMPARE(c.edges.at(0).toSide, QString());
    }

    void duplicateAndPaste()
    {
        Canvas c = threeCards();
        c = connectNodes(c, u"n1"_s, u"right"_s, u"n2"_s, u"left"_s, nullptr, counter(u"e"_s));
        c = connectNodes(c, u"n2"_s, {}, u"n3"_s, {}, nullptr, counter(u"f"_s));
        QStringList copies;
        const Canvas dup = duplicate(c, {u"n1"_s, u"n2"_s}, {30, 30}, &copies, counter(u"d"_s));
        QCOMPARE(dup.nodes.size(), 5);
        QCOMPARE(copies, (QStringList{u"d1"_s, u"d2"_s}));
        QCOMPARE(dup.node(u"d1")->x, 30.0);
        QCOMPARE(dup.node(u"d1")->text, u"Pierwsza"_s);
        QVERIFY(!dup.node(u"d1")->origin);
        // Only the edge between the copies is copied.
        QCOMPARE(dup.edges.size(), 3);
        QCOMPARE(dup.edges.at(2).fromNode, u"d1"_s);
        QCOMPARE(dup.edges.at(2).toNode, u"d2"_s);

        const std::optional<Canvas> fragment = parseFragment(fragmentText(c, {u"n2"_s, u"n3"_s}));
        QVERIFY(fragment);
        QStringList pasted;
        const Canvas p = paste(c, *fragment, {1000, 1000}, &pasted, counter(u"p"_s));
        QCOMPARE(pasted.size(), 2);
        // The fragment's top-left at the point given.
        const QRectF b = bounds(p, QSet<QString>(pasted.cbegin(), pasted.cend()));
        QCOMPARE(b.topLeft(), QPointF(1000, 1000));
        QCOMPARE(p.edges.size(), 3);
        QCOMPARE(p.edges.constLast().fromNode, pasted.at(0));
        // A paste of the canvas's own nodes again: new ids, never the old ones.
        QVERIFY(!pasted.contains(u"n2"_s));
    }

    void groups()
    {
        Canvas c = threeCards();
        QString g;
        c = group(c, {u"n2"_s, u"n3"_s}, u"Etap 1"_s, &g, counter(u"g"_s));
        QCOMPARE(g, u"g1"_s);
        // Below the lowest of its members.
        QCOMPARE(nodeIds(c), (QStringList{u"n1"_s, u"g1"_s, u"n2"_s, u"n3"_s}));
        const Node *frame = c.node(g);
        QVERIFY(frame->isGroup());
        QCOMPARE(frame->label, u"Etap 1"_s);
        QCOMPARE(nodeRect(*frame), QRectF(100 - GroupPadding, 40 - GroupPadding, 320 + 2 * GroupPadding, 320 + 2 * GroupPadding));
        QCOMPARE(groupContents(c, g), (QSet<QString>{u"n2"_s, u"n3"_s}));
        QCOMPARE(withGroupContents(c, {g}), (QSet<QString>{g, u"n2"_s, u"n3"_s}));
        // A group inside a group: the outer one holds both.
        QString outer;
        c = group(c, {g, u"n1"_s}, QString(), &outer, counter(u"o"_s));
        QCOMPARE(groupContents(c, outer), (QSet<QString>{g, u"n1"_s, u"n2"_s, u"n3"_s}));
        c = ungroup(c, {outer, u"n1"_s}); // a card is no group: stays
        QCOMPARE(nodeIds(c), (QStringList{u"n1"_s, u"g1"_s, u"n2"_s, u"n3"_s}));
        c = setGroupBackground(c, g, u"assets/tło.png"_s, u"bogus"_s);
        QCOMPARE(c.node(g)->background, u"assets/tło.png"_s);
        QCOMPARE(c.node(g)->backgroundStyle, QString());
        c = setGroupBackground(c, g, u"assets/tło.png"_s, u"cover"_s);
        QCOMPARE(c.node(g)->backgroundStyle, u"cover"_s);
    }

    void alignAndDistribute()
    {
        const Canvas c = threeCards();
        const QSet<QString> all{u"n1"_s, u"n2"_s, u"n3"_s};
        const Canvas left = align(c, all, Align::Left);
        QCOMPARE(left.node(u"n2")->x, 0.0);
        QCOMPARE(left.node(u"n3")->x, 0.0);
        const Canvas right = align(c, all, Align::Right);
        QCOMPARE(right.node(u"n1")->x, 320.0); // the right edge 420 minus its width
        const Canvas middle = align(c, all, Align::VCenter);
        QCOMPARE(middle.node(u"n1")->y + 25, middle.node(u"n3")->y + 30);
        // Three in a row, spaced evenly between the first and the last.
        Canvas row;
        const IdSource ids = counter();
        row = addNode(row, makeTextNode(u"a"_s, QRectF(0, 0, 100, 50), ids));
        row = addNode(row, makeTextNode(u"b"_s, QRectF(130, 0, 100, 50), ids));
        row = addNode(row, makeTextNode(u"c"_s, QRectF(500, 0, 100, 50), ids));
        const Canvas spaced = distribute(row, {u"id1"_s, u"id2"_s, u"id3"_s}, true);
        QCOMPARE(spaced.node(u"id2")->x, 250.0);
        QCOMPARE(spaced.node(u"id3")->x, 500.0);
        // Two nodes: nothing to distribute.
        QCOMPARE(distribute(row, {u"id1"_s, u"id2"_s}, true).node(u"id2")->x, 130.0);
    }

    void zOrder()
    {
        const Canvas c = threeCards();
        QCOMPARE(nodeIds(reorder(c, {u"n1"_s}, Order::Front)), (QStringList{u"n2"_s, u"n3"_s, u"n1"_s}));
        QCOMPARE(nodeIds(reorder(c, {u"n3"_s}, Order::Back)), (QStringList{u"n3"_s, u"n1"_s, u"n2"_s}));
        QCOMPARE(nodeIds(reorder(c, {u"n1"_s}, Order::Forward)), (QStringList{u"n2"_s, u"n1"_s, u"n3"_s}));
        QCOMPARE(nodeIds(reorder(c, {u"n3"_s}, Order::Backward)), (QStringList{u"n1"_s, u"n3"_s, u"n2"_s}));
        QCOMPARE(nodeIds(reorder(c, {u"n2"_s, u"n3"_s}, Order::Forward)), (QStringList{u"n1"_s, u"n2"_s, u"n3"_s}));
    }

    void untouchedKeepOrigin()
    {
        const Canvas c = read(canvasfixtures::obsidian());
        const Canvas moved = moveNodes(c, {u"0f1e2d3c4b5a6978"_s}, {5, 5});
        qsizetype changed = 0;
        for (const Node &n : moved.nodes)
            changed += n.changed() ? 1 : 0;
        QCOMPARE(changed, 1);
        QVERIFY(std::ranges::none_of(moved.edges, &Edge::changed));
    }

    void edgeCurves()
    {
        const QRectF a(0, 0, 100, 100);
        const QRectF b(400, 0, 100, 100);
        Edge e;
        // No sides: the facing ones.
        EdgeShape s = edgeShape(e, a, b);
        QVERIFY(s.valid);
        QCOMPARE(s.fromSide, Side::Right);
        QCOMPARE(s.toSide, Side::Left);
        QVERIFY(s.startArrow.isEmpty());
        QCOMPARE(s.endArrow.size(), 3);
        QCOMPARE(s.endArrow.at(0), QPointF(400, 50)); // the tip on the side's middle
        QVERIFY(s.end.x() < 400); // the line ends inside the arrowhead
        QCOMPARE(s.start, QPointF(100, 50));
        // Below: bottom to top.
        s = edgeShape(e, a, QRectF(0, 500, 100, 100));
        QCOMPARE(s.fromSide, Side::Bottom);
        QCOMPARE(s.toSide, Side::Top);
        // Named sides win; arrows at both ends.
        e.fromSide = u"top"_s;
        e.toSide = u"top"_s;
        e.fromEnd = u"arrow"_s;
        s = edgeShape(e, a, b);
        QCOMPARE(s.fromSide, Side::Top);
        QCOMPARE(s.startArrow.size(), 3);
        QCOMPARE(s.startArrow.at(0), QPointF(50, 0));
        QVERIFY(s.c1.y() < 0); // leaves upwards
        // The label in the middle of the curve.
        QCOMPARE(s.labelPoint, bezierPoint(s, 0.5));
        QVERIFY(svgPath(s).startsWith(u"M 50.0 "_s));
        // Distance: on the curve 0, far away large.
        QVERIFY(distanceTo(s, bezierPoint(s, 0.3)) < 1);
        QVERIFY(distanceTo(s, {250, 400}) > 100);
    }

    void hitTests()
    {
        Canvas c = threeCards();
        QString g;
        c = group(c, {u"n2"_s}, u"Ramka"_s, &g, counter(u"g"_s));
        c = connectNodes(c, u"n1"_s, u"right"_s, u"n2"_s, u"left"_s, nullptr, counter(u"e"_s));
        QCOMPARE(c.nodes.at(nodeAt(c, {10, 10})).id, u"n1"_s);
        // A card over its group wins; the group where only the group is.
        QCOMPARE(c.nodes.at(nodeAt(c, {310, 50})).id, u"n2"_s);
        QCOMPARE(c.nodes.at(nodeAt(c, {275, 15})).id, g);
        // By its frame only: inside, away from the frame, nothing.
        QCOMPARE(nodeAt(c, {430, 130}, true, 4), -1);
        QCOMPARE(c.nodes.at(nodeAt(c, {272, 130}, true, 4)).id, g);
        // On the label above the frame.
        QCOMPARE(c.nodes.at(nodeAt(c, {280, 5})).id, g);
        QCOMPARE(nodeAt(c, {-500, -500}), -1);
        // The edge between them.
        const EdgeShape s = edgeShape(c, c.edges.at(0));
        QCOMPARE(edgeAt(c, bezierPoint(s, 0.5), 6), 0);
        QCOMPARE(edgeAt(c, {150, 500}, 6), -1);
        // A rubber band: intersecting or inside.
        QCOMPARE(nodesIn(c, QRectF(-10, -10, 60, 60), false), (QSet<QString>{u"n1"_s}));
        QCOMPARE(nodesIn(c, QRectF(-10, -10, 60, 60), true), QSet<QString>());
        QCOMPARE(nodesIn(c, QRectF(-10, -10, 200, 200), true), (QSet<QString>{u"n1"_s}));
        QCOMPARE(nearestSide(QRectF(0, 0, 100, 100), {98, 40}), Side::Right);
        QCOMPARE(nearestSide(QRectF(0, 0, 100, 100), {50, 2}), Side::Top);
    }

    void looseEdge()
    {
        const EdgeShape s = looseShape(QRectF(0, 0, 100, 100), Side::Right, {300, 300});
        QVERIFY(s.valid);
        QCOMPARE(s.start, QPointF(100, 50));
        QCOMPARE(s.endArrow.at(0), QPointF(300, 300));
    }

    void canvasBounds()
    {
        const Canvas c = threeCards();
        QCOMPARE(bounds(c), QRectF(0, 0, 420, 360));
        QCOMPARE(bounds(Canvas()), QRectF());
        QCOMPARE(bounds(c, {u"n3"_s}), QRectF(100, 300, 80, 60));
    }
};

QTEST_GUILESS_MAIN(TestCanvasEdit)
#include "tst_canvas_edit.moc"
