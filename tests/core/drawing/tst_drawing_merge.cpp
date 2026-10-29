// Two versions of a drawing merged element by element (core drawing/merge.h): the version another
// device wrote taken in while the drawing is open (a known base: the last load or save), and the
// sync center's merge of a conflict copy (no known base: a union). New here on top in ours' order
// (a backdrop new here at the bottom), changed here wins, deleted here goes, deleted there stays
// deleted, the pattern and the theme ours only when changed here, files of both.
#include "drawing/drawing.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/merge.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::drawing;

namespace {

/// A drawing of "id:version" elements (a trailing "L": locked) — rectangles in a row.
Drawing scene(const QString &spec)
{
    Drawing d;
    int i = 0;
    for (const QString &token : spec.split(u' ', Qt::SkipEmptyParts)) {
        const QStringList parts = token.split(u':');
        Stroke s;
        s.type = ElementType::Rectangle;
        s.id = parts.at(0);
        QString version = parts.value(1, u"1"_s);
        s.locked = version.endsWith(u'L');
        if (s.locked)
            version.chop(1);
        s.version = version.toInt();
        s.box = QRectF(i * 50, 0, 40, 40);
        d.strokes.append(s);
        ++i;
    }
    return d;
}

QString describe(const Drawing &d)
{
    QStringList out;
    for (const Stroke &s : d.strokes)
        out.append(s.id + u':' + QString::number(s.version));
    return out.join(u' ');
}

} // namespace

