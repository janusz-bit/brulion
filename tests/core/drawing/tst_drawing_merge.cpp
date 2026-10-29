// Two versions of a drawing merged element by element (core drawing/merge.h): the version another
// device wrote taken in while the drawing is open (a known base: the last load or save), and the
// sync center's merge of a conflict copy (no known base: a union). New here on top in ours' order
// (a backdrop new here at the bottom), changed here wins, deleted here goes, deleted there stays
// deleted, the pattern and the theme ours only when changed here, files of both. How two versions
// relate (mergeCounts, sameDrawing, containsDrawing: the sync center's cards), and Nextcloud's
// merge of drawing files against the version both had (mergeDrawingFiles: SVG, `.excalidraw`, the
// Obsidian Excalidraw plugin's Markdown kept around the scene; everything it refuses). Random
// merges keep their invariants.
#include "drawing/drawing.h"
#include "drawing/drawingfile.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/merge.h"
#include "drawing/svgwrite.h"

#include <QFile>
#include <QRandomGenerator>
#include <QSet>
#include <QTest>

#include <algorithm>

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

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

/// A freehand line `id` (version `version`) at a height of its own — the same id lies at the same
/// place in every drawing (as tst_drawingcanvas strokesFile).
Stroke line(const QString &id, int version = 1)
{
    int sum = 0;
    for (const QChar c : id)
        sum += c.unicode();
    const double y = 40 + 30 * (sum % 40);
    Stroke s;
    s.id = id;
    s.version = version;
    s.points = {{100, y, 0.5}, {200, y + 5, 0.5}, {300, y, 0.5}};
    return s;
}

/// A drawing of freehand lines ("id" or "id:version").
Drawing linesOf(const QString &spec, Background background = Background::Plain)
{
    Drawing d;
    d.background = background;
    for (const QString &token : spec.split(u' ', Qt::SkipEmptyParts)) {
        const QStringList parts = token.split(u':');
        d.strokes.append(line(parts.at(0), parts.value(1, u"1"_s).toInt()));
    }
    return d;
}

QByteArray svgOf(const QString &spec)
{
    return toSvg(linesOf(spec));
}

QString idsOf(const Drawing &d)
{
    QStringList out;
    for (const Stroke &s : d.strokes)
        out.append(s.id);
    return out.join(u' ');
}

/// The ids of the drawing in a file (nullopt: none read).
std::optional<QString> idsOfFile(const QByteArray &bytes)
{
    const std::optional<DrawingFile> file = readDrawingFile(bytes);
    if (!file)
        return std::nullopt;
    return idsOf(file->drawing);
}

/// A `.excalidraw` file of `spec`.
QByteArray excalidrawOf(const QString &spec)
{
    DrawingFile file;
    file.format = FileFormat::Excalidraw;
    return writeDrawingFile(file, linesOf(spec));
}

