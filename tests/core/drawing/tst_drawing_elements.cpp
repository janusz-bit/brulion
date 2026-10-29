// Shapes, lines, arrows, text and pictures in drawings (core drawing: excalidraw.h elementFromJson /
// elementJson, geometry.h, edit.h, svgwrite.h): Excalidraw's elements read (every kind, malformed
// ones normalised or kept foreign), written back byte for byte while unchanged and from the model
// after an edit, new elements as excalidraw.com reads them, fractional indices, the geometry
// (rotation, corners, arrowheads, hatching), the hit tests of the tools, the edits and their undo
// diffs, and the SVG of shapes and text.
#include "drawing/drawing.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/geometry.h"
#include "drawing/merge.h"
#include "drawing/palette.h"
#include "drawing/svgwrite.h"

#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTest>
#include <QXmlStreamReader>

#include <cmath>
#include <memory>
#include <numbers>

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

Drawing shapesFixture()
{
    std::optional<Drawing> d = readDrawing(fixture(u"excalidraw_shapes.excalidraw"_s));
    if (!d)
        qFatal("shapes fixture unreadable");
    return *d;
}

const Stroke *find(const Drawing &d, const QString &id)
{
    for (const Stroke &s : d.strokes) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

QStringList ids(const Drawing &d)
{
    QStringList out;
    for (const Stroke &s : d.strokes)
        out.append(s.id);
    return out;
}

/// An edit stamp of fixed time, numbers and ids ("n1", "n2", …).
EditStamp testStamp(qint64 now = 1761600000000)
{
    auto counter = std::make_shared<int>(0);
    auto random = std::make_shared<quint32>(1000);
    EditStamp s;
    s.now = now;
    s.random = [random] { return ++*random; };
    s.newId = [counter] { return u"n"_s + QString::number(++*counter); };
    return s;
}

/// The measure of the tests: 0.55 em a character.
TextMetrics fakeMetrics()
{
    TextMetrics m;
    m.width = [](QStringView line, int, double size) { return 0.55 * size * double(line.size()); };
    return m;
}

QString typeName(int type)
{
    return elementTypeName(ElementType(type));
}

/// The fields Brulion models, as one line (the rows of readElements and malformedElements look for
/// parts of it).
QString describe(const Stroke &s)
{
    const auto heads = [](Arrowhead h) { return h == Arrowhead::None ? u"-"_s : arrowheadName(h); };
    QStringList parts{
            u"type=%1"_s.arg(elementTypeName(s.type)),
            // (Freehand lines, lines and arrows are their points.)
            hasPoints(s.type) ? u"box=-"_s
                              : u"box=%1,%2,%3,%4"_s.arg(s.box.x()).arg(s.box.y()).arg(s.box.width()).arg(s.box.height()),
            u"points=%1"_s.arg(s.points.size()),
            u"angle=%1"_s.arg(s.angle),
            u"color=%1"_s.arg(s.color),
            u"fill=%1"_s.arg(s.fill),
            u"fillStyle=%1"_s.arg(fillStyleName(s.fillStyle)),
            u"width=%1"_s.arg(s.width),
            u"strokeStyle=%1"_s.arg(strokeStyleName(s.strokeStyle)),
            u"roughness=%1"_s.arg(s.roughness),
            u"opacity=%1"_s.arg(s.opacity),
            u"roundness=%1"_s.arg(s.roundness),
            u"locked=%1"_s.arg(s.locked ? u"true"_s : u"false"_s),
            u"groups=%1"_s.arg(s.groupIds.join(u'+')),
            u"bound=%1"_s.arg(s.boundIds.join(u'+')),
            u"heads=%1/%2"_s.arg(heads(s.startArrowhead), heads(s.endArrowhead)),
            u"bindings=%1/%2"_s.arg(s.startBound, s.endBound),
            u"elbowed=%1"_s.arg(s.elbowed ? u"true"_s : u"false"_s),
            u"text=%1"_s.arg(s.text),
            u"fontSize=%1"_s.arg(s.fontSize),
            u"fontFamily=%1"_s.arg(s.fontFamily),
            u"align=%1/%2"_s.arg(textAlignName(s.textAlign), verticalAlignName(s.verticalAlign)),
            u"lineHeight=%1"_s.arg(s.lineHeight),
            u"container=%1"_s.arg(s.containerId),
            u"file=%1"_s.arg(s.fileId),
    };
    return parts.join(u' ');
}

Stroke rectangle(const QString &id, QRectF box)
{
    Stroke s;
    s.type = ElementType::Rectangle;
    s.id = id;
    s.box = box;
    s.width = 2;
    return s;
}

Stroke shapeOf(ElementType type, const QString &id, QRectF box)
{
    Stroke s = rectangle(id, box);
    s.type = type;
    return s;
}

Stroke lineOf(ElementType type, const QString &id, const QList<QPointF> &points)
{
    Stroke s;
    s.type = type;
    s.id = id;
    s.width = 2;
    for (const QPointF &p : points)
        s.points.append({p.x(), p.y(), 0.5});
    s.box = pointsRect(s.points);
    if (type == ElementType::Arrow)
        s.endArrowhead = Arrowhead::Arrow;
    return s;
}

Stroke freehand(const QString &id, double x0, double y0, int n, double dx = 10)
{
    Stroke s;
    s.id = id;
    for (int i = 0; i < n; ++i)
        s.points.append({x0 + i * dx, y0, 0.5});
    return s;
}

Stroke textOf(const QString &id, QRectF box, const QString &text)
{
    Stroke s;
    s.type = ElementType::Text;
    s.id = id;
    s.box = box;
    s.text = text;
    s.originalText = text;
    return s;
}

/// The same elements (ids, versions, geometry) in the same order.
bool sameElements(const QList<Stroke> &a, const QList<Stroke> &b)
{
    if (a.size() != b.size())
        return false;
    for (qsizetype i = 0; i < a.size(); ++i) {
        const Stroke &x = a.at(i);
        const Stroke &y = b.at(i);
        if (x.id != y.id || x.version != y.version || x.versionNonce != y.versionNonce || x.box != y.box
            || x.points != y.points || x.color != y.color || x.text != y.text || x.boundIds != y.boundIds
            || x.startBound != y.startBound || x.groupIds != y.groupIds)
            return false;
    }
    return true;
}

bool wellFormed(const QByteArray &xml)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd())
        r.readNext();
    return !r.hasError();
}

} // namespace

