// Two devices syncing through one WebDAV server: edits, deletions, concurrent journal appends,
// real conflicts (text and binary), drawings changed on both devices (merged element by element,
// both versions kept when they cannot be), first sync with content on both sides, the vault
// config, line endings, parallel transfers, files that are not UTF-8, failing conflict uploads,
// renames.
#include "synctest.h"

#include "drawing/drawingfile.h"
#include "drawing/svgwrite.h"
#include "vault/vaultconfig.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>

using namespace synctest;

namespace {

const QString kStamp = u"20261014-100000"_s;

/// Server, clock and two devices ("laptop" = a, "telefon" = b).
struct Pair {
    explicit Pair(const QMap<QString, QByteArray> &aFiles = {}, const QMap<QString, QByteArray> &bFiles = {},
                  FakeWebDavServer::Options options = {})
        : server(serverOptions(clock, std::move(options)))
        , a(server, u"laptop"_s, aFiles, withClock())
        , b(server, u"telefon"_s, bFiles, withClock())
    {
    }
    DeviceOptions withClock()
    {
        DeviceOptions o;
        o.clock = &clock;
        return o;
    }
    TestClock clock;
    FakeWebDavServer server;
    Device a;
    Device b;
};

QStringList conflictCopies(const Device &d)
{
    QStringList out;
    for (const QString &path : d.files().keys()) {
        if (path.contains(u".conflict-"_s))
            out.append(path);
    }
    return out;
}

QJsonObject jsonOf(const QString &text)
{
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

/// A drawing of freehand lines as Brulion writes it: one line per id, at a height of its own (the
/// same id is the same line in every version); `foreign`: elements Brulion does not draw.
QString drawingSvg(const QStringList &ids, const QJsonArray &foreign = {})
{
    brulion::drawing::Drawing d;
    for (const QString &id : ids) {
        brulion::drawing::Stroke s;
        s.id = id;
        const double y = 50 + 40 * (id.back().unicode() % 12);
        s.points = {{100, y, 0.5}, {200, y + 10, 0.5}, {300, y, 0.5}};
        d.strokes.append(s);
    }
    d.foreign = foreign;
    return QString::fromUtf8(brulion::drawing::toSvg(d));
}

/// The ids of the elements of the drawing in `text` ("<no drawing>" when it holds none).
QStringList drawingIds(const QString &text)
{
    const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(text.toUtf8());
    if (!file)
        return {u"<no drawing>"_s};
    QStringList ids;
    for (const brulion::drawing::Stroke &s : file->drawing.strokes)
        ids.append(s.id);
    return ids;
}

QByteArray fixture(const QString &name)
{
    QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + name);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", qPrintable(name));
    return f.readAll();
}

// "Zażółć gęślą\n" in Windows-1250 (common for CSV files exported by Excel in Poland).
const QByteArray kCp1250("\x5a\x61\xbf\xf3\xb3\xe6\x20\x67\xea\x9c\x6c\xb9\x0a", 13);

} // namespace

