// Tests of the drawing module: the outline of strokes (perfect-freehand), the Excalidraw scene
// and its SVG payload (Excalidraw's own fixtures and ones written like excalidraw.com writes them,
// tests/data/drawing/make-fixtures.py), the SVG Brulion writes, the colours of the dark theme
// (Excalidraw's dark mode, readable contrast) and malformed input.
// BRULION_UPDATE_GOLDEN=1 rewrites tests/data/drawing/auto_theme.svg (svgAutoGolden).
#include "drawing/drawing.h"
#include "drawing/excalidraw.h"
#include "drawing/palette.h"
#include "drawing/stroke.h"
#include "drawing/svgwrite.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
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

/// A scene of one freehand stroke of `points` points [0,0] (a hostile file).
QByteArray freedrawScene(qsizetype points)
{
    QByteArray out("{\"type\":\"excalidraw\",\"version\":2,\"elements\":[{\"type\":\"freedraw\",\"id\":\"ź\","
                   "\"x\":0,\"y\":0,\"points\":[");
    out.reserve(out.size() + points * 6 + 8);
    for (qsizetype i = 0; i < points; ++i)
        out.append(i ? ",[0,0]" : "[0,0]");
    out.append("]}]}");
    return out;
}

/// The scene embedded in an SVG Brulion wrote, as JSON.
QJsonObject embeddedScene(const QByteArray &svg)
{
    const std::optional<QByteArray> scene = svgPayload(svg);
    return scene ? QJsonDocument::fromJson(*scene).object() : QJsonObject();
}

/// The text of the SVG's only <style> element ("" none), and how many there are.
QString styleSheet(const QByteArray &svg, int *count = nullptr)
{
    const QString text = QString::fromUtf8(svg);
    if (count)
        *count = int(text.count(u"<style"_s));
    const qsizetype from = text.indexOf(u"<style>"_s);
    const qsizetype to = text.indexOf(u"</style>"_s);
    return from < 0 || to < from ? QString() : text.mid(from + 7, to - from - 7);
}

/// A drawing of an ink stroke and a highlighter stroke under it, on lined paper (svgAutoGolden).
Drawing twoStrokes()
{
    Drawing d;
    d.background = Background::Lines;
    // (Apart: the browser check finds the ink's own colour, not mixed with the highlighter's.)
    d.strokes = {wavyStroke(u"atrament"_s, 20, 40, 24), wavyStroke(u"zakreślacz"_s, 16, 84, 26)};
    d.strokes[1].tool = Tool::Highlighter;
    d.strokes[1].color = u"#ffd43b"_s;
    d.strokes[1].opacity = 40;
    d.strokes[1].width = 5;
    return d;
}

