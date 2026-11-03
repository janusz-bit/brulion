// The flowchart layout (diagram/flowchart_p.h, diagram/layered_p.h): nodes never overlap, links
// run along the direction (TB, BT, LR, RL) in acyclic charts, subgraphs hold their members and
// nothing else, links end on their shapes' outlines (under their heads), labels keep clear of
// nodes and of each other, everything is inside the scene, and the same chart is laid out the same
// way every time — on hand-written charts and on seeded random ones.
#include "diagram/diagram.h"
#include "diagram/flowchart_p.h"
#include "diagram/layered_p.h"

#include <QLineF>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using namespace brulion::diagram;
using namespace brulion::diagram::detail;

namespace {

constexpr qreal Eps = 0.5;

struct Laid {
    Flowchart chart;
    FlowLayout layout;
};

std::optional<Laid> lay(const QString &source)
{
    auto f = parseFlowchart(source);
    if (!f) {
        qWarning() << "parse" << int(f.error().code) << f.error().line << f.error().detail;
        return std::nullopt;
    }
    auto l = layoutFlowchart(*f, approximateMetrics());
    if (!l) {
        qWarning() << "layout" << int(l.error().code);
        return std::nullopt;
    }
    return Laid{*f, *l};
}

bool overlap(const QRectF &a, const QRectF &b)
{
    return a.adjusted(Eps, Eps, -Eps, -Eps).intersects(b.adjusted(Eps, Eps, -Eps, -Eps));
}

bool contains(const QRectF &outer, const QRectF &inner)
{
    return outer.adjusted(-Eps, -Eps, Eps, Eps).contains(inner);
}

qreal distanceToOutline(const QList<QPointF> &outline, const QPointF &p)
{
    qreal best = std::numeric_limits<qreal>::max();
    for (qsizetype i = 0; i < outline.size(); ++i) {
        const QPointF a = outline[i], b = outline[(i + 1) % outline.size()];
        const QPointF ab = b - a;
        const qreal len2 = QPointF::dotProduct(ab, ab);
        const qreal t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0) : 0;
        const QPointF q = a + ab * t;
        best = std::min(best, std::hypot(p.x() - q.x(), p.y() - q.y()));
    }
    return best;
}

/// The subgraphs a node is in (innermost first).
QList<int> clustersOf(const Flowchart &f, int node)
{
    QList<int> out;
    for (int c = f.nodes[node].cluster; c >= 0; c = f.clusters[c].parent)
        out.append(c);
    return out;
}

bool encloses(const Flowchart &f, int outer, int inner)
{
    for (int c = inner; c >= 0; c = f.clusters[c].parent)
        if (c == outer)
            return true;
    return false;
}

