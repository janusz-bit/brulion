// A drawing's backdrop (core drawing/backdrop.h): the image element at index 0, locked, with
// `customData.brulionBackdrop` and its raster in `files`; set, replaced, removed; read back from
// the scene and the SVG; only index 0 counts; hostile markers and files; the pages a user types;
// the SVG's theme with a backdrop (no style sheet for Auto, the dark raster for Dark — or the Light
// picture without it); a merge keeps a backdrop new here at the bottom.
#include "drawing/backdrop.h"
#include "drawing/drawing.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/merge.h"
#include "drawing/svgwrite.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using namespace brulion::drawing;

namespace {

EditStamp testStamp(qint64 now = 1761700000000)
{
    auto counter = std::make_shared<int>(0);
    auto random = std::make_shared<quint32>(500);
    EditStamp s;
    s.now = now;
    s.random = [random] { return ++*random; };
    s.newId = [counter] { return u"b"_s + QString::number(++*counter); };
    return s;
}

BackdropImage image(const QByteArray &bytes = "slajd-PNG-bajty"_ba, QSize pixels = QSize(2400, 1350),
                    const QString &mime = u"image/png"_s)
{
    return BackdropImage{bytes, mime, pixels};
}

const BackdropSource Lecture{u"Załączniki/Fizyka – wykład 3.pdf"_s, 3};

Stroke ink(const QString &id, double x, double y)
{
    Stroke s;
    s.id = id;
    s.color = u"#1f2430"_s;
    s.points = {{x, y, 0.5}, {x + 40, y + 10, 0.5}, {x + 80, y, 0.5}};
    s.version = 1;
    s.versionNonce = 7;
    s.updated = 1761600000000;
    s.seed = 3;
    return s;
}

/// A scene JSON with these elements and files.
QByteArray scene(const QJsonArray &elements, const QJsonObject &files = {})
{
    return QJsonDocument(QJsonObject{{u"type"_s, u"excalidraw"_s},
                                     {u"version"_s, 2},
                                     {u"elements"_s, elements},
                                     {u"appState"_s, QJsonObject{}},
                                     {u"files"_s, files}})
            .toJson(QJsonDocument::Compact);
}

QJsonObject imageElement(const QString &id, bool locked, const QJsonValue &marker, const QJsonValue &fileId = u"f1"_s)
{
    QJsonObject e{{u"id"_s, id},
                  {u"type"_s, u"image"_s},
                  {u"x"_s, 10},
                  {u"y"_s, 20},
                  {u"width"_s, 120},
                  {u"height"_s, 90},
                  {u"angle"_s, 0},
                  {u"strokeColor"_s, u"transparent"_s},
                  {u"backgroundColor"_s, u"transparent"_s},
                  {u"locked"_s, locked},
                  {u"fileId"_s, fileId},
                  {u"version"_s, 4},
                  {u"versionNonce"_s, 11},
                  {u"seed"_s, 5},
                  {u"isDeleted"_s, false},
                  {u"status"_s, u"saved"_s}};
    if (!marker.isUndefined())
        e.insert(u"customData"_s, QJsonObject{{u"brulionBackdrop"_s, marker}, {u"inny"_s, u"zostaje"_s}});
    return e;
}

QJsonObject freedraw(const QString &id)
{
    return QJsonObject{{u"id"_s, id},
                       {u"type"_s, u"freedraw"_s},
                       {u"x"_s, 0},
                       {u"y"_s, 0},
                       {u"width"_s, 10},
                       {u"height"_s, 10},
                       {u"points"_s, QJsonArray{QJsonArray{0, 0}, QJsonArray{10, 10}}},
                       {u"version"_s, 2},
                       {u"strokeColor"_s, u"#1e1e1e"_s}};
}

QJsonObject pngFile(const QString &id, const QString &url = u"data:image/png;base64,iVBORw0KGgo="_s)
{
    return QJsonObject{{u"id"_s, id}, {u"mimeType"_s, u"image/png"_s}, {u"dataURL"_s, url}};
}

int count(const QByteArray &haystack, const QByteArray &needle)
{
    int n = 0;
    for (qsizetype i = haystack.indexOf(needle); i >= 0; i = haystack.indexOf(needle, i + needle.size()))
        ++n;
    return n;
}

} // namespace

