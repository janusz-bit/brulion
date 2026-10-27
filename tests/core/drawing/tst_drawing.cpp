// Tests of the drawing module: the outline of strokes (perfect-freehand), the Excalidraw scene
// and its SVG payload (Excalidraw's own fixtures and ones written like excalidraw.com writes them,
// tests/data/drawing/make-fixtures.py), the SVG Brulion writes and malformed input.
#include "drawing/drawing.h"
#include "drawing/excalidraw.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QElapsedTimer>
#include <QTest>
#include <QXmlStreamReader>
#include <QtMath>

#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;
using namespace brulion::drawing;

namespace {

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

Stroke wavyStroke(const QString &id, double x0, double y0, int n, bool pressure = false)
{
    Stroke s;
    s.id = id;
    for (int i = 0; i < n; ++i)
        s.points.append(
                {x0 + i * 4.0, y0 + 12 * std::sin(i / 3.0), pressure ? 0.2 + 0.6 * i / std::max(1, n - 1) : 0.5});
    s.simulatePressure = !pressure;
    normalizeStroke(s);
    s.seed = 7;
    s.versionNonce = 11;
    s.updated = 1761568000000;
    return s;
}

double width(const QList<QPointF> &outline, bool horizontal)
{
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (const QPointF &p : outline) {
        const double v = horizontal ? p.x() : p.y();
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }
    return outline.isEmpty() ? 0 : hi - lo;
}

/// The SVG's root element and whether the whole document is well-formed XML.
struct SvgCheck {
    bool wellFormed = false;
    QString rootName;
    QXmlStreamAttributes root;
    QString rootNamespace;
    int paths = 0;
};

SvgCheck checkSvg(const QByteArray &svg)
{
    SvgCheck out;
    QXmlStreamReader xml(svg);
    bool first = true;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (first) {
                out.rootName = xml.name().toString();
                out.root = xml.attributes();
                out.rootNamespace = xml.namespaceUri().toString();
                first = false;
            }
            if (xml.name() == u"path")
                ++out.paths;
        }
    }
    out.wellFormed = !xml.hasError();
    return out;
}

} // namespace