/// Checks every geometric property of a laid-out chart; "" when all hold.
QString check(const Laid &l)
{
    const Flowchart &f = l.chart;
    const FlowLayout &g = l.layout;
    const QRectF scene(QPointF(0, 0), g.size);
    for (qsizetype a = 0; a < f.nodes.size(); ++a) {
        if (!contains(scene, g.nodes[a]))
            return u"node %1 outside the scene"_s.arg(f.nodes[a].id);
        for (qsizetype b = a + 1; b < f.nodes.size(); ++b)
            if (overlap(g.nodes[a], g.nodes[b]))
                return u"nodes %1 and %2 overlap"_s.arg(f.nodes[a].id, f.nodes[b].id);
    }
    for (qsizetype c = 0; c < f.clusters.size(); ++c) {
        const QRectF r = g.clusters[c];
        if (!contains(scene, r))
            return u"subgraph %1 outside the scene"_s.arg(f.clusters[c].id);
        for (qsizetype n = 0; n < f.nodes.size(); ++n) {
            const bool member = clustersOf(f, int(n)).contains(int(c));
            if (member && !contains(r, g.nodes[n]))
                return u"subgraph %1 does not hold %2"_s.arg(f.clusters[c].id, f.nodes[n].id);
            if (!member && overlap(r, g.nodes[n]))
                return u"subgraph %1 overlaps %2"_s.arg(f.clusters[c].id, f.nodes[n].id);
        }
        for (qsizetype d = 0; d < f.clusters.size(); ++d) {
            if (d == c)
                continue;
            if (encloses(f, int(c), int(d))) {
                if (!contains(r, g.clusters[d]))
                    return u"subgraph %1 does not hold %2"_s.arg(f.clusters[c].id, f.clusters[d].id);
            } else if (!encloses(f, int(d), int(c)) && overlap(r, g.clusters[d])) {
                return u"subgraphs %1 and %2 overlap"_s.arg(f.clusters[c].id, f.clusters[d].id);
            }
        }
    }
    QList<QRectF> labels;
    for (qsizetype e = 0; e < f.edges.size(); ++e) {
        const FlowEdge &edge = f.edges[e];
        const FlowLayout::Edge &ge = g.edges[e];
        if (!ge.drawn)
            continue;
        const auto outline = [&](const FlowEnd &end) {
            if (end.cluster) {
                const QRectF r = g.clusters[end.index];
                return QList<QPointF>{r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
            }
            return shapeOutline(f.nodes[end.index].shape, g.nodes[end.index]);
        };
        if (distanceToOutline(outline(edge.from), ge.start) > 1.0)
            return u"link %1 does not start on its shape"_s.arg(e);
        if (distanceToOutline(outline(edge.to), ge.end) > 1.0)
            return u"link %1 does not end on its shape"_s.arg(e);
        // The curve stops under the heads, the path's ends are the lines' ends.
        if (std::abs(std::hypot(ge.end.x() - ge.lineEnd.x(), ge.end.y() - ge.lineEnd.y())
                     - headInset(edge.endHead, ge.width))
            > 0.01)
            return u"link %1 does not stop under its head"_s.arg(e);
        if (ge.path.front().p != ge.lineStart || ge.path.last().p != ge.lineEnd)
            return u"link %1's path is not its line"_s.arg(e);
        for (const PathOp &op : ge.path)
            if (op.type != PathOp::Type::Close && !scene.adjusted(-Eps, -Eps, Eps, Eps).contains(op.p))
                return u"link %1 leaves the scene"_s.arg(e);
        if (ge.label.isEmpty())
            continue;
        if (!contains(scene, ge.label))
            return u"label of link %1 outside the scene"_s.arg(e);
        for (qsizetype n = 0; n < f.nodes.size(); ++n)
            if (overlap(ge.label, g.nodes[n]))
                return u"label of link %1 overlaps %2"_s.arg(QString::number(e), f.nodes[n].id);
        for (const QRectF &other : std::as_const(labels))
            if (overlap(ge.label, other))
                return u"label of link %1 overlaps another"_s.arg(e);
        labels.append(ge.label);
    }
    return {};
}

/// Do the links of an acyclic chart run along `dir` (each target after its source)?
QString checkDirection(const Laid &l, Direction dir)
{
    for (const FlowEdge &e : l.chart.edges) {
        if (e.from.cluster || e.to.cluster || e.from.index == e.to.index)
            continue;
        const QRectF a = l.layout.nodes[e.from.index], b = l.layout.nodes[e.to.index];
        bool ok = false;
        switch (dir) {
        case Direction::TB: ok = b.top() >= a.bottom() - Eps; break;
        case Direction::BT: ok = b.bottom() <= a.top() + Eps; break;
        case Direction::LR: ok = b.left() >= a.right() - Eps; break;
        case Direction::RL: ok = b.right() <= a.left() + Eps; break;
        }
        if (!ok)
            return u"%1 → %2 runs against the direction"_s.arg(l.chart.nodes[e.from.index].id,
                                                               l.chart.nodes[e.to.index].id);
    }
    return {};
}

QString header(Direction d)
{
    switch (d) {
    case Direction::TB: return u"flowchart TB\n"_s;
    case Direction::BT: return u"flowchart BT\n"_s;
    case Direction::LR: return u"flowchart LR\n"_s;
    case Direction::RL: return u"flowchart RL\n"_s;
    }
    return {};
}

} // namespace