/// A scene of `count` empty elements (a hostile file).
QByteArray emptyElementsScene(qsizetype count)
{
    QByteArray out("{\"type\":\"excalidraw\",\"version\":2,\"elements\":[");
    out.reserve(out.size() + count * 3 + 4);
    for (qsizetype i = 0; i < count; ++i)
        out.append(i ? ",{}" : "{}");
    out.append("]}");
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
        QTest::newRow("a point far away")
                << QByteArray("{\"type\":\"freedraw\",\"x\":0,\"y\":0,\"points\":[[0,0],[0,1000001]]}") << false;
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

    void hostileScenesAreRefusedEarly_data()
    {
        QTest::addColumn<QByteArray>("scene");
        QTest::addColumn<bool>("readable");
        QTest::newRow("a long stroke, within the budget") << freedrawScene(100000) << true;
        QTest::newRow("one stroke of a million points") << freedrawScene(1000000) << false;
        QTest::newRow("six hundred thousand empty elements") << emptyElementsScene(600000) << false;
        // Beyond MaxSceneBytes: a few KB of zlib inflating to 40 MB.
        QByteArray padded = "{\"type\":\"excalidraw\",\"version\":2,\"elements\":[],\"source\":\"";
        padded += QByteArray(40 * 1024 * 1024, 'a');
        QTest::newRow("inflating beyond the scene limit") << (padded + "\"}") << false;
    }

    void hostileScenesAreRefusedEarly()
    {
        // A small file synced or imported into the notebook (an SVG with an embedded scene) that
        // would inflate and parse into gigabytes is refused before it is parsed.
        QFETCH(QByteArray, scene);
        QFETCH(bool, readable);
        const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\"><!-- svg-source:excalidraw -->"
                               "<!-- payload-type:application/vnd.excalidraw+json --><!-- payload-version:2 -->"
                               "<!-- payload-start -->"
                               + encodePayload(scene) + "<!-- payload-end --></svg>";
        QVERIFY(svg.size() < 2 * 1024 * 1024);
        QElapsedTimer timer;
        timer.start();
        ReadError error = ReadError::None;
        const std::optional<Drawing> d = readDrawing(svg, &error);
        QCOMPARE(d.has_value(), readable);
        if (readable)
            QCOMPARE(d->strokes.size(), 1);
        else
            QCOMPARE(error, ReadError::Corrupt);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }

    // --- The dark theme (palette.h) -------------------------------------------------------

    void darkColors_data()
    {
        // Excalidraw's dark mode (applyDarkModeFilter, computed with its reference algorithm).
        QTest::addColumn<QString>("color");
        QTest::addColumn<QString>("dark");
        QTest::newRow("white paper") << u"#ffffff"_s << u"#121212"_s;
        QTest::newRow("black") << u"#000000"_s << u"#ededed"_s;
        QTest::newRow("Brulion's ink") << u"#1f2430"_s << u"#caced8"_s;
        QTest::newRow("Excalidraw's ink") << u"#1e1e1e"_s << u"#d3d3d3"_s;
        QTest::newRow("blue") << u"#2f6fdd"_s << u"#6198f7"_s;
        QTest::newRow("red") << u"#e03131"_s << u"#ff8383"_s;
        QTest::newRow("green") << u"#2f9e44"_s << u"#39994b"_s;
        QTest::newRow("orange") << u"#f08c00"_s << u"#b86200"_s;
        QTest::newRow("violet") << u"#7048e8"_s << u"#af8dff"_s;
        QTest::newRow("rules") << u"#c9d3e0"_s << u"#323a45"_s;
        QTest::newRow("yellow") << u"#ffd43b"_s << u"#5f3a00"_s;
        QTest::newRow("zoom label") << u"#535a6a"_s << u"#9ba1af"_s;
        QTest::newRow("Excalidraw's paper") << u"#f8f9fa"_s << u"#161718"_s;
        QTest::newRow("short form") << u"#fff"_s << u"#121212"_s;
        QTest::newRow("upper case") << u"#1F2430"_s << u"#caced8"_s;
        QTest::newRow("spaces") << u" #1f2430 "_s << u"#caced8"_s;
        QTest::newRow("alpha kept") << u"#1f243080"_s << u"#caced880"_s;
        QTest::newRow("opaque alpha") << u"#1f2430ff"_s << u"#caced8"_s;
        QTest::newRow("named") << u"red"_s << u"brak"_s;
        QTest::newRow("too short") << u"#12"_s << u"brak"_s;
        QTest::newRow("empty") << QString() << u"brak"_s;
        QTest::newRow("bad digits") << u"#gggggg"_s << u"brak"_s;
        QTest::newRow("four digits") << u"#1f24"_s << u"brak"_s;
        QTest::newRow("url") << u"url(javascript:x)"_s << u"brak"_s;
        QTest::newRow("css") << u"#ffffff;fill:red"_s << u"brak"_s;
        QTest::newRow("polish") << u"#żółć12"_s << u"brak"_s;
    }
    void darkColors()
    {
        QFETCH(QString, color);
        QFETCH(QString, dark);
        QCOMPARE(darkColor(color, u"brak"_s), dark);
        const std::optional<Rgba> rgba = parseColor(color);
        QCOMPARE(rgba.has_value(), dark != u"brak");
        if (rgba)
            QCOMPARE(colorName(darkRgba(*rgba)), dark);
    }

    void colorNames()
    {
        QCOMPARE(parseColor(u"#abc"), (Rgba{0xaa, 0xbb, 0xcc, 255}));
        QCOMPARE(parseColor(u"#0a0B0c80"), (Rgba{10, 11, 12, 128}));
        QCOMPARE(colorName({10, 11, 12, 255}), u"#0a0b0c"_s);
        QCOMPARE(colorName({10, 11, 12, 0}), u"#0a0b0c00"_s);
        QCOMPARE(colorName({-5, 300, 12, 255}), u"#00ff0c"_s);
        // Light paper and rules stay as they are; malformed paper is white.
        QCOMPARE(displayPaper(u"#FFF9DB", false), u"#fff9db"_s);
        QCOMPARE(displayPaper(u"transparent", false), u"#ffffff"_s);
        QCOMPARE(displayPaper(u"transparent", true), u"#121212"_s);
        QCOMPARE(displayPaper(u"#fff9db", true), u"#1e1900"_s);
        QCOMPARE(displayRule(false), RuleColor.toString());
        QCOMPARE(displayRule(true), u"#323a45"_s);
        QVERIFY(!rendersDark(PaperTheme::Auto, false));
        QVERIFY(rendersDark(PaperTheme::Auto, true));
        QVERIFY(!rendersDark(PaperTheme::Light, true));
        QVERIFY(rendersDark(PaperTheme::Dark, false));
    }

    void displayInks_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<bool>("highlighter");
        QTest::addColumn<int>("opacity");
        QTest::addColumn<QString>("lightColor");
        QTest::addColumn<int>("lightOpacity");
        QTest::addColumn<QString>("darkColor");
        QTest::addColumn<int>("darkOpacity");
        QTest::newRow("ink") << u"#1f2430"_s << false << 100 << u"#1f2430"_s << 100 << u"#caced8"_s << 100;
        QTest::newRow("translucent ink") << u"#2f6fdd"_s << false << 60 << u"#2f6fdd"_s << 60 << u"#6198f7"_s << 60;
        QTest::newRow("highlighter") << u"#ffd43b"_s << true << 40 << u"#ffd43b"_s << 40 << u"#ffd43b"_s << 50;
        QTest::newRow("Excalidraw's 30") << u"#8ce99a"_s << true << 30 << u"#8ce99a"_s << 30 << u"#8ce99a"_s << 38;
        QTest::newRow("highlighter at 90") << u"#74c0fc"_s << true << 90 << u"#74c0fc"_s << 90 << u"#74c0fc"_s << 100;
        QTest::newRow("dark highlighter stays")
                << u"#1f2430"_s << true << 40 << u"#1f2430"_s << 40 << u"#1f2430"_s << 50;
        QTest::newRow("short form") << u"#ABC"_s << false << 100 << u"#aabbcc"_s << 100 << darkColor(u"#aabbcc") << 100;
        QTest::newRow("malformed") << u"czerwony"_s << false << 100 << u"#1e1e1e"_s << 100 << u"#d3d3d3"_s << 100;
        QTest::newRow("opacity out of range")
                << u"#1f2430"_s << false << 250 << u"#1f2430"_s << 100 << u"#caced8"_s << 100;
        QTest::newRow("negative opacity") << u"#ffd43b"_s << true << -20 << u"#ffd43b"_s << 0 << u"#ffd43b"_s << 0;
    }
    void displayInks()
    {
        QFETCH(QString, color);
        QFETCH(bool, highlighter);
        QFETCH(int, opacity);
        QFETCH(QString, lightColor);
        QFETCH(int, lightOpacity);
        QFETCH(QString, darkColor);
        QFETCH(int, darkOpacity);
        Stroke s;
        s.color = color;
        s.opacity = opacity;
        s.tool = highlighter ? Tool::Highlighter : Tool::Pen;
        QCOMPARE(displayInk(s, false), (Ink{lightColor, lightOpacity}));
        QCOMPARE(displayInk(s, true), (Ink{darkColor, darkOpacity}));
    }

    void darkInkContrast_data()
    {
        QTest::addColumn<QString>("color");
        QTest::addColumn<double>("minimum");
        // The pen's colours (DrawingView.qml) on the dark paper: readable (AA for large text,
        // the ink the default far above).
        for (const QString &c : {u"#1f2430"_s, u"#2f6fdd"_s, u"#e03131"_s, u"#2f9e44"_s, u"#f08c00"_s, u"#7048e8"_s})
            QTest::newRow(qPrintable(c)) << c << 4.2;
        // The lines: subtle, as on white paper (1.51:1).
        QTest::newRow("rules") << RuleColor.toString() << 1.5;
    }
    void darkInkContrast()
    {
        QFETCH(QString, color);
        QFETCH(double, minimum);
        Stroke s;
        s.color = color;
        const Rgba paper = *parseColor(displayPaper(DefaultPaper, true));
        const Rgba ink = color == RuleColor ? *parseColor(displayRule(true)) : *parseColor(displayInk(s, true).color);
        const double ratio = contrast(ink, paper);
        QVERIFY2(ratio >= minimum, qPrintable(u"%1 → %2: %3"_s.arg(color, colorName(ink)).arg(ratio)));
        // On white the same colours as drawn.
        const Rgba light = *parseColor(color == RuleColor ? displayRule(false) : displayInk(s, false).color);
        QVERIFY(contrast(light, *parseColor(DefaultPaper)) >= 1.5);
    }

    void highlighterInDark_data()
    {
        QTest::addColumn<QString>("color");
        for (const QString &c : {u"#ffd43b"_s, u"#8ce99a"_s, u"#faa2c1"_s, u"#74c0fc"_s, u"#ffa94d"_s})
            QTest::newRow(qPrintable(c)) << c;
    }
    void highlighterInDark()
    {
        // The highlighter's colours (DrawingView.qml) at 40 % show at 50 % in the dark: visible on
        // the dark paper, the dark ink readable on them. (Mapped as ink they would sink: < 3:1.)
        QFETCH(QString, color);
        Stroke s;
        s.color = color;
        s.tool = Tool::Highlighter;
        s.opacity = 40;
        const Ink ink = displayInk(s, true);
        QCOMPARE(ink.opacity, 50);
        const Rgba paper = *parseColor(displayPaper(DefaultPaper, true));
        const Rgba seen = over(*parseColor(ink.color), ink.opacity, paper);
        QVERIFY2(contrast(seen, paper) >= 3.2, qPrintable(u"%1: %2"_s.arg(colorName(seen)).arg(contrast(seen, paper))));
        const Rgba darkInk = *parseColor(darkColor(u"#1f2430"));
        QVERIFY2(contrast(darkInk, seen) >= 2.9, qPrintable(QString::number(contrast(darkInk, seen))));
        QVERIFY(contrast(over(darkRgba(*parseColor(color)), 40, paper), paper) < 3.2);
    }

    void contrastHelpers()
    {
        QCOMPARE(qRound(contrast({0, 0, 0, 255}, {255, 255, 255, 255}) * 100), 2100);
        QCOMPARE(contrast({18, 18, 18, 255}, {18, 18, 18, 255}), 1.0);
        QCOMPARE(over({255, 255, 255, 255}, 50, {0, 0, 0, 255}), (Rgba{128, 128, 128, 255}));
        QCOMPARE(over({255, 255, 255, 128}, 100, {0, 0, 0, 255}), (Rgba{128, 128, 128, 255}));
        QCOMPARE(over({255, 0, 0, 255}, 0, {1, 2, 3, 255}), (Rgba{1, 2, 3, 255}));
        QCOMPARE(over({255, 0, 0, 255}, 400, {1, 2, 3, 255}), (Rgba{255, 0, 0, 255}));
    }

    void paperThemeRoundTrip_data()
    {
        QTest::addColumn<QByteArray>("appState"); // the scene's appState JSON
        QTest::addColumn<int>("theme");
        QTest::newRow("none") << QByteArray("{}") << int(PaperTheme::Auto);
        QTest::newRow("light") << QByteArray(R"({"brulionTheme":"light"})") << int(PaperTheme::Light);
        QTest::newRow("dark") << QByteArray(R"({"brulionTheme":"dark"})") << int(PaperTheme::Dark);
        QTest::newRow("auto") << QByteArray(R"({"brulionTheme":"auto"})") << int(PaperTheme::Auto);
        QTest::newRow("unknown") << QByteArray(R"({"brulionTheme":"sepia"})") << int(PaperTheme::Auto);
        QTest::newRow("upper case") << QByteArray(R"({"brulionTheme":"DARK"})") << int(PaperTheme::Auto);
        QTest::newRow("number") << QByteArray(R"({"brulionTheme":42})") << int(PaperTheme::Auto);
        QTest::newRow("null") << QByteArray(R"({"brulionTheme":null})") << int(PaperTheme::Auto);
        QTest::newRow("object") << QByteArray(R"({"brulionTheme":{"ciemny":true}})") << int(PaperTheme::Auto);
        QTest::newRow("with others") << QByteArray(R"({"gridSize":20,"brulionBackground":"grid","zażółć":1,)"
                                                   R"("brulionTheme":"dark","theme":"light"})")
                                     << int(PaperTheme::Dark);
    }
    void paperThemeRoundTrip()
    {
        QFETCH(QByteArray, appState);
        QFETCH(int, theme);
        const QByteArray scene = R"({"type":"excalidraw","version":2,"elements":[],"appState":)" + appState + "}";
        std::optional<Drawing> d = parseScene(scene);
        QVERIFY(d);
        QCOMPARE(int(d->theme), theme);
        QCOMPARE(paperThemeFromName(paperThemeName(d->theme)), d->theme);
        // Written back: the key only for Light and Dark; every other key as it was.
        const QJsonObject before = QJsonDocument::fromJson(appState).object();
        for (const PaperTheme t : {PaperTheme::Auto, PaperTheme::Light, PaperTheme::Dark}) {
            d->theme = t;
            const QJsonObject written = QJsonDocument::fromJson(sceneJson(*d)).object().value(u"appState"_s).toObject();
            if (t == PaperTheme::Auto)
                QVERIFY(!written.contains(u"brulionTheme"_s));
            else
                QCOMPARE(written.value(u"brulionTheme"_s).toString(), paperThemeName(t));
            for (auto it = before.constBegin(); it != before.constEnd(); ++it) {
                if (it.key() != u"brulionTheme")
                    QCOMPARE(written.value(it.key()), it.value());
            }
            const std::optional<Drawing> back = parseScene(sceneJson(*d));
            QVERIFY(back);
            QCOMPARE(back->theme, t);
            QCOMPARE(back->background, d->background);
        }
    }

    void svgThemes_data()
    {
        QTest::addColumn<int>("theme");
        QTest::addColumn<QString>("inkColor");
        QTest::newRow("light") << int(PaperTheme::Light) << u"#1f2430"_s;
        QTest::newRow("dark") << int(PaperTheme::Dark) << u"#1f2430"_s;
        QTest::newRow("auto") << int(PaperTheme::Auto) << u"#1f2430"_s;
        // A scene from anywhere: the colour is no markup in the attributes nor in the CSS.
        QTest::newRow("auto, hostile colour") << int(PaperTheme::Auto) << u"#123;}</style><script>"_s;
        QTest::newRow("dark, hostile colour") << int(PaperTheme::Dark) << u"#fff\"/><script>"_s;
        QTest::newRow("auto, short form") << int(PaperTheme::Auto) << u"#E03"_s;
    }
    void svgThemes()
    {
        QFETCH(int, theme);
        QFETCH(QString, inkColor);
        Drawing d = twoStrokes();
        d.theme = PaperTheme(theme);
        d.strokes[0].color = inkColor;
        d.strokes.append(wavyStroke(u"druga linia ćwiczenia"_s, 30, 90, 10));
        d.strokes.append(wavyStroke(u"niebieska"_s, 30, 120, 10));
        d.strokes.last().color = u"#2f6fdd"_s;
        const QByteArray svg = toSvg(d);
        const SvgCheck check = checkSvg(svg);
        QVERIFY(check.wellFormed);
        QCOMPARE(check.paths, 5);
        QVERIFY(!svg.contains("<script"));
        QVERIFY(looksLikeExcalidrawSvg(svg.left(HeadBytes)));
        // The scene keeps the colours as drawn, and the choice.
        const QJsonObject scene = embeddedScene(svg);
        QCOMPARE(scene.value(u"elements"_s).toArray().at(0).toObject().value(u"strokeColor"_s).toString(), inkColor);
        QCOMPARE(scene.value(u"appState"_s).toObject().value(u"brulionTheme"_s).toString(),
                 d.theme == PaperTheme::Auto ? QString() : paperThemeName(d.theme));
        const std::optional<Drawing> back = readDrawing(svg);
        QVERIFY(back);
        QCOMPARE(back->theme, d.theme);
        QCOMPARE(toSvg(*back), svg);
        int styles = 0;
        const QString css = styleSheet(svg, &styles);
        const QString ink = safeColor(inkColor, u"#1e1e1e"_s);
        switch (d.theme) {
        case PaperTheme::Light:
            QCOMPARE(styles, 0);
            QVERIFY(!svg.contains("class="));
            QVERIFY(svg.contains("fill=\"#ffffff\""));
            QVERIFY(svg.contains(QByteArray("fill=\"" + ink.toLatin1() + '"')));
            QVERIFY(svg.contains("stroke=\"#c9d3e0\""));
            QVERIFY(svg.contains("fill=\"#ffd43b\" fill-opacity=\"0.4\""));
            break;
        case PaperTheme::Dark:
            // The dark picture in the attributes: every program shows it dark.
            QCOMPARE(styles, 0);
            QVERIFY(!svg.contains("class="));
            QVERIFY(svg.contains("<rect x=\"0\" y=\"0\""));
            QVERIFY(svg.contains("fill=\"#121212\""));
            QVERIFY(!svg.contains("fill=\"#ffffff\""));
            QVERIFY(svg.contains(QByteArray("fill=\"" + darkColor(ink).toLatin1() + '"')));
            QVERIFY(svg.contains("fill=\"#6198f7\""));
            QVERIFY(svg.contains("stroke=\"#323a45\""));
            QVERIFY(svg.contains("fill=\"#ffd43b\" fill-opacity=\"0.5\""));
            break;
        case PaperTheme::Auto: {
            // The light picture, and the dark one for browsers in one @media block right after the
            // metadata.
            QCOMPARE(styles, 1);
            QVERIFY(svg.contains("</metadata>\n<style>@media (prefers-color-scheme: dark){"));
            QVERIFY(svg.contains("fill=\"#ffffff\" class=\"p\""));
            QVERIFY(svg.contains(QByteArray("fill=\"" + ink.toLatin1() + "\" class=\"k0\"")));
            QVERIFY(svg.contains("fill=\"#ffd43b\" fill-opacity=\"0.4\" class=\"h0\""));
            // One class per pen colour: the second stroke's #1f2430 shares the first one's unless
            // that one has another colour.
            const bool shared = ink == u"#1f2430";
            QVERIFY(svg.contains(QByteArray("fill=\"#2f6fdd\" class=\"" + QByteArray(shared ? "k1" : "k2") + '"')));
            QVERIFY2(QRegularExpression(u"^@media \\(prefers-color-scheme: dark\\)\\{[@#.a-z0-9{}:;()\\- ]*\\}$"_s)
                             .match(css)
                             .hasMatch(),
                     qPrintable(css));
            QVERIFY(css.contains(u".p{fill:#121212}"_s));
            QVERIFY(css.contains(u".r{stroke:#323a45}"_s));
            QVERIFY(css.contains(u".k0{fill:"_s + darkColor(ink) + u'}'));
            QVERIFY(css.contains(u".h0{fill-opacity:0.5}"_s));
            // Every class used is defined, every class defined is used.
            QSet<QString> used, defined;
            for (const QRegularExpressionMatch &m :
                 QRegularExpression(u"class=\"([a-z0-9]+)\""_s).globalMatch(QString::fromUtf8(svg)))
                used.insert(m.captured(1));
            for (const QRegularExpressionMatch &m : QRegularExpression(u"\\.([a-z0-9]+)\\{"_s).globalMatch(css))
                defined.insert(m.captured(1));
            QCOMPARE(used, defined);
            QCOMPARE(used.size(), shared ? 5 : 6); // p, r, h0 and the pens' colours
            break;
        }
        }
    }

    void svgAutoGolden()
    {
        // The file a two-stroke drawing gives as the app writes it by default (Auto): byte for byte
        // (the browser check, tests/app/drawing/svg-theme-check.mjs, loads it).
        const QByteArray svg = toSvg(twoStrokes());
        const QString path = QStringLiteral(BRULION_TEST_DATA "/drawing/auto_theme.svg");
        if (qEnvironmentVariableIsSet("BRULION_UPDATE_GOLDEN")) {
            QSaveFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(svg);
            QVERIFY(out.commit());
            QSKIP("auto_theme.svg rewritten");
        }
        const QByteArray golden = fixture(u"auto_theme.svg"_s);
        if (svg != golden)
            qWarning().noquote() << svg;
        QCOMPARE(svg, golden);
        QCOMPARE(readDrawing(golden)->theme, PaperTheme::Auto);
    }

    void pictureRectMatchesSvgRoot_data()
    {
        QTest::addColumn<int>("strokes");
        QTest::addColumn<double>("x");
        QTest::newRow("empty") << 0 << 0.0;
        QTest::newRow("one") << 1 << 10.5;
        QTest::newRow("left of the origin") << 3 << -812.37;
        QTest::newRow("far") << 2 << 98765.4;
    }
    void pictureRectMatchesSvgRoot()
    {
        QFETCH(int, strokes);
        QFETCH(double, x);
        Drawing d;
        for (int i = 0; i < strokes; ++i)
            d.strokes.append(wavyStroke(u"ż%1"_s.arg(i), x + i * 17, x / 3 + i * 9, 12, i % 2));
        const QRectF r = pictureRect(d);
        const SvgCheck check = checkSvg(toSvg(d));
        QCOMPARE(int(r.width()), check.root.value(u"width"_s).toInt());
        QCOMPARE(int(r.height()), check.root.value(u"height"_s).toInt());
        QCOMPARE(r.width(), std::round(r.width()));
        if (strokes == 0) {
            QCOMPARE(r, QRectF(0, 0, EmptyWidth, EmptyHeight));
        } else {
            const QRectF b = bounds(d);
            QVERIFY(r.contains(b));
            QVERIFY(std::abs(r.left() - (b.left() - SvgMargin)) <= 1);
            QVERIFY(std::abs(r.bottom() - (b.bottom() + SvgMargin)) <= 1);
        }
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