constexpr qint64 Now = 1761650000000;

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
        QTest::newRow("erased here, new there") << u"a b"_s << u"b"_s << u"a b c"_s << u"b:1 c:1"_s;
        QTest::newRow("erased there, new here") << u"a b"_s << u"a b x"_s << u"b"_s << u"b:1 x:1"_s;
        QTest::newRow("erased on both sides") << u"a b"_s << u"b"_s << u"b"_s << u"b:1"_s;
        QTest::newRow("unknown base: union in their order") << u"?"_s << u"a b x"_s << u"a c"_s << u"a:1 c:1 b:1 x:1"_s;
        QTest::newRow("unknown base: union of equal ones") << u"?"_s << u"a b"_s << u"a b"_s << u"a:1 b:1"_s;
        QTest::newRow("Polish ids") << u"ż łódź-1"_s << u"ż łódź-1:2 Ćma"_s << u"ż łódź-1 źdźbło"_s
                                    << u"ż:1 łódź-1:2 źdźbło:1 Ćma:1"_s;
        QTest::newRow("an id twice there: kept as it is") << u"a"_s << u"a x"_s << u"a b a"_s << u"a:1 b:1 a:1 x:1"_s;
        QTest::newRow("empty drawings") << u""_s << u""_s << u""_s << u""_s;
        QTest::newRow("everything erased here") << u"a b"_s << u""_s << u"a b"_s << u""_s;
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

    void mergeVersions_data()
    {
        // One element "a" on both sides: which one the merge keeps (ours red, theirs green).
        QTest::addColumn<int>("base"); // 0: none known
        QTest::addColumn<int>("ours");
        QTest::addColumn<int>("theirs");
        QTest::addColumn<bool>("oursKept");
        QTest::newRow("unknown base: ours newer") << 0 << 3 << 2 << true;
        QTest::newRow("unknown base: theirs newer") << 0 << 2 << 3 << false;
        QTest::newRow("unknown base: a tie keeps theirs") << 0 << 2 << 2 << false;
        QTest::newRow("changed here") << 1 << 2 << 1 << true;
        QTest::newRow("changed there") << 1 << 1 << 3 << false;
        QTest::newRow("changed on both sides: ours") << 1 << 2 << 3 << true;
    }
    void mergeVersions()
    {
        QFETCH(int, base);
        QFETCH(int, ours);
        QFETCH(int, theirs);
        QFETCH(bool, oursKept);
        Drawing mine = scene(u"a:"_s + QString::number(ours));
        mine.strokes[0].color = u"#e03131"_s;
        Drawing other = scene(u"a:"_s + QString::number(theirs));
        other.strokes[0].color = u"#2f9e44"_s;
        const MergeBase b = base ? mergeBase(scene(u"a:"_s + QString::number(base))) : MergeBase{};
        const Drawing merged = brulion::drawing::merge(b, mine, other);
        QCOMPARE(merged.strokes.size(), 1);
        QCOMPARE(merged.strokes.first().color, oursKept ? u"#e03131"_s : u"#2f9e44"_s);
        QCOMPARE(merged.strokes.first().version, oursKept ? ours : theirs);
    }

    void mergeIsDeterministic()
    {
        const MergeBase b = mergeBase(linesOf(u"a b c"_s));
        const Drawing ours = linesOf(u"a c:2 x y"_s, Background::Lines);
        const Drawing theirs = linesOf(u"b c a z"_s, Background::Grid);
        const QByteArray first = sceneJson(brulion::drawing::merge(b, ours, theirs));
        QCOMPARE(sceneJson(brulion::drawing::merge(b, ours, theirs)), first);
        QCOMPARE(idsOf(brulion::drawing::merge(b, ours, theirs)), u"c a z x y"_s);
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

    // ---- How two versions relate ------------------------------------------------------------------

    void counts()
    {
        const Drawing original = linesOf(u"a b c"_s);
        const Drawing copy = linesOf(u"a b d"_s);
        const Drawing merged = brulion::drawing::merge(MergeBase{}, copy, original);
        QCOMPARE(idsOf(merged), u"a b c d"_s);
        const MergeCounts c = mergeCounts(original, copy, merged);
        QCOMPARE(c.ours, 3);
        QCOMPARE(c.theirs, 3);
        QCOMPARE(c.common, 2);
        QCOMPARE(c.onlyOurs, 1);
        QCOMPARE(c.onlyTheirs, 1);
        QCOMPARE(c.merged, 4);
        // Versions do not matter, keys do; an empty side.
        const MergeCounts d = mergeCounts(linesOf(u"a:3 ż"_s), Drawing(), linesOf(u"a ż"_s));
        QCOMPARE(d.ours, 2);
        QCOMPARE(d.theirs, 0);
        QCOMPARE(d.common, 0);
        QCOMPARE(d.onlyOurs, 2);
        QCOMPARE(d.onlyTheirs, 0);
        QCOMPARE(d.merged, 2);
    }

    void relations_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("bBackground");
        QTest::addColumn<bool>("same");
        QTest::addColumn<bool>("aContainsB");
        const int plain = int(Background::Plain);
        QTest::newRow("equal") << u"a b"_s << u"a b"_s << plain << true << true;
        QTest::newRow("a subset") << u"a b c"_s << u"a b"_s << plain << false << true;
        QTest::newRow("a superset") << u"a b"_s << u"a b c"_s << plain << false << false;
        QTest::newRow("an older version") << u"a:3 b"_s << u"a:2 b"_s << plain << false << true;
        QTest::newRow("a newer version") << u"a:2 b"_s << u"a:3 b"_s << plain << false << false;
        QTest::newRow("another order") << u"a b"_s << u"b a"_s << plain << false << true;
        QTest::newRow("another pattern") << u"a b"_s << u"a b"_s << int(Background::Lines) << false << false;
        QTest::newRow("empty in a drawing") << u"a"_s << u""_s << plain << false << true;
        QTest::newRow("Polish ids") << u"żółw łódź"_s << u"łódź"_s << plain << false << true;
    }
    void relations()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, bBackground);
        QFETCH(bool, same);
        QFETCH(bool, aContainsB);
        const Drawing x = linesOf(a);
        const Drawing y = linesOf(b, Background(bBackground));
        QCOMPARE(sameDrawing(x, y), same);
        QCOMPARE(sameDrawing(y, x), same);
        QCOMPARE(containsDrawing(x, y), aContainsB);
    }

    void relationsLookAtTheContent()
    {
        // The same scene written by another program (other bytes around it): the same drawing.
        const QByteArray file = svgOf(u"a b"_s);
        QByteArray other = file;
        other.replace("</svg>", "<!-- zapisane gdzie indziej: zażółć --></svg>");
        const std::optional<Drawing> x = readDrawing(file);
        const std::optional<Drawing> y = readDrawing(other);
        QVERIFY(x && y);
        QVERIFY(file != other);
        QVERIFY(sameDrawing(*x, *y));
        // The same version with other points: changed by hand — neither the same nor contained.
        Drawing moved = linesOf(u"a b"_s);
        moved.strokes[1].points[0].x += 40;
        QVERIFY(!sameDrawing(linesOf(u"a b"_s), moved));
        QVERIFY(!containsDrawing(linesOf(u"a b c"_s), moved));
        // A pattern, a theme or a paper only the copy has is not contained; the defaults are.
        Drawing grid = linesOf(u"a"_s, Background::Grid);
        QVERIFY(containsDrawing(grid, linesOf(u"a"_s)));
        Drawing dark = linesOf(u"a"_s);
        dark.theme = PaperTheme::Dark;
        QVERIFY(!containsDrawing(linesOf(u"a"_s), dark));
        QVERIFY(containsDrawing(dark, linesOf(u"a"_s)));
        QVERIFY(!sameDrawing(dark, linesOf(u"a"_s)));
        Drawing cream = linesOf(u"a"_s);
        cream.paper = u"#FFFDF5"_s;
        QVERIFY(!containsDrawing(linesOf(u"a"_s), cream));
        cream.paper = u"#ffffff"_s.toUpper();
        QVERIFY(sameDrawing(cream, linesOf(u"a"_s)));
        // Elements Brulion does not draw count too.
        Drawing framed = linesOf(u"a"_s);
        framed.foreign.append(QJsonObject{{u"id"_s, u"ramka"_s}, {u"type"_s, u"frame"_s}});
        QVERIFY(!sameDrawing(framed, linesOf(u"a"_s)));
        QVERIFY(!containsDrawing(linesOf(u"a"_s), framed));
        QVERIFY(containsDrawing(framed, linesOf(u"a"_s)));
    }

    // ---- Nextcloud: files merged against the version both had ---------------------------------

    void mergeDrawingFiles_data()
    {
        QTest::addColumn<QByteArray>("base");
        QTest::addColumn<QByteArray>("local");
        QTest::addColumn<QByteArray>("remote");
        QTest::addColumn<QString>("merged"); // "-": no merge (both kept)
        QTest::newRow("both drew") << svgOf(u"s1"_s) << svgOf(u"s1 s3"_s) << svgOf(u"s1 s2"_s) << u"s1 s2 s3"_s;
        QTest::newRow("erased here, drawn there") << svgOf(u"s1 s2"_s) << svgOf(u"s2"_s) << svgOf(u"s1 s2 s3"_s)
                                                  << u"s2 s3"_s;
        QTest::newRow("erased there, drawn here") << svgOf(u"s1 s2"_s) << svgOf(u"s1 s2 s4"_s) << svgOf(u"s2"_s)
                                                  << u"s2 s4"_s;
        QTest::newRow("changed here and there") << svgOf(u"s1 s2"_s) << svgOf(u"s1:2 s2"_s) << svgOf(u"s1 s2:5 ż"_s)
                                                << u"s1 s2 ż"_s;
        QTest::newRow("only one side changed") << svgOf(u"s1"_s) << svgOf(u"s1"_s) << svgOf(u"s1 łódź"_s)
                                               << u"s1 łódź"_s;
        QTest::newRow(".excalidraw") << excalidrawOf(u"s1"_s) << excalidrawOf(u"s1 s3"_s) << excalidrawOf(u"s1 s2"_s)
                                     << u"s1 s2 s3"_s;
        QTest::newRow("a frame there: not editable") << svgOf(u"s1"_s) << svgOf(u"s1 s3"_s)
                                                     << fixture(u"excalidraw_frame.svg"_s) << u"-"_s;
        QTest::newRow("a frame here") << svgOf(u"s1"_s) << fixture(u"excalidraw_frame.svg"_s) << svgOf(u"s1 s2"_s)
                                      << u"-"_s;
        QByteArray broken = svgOf(u"s1"_s);
        const qsizetype start = broken.indexOf("<!-- payload-start -->") + 22;
        broken.replace(start, 12, "@@@ zepsute ąę");
        QTest::newRow("a broken base") << broken << svgOf(u"s1 s3"_s) << svgOf(u"s1 s2"_s) << u"-"_s;
        QTest::newRow("a broken side") << svgOf(u"s1"_s) << broken << svgOf(u"s1 s2"_s) << u"-"_s;
        QTest::newRow("a plain picture") << svgOf(u"s1"_s) << svgOf(u"s1 s3"_s)
                                         << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>") << u"-"_s;
        QTest::newRow("no base") << QByteArray() << svgOf(u"s1 s3"_s) << svgOf(u"s1 s2"_s) << u"-"_s;
        QTest::newRow("formats differ") << svgOf(u"s1"_s) << svgOf(u"s1 s3"_s) << excalidrawOf(u"s1 s2"_s) << u"-"_s;
        QTest::newRow("not a drawing at all") << QByteArray("# Notatka\n") << QByteArray("# Notatka ż\n")
                                              << QByteArray("# Notatka ź\n") << u"-"_s;
        // A line moved without a new version (by hand, in a text editor): versions say nothing.
        Drawing moved = linesOf(u"s1 s2"_s);
        moved.strokes[0].points[1].y += 30;
        QTest::newRow("changed without a new version here") << svgOf(u"s1 s2"_s) << toSvg(moved)
                                                            << svgOf(u"s1 s2 s3"_s) << u"-"_s;
        QTest::newRow("changed without a new version there") << svgOf(u"s1 s2"_s) << svgOf(u"s1 s2 s3"_s)
                                                             << toSvg(moved) << u"-"_s;
    }
    void mergeDrawingFiles()
    {
        QFETCH(QByteArray, base);
        QFETCH(QByteArray, local);
        QFETCH(QByteArray, remote);
        QFETCH(QString, merged);
        const std::optional<QByteArray> out = brulion::drawing::mergeDrawingFiles(base, local, remote, Now);
        if (merged == u"-") {
            QVERIFY(!out);
            return;
        }
        QVERIFY(out);
        QCOMPARE(idsOfFile(*out), std::optional<QString>(merged));
        // In the remote's format; read back as a drawing by every reader.
        QCOMPARE(readDrawingFile(*out)->format, readDrawingFile(remote)->format);
        QVERIFY(readDrawing(*out));
        // The same inputs, the same bytes.
        QCOMPARE(brulion::drawing::mergeDrawingFiles(base, local, remote, Now), out);
    }

    void mergePluginFiles()
    {
        // An Obsidian Excalidraw plugin file ("```json"): a line drawn here, one erased there.
        const QByteArray base = fixture(u"obsidian_json.excalidraw.md"_s);
        const std::optional<DrawingFile> file = readDrawingFile(base);
        QVERIFY(file);
        QCOMPARE(file->format, FileFormat::ExcalidrawMarkdown);
        QCOMPARE(file->drawing.strokes.size(), 2);
        const QString kept = file->drawing.strokes.at(0).id;
        Drawing mine = file->drawing;
        mine.strokes.append(line(u"tutaj-ż"_s));
        const QByteArray local = writeDrawingFile(*file, mine, Now);
        Drawing theirs = file->drawing;
        theirs.strokes.removeLast();
        const QByteArray remote = writeDrawingFile(*file, theirs, Now);
        const std::optional<QByteArray> merged = brulion::drawing::mergeDrawingFiles(base, local, remote, Now);
        QVERIFY(merged);
        QCOMPARE(idsOfFile(*merged), std::optional<QString>(kept + u" tutaj-ż"_s));
        // The Markdown before the scene (frontmatter, the back of the card) byte for byte.
        const qsizetype scene = base.indexOf("## Drawing");
        QVERIFY(scene > 0);
        QVERIFY(merged->startsWith(base.first(scene)));
        QVERIFY(merged->contains("Notatka z tyłu: Zażółć gęślą jaźń"));

        // The back of the card changed here only: this side's Markdown, the merged scene in it.
        QByteArray noted = local;
        noted.replace("Notatka z tyłu:", "Notatka z tyłu (dopisane tutaj, ąę):");
        const std::optional<QByteArray> withNote = brulion::drawing::mergeDrawingFiles(base, noted, remote, Now);
        QVERIFY(withNote);
        QVERIFY(withNote->contains("dopisane tutaj, ąę"));
        QCOMPARE(idsOfFile(*withNote), std::optional<QString>(kept + u" tutaj-ż"_s));
        // … there only: theirs.
        QByteArray notedThere = remote;
        notedThere.replace("Notatka z tyłu:", "Notatka z tyłu (dopisane tam, źż):");
        const std::optional<QByteArray> theirNote = brulion::drawing::mergeDrawingFiles(base, local, notedThere, Now);
        QVERIFY(theirNote);
        QVERIFY(theirNote->contains("dopisane tam, źż"));
        QCOMPARE(idsOfFile(*theirNote), std::optional<QString>(kept + u" tutaj-ż"_s));
        // … on both sides, differently: no merge of the notes — both versions are kept.
        QVERIFY(!brulion::drawing::mergeDrawingFiles(base, noted, notedThere, Now));
        // … on both sides the same way: fine.
        QByteArray sameNote = remote;
        sameNote.replace("Notatka z tyłu:", "Notatka z tyłu (dopisane tutaj, ąę):");
        QVERIFY(brulion::drawing::mergeDrawingFiles(base, noted, sameNote, Now));
        // The plugin's oldest form is never written.
        const QByteArray legacy = fixture(u"obsidian_legacy.excalidraw.md"_s);
        QVERIFY(!brulion::drawing::mergeDrawingFiles(legacy, legacy, legacy, Now));
    }

    void mergeCompressedPluginFiles()
    {
        // "```compressed-json" with a text element: the merged file keeps its encoding and text.
        const QByteArray base = fixture(u"obsidian_crlf.excalidraw.md"_s);
        const std::optional<DrawingFile> file = readDrawingFile(base);
        QVERIFY(file);
        Drawing mine = file->drawing;
        mine.strokes.append(line(u"tu"_s));
        Drawing theirs = file->drawing;
        theirs.strokes.append(line(u"tam"_s));
        const std::optional<QByteArray> merged = brulion::drawing::mergeDrawingFiles(
                base, writeDrawingFile(*file, mine, Now), writeDrawingFile(*file, theirs, Now), Now);
        QVERIFY(merged);
        QVERIFY(merged->contains("```compressed-json\r\n"));
        QVERIFY(merged->contains("Zażółć gęślą jaźń ^Zt8xQw1a"));
        const std::optional<DrawingFile> back = readDrawingFile(*merged);
        QVERIFY(back);
        QCOMPARE(back->drawing.strokes.size(), file->drawing.strokes.size() + 2);
        QCOMPARE(back->drawing.strokes.at(back->drawing.strokes.size() - 2).id, u"tam"_s);
        QCOMPARE(back->drawing.strokes.last().id, u"tu"_s);
    }

    // ---- The sync center: the Markdown around a plugin file's scene -------------------------------

    void keepsMarkdownOf_data()
    {
        // `into` is the original (written with the merge), `other` the conflict copy — both from
        // obsidian_mixed.excalidraw.md: two text entries, an element link, an embedded file.
        QTest::addColumn<QByteArray>("into");
        QTest::addColumn<QByteArray>("other");
        QTest::addColumn<bool>("keeps");
        const QByteArray base = fixture(u"obsidian_mixed.excalidraw.md"_s);
        const std::optional<DrawingFile> file = readDrawingFile(base);
        QVERIFY(file);
        QVERIFY(base.contains("Zażółć gęślą jaźń ^Zt8xQw1a"));
        const auto edited = [&](const QByteArray &bytes, const char *from, const char *to) {
            QByteArray out = bytes;
            out.replace(from, to);
            return out;
        };
        const auto withText = [&](const QString &id, const QString &text, int raise) {
            Drawing d = file->drawing;
            for (Stroke &s : d.strokes) {
                if (s.id == id) {
                    s.text = s.originalText = text;
                    s.version += raise;
                }
            }
            return writeDrawingFile(*file, d, Now);
        };
        Drawing drawn = file->drawing;
        drawn.strokes.append(line(u"kreska-ż"_s));
        const QByteArray withLine = writeDrawingFile(*file, drawn, Now);

        QTest::newRow("the same file") << base << base << true;
        QTest::newRow("a line drawn there") << base << withLine << true;
        QTest::newRow("a line drawn here") << withLine << base << true;
        QTest::newRow("the back of the card changed there")
                << base << edited(withLine, "Notatka z tyłu:", "Notatka z tyłu (z laptopa, ąę):") << false;
        QTest::newRow("the back of the card changed here")
                << edited(base, "Notatka z tyłu:", "Notatka z tyłu (tutaj):") << base << false;
        QTest::newRow("the frontmatter changed there")
                << base << edited(base, "tags: [excalidraw]", "tags: [excalidraw, szkic]") << false;
        QTest::newRow("an element link changed there")
                << base << edited(base, "[[Plan ogrodu]]", "[[Plan ogródka]]") << false;
        QTest::newRow("an embedded file added there")
                << base << edited(base, "## Embedded Files\n", "## Embedded Files\nffff0000ffff0000: [[Zdjęcie.png]]\n")
                << false;
        QTest::newRow("CRLF there") << base << edited(base, "\n", "\r\n") << false;
        // Text entries follow their elements.
        const QByteArray newerThere = withText(u"Zt8xQw1a"_s, u"Źdźbło trawy"_s, 1);
        QVERIFY(newerThere.contains("Źdźbło trawy ^Zt8xQw1a"));
        QTest::newRow("a text changed there") << base << newerThere << true;
        QTest::newRow("a text changed here") << newerThere << base << true;
        QTest::newRow("a text edited by hand there (the same version)")
                << base << edited(base, "Zażółć gęślą jaźń ^Zt8xQw1a", "Zażółć [[gęślą]] jaźń ^Zt8xQw1a") << false;
        QTest::newRow("a text edited by hand here")
                << edited(base, "Zażółć gęślą jaźń ^Zt8xQw1a", "Zażółć [[gęślą]] jaźń ^Zt8xQw1a") << base << false;
        // A newer version of the text with only its entry changed (a link added): the writer keeps
        // the original's entry, so the copy's is lost.
        Drawing raised = file->drawing;
        for (Stroke &s : raised.strokes) {
            if (s.id == u"Zt8xQw1a")
                ++s.version;
        }
        const QByteArray linked = edited(writeDrawingFile(*file, raised, Now), "Zażółć gęślą jaźń ^Zt8xQw1a",
                                         "Zażółć [[gęślą]] jaźń ^Zt8xQw1a");
        QVERIFY(linked.contains("[[gęślą]]"));
        QTest::newRow("only a link added there") << base << linked << false;
        // A text element only the copy has, with its entry (ids of 8 characters, as the plugin's):
        // its text is in the scene as well.
        Drawing texted = file->drawing;
        Stroke t;
        t.type = ElementType::Text;
        t.id = u"nowyTkst"_s;
        t.text = t.originalText = u"Nowy tekst ąę"_s;
        t.box = QRectF(40, 400, 160, 25);
        texted.strokes.append(t);
        const QByteArray newText = edited(writeDrawingFile(*file, texted, Now), "## Element Links",
                                          "Nowy tekst ąę ^nowyTkst\n\n## Element Links");
        const std::optional<DrawingFile> withNewText = readDrawingFile(newText);
        QVERIFY(withNewText);
        QVERIFY(std::any_of(withNewText->markdown->textEntries.cbegin(), withNewText->markdown->textEntries.cend(),
                            [](const ExcalidrawMarkdown::TextEntry &e) { return e.id == u"nowyTkst"; }));
        QTest::newRow("a text new there") << base << newText << true;
        // Other formats have nothing around their scene.
        QTest::newRow("an SVG copy") << base << svgOf(u"a"_s) << true;
        QTest::newRow("an SVG original, a plugin copy") << svgOf(u"a"_s) << base << false;
        QTest::newRow("two SVGs") << svgOf(u"a"_s) << svgOf(u"b"_s) << true;
    }
    void keepsMarkdownOf()
    {
        QFETCH(QByteArray, into);
        QFETCH(QByteArray, other);
        QFETCH(bool, keeps);
        const std::optional<DrawingFile> a = readDrawingFile(into);
        const std::optional<DrawingFile> b = readDrawingFile(other);
        QVERIFY(a);
        QVERIFY(b);
        QCOMPARE(brulion::drawing::keepsMarkdownOf(*a, *b), keeps);
    }

    void markdownAroundScene()
    {
        const QByteArray base = fixture(u"obsidian_mixed.excalidraw.md"_s);
        const std::optional<DrawingFile> file = readDrawingFile(base);
        QVERIFY(file);
        const QByteArray around = brulion::drawing::markdownAroundScene(*file);
        QVERIFY(around.startsWith(base.first(base.indexOf("## Drawing"))));
        QVERIFY(around.contains("Notatka z tyłu: Zażółć gęślą jaźń"));
        QVERIFY(around.contains("```compressed-json\n```"));
        QCOMPARE(around, base.first(file->markdown->sceneFrom) + base.sliced(file->markdown->sceneTo));
        QCOMPARE(brulion::drawing::markdownAroundScene(*readDrawingFile(svgOf(u"a"_s))), QByteArray());
        QCOMPARE(brulion::drawing::markdownAroundScene(*readDrawingFile(excalidrawOf(u"a"_s))), QByteArray());
    }

    void randomMergesKeepTheirInvariants()
    {
        // Random versions of a drawing (ids from a small pool, Polish ones too): whatever is
        // merged, theirs not erased here stays in their order, what is new here is added, no key
        // twice.
        QRandomGenerator random(42);
        const QStringList pool{u"a"_s, u"b"_s, u"c"_s, u"ż"_s, u"łódź"_s, u"Ćma"_s, u"x"_s, u"y"_s, u"z"_s};
        const auto pick = [&](const QStringList &from, int chance) {
            QStringList out;
            for (const QString &id : from) {
                if (int(random.bounded(100)) < chance)
                    out.append(id);
            }
            return out;
        };
        for (int round = 0; round < 500; ++round) {
            const QStringList base = pick(pool, 50);
            QStringList ours = pick(base, 70) + pick(pool, 20);
            QStringList theirs = pick(base, 70) + pick(pool, 20);
            ours.removeDuplicates();
            theirs.removeDuplicates();
            const bool known = random.bounded(2) == 0;
            const MergeBase b = known ? mergeBase(linesOf(base.join(u' '))) : MergeBase{};
            const Drawing merged =
                    brulion::drawing::merge(b, linesOf(ours.join(u' ')), linesOf(theirs.join(u' ')));
            QStringList result;
            for (const Stroke &s : merged.strokes)
                result.append(s.id);
            const QString what = u"base %1, ours %2, theirs %3 → %4"_s.arg(base.join(u','), ours.join(u','),
                                                                           theirs.join(u','), result.join(u','));
            QCOMPARE(QSet<QString>(result.cbegin(), result.cend()).size(), result.size());
            qsizetype at = 0;
            for (const QString &id : std::as_const(theirs)) {
                const bool erasedHere = known && base.contains(id) && !ours.contains(id);
                if (erasedHere) {
                    QVERIFY2(!result.contains(id), qPrintable(what));
                    continue;
                }
                const qsizetype found = result.indexOf(id, at);
                QVERIFY2(found >= at, qPrintable(what)); // in their order
                at = found + 1;
            }
            for (const QString &id : std::as_const(ours)) {
                const bool erasedThere = known && base.contains(id) && !theirs.contains(id);
                QCOMPARE(result.contains(id), !erasedThere);
            }
        }
    }
};

QTEST_GUILESS_MAIN(TestDrawingMerge)
#include "tst_drawing_merge.moc"