class TestDrawingMerge : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void merge_data()
    {
        QTest::addColumn<QString>("base"); // "?" none known
        QTest::addColumn<QString>("ours");
        QTest::addColumn<QString>("theirs");
        QTest::addColumn<QString>("merged");
        QTest::newRow("new here: on top") << u"a b"_s << u"a b n"_s << u"a b c"_s << u"a:1 b:1 c:1 n:1"_s;
        QTest::newRow("new here: in ours' order") << u"a"_s << u"x a y"_s << u"a c"_s << u"a:1 c:1 x:1 y:1"_s;
        QTest::newRow("changed here wins") << u"a b"_s << u"a:2 b"_s << u"a b:3"_s << u"a:2 b:3"_s;
        QTest::newRow("changed on both sides: ours") << u"a"_s << u"a:2"_s << u"a:5"_s << u"a:2"_s;
        QTest::newRow("deleted here") << u"a b"_s << u"b"_s << u"a b"_s << u"b:1"_s;
        QTest::newRow("deleted here, changed there") << u"a b"_s << u"b"_s << u"a:4 b"_s << u"b:1"_s;
        QTest::newRow("deleted there stays deleted") << u"a b"_s << u"a b:2"_s << u"a"_s << u"a:1"_s;
        QTest::newRow("both added different ones") << u"a"_s << u"a x"_s << u"a y"_s << u"a:1 y:1 x:1"_s;
        QTest::newRow("nothing changed here") << u"a"_s << u"a"_s << u"a c"_s << u"a:1 c:1"_s;
        QTest::newRow("their order kept") << u"a b"_s << u"a b"_s << u"b a"_s << u"b:1 a:1"_s;
        // drawbg's backdrop: a locked element new here under everything; the rest on top.
        QTest::newRow("a backdrop new here: at the bottom") << u"a"_s << u"B:1L n a"_s << u"a c"_s
                                                            << u"B:1 a:1 c:1 n:1"_s;
        QTest::newRow("a locked element not leading: on top") << u"a"_s << u"a B:1L"_s << u"a c"_s << u"a:1 c:1 B:1"_s;
        // No common ancestor (the sync center): a union, the higher version of each.
        QTest::newRow("unknown base: union") << u"?"_s << u"a:3 b x"_s << u"a:2 b:4 y"_s << u"a:3 b:4 y:1 x:1"_s;
        QTest::newRow("unknown base: nothing deleted") << u"?"_s << u"b"_s << u"a b"_s << u"a:1 b:1"_s;
    }
    void merge()
    {
        QFETCH(QString, base);
        QFETCH(QString, ours);
        QFETCH(QString, theirs);
        QFETCH(QString, merged);
        const MergeBase b = base == u"?" ? MergeBase{} : mergeBase(scene(base));
        QCOMPARE(b.known, base != u"?");
        const Drawing result = brulion::drawing::merge(b, scene(ours), scene(theirs));
        QCOMPARE(describe(result), merged);
        // The same inputs, the same result.
        QCOMPARE(describe(brulion::drawing::merge(b, scene(ours), scene(theirs))), merged);
    }

    void aTieKeepsTheirs()
    {
        Drawing ours = scene(u"a:2"_s);
        ours.strokes[0].color = u"#e03131"_s;
        Drawing theirs = scene(u"a:2"_s);
        theirs.strokes[0].color = u"#2f9e44"_s;
        QCOMPARE(brulion::drawing::merge(MergeBase{}, ours, theirs).strokes.first().color, u"#2f9e44"_s);
    }

    void aRectangleMovedOnEachSide()
    {
        // Here "a" moved right, there "b" moved down: both moves are in the merge.
        const Drawing base = scene(u"a b"_s);
        Drawing ours = base;
        Drawing theirs = base;
        EditStamp stamp;
        stamp.now = 1761600000000;
        moveElements(ours, {u"a"_s}, QPointF(100, 0), stamp);
        moveElements(theirs, {u"b"_s}, QPointF(0, 100), stamp);
        const Drawing merged = brulion::drawing::merge(mergeBase(base), ours, theirs);
        QCOMPARE(merged.strokes.size(), 2);
        QCOMPARE(merged.strokes.at(0).box, QRectF(100, 0, 40, 40));
        QCOMPARE(merged.strokes.at(1).box, QRectF(50, 100, 40, 40));
        // And as the files of both devices: read back the same.
        const std::optional<Drawing> back = parseScene(sceneJson(merged));
        QVERIFY(back);
        QCOMPARE(back->strokes.at(0).box, QRectF(100, 0, 40, 40));
    }

    void patternAndTheme_data()
    {
        QTest::addColumn<bool>("known");
        QTest::addColumn<int>("ourBackground");
        QTest::addColumn<int>("theirBackground");
        QTest::addColumn<int>("merged");
        QTest::newRow("changed here") << true << int(Background::Grid) << int(Background::Lines) << int(Background::Grid);
        QTest::newRow("changed there") << true << int(Background::Plain) << int(Background::Lines)
                                       << int(Background::Lines);
        QTest::newRow("unknown base: theirs") << false << int(Background::Grid) << int(Background::Lines)
                                              << int(Background::Lines);
    }
    void patternAndTheme()
    {
        QFETCH(bool, known);
        QFETCH(int, ourBackground);
        QFETCH(int, theirBackground);
        QFETCH(int, merged);
        Drawing base = scene(u"a"_s);
        MergeBase b = known ? mergeBase(base) : MergeBase{};
        Drawing ours = base;
        ours.background = Background(ourBackground);
        ours.theme = Background(ourBackground) == Background::Grid ? PaperTheme::Dark : PaperTheme::Auto;
        Drawing theirs = base;
        theirs.background = Background(theirBackground);
        theirs.theme = PaperTheme::Light;
        theirs.paper = u"#fffdf5"_s;
        const Drawing result = brulion::drawing::merge(b, ours, theirs);
        QCOMPARE(int(result.background), merged);
        // The theme by the same rule; the paper's colour theirs.
        QCOMPARE(result.theme, merged == ourBackground && known ? ours.theme : PaperTheme::Light);
        QCOMPARE(result.paper, u"#fffdf5"_s);
    }

    void filesOfBoth()
    {
        Drawing ours = scene(u"a"_s);
        ours.files.insert(u"zdjęcie"_s, QJsonObject{{u"dataURL"_s, u"data:image/png;base64,AAAA"_s}});
        ours.files.insert(u"wspólne"_s, QJsonObject{{u"id"_s, u"nasze"_s}});
        Drawing theirs = scene(u"a"_s);
        theirs.files.insert(u"wspólne"_s, QJsonObject{{u"id"_s, u"ich"_s}});
        theirs.appState.insert(u"gridSize"_s, 20);
        const Drawing merged = brulion::drawing::merge(mergeBase(scene(u"a"_s)), ours, theirs);
        QCOMPARE(merged.files.size(), 2);
        QCOMPARE(merged.files.value(u"wspólne"_s).toObject().value(u"id"_s).toString(), u"ich"_s);
        QVERIFY(merged.files.contains(u"zdjęcie"_s));
        QCOMPARE(merged.appState.value(u"gridSize"_s).toInt(), 20);
    }

    void elementsWithoutIds()
    {
        // Hand-made scenes: elements without ids are matched by their content.
        Stroke a;
        a.points = {{0, 0, 0.5}, {10, 10, 0.5}};
        Stroke b = a;
        b.points.append({20, 0, 0.5});
        QVERIFY(mergeKey(a).startsWith(u'~'));
        QCOMPARE(mergeKey(a), mergeKey(Stroke(a)));
        QVERIFY(mergeKey(a) != mergeKey(b));
        Drawing ours;
        ours.strokes = {a, b};
        Drawing theirs;
        theirs.strokes = {a};
        const Drawing merged = brulion::drawing::merge(MergeBase{}, ours, theirs);
        QCOMPARE(merged.strokes.size(), 2);
        Stroke named = a;
        named.id = u"źdźbło"_s;
        QCOMPARE(mergeKey(named), u"źdźbło"_s);
    }
};

QTEST_GUILESS_MAIN(TestDrawingMerge)
#include "tst_drawing_merge.moc"
