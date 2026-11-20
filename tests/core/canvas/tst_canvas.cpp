// The JSON Canvas file (canvas/canvas.h): reading the spec's and Obsidian's files, writing them back
// byte for byte, a changed node with its keys in their order and unknown keys kept, added and
// removed nodes in the file's own layout, malformed and huge input, clipboard fragments.
#include "canvas/canvas.h"
#include "canvas/edit.h"
#include "canvas/json_p.h"
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
        qFatal("not a canvas: %d", int(r.error));
    return *r.canvas;
}

/// The lines of `a` and `b` that differ (same number of lines expected).
QStringList changedLines(const QString &a, const QString &b)
{
    const QStringList la = a.split(u'\n');
    const QStringList lb = b.split(u'\n');
    QStringList out;
    for (qsizetype i = 0; i < std::max(la.size(), lb.size()); ++i) {
        if (la.value(i) != lb.value(i))
            out.append(lb.value(i));
    }
    return out;
}

} // namespace

class TestCanvas : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void specSample()
    {
        const Canvas c = read(canvasfixtures::specSample());
        QCOMPARE(c.nodes.size(), 5);
        QCOMPARE(c.edges.size(), 1);
        QVERIFY(c.nodes.at(0).isGroup());
        QCOMPARE(c.nodes.at(0).label, u"JSON Canvas"_s);
        QCOMPARE(c.nodes.at(1).file, u"readme.md"_s);
        QCOMPARE(c.nodes.at(1).color, u"6"_s);
        QCOMPARE(c.nodes.at(3).text.left(12), u"Learn more:\n"_s);
        QCOMPARE(c.nodes.at(2).width, 217.0);
        const Edge &e = c.edges.at(0);
        QCOMPARE(e.fromNode, u"7efdbbe0c4742315"_s);
        QCOMPARE(e.fromSide, u"right"_s);
        QVERIFY(!e.arrowAtStart());
        QVERIFY(e.arrowAtEnd());
    }

    void obsidianFields()
    {
        const Canvas c = read(canvasfixtures::obsidian());
        QCOMPARE(c.nodes.size(), 6);
        const Node *text = c.node(u"a1b2c3d4e5f60718");
        QVERIFY(text && text->isText());
        QVERIFY(text->text.contains(u"zamówić płytki"_s));
        QVERIFY(text->text.contains(u'\n'));
        QCOMPARE(text->color, u"4"_s);
        const Node *section = c.node(u"1122334455667788");
        QCOMPARE(section->subpath, u"#Szafki"_s);
        QCOMPARE(section->color, u"#ff8800"_s);
        QCOMPARE(c.node(u"abcdefabcdefabcd")->url, u"https://example.com/żółw"_s);
        QCOMPARE(c.edges.at(1).toEnd, u"none"_s);
        QVERIFY(!c.edges.at(1).arrowAtEnd());
        QVERIFY(c.edges.at(2).arrowAtStart());
        QCOMPARE(c.edges.at(0).label, u"szczegóły"_s);
    }

    void roundTripUnchanged_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("obsidian") << canvasfixtures::obsidian();
        QTest::newRow("spec sample") << canvasfixtures::specSample();
        QTest::newRow("pretty") << canvasfixtures::pretty();
        QTest::newRow("advanced canvas") << canvasfixtures::advanced();
        QTest::newRow("crlf and bom") << canvasfixtures::crlfBom();
        QTest::newRow("compact")
                << uR"({"nodes":[{"id":"a","type":"text","text":"x","x":0,"y":0,"width":1,"height":1}],"edges":[]})"_s;
        QTest::newRow("only nodes") << uR"({"nodes":[]})"_s;
        QTest::newRow("empty object") << u"{}"_s;
        QTest::newRow("escapes") << canvasfixtures::escaped();
        QTest::newRow("fractions")
                << uR"({"nodes":[{"id":"f","type":"text","text":"","x":12.5,"y":-0.25,"width":1e2,"height":60}]})"_s;
        QTest::newRow("duplicate keys")
                << uR"({"nodes":[{"id":"d","x":1,"x":2,"type":"text","text":"a","y":0,"width":9,"height":9}]})"_s;
    }
    void roundTripUnchanged()
    {
        QFETCH(QString, text);
        const ReadResult r = readCanvas(text);
        QVERIFY(r.canvas);
        QCOMPARE(writeCanvas(*r.canvas), text);
    }

    void duplicateKeysLastWins()
    {
        const Canvas c =
                read(uR"({"nodes":[{"id":"d","x":1,"x":2,"type":"text","text":"a","y":0,"width":9,"height":9}]})"_s);
        QCOMPARE(c.nodes.at(0).x, 2.0);
        // A move rewrites the occurrence JSON.parse keeps; the first stays as written.
        const Canvas moved = moveNodes(c, {u"d"_s}, {10, 0});
        QCOMPARE(writeCanvas(moved),
                 uR"({"nodes":[{"id":"d","x":1,"x":12,"type":"text","text":"a","y":0,"width":9,"height":9}]})"_s);
    }

    void escapesDecoded()
    {
        const Canvas c = read(canvasfixtures::escapedShort());
        QCOMPARE(c.nodes.at(0).text, u"\"a\" \\ / \t\ną 😀"_s);
        // Written back changed: JSON.stringify's escaping (Polish letters and emoji as they are).
        const Canvas edited = setNodeText(c, u"q"_s, u"\"b\" \\ \t\nżółw 😀"_s);
        QCOMPARE(writeCanvas(edited), canvasfixtures::escapedWritten());
    }

    void movedNodeOnlyItsLine()
    {
        const QString text = canvasfixtures::obsidian();
        const Canvas c = read(text);
        const Canvas moved = moveNodes(c, {u"0f1e2d3c4b5a6978"_s}, {20.4, -10});
        const QString out = writeCanvas(moved);
        const QStringList changed = changedLines(text, out);
        QCOMPARE(changed.size(), 1);
        QCOMPARE(
                changed.at(0),
                u"\t\t{\"id\":\"0f1e2d3c4b5a6978\",\"type\":\"file\",\"file\":\"Projekty/Kuchnia.md\",\"x\":-20,\"y\":-290,\"width\":400,\"height\":400},"_s);
    }

    void keysAddedAndRemoved()
    {
        const QString text = canvasfixtures::obsidian();
        const Canvas c = read(text);
        // A colour set: after the last key. A colour removed: the key goes.
        Canvas edited = setColor(c, {u"0f1e2d3c4b5a6978"_s}, {}, u"3"_s);
        edited = setColor(edited, {u"a1b2c3d4e5f60718"_s}, {u"e2e2e2e2e2e2e2e2"_s}, QString());
        const QStringList changed = changedLines(text, writeCanvas(edited));
        QCOMPARE(changed.size(), 3);
        QVERIFY(changed.at(0).endsWith(u"\"width\":320,\"height\":240},"_s));
        QVERIFY(changed.at(1).endsWith(u"\"width\":400,\"height\":400,\"color\":\"3\"},"_s));
        QCOMPARE(
                changed.at(2),
                u"\t\t{\"id\":\"e2e2e2e2e2e2e2e2\",\"fromNode\":\"0f1e2d3c4b5a6978\",\"fromSide\":\"bottom\",\"toNode\":\"1122334455667788\",\"toSide\":\"top\",\"toEnd\":\"none\"},"_s);
    }

    void unknownKeysKept()
    {
        const QString text = canvasfixtures::advanced();
        const Canvas c = read(text);
        QCOMPARE(c.nodes.size(), 3);
        QCOMPARE(c.nodes.at(2).type, u"diagram"_s);
        Canvas edited = setNodeText(c, u"n1"_s, u"Początek"_s);
        edited = setEdgeLabel(edited, u"x1"_s, u"dalej"_s);
        const QString out = writeCanvas(edited);
        QVERIFY(out.startsWith(
                u"{\n\t\"metadata\":{\"version\":\"1.0-1.0\",\"frontmatter\":{\"tags\":[\"projekt\"]},\"startNode\":\"n1\"},"_s));
        QVERIFY(out.contains(
                u"{\"id\":\"n1\",\"type\":\"text\",\"text\":\"Początek\",\"styleAttributes\":{\"shape\":\"pill\",\"border\":\"dashed\"},\"x\":0,\"y\":0,\"width\":200,\"height\":80,\"zIndex\":3}"_s));
        QVERIFY(out.contains(
                u"{\"id\":\"x1\",\"styleAttributes\":{\"path\":\"dotted\",\"pathfindingMethod\":\"square\"},\"fromNode\":\"n1\",\"fromSide\":\"right\",\"toNode\":\"n2\",\"toSide\":\"left\",\"label\":\"dalej\"}"_s));
        // The node of an unknown type is still as written.
        QVERIFY(out.contains(
                u"{\"id\":\"n3\",\"type\":\"diagram\",\"text\":\"(typ, którego Brulion nie zna)\",\"x\":0,\"y\":200,\"width\":200,\"height\":100}"_s));
    }

    void addedNodeInObsidianLayout()
    {
        const QString text = canvasfixtures::specSample();
        const Canvas c = read(text);
        int n = 0;
        const IdSource ids = [&n] { return u"new%1"_s.arg(++n); };
        Canvas edited = addNode(c, makeTextNode(u"Nowa karta"_s, QRectF(10.2, 20.7, 250, 60), ids));
        edited = connectNodes(edited, u"new1"_s, u"left"_s, u"8132d4d894c80022"_s, QString(), nullptr, ids);
        const QString out = writeCanvas(edited);
        QVERIFY(out.contains(
                u"\t\t{\"id\":\"0ba565e7f30e0652\",\"type\":\"file\",\"file\":\"spec/"
                u"1.0.md\",\"x\":360,\"y\":-400,\"width\":400,\"height\":400},\n"
                u"\t\t{\"id\":\"new1\",\"type\":\"text\",\"text\":\"Nowa karta\",\"x\":10,\"y\":21,\"width\":250,\"height\":60}\n\t],"_s));
        QVERIFY(out.contains(
                u"\"toSide\":\"left\"},\n\t\t{\"id\":\"new2\",\"fromNode\":\"new1\",\"fromSide\":\"left\",\"toNode\":\"8132d4d894c80022\"}\n\t]"_s));
        QVERIFY(out.endsWith(u"}\n"_s)); // the sample's newline at the end kept
        QVERIFY(readCanvas(out).canvas);
    }

    void removedNodes()
    {
        const Canvas c = read(canvasfixtures::specSample());
        const Canvas first = removeItems(c, {u"754a8ef995f366bc"_s}, {});
        QVERIFY(writeCanvas(first).startsWith(u"{\n\t\"nodes\":[\n\t\t{\"id\":\"8132d4d894c80022\""_s));
        // A node's edges go with it; an array left empty is written `[]`.
        const Canvas noLogo = removeItems(c, {u"7efdbbe0c4742315"_s}, {});
        QVERIFY(writeCanvas(noLogo).contains(u"\t\"edges\":[]\n}"_s));
        Canvas all = c;
        all.nodes.clear();
        all.edges.clear();
        QCOMPARE(writeCanvas(all), u"{\n\t\"nodes\":[],\n\t\"edges\":[]\n}\n"_s);
    }

    void emptyFiles_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        const QString node =
                u"{\"id\":\"a\",\"type\":\"text\",\"text\":\"x\",\"x\":0,\"y\":0,\"width\":250,\"height\":60}"_s;
        QTest::newRow("empty") << QString() << u"{\n\t\"nodes\":[\n\t\t"_s + node + u"\n\t],\n\t\"edges\":[]\n}"_s;
        QTest::newRow("whitespace") << u" \n"_s << u"{\n\t\"nodes\":[\n\t\t"_s + node + u"\n\t],\n\t\"edges\":[]\n}"_s;
        QTest::newRow("{}") << u"{}"_s << u"{\n\t\"nodes\":[\n\t\t"_s + node + u"\n\t]\n}"_s;
        QTest::newRow("compact without nodes")
                << u"{\"edges\":[]}"_s << u"{\"edges\":[],\"nodes\":["_s + node + u"]}"_s;
        QTest::newRow("obsidian without edges")
                << u"{\n\t\"nodes\":[]\n}"_s << u"{\n\t\"nodes\":[\n\t\t"_s + node + u"\n\t]\n}"_s;
    }
    void emptyFiles()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        const Canvas c = read(text);
        QVERIFY(c.isEmpty());
        const Canvas added = addNode(c, makeTextNode(u"x"_s, QRectF(0, 0, 250, 60), [] { return u"a"_s; }));
        QCOMPARE(writeCanvas(added), expected);
        QVERIFY(readCanvas(writeCanvas(added)).canvas);
    }

    void edgesInsertedWhenMissing()
    {
        const Canvas c = read(
                u"{\n\t\"nodes\":[\n\t\t{\"id\":\"a\",\"type\":\"text\",\"text\":\"1\",\"x\":0,\"y\":0,\"width\":9,"
                u"\"height\":9},\n"
                u"\t\t{\"id\":\"b\",\"type\":\"text\",\"text\":\"2\",\"x\":50,\"y\":0,\"width\":9,\"height\":9}\n\t]\n}"_s);
        const Canvas linked = connectNodes(c, u"a"_s, u"right"_s, u"b"_s, u"left"_s, nullptr, [] { return u"e"_s; });
        QCOMPARE(
                writeCanvas(linked),
                u"{\n\t\"nodes\":[\n\t\t{\"id\":\"a\",\"type\":\"text\",\"text\":\"1\",\"x\":0,\"y\":0,\"width\":9,"
                u"\"height\":9},\n"
                u"\t\t{\"id\":\"b\",\"type\":\"text\",\"text\":\"2\",\"x\":50,\"y\":0,\"width\":9,\"height\":9}\n\t],\n"
                u"\t\"edges\":[\n\t\t{\"id\":\"e\",\"fromNode\":\"a\",\"fromSide\":\"right\",\"toNode\":\"b\",\"toSide\":\"left\"}\n\t]\n}"_s);
    }

    void prettyLayoutKept()
    {
        const QString text = canvasfixtures::pretty();
        const Canvas c = read(text);
        Canvas edited = setNodeText(c, u"t1"_s, u"Nowy tekst"_s);
        edited = setColor(edited, {u"t1"_s}, {}, u"5"_s);
        edited = addNode(edited, makeTextNode(u"Trzecia"_s, QRectF(0, 200, 250, 60), [] { return u"t3"_s; }));
        const QString out = writeCanvas(edited);
        QVERIFY(out.contains(u"      \"text\": \"Nowy tekst\",\n"_s));
        QVERIFY(out.contains(u"      \"height\": 60,\n      \"color\": \"5\"\n    },"_s));
        QVERIFY(out.contains(
                u"    },\n    {\n      \"id\": \"t3\",\n      \"type\": \"text\",\n      \"text\": \"Trzecia\",\n"_s));
        QVERIFY(out.contains(u"      \"height\": 60\n    }\n  ],"_s));
        QVERIFY(out.endsWith(u"}\n"_s));
        QVERIFY(readCanvas(out).canvas);
    }

    void crlfAndBomKept()
    {
        const QString text = canvasfixtures::crlfBom();
        const Canvas c = read(text);
        QCOMPARE(c.nodes.at(0).text, u"ą"_s);
        const QString out = writeCanvas(moveNodes(c, {u"a"_s}, {1, 1}));
        QVERIFY(out.startsWith(QChar(0xFEFF)));
        QVERIFY(out.contains(u"\"x\": 2, \"y\": 3,"_s)); // the file's ": " kept
        QVERIFY(out.endsWith(u"\r\n  \"edges\": []\r\n}\r\n"_s));
    }

    void fractionsUntilMoved()
    {
        const Canvas c =
                read(uR"({"nodes":[{"id":"f","type":"text","text":"","x":12.5,"y":-0.25,"width":1e2,"height":60}]})"_s);
        QCOMPARE(c.nodes.at(0).width, 100.0);
        // Untouched numbers as written; moved ones whole.
        QCOMPARE(writeCanvas(moveNodes(c, {u"f"_s}, {0, 10})),
                 uR"({"nodes":[{"id":"f","type":"text","text":"","x":12.5,"y":10,"width":1e2,"height":60}]})"_s);
    }

    void idsMadeForMissingAndDuplicates()
    {
        const QString text =
                uR"({"nodes":[{"id":"a","type":"text","text":"1","x":0,"y":0,"width":9,"height":9},{"id":"a","type":"text","text":"2","x":0,"y":0,"width":9,"height":9},{"type":"text","text":"3","x":0,"y":0,"width":9,"height":9}]})"_s;
        const Canvas c = read(text);
        QCOMPARE(c.nodes.at(0).id, u"a"_s);
        QVERIFY(c.nodes.at(1).id != u"a"_s);
        QVERIFY(!c.nodes.at(2).id.isEmpty());
        QVERIFY(c.nodes.at(1).id != c.nodes.at(2).id);
        // The same ids on every read (deterministic).
        const Canvas again = read(text);
        QCOMPARE(again.nodes.at(1).id, c.nodes.at(1).id);
        QCOMPARE(again.nodes.at(2).id, c.nodes.at(2).id);
        // Written with them only once the file is saved.
        const QString out = writeCanvas(c);
        QVERIFY(out.contains(u"\"id\":\""_s + c.nodes.at(1).id + u"\""_s));
        QVERIFY(out.contains(u"{\"type\":\"text\",\"text\":\"3\",\"x\":0,\"y\":0,\"width\":9,\"height\":9,\"id\":\""_s
                             + c.nodes.at(2).id + u"\"}"_s));
    }

    void malformed_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("error");
        QTest::newRow("text") << u"to nie jest JSON"_s << int(ReadError::NotJson);
        QTest::newRow("truncated") << u"{\"nodes\":[{\"id\":\"a\",\"type\":\"text\""_s << int(ReadError::NotJson);
        QTest::newRow("trailing garbage") << u"{} {}"_s << int(ReadError::NotJson);
        QTest::newRow("trailing comma") << uR"({"nodes":[],})"_s << int(ReadError::NotJson);
        QTest::newRow("unterminated string") << u"{\"nodes\":[{\"id\":\"a"_s << int(ReadError::NotJson);
        QTest::newRow("bad escape") << u"{\"a\":\"\\q\"}"_s << int(ReadError::NotJson);
        QTest::newRow("bad number") << uR"({"a":-})"_s << int(ReadError::NotJson);
        QTest::newRow("array root") << u"[1, 2]"_s << int(ReadError::NotCanvas);
        QTest::newRow("string root") << u"\"canvas\""_s << int(ReadError::NotCanvas);
        QTest::newRow("nodes object") << uR"({"nodes":{}})"_s << int(ReadError::NotCanvas);
        QTest::newRow("edges string") << uR"({"edges":"x"})"_s << int(ReadError::NotCanvas);
        QTest::newRow("node number") << uR"({"nodes":[1]})"_s << int(ReadError::NotCanvas);
        QTest::newRow("edge null") << uR"({"edges":[null]})"_s << int(ReadError::NotCanvas);
        QTest::newRow("too deep") << u"{\"x\":"_s + QString(200, u'[') + QString(200, u']') + u'}'
                                  << int(ReadError::TooLarge);
    }
    void malformed()
    {
        QFETCH(QString, text);
        QFETCH(int, error);
        const ReadResult r = readCanvas(text);
        QVERIFY(!r.canvas);
        QCOMPARE(int(r.error), error);
    }

    void oddButReadable()
    {
        // Strings where numbers belong, numbers where strings belong: read leniently, kept as written.
        const QString text =
                uR"({"nodes":[{"id":7,"type":"text","text":null,"x":"10","y":0,"width":9,"height":9,"color":3}]})"_s;
        const Canvas c = read(text);
        QCOMPARE(c.nodes.at(0).id, u"7"_s);
        QCOMPARE(c.nodes.at(0).x, 0.0);
        QCOMPARE(c.nodes.at(0).color, u"3"_s);
        QCOMPARE(writeCanvas(c), text);
        // Infinity (1e999) reads but stays within reach when drawn.
        const Canvas far =
                read(uR"({"nodes":[{"id":"i","type":"text","text":"","x":1e999,"y":0,"width":9,"height":9}]})"_s);
        QVERIFY(qIsInf(far.nodes.at(0).x));
        QCOMPARE(nodeRect(far.nodes.at(0)).x(), 0.0);
    }

    void huge()
    {
        // Beyond the size limit: refused before parsing.
        QString big = u"{\"nodes\":[],\"x\":\""_s;
        big += QString(MaxCanvasChars, u'a');
        big += u"\"}"_s;
        QCOMPARE(int(readCanvas(big).error), int(ReadError::TooLarge));
        // Too many nodes.
        QString many = u"{\"nodes\":["_s;
        for (qsizetype i = 0; i <= MaxNodes; ++i)
            many += (i ? u","_s : QString()) + u"{\"id\":\"n%1\"}"_s.arg(i);
        many += u"]}"_s;
        QCOMPARE(int(readCanvas(many).error), int(ReadError::TooLarge));
        // Many values in one unknown key: bounded by the reader.
        QString values = u"{\"x\":["_s;
        for (qsizetype i = 0; i < detail::MaxJsonValues + 10; ++i)
            values += i ? u",0"_s : u"0"_s;
        values += u"]}"_s;
        QCOMPARE(int(readCanvas(values).error), int(ReadError::TooLarge));
    }

    void thousandNodesFast()
    {
        Canvas c;
        for (int i = 0; i < 1000; ++i) {
            c = addNode(c, makeTextNode(u"Karta %1 z [[Strona %1]] i tekstem do przeczytania"_s.arg(i),
                                        QRectF((i % 40) * 300, (i / 40) * 200, 250, 120),
                                        [i] { return u"n%1"_s.arg(i); }));
        }
        for (int i = 1; i < 1000; ++i)
            c = connectNodes(c, u"n%1"_s.arg(i - 1), QString(), u"n%1"_s.arg(i), QString(), nullptr,
                             [i] { return u"e%1"_s.arg(i); });
        const QString text = writeCanvas(c);
        const Canvas back = read(text);
        QCOMPARE(back.nodes.size(), 1000);
        QCOMPARE(back.edges.size(), 999);
        QCOMPARE(writeCanvas(back), text);
    }

    void jsonNumbers_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("zero") << 0.0 << u"0"_s;
        QTest::newRow("minus zero") << -0.0 << u"0"_s;
        QTest::newRow("integer") << -15.0 << u"-15"_s;
        QTest::newRow("fraction") << 1.5 << u"1.5"_s;
        QTest::newRow("small") << 1.5e-7 << u"1.5e-7"_s;
        QTest::newRow("large") << 1e21 << u"1e+21"_s;
        QTest::newRow("nan") << qQNaN() << u"0"_s;
    }
    void jsonNumbers()
    {
        QFETCH(double, value);
        QFETCH(QString, expected);
        QCOMPARE(detail::jsonNumber(value), expected);
    }

    void jsonStrings()
    {
        QCOMPARE(detail::jsonString(u"a\"b\\c\nd\u0001ż"_s), u"\"a\\\"b\\\\c\\nd\\u0001ż\""_s);
        // A lone surrogate escaped (well-formed JSON.stringify), a pair as it is.
        QCOMPARE(detail::jsonString(QString(QChar(0xD800))), u"\"\\ud800\""_s);
        QCOMPARE(detail::jsonString(u"😀"_s), u"\"😀\""_s);
    }

    void fragments()
    {
        const Canvas c = read(canvasfixtures::obsidian());
        const QString text = fragmentText(c, {u"a1b2c3d4e5f60718"_s, u"0f1e2d3c4b5a6978"_s});
        const std::optional<Canvas> f = parseFragment(text);
        QVERIFY(f);
        QCOMPARE(f->nodes.size(), 2);
        QCOMPARE(f->edges.size(), 1); // only the edge between them
        QCOMPARE(f->edges.at(0).id, u"e1e1e1e1e1e1e1e1"_s);
        QVERIFY(f->nodes.at(0).text.contains(u"zamówić"_s));
        // Not fragments.
        QVERIFY(!parseFragment(u"Zwykły tekst"_s));
        QVERIFY(!parseFragment(u"https://example.com"_s));
        QVERIFY(!parseFragment(u"{\"a\":1}"_s));
        QVERIFY(!parseFragment(u"{\"nodes\":[]}"_s));
        QVERIFY(!parseFragment(u"{\"nodes\":[1]}"_s));
    }

    void paths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("canvas");
        QTest::addColumn<QString>("title");
        QTest::newRow("plain") << u"Tablica.canvas"_s << true << u"Tablica"_s;
        QTest::newRow("folder") << u"Plany/Remont kuchni.canvas"_s << true << u"Remont kuchni"_s;
        QTest::newRow("upper case") << u"A.CANVAS"_s << true << u"A"_s;
        QTest::newRow("hidden name") << u"x/.canvas"_s << false << u".canvas"_s;
        QTest::newRow("note") << u"Tablica.md"_s << false << u"Tablica.md"_s;
        QTest::newRow("canvas folder") << u"canvas/Tablica.md"_s << false << u"Tablica.md"_s;
    }
    void paths()
    {
        QFETCH(QString, path);
        QFETCH(bool, canvas);
        QFETCH(QString, title);
        QCOMPARE(isCanvasPath(path), canvas);
        QCOMPARE(canvasTitle(path), title);
    }
};

QTEST_GUILESS_MAIN(TestCanvas)
#include "tst_canvas.moc"