class TestDrawingBackdrop : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fileIdOfKnownBytes();
    void backdropSceneSize_data();
    void backdropSceneSize();
    void setReplaceRemove_data();
    void setReplaceRemove();
    void invalidImageRefused_data();
    void invalidImageRefused();
    void roundTrip();
    void onlyIndexZeroCounts_data();
    void onlyIndexZeroCounts();
    void hostileBackdrop_data();
    void hostileBackdrop();
    void parsePageRanges_data();
    void parsePageRanges();
    void svgThemeWithBackdrop_data();
    void svgThemeWithBackdrop();
    void reorderKeepsBackdropAtBottom();
    void backdropMerge();
};

void TestDrawingBackdrop::fileIdOfKnownBytes()
{
    QCOMPARE(fileIdOf("abc"), u"a9993e364706816aba3e25717850c26c9cd0d89d"_s);
    QCOMPARE(fileIdOf(QByteArrayView()), u"da39a3ee5e6b4b0d3255bfef95601890afd80709"_s);
    QCOMPARE(dataUrl(image("abc"_ba, QSize(1, 1), u"image/jpeg"_s)), u"data:image/jpeg;base64,YWJj"_s);
}

void TestDrawingBackdrop::backdropSceneSize_data()
{
    QTest::addColumn<QSize>("pixels");
    QTest::addColumn<QSizeF>("expected");
    QTest::newRow("a slide 16:9") << QSize(2400, 1350) << QSizeF(1200, 675);
    QTest::newRow("an A4 page upright") << QSize(1754, 2480) << QSizeF(848.71, 1200);
    QTest::newRow("one pixel") << QSize(1, 1) << QSizeF(1200, 1200);
    QTest::newRow("small, wide") << QSize(64, 32) << QSizeF(1200, 600);
    QTest::newRow("empty") << QSize(0, 0) << QSizeF();
    QTest::newRow("negative") << QSize(-5, 10) << QSizeF();
}

void TestDrawingBackdrop::backdropSceneSize()
{
    QFETCH(QSize, pixels);
    QFETCH(QSizeF, expected);
    const QSizeF size = brulion::drawing::backdropSceneSize(pixels);
    if (!expected.isValid()) {
        QVERIFY(size.isEmpty());
        return;
    }
    QCOMPARE(size.width(), expected.width());
    QCOMPARE(size.height(), expected.height());
}

void TestDrawingBackdrop::setReplaceRemove_data()
{
    QTest::addColumn<QString>("start");
    QTest::newRow("an empty drawing") << u"empty"_s;
    QTest::newRow("with strokes") << u"strokes"_s;
    QTest::newRow("replacing a backdrop") << u"replace"_s;
}