class TestDrawing : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void formatNumbers_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("text");
        QTest::newRow("integer") << 12.0 << u"12"_s;
        QTest::newRow("two decimals") << 3.14159 << u"3.14"_s;
        QTest::newRow("trailing zero") << 2.5 << u"2.5"_s;
        QTest::newRow("negative") << -0.126 << u"-0.13"_s;
        QTest::newRow("negative zero") << -0.001 << u"0"_s;
        QTest::newRow("big") << 123456.789 << u"123456.79"_s;
        QTest::newRow("nan") << std::nan("") << u"0"_s;
        QTest::newRow("infinity") << std::numeric_limits<double>::infinity() << u"0"_s;
    }
    void formatNumbers()
    {
        QFETCH(double, value);
        QFETCH(QString, text);
        QCOMPARE(formatNumber(value), text);
    }

    void outlineOfFewPoints_data()
    {
        QTest::addColumn<QList<StrokePoint>>("points");
        QTest::addColumn<bool>("drawn");
        QTest::newRow("none") << QList<StrokePoint>{} << false;
        QTest::newRow("one point: a dot") << QList<StrokePoint>{{10, 10, 0.5}} << true;
        QTest::newRow("two identical points: a dot") << QList<StrokePoint>{{10, 10, 0.5}, {10, 10, 0.5}} << true;
        QTest::newRow("two points") << QList<StrokePoint>{{0, 0, 0.5}, {40, 0, 0.5}} << true;
        QTest::newRow("only nan") << QList<StrokePoint>{{std::nan(""), 1, 0.5}} << false;
        QTest::newRow("infinity dropped")
                << QList<StrokePoint>{{0, 0, 0.5}, {std::numeric_limits<double>::infinity(), 3, 0.5}, {30, 0, 0.5}}
                << true;
    }
    void outlineOfFewPoints()
    {
        QFETCH(QList<StrokePoint>, points);
        QFETCH(bool, drawn);
        Stroke s;
        s.points = points;
        const QList<QPointF> outline = strokeOutline(s);
        QCOMPARE(!outline.isEmpty(), drawn);
        for (const QPointF &p : outline) {
            QVERIFY(std::isfinite(p.x()));
            QVERIFY(std::isfinite(p.y()));
        }
        if (drawn) {
            const QString d = outlineToSvgPath(outline);
            QVERIFY(d.startsWith(u'M'));
            QVERIFY(d.contains(u" Q "_s));
            QVERIFY(d.endsWith(u" Z"_s));
        } else {
            QCOMPARE(outlineToSvgPath(outline), QString());
        }
    }

    void dotIsRound()
    {
        Stroke s;
        s.width = 2; // size 8.5
        s.points = {{100, 50, 0.5}};
        const QList<QPointF> outline = strokeOutline(s);
        QVERIFY(outline.size() >= 10);
        const double w = width(outline, true);
        const double h = width(outline, false);
        QVERIFY2(w > 4 && w < 25, qPrintable(QString::number(w)));
        QVERIFY(std::abs(w - h) < 3);
    }

    void pressureChangesWidth()
    {
        // A horizontal line: its outline's height is the stroke's width.
        const auto line = [](double pressure) {
            Stroke s;
            s.simulatePressure = false;
            s.width = 2;
            for (int i = 0; i <= 40; ++i)
                s.points.append({i * 5.0, 0, pressure});
            return width(strokeOutline(s), false);
        };
        const double light = line(0.0);
        const double middle = line(0.5);
        const double hard = line(1.0);
        QVERIFY2(light < middle && middle < hard, qPrintable(u"%1 %2 %3"_s.arg(light).arg(middle).arg(hard)));
        // Excalidraw's size: strokeWidth 2 × 4.25, the radius size·sin(π/4) at middle pressure.
        QVERIFY(std::abs(middle - 2 * 8.5 * std::sin(M_PI / 4)) < 1.5);
        // Pressures outside 0–1 are clamped.
        QCOMPARE(line(7.0), hard);
    }

    void highlighterHasEvenWidth()
    {
        Stroke s;
        s.tool = Tool::Highlighter;
        s.simulatePressure = false;
        s.width = 4;
        for (int i = 0; i <= 30; ++i)
            s.points.append({i * 6.0, 0, 0.5});
        const QRectF b = strokeBounds(s);
        QVERIFY(b.height() > 20 && b.height() < 30);
    }

    void outlinePathMoves()
    {
        const QList<QPointF> outline{{1, 2}, {3, 4}, {5.5, 6}};
        QCOMPARE(outlineToSvgPath(outline), u"M1,2 Q 1,2 2,3 3,4 4.25,5 5.5,6 3.25,4 L1,2 Z"_s);
        QVERIFY(outlineToSvgPath(outline, 10, -2).startsWith(u"M11,0 Q 11,0"_s));
    }

    void zlibRoundTrip()
    {
        const QByteArray text = u"Zażółć gęślą jaźń — rysunek z wykładu "_s.repeated(200).toUtf8();
        const QByteArray z = deflate(text);
        QVERIFY(!z.isEmpty());
        QCOMPARE(uchar(z.at(0)), uchar(0x78)); // a zlib stream, as pako writes
        QVERIFY(z.size() < text.size());
        QCOMPARE(inflate(z), std::optional<QByteArray>(text));
        // Broken, truncated, not compressed at all, too big.
        QByteArray broken = z;
        broken[10] = char(broken[10] ^ 0x5a);
        broken[11] = char(broken[11] ^ 0x33);
        QVERIFY(!inflate(broken));
        QVERIFY(!inflate(z.left(z.size() / 2)));
        QVERIFY(!inflate(QByteArray("not zlib at all")));
        QVERIFY(!inflate(QByteArray()));
        const QByteArray zeros(4 * 1024 * 1024, '\0');
        QVERIFY(!inflate(deflate(zeros), 1024 * 1024)); // a "bomb" beyond the limit
        QVERIFY(inflate(deflate(zeros), 8 * 1024 * 1024));
    }

    void payloadRoundTrip()
    {
        Drawing d;
        d.strokes = {wavyStroke(u"a"_s, 0, 0, 20), wavyStroke(u"b"_s, 10, 40, 30, true)};
        const QByteArray scene = sceneJson(d);
        const QByteArray b64 = encodePayload(scene);
        QVERIFY(!b64.contains('\n'));
        QCOMPARE(decodePayload(b64, 2), std::optional<QByteArray>(scene));
        // Whitespace and CRLF between the markers.
        QByteArray wrapped = "\r\n  " + b64.left(40) + "\r\n" + b64.mid(40) + "\n\t";
        QCOMPARE(decodePayload(wrapped, 2), std::optional<QByteArray>(scene));
        // Not base64, truncated base64, empty.
        QVERIFY(!decodePayload("@@@@not*base64", 2));
        QVERIFY(!decodePayload(b64.left(b64.size() / 2), 2));
        QVERIFY(!decodePayload("", 2));
        // Base64 of JSON that is no payload.
        QVERIFY(!decodePayload(QByteArray("{\"hello\":\"world\"}").toBase64(), 2));
        QVERIFY(!decodePayload(QByteArray("[1,2,3]").toBase64(), 2));
        QVERIFY(!decodePayload(QByteArray("{\"encoded\":\"x\",\"encoding\":\"other\"}").toBase64(), 2));
    }

    void uncompressedPayload()
    {
        // compressed:false keeps the UTF-8 scene as a byte string ("ą" = two characters).
        const QByteArray scene = "{\"type\":\"excalidraw\",\"version\":2,\"source\":\"Zażółć\",\"elements\":[]}";
        QByteArray wrapper = "{\"version\":\"1\",\"encoding\":\"bstring\",\"compressed\":false,\"encoded\":\"";
        wrapper += QByteArray(scene).replace(QByteArrayView("\""), QByteArrayView("\\\""));
        wrapper += "\"}";
        QCOMPARE(decodePayload(wrapper.toBase64(), 2), std::optional<QByteArray>(scene));
        const std::optional<Drawing> d = parseScene(*decodePayload(wrapper.toBase64(), 2));
        QVERIFY(d);
        QCOMPARE(d->scene.value(u"source"_s).toString(), u"Zażółć"_s);
    }

    void svgPayloads_data()
    {
        QTest::addColumn<QByteArray>("svg");
        QTest::addColumn<bool>("found");
        QTest::addColumn<bool>("corrupt");
        const QByteArray b64 = encodePayload(sceneJson(Drawing()));
        const QByteArray head =
                "<svg xmlns=\"http://www.w3.org/2000/svg\"><!-- payload-type:application/vnd.excalidraw+json -->";
        QTest::newRow("plain svg") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><rect/></svg>") << false
                                   << false;
        QTest::newRow("png") << QByteArray("\x89PNG\r\n\x1a\n....") << false << false;
        QTest::newRow("ok") << head + "<!-- payload-version:2 --><!-- payload-start -->" + b64
                                       + "<!-- payload-end --></svg>"
                            << true << false;
        QTest::newRow("crlf around") << head + "<!-- payload-version:2 -->\r\n<!-- payload-start -->\r\n  " + b64
                                                + "\r\n<!-- payload-end -->\r\n</svg>"
                                     << true << false;
        QTest::newRow("no end marker") << head + "<!-- payload-version:2 --><!-- payload-start -->" + b64 + "</svg>"
                                       << false << true;
        QTest::newRow("no start marker") << head + "<!-- payload-version:2 -->" + b64 + "<!-- payload-end -->" << false
                                         << true;
        QTest::newRow("truncated base64") << head + "<!-- payload-version:2 --><!-- payload-start -->"
                                                     + b64.left(b64.size() - 9) + "<!-- payload-end -->"
                                          << false << true;
        QTest::newRow("wrong json") << head + "<!-- payload-version:2 --><!-- payload-start -->"
                                               + QByteArray("{not json").toBase64() + "<!-- payload-end -->"
                                    << false << true;
        QTest::newRow("huge version number")
                << head + "<!-- payload-version:99999999999 --><!-- payload-start -->" + b64 + "<!-- payload-end -->"
                << true << false;
    }
    void svgPayloads()
    {
        QFETCH(QByteArray, svg);
        QFETCH(bool, found);
        QFETCH(bool, corrupt);
        bool wasCorrupt = !corrupt;
        QCOMPARE(svgPayload(svg, &wasCorrupt).has_value(), found);
        QCOMPARE(wasCorrupt, corrupt);
        ReadError error = ReadError::None;
        QCOMPARE(readDrawing(svg, &error).has_value(), found);
        QCOMPARE(error, found ? ReadError::None : corrupt ? ReadError::Corrupt : ReadError::NoScene);
    }

    void fixtures_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("strokes");
        QTest::addColumn<int>("foreign");
        QTest::addColumn<QString>("paper");
        QTest::newRow("excalidraw.com v2 (text)") << u"smiley_embedded_v2.svg"_s << 0 << 1 << u"#ffffff"_s;
        QTest::newRow("excalidraw.com v1 (text)") << u"test_embedded_v1.svg"_s << 0 << 1 << u"#ffffff"_s;
        QTest::newRow("freehand only") << u"excalidraw_freedraw.svg"_s << 2 << 0 << u"#ffffff"_s;
        QTest::newRow("freehand, uncompressed") << u"excalidraw_freedraw_uncompressed.svg"_s << 2 << 0 << u"#ffffff"_s;
        QTest::newRow("mixed svg") << u"excalidraw_mixed.svg"_s << 1 << 2 << u"#ffffff"_s;
        QTest::newRow("mixed .excalidraw") << u"excalidraw_mixed.excalidraw"_s << 1 << 2 << u"#ffffff"_s;
    }
    void fixtures()
    {
        QFETCH(QString, file);
        QFETCH(int, strokes);
        QFETCH(int, foreign);
        QFETCH(QString, paper);
        const QByteArray bytes = fixture(file);
        ReadError error = ReadError::Corrupt;
        const std::optional<Drawing> d = readDrawing(bytes, &error);
        QVERIFY(d);
        QCOMPARE(error, ReadError::None);
        QCOMPARE(d->strokes.size(), strokes);
        QCOMPARE(d->foreign.size(), foreign);
        QCOMPARE(editable(*d), foreign == 0);
        QCOMPARE(d->paper, paper);
        if (file.endsWith(u".svg"_s))
            QVERIFY(looksLikeExcalidrawSvg(QByteArrayView(bytes).first(std::min<qsizetype>(bytes.size(), HeadBytes))));
    }

    void freehandFixtureValues()
    {
        const std::optional<Drawing> d = readDrawing(fixture(u"excalidraw_freedraw.svg"_s));
        QVERIFY(d);
        // The deleted stroke is gone; the others are in scene coordinates.
        const Stroke &a = d->strokes.at(0);
        QCOMPARE(a.id, u"Qk8_3vL1nYbXc0Rr2mT9a"_s);
        QCOMPARE(a.points.size(), 24);
        QCOMPARE(a.points.first(), (StrokePoint{412.5, 230.25, 0.2}));
        QCOMPARE(a.points.at(1).x, 412.5 + 5.25);
        QVERIFY(!a.simulatePressure);
        QCOMPARE(a.width, 2.0);
        QCOMPARE(a.color, u"#1e1e1e"_s);
        const Stroke &b = d->strokes.at(1);
        QVERIFY(b.simulatePressure);
        QCOMPARE(b.opacity, 80);
        QCOMPARE(b.color, u"#e03131"_s);
        QCOMPARE(b.points.at(3).pressure, 0.5);
        QCOMPARE(d->appState.value(u"gridSize"_s).toInt(), 20);
    }

    void unknownFieldsSurvive()
    {
        // Read, written, read again: every element as it was (index, customData, frameId …), the
        // appState's other keys, the scene's source.
        const std::optional<Drawing> d = readDrawing(fixture(u"excalidraw_mixed.svg"_s));
        QVERIFY(d);
        const QByteArray json = sceneJson(*d);
        const std::optional<Drawing> again = parseScene(json);
        QVERIFY(again);
        QCOMPARE(again->strokes.size(), d->strokes.size());
        QCOMPARE(again->strokes.at(0).element, d->strokes.at(0).element);
        QCOMPARE(again->strokes.at(0).element.value(u"index"_s).toString(), u"a0"_s);
        QCOMPARE(again->foreign, d->foreign);
        QCOMPARE(again->foreign.at(1).toObject().value(u"text"_s).toString(), u"Zażółć gęślą jaźń"_s);
        QCOMPARE(again->appState.value(u"gridStep"_s).toInt(), 5);
        QCOMPARE(again->scene.value(u"source"_s).toString(), u"https://excalidraw.com"_s);
        QCOMPARE(sceneJson(*again), json);
        // Through Brulion's SVG too.
        const std::optional<Drawing> viaSvg = readDrawing(toSvg(*d));
        QVERIFY(viaSvg);
        QCOMPARE(viaSvg->foreign, d->foreign);
        QCOMPARE(viaSvg->strokes.at(0).element, d->strokes.at(0).element);
        const std::optional<Drawing> freehand = readDrawing(fixture(u"excalidraw_freedraw.svg"_s));
        QVERIFY(freehand);
        const std::optional<Drawing> back = readDrawing(toSvg(*freehand));
        QVERIFY(back);
        QCOMPARE(back->strokes.at(1).element.value(u"customData"_s).toObject().value(u"note"_s).toString(), u"kept"_s);
    }

    void foreignElements_data()
    {
        QTest::addColumn<QByteArray>("element");
        QTest::addColumn<bool>("stroke");
        QTest::newRow("freedraw") << QByteArray("{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"points\":[[0,0],[3,4]]}")
                                  << true;
        QTest::newRow("rotated") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"angle\":0.5,\"points\":[[0,0],[3,4]]}")
                                 << false;
        QTest::newRow("filled") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"backgroundColor\":\"#ffc9c9\",\"points\":[[0,0]]}")
                                << false;
        QTest::newRow("no points") << QByteArray("{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"points\":[]}") << false;
        QTest::newRow("point of text") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"points\":[[\"a\",\"b\"]]}")
                                       << false;
        QTest::newRow("short point") << QByteArray("{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"points\":[[1]]}") << false;
        QTest::newRow("no x") << QByteArray("{\"type\":\"freedraw\",\"y\":2,\"points\":[[0,0]]}") << false;
        QTest::newRow("bad pressure") << QByteArray("{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"simulatePressure\":false,"
                                                    "\"pressures\":[\"x\"],\"points\":[[0,0]]}")
                                      << false;
        QTest::newRow("pressures of another length")
                << QByteArray("{\"type\":\"freedraw\",\"x\":1,\"y\":2,\"simulatePressure\":false,\"pressures\":[0.3],"
                              "\"points\":[[0,0],[1,1]]}")
                << true;
        // Far away (CoordinateLimit): shown, never edited — the paper's lines stay bounded.
        QTest::newRow("far away") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":1e18,\"y\":1e18,\"points\":[[0,0],[1024,1024]]}")
                                  << false;
        QTest::newRow("a point far away") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":0,\"y\":0,\"points\":[[0,0],[0,1000001]]}")
                                          << false;
        QTest::newRow("at the limit") << QByteArray(
                "{\"type\":\"freedraw\",\"x\":-1e6,\"y\":0,\"points\":[[0,0],[2e6,1e6]]}")
                                      << true;
        QTest::newRow("rectangle") << QByteArray("{\"type\":\"rectangle\",\"x\":1,\"y\":2,\"width\":3,\"height\":4}")
                                   << false;
        QTest::newRow("image") << QByteArray("{\"type\":\"image\",\"x\":1,\"y\":2,\"fileId\":\"abc\"}") << false;
        QTest::newRow("not an object") << QByteArray("42") << false;
    }
    void foreignElements()
    {
        QFETCH(QByteArray, element);
        QFETCH(bool, stroke);
        const QByteArray scene = "{\"type\":\"excalidraw\",\"version\":2,\"elements\":[" + element + "]}";
        const std::optional<Drawing> d = parseScene(scene);
        QVERIFY(d);
        QCOMPARE(d->strokes.size(), stroke ? 1 : 0);
        QCOMPARE(d->foreign.size(), stroke ? 0 : 1);
        QCOMPARE(editable(*d), stroke);
        if (stroke)
            QVERIFY(d->strokes.at(0).simulatePressure); // no matching pressures: simulated
    }

    void notAScene_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("text") << QByteArray("Zażółć gęślą jaźń");
        QTest::newRow("json array") << QByteArray("[1,2]");
        QTest::newRow("other json") << QByteArray("{\"type\":\"excalidrawlib\",\"libraryItems\":[]}");
        QTest::newRow("elements not array") << QByteArray("{\"type\":\"excalidraw\",\"elements\":{}}");
        QTest::newRow("broken json") << QByteArray("{\"type\":\"excalidraw\",");
    }
    void notAScene()
    {
        QFETCH(QByteArray, bytes);
        ReadError error = ReadError::None;
        QVERIFY(!readDrawing(bytes, &error));
        QCOMPARE(error, ReadError::NoScene);
    }

    void excalidrawFileWithBom()
    {
        QByteArray bytes = "\xEF\xBB\xBF\r\n" + fixture(u"excalidraw_mixed.excalidraw"_s);
        const std::optional<Drawing> d = readDrawing(bytes);
        QVERIFY(d);
        QCOMPARE(d->strokes.size(), 1);
    }

    void headCheck_data()
    {
        QTest::addColumn<QByteArray>("head");
        QTest::addColumn<bool>("drawing");
        QTest::newRow("plain svg") << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"><path d=\"M0,0\"/></svg>")
                                   << false;
        QTest::newRow("excalidraw export without the scene")
                << QByteArray("<svg><!-- svg-source:excalidraw --><metadata></metadata></svg>") << false;
        Drawing d;
        d.strokes = {wavyStroke(u"x"_s, 0, 0, 5)};
        QTest::newRow("ours") << toSvg(d).left(HeadBytes) << true;
        QTest::newRow("bom") << "\xEF\xBB\xBF" + toSvg(d).left(HeadBytes) << true;
        const QByteArray late =
                "<svg>" + QByteArray(HeadBytes, ' ') + "<!-- payload-type:application/vnd.excalidraw+json --></svg>";
        QTest::newRow("marker after the head") << late.left(HeadBytes) << false;
    }
    void headCheck()
    {
        QFETCH(QByteArray, head);
        QFETCH(bool, drawing);
        QCOMPARE(looksLikeExcalidrawSvg(head), drawing);
    }

    void newStrokeElement()
    {
        Stroke s = wavyStroke(u"Rysunek-ćw-3"_s, 100.123, 50.456, 4, true);
        s.color = u"#2f6fdd"_s;
        s.width = 0.5;
        const QJsonObject e = strokeElement(s);
        QCOMPARE(e.value(u"type"_s).toString(), u"freedraw"_s);
        QCOMPARE(e.value(u"id"_s).toString(), u"Rysunek-ćw-3"_s);
        QCOMPARE(e.value(u"x"_s).toDouble(), 100.12);
        QCOMPARE(e.value(u"y"_s).toDouble(), 50.46);
        const QJsonArray points = e.value(u"points"_s).toArray();
        QCOMPARE(points.size(), 4);
        QCOMPARE(points.at(0).toArray(), (QJsonArray{0, 0}));
        QCOMPARE(points.at(1).toArray().at(0).toDouble(), 4.0);
        QCOMPARE(e.value(u"pressures"_s).toArray().size(), 4);
        QCOMPARE(e.value(u"simulatePressure"_s).toBool(), false);
        QCOMPARE(e.value(u"strokeWidth"_s).toDouble(), 0.5);
        QCOMPARE(e.value(u"isDeleted"_s).toBool(), false);
        QCOMPARE(e.value(u"updated"_s).toDouble(), 1761568000000.0);
        QVERIFY(!e.contains(u"customData"_s));
        QVERIFY(e.value(u"width"_s).toDouble() > 11);
        s.tool = Tool::Highlighter;
        s.simulatePressure = true;
        const QJsonObject h = strokeElement(s);
        QCOMPARE(h.value(u"customData"_s).toObject().value(u"brulionTool"_s).toString(), u"highlighter"_s);
        QVERIFY(h.value(u"pressures"_s).toArray().isEmpty());
        // Read back.
        Drawing d;
        d.strokes = {s};
        const std::optional<Drawing> back = parseScene(sceneJson(d));
        QVERIFY(back);
        QCOMPARE(back->strokes.at(0).tool, Tool::Highlighter);
        QCOMPARE(back->strokes.at(0).color, u"#2f6fdd"_s);
    }

    void svgOfDrawing_data()
    {
        QTest::addColumn<int>("background");
        QTest::addColumn<int>("strokes");
        QTest::newRow("empty, plain") << int(Background::Plain) << 0;
        QTest::newRow("empty, lines") << int(Background::Lines) << 0;
        QTest::newRow("one stroke") << int(Background::Plain) << 1;
        QTest::newRow("lines") << int(Background::Lines) << 3;
        QTest::newRow("grid") << int(Background::Grid) << 3;
        QTest::newRow("many") << int(Background::Grid) << 200;
    }
    void svgOfDrawing()
    {
        QFETCH(int, background);
        QFETCH(int, strokes);
        Drawing d;
        d.background = Background(background);
        for (int i = 0; i < strokes; ++i)
            d.strokes.append(wavyStroke(u"s%1"_s.arg(i), -40 + (i % 10) * 30, 20 + (i / 10) * 25, 15, i % 2));
        if (strokes > 1) {
            d.strokes[1].tool = Tool::Highlighter;
            d.strokes[1].opacity = 35;
            d.strokes[1].width = 4;
            d.strokes[1].color = u"#ffd43b"_s;
        }
        const QByteArray svg = toSvg(d);
        const SvgCheck check = checkSvg(svg);
        QVERIFY(check.wellFormed);
        QCOMPARE(check.rootName, u"svg"_s);
        QCOMPARE(check.rootNamespace, u"http://www.w3.org/2000/svg"_s);
        const int w = check.root.value(u"width"_s).toInt();
        const int h = check.root.value(u"height"_s).toInt();
        QCOMPARE(check.root.value(u"viewBox"_s).toString(), u"0 0 %1 %2"_s.arg(w).arg(h));
        if (strokes == 0) {
            QCOMPARE(w, EmptyWidth);
            QCOMPARE(h, EmptyHeight);
        } else {
            const QRectF b = bounds(d);
            QVERIFY(std::abs(w - (b.width() + 2 * SvgMargin)) <= 2);
            QVERIFY(std::abs(h - (b.height() + 2 * SvgMargin)) <= 2);
        }
        QCOMPARE(check.paths, strokes + (d.background == Background::Plain ? 0 : 1));
        QVERIFY(svg.contains("<rect x=\"0\" y=\"0\""));
        QVERIFY(svg.contains("<!-- svg-source:excalidraw -->"));
        QVERIFY(looksLikeExcalidrawSvg(svg.left(HeadBytes)));
        QCOMPARE(svg.contains("fill-opacity=\"0.35\""), strokes > 1);
        // The payload is one line between the markers (excalidraw.com's pattern takes no newline).
        const qsizetype start = svg.indexOf("<!-- payload-start -->");
        const qsizetype end = svg.indexOf("<!-- payload-end -->");
        QVERIFY(start > 0 && end > start);
        QVERIFY(!svg.mid(start, end - start).contains('\n'));
        // Read back as the same drawing.
        const std::optional<Drawing> back = readDrawing(svg);
        QVERIFY(back);
        QCOMPARE(back->background, d.background);
        QCOMPARE(back->strokes.size(), d.strokes.size());
        for (qsizetype i = 0; i < d.strokes.size(); ++i) {
            QCOMPARE(back->strokes.at(i).id, d.strokes.at(i).id);
            QCOMPARE(back->strokes.at(i).points.size(), d.strokes.at(i).points.size());
            QVERIFY(std::abs(back->strokes.at(i).points.last().x - d.strokes.at(i).points.last().x) < 0.01);
        }
        // Written again from what was read: the same file.
        QCOMPARE(toSvg(*back), svg);
    }

    void svgOfHugeDrawingIsBounded_data()
    {
        QTest::addColumn<double>("far");
        QTest::addColumn<int>("background");
        QTest::newRow("the limit, lines") << CoordinateLimit << int(Background::Lines);
        QTest::newRow("the limit, grid") << CoordinateLimit << int(Background::Grid);
        // Beyond what is read or drawn (a caller's own drawing): no endless loop, no huge file.
        QTest::newRow("1e9, lines") << 1e9 << int(Background::Lines);
        QTest::newRow("1e18, grid") << 1e18 << int(Background::Grid);
    }
    void svgOfHugeDrawingIsBounded()
    {
        QFETCH(double, far);
        QFETCH(int, background);
        Drawing d;
        d.background = Background(background);
        d.strokes.append(wavyStroke(u"źdźbło"_s, -far, -far, 5));
        d.strokes.append(wavyStroke(u"ćma"_s, far - 100, far - 100, 5));
        QElapsedTimer timer;
        timer.start();
        const QByteArray svg = toSvg(d);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
        QVERIFY2(svg.size() < 16 * 1024 * 1024, qPrintable(QString::number(svg.size())));
        const SvgCheck check = checkSvg(svg);
        QVERIFY(check.wellFormed);
        QVERIFY(check.root.value(u"width"_s).toInt() >= 1);
        // At the limit every line is there (2e6 / 32 of them, in one path).
        if (far == CoordinateLimit)
            QVERIFY(svg.count("H") >= qsizetype(2 * CoordinateLimit / LineSpacing));
    }

    void svgColoursAreSafe_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<QString>("written");
        QTest::newRow("hex") << u"#1F2430"_s << u"#1f2430"_s;
        QTest::newRow("short hex") << u"#abc"_s << u"#abc"_s;
        QTest::newRow("with alpha") << u"#11223344"_s << u"#11223344"_s;
        QTest::newRow("markup") << u"\"/><script>alert(1)</script>"_s << u"#1e1e1e"_s;
        QTest::newRow("named") << u"red"_s << u"#1e1e1e"_s;
        QTest::newRow("bad digit") << u"#12345g"_s << u"#1e1e1e"_s;
        QTest::newRow("polish") << u"#ąęćżźń"_s << u"#1e1e1e"_s;
    }
    void svgColoursAreSafe()
    {
        QFETCH(QString, color);
        QFETCH(QString, written);
        QCOMPARE(safeColor(color, u"#1e1e1e"_s), written);
        Drawing d;
        d.paper = color;
        d.strokes = {wavyStroke(u"x"_s, 0, 0, 6)};
        d.strokes[0].color = color;
        const QByteArray svg = toSvg(d);
        QVERIFY(checkSvg(svg).wellFormed);
        QVERIFY(!svg.contains("<script"));
    }

    void boundsCoverOutlines()
    {
        Drawing d;
        QVERIFY(bounds(d).isNull());
        d.strokes = {wavyStroke(u"a"_s, 0, 0, 10), wavyStroke(u"b"_s, 200, 300, 10)};
        const QRectF b = bounds(d);
        // (The outline reaches beyond the points by the pen's radius.)
        QVERIFY(b.left() < 0 && b.top() < 0);
        QVERIFY(b.right() > 236 && b.bottom() > 312);
        Stroke empty;
        QVERIFY(strokeBounds(empty).isNull());
        d.strokes.append(empty);
        QCOMPARE(bounds(d), b);
    }

    void randomInputNeverCrashes()
    {
        // Random bytes, and random damage to a valid file, as a sync conflict or a bad copy may give.
        QRandomGenerator random(20261027);
        Drawing d;
        d.strokes = {wavyStroke(u"a"_s, 0, 0, 30), wavyStroke(u"b"_s, 5, 9, 12, true)};
        const QByteArray svg = toSvg(d);
        for (int i = 0; i < 300; ++i) {
            QByteArray bytes = svg;
            const int damage = 1 + int(random.bounded(8));
            for (int k = 0; k < damage; ++k)
                bytes[qsizetype(random.bounded(quint32(bytes.size())))] = char(random.bounded(256));
            if (i % 3 == 0)
                bytes.truncate(qsizetype(random.bounded(quint32(bytes.size()))));
            const std::optional<Drawing> got = readDrawing(bytes);
            if (got)
                (void)toSvg(*got);
        }
        for (int i = 0; i < 100; ++i) {
            QByteArray bytes(qsizetype(random.bounded(2000)), Qt::Uninitialized);
            for (char &c : bytes)
                c = char(random.bounded(256));
            QVERIFY(!readDrawing(bytes) || true);
        }
    }
};

QTEST_GUILESS_MAIN(TestDrawing)
#include "tst_drawing.moc"