class TestSyncDevices : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void propagatesCreateEditAndDelete()
    {
        Pair p({{u"Strona.md"_s, "v1\n"}, {u"Zażółć.md"_s, u8(u"ź\n"_s)}});
        p.a.sync();
        QCOMPARE(counts(p.b.sync()), (Counts{.down = 2}));
        QCOMPARE(p.b.files(), (QMap<QString, QString>{{u"Strona.md"_s, u"v1\n"_s}, {u"Zażółć.md"_s, u"ź\n"_s}}));

        QVERIFY(p.b.write(u"Strona.md"_s, u"v2 z telefonu\n"_s));
        QVERIFY(p.b.write(u"folder/Nowa.md"_s, u"nowa\n"_s));
        QVERIFY(p.b.remove(u"Zażółć.md"_s));
        QCOMPARE(counts(p.b.sync()), (Counts{.up = 2, .delR = 1}));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 2, .delL = 1}));
        const QMap<QString, QString> expected{{u"Strona.md"_s, u"v2 z telefonu\n"_s}, {u"folder/Nowa.md"_s, u"nowa\n"_s}};
        QCOMPARE(p.a.files(), expected);
        QCOMPARE(p.a.trash(), QStringList{u".trash/Zażółć.md"_s});
        QCOMPARE(p.server.files(kFolder), expected);
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void journalAppendsOnBothDevicesMergeWithoutAConflict()
    {
        const QString journal = u"journals/2026-10-14.md"_s;
        Pair p({{journal, "- 08:00 kawa\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(journal, u"- 08:00 kawa\n- 09:00 spotkanie z Łucją\n"_s));
        QVERIFY(p.b.write(journal, u"- 08:00 kawa\n- 12:30 obiad: żurek\n"_s));
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 1}));
        QCOMPARE(counts(p.b.sync()), (Counts{.merged = 1}));
        // Union of both insertions: this device's lines first, then the ones from the server.
        const QString merged = u"- 08:00 kawa\n- 12:30 obiad: żurek\n- 09:00 spotkanie z Łucją\n"_s;
        QCOMPARE(p.b.read(journal), merged);
        QCOMPARE(p.server.text(kFolder + u'/' + journal), std::optional<QString>(merged));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(journal), merged);
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void mergesEditsOfDifferentParts()
    {
        Pair p({{u"n.md"_s, u8(u"# Tytuł\n\nakapit 1\n\nakapit 2\n\nakapit 3\n"_s)}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(u"n.md"_s, u"# Tytuł zmieniony\n\nakapit 1\n\nakapit 2\n\nakapit 3\n"_s));
        QVERIFY(p.b.write(u"n.md"_s, u"# Tytuł\n\nakapit 1\n\nakapit 2\n\nakapit 3 — poprawiony\n"_s));
        p.a.sync();
        QCOMPARE(counts(p.b.sync()), (Counts{.merged = 1}));
        p.a.sync();
        const QString expected = u"# Tytuł zmieniony\n\nakapit 1\n\nakapit 2\n\nakapit 3 — poprawiony\n"_s;
        QCOMPARE(p.a.read(u"n.md"_s), expected);
        QCOMPARE(p.b.read(u"n.md"_s), expected);
    }

    void aRealConflictKeepsBothVersions()
    {
        Pair p({{u"Zakupy.md"_s, "- [ ] chleb\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(u"Zakupy.md"_s, u"- [ ] chleb razowy\n"_s));
        QVERIFY(p.b.write(u"Zakupy.md"_s, u"- [ ] chleb żytni\n"_s));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        const QString copy = u"Zakupy.conflict-telefon-"_s + kStamp + u".md"_s;
        QCOMPARE(counts(rb), (Counts{.conflicts = 1}));
        QCOMPARE(rb.conflicts, QList<VaultPath>{u"Zakupy.md"_s});
        QCOMPARE(rb.conflictCopies, QList<VaultPath>{copy});
        const QMap<QString, QString> expected{{u"Zakupy.md"_s, u"- [ ] chleb żytni\n"_s}, {copy, u"- [ ] chleb razowy\n"_s}};
        QCOMPARE(p.b.files(), expected);
        QCOMPARE(p.server.files(kFolder), expected);
        // The other device receives both.
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 2}));
        QCOMPARE(p.a.files(), expected);
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void excalidrawDrawingsMergeElementByElement_data()
    {
        // An Obsidian Excalidraw plugin file with its scene as JSON ("```json"): the two devices
        // move different elements, lines far apart — a line merge would be "clean" and might glue
        // two scenes into broken JSON: merged element by element instead (each move raises the
        // element's version, as every editor does). Moved without a new version (by hand): no
        // merge by versions, both versions kept. The same text without the plugin's key merges
        // line by line.
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("head");
        QTest::addColumn<bool>("drawing");
        QTest::addColumn<bool>("crlf");
        QTest::addColumn<bool>("versioned");
        const QString plugin = u"---\n\nexcalidraw-plugin: parsed\ntags: [excalidraw]\n\n---\nNotatka z tyłu: "
                               u"ąćęłńóśźż\n\n# Excalidraw Data\n\n## Text Elements\n%%\n## Drawing\n```json\n"_s;
        QTest::newRow("plugin file") << u"Excalidraw/Szkic ćwiczeń.excalidraw.md"_s << plugin << true << false << true;
        QTest::newRow("CRLF plugin file") << u"Szkic.excalidraw.md"_s << plugin << true << true << true;
        QTest::newRow("plugin file edited by hand") << u"Szkic źdźbła.excalidraw.md"_s << plugin << true << false
                                                    << false;
        QTest::newRow("a note") << u"Notatka.md"_s << u"---\ntytul: Szkic\n---\n```json\n"_s << false << false
                                << true;
    }

    void excalidrawDrawingsMergeElementByElement()
    {
        QFETCH(QString, path);
        QFETCH(QString, head);
        QFETCH(bool, drawing);
        QFETCH(bool, crlf);
        QFETCH(bool, versioned);
        const auto scene = [&head, crlf, versioned](int x1, int x2) {
            QString elements;
            for (int i = 0; i < 2; ++i) {
                const int x = i == 0 ? x1 : x2;
                const int version = versioned && x != (i == 0 ? 0 : 100) ? 2 : 1;
                elements += u"\t\t{\n\t\t\t\"id\": \"e%1\",\n\t\t\t\"type\": \"rectangle\",\n\t\t\t\"x\": %2,\n"
                            u"\t\t\t\"y\": 10,\n\t\t\t\"width\": 40,\n\t\t\t\"height\": 40,\n\t\t\t\"angle\": 0,\n"
                            u"\t\t\t\"strokeColor\": \"#1e1e1e\",\n\t\t\t\"version\": %3\n\t\t}%4\n"_s.arg(i)
                                    .arg(x)
                                    .arg(version)
                                    .arg(i == 0 ? u","_s : QString());
            }
            QString text = head + u"{\n\t\"type\": \"excalidraw\",\n\t\"version\": 2,\n\t\"elements\": [\n"_s + elements
                           + u"\t],\n\t\"appState\": {},\n\t\"files\": {}\n}\n```\n%%"_s;
            return crlf ? text.replace(u"\n"_s, u"\r\n"_s) : text;
        };
        Pair p({{path, scene(0, 100).toUtf8()}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(path, scene(30, 100)));
        QVERIFY(p.b.write(path, scene(0, 170)));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        if (!drawing) {
            QCOMPARE(counts(rb), (Counts{.merged = 1}));
            QCOMPARE(p.b.read(path), scene(30, 170));
            return;
        }
        if (!versioned) {
            // Both versions kept, each a whole drawing.
            QCOMPARE(counts(rb), (Counts{.conflicts = 1}));
            QCOMPARE(rb.conflicts, QList<VaultPath>{path});
            QCOMPARE(rb.conflictCopies.size(), 1);
            QCOMPARE(p.b.read(path), scene(0, 170));
            QCOMPARE(p.b.read(rb.conflictCopies.first()), scene(30, 100));
            QCOMPARE(counts(p.a.sync()), (Counts{.down = 2}));
            QCOMPARE(p.a.read(path), scene(0, 170));
            return;
        }
        // Both moves in one drawing, the Markdown around the scene as it was, no conflict copy.
        QCOMPARE(counts(rb), (Counts{.merged = 1}));
        QVERIFY(rb.conflictCopies.isEmpty());
        const QString merged = p.b.read(path);
        const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(merged.toUtf8());
        QVERIFY(file);
        QCOMPARE(file->format, brulion::drawing::FileFormat::ExcalidrawMarkdown);
        QCOMPARE(file->drawing.strokes.size(), 2);
        QCOMPARE(file->drawing.strokes.at(0).box.x(), 30.0);
        QCOMPARE(file->drawing.strokes.at(1).box.x(), 170.0);
        const QString before = crlf ? QString(head).replace(u"\n"_s, u"\r\n"_s) : head;
        QVERIFY(merged.startsWith(before));
        QVERIFY(conflictCopies(p.b).isEmpty());
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(path), merged);
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void drawingsChangedOnBothDevicesMergeByStroke()
    {
        // Brulion's SVG drawing: a line drawn on each device. A line merge would cut and glue the
        // scene's payload line (a broken drawing); merged line by line of the drawing instead.
        const QString path = u"assets/Rysunek ćwiczeń.svg"_s;
        Pair p({{path, drawingSvg({u"s1"_s}).toUtf8()}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(path, drawingSvg({u"s1"_s, u"s2"_s})));
        QVERIFY(p.b.write(path, drawingSvg({u"s1"_s, u"s3"_s})));
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 1}));
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.merged = 1}));
        QVERIFY(rb.conflictCopies.isEmpty());
        // The server's order, this device's new line on top.
        QCOMPARE(drawingIds(p.b.read(path)), (QStringList{u"s1"_s, u"s2"_s, u"s3"_s}));
        QCOMPARE(p.server.text(kFolder + u'/' + path), std::optional<QString>(p.b.read(path)));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(path), p.b.read(path));
        QVERIFY(conflictCopies(p.a).isEmpty());
        QVERIFY(conflictCopies(p.b).isEmpty());
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void drawingErasedOnOneDeviceStaysErased()
    {
        const QString path = u"assets/Szkic.svg"_s;
        Pair p({{path, drawingSvg({u"s1"_s, u"s2"_s}).toUtf8()}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(path, drawingSvg({u"s2"_s})));
        QVERIFY(p.b.write(path, drawingSvg({u"s1"_s, u"s2"_s, u"s3"_s})));
        p.a.sync();
        QCOMPARE(counts(p.b.sync()), (Counts{.merged = 1}));
        QCOMPARE(drawingIds(p.b.read(path)), (QStringList{u"s2"_s, u"s3"_s}));
        p.a.sync();
        QCOMPARE(drawingIds(p.a.read(path)), (QStringList{u"s2"_s, u"s3"_s}));
        // And the other way round: erased on the device that syncs second.
        QVERIFY(p.a.write(path, drawingSvg({u"s2"_s, u"s3"_s, u"s4"_s})));
        QVERIFY(p.b.write(path, drawingSvg({u"s3"_s})));
        p.a.sync();
        QCOMPARE(counts(p.b.sync()), (Counts{.merged = 1}));
        QCOMPARE(drawingIds(p.b.read(path)), (QStringList{u"s3"_s, u"s4"_s}));
    }

    void drawingWithAFrameChangedOnBothDevicesKeepsBothVersions()
    {
        // One version has a frame from excalidraw.com (an element Brulion does not draw): no merge,
        // both versions kept as for any conflict — the sync center shows them.
        const QString path = u"assets/Plan.svg"_s;
        Pair p({{path, drawingSvg({u"s1"_s}).toUtf8()}});
        p.a.sync();
        p.b.sync();
        const QJsonArray frame{QJsonObject{{u"id"_s, u"ramka"_s}, {u"type"_s, u"frame"_s}, {u"x"_s, 0}, {u"y"_s, 0},
                                           {u"width"_s, 300}, {u"height"_s, 200}, {u"name"_s, u"Ramka"_s}}};
        const QString framed = drawingSvg({u"s1"_s, u"s2"_s}, frame);
        QVERIFY(p.a.write(path, framed));
        QVERIFY(p.b.write(path, drawingSvg({u"s1"_s, u"s3"_s})));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.conflicts = 1}));
        const QString copy = u"assets/Plan.conflict-telefon-"_s + kStamp + u".svg"_s;
        QCOMPARE(rb.conflictCopies, QList<VaultPath>{copy});
        QCOMPARE(drawingIds(p.b.read(path)), (QStringList{u"s1"_s, u"s3"_s}));
        QCOMPARE(p.b.read(copy), framed);
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 2}));
    }

    void twoNewDrawingsWithTheSameNameKeepBoth()
    {
        // Two drawings made apart under one name (names go by the minute): no common version —
        // never mixed automatically, both kept.
        const QString path = u"assets/Rysunek 2026-10-14 10.00.svg"_s;
        Pair p({{path, drawingSvg({u"laptop1"_s}).toUtf8()}}, {{path, drawingSvg({u"telefon1"_s}).toUtf8()}});
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 1}));
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.conflicts = 1}));
        QCOMPARE(rb.conflictCopies.size(), 1);
        QCOMPARE(drawingIds(p.b.read(path)), QStringList{u"telefon1"_s});
        QCOMPARE(drawingIds(p.b.read(rb.conflictCopies.first())), QStringList{u"laptop1"_s});
    }

    void pluginDrawingChangedOnBothDevicesMergesByStroke()
    {
        // The Obsidian Excalidraw plugin's file ("```json", a back of the card): a line drawn in
        // Brulion on one device, one erased on the other.
        const QString path = u"Excalidraw/Szkic ćwiczeń.excalidraw.md"_s;
        const QByteArray original = fixture(u"obsidian_json.excalidraw.md"_s);
        const std::optional<brulion::drawing::DrawingFile> file = brulion::drawing::readDrawingFile(original);
        QVERIFY(file);
        QCOMPARE(file->drawing.strokes.size(), 2);
        const QString kept = file->drawing.strokes.first().id;
        Pair p({{path, original}});
        p.a.sync();
        p.b.sync();
        brulion::drawing::Drawing drawn = file->drawing;
        brulion::drawing::Stroke line;
        line.id = u"nowa-łódź"_s;
        line.points = {{10, 10, 0.5}, {90, 30, 0.5}};
        drawn.strokes.append(line);
        brulion::drawing::Drawing erased = file->drawing;
        erased.strokes.removeLast();
        QVERIFY(p.a.write(path, QString::fromUtf8(brulion::drawing::writeDrawingFile(*file, drawn, 1761650000000))));
        QVERIFY(p.b.write(path, QString::fromUtf8(brulion::drawing::writeDrawingFile(*file, erased, 1761650000000))));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.merged = 1}));
        QVERIFY(conflictCopies(p.b).isEmpty());
        const QString merged = p.b.read(path);
        QCOMPARE(drawingIds(merged), (QStringList{kept, u"nowa-łódź"_s}));
        // A valid "```json" block in the plugin's file; the back of the card byte for byte.
        const QByteArray bytes = merged.toUtf8();
        const qsizetype from = bytes.indexOf("```json\n") + 8;
        const qsizetype to = bytes.indexOf("\n```", from);
        QVERIFY(from > 8 && to > from);
        QJsonParseError error;
        QVERIFY(QJsonDocument::fromJson(bytes.sliced(from, to - from), &error).isObject());
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(bytes.startsWith(original.first(original.indexOf("## Drawing"))));
        p.a.sync();
        QCOMPARE(p.a.read(path), merged);
    }

    void anExcalidrawDrawingUploadedAgainUnchangedIsNoConflict_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        const QString plugin = u"---\nexcalidraw-plugin: parsed\n---\nNotatka z tyłu: ąćęłńóśźż\n# Excalidraw Data\n"
                               u"## Text Elements\n%%\n## Drawing\n```json\n{\n\t\"type\": \"excalidraw\",\n"
                               u"\t\"elements\": [%1]\n}\n```\n%%"_s;
        const QString element = u"{\"id\": \"e1\", \"type\": \"rectangle\", \"x\": 10, \"y\": 10, \"version\": 2}"_s;
        // (A `.excalidraw` file syncs as a binary file: never merged, no base kept.)
        QTest::newRow("plugin file") << u"Excalidraw/Szkic ćwiczeń.excalidraw.md"_s << plugin.arg(QString())
                                     << plugin.arg(element);
        QTest::newRow("CRLF plugin file")
                << u"Szkic źdźbła.excalidraw.md"_s << QString(plugin).replace(u"\n"_s, u"\r\n"_s).arg(QString())
                << QString(plugin).replace(u"\n"_s, u"\r\n"_s).arg(element);
    }

    void anExcalidrawDrawingUploadedAgainUnchangedIsNoConflict()
    {
        // The server's copy of a drawing got a new ETag with the same bytes (another client
        // uploaded it again) while this device changed the drawing: no change there, so this
        // device's version goes up — no conflict copy.
        QFETCH(QString, path);
        QFETCH(QString, before);
        QFETCH(QString, after);
        Pair p({{path, before.toUtf8()}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.b.write(path, after));
        p.server.touchEtag(kFolder + u'/' + path);
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (Counts{.up = 1}));
        QVERIFY(rb.conflictCopies.isEmpty());
        QCOMPARE(p.server.text(kFolder + u'/' + path), std::optional<QString>(after));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(path), after);
        QVERIFY(conflictCopies(p.a).isEmpty());
    }

    void anEditWinsOverADeletionInBothOrders()
    {
        Pair p({{u"x.md"_s, "x\n"}, {u"y.md"_s, "y\n"}});
        p.a.sync();
        p.b.sync();
        // x: deleted on A, edited on B. y: edited on A, deleted on B.
        QVERIFY(p.a.remove(u"x.md"_s));
        QVERIFY(p.b.write(u"x.md"_s, u"x poprawione\n"_s));
        QVERIFY(p.a.write(u"y.md"_s, u"y poprawione\n"_s));
        QVERIFY(p.b.remove(u"y.md"_s));
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 1, .delR = 1}));
        // B: x edited locally but deleted remotely -> restored on the server; y deleted locally
        // but edited remotely -> restored locally.
        QCOMPARE(counts(p.b.sync()), (Counts{.up = 1, .down = 1}));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        const QMap<QString, QString> expected{{u"x.md"_s, u"x poprawione\n"_s}, {u"y.md"_s, u"y poprawione\n"_s}};
        QCOMPARE(p.a.files(), expected);
        QCOMPARE(p.b.files(), expected);
        QCOMPARE(p.server.files(kFolder), expected);
    }

    void binaryFilesSyncAndConflictsKeepBoth()
    {
        Pair p({{u"assets/kot.png"_s, QByteArray("\x01\x01\x01")}});
        p.a.sync();
        p.b.sync();
        QCOMPARE(p.b.readBytes(u"assets/kot.png"_s), QByteArray("\x01\x01\x01"));
        p.clock.advance(1000);
        QVERIFY(p.a.writeBytes(u"assets/kot.png"_s, QByteArray("\x02\x02\x02\x02")));
        QVERIFY(p.b.writeBytes(u"assets/kot.png"_s, QByteArray("\x03\x03")));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.conflicts, QList<VaultPath>{u"assets/kot.png"_s});
        const QString copy = u"assets/kot.conflict-telefon-20261014-100001.png"_s;
        QCOMPARE(rb.conflictCopies, QList<VaultPath>{copy});
        QCOMPARE(p.b.readBytes(u"assets/kot.png"_s), QByteArray("\x03\x03"));
        QCOMPARE(p.b.readBytes(copy), QByteArray("\x02\x02\x02\x02"));
        p.a.sync();
        QCOMPARE(p.a.readBytes(u"assets/kot.png"_s), QByteArray("\x03\x03"));
        QCOMPARE(p.a.readBytes(copy), QByteArray("\x02\x02\x02\x02"));
    }

    void binaryFileRewrittenInPlaceIsNotOverwritten()
    {
        // Review regression (perf plan N3): a watching vault watches folders, and a folder watch
        // does not report a file rewritten in place. An unchanged binary file is judged by the
        // vault's listing (size, mtime): without a rescan the download replaced the local edit.
        Pair p({{u"assets/kot.png"_s, QByteArray("\x01\x01\x01")}});
        p.a.sync();
        p.b.sync();
        p.b.vault->startWatching();
        p.clock.advance(1000);
        QVERIFY(p.a.writeBytes(u"assets/kot.png"_s, QByteArray("\x02\x02\x02\x02")));
        p.a.sync();
        // Another program on the phone edits the picture in place (no new file, no rename).
        QVERIFY(writeFileBehind(p.b.root, u"assets/kot.png"_s, QByteArray("\x03\x03")));
        QCoreApplication::processEvents();
        QCOMPARE(p.b.vault->stat(u"assets/kot.png"_s)->size, qint64(3)); // not seen yet
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.conflicts, QList<VaultPath>{u"assets/kot.png"_s});
        QCOMPARE(rb.conflictCopies.size(), 1);
        QCOMPARE(p.b.readBytes(u"assets/kot.png"_s), QByteArray("\x03\x03"));
        QCOMPARE(p.b.readBytes(rb.conflictCopies.first()), QByteArray("\x02\x02\x02\x02"));
        p.a.sync();
        QCOMPARE(p.a.readBytes(u"assets/kot.png"_s), QByteArray("\x03\x03"));
    }

    void firstSyncWithContentOnBothSidesKeepsEverything()
    {
        Pair p({{u"wspólny.md"_s, "taki sam\n"},
                {u"tylko-a.md"_s, "a\n"},
                {u"dziennik.md"_s, "# Dziennik\n- rano: bieganie\n"},
                {u"obraz.bin"_s, QByteArray("\x01")}},
               {{u"wspólny.md"_s, "taki sam\n"},
                {u"tylko-b.md"_s, "b\n"},
                {u"dziennik.md"_s, u8(u"# Dziennik\n- wieczorem: książka\n"_s)},
                {u"obraz.bin"_s, QByteArray("\x02")}});
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 4}));
        // tylko-a: download; tylko-b: upload; dziennik: union merge; obraz: conflict; wspólny: identical.
        QCOMPARE(counts(p.b.sync()), (Counts{.up = 1, .down = 1, .merged = 1, .conflicts = 1}));
        QCOMPARE(p.b.read(u"dziennik.md"_s), u"# Dziennik\n- wieczorem: książka\n- rano: bieganie\n"_s);
        QCOMPARE(p.b.readBytes(u"obraz.bin"_s), QByteArray("\x02"));
        QCOMPARE(p.b.readBytes(u"obraz.conflict-telefon-"_s + kStamp + u".bin"_s), QByteArray("\x01"));
        p.a.sync();
        QCOMPARE(p.a.files(), p.b.files());
        QCOMPARE(p.server.files(kFolder), p.b.files());
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void identicalFilesOnBothSidesAreOnlyRecorded()
    {
        Pair p({{u"a.md"_s, "same\n"}}, {{u"a.md"_s, "same\n"}});
        p.a.sync();
        const qsizetype puts = p.server.requests("PUT").size();
        QCOMPARE(counts(p.b.sync()), Counts{});
        QCOMPARE(p.server.requests("PUT").size(), puts);
        QCOMPARE(p.b.engine->stateOf(u"a.md"_s)->remoteEtag, p.server.etag(u"Notatki/a.md"_s));
    }

    void conflictCopiesAreSyncedLikeNormalFiles()
    {
        Pair p({{u"n.sync-conflict-20261014-101010-ABCDEFG.md"_s, "kopia\n"},
                {u"m.conflict-pc-20261013-120000.md"_s, "moja kopia\n"}});
        p.a.sync();
        p.b.sync();
        QCOMPARE(p.b.files(), (QMap<QString, QString>{{u"m.conflict-pc-20261013-120000.md"_s, u"moja kopia\n"_s},
                                                      {u"n.sync-conflict-20261014-101010-ABCDEFG.md"_s, u"kopia\n"_s}}));
        QCOMPARE(findConflicts(p.b.vault->files()).size(), 2);
    }

    void vaultConfigChangesMergeKeyByKey()
    {
        const QByteArray base = "{\n  \"journalsFolder\": \"journals\",\n  \"weekStartsOn\": 1,\n  \"defaultReminderTime\": \"09:00\"\n}\n";
        Pair p({{u".brulion/config.json"_s, base}});
        p.a.sync();
        p.b.sync();
        // Adjacent lines changed on both sides: a line conflict, but different keys.
        QVERIFY(p.a.write(u".brulion/config.json"_s,
                          u"{\n  \"journalsFolder\": \"Dziennik\",\n  \"weekStartsOn\": 1,\n  \"defaultReminderTime\": \"09:00\"\n}\n"_s));
        QVERIFY(p.b.write(u".brulion/config.json"_s,
                          u"{\n  \"journalsFolder\": \"journals\",\n  \"weekStartsOn\": 0,\n  \"defaultReminderTime\": \"09:00\"\n}\n"_s));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QVERIFY(rb.conflicts.isEmpty());
        QCOMPARE(rb.merged, 1);
        p.a.sync();
        for (Device *d : {&p.a, &p.b}) {
            QCOMPARE(d->vault->config().journalsFolder, u"Dziennik"_s);
            QCOMPARE(d->vault->config().weekStartsOn, Qt::Sunday);
        }
    }

    void firstSyncOfTwoDifferentConfigsGivesValidJson()
    {
        const auto cfg = [](const QJsonObject &over) {
            QJsonObject o{{u"version"_s, 1}, {u"journalsFolder"_s, u"journals"_s}, {u"weekStartsOn"_s, 1}};
            for (auto it = over.begin(); it != over.end(); ++it)
                o.insert(it.key(), it.value());
            return QJsonDocument(o).toJson(QJsonDocument::Indented);
        };
        Pair p({{u".brulion/config.json"_s, cfg({{u"weekStartsOn"_s, 1}})}},
               {{u".brulion/config.json"_s,
                 cfg({{u"journalsFolder"_s, u"Dziennik"_s}, {u"weekStartsOn"_s, 0}, {u"extra"_s, u"ż"_s}})}});
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QVERIFY(rb.conflicts.isEmpty());
        const QString text = *p.server.text(u"Notatki/.brulion/config.json"_s);
        QCOMPARE(jsonOf(text), (QJsonObject{{u"version"_s, 1}, {u"journalsFolder"_s, u"Dziennik"_s}, {u"weekStartsOn"_s, 0},
                                            {u"extra"_s, u"ż"_s}}));
        QCOMPARE(p.b.read(u".brulion/config.json"_s), text);
        p.a.sync();
        QCOMPARE(p.a.vault->config().journalsFolder, u"Dziennik"_s);
    }

    void keepsCrlfLineEndingsOfTheLocalFileWhenMerging()
    {
        Pair p({{u"w.md"_s, "a\r\nb\r\nc\r\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(u"w.md"_s, u"A\nb\nc\n"_s));
        QVERIFY(p.b.write(u"w.md"_s, u"a\r\nb\r\nc\r\nd\r\n"_s));
        p.a.sync();
        QCOMPARE(counts(p.b.sync()), (Counts{.merged = 1}));
        QCOMPARE(p.b.read(u"w.md"_s), u"A\r\nb\r\nc\r\nd\r\n"_s);
    }

    void transfersRunInParallelAtMostFourAtATime()
    {
        QMap<QString, QByteArray> files;
        for (int i = 0; i < 12; ++i)
            files.insert(u"p%1.md"_s.arg(i), QByteArray::number(i));
        FakeWebDavServer server;
        server.setLatency("PUT", 5);
        Device a(server, u"pc"_s, files);
        QCOMPARE(a.sync().uploaded, 12);
        QVERIFY(server.peakInFlight("PUT") > 1);
        QVERIFY(server.peakInFlight("PUT") <= 4);
    }

    void concurrencyIsConfigurable()
    {
        QMap<QString, QByteArray> files;
        for (int i = 0; i < 6; ++i)
            files.insert(u"p%1.md"_s.arg(i), QByteArray::number(i));
        FakeWebDavServer server;
        server.setLatency("PUT", 5);
        DeviceOptions options;
        options.concurrency = 1;
        Device a(server, u"pc"_s, files, options);
        QCOMPARE(a.sync().uploaded, 6);
        QCOMPARE(server.peakInFlight("PUT"), 1);
    }

    void aFailingConflictCopyUploadIsRetriedNextTime()
    {
        Pair p({{u"k.md"_s, "baza\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(u"k.md"_s, u"wersja A\n"_s));
        QVERIFY(p.b.write(u"k.md"_s, u"wersja B\n"_s));
        p.a.sync();
        p.server.hooks.append([](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PUT" && path.contains(u".conflict-"_s))
                return FakeWebDavServer::status(500);
            return std::nullopt;
        });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"will be uploaded on the next sync"_s));
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.conflicts, QList<VaultPath>{u"k.md"_s});
        QVERIFY(rb.errors.isEmpty());
        p.server.hooks.clear();
        // The copy is uploaded by the next sync.
        QCOMPARE(counts(p.b.sync()), (Counts{.up = 1}));
        bool onServer = false;
        for (const QString &path : p.server.files(kFolder).keys())
            onServer = onServer || path.contains(u".conflict-telefon-"_s);
        QVERIFY(onServer);
    }

    void doesNotPileUpConflictCopiesWhileTheUploadKeepsFailing()
    {
        Pair p({{u"k.md"_s, "baza\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.write(u"k.md"_s, u"wersja A\n"_s));
        QVERIFY(p.b.write(u"k.md"_s, u"wersja B\n"_s));
        p.a.sync();
        bool full = true;
        p.server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (full && request.method == "PUT" && path.endsWith(u"/k.md"_s))
                return FakeWebDavServer::status(507);
            return std::nullopt;
        });
        for (int i = 0; i < 3; ++i) {
            p.clock.advance(60'000);
            const SyncReport r = p.b.sync();
            QCOMPARE(r.errors.size(), 1);
            QCOMPARE(r.errors.first().path, u"k.md"_s);
        }
        QCOMPARE(conflictCopies(p.b).size(), 1);
        QCOMPARE(p.b.files().value(conflictCopies(p.b).first()), u"wersja A\n"_s);
        full = false;
        p.clock.advance(60'000);
        const SyncReport r = p.b.sync();
        QCOMPARE(r.conflicts, QList<VaultPath>{u"k.md"_s});
        QVERIFY(r.conflictCopies.isEmpty());
        QCOMPARE(conflictCopies(p.b).size(), 1);
        QCOMPARE(p.server.text(u"Notatki/k.md"_s), std::optional<QString>(u"wersja B\n"_s));
        p.a.sync();
        QCOMPARE(p.a.files(), p.b.files());
    }

    void serverContentIsByteIdenticalToTheVaultText()
    {
        Pair p({{u"pl.md"_s, u8(u"Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ\n"_s)}});
        p.a.sync();
        QCOMPARE(p.server.bytes(u"Notatki/pl.md"_s), std::optional<QByteArray>(u8(u"Zażółć gęślą jaźń — ĄĆĘŁŃÓŚŹŻ\n"_s)));
    }

    void textFilesThatAreNotUtf8TravelByteForByte()
    {
        TestClock clock;
        FakeWebDavServer server(serverOptions(clock));
        DeviceOptions options;
        options.clock = &clock;
        Device a(server, u"laptop"_s, {{u"dane.csv"_s, kCp1250}, {u"notatka.md"_s, "ok\n"}}, options);
        QCOMPARE(counts(a.sync()), (Counts{.up = 2}));
        QCOMPARE(server.bytes(u"Notatki/dane.csv"_s), std::optional<QByteArray>(kCp1250));
        Device b(server, u"telefon"_s, {}, options);
        QCOMPARE(counts(b.sync()), (Counts{.down = 2}));
        QCOMPARE(b.readBytes(u"dane.csv"_s), kCp1250);
        // Nothing left to do on either side.
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(counts(b.sync()), Counts{});
    }

    void aNonUtf8TextFileChangedOnBothSidesBecomesAConflictCopy()
    {
        Pair p({{u"dane.txt"_s, kCp1250}});
        p.a.sync();
        p.b.sync();
        p.clock.advance(1000);
        const QByteArray fromA = QByteArray(kCp1250 + "\xb3\x0a");
        QVERIFY(p.a.writeBytes(u"dane.txt"_s, fromA));
        QVERIFY(p.b.writeBytes(u"dane.txt"_s, QByteArray(kCp1250 + "\xea\x0a")));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.conflicts, QList<VaultPath>{u"dane.txt"_s});
        QCOMPARE(p.b.readBytes(rb.conflictCopies.value(0)), fromA);
    }

    void deletingOnBothDevicesLeavesNoTrace()
    {
        Pair p({{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}, {u"c.md"_s, "c\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.remove(u"a.md"_s));
        QVERIFY(p.b.remove(u"a.md"_s));
        QCOMPARE(counts(p.a.sync()), (Counts{.delR = 1}));
        QCOMPARE(counts(p.b.sync()), Counts{});
        QVERIFY(!p.b.engine->stateOf(u"a.md"_s));
        QCOMPARE(p.server.files(kFolder).keys(), (QStringList{u"b.md"_s, u"c.md"_s}));
    }

    void specialCharactersInNamesRoundTrip()
    {
        // "?" means a query in a URL; Windows allows no "?" in file names, so "!" there.
#ifdef Q_OS_WIN
        const QString percent = u"100% gotowe #1 !.md"_s;
#else
        const QString percent = u"100% gotowe #1 ?.md"_s;
#endif
        const QMap<QString, QByteArray> files = {
                {percent, "procenty\n"},
                {u"Notatki (stare)/[szkic] ąę & żółw.md"_s, u8(u"nawiasy\n"_s)},
                {u"a+b=c; d,e'f.md"_s, "znaki\n"},
                {u"Łódź/Źródła/Ćwiczenia/ŚWIT.md"_s, u8(u"wielkie litery\n"_s)},
                // Surrogate pairs (UTF-16) in folder and file names and in the content.
                {u"📓 Dziennik/😀 radość 👍🏽.md"_s, u8(u"emoji 🎉\n"_s)},
        };
        Pair p(files);
        QCOMPARE(counts(p.a.sync()), (Counts{.up = 5}));
        QCOMPARE(counts(p.b.sync()), (Counts{.down = 5}));
        QCOMPARE(p.b.files(), p.a.files());
        QCOMPARE(p.server.files(kFolder), p.a.files());
        QVERIFY(p.b.write(percent, u"procenty\nzmiana\n"_s));
        QCOMPARE(counts(p.b.sync()), (Counts{.up = 1}));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(percent), u"procenty\nzmiana\n"_s);
    }

    void notesInAFolderTheServerSpellsInNfdSyncBothWays()
    {
        // A folder created by a macOS client (NFD name on the server): a new note there is
        // uploaded into it and reaches the other device; nothing ends up in an NFC twin.
        Pair p;
        const QString nfdDir = u"Źródła"_s.normalized(QString::NormalizationForm_D);
        p.server.putText(u"Notatki/"_s + nfdDir + u"/stary.md"_s, u"stary\n"_s);
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.read(u"Źródła/stary.md"_s), u"stary\n"_s);
        QVERIFY(p.a.write(u"Źródła/nowy.md"_s, u"nowy ąę\n"_s));
        QVERIFY(p.a.write(u"Źródła/stary.md"_s, u"stary zmieniony\n"_s));
        const SyncReport r = p.a.sync();
        QVERIFY2(r.errors.isEmpty(), qPrintable(r.errors.value(0).message));
        QCOMPARE(counts(r), (Counts{.up = 2}));
        QCOMPARE(p.server.files(kFolder),
                 (QMap<QString, QString>{{nfdDir + u"/nowy.md"_s, u"nowy ąę\n"_s},
                                         {nfdDir + u"/stary.md"_s, u"stary zmieniony\n"_s}}));
        QCOMPARE(counts(p.b.sync()), (Counts{.down = 2}));
        QCOMPARE(p.b.read(u"Źródła/nowy.md"_s), u"nowy ąę\n"_s);
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void mergedTextReachesTheServerAsWrittenLocally()
    {
        // A note with a byte order mark and CRLF (written by Windows Notepad): after a merge the
        // server holds exactly the bytes of the merged local file, like after a plain upload.
        const QByteArray bom = "\xef\xbb\xbf";
        Pair p({{u"win.md"_s, bom + "a\r\nb\r\nc\r\n"}});
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.writeBytes(u"win.md"_s, bom + "A\r\nb\r\nc\r\n"));
        QVERIFY(p.b.writeBytes(u"win.md"_s, bom + "a\r\nb\r\nC\r\n"));
        p.a.sync();
        const SyncReport rb = p.b.sync();
        QCOMPARE(rb.merged, 1);
        const QByteArray merged = bom + "A\r\nb\r\nC\r\n";
        QCOMPARE(p.b.readBytes(u"win.md"_s), merged);
        QCOMPARE(p.server.bytes(u"Notatki/win.md"_s), std::optional<QByteArray>(merged));
        QCOMPARE(counts(p.a.sync()), (Counts{.down = 1}));
        QCOMPARE(p.a.readBytes(u"win.md"_s), merged);
        QCOMPARE(counts(p.b.sync()), Counts{});
    }

    void renamesTravelAsRenames_data()
    {
        QTest::addColumn<bool>("supportsMove");
        QTest::addColumn<bool>("fileIds");
        QTest::newRow("Nextcloud: MOVE, fileid") << true << true;
        QTest::newRow("MOVE, ETags only") << true << false;
        QTest::newRow("no MOVE: upload and delete") << false << true;
    }

    void renamesTravelAsRenames()
    {
        // A page renamed on one device is moved on the server (one MOVE) and renamed on the other
        // (its version history, favourites and folds follow it there); a server without MOVE gets
        // an upload and a deletion, and the other device a download and the trash.
        QFETCH(bool, supportsMove);
        QFETCH(bool, fileIds);
        FakeWebDavServer::Options options;
        options.supportsMove = supportsMove;
        options.fileIds = fileIds;
        Pair p({{u"Stara nazwa.md"_s, u8(u"treść ąę\n"_s)}, {u"inna.md"_s, "x\n"}}, {}, options);
        p.a.sync();
        p.b.sync();
        QVERIFY(p.a.vault->rename(u"Stara nazwa.md"_s, u"Projekty/Nowa nazwa.md"_s));
        const SyncReport r = p.a.sync();
        QCOMPARE(counts(r), (supportsMove ? Counts{} : Counts{.up = 1, .delR = 1}));
        QCOMPARE(r.filesMoved, supportsMove ? 1 : 0);
        p.b.changes.clear();
        const SyncReport rb = p.b.sync();
        QCOMPARE(counts(rb), (supportsMove ? Counts{} : Counts{.down = 1, .delL = 1}));
        QCOMPARE(rb.filesMovedHere, supportsMove ? 1 : 0);
        QCOMPARE(p.b.trash().size(), supportsMove ? 0 : 1);
        const bool renamed = std::any_of(p.b.changes.cbegin(), p.b.changes.cend(), [](const VaultChange &c) {
            return c.type == ChangeType::Renamed && c.source == ChangeSource::Sync
                   && c.oldPath == u"Stara nazwa.md"_s && c.path == u"Projekty/Nowa nazwa.md"_s;
        });
        QCOMPARE(renamed, supportsMove);
        QCOMPARE(p.b.files(), (QMap<QString, QString>{{u"Projekty/Nowa nazwa.md"_s, u"treść ąę\n"_s}, {u"inna.md"_s, u"x\n"_s}}));
        QCOMPARE(counts(p.a.sync()), Counts{});
        QCOMPARE(counts(p.b.sync()), Counts{});
    }
};

QTEST_GUILESS_MAIN(TestSyncDevices)
#include "tst_sync_devices.moc"