void TestDrawingBackdrop::setReplaceRemove()
{
    QFETCH(QString, start);
    Drawing d;
    EditStamp stamp = testStamp();
    if (start != u"empty") {
        d.strokes = {ink(u"a"_s, 100, 100), ink(u"b"_s, 300, 400)};
    }
    QString oldId;
    QPointF oldTopLeft(0, 0);
    if (start == u"replace") {
        QVERIFY(setBackdrop(d, image("stary slajd"_ba, QSize(800, 600)), {u"Załączniki/stary.png"_s, 0}, stamp));
        d.strokes.first().box.moveTopLeft(QPointF(-40, 25)); // (moved in excalidraw.com)
        oldId = d.strokes.first().id;
        oldTopLeft = d.strokes.first().box.topLeft();
    }
    const QList<Stroke> others = start == u"replace" ? d.strokes.mid(1) : d.strokes;
    const int oldVersion = start == u"replace" ? d.strokes.first().version : 0;

    QVERIFY(setBackdrop(d, image(), Lecture, stamp));
    QCOMPARE(backdropIndex(d), 0);
    const Stroke &b = d.strokes.first();
    QCOMPARE(b.type, ElementType::Image);
    QVERIFY(b.locked);
    QCOMPARE(b.fileId, fileIdOf("slajd-PNG-bajty"));
    QCOMPARE(b.box.topLeft(), oldTopLeft);
    QCOMPARE(b.box.size(), QSizeF(1200, 675));
    if (start == u"replace") {
        QCOMPARE(b.id, oldId); // the element itself changed: merges and undo see it
        QCOMPARE(b.version, oldVersion + 1);
        QCOMPARE(b.updated, stamp.now);
    }
    QCOMPARE(d.strokes.mid(1).size(), others.size());
    for (qsizetype i = 0; i < others.size(); ++i) {
        QCOMPARE(d.strokes.at(i + 1).id, others.at(i).id);
        QCOMPARE(d.strokes.at(i + 1).version, others.at(i).version);
    }
    const std::optional<BackdropSource> source = backdropSource(d);
    QVERIFY(source);
    QCOMPARE(*source, Lecture);
    const QJsonObject file = d.files.value(b.fileId).toObject();
    QCOMPARE(file.value(u"mimeType"_s).toString(), u"image/png"_s);
    QCOMPARE(file.value(u"dataURL"_s).toString(), dataUrl(image()));

    // Written: the element with the marker, only the new file (the old one dropped).
    const QJsonObject root = QJsonDocument::fromJson(sceneJson(d)).object();
    const QJsonObject first = root.value(u"elements"_s).toArray().first().toObject();
    QCOMPARE(first.value(u"type"_s).toString(), u"image"_s);
    QCOMPARE(first.value(u"locked"_s).toBool(), true);
    QCOMPARE(first.value(u"fileId"_s).toString(), b.fileId);
    QCOMPARE(first.value(u"width"_s).toDouble(), 1200.0);
    const QJsonObject marker = first.value(u"customData"_s).toObject().value(u"brulionBackdrop"_s).toObject();
    QCOMPARE(marker.value(u"path"_s).toString(), u"Załączniki/Fizyka – wykład 3.pdf"_s);
    QCOMPARE(marker.value(u"page"_s).toInt(), 3);
    QCOMPARE(root.value(u"files"_s).toObject().keys(), QStringList{b.fileId});

    // Removed: the other elements stay, the file entry too (undo brings it back), not written.
    QVERIFY(removeBackdrop(d));
    QCOMPARE(backdropIndex(d), -1);
    QVERIFY(!backdropSource(d));
    QCOMPARE(d.strokes.size(), others.size());
    QVERIFY(d.files.contains(fileIdOf("slajd-PNG-bajty")));
    QVERIFY(QJsonDocument::fromJson(sceneJson(d)).object().value(u"files"_s).toObject().isEmpty());
    QVERIFY(!removeBackdrop(d));
}

void TestDrawingBackdrop::invalidImageRefused_data()
{
    QTest::addColumn<QByteArray>("bytes");
    QTest::addColumn<QString>("mime");
    QTest::addColumn<QSize>("pixels");
    QTest::newRow("no bytes") << QByteArray() << u"image/png"_s << QSize(10, 10);
    QTest::newRow("too large") << QByteArray(MaxBackdropBytes + 1, 'x') << u"image/png"_s << QSize(10, 10);
    QTest::newRow("SVG") << "<svg/>"_ba << u"image/svg+xml"_s << QSize(10, 10);
    QTest::newRow("HTML") << "<b>x</b>"_ba << u"text/html"_s << QSize(10, 10);
    QTest::newRow("no size") << "x"_ba << u"image/jpeg"_s << QSize(0, 10);
}

void TestDrawingBackdrop::invalidImageRefused()
{
    QFETCH(QByteArray, bytes);
    QFETCH(QString, mime);
    QFETCH(QSize, pixels);
    Drawing d;
    d.strokes = {ink(u"a"_s, 0, 0)};
    QVERIFY(!setBackdrop(d, BackdropImage{bytes, mime, pixels}, Lecture, testStamp()));
    QCOMPARE(d.strokes.size(), 1);
    QVERIFY(d.files.isEmpty());
}