class TestDrawingElements : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void readElements_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<QStringList>("expected");
        const auto row = [](const char *name, const QString &id, const QStringList &parts) {
            QTest::newRow(name) << id << parts;
        };
        row("rounded rectangle with a label", u"prostokat"_s,
            {u"type=rectangle"_s, u"box=40,40,200,100"_s, u"roundness=3"_s, u"bound=etykieta+strzalka"_s,
             u"roughness=1"_s, u"width=2"_s, u"color=#1e1e1e"_s});
        row("its label", u"etykieta"_s,
            {u"type=text"_s, u"text=Zażółć gęślą jaźń"_s, u"container=prostokat"_s, u"align=center/middle"_s,
             u"fontFamily=5"_s, u"lineHeight=1.25"_s, u"fontSize=20"_s});
        row("hachure ellipse, dashed", u"elipsa"_s,
            {u"type=ellipse"_s, u"fill=#a5d8ff"_s, u"fillStyle=hachure"_s, u"strokeStyle=dashed"_s, u"roundness=0"_s});
        row("rounded diamond", u"romb"_s, {u"type=diamond"_s, u"roundness=2"_s, u"box=520,40,120,120"_s});
        row("curved line, dotted", u"linia"_s,
            {u"type=line"_s, u"points=3"_s, u"roundness=2"_s, u"strokeStyle=dotted"_s, u"heads=-/-"_s});
        row("arrow with bindings and a label", u"strzalka"_s,
            {u"type=arrow"_s, u"points=2"_s, u"heads=-/arrow"_s, u"bindings=prostokat/romb"_s, u"bound=podpis"_s,
             u"elbowed=false"_s});
        row("the arrow's label", u"podpis"_s, {u"type=text"_s, u"text=tak"_s, u"container=strzalka"_s});
        row("elbow arrow", u"lokiec"_s, {u"type=arrow"_s, u"points=3"_s, u"elbowed=true"_s, u"heads=-/triangle"_s});
        row("rotated rectangle", u"obrocony"_s, {u"type=rectangle"_s, u"angle=0.5"_s});
        row("two lines of text", u"tekst"_s,
            {u"type=text"_s, u"text=Dwie linie: ąćęłńóśźż\nI emoji 🙂"_s, u"fontFamily=2"_s, u"container="_s,
             u"align=left/top"_s});
        row("group: a rectangle", u"grupa1"_s, {u"groups=grupaA"_s});
        row("group: an ellipse", u"grupa2"_s, {u"type=ellipse"_s, u"groups=grupaA"_s});
        row("picture", u"obrazek"_s, {u"type=image"_s, u"file=plik-png"_s, u"box=440,320,40,40"_s});
        row("picture without its file", u"bez-pliku"_s, {u"type=image"_s, u"file=brak"_s});
        row("filled freehand line", u"wypelniona"_s, {u"type=freedraw"_s, u"fill=#ffc9c9"_s, u"points=4"_s});
        row("rotated freehand line", u"obrocona"_s, {u"type=freedraw"_s, u"angle=1"_s, u"points=12"_s});
    }
    void readElements()
    {
        QFETCH(QString, id);
        QFETCH(QStringList, expected);
        const Drawing d = shapesFixture();
        QVERIFY(editable(d));
        QCOMPARE(d.strokes.size(), 16);
        const Stroke *s = find(d, id);
        QVERIFY(s);
        const QString text = describe(*s);
        for (const QString &part : expected)
            QVERIFY2(text.contains(part), qPrintable(part + u" not in "_s + text));
        // The points of lines and arrows are in scene coordinates.
        if (id == u"strzalka") {
            QCOMPARE(s->points.first(), (StrokePoint{240, 90, 0.5}));
            QCOMPARE(s->points.last(), (StrokePoint{520, 100, 0.5}));
        }
        // The SVG fixture holds the same scene.
        const std::optional<Drawing> svg = readDrawing(fixture(u"excalidraw_shapes.svg"_s));
        QVERIFY(svg);
        QCOMPARE(describe(*find(*svg, id)), text);
    }

    void aFrameKeepsTheDrawingReadOnly()
    {
        const std::optional<Drawing> d = readDrawing(fixture(u"excalidraw_frame.svg"_s));
        QVERIFY(d);
        QVERIFY(!editable(*d));
        QCOMPARE(d->foreign.size(), 1);
        QCOMPARE(d->foreign.at(0).toObject().value(u"type"_s).toString(), u"frame"_s);
        // A frame that was deleted is no frame: editable.
        QByteArray scene = R"({"type":"excalidraw","elements":[{"type":"frame","x":0,"y":0,"isDeleted":true},
                              {"type":"rectangle","id":"r","x":1,"y":2,"width":3,"height":4}]})";
        const std::optional<Drawing> deleted = parseScene(scene);
        QVERIFY(deleted && editable(*deleted) && deleted->strokes.size() == 1);
    }

    void malformedElements_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<bool>("drawn");
        QTest::addColumn<QString>("expected");
        QTest::newRow("text that is a number") << QByteArray(R"({"type":"text","x":0,"y":0,"text":5})") << false
                                               << QString();
        QTest::newRow("font size of text") << QByteArray(R"({"type":"text","x":0,"y":0,"text":"ą","fontSize":"NaN"})")
                                           << true << u"fontSize=20"_s;
        QTest::newRow("huge font size") << QByteArray(R"({"type":"text","x":0,"y":0,"text":"ą","fontSize":1e9})")
                                        << true << u"fontSize=1000"_s;
        QTest::newRow("tiny line height")
                << QByteArray(R"({"type":"text","x":0,"y":0,"text":"ą","lineHeight":0.01})") << true
                << u"lineHeight=1.25"_s;
        QTest::newRow("line of one point") << QByteArray(R"({"type":"line","x":0,"y":0,"points":[[0,0]]})") << false
                                           << QString();
        QTest::newRow("point of text") << QByteArray(R"({"type":"arrow","x":0,"y":0,"points":[[0,"a"],[1,1]]})")
                                       << false << QString();
        QTest::newRow("arrowhead 42")
                << QByteArray(R"({"type":"arrow","x":0,"y":0,"points":[[0,0],[9,9]],"endArrowhead":42})") << true
                << u"heads=-/arrow"_s;
        QTest::newRow("arrowhead of the future") << QByteArray(
                R"({"type":"arrow","x":0,"y":0,"points":[[0,0],[9,9]],"startArrowhead":"crowfoot_one"})")
                                                 << true << u"heads=arrow/-"_s;
        QTest::newRow("boundElements a string")
                << QByteArray(R"({"type":"rectangle","x":0,"y":0,"width":5,"height":5,"boundElements":"x"})") << true
                << u"bound= "_s;
        QTest::newRow("groupIds an object")
                << QByteArray(R"({"type":"rectangle","x":0,"y":0,"width":5,"height":5,"groupIds":{}})") << true
                << u"groups= "_s;
        QTest::newRow("image without a file") << QByteArray(R"({"type":"image","x":0,"y":0,"width":5,"height":5})")
                                              << true << u"file="_s;
        QTest::newRow("far away") << QByteArray(R"({"type":"ellipse","x":2e6,"y":0,"width":5,"height":5})") << false
                                  << QString();
        QTest::newRow("negative width")
                << QByteArray(R"({"type":"rectangle","x":100,"y":10,"width":-50,"height":20})") << true
                << u"box=50,10,50,20"_s;
        QTest::newRow("opacity over 100")
                << QByteArray(R"({"type":"diamond","x":0,"y":0,"width":5,"height":5,"opacity":400})") << true
                << u"opacity=100"_s;
        QTest::newRow("stroke width 0") << QByteArray(R"({"type":"diamond","x":0,"y":0,"strokeWidth":0})") << true
                                        << u"width=0.05"_s;
        QTest::newRow("colour a number") << QByteArray(R"({"type":"ellipse","x":0,"y":0,"strokeColor":7})") << true
                                         << u"color=#1e1e1e"_s;
        QTest::newRow("roundness a string")
                << QByteArray(R"({"type":"rectangle","x":0,"y":0,"roundness":"round"})") << true << u"roundness=0"_s;
        QTest::newRow("no x") << QByteArray(R"({"type":"rectangle","y":0})") << false << QString();
    }
    void malformedElements()
    {
        QFETCH(QByteArray, json);
        QFETCH(bool, drawn);
        QFETCH(QString, expected);
        const QByteArray scene = "{\"type\":\"excalidraw\",\"elements\":[" + json + "]}";
        const std::optional<Drawing> d = parseScene(scene);
        QVERIFY(d);
        QCOMPARE(d->strokes.size(), drawn ? 1 : 0);
        QCOMPARE(d->foreign.size(), drawn ? 0 : 1);
        if (drawn) {
            const QString text = describe(d->strokes.first()) + u' ';
            QVERIFY2(text.contains(expected), qPrintable(text));
            // Drawn and written without trouble.
            QVERIFY(wellFormed(toSvg(*d)));
            QVERIFY(parseScene(sceneJson(*d)));
        }
    }

    void writeUnchangedIsByteIdentical()
    {
        const QByteArray bytes = fixture(u"excalidraw_shapes.excalidraw"_s);
        const QJsonArray raw = QJsonDocument::fromJson(bytes).object().value(u"elements"_s).toArray();
        Drawing d = shapesFixture();
        // Fields Brulion does not know survive.
        Stroke &r = d.strokes.first();
        r.element.insert(u"zażółć"_s, 1);
        r.element.insert(u"customData"_s, QJsonObject{{u"własne"_s, true}});
        r.element.insert(u"link"_s, u"https://example.org/ą"_s);
        const QByteArray json = sceneJson(d);
        const std::optional<Drawing> again = parseScene(json);
        QVERIFY(again);
        QCOMPARE(again->strokes.size(), d.strokes.size());
        for (qsizetype i = 0; i < d.strokes.size(); ++i)
            QCOMPARE(again->strokes.at(i).element, d.strokes.at(i).element);
        for (qsizetype i = 1; i < raw.size(); ++i)
            QCOMPARE(again->strokes.at(i).element, raw.at(i).toObject());
        QCOMPARE(again->strokes.first().element.value(u"zażółć"_s).toInt(), 1);
        QCOMPARE(sceneJson(*again), json);
        // The files of the pictures are kept.
        QVERIFY(again->files.contains(u"plik-png"_s));
    }

    void writeChangedBumpsVersion()
    {
        Drawing d = shapesFixture();
        const Stroke before = *find(d, u"elipsa"_s);
        EditStamp stamp = testStamp(1761700000000);
        stamp.random = [] { return quint32(42); };
        moveElements(d, {u"elipsa"_s}, QPointF(10, -5), stamp);
        const Stroke &after = *find(d, u"elipsa"_s);
        QCOMPARE(after.version, before.version + 1);
        QCOMPARE(after.versionNonce, 42);
        QCOMPARE(after.updated, qint64(1761700000000));
        const QJsonObject e = elementJson(after);
        QCOMPARE(e.value(u"x"_s).toDouble(), 310.0);
        QCOMPARE(e.value(u"y"_s).toDouble(), 35.0);
        QCOMPARE(e.value(u"version"_s).toInt(), before.version + 1);
        QCOMPARE(e.value(u"versionNonce"_s).toInt(), 42);
        QCOMPARE(e.value(u"updated"_s).toDouble(), 1761700000000.0);
        // What Brulion does not change, as read: rough strokes stay rough in excalidraw.com.
        for (const QString &key : {u"roughness"_s, u"seed"_s, u"index"_s, u"strokeStyle"_s, u"fillStyle"_s,
                                   u"backgroundColor"_s, u"frameId"_s, u"link"_s})
            QCOMPARE(e.value(key), before.element.value(key));
        // Read back: the same element, moved.
        const std::optional<Drawing> back = parseScene(sceneJson(d));
        QVERIFY(back);
        QCOMPARE(find(*back, u"elipsa"_s)->box, QRectF(310, 35, 160, 100));
        // A moved arrow: its points relative to its new x, y.
        moveElements(d, {u"linia"_s}, QPointF(-40, 0), stamp);
        const QJsonObject line = elementJson(*find(d, u"linia"_s));
        QCOMPARE(line.value(u"x"_s).toDouble(), 0.0);
        QCOMPARE(line.value(u"points"_s).toArray().at(1).toArray(), (QJsonArray{80, -40}));
        QCOMPARE(line.value(u"width"_s).toDouble(), 160.0);
        QCOMPARE(line.value(u"height"_s).toDouble(), 40.0);
    }

    void newElementsAreExcalidrawElements_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QStringList>("keys");
        const QStringList shape;
        const QStringList linear{u"points"_s,          u"lastCommittedPoint"_s, u"startBinding"_s, u"endBinding"_s,
                                 u"startArrowhead"_s, u"endArrowhead"_s};
        QTest::newRow("rectangle") << int(ElementType::Rectangle) << shape;
        QTest::newRow("ellipse") << int(ElementType::Ellipse) << shape;
        QTest::newRow("diamond") << int(ElementType::Diamond) << shape;
        QTest::newRow("line") << int(ElementType::Line) << (linear + QStringList{u"polygon"_s});
        QTest::newRow("arrow") << int(ElementType::Arrow) << (linear + QStringList{u"elbowed"_s});
        QTest::newRow("text") << int(ElementType::Text)
                              << QStringList{u"text"_s,          u"fontSize"_s,  u"fontFamily"_s, u"textAlign"_s,
                                             u"verticalAlign"_s, u"containerId"_s, u"originalText"_s,
                                             u"autoResize"_s,    u"lineHeight"_s};
        QTest::newRow("freedraw") << int(ElementType::Freedraw)
                                  << QStringList{u"points"_s, u"pressures"_s, u"simulatePressure"_s,
                                                 u"lastCommittedPoint"_s};
    }
    void newElementsAreExcalidrawElements()
    {
        QFETCH(int, type);
        QFETCH(QStringList, keys);
        const EditStamp stamp = testStamp();
        Stroke s;
        const ElementType t = ElementType(type);
        if (t == ElementType::Text)
            s = newText(QPointF(10, 20), u"Ćma"_s, TextStyle{}, fakeMetrics(), stamp);
        else if (t == ElementType::Freedraw)
            s = freehand(u"f"_s, 0, 0, 5);
        else
            s = newShape(t, QPointF(10, 20), QPointF(110, 80), false, ShapeStyle{u"#2f6fdd"_s, 2, true}, stamp);
        Drawing d;
        d.strokes = {s};
        const QJsonObject e = QJsonDocument::fromJson(sceneJson(d)).object().value(u"elements"_s).toArray().at(0).toObject();
        // What Excalidraw's restore.ts reads of every element.
        const QStringList common{u"id"_s,       u"type"_s,          u"x"_s,           u"y"_s,         u"width"_s,
                                 u"height"_s,   u"angle"_s,         u"strokeColor"_s, u"backgroundColor"_s,
                                 u"fillStyle"_s, u"strokeWidth"_s,  u"strokeStyle"_s, u"roughness"_s,
                                 u"opacity"_s,  u"groupIds"_s,      u"frameId"_s,     u"roundness"_s,
                                 u"seed"_s,     u"version"_s,       u"versionNonce"_s, u"isDeleted"_s,
                                 u"boundElements"_s, u"updated"_s,  u"link"_s,        u"locked"_s};
        for (const QString &key : common + keys)
            QVERIFY2(e.contains(key), qPrintable(key));
        QCOMPARE(e.value(u"type"_s).toString(), typeName(type));
        QCOMPARE(e.value(u"roughness"_s).toInt(), 0);
        QVERIFY(!e.contains(u"index"_s));
        QCOMPARE(e.value(u"isDeleted"_s).toBool(), false);
        if (t == ElementType::Rectangle)
            QCOMPARE(e.value(u"roundness"_s).toObject().value(u"type"_s).toInt(), 3);
        if (t == ElementType::Arrow) {
            QCOMPARE(e.value(u"endArrowhead"_s).toString(), u"arrow"_s);
            QVERIFY(e.value(u"startBinding"_s).isNull()); // Brulion never binds new arrows
        }
        if (t == ElementType::Text) {
            QCOMPARE(e.value(u"fontFamily"_s).toInt(), 2);
            QCOMPARE(e.value(u"lineHeight"_s).toDouble(), NewTextLineHeight);
            QVERIFY(e.value(u"containerId"_s).isNull());
        }
        // And read back as it was.
        const std::optional<Drawing> back = parseScene(sceneJson(d));
        QVERIFY(back);
        QCOMPARE(describe(back->strokes.first()), describe(s));
    }

    void indexKeptOrDropped_data()
    {
        QTest::addColumn<QString>("edit");
        QTest::addColumn<bool>("kept");
        QTest::newRow("unchanged") << u"none"_s << true;
        QTest::newRow("moved") << u"move"_s << true;
        QTest::newRow("a new stroke on top") << u"add"_s << true;
        QTest::newRow("to the front") << u"front"_s << false;
        QTest::newRow("one step back") << u"backward"_s << false;
    }
    void indexKeptOrDropped()
    {
        QFETCH(QString, edit);
        QFETCH(bool, kept);
        Drawing d = shapesFixture();
        const EditStamp stamp = testStamp();
        if (edit == u"move")
            moveElements(d, {u"romb"_s}, QPointF(5, 5), stamp);
        else if (edit == u"add")
            d.strokes.append(freehand(u"nowa"_s, 0, 600, 4));
        else if (edit == u"front")
            reorderElements(d, {u"elipsa"_s}, Reorder::ToFront, stamp);
        else if (edit == u"backward")
            reorderElements(d, {u"romb"_s}, Reorder::Backward, stamp);
        const QJsonArray elements = QJsonDocument::fromJson(sceneJson(d)).object().value(u"elements"_s).toArray();
        int withIndex = 0;
        for (const QJsonValue &v : elements)
            withIndex += v.toObject().contains(u"index"_s) ? 1 : 0;
        QCOMPARE(withIndex, kept ? 16 : 0);
    }

    void cornerRadius_data()
    {
        QTest::addColumn<QRectF>("box");
        QTest::addColumn<int>("roundness");
        QTest::addColumn<double>("radius");
        QTest::newRow("adaptive, small") << QRectF(0, 0, 100, 100) << 3 << 25.0;
        QTest::newRow("adaptive, large") << QRectF(0, 0, 400, 300) << 3 << 32.0;
        QTest::newRow("adaptive at the cutoff") << QRectF(0, 0, 128, 500) << 3 << 32.0;
        QTest::newRow("proportional") << QRectF(0, 0, 100, 60) << 2 << 15.0;
        QTest::newRow("legacy") << QRectF(0, 0, 40, 400) << 1 << 10.0;
        QTest::newRow("sharp") << QRectF(0, 0, 100, 100) << 0 << 0.0;
    }
    void cornerRadius()
    {
        QFETCH(QRectF, box);
        QFETCH(int, roundness);
        QFETCH(double, radius);
        Stroke s = rectangle(u"r"_s, box);
        s.roundness = roundness;
        QCOMPARE(brulion::drawing::cornerRadius(s), radius);
    }

    void rotatedBounds()
    {
        Stroke s = rectangle(u"r"_s, QRectF(0, 0, 100, 50));
        s.angle = std::numbers::pi / 2;
        const QRectF b = elementBounds(s);
        // About the centre (50, 25): 50 wide, 100 high; half the stroke around.
        QVERIFY(std::abs(b.left() - 24) < 1e-6 && std::abs(b.right() - 76) < 1e-6);
        QVERIFY(std::abs(b.top() + 26) < 1e-6 && std::abs(b.bottom() - 76) < 1e-6);
        // Text and pictures: their rotated box.
        Stroke t = textOf(u"t"_s, QRectF(0, 0, 100, 20), u"ą"_s);
        t.angle = std::numbers::pi / 2;
        const QRectF tb = elementBounds(t);
        QVERIFY(std::abs(tb.left() - 40) < 1e-6 && std::abs(tb.top() + 40) < 1e-6);
        QVERIFY(std::abs(tb.width() - 20) < 1e-6 && std::abs(tb.height() - 100) < 1e-6);
    }

    void arrowheads_data()
    {
        QTest::addColumn<double>("length");
        QTest::addColumn<double>("size");
        QTest::addColumn<double>("degrees");
        QTest::newRow("long arrow") << 100.0 << 25.0 << 20.0;
        QTest::newRow("short arrow: half its length, wider") << 10.0 << 5.0 << 30.0;
    }
    void arrowheads()
    {
        QFETCH(double, length);
        QFETCH(double, size);
        QFETCH(double, degrees);
        const Stroke s = lineOf(ElementType::Arrow, u"a"_s, {{0, 0}, {length, 0}});
        const ElementShape shape = elementShape(s);
        QCOMPARE(shape.heads.size(), 1);
        QCOMPARE(shape.headFilled.first(), false);
        const Path &h = shape.heads.first();
        QCOMPARE(h.size(), 3);
        QCOMPARE(h.at(1).a, QPointF(length, 0)); // the tip at the end
        for (const QPointF wing : {h.at(0).a, h.at(2).a}) {
            QVERIFY(std::abs(std::hypot(wing.x() - length, wing.y()) - size) < 1e-6);
            const double angle = std::atan2(std::abs(wing.y()), length - wing.x()) * 180 / std::numbers::pi;
            QVERIFY2(std::abs(angle - degrees) < 1e-6, qPrintable(QString::number(angle)));
        }
        QVERIFY(h.at(0).a.y() * h.at(2).a.y() < 0); // one wing either side
        // Bounds hold the head.
        QVERIFY(shape.bounds.contains(h.at(0).a) && shape.bounds.contains(h.at(2).a));
    }

    void otherArrowheads_data()
    {
        QTest::addColumn<int>("head");
        QTest::addColumn<int>("ops");
        QTest::addColumn<bool>("filled");
        QTest::newRow("triangle") << int(Arrowhead::Triangle) << 4 << true;
        QTest::newRow("triangle outline") << int(Arrowhead::TriangleOutline) << 4 << false;
        QTest::newRow("bar") << int(Arrowhead::Bar) << 2 << false;
        QTest::newRow("diamond") << int(Arrowhead::Diamond) << 5 << true;
        QTest::newRow("circle") << int(Arrowhead::Circle) << 6 << true;
        QTest::newRow("dot") << int(Arrowhead::Dot) << 6 << true;
        QTest::newRow("circle outline") << int(Arrowhead::CircleOutline) << 6 << false;
        QTest::newRow("unknown: an arrow") << int(Arrowhead::Other) << 3 << false;
    }
    void otherArrowheads()
    {
        QFETCH(int, head);
        QFETCH(int, ops);
        QFETCH(bool, filled);
        bool full = !filled;
        const Path p = arrowhead(Arrowhead(head), QPointF(100, 0), QPointF(0, 0), 2, &full);
        QCOMPARE(p.size(), ops);
        QCOMPARE(full, filled);
        QVERIFY(arrowhead(Arrowhead(head), QPointF(5, 5), QPointF(5, 5), 2).isEmpty()); // no length: none
    }

    void hatching_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("style");
        QTest::addColumn<int>("minLines");
        QTest::addColumn<int>("maxLines");
        QTest::newRow("square, hachure") << int(ElementType::Rectangle) << int(FillStyle::Hachure) << 17 << 19;
        QTest::newRow("square, cross-hatch") << int(ElementType::Rectangle) << int(FillStyle::CrossHatch) << 34 << 38;
        QTest::newRow("square, zigzag (as hachure)") << int(ElementType::Rectangle) << int(FillStyle::Zigzag) << 17 << 19;
        QTest::newRow("square, solid") << int(ElementType::Rectangle) << int(FillStyle::Solid) << 0 << 0;
        QTest::newRow("diamond") << int(ElementType::Diamond) << int(FillStyle::Hachure) << 8 << 19;
        QTest::newRow("ellipse") << int(ElementType::Ellipse) << int(FillStyle::Hachure) << 10 << 19;
    }
    void hatching()
    {
        QFETCH(int, type);
        QFETCH(int, style);
        QFETCH(int, minLines);
        QFETCH(int, maxLines);
        Stroke s = shapeOf(ElementType(type), u"s"_s, QRectF(0, 0, 100, 100));
        s.fill = u"#ffc9c9"_s;
        s.fillStyle = FillStyle(style);
        const ElementShape shape = elementShape(s);
        QVERIFY2(shape.hatch.size() >= minLines && shape.hatch.size() <= maxLines,
                 qPrintable(QString::number(shape.hatch.size())));
        QCOMPARE(shape.hatchWidth, 1.0);
        QCOMPARE(shape.area.isEmpty(), style != int(FillStyle::Solid));
        for (const QLineF &l : shape.hatch) {
            for (const QPointF p : {l.p1(), l.p2()}) {
                if (ElementType(type) == ElementType::Rectangle) {
                    // On the border.
                    const bool onBorder = std::abs(p.x()) < 1e-6 || std::abs(p.x() - 100) < 1e-6
                                          || std::abs(p.y()) < 1e-6 || std::abs(p.y() - 100) < 1e-6;
                    QVERIFY2(onBorder, qPrintable(u"%1,%2"_s.arg(p.x()).arg(p.y())));
                } else if (ElementType(type) == ElementType::Ellipse) {
                    const double r = std::hypot(p.x() - 50, p.y() - 50);
                    QVERIFY(std::abs(r - 50) < 1e-6);
                } else {
                    QVERIFY(std::abs(std::abs(p.x() - 50) + std::abs(p.y() - 50) - 50) < 1e-6);
                }
            }
        }
    }

    void ellipseWithinItsBox()
    {
        const Stroke s = shapeOf(ElementType::Ellipse, u"e"_s, QRectF(10, 20, 160, 60));
        for (const QList<QPointF> &line : flatten(elementShape(s).outline, 0.1)) {
            for (const QPointF &p : line)
                QVERIFY(QRectF(10, 20, 160, 60).adjusted(-0.01, -0.01, 0.01, 0.01).contains(p));
        }
    }

    void textLinesAreFontIndependent()
    {
        Stroke t = textOf(u"t"_s, QRectF(10, 20, 100, 46), u"Ąę\r\nżółw"_s);
        t.lineHeight = 1.15;
        t.textAlign = TextAlign::Center;
        const QList<TextLine> lines = textLines(t);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0).text, u"Ąę"_s);
        QCOMPARE(lines.at(1).text, u"żółw"_s);
        QVERIFY(std::abs(lines.at(0).baseline.y() - (20 + 0.5 * 23 + 7)) < 1e-9);
        QVERIFY(std::abs(lines.at(1).baseline.y() - lines.at(0).baseline.y() - 23) < 1e-9);
        QCOMPARE(lines.at(0).baseline.x(), 60.0);
    }

    void hitTest_data()
    {
        QTest::addColumn<QString>("what");
        QTest::addColumn<QPointF>("at");
        QTest::addColumn<double>("tolerance");
        QTest::addColumn<bool>("hit");
        QTest::newRow("unfilled rectangle: inside") << u"rect"_s << QPointF(50, 50) << 8.0 << false;
        QTest::newRow("unfilled rectangle: on its edge") << u"rect"_s << QPointF(100, 50) << 8.0 << true;
        QTest::newRow("unfilled rectangle: 5 px off") << u"rect"_s << QPointF(105, 50) << 8.0 << true;
        QTest::newRow("unfilled rectangle: far") << u"rect"_s << QPointF(120, 50) << 8.0 << false;
        QTest::newRow("filled rectangle: inside") << u"filled"_s << QPointF(50, 50) << 8.0 << true;
        QTest::newRow("filled ellipse: its box's corner") << u"ellipse"_s << QPointF(3, 3) << 2.0 << false;
        QTest::newRow("filled ellipse: inside") << u"ellipse"_s << QPointF(50, 50) << 2.0 << true;
        QTest::newRow("rotated text: in its turned box") << u"text"_s << QPointF(50, 50) << 0.0 << true;
        QTest::newRow("rotated text: where it was") << u"text"_s << QPointF(90, 10) << 0.0 << false;
        QTest::newRow("arrow: near its head") << u"arrow"_s << QPointF(90, 4.5) << 2.0 << true;
        QTest::newRow("line: as far") << u"line"_s << QPointF(90, 4.5) << 2.0 << false;
        QTest::newRow("freehand line") << u"stroke"_s << QPointF(45, 3) << 2.0 << true;
        QTest::newRow("freehand line: far") << u"stroke"_s << QPointF(45, 30) << 2.0 << false;
        QTest::newRow("picture") << u"image"_s << QPointF(20, 20) << 0.0 << true;
        QTest::newRow("locked") << u"locked"_s << QPointF(50, 50) << 8.0 << false;
    }
    void hitTest()
    {
        QFETCH(QString, what);
        QFETCH(QPointF, at);
        QFETCH(double, tolerance);
        QFETCH(bool, hit);
        Stroke s;
        if (what == u"rect" || what == u"filled" || what == u"locked") {
            s = rectangle(u"r"_s, QRectF(0, 0, 100, 100));
            if (what != u"rect")
                s.fill = u"#ffec99"_s;
            s.locked = what == u"locked";
        } else if (what == u"ellipse") {
            s = shapeOf(ElementType::Ellipse, u"e"_s, QRectF(0, 0, 100, 100));
            s.fill = u"#ffec99"_s;
        } else if (what == u"text") {
            s = textOf(u"t"_s, QRectF(0, 0, 100, 20), u"Zażółć"_s);
            s.angle = std::numbers::pi / 2;
        } else if (what == u"arrow" || what == u"line") {
            s = lineOf(what == u"arrow" ? ElementType::Arrow : ElementType::Line, u"a"_s, {{0, 0}, {100, 0}});
        } else if (what == u"stroke") {
            s = freehand(u"f"_s, 0, 0, 10);
        } else if (what == u"image") {
            s = shapeOf(ElementType::Image, u"i"_s, QRectF(0, 0, 40, 40));
        }
        QCOMPARE(brulion::drawing::hitTest(s, at, tolerance), hit);
    }

    void eraserTouches()
    {
        const Stroke r = rectangle(u"r"_s, QRectF(0, 0, 100, 100));
        QVERIFY(touchesSegment(r, QPointF(-20, 50), QPointF(20, 50), 2)); // across its edge
        QVERIFY(!touchesSegment(r, QPointF(30, 50), QPointF(70, 50), 2)); // inside, unfilled
        Stroke filled = r;
        filled.fill = u"#a5d8ff"_s;
        QVERIFY(touchesSegment(filled, QPointF(30, 50), QPointF(70, 50), 2));
        Stroke locked = filled;
        locked.locked = true;
        QVERIFY(!touchesSegment(locked, QPointF(-20, 50), QPointF(20, 50), 2));
        const Stroke t = textOf(u"t"_s, QRectF(0, 0, 100, 20), u"Ćma"_s);
        QVERIFY(touchesSegment(t, QPointF(50, -10), QPointF(50, 30), 1));
        QVERIFY(!touchesSegment(t, QPointF(150, -10), QPointF(150, 30), 1));
    }

    void lassoSelects_data()
    {
        QTest::addColumn<QList<QPointF>>("lasso");
        QTest::addColumn<QStringList>("selected");
        const QList<QPointF> square{{-20, -30}, {160, -30}, {160, 40}, {-20, 40}};
        QTest::newRow("around two of three") << square << QStringList{u"a"_s, u"b"_s};
        QTest::newRow("half of the long one: taken") << QList<QPointF>{{345, -10}, {399, -10}, {399, 10}, {345, 10}}
                                                     << QStringList{u"d"_s};
        QTest::newRow("40 % of it: not") << QList<QPointF>{{355, -10}, {399, -10}, {399, 10}, {355, 10}}
                                         << QStringList{};
        // A bow-tie (self-intersecting, even-odd): its left wing holds "e", its top gap not "f".
        QTest::newRow("self-intersecting") << QList<QPointF>{{0, 100}, {100, 200}, {100, 100}, {0, 200}}
                                           << QStringList{u"e"_s};
        QTest::newRow("two points: nothing") << QList<QPointF>{{-1000, -1000}, {1000, 1000}} << QStringList{};
    }
    void lassoSelects()
    {
        QFETCH(QList<QPointF>, lasso);
        QFETCH(QStringList, selected);
        QList<Stroke> all{freehand(u"a"_s, 0, 0, 5), freehand(u"b"_s, 100, 0, 5), freehand(u"c"_s, 300, 0, 5),
                          freehand(u"d"_s, 400, 0, 10, -10)};
        // d: x 400 … 310 (10 points); "e" in the bow-tie's left wing, "f" in its top gap.
        all.append(shapeOf(ElementType::Rectangle, u"e"_s, QRectF(5, 140, 20, 20)));
        all.append(shapeOf(ElementType::Rectangle, u"f"_s, QRectF(40, 105, 20, 20)));
        Stroke locked = shapeOf(ElementType::Rectangle, u"g"_s, QRectF(0, 0, 10, 10));
        locked.locked = true;
        all.append(locked);
        QStringList got;
        for (const Stroke &s : all) {
            if (brulion::drawing::lassoSelects(s, lasso))
                got.append(s.id);
        }
        QCOMPARE(got, selected);
    }

    void moveFollowsLabelsAndArrows()
    {
        // R (with label L) — A1 → D; A2 → D; A3 an elbow arrow from R.
        Drawing d;
        Stroke r = rectangle(u"R"_s, QRectF(0, 0, 100, 50));
        r.boundIds = {u"L"_s, u"A1"_s, u"A3"_s};
        Stroke l = textOf(u"L"_s, QRectF(20, 15, 60, 20), u"Ala"_s);
        l.containerId = u"R"_s;
        Stroke a1 = lineOf(ElementType::Arrow, u"A1"_s, {{100, 25}, {200, 25}});
        a1.startBound = u"R"_s;
        a1.endBound = u"D"_s;
        Stroke dia = shapeOf(ElementType::Diamond, u"D"_s, QRectF(200, 0, 60, 50));
        dia.boundIds = {u"A1"_s, u"A2"_s};
        Stroke a2 = lineOf(ElementType::Arrow, u"A2"_s, {{100, 100}, {230, 50}});
        a2.endBound = u"D"_s;
        Stroke a3 = lineOf(ElementType::Arrow, u"A3"_s, {{50, 50}, {50, 80}, {150, 80}});
        a3.elbowed = true;
        a3.startBound = u"R"_s;
        d.strokes = {r, l, a1, dia, a2, a3};
        const EditStamp stamp = testStamp();
        Drawing one = d;
        const QStringList closure = selectionClosure(one, {u"R"_s});
        QCOMPARE(closure, (QStringList{u"R"_s, u"L"_s}));
        moveElements(one, QSet<QString>(closure.cbegin(), closure.cend()), QPointF(10, 5), stamp);
        QCOMPARE(find(one, u"R"_s)->box, QRectF(10, 5, 100, 50));
        QCOMPARE(find(one, u"L"_s)->box, QRectF(30, 20, 60, 20));
        // One end bound to what moved: that end moves.
        QCOMPARE(find(one, u"A1"_s)->points.first(), (StrokePoint{110, 30, 0.5}));
        QCOMPARE(find(one, u"A1"_s)->points.last(), (StrokePoint{200, 25, 0.5}));
        QCOMPARE(find(one, u"A1"_s)->version, 2);
        QCOMPARE(find(one, u"A2"_s)->version, 1); // not bound to what moved
        // The elbow arrow keeps its right angles.
        const QList<StrokePoint> &e = find(one, u"A3"_s)->points;
        QCOMPARE(e.at(0), (StrokePoint{60, 55, 0.5}));
        QCOMPARE(e.at(1), (StrokePoint{60, 80, 0.5}));
        QCOMPARE(e.at(2), (StrokePoint{150, 80, 0.5}));
        // Both ends bound to what moved: the arrow moves.
        Drawing both = d;
        moveElements(both, {u"R"_s, u"L"_s, u"D"_s}, QPointF(-10, 10), stamp);
        QCOMPARE(find(both, u"A1"_s)->points.first(), (StrokePoint{90, 35, 0.5}));
        QCOMPARE(find(both, u"A1"_s)->points.last(), (StrokePoint{190, 35, 0.5}));
        // Nothing leaves the paper's limit.
        Drawing far = d;
        moveElements(far, {u"D"_s}, QPointF(5e6, 0), stamp);
        QCOMPARE(find(far, u"D"_s)->box.right(), CoordinateLimit);
        // Locked elements stay.
        Drawing locked = d;
        locked.strokes[3].locked = true;
        moveElements(locked, {u"D"_s}, QPointF(5, 5), stamp);
        QCOMPARE(find(locked, u"D"_s)->box, QRectF(200, 0, 60, 50));
    }

    void duplicateWithGroupAndLabel()
    {
        Drawing d;
        Stroke r = rectangle(u"R"_s, QRectF(0, 0, 100, 50));
        r.boundIds = {u"L"_s, u"A"_s};
        r.groupIds = {u"G"_s};
        Stroke l = textOf(u"L"_s, QRectF(20, 15, 60, 20), u"Źrebak"_s);
        l.containerId = u"R"_s;
        Stroke e = shapeOf(ElementType::Ellipse, u"E"_s, QRectF(120, 0, 50, 50));
        e.groupIds = {u"G"_s};
        Stroke a = lineOf(ElementType::Arrow, u"A"_s, {{100, 25}, {300, 25}});
        a.startBound = u"R"_s;
        Stroke top = freehand(u"T"_s, 0, 200, 4);
        d.strokes = {r, l, e, a, top};
        const QStringList closure = selectionClosure(d, {u"E"_s});
        QCOMPARE(closure, (QStringList{u"R"_s, u"L"_s, u"E"_s})); // the whole group, the label
        const QStringList copies = duplicateElements(d, closure, QPointF(10, 10), testStamp());
        QCOMPARE(copies.size(), 3);
        QCOMPARE(ids(d), (QStringList{u"R"_s, u"L"_s, u"E"_s} + copies + QStringList{u"A"_s, u"T"_s}));
        const Stroke &rc = *find(d, copies.at(0));
        const Stroke &lc = *find(d, copies.at(1));
        const Stroke &ec = *find(d, copies.at(2));
        QCOMPARE(rc.box, QRectF(10, 10, 100, 50));
        QCOMPARE(lc.containerId, rc.id);
        QCOMPARE(rc.boundIds, QStringList{lc.id}); // the arrow was not copied
        QCOMPARE(rc.groupIds, ec.groupIds);
        QVERIFY(rc.groupIds.first() != u"G"_s);
        QCOMPARE(rc.version, 1);
        QVERIFY(rc.seed != r.seed);
        // Written as new elements (never the original's JSON).
        const std::optional<Drawing> back = parseScene(sceneJson(d));
        QVERIFY(back);
        QCOMPARE(ids(*back), ids(d));
        QCOMPARE(find(*back, lc.id)->containerId, rc.id);
    }

    void deleteAContainer()
    {
        Drawing d;
        Stroke r = rectangle(u"R"_s, QRectF(0, 0, 100, 50));
        r.boundIds = {u"L"_s, u"A"_s};
        Stroke l = textOf(u"L"_s, QRectF(20, 15, 60, 20), u"Ala"_s);
        l.containerId = u"R"_s;
        Stroke a = lineOf(ElementType::Arrow, u"A"_s, {{100, 25}, {300, 25}});
        a.startBound = u"R"_s;
        d.strokes = {r, l, a};
        Drawing gone = d;
        deleteElements(gone, {u"R"_s}, testStamp());
        QCOMPARE(ids(gone), QStringList{u"A"_s});
        QCOMPARE(gone.strokes.first().startBound, QString());
        QCOMPARE(gone.strokes.first().version, 2);
        QVERIFY(elementJson(gone.strokes.first()).value(u"startBinding"_s).isNull());
        // A label alone: its shape forgets it.
        Drawing label = d;
        deleteElements(label, {u"L"_s}, testStamp());
        QCOMPARE(ids(label), (QStringList{u"R"_s, u"A"_s}));
        QCOMPARE(label.strokes.first().boundIds, QStringList{u"A"_s});
    }

    void recolourSkipsLocked()
    {
        Drawing d;
        d.strokes = {rectangle(u"a"_s, QRectF(0, 0, 10, 10)), rectangle(u"b"_s, QRectF(20, 0, 10, 10)),
                     freehand(u"h"_s, 0, 40, 3)};
        d.strokes[1].locked = true;
        d.strokes[2].tool = Tool::Highlighter;
        d.strokes[2].opacity = 40;
        recolorElements(d, {u"a"_s, u"b"_s, u"h"_s}, u"#e03131"_s, testStamp());
        QCOMPARE(d.strokes[0].color, u"#e03131"_s);
        QCOMPARE(d.strokes[1].color, u"#1f2430"_s);
        QCOMPARE(d.strokes[1].version, 1);
        // A highlighter stays one.
        QCOMPARE(d.strokes[2].color, u"#e03131"_s);
        QCOMPARE(d.strokes[2].tool, Tool::Highlighter);
        QCOMPARE(d.strokes[2].opacity, 40);
    }

    void reorder_data()
    {
        QTest::addColumn<QStringList>("selected");
        QTest::addColumn<int>("how");
        QTest::addColumn<QStringList>("order");
        // B a locked backdrop; L the label of R; g1 and g2 a group.
        QTest::newRow("to the front") << QStringList{u"R"_s} << int(Reorder::ToFront)
                                      << QStringList{u"B"_s, u"x"_s, u"y"_s, u"g1"_s, u"g2"_s, u"z"_s, u"R"_s, u"L"_s};
        QTest::newRow("to the back: above the backdrop")
                << QStringList{u"R"_s} << int(Reorder::ToBack)
                << QStringList{u"B"_s, u"R"_s, u"L"_s, u"x"_s, u"y"_s, u"g1"_s, u"g2"_s, u"z"_s};
        QTest::newRow("forward: over one") << QStringList{u"R"_s} << int(Reorder::Forward)
                                           << QStringList{u"B"_s, u"x"_s, u"y"_s, u"R"_s, u"L"_s, u"g1"_s, u"g2"_s, u"z"_s};
        QTest::newRow("forward over a group") << QStringList{u"y"_s} << int(Reorder::Forward)
                                              << QStringList{u"B"_s, u"x"_s, u"R"_s, u"L"_s, u"g1"_s, u"g2"_s, u"y"_s, u"z"_s};
        QTest::newRow("a group backward") << QStringList{u"g1"_s, u"g2"_s} << int(Reorder::Backward)
                                          << QStringList{u"B"_s, u"x"_s, u"R"_s, u"L"_s, u"g1"_s, u"g2"_s, u"y"_s, u"z"_s};
        QTest::newRow("backward at the bottom: nothing")
                << QStringList{u"x"_s} << int(Reorder::Backward)
                << QStringList{u"B"_s, u"x"_s, u"R"_s, u"L"_s, u"y"_s, u"g1"_s, u"g2"_s, u"z"_s};
        QTest::newRow("the backdrop stays") << QStringList{u"B"_s} << int(Reorder::ToFront)
                                            << QStringList{u"B"_s, u"x"_s, u"R"_s, u"L"_s, u"y"_s, u"g1"_s, u"g2"_s, u"z"_s};
    }
    void reorder()
    {
        QFETCH(QStringList, selected);
        QFETCH(int, how);
        QFETCH(QStringList, order);
        Drawing d;
        Stroke b = rectangle(u"B"_s, QRectF(0, 0, 500, 500));
        b.locked = true;
        Stroke r = rectangle(u"R"_s, QRectF(0, 0, 100, 50));
        r.boundIds = {u"L"_s};
        Stroke l = textOf(u"L"_s, QRectF(20, 15, 60, 20), u"Ala"_s);
        l.containerId = u"R"_s;
        Stroke g1 = rectangle(u"g1"_s, QRectF(0, 100, 10, 10));
        g1.groupIds = {u"G"_s};
        Stroke g2 = rectangle(u"g2"_s, QRectF(20, 100, 10, 10));
        g2.groupIds = {u"G"_s};
        d.strokes = {b, freehand(u"x"_s, 0, 0, 3), r, l, freehand(u"y"_s, 0, 0, 3), g1, g2, freehand(u"z"_s, 0, 0, 3)};
        const QStringList before = ids(d);
        reorderElements(d, QSet<QString>(selected.cbegin(), selected.cend()), Reorder(how), testStamp());
        QCOMPARE(ids(d), order);
        // Moved elements stamped; nothing else.
        for (const Stroke &s : d.strokes)
            QCOMPARE(s.version, before != order && selected.contains(s.id) && !s.locked ? 2 : 1);
    }

    void newShapes_data()
    {
        QTest::addColumn<int>("type");
        QTest::addColumn<QPointF>("to");
        QTest::addColumn<bool>("constrain");
        QTest::addColumn<QRectF>("box");
        QTest::addColumn<QPointF>("end");
        QTest::newRow("rectangle") << int(ElementType::Rectangle) << QPointF(50, 30) << false << QRectF(10, 10, 40, 20)
                                   << QPointF();
        QTest::newRow("Shift: a square") << int(ElementType::Rectangle) << QPointF(50, 30) << true
                                         << QRectF(10, 10, 40, 40) << QPointF();
        QTest::newRow("Shift: a square up and left") << int(ElementType::Ellipse) << QPointF(-20, 30) << true
                                                     << QRectF(-20, 10, 30, 30) << QPointF();
        QTest::newRow("diamond dragged back") << int(ElementType::Diamond) << QPointF(0, 0) << false
                                              << QRectF(0, 0, 10, 10) << QPointF();
        QTest::newRow("line") << int(ElementType::Line) << QPointF(110, 20) << false << QRectF() << QPointF(110, 20);
        QTest::newRow("Shift: a level line") << int(ElementType::Line) << QPointF(110, 20) << true << QRectF()
                                             << QPointF(110.5, 10);
        QTest::newRow("Shift: an arrow at 45°") << int(ElementType::Arrow) << QPointF(60, 62) << true << QRectF()
                                                << QPointF(61.01, 61.01);
    }
    void newShapes()
    {
        QFETCH(int, type);
        QFETCH(QPointF, to);
        QFETCH(bool, constrain);
        QFETCH(QRectF, box);
        QFETCH(QPointF, end);
        const Stroke s = newShape(ElementType(type), QPointF(10, 10), to, constrain, ShapeStyle{u"#e03131"_s, 4, true},
                                  testStamp());
        QCOMPARE(int(s.type), type);
        QCOMPARE(s.color, u"#e03131"_s);
        QCOMPARE(s.width, 4.0);
        QCOMPARE(s.roughness, 0);
        QCOMPARE(s.id, u"n1"_s);
        if (isLinear(s.type)) {
            QCOMPARE(s.points.size(), 2);
            QCOMPARE(s.points.first(), (StrokePoint{10, 10, 0.5}));
            QVERIFY2(std::abs(s.points.last().x - end.x()) < 0.01 && std::abs(s.points.last().y - end.y()) < 0.01,
                     qPrintable(u"%1,%2"_s.arg(s.points.last().x).arg(s.points.last().y)));
            QCOMPARE(s.endArrowhead, s.type == ElementType::Arrow ? Arrowhead::Arrow : Arrowhead::None);
        } else {
            QCOMPARE(s.box, box);
            QCOMPARE(s.roundness, s.type == ElementType::Rectangle ? 3 : 0);
        }
    }

    void textEdits()
    {
        const TextMetrics m = fakeMetrics();
        const EditStamp stamp = testStamp();
        Drawing d;
        // A new text: measured.
        const Stroke t = newText(QPointF(10, 10), u"Zażółć gęślą jaźń 🙂\nDruga"_s, TextStyle{u"#2f6fdd"_s, 20, 2}, m,
                                 stamp);
        QCOMPARE(t.box.topLeft(), QPointF(10, 10));
        QVERIFY(std::abs(t.box.width() - 0.55 * 20 * u"Zażółć gęślą jaźń 🙂"_s.size()) < 0.01);
        QVERIFY(std::abs(t.box.height() - 2 * 20 * 1.15) < 0.01);
        d.strokes = {t};
        // Edited: measured again.
        setText(d, t.id, u"Ąę"_s, m, stamp);
        QCOMPARE(d.strokes.first().text, u"Ąę"_s);
        QVERIFY(std::abs(d.strokes.first().box.width() - 22) < 0.01);
        QCOMPARE(d.strokes.first().version, 2);
        // Emptied: gone.
        setText(d, t.id, u"  \n "_s, m, stamp);
        QVERIFY(d.strokes.isEmpty());
        // A label: wrapped to its shape (60 units inside), centred; the shape grows to hold it.
        d.strokes = {rectangle(u"R"_s, QRectF(0, 0, 70, 50))};
        const QString label = addLabel(d, u"R"_s, u"Zażółć gęślą jaźń"_s, TextStyle{}, m, stamp);
        QVERIFY(!label.isEmpty());
        QCOMPARE(ids(d), (QStringList{u"R"_s, label}));
        const Stroke &l = d.strokes.at(1);
        const Stroke &r = d.strokes.at(0);
        QCOMPARE(r.boundIds, QStringList{label});
        QCOMPARE(l.containerId, u"R"_s);
        QCOMPARE(l.originalText, u"Zażółć gęślą jaźń"_s);
        const QStringList lines = l.text.split(u'\n');
        QVERIFY2(lines.size() >= 3, qPrintable(l.text));
        for (const QString &line : lines)
            QVERIFY2(m.width(line, 2, 20) <= 60 + 1e-9, qPrintable(line));
        QCOMPARE(QString(l.text).remove(u'\n').remove(u' '), u"Zażółćgęśląjaźń"_s);
        QVERIFY(std::abs(l.box.center().x() - r.box.center().x()) < 0.01);
        QVERIFY(std::abs(l.box.center().y() - r.box.center().y()) < 0.01);
        QVERIFY(r.box.height() >= l.box.height() + 2 * LabelPadding - 0.01);
        QCOMPARE(l.textAlign, TextAlign::Center);
        // No label on a picture, none of nothing.
        QVERIFY(addLabel(d, label, u"x"_s, TextStyle{}, m, stamp).isEmpty());
        QVERIFY(addLabel(d, u"R"_s, u""_s, TextStyle{}, m, stamp).isEmpty());
    }

    void wrapping_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("width");
        QTest::addColumn<QString>("wrapped");
        // (0.55 × 20 = 11 a character.)
        QTest::newRow("fits") << u"Ala ma"_s << 100.0 << u"Ala ma"_s;
        QTest::newRow("at a space") << u"Ala ma kota"_s << 70.0 << u"Ala ma\nkota"_s;
        QTest::newRow("a long word") << u"Źdźbłoźdźbło"_s << 50.0 << u"Źdźb\nłoźd\nźbło"_s;
        QTest::newRow("emoji kept whole") << u"🙂🙂🙂"_s << 30.0 << u"🙂\n🙂\n🙂"_s;
        QTest::newRow("its own lines stay") << u"a\nb"_s << 100.0 << u"a\nb"_s;
        QTest::newRow("no width: as it is") << u"Ala ma kota"_s << 0.0 << u"Ala ma kota"_s;
    }
    void wrapping()
    {
        QFETCH(QString, text);
        QFETCH(double, width);
        QFETCH(QString, wrapped);
        QCOMPARE(wrapText(text, width, 2, 20, fakeMetrics()), wrapped);
    }

    void diffApply()
    {
        // Random edits: every diff turns before into after and back.
        QRandomGenerator random(20261028);
        Drawing d = shapesFixture();
        const TextMetrics m = fakeMetrics();
        EditStamp stamp = testStamp();
        stamp.random = [&random] { return random.generate(); };
        for (int step = 0; step < 200; ++step) {
            const QList<Stroke> before = d.strokes;
            const auto pick = [&] {
                return d.strokes.isEmpty() ? QString()
                                           : d.strokes.at(qsizetype(random.bounded(quint32(d.strokes.size())))).id;
            };
            switch (random.bounded(8)) {
            case 0: d.strokes.append(freehand(u"f%1"_s.arg(step), random.bounded(300), random.bounded(300), 4)); break;
            case 1: deleteElements(d, {pick()}, stamp); break;
            case 2: moveElements(d, {pick(), pick()}, QPointF(random.bounded(20) - 10, 3), stamp); break;
            case 3: recolorElements(d, {pick()}, u"#7048e8"_s, stamp); break;
            case 4: reorderElements(d, {pick()}, Reorder(random.bounded(4)), stamp); break;
            case 5: duplicateElements(d, selectionClosure(d, {pick()}), QPointF(10, 10), stamp); break;
            case 6: {
                const QString id = pick();
                const qsizetype i = indexOfElement(d, id);
                if (i >= 0 && d.strokes.at(i).type == ElementType::Text)
                    setText(d, id, step % 5 ? u"Nowy ąę %1"_s.arg(step) : QString(), m, stamp);
                else if (i >= 0 && isShape(d.strokes.at(i).type))
                    addLabel(d, id, u"Etykieta"_s, TextStyle{}, m, stamp);
                break;
            }
            default: break; // nothing changed
            }
            const ElementDiff diff = diffElements(before, d.strokes);
            QList<Stroke> forward = before;
            applyDiff(forward, diff, true);
            QVERIFY2(sameElements(forward, d.strokes), qPrintable(u"step %1 forward"_s.arg(step)));
            QList<Stroke> back = d.strokes;
            applyDiff(back, diff, false);
            QVERIFY2(sameElements(back, before), qPrintable(u"step %1 back"_s.arg(step)));
        }
        QVERIFY(diffElements(d.strokes, d.strokes).isEmpty());
    }

    void svgOfShapes()
    {
        Drawing d = shapesFixture();
        // Markup in text is text; a picture of another kind is not let in.
        d.strokes[1].text = u"<script>alert('ą')</script> & \x01"_s;
        d.strokes[1].version += 1;
        d.files.insert(u"zły"_s, QJsonObject{{u"dataURL"_s, u"javascript:alert(1)"_s}});
        d.strokes[13].fileId = u"zły"_s;
        d.strokes[13].version += 1;
        const QByteArray svg = toSvg(d);
        QVERIFY(wellFormed(svg));
        QVERIFY(svg.contains("stroke-dasharray=\"8 10\""));   // the dashed ellipse (width 2)
        QVERIFY(svg.contains("stroke-dasharray=\"1.5 8\""));  // the dotted line
        QVERIFY(svg.contains("xml:space=\"preserve\""));
        QVERIFY(svg.contains("&lt;script&gt;alert(&apos;ą&apos;)&lt;/script&gt; &amp; </text>"));
        QVERIFY(!svg.contains("<script"));
        QVERIFY(!svg.contains("javascript"));
        QVERIFY(svg.contains("transform=\"rotate(28.65 "));   // the rotated rectangle (0.5 rad)
        QVERIFY(svg.contains("xmlns:xlink=\"http://www.w3.org/1999/xlink\""));
        QVERIFY(svg.contains("xlink:href=\"data:image/png;base64,"));
        QVERIFY(svg.contains("fill=\"#e9ecef\""));             // the pictures without a file
        QVERIFY(svg.contains("font-family=\"Helvetica, Arial, sans-serif\""));
        QVERIFY(svg.contains("text-anchor=\"middle\""));       // the label
        QVERIFY(svg.contains("Dwie linie: ąćęłńóśźż</text>"));
        QVERIFY(svg.contains("I emoji 🙂</text>"));
        QVERIFY(looksLikeExcalidrawSvg(svg.left(HeadBytes)));
        // Auto: the dark colours of outlines and fills for browsers.
        QVERIFY(svg.contains(".s0{stroke:"));
        QVERIFY(svg.contains(".f0{fill:"));
        // The picture covers everything: the text box too.
        const QRectF picture = pictureRect(d);
        QVERIFY(picture.contains(find(d, u"tekst"_s)->box));
        // Read again: the same elements; written again: the same file.
        const std::optional<Drawing> back = readDrawing(svg);
        QVERIFY(back && editable(*back));
        QCOMPARE(ids(*back), ids(d));
        QCOMPARE(back->strokes[1].text, d.strokes[1].text);
        QCOMPARE(toSvg(*back), svg);
        // Dark: every colour mapped in the attributes, the picture's colours kept.
        d.theme = PaperTheme::Dark;
        const QByteArray dark = toSvg(d);
        QVERIFY(wellFormed(dark));
        QVERIFY(dark.contains(QByteArray("stroke=\"" + darkColor(u"#1e1e1e").toLatin1() + '"')));
        QVERIFY(!dark.contains("<style>"));
        QVERIFY(dark.contains("xlink:href=\"data:image/png;base64,"));
    }

    void randomElementsNeverCrash()
    {
        // Elements of every kind with random fields (a broken or hostile file): read, drawn, hit,
        // edited and written without trouble.
        QRandomGenerator random(29102026);
        const QStringList types{u"freedraw"_s, u"rectangle"_s, u"ellipse"_s, u"diamond"_s, u"line"_s,
                                u"arrow"_s,    u"text"_s,      u"image"_s,   u"frame"_s};
        const auto value = [&random]() -> QJsonValue {
            switch (random.bounded(7)) {
            case 0: return random.bounded(2000.0) - 1000;
            case 1: return 1e300;
            case 2: return u"ąę"_s;
            case 3: return QJsonValue::Null;
            case 4: return QJsonArray{random.bounded(50), u"x"_s};
            case 5: return QJsonObject{{u"type"_s, random.bounded(5)}};
            default: return -random.bounded(100.0);
            }
        };
        const QStringList keys{u"width"_s,     u"height"_s,      u"angle"_s,     u"strokeWidth"_s, u"opacity"_s,
                               u"roundness"_s, u"fontSize"_s,    u"lineHeight"_s, u"groupIds"_s,   u"boundElements"_s,
                               u"text"_s,      u"containerId"_s, u"fileId"_s,     u"startBinding"_s,
                               u"endArrowhead"_s, u"fillStyle"_s, u"backgroundColor"_s};
        for (int i = 0; i < 400; ++i) {
            QJsonObject e{{u"type"_s, types.at(qsizetype(random.bounded(quint32(types.size()))))},
                          {u"id"_s, u"e%1"_s.arg(i % 7)},
                          {u"x"_s, random.bounded(200.0)},
                          {u"y"_s, random.bounded(200.0)},
                          {u"text"_s, u"Zażółć"_s}};
            QJsonArray points;
            for (int k = int(random.bounded(5)); k > 0; --k)
                points.append(QJsonArray{random.bounded(100.0), random.bounded(100.0)});
            e.insert(u"points"_s, points);
            for (int k = 0; k < 4; ++k)
                e.insert(keys.at(qsizetype(random.bounded(quint32(keys.size())))), value());
            Drawing d;
            if (std::optional<Stroke> s = elementFromJson(e)) {
                d.strokes = {*s, *s};
                (void)elementShape(*s);
                (void)brulion::drawing::hitTest(*s, QPointF(50, 50), 4);
                (void)touchesSegment(*s, QPointF(0, 0), QPointF(100, 100), 4);
                (void)brulion::drawing::lassoSelects(*s, {{0, 0}, {100, 0}, {100, 100}});
                const EditStamp stamp = testStamp();
                moveElements(d, {s->id}, QPointF(3, 4), stamp);
                duplicateElements(d, {s->id}, QPointF(1, 1), stamp);
                reorderElements(d, {s->id}, Reorder::ToBack, stamp);
                setText(d, s->id, u"x"_s, fakeMetrics(), stamp);
                QVERIFY(wellFormed(toSvg(d)));
                QVERIFY(parseScene(sceneJson(d)));
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestDrawingElements)
#include "tst_drawing_elements.moc"