class TstDiagramLayout : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void properties_data()
    {
        QTest::addColumn<QString>("body");
        QTest::addColumn<bool>("acyclic");
        QTest::newRow("chain") << u"A-->B-->C-->D"_s << true;
        QTest::newRow("diamond") << u"A-->B & C\nB & C-->D"_s << true;
        QTest::newRow("labels") << u"A{Czy działa?}-->|Tak, oczywiście że działa|B[Świetnie]\nA-->|Nie|C[Napraw]\n"
                                   u"A-- jeszcze jedna długa etykieta -->C\nB-->D((Koniec))\nC-->D"_s
                                << true;
        QTest::newRow("long link") << u"A-->B-->C-->D-->E\nA-->|na skróty|E\nA-->E"_s << true;
        QTest::newRow("all shapes") << u"a[x]-->b(x)-->c([x])-->d[[x]]-->e[(x)]-->f((x))-->g(((x)))\n"
                                       u"g-->h>x]-->i{x}-->j{{x}}-->k[/x/]-->l[\\x\\]-->m[/x\\]-->n[\\x/]"_s
                                    << true;
        QTest::newRow("heads") << u"A--oB--xC<-->D o--o E x--x F==>G-.->H~~~I"_s << true;
        QTest::newRow("wide fan") << u"R-->a & b & c & d & e & f & g & h & i & j & k\na & b & c-->Z"_s << true;
        QTest::newRow("cycle") << u"A-->B-->C-->A\nC-->D-->B"_s << false;
        QTest::newRow("self loops") << u"A-->A\nA-->|jeszcze raz|A\nA-->B\nB-->B"_s << false;
        QTest::newRow("subgraphs") << u"c1-->a2\nsubgraph one [Pierwszy]\na1-->a2\nend\nsubgraph two\nb1-->b2\nend\n"
                                      u"subgraph three\nc1-->c2\nend\none --> two\nthree --> two\ntwo --> c2"_s
                                   << false;
        QTest::newRow("nested subgraphs") << u"X-->a\nsubgraph outer [Zewnętrzny z długim tytułem]\na-->b\n"
                                             u"subgraph mid\nb-->c\nsubgraph inner\nc-->d\nend\nend\nend\nd-->Y"_s
                                          << true;
        QTest::newRow("subgraph direction") << u"subgraph s\ndirection LR\na-->b-->c\nend\nsubgraph t\ndirection BT\n"
                                               u"d-->e\nend\nX-->s\ns-->t"_s
                                            << false;
        QTest::newRow("empty subgraph") << u"subgraph pusty [Nic tu nie ma]\nend\nA-->pusty\npusty-->B"_s << true;
        QTest::newRow("disconnected") << u"A-->B\nC\nD-->E-->F\nG"_s << true;
        QTest::newRow("parallel links") << u"A-->B\nA-->B\nA-.->B\nA==>|trzy|B"_s << true;
        QTest::newRow("multiline labels") << u"A[\"Pierwsza linia<br>druga<br>trzecia\"]-->|raz<br>dwa|B\n"
                                             u"B-->C[\"Bardzo długa etykieta węzła, która się zawija w kilka linii\"]"_s
                                          << true;
    }
    void properties()
    {
        QFETCH(QString, body);
        QFETCH(bool, acyclic);
        for (const Direction d : {Direction::TB, Direction::BT, Direction::LR, Direction::RL}) {
            const auto l = lay(header(d) + body);
            QVERIFY(l);
            const QString problem = check(*l);
            QVERIFY2(problem.isEmpty(), qPrintable(header(d) + problem));
            if (acyclic) {
                const QString wrong = checkDirection(*l, d);
                QVERIFY2(wrong.isEmpty(), qPrintable(header(d) + wrong));
            }
        }
    }

    void random()
    {
        // Seeded random acyclic charts (links from earlier to later nodes), some with subgraphs.
        QRandomGenerator rng(20261103);
        for (int round = 0; round < 40; ++round) {
            const int n = 4 + int(rng.bounded(30));
            const int links = int(rng.bounded(n * 2));
            const Direction dir = Direction(rng.bounded(4));
            QString src = header(dir);
            for (int i = 0; i < n; ++i)
                src += u"n"_s + QString::number(i) + u"[\""_s + QString(u"węzeł "_s).repeated(1 + int(rng.bounded(3)))
                       + QString::number(i) + u"\"]\n"_s;
            const bool clusters = round % 3 == 0;
            if (clusters) {
                // Two subgraphs of consecutive nodes.
                src += u"subgraph s1 [Pierwsza grupa]\n"_s;
                for (int i = 0; i < n / 3; ++i)
                    src += u"n"_s + QString::number(i) + u'\n';
                src += u"end\nsubgraph s2\n"_s;
                for (int i = n / 2; i < n / 2 + n / 4; ++i)
                    src += u"n"_s + QString::number(i) + u'\n';
                src += u"end\n"_s;
            }
            for (int k = 0; k < links; ++k) {
                const int a = int(rng.bounded(n - 1));
                const int b = a + 1 + int(rng.bounded(n - a - 1));
                src += u"n"_s + QString::number(a) + (rng.bounded(4) == 0 ? u"-->|etykieta|"_s : u"-->"_s) + u"n"_s
                       + QString::number(b) + u'\n';
            }
            const auto l = lay(src);
            QVERIFY(l);
            const QString problem = check(*l);
            QVERIFY2(problem.isEmpty(), qPrintable(src + u"\n→ "_s + problem));
            if (!clusters) {
                // (Links leaving a subgraph run from its box: only charts without them must be
                // strictly monotonic node to node.)
                const QString wrong = checkDirection(*l, dir);
                QVERIFY2(wrong.isEmpty(), qPrintable(src + u"\n→ "_s + wrong));
            }
        }
    }

    void sameEveryTime()
    {
        const QString src = u"graph LR\nA-->B & C & D\nB-->E\nC-->|x|E\nD-->F\nE-->F\nF-->A\nsubgraph s\nB\nC\nend"_s;
        const auto a = lay(src);
        QVERIFY(a);
        for (int i = 0; i < 3; ++i) {
            const auto b = lay(src);
            QCOMPARE(b->layout.nodes, a->layout.nodes);
            QCOMPARE(b->layout.clusters, a->layout.clusters);
            QCOMPARE(b->layout.size, a->layout.size);
        }
    }

    void centred()
    {
        // A parent over two children sits midway; a straight chain is straight.
        const auto l = lay(u"graph TD\nA-->B\nA-->C\nB-->D-->E"_s);
        QVERIFY(l);
        const auto cx = [&](int i) { return l->layout.nodes[i].center().x(); };
        QVERIFY(std::abs(cx(0) - (cx(1) + cx(2)) / 2) < 1);
        QVERIFY(std::abs(cx(1) - cx(3)) < 1);
        QVERIFY(std::abs(cx(3) - cx(4)) < 1);
    }

    void labelsOnTheirLinks()
    {
        const auto l = lay(u"graph TD\nA-->|eins|B\nA-->|zwei|C\nB-->|drei|C"_s);
        QVERIFY(l);
        for (const FlowLayout::Edge &e : l->layout.edges) {
            QVERIFY(!e.label.isEmpty());
            // The curve passes through the label's centre (its bend there).
            bool through = false;
            for (const PathOp &op : e.path)
                through = through || QLineF(op.p, e.label.center()).length() < 0.01;
            QVERIFY(through);
        }
    }

    void selfLoops()
    {
        const auto l = lay(u"graph TD\nA-->A\nA-->|pętla|A"_s);
        QVERIFY(l);
        const QRectF a = l->layout.nodes[0];
        for (const FlowLayout::Edge &e : l->layout.edges) {
            QVERIFY(e.drawn);
            QVERIFY(e.start.x() >= a.right() - Eps);
            QVERIFY(e.end.x() >= a.right() - Eps);
            // The loop bulges out to the right, the second one further.
            QVERIFY(e.path.last().c1.x() > a.right() + 10);
        }
        QVERIFY(l->layout.edges[1].path.last().c1.x() > l->layout.edges[0].path.last().c1.x());
        QVERIFY(!l->layout.edges[1].label.isEmpty());
        QVERIFY(!overlap(l->layout.edges[1].label, a));
        QVERIFY(l->layout.size.width() >= l->layout.edges[1].label.right());
    }

    void invisibleAndImpossible()
    {
        const auto l = lay(u"graph TD\nA~~~B\nsubgraph s\nC\nend\nC-->s"_s);
        QVERIFY(l);
        QVERIFY(!l->layout.edges[0].drawn);
        QVERIFY(!l->layout.edges[1].drawn);
        // An invisible link still orders the ranks.
        QVERIFY(l->layout.nodes[1].top() > l->layout.nodes[0].bottom());
        const Result r = render(u"graph TD\nA~~~B"_s);
        for (const Item &it : r.scene.items)
            QVERIFY(it.stroke.role != Role::Line);
    }

    void outlines_data()
    {
        QTest::addColumn<int>("shape");
        for (int s = int(NodeShape::Rect); s <= int(NodeShape::TrapezoidAlt); ++s)
            QTest::newRow(qPrintable(QString::number(s))) << s;
    }
    void outlines()
    {
        QFETCH(int, shape);
        const QRectF r(10, 20, 120, 60);
        const QList<QPointF> o = shapeOutline(NodeShape(shape), r);
        QVERIFY(o.size() >= 4);
        for (const QPointF &p : o)
            QVERIFY(r.adjusted(-0.01, -0.01, 0.01, 0.01).contains(p));
        // From the centre out in every direction: a point on the outline, inside the bounds.
        for (int deg = 0; deg < 360; deg += 15) {
            const qreal a = deg * M_PI / 180;
            const QPointF far = r.center() + QPointF(std::cos(a), std::sin(a)) * 500;
            const QPointF p = outlineExit(o, r.center(), far);
            QVERIFY(p != r.center());
            QVERIFY(distanceToOutline(o, p) < 0.01);
        }
        // The size around a text holds the text.
        const QSizeF text(80, 18);
        const QSizeF s = shapeSize(NodeShape(shape), text, 14);
        QVERIFY(s.width() >= text.width() && s.height() >= text.height());
    }

    void layeredDirect()
    {
        // The generic layout: ranks follow the links, labels get the rank between.
        const QList<LayerNode> nodes(4, LayerNode{40, 20, 0});
        const QList<LayerEdge> edges{{0, 1, 1, {}}, {1, 2, 1, QSizeF(60, 16)}, {0, 3, 2, {}}, {2, 0, 1, {}}};
        const auto r = layered(nodes, edges);
        QVERIFY(r);
        QVERIFY(r->ranks[1] > r->ranks[0]);
        QVERIFY(r->ranks[2] > r->ranks[1]);
        QVERIFY(r->ranks[3] - r->ranks[0] >= 4);
        QVERIFY(r->labels[1].has_value());
        QVERIFY(!r->labels[0].has_value());
        QVERIFY(r->labels[1]->y() > r->centers[1].y() && r->labels[1]->y() < r->centers[2].y());
        // The reversed link's bends run from its source to its target.
        QVERIFY(!r->bends[3].isEmpty());
        QVERIFY(r->bends[3].front().y() > r->bends[3].back().y());
        LayerOptions small;
        small.maxNodes = 5;
        QVERIFY(!layered(nodes, {{0, 1, 10, {}}}, small));
        QVERIFY(layered({}, {}));
    }
};

QTEST_GUILESS_MAIN(TstDiagramLayout)
#include "tst_diagram_layout.moc"