void TestDrawingBackdrop::roundTrip()
{
    Drawing d;
    d.strokes = {ink(u"a"_s, 50, 60)};
    QVERIFY(setBackdrop(d, image(), Lecture, testStamp()));
    // A key of another program in the element's customData survives.
    {
        QJsonObject custom = d.strokes.first().element.value(u"customData"_s).toObject();
        custom.insert(u"obcy"_s, u"zażółć"_s);
        d.strokes.first().element.insert(u"customData"_s, custom);
    }
    const std::optional<Drawing> back = parseScene(sceneJson(d));
    QVERIFY(back);
    QCOMPARE(backdropIndex(*back), 0);
    QCOMPARE(*backdropSource(*back), Lecture);
    QCOMPARE(back->strokes.first().element.value(u"customData"_s).toObject().value(u"obcy"_s).toString(), u"zażółć"_s);
    QCOMPARE(back->strokes.size(), 2);

    // The SVG: the picture once (the scene has it compressed), self-contained.
    const QByteArray svg = toSvg(d);
    const QByteArray payload = "slajd-PNG-bajty"_ba.toBase64();
    QCOMPARE(count(svg, "xlink:href=\"data:image/png;base64,"), 1);
    QCOMPARE(count(svg, payload), 1);
    QVERIFY(looksLikeExcalidrawSvg(svg.left(HeadBytes)));
    const std::optional<Drawing> fromSvg = readDrawing(svg);
    QVERIFY(fromSvg);
    QCOMPARE(*backdropSource(*fromSvg), Lecture);
    // The picture covers the backdrop.
    const QRectF picture = pictureRect(d);
    QVERIFY(picture.contains(QRectF(0, 0, 1200, 675)));
    // The backdrop is drawn first, under the ink.
    QVERIFY(svg.indexOf("<image ") < svg.indexOf("<path d="));
}

void TestDrawingBackdrop::onlyIndexZeroCounts_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<bool>("backdrop");
    QTest::addColumn<bool>("firstLocked");
    const QJsonObject marker{{u"path"_s, u"slajdy/s1.png"_s}, {u"page"_s, 0}};
    const QJsonObject files{{u"f1"_s, pngFile(u"f1"_s)}};
    QTest::newRow("index 0, locked, marked")
            << scene({imageElement(u"i"_s, true, marker), freedraw(u"a"_s)}, files) << true << true;
    QTest::newRow("marker on index 2: a picture")
            << scene({freedraw(u"a"_s), freedraw(u"b"_s), imageElement(u"i"_s, true, marker)}, files) << false << false;
    QTest::newRow("index 0 unlocked: a picture")
            << scene({imageElement(u"i"_s, false, marker)}, files) << false << false;
    QTest::newRow("locked picture without marker")
            << scene({imageElement(u"i"_s, true, QJsonValue(QJsonValue::Undefined))}, files) << false << true;
    QTest::newRow("a locked rectangle") << scene(
            {QJsonObject{{u"id"_s, u"r"_s},
                         {u"type"_s, u"rectangle"_s},
                         {u"x"_s, 0},
                         {u"y"_s, 0},
                         {u"width"_s, 10},
                         {u"height"_s, 10},
                         {u"locked"_s, true},
                         {u"customData"_s, QJsonObject{{u"brulionBackdrop"_s, marker}}}}})
                                        << false << true;
}

void TestDrawingBackdrop::onlyIndexZeroCounts()
{
    QFETCH(QByteArray, json);
    QFETCH(bool, backdrop);
    QFETCH(bool, firstLocked);
    const std::optional<Drawing> d = parseScene(json);
    QVERIFY(d);
    QCOMPARE(backdropIndex(*d) == 0, backdrop);
    QCOMPARE(backdropSource(*d).has_value(), backdrop);
    QCOMPARE(d->strokes.first().locked, firstLocked);
    // Written back unchanged.
    QCOMPARE(QJsonDocument::fromJson(sceneJson(*d)).object().value(u"elements"_s),
             QJsonDocument::fromJson(json).object().value(u"elements"_s));
}

