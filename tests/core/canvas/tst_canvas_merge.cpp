// Two versions of a canvas merged (canvas/merge.h): against their base field by field, as a union
// without one, edges of removed nodes, the remote's layout and order, files that are no canvas.
#include "canvas/edit.h"
#include "canvas/merge.h"
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

QStringList ids(const Canvas &c)
{
    QStringList out;
    for (const Node &n : c.nodes)
        out.append(n.id);
    return out;
}

IdSource fixed(const QString &id)
{
    return [id] { return id; };
}

} // namespace

class TestCanvasMerge : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fieldByField()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        // Here: the note moved. There: its section card's text… and the note recoloured.
        const Canvas ours = moveNodes(base, {u"0f1e2d3c4b5a6978"_s}, {100, 0});
        Canvas theirs = setColor(base, {u"0f1e2d3c4b5a6978"_s}, {}, u"5"_s);
        theirs = setNodeText(theirs, u"a1b2c3d4e5f60718"_s, u"Zmienione gdzie indziej"_s);
        const Canvas m = merge(&base, ours, theirs);
        const Node *note = m.node(u"0f1e2d3c4b5a6978");
        QCOMPARE(note->x, base.node(u"0f1e2d3c4b5a6978")->x + 100);
        QCOMPARE(note->color, u"5"_s);
        QCOMPARE(m.node(u"a1b2c3d4e5f60718")->text, u"Zmienione gdzie indziej"_s);
        QCOMPARE(ids(m), ids(base));
    }

    void bothChangedOursWins()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        const Canvas ours = setNodeText(base, u"a1b2c3d4e5f60718"_s, u"Nasze"_s);
        const Canvas theirs = setNodeText(base, u"a1b2c3d4e5f60718"_s, u"Ich"_s);
        QCOMPARE(merge(&base, ours, theirs).node(u"a1b2c3d4e5f60718")->text, u"Nasze"_s);
    }

    void deletions()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        // Deleted here: gone (with its edges), even though changed there… the change of a card
        // deleted here does not bring it back.
        const Canvas ours = removeItems(base, {u"99aabbccddeeff00"_s}, {});
        const Canvas theirs = removeItems(base, {u"abcdefabcdefabcd"_s}, {u"e1e1e1e1e1e1e1e1"_s});
        const Canvas m = merge(&base, ours, theirs);
        QVERIFY(!m.node(u"99aabbccddeeff00"));
        QVERIFY(!m.node(u"abcdefabcdefabcd")); // deleted there: stays deleted
        QVERIFY(!m.edge(u"e3e3e3e3e3e3e3e3")); // its node deleted here
        QVERIFY(!m.edge(u"e1e1e1e1e1e1e1e1")); // deleted there
        QVERIFY(m.edge(u"e2e2e2e2e2e2e2e2"));
        QCOMPARE(m.nodes.size(), 4);
    }

    void additionsOnTop()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        const Canvas ours = addNode(base, makeTextNode(u"Tutaj"_s, QRectF(0, 600, 250, 60), fixed(u"ours1"_s)));
        Canvas theirs = addNode(base, makeTextNode(u"Tam"_s, QRectF(0, 700, 250, 60), fixed(u"theirs1"_s)));
        theirs = reorder(theirs, {u"abcdefabcdefabcd"_s}, Order::Back);
        const Canvas m = merge(&base, ours, theirs);
        // Theirs' order (their reorder too), ours' new card on top.
        QCOMPARE(ids(m).constFirst(), u"abcdefabcdefabcd"_s);
        QCOMPARE(ids(m).mid(ids(m).size() - 2), (QStringList{u"theirs1"_s, u"ours1"_s}));
        // An edge new here to a card new there… and one to a card deleted there: left out.
        Canvas ours2 =
                connectNodes(base, u"0f1e2d3c4b5a6978"_s, {}, u"abcdefabcdefabcd"_s, {}, nullptr, fixed(u"x1"_s));
        const Canvas theirs2 = removeItems(base, {u"abcdefabcdefabcd"_s}, {});
        QVERIFY(!merge(&base, ours2, theirs2).edge(u"x1"));
    }

    void unionWithoutBase()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        const Canvas ours = removeItems(base, {u"99aabbccddeeff00"_s}, {});
        Canvas theirs = setNodeText(base, u"a1b2c3d4e5f60718"_s, u"Ich"_s);
        theirs = addNode(theirs, makeTextNode(u"Nowa"_s, QRectF(0, 0, 250, 60), fixed(u"t1"_s)));
        const Canvas m = merge(nullptr, setNodeText(ours, u"a1b2c3d4e5f60718"_s, u"Nasze"_s), theirs);
        // Nothing deleted, theirs where both have it.
        QVERIFY(m.node(u"99aabbccddeeff00"));
        QVERIFY(m.node(u"t1"));
        QCOMPARE(m.node(u"a1b2c3d4e5f60718")->text, u"Ich"_s);
        const MergeCounts counts = mergeCounts(ours, theirs, m);
        QCOMPARE(counts.ours, 5 + 2); // the picture's edge went with it
        QCOMPARE(counts.theirs, 7 + 3);
        QCOMPARE(counts.onlyTheirs, 3);
        QCOMPARE(counts.onlyOurs, 0);
        QCOMPARE(counts.changed, 1);
        QCOMPARE(counts.merged, 10);
    }

    void sameIdNewOnBothSides()
    {
        const Canvas base = read(canvasfixtures::specSample());
        const Node mine = makeTextNode(u"Wklejone"_s, QRectF(0, 0, 250, 60), fixed(u"p1"_s));
        Node other = mine;
        other.x = 50;
        const Canvas m = merge(&base, addNode(base, mine), addNode(base, other));
        QCOMPARE(std::ranges::count(ids(m), u"p1"_s), 1);
        QCOMPARE(m.node(u"p1")->x, 0.0);
    }

    void remoteBytesKept()
    {
        const QString remoteText = canvasfixtures::obsidian();
        const Canvas base = read(remoteText);
        const Canvas local = moveNodes(base, {u"0f1e2d3c4b5a6978"_s}, {10, 0});
        const std::optional<QString> merged = mergeCanvasFiles(writeCanvas(base), writeCanvas(local), remoteText);
        QVERIFY(merged);
        // Only the moved card's line differs from the remote file.
        const QStringList a = remoteText.split(u'\n');
        const QStringList b = merged->split(u'\n');
        QCOMPARE(a.size(), b.size());
        int different = 0;
        for (qsizetype i = 0; i < a.size(); ++i)
            different += a.at(i) != b.at(i) ? 1 : 0;
        QCOMPARE(different, 1);
        QVERIFY(merged->contains(u"\"x\":-30,"_s));
    }

    void prettyRemoteLayout()
    {
        // A local Obsidian-style file and a pretty remote one: written in the remote's layout.
        const Canvas remote = read(canvasfixtures::pretty());
        Canvas base = remote;
        base.layout.reset();
        const Canvas local = addNode(base, makeTextNode(u"Lokalna"_s, QRectF(0, 300, 250, 60), fixed(u"l1"_s)));
        const std::optional<QString> merged =
                mergeCanvasFiles(writeCanvas(base), writeCanvas(local), canvasfixtures::pretty());
        QVERIFY(merged);
        QVERIFY(merged->startsWith(u"{\n  \"nodes\": [\n    {\n      \"id\": \"t1\","_s));
        // The local card as the local file wrote it (in the pretty layout its nodes came with).
        QVERIFY(merged->contains(u"\"id\": \"l1\""_s));
        QCOMPARE(readCanvas(*merged).canvas->nodes.size(), 3);
    }

    void notCanvases()
    {
        const QString ok = canvasfixtures::specSample();
        QVERIFY(!mergeCanvasFiles(ok, u"{\"nodes\":["_s, ok));
        QVERIFY(!mergeCanvasFiles(ok, ok, u"nie JSON"_s));
        QVERIFY(!mergeCanvasFiles(u"[]"_s, ok, ok));
        // Empty files are empty canvases (a canvas created on both devices).
        const std::optional<QString> fromEmpty = mergeCanvasFiles(QString(), ok, QString());
        QVERIFY(fromEmpty);
        QCOMPARE(read(*fromEmpty).nodes.size(), 5);
    }

    void deterministic()
    {
        const Canvas base = read(canvasfixtures::obsidian());
        const Canvas ours = moveNodes(base, {u"a1b2c3d4e5f60718"_s}, {1, 1});
        const Canvas theirs = setColor(base, {u"a1b2c3d4e5f60718"_s}, {}, u"1"_s);
        QCOMPARE(writeCanvas(merge(&base, ours, theirs)), writeCanvas(merge(&base, ours, theirs)));
    }
};

QTEST_GUILESS_MAIN(TestCanvasMerge)
#include "tst_canvas_merge.moc"
