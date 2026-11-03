// Elements copied and pasted as Excalidraw's clipboard (drawing/clipboard.h): the JSON Brulion
// copies (as excalidraw.com's serializeAsClipboardJSON writes it: bindings to what was not copied
// dropped, the pictures' files, a polygon's label grouped with it), what it pastes (excalidraw.com's
// clipboard with frames, embedded pages, deleted elements and frame membership; a whole scene; new
// ids, groups and bindings among the pasted elements; placed at the pointer; within the limit) and
// what it refuses (plain text, broken JSON, other types, hostile sizes).
#include "drawing/clipboard.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/geometry.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion::drawing;

namespace {

EditStamp testStamp(const QString &prefix = u"n"_s)
{
    auto counter = std::make_shared<int>(0);
    auto random = std::make_shared<quint32>(1000);
    EditStamp s;
    s.now = 1761600000000;
    s.random = [random] { return ++*random; };
    s.newId = [counter, prefix] { return prefix + QString::number(++*counter); };
    return s;
}

TextMetrics fakeMetrics()
{
    TextMetrics m;
    m.width = [](QStringView line, int, double size) { return 0.55 * size * double(line.size()); };
    return m;
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

const Stroke *find(const Drawing &d, const QString &id)
{
    for (const Stroke &s : d.strokes) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

QJsonObject byId(const QJsonArray &elements, const QString &id)
{
    for (const QJsonValue &v : elements) {
        if (v.toObject().value(u"id"_s).toString() == id)
            return v.toObject();
    }
    return {};
}

/// R (with the label L) — the arrow A → E (an ellipse); a picture P with its file; a triangle T
/// with the label TL; the locked rectangle K.
Drawing sample()
{
    const TextMetrics m = fakeMetrics();
    Drawing d;
    Stroke r = rectangle(u"R"_s, QRectF(0, 0, 100, 60));
    Stroke e = rectangle(u"E"_s, QRectF(300, 0, 80, 60));
    e.type = ElementType::Ellipse;
    Stroke a;
    a.type = ElementType::Arrow;
    a.id = u"A"_s;
    a.points = {{101, 30, 0.5}, {299, 30, 0.5}};
    a.startBound = u"R"_s;
    a.endBound = u"E"_s;
    r.boundIds = {u"A"_s};
    e.boundIds = {u"A"_s};
    Stroke p;
    p.type = ElementType::Image;
    p.id = u"P"_s;
    p.box = QRectF(0, 100, 40, 30);
    p.fileId = u"plik"_s;
    Stroke t =
            newPolygon(PolygonKind::Triangle, QPointF(200, 100), QPointF(300, 200), false, ShapeStyle{}, testStamp());
    t.id = u"T"_s;
    Stroke k = rectangle(u"K"_s, QRectF(-50, -50, 10, 10));
    k.locked = true;
    d.strokes = {k, r, e, a, p, t};
    d.files.insert(u"plik"_s, QJsonObject{{u"id"_s, u"plik"_s},
                                          {u"mimeType"_s, u"image/png"_s},
                                          {u"dataURL"_s, u"data:image/png;base64,AAAA"_s}});
    d.files.insert(u"inny"_s, QJsonObject{{u"id"_s, u"inny"_s}, {u"mimeType"_s, u"image/png"_s}});
    EditStamp s = testStamp();
    s.newId = [] { return u"L"_s; };
    addLabel(d, u"R"_s, u"Zażółć"_s, TextStyle{}, m, s);
    s.newId = [] { return u"TL"_s; };
    addLabel(d, u"T"_s, u"Gęś"_s, TextStyle{}, m, s);
    return d;
}

} // namespace

class TestDrawingClipboard : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void copiesAsExcalidraw()
    {
        const Drawing d = sample();
        const QByteArray json = clipboardJson(d, selectionClosure(d, {u"R"_s, u"A"_s, u"P"_s, u"T"_s, u"K"_s}));
        const QJsonObject clip = QJsonDocument::fromJson(json).object();
        QCOMPARE(clip.value(u"type"_s).toString(), u"excalidraw/clipboard"_s);
        const QJsonArray elements = clip.value(u"elements"_s).toArray();
        QStringList ids;
        for (const QJsonValue &v : elements)
            ids.append(v.toObject().value(u"id"_s).toString());
        // In drawing order, with labels; the locked one and E (not chosen) left out.
        QCOMPARE(ids, (QStringList{u"R"_s, u"L"_s, u"A"_s, u"P"_s, u"T"_s, u"TL"_s}));
        // The arrow's binding to E (not copied) dropped; R still names its arrow and its label.
        const QJsonObject a = byId(elements, u"A"_s);
        QCOMPARE(a.value(u"startBinding"_s).toObject().value(u"elementId"_s).toString(), u"R"_s);
        QVERIFY(a.value(u"endBinding"_s).isNull());
        QCOMPARE(byId(elements, u"L"_s).value(u"containerId"_s).toString(), u"R"_s);
        QCOMPARE(byId(elements, u"R"_s).value(u"boundElements"_s).toArray().size(), 2);
        // The triangle's label as excalidraw.com shows it: a text grouped with it.
        const QJsonObject tl = byId(elements, u"TL"_s);
        QVERIFY(tl.value(u"containerId"_s).isNull());
        QCOMPARE(tl.value(u"groupIds"_s).toArray().first().toString(), polygonLabelGroup(u"T"));
        // Only the files of the pictures copied.
        const QJsonObject files = clip.value(u"files"_s).toObject();
        QCOMPARE(files.keys(), QStringList{u"plik"_s});
        QCOMPARE(files.value(u"plik"_s).toObject().value(u"dataURL"_s).toString(), u"data:image/png;base64,AAAA"_s);
        // Nothing (unknown ids, only the locked one): nothing copied.
        QVERIFY(clipboardJson(d, {u"nie ma"_s}).isEmpty());
        QVERIFY(clipboardJson(d, {u"K"_s}).isEmpty());
        QVERIFY(clipboardJson(d, {}).isEmpty());
    }

    void pastesWhatItCopied()
    {
        const Drawing source = sample();
        const QByteArray json = clipboardJson(source, selectionClosure(source, {u"R"_s, u"A"_s, u"P"_s, u"T"_s}));
        const std::optional<ClipboardContent> content = parseClipboard(json);
        QVERIFY(content);
        QCOMPARE(content->elements.size(), 6);
        QCOMPARE(content->skipped, 0);
        // The triangle's label is one again.
        QCOMPARE(find(Drawing{content->elements, {}, {}, {}, {}}, u"TL"_s)->containerId, u"T"_s);

        Drawing d;
        d.strokes = {rectangle(u"R"_s, QRectF(0, 0, 10, 10))}; // (an id the paste has too)
        const QStringList pasted = pasteElements(d, *content, QPointF(1000, 500), testStamp());
        QCOMPARE(pasted.size(), 6);
        QCOMPARE(d.strokes.size(), 7);
        QCOMPARE(d.strokes.first().id, u"R"_s);
        for (const QString &id : pasted) {
            QVERIFY2(!QStringList({u"R"_s, u"L"_s, u"A"_s, u"P"_s, u"T"_s, u"TL"_s}).contains(id), qPrintable(id));
            QVERIFY(!find(d, id)->locked);
        }
        const Stroke r = *find(d, pasted.at(0));
        const Stroke l = *find(d, pasted.at(1));
        const Stroke a = *find(d, pasted.at(2));
        const Stroke t = *find(d, pasted.at(4));
        const Stroke tl = *find(d, pasted.at(5));
        QCOMPARE(l.containerId, r.id);
        QCOMPARE(a.startBound, r.id);
        QVERIFY(a.endBound.isEmpty());
        QCOMPARE(tl.containerId, t.id);
        QVERIFY(r.boundIds.contains(l.id) && r.boundIds.contains(a.id));
        // Placed with the middle of what they cover at the point.
        QRectF box;
        for (const QString &id : pasted)
            box = box.isNull() ? elementBounds(*find(d, id)) : box.united(elementBounds(*find(d, id)));
        QVERIFY(QLineF(box.center(), QPointF(1000, 500)).length() < 0.02);
        // The picture's file came along; the drawing's own one is never replaced.
        QVERIFY(d.files.contains(u"plik"_s));
        // A second paste: other ids again.
        const QStringList again = pasteElements(d, *content, QPointF(0, 0), testStamp(u"m"_s));
        QCOMPARE(again.size(), 6);
        for (const QString &id : again)
            QVERIFY(!pasted.contains(id));
        // Written and read back: a drawing like any other, the triangle's label still one.
        const std::optional<Drawing> back = parseScene(sceneJson(d));
        QVERIFY(back);
        QVERIFY(editable(*back));
        QCOMPARE(find(*back, tl.id)->containerId, t.id);
    }

    void groupsPasteAsTheirOwn()
    {
        Drawing source;
        Stroke a = rectangle(u"a"_s, QRectF(0, 0, 10, 10));
        a.groupIds = {u"G"_s};
        Stroke b = rectangle(u"b"_s, QRectF(20, 0, 10, 10));
        b.groupIds = {u"G"_s};
        source.strokes = {a, b};
        const std::optional<ClipboardContent> content = parseClipboard(clipboardJson(source, {u"a"_s, u"b"_s}));
        QVERIFY(content);
        Drawing d = source;
        const QStringList pasted = pasteElements(d, *content, QPointF(100, 100), testStamp());
        QCOMPARE(pasted.size(), 2);
        const QStringList groups = find(d, pasted.first())->groupIds;
        QCOMPARE(groups.size(), 1);
        QVERIFY(groups.first() != u"G"_s);
        QCOMPARE(find(d, pasted.last())->groupIds, groups);
        QCOMPARE(selectionClosure(d, {pasted.first()}), pasted);
    }

    void pastesExcalidrawsClipboard()
    {
        // As excalidraw.com copies: elements with fractional indices and frame membership, a frame
        // (Brulion draws none), an embedded page, a deleted element, a freehand line in Polish.
        const QByteArray json = R"({"type":"excalidraw/clipboard","elements":[
{"id":"ramka","type":"frame","x":0,"y":0,"width":400,"height":300,"angle":0,"strokeColor":"#bbb","backgroundColor":"transparent","fillStyle":"solid","strokeWidth":2,"strokeStyle":"solid","roughness":0,"opacity":100,"groupIds":[],"frameId":null,"index":"a0","roundness":null,"seed":1,"version":3,"versionNonce":7,"isDeleted":false,"boundElements":null,"updated":1,"link":null,"locked":false,"name":"Ramka"},
{"id":"prost","type":"rectangle","x":10,"y":20,"width":100,"height":50,"angle":0,"strokeColor":"#1e1e1e","backgroundColor":"#a5d8ff","fillStyle":"hachure","strokeWidth":2,"strokeStyle":"solid","roughness":1,"opacity":100,"groupIds":[],"frameId":"ramka","index":"a1","roundness":{"type":3},"seed":2,"version":5,"versionNonce":9,"isDeleted":false,"boundElements":[{"type":"text","id":"napis"}],"updated":2,"link":null,"locked":false},
{"id":"napis","type":"text","x":30,"y":35,"width":60,"height":25,"angle":0,"strokeColor":"#1e1e1e","backgroundColor":"transparent","fillStyle":"solid","strokeWidth":2,"strokeStyle":"solid","roughness":1,"opacity":100,"groupIds":[],"frameId":"ramka","index":"a2","roundness":null,"seed":3,"version":4,"versionNonce":11,"isDeleted":false,"boundElements":null,"updated":3,"link":null,"locked":false,"text":"Źdźbło","fontSize":20,"fontFamily":5,"textAlign":"center","verticalAlign":"middle","containerId":"prost","originalText":"Źdźbło","autoResize":true,"lineHeight":1.25},
{"id":"usuniety","type":"ellipse","x":0,"y":0,"width":10,"height":10,"angle":0,"strokeColor":"#1e1e1e","backgroundColor":"transparent","fillStyle":"solid","strokeWidth":2,"strokeStyle":"solid","roughness":1,"opacity":100,"groupIds":[],"frameId":null,"index":"a3","roundness":null,"seed":4,"version":2,"versionNonce":1,"isDeleted":true,"boundElements":null,"updated":4,"link":null,"locked":false},
{"id":"strona","type":"embeddable","x":500,"y":0,"width":100,"height":100,"angle":0,"strokeColor":"#1e1e1e","backgroundColor":"transparent","fillStyle":"solid","strokeWidth":2,"strokeStyle":"solid","roughness":1,"opacity":100,"groupIds":[],"frameId":null,"index":"a4","roundness":null,"seed":5,"version":2,"versionNonce":1,"isDeleted":false,"boundElements":null,"updated":5,"link":"https:\/\/example.com","locked":false},
{"id":"kreska","type":"freedraw","x":200,"y":200,"width":20,"height":10,"angle":0,"strokeColor":"#e03131","backgroundColor":"transparent","fillStyle":"solid","strokeWidth":2,"strokeStyle":"solid","roughness":1,"opacity":100,"groupIds":[],"frameId":"ramka","index":"a5","roundness":null,"seed":6,"version":9,"versionNonce":3,"isDeleted":false,"boundElements":null,"updated":6,"link":null,"locked":true,"points":[[0,0],[10,5],[20,10]],"pressures":[],"simulatePressure":true,"lastCommittedPoint":null}
],"files":{}})";
        const std::optional<ClipboardContent> content = parseClipboard(json);
        QVERIFY(content);
        QCOMPARE(content->skipped, 2); // the frame and the embedded page
        QCOMPARE(content->elements.size(), 3);
        Drawing d;
        const QStringList pasted = pasteElements(d, *content, QPointF(0, 0), testStamp());
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(find(d, pasted.at(1))->containerId, pasted.at(0));
        QCOMPARE(find(d, pasted.at(1))->text, u"Źdźbło"_s);
        QVERIFY(!find(d, pasted.at(2))->locked);
        // Written: out of the frame it came from, no index of the other drawing, editable.
        const QJsonArray written = QJsonDocument::fromJson(sceneJson(d)).object().value(u"elements"_s).toArray();
        QCOMPARE(written.size(), 3);
        for (const QJsonValue &v : written) {
            QVERIFY(v.toObject().value(u"frameId"_s).isNull());
            QVERIFY(!v.toObject().contains(u"index"_s));
        }
        QVERIFY(editable(*parseScene(sceneJson(d))));
    }

    void pastesAScene()
    {
        // A whole scene (a `.excalidraw` file's text) and Excalidraw's API clipboard paste too.
        const QByteArray scene = sceneJson(sample());
        std::optional<ClipboardContent> content = parseClipboard(scene);
        QVERIFY(content);
        QCOMPARE(content->elements.size(), 8);
        QByteArray api = clipboardJson(sample(), {u"R"_s});
        api.replace("excalidraw/clipboard", "excalidraw-api/clipboard");
        content = parseClipboard(api);
        QVERIFY(content);
        QCOMPARE(content->elements.size(), 1);
    }

    void refuses_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("plain text") << QByteArray("Zażółć gęślą jaźń");
        QTest::newRow("spaces") << QByteArray("   \n\t ");
        QTest::newRow("broken JSON") << QByteArray(R"({"type":"excalidraw/clipboard","elements":[)");
        QTest::newRow("an array") << QByteArray("[1,2,3]");
        QTest::newRow("another type") << QByteArray(R"({"type":"obsidian/clipboard","elements":[]})");
        QTest::newRow("no type") << QByteArray(R"({"elements":[]})");
        QTest::newRow("no elements") << QByteArray(R"({"type":"excalidraw/clipboard"})");
        QTest::newRow("elements an object") << QByteArray(R"({"type":"excalidraw/clipboard","elements":{}})");
        QTest::newRow("type a number") << QByteArray(R"({"type":7,"elements":[]})");
        QByteArray nested = "{\"type\":\"excalidraw/clipboard\",\"elements\":[";
        nested += QByteArray(MaxSceneContainers + 1, '[');
        QTest::newRow("too many arrays") << nested;
        QTest::newRow("too large") << QByteArray(MaxSceneBytes + 1, ' ');
    }
    void refuses()
    {
        QFETCH(QByteArray, text);
        QVERIFY(!parseClipboard(text));
    }

    void malformedElements()
    {
        // Entries that are no element, or broken ones: skipped, the rest pasted.
        const QByteArray json = R"({"type":"excalidraw/clipboard","elements":[1,"x",null,[],
{"id":"zly","type":"rectangle","x":"tu","y":0,"width":10,"height":10},
{"id":"daleko","type":"rectangle","x":1e9,"y":0,"width":10,"height":10},
{"type":"rectangle","x":0,"y":0,"width":10,"height":10},
{"id":"dwa","type":"rectangle","x":0,"y":0,"width":10,"height":10},
{"id":"dwa","type":"ellipse","x":20,"y":0,"width":10,"height":10}],"files":"nie"})";
        const std::optional<ClipboardContent> content = parseClipboard(json);
        QVERIFY(content);
        QCOMPARE(content->elements.size(), 3); // no id, and the same id twice: each gets one
        QVERIFY(content->skipped >= 6);
        Drawing d;
        const QStringList pasted = pasteElements(d, *content, QPointF(5, 5), testStamp());
        QCOMPARE(pasted.size(), 3);
        QCOMPARE(QSet<QString>(pasted.cbegin(), pasted.cend()).size(), 3);
    }

    void pastedWithinTheLimit()
    {
        Drawing source;
        source.strokes = {rectangle(u"r"_s, QRectF(0, 0, 100, 100))};
        const std::optional<ClipboardContent> content = parseClipboard(clipboardJson(source, {u"r"_s}));
        QVERIFY(content);
        Drawing d;
        QCOMPARE(pasteElements(d, *content, QPointF(CoordinateLimit, CoordinateLimit), testStamp()).size(), 1);
        QVERIFY(d.strokes.first().box.right() <= CoordinateLimit);
        QVERIFY(d.strokes.first().box.bottom() <= CoordinateLimit);
        QVERIFY(editable(d));
        // Nowhere: nothing.
        Drawing none;
        QVERIFY(pasteElements(none, *content, QPointF(qQNaN(), 0), testStamp()).isEmpty());
        QVERIFY(pasteElements(none, ClipboardContent{}, QPointF(0, 0), testStamp()).isEmpty());
        QVERIFY(none.strokes.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDrawingClipboard)
#include "tst_drawing_clipboard.moc"