void TestDrawingBackdrop::hostileBackdrop_data()
{
    QTest::addColumn<QJsonValue>("marker");
    QTest::addColumn<QJsonValue>("fileId");
    QTest::addColumn<QJsonObject>("files");
    QTest::addColumn<bool>("isBackdrop");
    QTest::addColumn<QString>("path");
    QTest::addColumn<int>("page");
    QTest::addColumn<bool>("pictureShown");
    const QJsonObject marker{{u"path"_s, u"Załączniki/wykład.pdf"_s}, {u"page"_s, 2}};
    const auto with = [](const QString &url) { return QJsonObject{{u"f1"_s, pngFile(u"f1"_s, url)}}; };
    QTest::newRow("javascript: URL") << QJsonValue(marker) << QJsonValue(u"f1"_s) << with(u"javascript:alert(1)"_s)
                                     << true << u"Załączniki/wykład.pdf"_s << 2 << false;
    QTest::newRow("data:text/html") << QJsonValue(marker) << QJsonValue(u"f1"_s)
                                    << with(u"data:text/html;base64,PGI+eDwvYj4="_s) << true
                                    << u"Załączniki/wykład.pdf"_s << 2 << false;
    QTest::newRow("quote in the base64") << QJsonValue(marker) << QJsonValue(u"f1"_s)
                                         << with(u"data:image/png;base64,AA\"/><script>"_s) << true
                                         << u"Załączniki/wykład.pdf"_s << 2 << false;
    QTest::newRow("missing file") << QJsonValue(marker) << QJsonValue(u"f1"_s) << QJsonObject() << true
                                  << u"Załączniki/wykład.pdf"_s << 2 << false;
    QTest::newRow("fileId a number") << QJsonValue(marker) << QJsonValue(7) << with(u"data:image/png;base64,AAAA"_s)
                                     << true << u"Załączniki/wykład.pdf"_s << 2 << false;
    QTest::newRow("marker a string") << QJsonValue(u"x"_s) << QJsonValue(u"f1"_s)
                                     << with(u"data:image/png;base64,AAAA"_s) << false << QString() << 0 << true;
    QTest::newRow("page -3") << QJsonValue(QJsonObject{{u"path"_s, u"a.pdf"_s}, {u"page"_s, -3}}) << QJsonValue(u"f1"_s)
                             << with(u"data:image/png;base64,AAAA"_s) << true << u"a.pdf"_s << 0 << true;
    QTest::newRow("page 1e9") << QJsonValue(QJsonObject{{u"path"_s, u"a.pdf"_s}, {u"page"_s, 1e9}})
                              << QJsonValue(u"f1"_s) << with(u"data:image/png;base64,AAAA"_s) << true << u"a.pdf"_s << 0
                              << true;
    QTest::newRow("page 2.5, a string path")
            << QJsonValue(QJsonObject{{u"path"_s, 5}, {u"page"_s, 2.5}}) << QJsonValue(u"f1"_s)
            << with(u"data:image/png;base64,AAAA"_s) << true << QString() << 0 << true;
    QTest::newRow("absolute path") << QJsonValue(QJsonObject{{u"path"_s, u"/etc/passwd"_s}, {u"page"_s, 1}})
                                   << QJsonValue(u"f1"_s) << with(u"data:image/png;base64,AAAA"_s) << true << QString()
                                   << 1 << true;
    QTest::newRow("leaves the vault") << QJsonValue(QJsonObject{{u"path"_s, u"a/../../b.pdf"_s}, {u"page"_s, 1}})
                                      << QJsonValue(u"f1"_s) << with(u"data:image/png;base64,AAAA"_s) << true
                                      << QString() << 1 << true;
    QTest::newRow("Windows drive") << QJsonValue(QJsonObject{{u"path"_s, u"C:\\Users\\a.pdf"_s}, {u"page"_s, 1}})
                                   << QJsonValue(u"f1"_s) << with(u"data:image/png;base64,AAAA"_s) << true << QString()
                                   << 1 << true;
}

void TestDrawingBackdrop::hostileBackdrop()
{
    QFETCH(QJsonValue, marker);
    QFETCH(QJsonValue, fileId);
    QFETCH(QJsonObject, files);
    QFETCH(bool, isBackdrop);
    QFETCH(QString, path);
    QFETCH(int, page);
    QFETCH(bool, pictureShown);
    const std::optional<Drawing> d =
            parseScene(scene({imageElement(u"i"_s, true, marker, fileId), freedraw(u"a"_s)}, files));
    QVERIFY(d);
    QCOMPARE(backdropIndex(*d) == 0, isBackdrop);
    if (isBackdrop) {
        const std::optional<BackdropSource> source = backdropSource(*d);
        QVERIFY(source);
        QCOMPARE(source->path, path);
        QCOMPARE(source->page, page);
    }
    // Never written into the picture: a placeholder instead.
    for (const PaperTheme theme : {PaperTheme::Light, PaperTheme::Dark, PaperTheme::Auto}) {
        Drawing themed = *d;
        themed.theme = theme;
        SvgOptions options;
        options.pictureImages.insert(u"f1"_s, u"javascript:alert(2)"_s);
        const QByteArray svg = toSvg(themed, options);
        QVERIFY(!svg.contains("javascript:"));
        QVERIFY(!svg.contains("text/html"));
        QVERIFY(!svg.contains("<script"));
        QCOMPARE(svg.contains("<image "), pictureShown);
        QVERIFY(readDrawing(svg));
    }
}

void TestDrawingBackdrop::parsePageRanges_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("count");
    QTest::addColumn<QList<int>>("expected");
    QList<int> sixty;
    for (int i = 1; i <= 60; ++i)
        sixty.append(i);
    QTest::newRow("ranges and a page") << u"1-3, 7"_s << 10 << QList<int>{1, 2, 3, 7};
    QTest::newRow("en dash, reversed") << u"3–1"_s << 5 << QList<int>{1, 2, 3};
    QTest::newRow("em dash, semicolons") << u"2—3; 5"_s << 5 << QList<int>{2, 3, 5};
    QTest::newRow("none in range, malformed") << u"0, 11, abc"_s << 10 << QList<int>{};
    QTest::newRow("wszystkie") << u"wszystkie"_s << 4 << QList<int>{1, 2, 3, 4};
    QTest::newRow("ALL") << u" ALL "_s << 2 << QList<int>{1, 2};
    QTest::newRow("star") << u"*"_s << 3 << QList<int>{1, 2, 3};
    QTest::newRow("capped at 60") << u"1-1000"_s << 70 << sixty;
    QTest::newRow("all capped at 60") << u"wszystkie"_s << 200 << sixty;
    QTest::newRow("duplicates, blanks") << u" 2 ,2,2 "_s << 5 << QList<int>{2};
    QTest::newRow("empty") << QString() << 3 << QList<int>{};
    QTest::newRow("clipped range") << u"4-9"_s << 6 << QList<int>{4, 5, 6};
    QTest::newRow("lowest kept") << u"100-500, 1-2"_s << 1000 << [] {
        QList<int> l{1, 2};
        for (int i = 100; i < 158; ++i)
            l.append(i);
        return l;
    }();
    QTest::newRow("half ranges ignored") << u"-3, 4-, 1-2-3, +5, 6"_s << 10 << QList<int>{6};
    QTest::newRow("overflowing number") << u"99999999999999, 2"_s << 10 << QList<int>{2};
    QTest::newRow("no pages") << u"1-3"_s << 0 << QList<int>{};
    QTest::newRow("Polish words") << u"strona żółta, 2"_s << 3 << QList<int>{2};
}

void TestDrawingBackdrop::parsePageRanges()
{
    QFETCH(QString, text);
    QFETCH(int, count);
    QFETCH(QList<int>, expected);
    QCOMPARE(brulion::drawing::parsePageRanges(text, count), expected);
}

void TestDrawingBackdrop::svgThemeWithBackdrop_data()
{
    QTest::addColumn<int>("theme");
    QTest::addColumn<bool>("darkRaster");
    QTest::addColumn<bool>("style");
    QTest::addColumn<bool>("darkInk");
    QTest::addColumn<bool>("darkPicture");
    QTest::newRow("Auto: no style sheet") << int(PaperTheme::Auto) << false << false << false << false;
    QTest::newRow("Light") << int(PaperTheme::Light) << false << false << false << false;
    QTest::newRow("Dark with the dark raster") << int(PaperTheme::Dark) << true << false << true << true;
    QTest::newRow("Dark without it: the Light picture") << int(PaperTheme::Dark) << false << false << false << false;
}

void TestDrawingBackdrop::svgThemeWithBackdrop()
{
    QFETCH(int, theme);
    QFETCH(bool, darkRaster);
    QFETCH(bool, style);
    QFETCH(bool, darkInk);
    QFETCH(bool, darkPicture);
    Drawing d;
    d.strokes = {ink(u"a"_s, 50, 60)};
    QVERIFY(setBackdrop(d, image(), Lecture, testStamp()));
    d.theme = PaperTheme(theme);
    const QString fileId = d.strokes.first().fileId;
    const QString darkUrl = u"data:image/png;base64,"_s + QString::fromLatin1("ciemny-slajd"_ba.toBase64());
    SvgOptions options;
    if (darkRaster)
        options.pictureImages.insert(fileId, darkUrl);
    const QByteArray svg = toSvg(d, options);
    QCOMPARE(svg.contains("<style>"), style);
    QCOMPARE(svg.contains("prefers-color-scheme"), false);
    QCOMPARE(svg.contains(darkUrl.toLatin1()), darkPicture);
    QCOMPARE(svg.contains("slajd-PNG-bajty"_ba.toBase64()), !darkPicture);
    // The ink: stored #1f2430, the dark theme's light ink otherwise.
    QCOMPARE(svg.contains("fill=\"#1f2430\""), !darkInk);
    QCOMPARE(svg.contains("fill=\"#ffffff\""), !darkInk); // (the light paper)
    // The scene keeps the original raster and the theme.
    const std::optional<Drawing> back = readDrawing(svg);
    QVERIFY(back);
    QCOMPARE(back->theme, PaperTheme(theme));
    QCOMPARE(back->files.value(fileId).toObject().value(u"dataURL"_s).toString(), dataUrl(image()));
    // The dark raster is used only for the backdrop: a picture elsewhere keeps its own.
    if (darkRaster) {
        Drawing moved = d;
        moved.strokes.move(0, 1);
        QVERIFY(!toSvg(moved, options).contains(darkUrl.toLatin1()));
    }
}

void TestDrawingBackdrop::reorderKeepsBackdropAtBottom()
{
    Drawing d;
    d.strokes = {ink(u"a"_s, 0, 0), ink(u"b"_s, 10, 10)};
    QVERIFY(setBackdrop(d, image(), Lecture, testStamp()));
    const QString backdrop = d.strokes.first().id;
    reorderElements(d, {u"b"_s}, Reorder::ToBack, testStamp());
    QCOMPARE(d.strokes.first().id, backdrop);
    QCOMPARE(d.strokes.at(1).id, u"b"_s);
    // Locked: never moved, deleted, recoloured or selected.
    QVERIFY(selectionClosure(d, {backdrop}).isEmpty());
    moveElements(d, {backdrop}, QPointF(50, 50), testStamp());
    deleteElements(d, {backdrop}, testStamp());
    QCOMPARE(backdropIndex(d), 0);
    QCOMPARE(d.strokes.first().box.topLeft(), QPointF(0, 0));
}

void TestDrawingBackdrop::backdropMerge()
{
    // Both devices had the strokes a; here a backdrop was set, there a stroke c added.
    Drawing base;
    base.strokes = {ink(u"a"_s, 0, 0)};
    const MergeBase known = mergeBase(base);
    Drawing ours = base;
    QVERIFY(setBackdrop(ours, image(), Lecture, testStamp()));
    Drawing theirs = base;
    theirs.strokes.append(ink(u"c"_s, 200, 200));
    const Drawing merged = merge(known, ours, theirs);
    QCOMPARE(backdropIndex(merged), 0);
    QCOMPARE(*backdropSource(merged), Lecture);
    QCOMPARE(merged.strokes.size(), 3);
    QVERIFY(merged.files.contains(ours.strokes.first().fileId));
    // The picture of the merge still shows the slide.
    QVERIFY(toSvg(merged).contains("slajd-PNG-bajty"_ba.toBase64()));
}

QTEST_GUILESS_MAIN(TestDrawingBackdrop)
#include "tst_drawing_backdrop.moc"
