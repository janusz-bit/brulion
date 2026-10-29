// Tests of the Obsidian Excalidraw plugin's files (drawing/excalidrawmd.h), LZ-String
// (drawing/lzstring.h: lz-string's own vectors in tests/data/drawing/lzstring, and ones from the
// fixture generator's independent port), the one API of drawing files (drawing/drawingfile.h) and
// malformed input. The fixtures obsidian_*.excalidraw.md are written by
// tests/data/drawing/make-fixtures.py as the plugin writes its files.
#include "drawing/drawing.h"
#include "drawing/drawingfile.h"
#include "drawing/edit.h"
#include "drawing/excalidraw.h"
#include "drawing/excalidrawmd.h"
#include "drawing/lzstring.h"
#include "drawing/svgwrite.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

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

/// lz-string's test files: UTF-16 code units, big-endian; an odd last byte is the high byte of a
/// last unit.
QString unitsFromBin(const QByteArray &data)
{
    QString out;
    for (qsizetype i = 0; i + 1 < data.size(); i += 2)
        out.append(QChar(char16_t(uchar(data[i]) << 8 | uchar(data[i + 1]))));
    if (data.size() % 2)
        out.append(QChar(char16_t(uchar(data.back()) << 8)));
    return out;
}

QString lzVector(const QString &name, const char *kind)
{
    return unitsFromBin(fixture(u"lzstring/"_s + name + u'.' + QLatin1String(kind) + u".bin"_s));
}

const QByteArray PluginFrontmatter =
        "---\n\nexcalidraw-plugin: parsed\ntags: [excalidraw]\n\n---\n==⚠  Switch to EXCALIDRAW VIEW in the MORE "
        "OPTIONS menu of this document. ⚠==\n\n";

Stroke newStroke(const QString &id)
{
    Stroke s;
    s.id = id;
    for (int i = 0; i < 12; ++i)
        s.points.append({100 + i * 6.0, 80 + 10 * std::sin(i / 2.0), 0.5});
    normalizeStroke(s);
    s.seed = 5;
    s.versionNonce = 9;
    s.updated = 1761650000000;
    return s;
}

/// The block's content of a written plugin file.
QByteArray blockOf(const QByteArray &file)
{
    const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(file);
    if (!read)
        return {};
    return read->layout.bytes.sliced(read->layout.sceneFrom, read->layout.sceneTo - read->layout.sceneFrom);
}

QStringList strokeIds(const Drawing &d)
{
    QStringList out;
    for (const Stroke &s : d.strokes)
        out.append(s.id);
    return out;
}

/// A text element as the plugin writes it: `shown` its text, `raw` its Markdown.
QJsonObject pluginText(const QString &id, double x, double y, const QString &shown, const QString &raw,
                       const QString &containerId = {})
{
    return QJsonObject{
            {u"id"_s, id},
            {u"type"_s, u"text"_s},
            {u"x"_s, x},
            {u"y"_s, y},
            {u"width"_s, 260},
            {u"height"_s, 25},
            {u"angle"_s, 0},
            {u"strokeColor"_s, u"#1e1e1e"_s},
            {u"backgroundColor"_s, u"transparent"_s},
            {u"fillStyle"_s, u"solid"_s},
            {u"strokeWidth"_s, 1},
            {u"strokeStyle"_s, u"solid"_s},
            {u"roughness"_s, 1},
            {u"opacity"_s, 100},
            {u"groupIds"_s, QJsonArray()},
            {u"frameId"_s, QJsonValue::Null},
            {u"roundness"_s, QJsonValue::Null},
            {u"seed"_s, 7},
            {u"version"_s, 3},
            {u"versionNonce"_s, 11},
            {u"isDeleted"_s, false},
            {u"boundElements"_s, QJsonValue::Null},
            {u"updated"_s, 1761600000000.0},
            {u"link"_s, QJsonValue::Null},
            {u"locked"_s, false},
            {u"text"_s, shown},
            {u"rawText"_s, raw},
            {u"fontSize"_s, 20},
            {u"fontFamily"_s, 5},
            {u"textAlign"_s, containerId.isEmpty() ? u"left"_s : u"center"_s},
            {u"verticalAlign"_s, containerId.isEmpty() ? u"top"_s : u"middle"_s},
            {u"containerId"_s, containerId.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(containerId)},
            {u"originalText"_s, shown},
            {u"autoResize"_s, true},
            {u"lineHeight"_s, 1.25}};
}

const QString LinkedRaw = u"Zobacz [[Plan ogrodu]] i [[Dom|dom]]"_s;
const QString LabelRaw = u"![[Notatka z łąki#^blok1]]"_s;

/// A plugin file (the json form) whose texts hold Obsidian's Markdown: a text with links
/// (Tx12Ab34) and a label transcluding a block (Lb34Cd56) in a rectangle (Rr5Kd9mq).
QByteArray linkedPluginFile()
{
    QJsonObject rect = pluginText(u"Rr5Kd9mq"_s, 300, 300, {}, {});
    for (const QString &key : {u"text"_s, u"rawText"_s, u"originalText"_s, u"fontSize"_s, u"fontFamily"_s,
                               u"textAlign"_s, u"verticalAlign"_s, u"containerId"_s, u"autoResize"_s, u"lineHeight"_s})
        rect.remove(key);
    rect.insert(u"type"_s, u"rectangle"_s);
    rect.insert(u"height"_s, 80);
    rect.insert(u"version"_s, 2);
    rect.insert(u"boundElements"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s}, {u"id"_s, u"Lb34Cd56"_s}}});
    const QJsonObject scene{
            {u"type"_s, u"excalidraw"_s},
            {u"version"_s, 2},
            {u"source"_s, u"https://github.com/zsviczian/obsidian-excalidraw-plugin/releases/tag/2.16.0"_s},
            {u"elements"_s,
             QJsonArray{pluginText(u"Tx12Ab34"_s, 10, 10, u"Zobacz Plan ogrodu i dom"_s, LinkedRaw), rect,
                        pluginText(u"Lb34Cd56"_s, 305, 327, u"Żółta kartka"_s, LabelRaw, u"Rr5Kd9mq"_s)}},
            {u"appState"_s, QJsonObject{{u"viewBackgroundColor"_s, u"#ffffff"_s}}},
            {u"files"_s, QJsonObject()}};
    return PluginFrontmatter + "# Excalidraw Data\n\n## Text Elements\n" + LinkedRaw.toUtf8() + " ^Tx12Ab34\n\n"
           + LabelRaw.toUtf8() + " ^Lb34Cd56\n\n%%\n## Drawing\n```json\n" + stringifyJson(scene, "\t") + "\n```\n%%";
}

/// The element `id` of a JSON array (empty: none).
QJsonObject elementById(const QJsonArray &elements, const QString &id)
{
    for (const QJsonValue &v : elements) {
        if (v.toObject().value(u"id"_s).toString() == id)
            return v.toObject();
    }
    return {};
}

} // namespace

class TestDrawingExmd : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ---- LZ-String ----------------------------------------------------------------------------

    void lzKnownVectors_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("hello_world") << u"hello_world"_s;
        QTest::newRow("repeated") << u"repeated"_s;
        QTest::newRow("all_ascii") << u"all_ascii"_s;
        QTest::newRow("tattoo") << u"tattoo"_s;
    }

    void lzKnownVectors()
    {
        QFETCH(QString, name);
        const QString data = lzVector(name, "data");
        const QString base64 = lzVector(name, "base64");
        QVERIFY(!data.isEmpty());
        QCOMPARE(lzstring::compressToBase64(data), base64);
        const std::optional<QString> back = lzstring::decompressFromBase64(base64);
        QVERIFY(back);
        QCOMPARE(*back, data);
    }

    void lzKnownValues()
    {
        // As lz-string's README and test suite name them.
        QCOMPARE(lzstring::compressToBase64(lzVector(u"hello_world"_s, "data")), u"qYShsbQE9o9u6DZOgAJkA==="_s);
        QCOMPARE(lzstring::compressToBase64(lzVector(u"repeated"_s, "data")),
                 u"oaGNfGV09hGRgxTktW9H2GNjHu/4MKOJOMBMTCyq6m2u+h+oA==="_s);
    }

    void lzVectors_data()
    {
        // Computed by make-fixtures.py's port of lz-string (which reproduces lz-string's own
        // vectors).
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("base64");
        QTest::newRow("empty") << QString() << u"Q==="_s;
        QTest::newRow("a") << u"a"_s << u"IZA="_s;
        QTest::newRow("aaaaaaaaaa") << u"aaaaaaaaaa"_s << u"IY1o"_s;
        QTest::newRow("hello") << u"hello"_s << u"BYUwNmD2Q==="_s;
        QTest::newRow("Hello, world!") << u"Hello, world!"_s << u"BIUwNmD2A0AEDukBOYAmBCIA"_s;
        QTest::newRow("abab") << u"abababababababababab"_s << u"IYI17SIo"_s;
        QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s << u"FoQ0PoAM+EKAjggABAc0JiAhtQADaFBAOArEgvQEBFAIA==="_s;
        QTest::newRow("diacritics") << u"ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"_s
                                    << u"qCAnBASYgIQoCCKADPjagIL0BA+gAAkCCAgwQEBiAhBQEMKAAy4FqAgnoCDegEA="_s;
        QTest::newRow("emoji") << u"😀 Brulion 👍"_s << u"rwbgA9gEBCBOCuA2BLA9gOzMQsjtA==="_s;
        QTest::newRow("lone surrogate") << QString(QChar(char16_t(0xD83D))) << u"rwbQ"_s;
        QTest::newRow("blank scene")
                << u"{\"type\":\"excalidraw\",\"version\":2,\"elements\":[],\"appState\":{\"viewBackgroundColor\":\"#ffffff\"},\"files\":{}}"_s
                << u"N4IgLgngDgpiBcIYA8DGBDANgSwCYCd0B3EAGhADcZ8BnbAewDsEAmcmTGAWxkbBoQBtALrl0UKAGUw6MHHigK2GEQBC6VAGsA5vnoBXRrgDC9TPXwIQAYgBm9hyAC+5W9k4CFTp0A=="_s;
        QTest::newRow("other type") << u"{\"type\":\"other\"}"_s << u"N4IgLgngDgpiBcID2YAWMBOIC+Q="_s;
    }

    void lzVectors()
    {
        QFETCH(QString, input);
        QFETCH(QString, base64);
        QCOMPARE(lzstring::compressToBase64(input), base64);
        const std::optional<QString> back = lzstring::decompressFromBase64(base64);
        QVERIFY(back);
        QCOMPARE(*back, input);
    }

    void lzMalformed_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<qsizetype>("maxUnits");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QString>("expected");
        const QString hello = lzVector(u"hello_world"_s, "data");
        const qsizetype max = MaxSceneBytes;
        QTest::newRow("empty") << QString() << max << false << QString();
        QTest::newRow("whitespace only") << u" \n\r\n\t"_s << max << false << QString();
        QTest::newRow("A") << u"A"_s << max << false << QString();
        QTest::newRow("AAAA") << u"AAAA"_s << max << false << QString();
        QTest::newRow("====") << u"===="_s << max << false << QString();
        // Its first code is the end mark: the empty string, as JavaScript reads it (and "Q===").
        QTest::newRow("foobar") << u"Zm9vYmFy"_s << max << true << QString();
        QTest::newRow("no padding") << u"qYShsbQE9o9u6DZOgAJk"_s << max << true << hello;
        QTest::newRow("garbage after the end mark") << u"qYShsbQE9o9u6DZOgAJkA===x"_s << max << true << hello;
        QTest::newRow("lines") << u"qYShsbQE\n\n9o9u6DZO\n\ngAJkA===\n"_s << max << true << hello;
        QTest::newRow("CRLF lines") << u"qYShsbQE\r\n\r\n9o9u6DZO\r\n\r\ngAJkA==="_s << max << true << hello;
        QTest::newRow("dash") << u"qYShsbQE9o9u6D-ZOgAJkA==="_s << max << false << QString();
        QTest::newRow("bang") << u"qYShsbQE9o9u6DZOgAJkA===!"_s << max << false << QString();
        QTest::newRow("polish letter") << u"qYShsbQEą9o9u6DZOgAJkA==="_s << max << false << QString();
        QTest::newRow("truncated") << u"qYShsbQE9o9u"_s << max << false << QString();
        QTest::newRow("over the limit") << lzVector(u"tattoo"_s, "base64") << qsizetype(5) << false << QString();
        QTest::newRow("at the limit") << u"qYShsbQE9o9u6DZOgAJkA==="_s << hello.size() << true << hello;
    }

    void lzMalformed()
    {
        QFETCH(QString, input);
        QFETCH(qsizetype, maxUnits);
        QFETCH(bool, valid);
        QFETCH(QString, expected);
        const std::optional<QString> out = lzstring::decompressFromBase64(input, maxUnits);
        QCOMPARE(out.has_value(), valid);
        if (valid)
            QCOMPARE(*out, expected);
    }

    void lzRandomRoundTrip()
    {
        QRandomGenerator rng(7);
        for (int round = 0; round < 500; ++round) {
            const int length = int(rng.bounded(5001));
            // Small alphabets compress into long phrases; the whole range has every surrogate,
            // U+0000 and U+FFFF.
            const quint32 alphabet = round % 3 == 0 ? 65536 : round % 3 == 1 ? 4 : 300;
            QString s;
            s.reserve(length);
            for (int i = 0; i < length; ++i)
                s.append(QChar(
                        char16_t(round % 3 == 2 && i % 7 == 0 ? 0xD800 + rng.bounded(0x800) : rng.bounded(alphabet))));
            const QString b64 = lzstring::compressToBase64(s);
            QCOMPARE(b64.size() % 4, 0);
            const std::optional<QString> back = lzstring::decompressFromBase64(b64);
            QVERIFY2(back.has_value(), qPrintable(u"round %1"_s.arg(round)));
            QVERIFY2(*back == s, qPrintable(u"round %1"_s.arg(round)));
        }
        // A scene of a megabyte (handwriting: points and pressures).
        QByteArray scene = "{\"type\":\"excalidraw\",\"version\":2,\"elements\":[";
        for (int e = 0; scene.size() < 1024 * 1024; ++e) {
            if (e > 0)
                scene += ',';
            scene += "{\"id\":\"stroke-" + QByteArray::number(e) + "\",\"type\":\"freedraw\",\"points\":[";
            for (int p = 0; p < 40; ++p)
                scene += (p ? ",[" : "[") + QByteArray::number(p * 1.25 + e % 17) + ','
                         + QByteArray::number(std::round(std::sin(p + e) * 1000) / 100) + ']';
            scene += "]}";
        }
        scene += "],\"appState\":{},\"files\":{}}";
        const QString text = QString::fromUtf8(scene);
        QElapsedTimer timer;
        timer.start();
        const QString b64 = lzstring::compressToBase64(text);
        const qint64 compressMs = timer.restart();
        const std::optional<QString> back = lzstring::decompressFromBase64(b64);
        const qint64 decompressMs = timer.elapsed();
        QVERIFY(back);
        QVERIFY(*back == text);
        qInfo("1 MB scene: %lld chars of base64, compress %lld ms, decompress %lld ms", qlonglong(b64.size()),
              qlonglong(compressMs), qlonglong(decompressMs));
    }

    // ---- The plugin's files -------------------------------------------------------------------

    void readPluginFiles_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("encoding");
        QTest::addColumn<int>("strokes");
        QTest::addColumn<int>("foreign");
        QTest::addColumn<QStringList>("entries");
        QTest::addColumn<int>("tombstones");
        QTest::addColumn<bool>("editable");
        QTest::addColumn<QByteArray>("eol");
        const int compressed = int(ExcalidrawMarkdown::Encoding::CompressedJson);
        const int json = int(ExcalidrawMarkdown::Encoding::Json);
        const int legacy = int(ExcalidrawMarkdown::Encoding::Legacy);
        QTest::newRow("freedraw") << u"obsidian_freedraw.excalidraw.md"_s << compressed << 2 << 0 << QStringList() << 1
                                  << true << QByteArray("\n");
        QTest::newRow("mixed") << u"obsidian_mixed.excalidraw.md"_s << compressed << 5 << 0
                               << QStringList{u"Zt8xQw1a"_s, u"Dw2Lin3s"_s} << 0 << true << QByteArray("\n");
        QTest::newRow("frame") << u"obsidian_frame.excalidraw.md"_s << compressed << 2 << 1
                               << QStringList{u"Zt8xQw1a"_s} << 0 << false << QByteArray("\n");
        QTest::newRow("json") << u"obsidian_json.excalidraw.md"_s << json << 2 << 0 << QStringList() << 0 << true
                              << QByteArray("\n");
        QTest::newRow("crlf") << u"obsidian_crlf.excalidraw.md"_s << compressed << 3 << 0 << QStringList{u"Zt8xQw1a"_s}
                              << 0 << true << QByteArray("\r\n");
        QTest::newRow("commented") << u"obsidian_commented.excalidraw.md"_s << compressed << 2 << 0
                                   << QStringList{u"Zt8xQw1a"_s} << 0 << true << QByteArray("\n");
        QTest::newRow("legacy") << u"obsidian_legacy.excalidraw.md"_s << legacy << 1 << 0 << QStringList() << 0 << true
                                << QByteArray("\n");
    }

    void readPluginFiles()
    {
        QFETCH(QString, file);
        QFETCH(int, encoding);
        QFETCH(int, strokes);
        QFETCH(int, foreign);
        QFETCH(QStringList, entries);
        QFETCH(int, tombstones);
        QFETCH(bool, editable);
        QFETCH(QByteArray, eol);
        const int legacy = int(ExcalidrawMarkdown::Encoding::Legacy);
        const QByteArray bytes = fixture(file);
        QVERIFY(looksLikeExcalidrawMarkdown(bytes.left(HeadBytes)));
        ReadError error = ReadError::Corrupt;
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes, &error);
        QVERIFY(read);
        QCOMPARE(error, ReadError::None);
        QCOMPARE(int(read->layout.encoding), encoding);
        QCOMPARE(read->drawing.strokes.size(), strokes);
        QCOMPARE(read->drawing.foreign.size(), foreign);
        QStringList ids;
        for (const ExcalidrawMarkdown::TextEntry &e : read->layout.textEntries)
            ids.append(e.id);
        QCOMPARE(ids, entries);
        QCOMPARE(read->layout.tombstones.size(), tombstones);
        QCOMPARE(::brulion::drawing::editable(read->drawing), editable);
        QCOMPARE(read->layout.eol, eol);
        QCOMPARE(read->layout.bytes, bytes);
        QVERIFY(read->layout.sceneFrom > 0 && read->layout.sceneFrom <= read->layout.sceneTo);
        QVERIFY(read->layout.sceneTo <= bytes.size());
        // Through the one API too.
        const std::optional<DrawingFile> f = readDrawingFile(bytes);
        QVERIFY(f);
        QCOMPARE(f->format, FileFormat::ExcalidrawMarkdown);
        QCOMPARE(f->writable, encoding != legacy);
        QVERIFY(f->markdown);
        QCOMPARE(strokeIds(f->drawing), strokeIds(read->drawing));
        QCOMPARE(readDrawing(bytes)->strokes.size(), strokes);
        if (encoding == legacy)
            QVERIFY(writeDrawingFile(*f, f->drawing, 1).isEmpty());
        if (encoding == int(ExcalidrawMarkdown::Encoding::CompressedJson)) {
            // The block as make-fixtures.py's port of lz-string wrote it: decompressed and compressed
            // again by Brulion's, the same characters.
            QByteArray block = bytes.sliced(read->layout.sceneFrom, read->layout.sceneTo - read->layout.sceneFrom);
            block.replace("\r", "").replace("\n", "");
            const std::optional<QString> scene = lzstring::decompressFromBase64(QString::fromLatin1(block));
            QVERIFY(scene);
            QVERIFY(scene->startsWith(u"{\n\t\"type\": \"excalidraw\",\n"_s));
            QCOMPARE(lzstring::compressToBase64(*scene), QString::fromLatin1(block));
        }
    }

    void textEntriesAndPressures()
    {
        const QByteArray bytes = fixture(u"obsidian_mixed.excalidraw.md"_s);
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        const auto rawOf = [&](int i) {
            const ExcalidrawMarkdown::TextEntry &e = read->layout.textEntries.at(i);
            return QString::fromUtf8(bytes.sliced(e.from, e.to - e.from));
        };
        QCOMPARE(rawOf(0), u"Zażółć gęślą jaźń"_s);
        QCOMPARE(rawOf(1), u"Pierwsza linia\nDruga: ąćęłńóśźż"_s);
        // The entry's end: after " ^id" and the empty line.
        const ExcalidrawMarkdown::TextEntry &last = read->layout.textEntries.at(1);
        QVERIFY(bytes.sliced(last.end).startsWith("## Element Links\n"));
        // The elements: the freehand line with its pressures, the text with its text.
        const Stroke &pen = read->drawing.strokes.at(0);
        QCOMPARE(pen.type, ElementType::Freedraw);
        QVERIFY(!pen.simulatePressure);
        QCOMPARE(pen.points.size(), 24);
        QCOMPARE(pen.points.at(1).pressure, 0.23);
        const Stroke &text = read->drawing.strokes.at(2);
        QCOMPARE(text.type, ElementType::Text);
        QCOMPARE(text.id, u"Zt8xQw1a"_s);
        QCOMPARE(text.text, u"Zażółć gęślą jaźń"_s);
        QCOMPARE(read->drawing.strokes.at(4).type, ElementType::Image);
        QCOMPARE(read->drawing.strokes.at(4).fileId, u"a1b2c3d4e5f60718"_s);
        QCOMPARE(read->drawing.paper, u"#ffffff"_s);
        QCOMPARE(read->drawing.scene.value(u"source"_s).toString(),
                 u"https://github.com/zsviczian/obsidian-excalidraw-plugin/releases/tag/2.2.5"_s);
    }

    void writeKeepsTheRest_data()
    {
        QTest::addColumn<QString>("file");
        QTest::newRow("freedraw") << u"obsidian_freedraw.excalidraw.md"_s;
        QTest::newRow("json") << u"obsidian_json.excalidraw.md"_s;
        QTest::newRow("crlf") << u"obsidian_crlf.excalidraw.md"_s;
        QTest::newRow("commented") << u"obsidian_commented.excalidraw.md"_s;
        QTest::newRow("mixed") << u"obsidian_mixed.excalidraw.md"_s;
    }

    void writeKeepsTheRest()
    {
        QFETCH(QString, file);
        const QByteArray bytes = fixture(file);
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        const ExcalidrawMarkdown &layout = read->layout;
        Drawing d = read->drawing;
        d.strokes.append(newStroke(u"ż-nowa"_s));
        const qint64 now = 1761700000000;
        const QByteArray out = writeExcalidrawMarkdown(layout, d, now);
        // Every byte around the block as it was.
        QCOMPARE(out.left(layout.sceneFrom), bytes.left(layout.sceneFrom));
        const qsizetype tail = bytes.size() - layout.sceneTo;
        QCOMPARE(out.right(tail), bytes.right(tail));
        // The block holds the drawing: the new stroke and every element as read.
        const std::optional<ExcalidrawMarkdownRead> again = readExcalidrawMarkdown(out);
        QVERIFY(again);
        QCOMPARE(again->layout.encoding, layout.encoding);
        QCOMPARE(strokeIds(again->drawing), strokeIds(d));
        for (qsizetype i = 0; i < read->drawing.strokes.size(); ++i)
            QCOMPARE(again->drawing.strokes.at(i).element, read->drawing.strokes.at(i).element);
        QCOMPARE(again->drawing.strokes.last().points, d.strokes.last().points);
        QCOMPARE(again->layout.tombstones, layout.tombstones);
        QCOMPARE(again->drawing.scene, read->drawing.scene);
        QCOMPARE(again->drawing.appState, read->drawing.appState);
        const QByteArray block = blockOf(out);
        if (layout.encoding == ExcalidrawMarkdown::Encoding::CompressedJson) {
            // Lines of 256 characters with an empty line between, the file's line breaks.
            const QList<QByteArray> lines = block.split('\n');
            QVERIFY(lines.size() >= 3);
            QVERIFY(lines.last().isEmpty()); // (the line break before the closing fence)
            for (qsizetype i = 0; i + 1 < lines.size(); ++i) {
                QByteArray line = lines.at(i);
                if (layout.eol == "\r\n") {
                    QVERIFY(line.endsWith('\r'));
                    line.chop(1);
                }
                if (i % 2 == 1) {
                    QVERIFY(line.isEmpty());
                } else {
                    QVERIFY(!line.isEmpty());
                    QVERIFY(line.size() <= 256);
                    if (i + 2 < lines.size())
                        QCOMPARE(line.size(), 256);
                }
            }
        } else {
            QVERIFY(block.contains("\n\t\"type\": \"excalidraw\",\n"));
            QVERIFY(block.startsWith("{\n\t\""));
            QVERIFY(block.endsWith("\n}\n"));
        }
        // Written again without changes: the same bytes.
        QCOMPARE(writeExcalidrawMarkdown(again->layout, again->drawing, now + 5000), out);
        // Through the one API: the same file.
        const std::optional<DrawingFile> f = readDrawingFile(bytes);
        QVERIFY(f);
        QCOMPARE(writeDrawingFile(*f, d, now), out);
    }

    void writeTombstones()
    {
        const QByteArray bytes = fixture(u"obsidian_freedraw.excalidraw.md"_s);
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        Drawing d = read->drawing;
        const Stroke erased = d.strokes.takeFirst();
        const qint64 now = 1761700000000;
        const QByteArray out = writeExcalidrawMarkdown(read->layout, d, now);
        const std::optional<ExcalidrawMarkdownRead> again = readExcalidrawMarkdown(out);
        QVERIFY(again);
        QCOMPARE(strokeIds(again->drawing), strokeIds(d));
        QCOMPARE(again->layout.tombstones.size(), 2);
        // The file's own tombstone is kept as it was.
        QCOMPARE(elementById(again->layout.tombstones, u"deleted_stroke_0000001"_s),
                 elementById(read->layout.tombstones, u"deleted_stroke_0000001"_s));
        const QJsonObject buried = elementById(again->layout.tombstones, erased.id);
        QVERIFY(buried.value(u"isDeleted"_s).toBool());
        QCOMPARE(buried.value(u"version"_s).toInteger(), erased.version + 1);
        QCOMPARE(buried.value(u"updated"_s).toInteger(), now);
        QVERIFY(buried.value(u"versionNonce"_s).toInteger() != erased.versionNonce);
        QCOMPARE(buried.value(u"points"_s), erased.element.value(u"points"_s));
        // Written again: the tombstone stays as it is (no second version).
        const QByteArray twice = writeExcalidrawMarkdown(again->layout, again->drawing, now + 1000);
        QCOMPARE(twice, out);
        // The element back (undo, another device): no tombstone for it any more.
        Drawing back = again->drawing;
        back.strokes.prepend(erased);
        const std::optional<ExcalidrawMarkdownRead> revived =
                readExcalidrawMarkdown(writeExcalidrawMarkdown(again->layout, back, now + 2000));
        QVERIFY(revived);
        QCOMPARE(revived->layout.tombstones.size(), 1);
        QCOMPARE(strokeIds(revived->drawing), strokeIds(back));
    }

    void textEntriesFollowEdits()
    {
        const QByteArray bytes = fixture(u"obsidian_mixed.excalidraw.md"_s);
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        const qint64 now = 1761700000000;
        const QByteArray second = "Pierwsza linia\nDruga: ąćęłńóśźż ^Dw2Lin3s\n\n";
        const QByteArray links = "## Element Links\nRr5Kd9mq: [[Plan ogrodu]]\n\n## Embedded Files\n"
                                 "a1b2c3d4e5f60718: [[Pasted Image 20261028143205.png]]\n\n%%\n## Drawing\n";
        QVERIFY(bytes.contains(second));
        QVERIFY(bytes.contains(links));
        const auto indexOf = [](const Drawing &d, const QString &id) {
            for (qsizetype i = 0; i < d.strokes.size(); ++i) {
                if (d.strokes.at(i).id == id)
                    return i;
            }
            return qsizetype(-1);
        };

        // Edited: the entry and rawText follow.
        Drawing edited = read->drawing;
        Stroke &text = edited.strokes[indexOf(edited, u"Zt8xQw1a"_s)];
        text.text = text.originalText = u"Żółw"_s;
        ++text.version;
        const QByteArray out = writeExcalidrawMarkdown(read->layout, edited, now);
        QVERIFY(out.contains(QByteArray("## Text Elements\nŻółw ^Zt8xQw1a\n\n" + second + links)));
        QVERIFY(!out.contains("Zażółć gęślą jaźń ^Zt8xQw1a"));
        QVERIFY(out.startsWith(PluginFrontmatter.left(60)));
        QVERIFY(out.contains("Notatka z tyłu: Zażółć gęślą jaźń\n\n# Excalidraw Data\n\n## Text Elements\n"));
        std::optional<ExcalidrawMarkdownRead> again = readExcalidrawMarkdown(out);
        QVERIFY(again);
        QCOMPARE(elementById(again->layout.elements, u"Zt8xQw1a"_s).value(u"rawText"_s).toString(), u"Żółw"_s);
        QCOMPARE(elementById(again->layout.elements, u"Dw2Lin3s"_s).value(u"rawText"_s).toString(),
                 u"Pierwsza linia\nDruga: ąćęłńóśźż"_s);
        QCOMPARE(again->layout.textEntries.size(), 2);
        // Written again unchanged: the same bytes.
        QCOMPARE(writeExcalidrawMarkdown(again->layout, again->drawing, now + 1), out);

        // A multi-line text edited.
        Drawing lines = read->drawing;
        Stroke &two = lines.strokes[indexOf(lines, u"Dw2Lin3s"_s)];
        two.text = two.originalText = u"Jedna\nDwie\nTrzy"_s;
        ++two.version;
        const QByteArray linesOut = writeExcalidrawMarkdown(read->layout, lines, now);
        QVERIFY(linesOut.contains(
                QByteArray("Zażółć gęślą jaźń ^Zt8xQw1a\n\nJedna\nDwie\nTrzy ^Dw2Lin3s\n\n" + links)));

        // Removed: its entry goes, a tombstone is written.
        Drawing removed = read->drawing;
        removed.strokes.removeAt(indexOf(removed, u"Zt8xQw1a"_s));
        const QByteArray removedOut = writeExcalidrawMarkdown(read->layout, removed, now);
        QVERIFY(!removedOut.contains("^Zt8xQw1a"));
        QVERIFY(removedOut.contains(QByteArray("## Text Elements\n" + second + links)));
        again = readExcalidrawMarkdown(removedOut);
        QVERIFY(again);
        QVERIFY(elementById(again->layout.tombstones, u"Zt8xQw1a"_s).value(u"isDeleted"_s).toBool());
        QCOMPARE(again->layout.textEntries.size(), 1);

        // New: no entry (the plugin adds it with its own id), its rawText set.
        Drawing added = read->drawing;
        Stroke fresh;
        fresh.type = ElementType::Text;
        fresh.id = u"NowyTekst-123456789012"_s;
        fresh.box = QRectF(10, 10, 80, 25);
        fresh.text = fresh.originalText = u"Nowy ąę"_s;
        added.strokes.append(fresh);
        const QByteArray addedOut = writeExcalidrawMarkdown(read->layout, added, now);
        QVERIFY(!addedOut.contains("^NowyTekst"));
        QVERIFY(addedOut.left(read->layout.sceneFrom) == bytes.left(read->layout.sceneFrom));
        again = readExcalidrawMarkdown(addedOut);
        QVERIFY(again);
        QCOMPARE(elementById(again->layout.elements, fresh.id).value(u"rawText"_s).toString(), u"Nowy ąę"_s);

        // A rectangle moved: its element link (an entry of "Element Links") stays.
        Drawing moved = read->drawing;
        Stroke &rect = moved.strokes[indexOf(moved, u"Rr5Kd9mq"_s)];
        rect.box.translate(30, 0);
        ++rect.version;
        const QByteArray movedOut = writeExcalidrawMarkdown(read->layout, moved, now);
        QCOMPARE(movedOut.left(read->layout.sceneFrom), bytes.left(read->layout.sceneFrom));
        again = readExcalidrawMarkdown(movedOut);
        QVERIFY(again);
        QCOMPARE(elementById(again->layout.elements, u"Rr5Kd9mq"_s).value(u"link"_s).toString(), u"[[Plan ogrodu]]"_s);
    }

    void crlfEntriesFollowEdits()
    {
        const QByteArray bytes = fixture(u"obsidian_crlf.excalidraw.md"_s);
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        Drawing d = read->drawing;
        Stroke &text = d.strokes.last();
        QCOMPARE(text.id, u"Zt8xQw1a"_s);
        text.text = text.originalText = u"Żółw"_s;
        ++text.version;
        const QByteArray out = writeExcalidrawMarkdown(read->layout, d, 1761700000000);
        QVERIFY(out.contains("## Text Elements\r\nŻółw ^Zt8xQw1a\r\n\r\n%%\r\n## Drawing\r\n```compressed-json\r\n"));
        // Every line break still CRLF, also in a text of two lines.
        for (qsizetype i = out.indexOf('\n'); i >= 0; i = out.indexOf('\n', i + 1))
            QVERIFY2(i > 0 && out.at(i - 1) == '\r', qPrintable(u"at %1"_s.arg(i)));
        text.text = text.originalText = u"Dwie\nlinie"_s;
        ++text.version;
        const QByteArray twoLines = writeExcalidrawMarkdown(read->layout, d, 1761700000000);
        QVERIFY(twoLines.contains("## Text Elements\r\nDwie\r\nlinie ^Zt8xQw1a\r\n\r\n%%"));
        const std::optional<ExcalidrawMarkdownRead> back = readExcalidrawMarkdown(twoLines);
        QVERIFY(back);
        QCOMPARE(back->layout.textEntries.size(), 1);
        QCOMPARE(back->drawing.strokes.last().text, u"Dwie\nlinie"_s);
    }

    void notAPluginFile_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("error");
        const int noScene = int(ReadError::NoScene);
        const int corrupt = int(ReadError::Corrupt);
        const QByteArray block = "%%\n## Drawing\n```compressed-json\n";
        QTest::newRow("a note quoting a drawing")
                << QByteArray("---\ntitle: Notatka\n---\n## Drawing\n```json\n{\"type\":\"excalidraw\"}\n```\n")
                << noScene;
        QTest::newRow("no frontmatter") << QByteArray(
                "excalidraw-plugin: parsed\n## Drawing\n```json\n{\"type\":\"excalidraw\"}\n```\n")
                                        << noScene;
        QTest::newRow("empty") << QByteArray() << noScene;
        QTest::newRow("the key but no block")
                << QByteArray(PluginFrontmatter + "# Excalidraw Data\n\n## Text Elements\n%%\n## Drawing\n%%")
                << corrupt;
        QTest::newRow("no drawing heading") << QByteArray(PluginFrontmatter + "Zwykły tekst ąę\n") << corrupt;
        QTest::newRow("broken base64") << QByteArray(PluginFrontmatter + block + "N4Ig!gngDg\n```\n%%") << corrupt;
        QTest::newRow("non-ASCII in base64") << QByteArray(PluginFrontmatter + block + "N4Igążć\n```\n%%") << corrupt;
        QTest::newRow("no end mark") << QByteArray(PluginFrontmatter + block + "AAAA\n```\n%%") << corrupt;
        QTest::newRow("not JSON") << QByteArray(PluginFrontmatter + block + "BYUwNmD2Q===\n```\n%%") << corrupt;
        QTest::newRow("not a scene") << QByteArray(PluginFrontmatter + block + "N4IgLgngDgpiBcID2YAWMBOIC+Q=\n```\n%%")
                                     << corrupt;
        QTest::newRow("no closing fence") << QByteArray(
                PluginFrontmatter + "%%\n## Drawing\n```json\n{\"type\":\"excalidraw\",\"elements\":[]}\n")
                                          << corrupt;
        QTest::newRow("json not a scene")
                << QByteArray(PluginFrontmatter + "%%\n## Drawing\n```json\n{\"type\":\"other\"}\n```\n%%") << corrupt;
        QTest::newRow("json elements not an array") << QByteArray(
                PluginFrontmatter + "%%\n## Drawing\n```json\n{\"type\":\"excalidraw\",\"elements\":7}\n```\n%%")
                                                    << corrupt;
        QTest::newRow("json without a brace")
                << QByteArray(PluginFrontmatter + "%%\n## Drawing\n```json\nnic\n```\n%%") << corrupt;
    }

    void notAPluginFile()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, error);
        ReadError why = ReadError::None;
        QVERIFY(!readExcalidrawMarkdown(bytes, &why));
        QCOMPARE(int(why), error);
        why = ReadError::None;
        QVERIFY(!readDrawingFile(bytes, &why));
        QCOMPARE(int(why), error);
    }

    void smallestPluginFiles()
    {
        // The plugin's own empty drawing: no text, no elements; and the json form with CRLF.
        const QByteArray compressed =
                PluginFrontmatter
                + "# Excalidraw Data\n\n## Text Elements\n%%\n## Drawing\n```compressed-json\n"
                  "N4IgLgngDgpiBcIYA8DGBDANgSwCYCd0B3EAGhADcZ8BnbAewDsEAmcmTGAWxkbBoQBtALrl0UKAGUw6M"
                  "HHjAAvuQBm2TgIWLFQA\n```\n%%";
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(compressed);
        QVERIFY(read);
        QVERIFY(read->drawing.strokes.isEmpty());
        QVERIFY(::brulion::drawing::editable(read->drawing));
        // A first stroke: the block rewritten, the rest as it was.
        Drawing d = read->drawing;
        d.strokes.append(newStroke(u"pierwsza"_s));
        const QByteArray out = writeExcalidrawMarkdown(read->layout, d, 1);
        QVERIFY(out.startsWith(QByteArray(
                PluginFrontmatter + "# Excalidraw Data\n\n## Text Elements\n%%\n## Drawing\n```compressed-json\n")));
        QVERIFY(out.endsWith("\n```\n%%"));
        QCOMPARE(readDrawing(out)->strokes.size(), 1);

        const QByteArray json = QByteArray(PluginFrontmatter
                                           + "%%\n## Drawing\n```json\n{\"type\":\"excalidraw\","
                                             "\"elements\":[]}  trailing garbage\n```\n%%")
                                        .replace("\n", "\r\n");
        const std::optional<ExcalidrawMarkdownRead> crlf = readExcalidrawMarkdown(json);
        QVERIFY(crlf);
        QCOMPARE(crlf->layout.encoding, ExcalidrawMarkdown::Encoding::Json);
        QCOMPARE(crlf->layout.eol, QByteArray("\r\n"));
        const QByteArray crlfOut = writeExcalidrawMarkdown(crlf->layout, d, 1);
        QVERIFY(crlfOut.endsWith("\r\n}\r\n```\r\n%%"));
        QVERIFY(!crlfOut.contains("trailing garbage"));
        QCOMPARE(readDrawing(crlfOut)->strokes.size(), 1);
        // The tab-indented JSON in the file's line breaks too.
        QVERIFY(crlfOut.contains("\r\n```json\r\n{\r\n\t\""));
        for (qsizetype i = crlfOut.indexOf('\n'); i >= 0; i = crlfOut.indexOf('\n', i + 1))
            QVERIFY(crlfOut.at(i - 1) == '\r');
    }

    void headingsInText()
    {
        // A text element "# Drawing" before the real one: the first heading followed by the block.
        const QByteArray bytes = PluginFrontmatter
                                 + "# Excalidraw Data\n\n## Text Elements\n# Drawing ^Ab12Cd34\n\n%%\n## Drawing\n"
                                   "```compressed-json\nN4IgLgngDgpiBcIYA8DGBDANgSwCYCd0B3EAGhADcZ8BnbAewDsEAmcmTGAW"
                                   "xkbBoQBtALrl0UKAGUw6MHHjAAvuQBm2TgIWLFQA\n```\n%%";
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        QCOMPARE(read->layout.textEntries.size(), 1);
        QCOMPARE(read->layout.textEntries.first().id, u"Ab12Cd34"_s);
        QVERIFY(bytes.sliced(read->layout.sceneFrom).startsWith("N4Ig"));
    }

    void headChecks_data()
    {
        QTest::addColumn<QByteArray>("head");
        QTest::addColumn<bool>("markdown");
        QTest::addColumn<bool>("json");
        QTest::newRow("plugin frontmatter") << PluginFrontmatter << true << false;
        QTest::newRow("raw mode") << QByteArray("---\nexcalidraw-plugin: raw\n---\n") << true << false;
        QTest::newRow("BOM") << QByteArray("\xEF\xBB\xBF" + PluginFrontmatter) << true << false;
        QTest::newRow("CRLF") << QByteArray(PluginFrontmatter).replace("\n", "\r\n") << true << false;
        QTest::newRow("quoted key") << QByteArray("---\n\"excalidraw-plugin\": parsed\n---\n") << true << false;
        QTest::newRow("single-quoted key") << QByteArray("---\n'excalidraw-plugin' : parsed\n---\n") << true << false;
        QTest::newRow("after other keys")
                << QByteArray("---\ntitle: Szkic ćwiczeń\naliases: [a, b]\nexcalidraw-plugin: parsed\n---\n") << true
                << false;
        QTest::newRow("cut inside the frontmatter")
                << QByteArray("---\ntags: [x]\nexcalidraw-plugin: par") << true << false;
        QTest::newRow("the key in the body")
                << QByteArray("---\ntitle: x\n---\nexcalidraw-plugin: parsed\n") << false << false;
        QTest::newRow("a longer key") << QByteArray("---\nexcalidraw-plugin-x: parsed\n---\n") << false << false;
        QTest::newRow("an indented key") << QByteArray("---\nmeta:\n  excalidraw-plugin: parsed\n---\n") << false
                                         << false;
        QTest::newRow("no colon") << QByteArray("---\nexcalidraw-plugin\n---\n") << false << false;
        QTest::newRow("no frontmatter") << QByteArray("excalidraw-plugin: parsed\n") << false << false;
        QTest::newRow("four dashes") << QByteArray("----\nexcalidraw-plugin: parsed\n---\n") << false << false;
        QTest::newRow("empty") << QByteArray() << false << false;
        QTest::newRow("scene") << QByteArray("{\n  \"type\": \"excalidraw\",\n  \"version\": 2") << false << true;
        QTest::newRow("scene after BOM and spaces")
                << QByteArray("\xEF\xBB\xBF \r\n{\"type\":\"excalidraw\"}") << false << true;
        QTest::newRow("other JSON") << QByteArray("{\"type\":\"other\"}") << false << false;
        QTest::newRow("array") << QByteArray("[\"excalidraw\"]") << false << false;
        QTest::newRow("svg") << fixture(u"excalidraw_freedraw.svg"_s).left(HeadBytes) << false << false;
    }

    void headChecks()
    {
        QFETCH(QByteArray, head);
        QFETCH(bool, markdown);
        QFETCH(bool, json);
        QCOMPARE(looksLikeExcalidrawMarkdown(head), markdown);
        QCOMPARE(looksLikeExcalidrawJson(head), json);
    }

    void drawingFileDispatch_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<int>("format");
        QTest::addColumn<QString>("name");
        QTest::newRow("svg") << u"excalidraw_freedraw.svg"_s << int(FileFormat::Svg) << u"svg"_s;
        QTest::newRow("excalidraw") << u"excalidraw_freedraw.excalidraw"_s << int(FileFormat::Excalidraw)
                                    << u"excalidraw"_s;
        QTest::newRow("shapes excalidraw")
                << u"excalidraw_shapes.excalidraw"_s << int(FileFormat::Excalidraw) << u"excalidraw"_s;
        QTest::newRow("plugin") << u"obsidian_freedraw.excalidraw.md"_s << int(FileFormat::ExcalidrawMarkdown)
                                << u"excalidrawMd"_s;
    }

    void drawingFileDispatch()
    {
        QFETCH(QString, file);
        QFETCH(int, format);
        QFETCH(QString, name);
        const QByteArray bytes = fixture(file);
        const std::optional<DrawingFile> f = readDrawingFile(bytes);
        QVERIFY(f);
        QCOMPARE(int(f->format), format);
        QCOMPARE(formatName(f->format), name);
        QVERIFY(f->writable);
        QCOMPARE(f->markdown != nullptr, f->format == FileFormat::ExcalidrawMarkdown);
        QCOMPARE(strokeIds(*readDrawing(bytes)), strokeIds(f->drawing));
        // By name and start.
        QVERIFY(looksLikeDrawingFile(file, bytes.left(HeadBytes)));
        QVERIFY(looksLikeDrawingFile(file.toUpper(), bytes.left(HeadBytes)));
        QVERIFY(!looksLikeDrawingFile(QString(file + u".txt"_s), bytes.left(HeadBytes)));
        // Written in its own format, read back the same.
        const QByteArray written = writeDrawingFile(*f, f->drawing, 1761700000000);
        QVERIFY(!written.isEmpty());
        const std::optional<DrawingFile> back = readDrawingFile(written);
        QVERIFY(back);
        QCOMPARE(back->format, f->format);
        QCOMPARE(strokeIds(back->drawing), strokeIds(f->drawing));
        for (qsizetype i = 0; i < f->drawing.strokes.size(); ++i) {
            QCOMPARE(back->drawing.strokes.at(i).points, f->drawing.strokes.at(i).points);
            QCOMPARE(back->drawing.strokes.at(i).box, f->drawing.strokes.at(i).box);
        }
        if (f->format == FileFormat::Excalidraw) {
            // Excalidraw's layout: two spaces, its keys in serializeAsJSON's order, a line break at
            // the end.
            QVERIFY(written.startsWith("{\n  \"type\": \"excalidraw\",\n  \"version\": 2,\n  \"source\": \""));
            QVERIFY(written.contains("\n  \"elements\": [\n    {\n      \""));
            QVERIFY(written.indexOf("\n  \"elements\": [") < written.indexOf("\n  \"appState\": {\n    \""));
            QVERIFY(written.indexOf("\n  \"appState\": {") < written.indexOf("\n  \"files\": "));
            QVERIFY(written.endsWith("\n}\n"));
            QCOMPARE(writeDrawingFile(*back, back->drawing, 1), written);
        }
        if (f->format == FileFormat::Svg)
            QCOMPARE(written, toSvg(f->drawing));
    }

    void largeExcalidrawFileStaysADrawing()
    {
        // Written larger than HeadBytes: "type": "excalidraw" still opens it, so the file is still
        // a drawing by its start (embeds, the picture view, sync's guard).
        const QByteArray bytes = fixture(u"excalidraw_freedraw.excalidraw"_s);
        std::optional<DrawingFile> f = readDrawingFile(bytes);
        QVERIFY(f);
        Drawing d = f->drawing;
        for (int i = 0; i < 20; ++i)
            d.strokes.append(newStroke(u"kreska-ż-%1"_s.arg(i)));
        d.scene.insert(u"prevTextMode"_s, u"parsed"_s); // a key of the plugin's own: after the others
        const QByteArray written = writeDrawingFile(*f, d, 1761700000000);
        QVERIFY2(written.size() > 4 * HeadBytes, qPrintable(QString::number(written.size())));
        QVERIFY(looksLikeExcalidrawJson(written.left(HeadBytes)));
        QVERIFY(looksLikeDrawingFile(u"Szkic ćwiczeń.excalidraw"_s, written.left(HeadBytes)));
        QVERIFY(written.startsWith("{\n  \"type\": \"excalidraw\",\n  \"version\": 2,\n  \"source\": "));
        QVERIFY(written.endsWith("\n  \"files\": {},\n  \"prevTextMode\": \"parsed\"\n}\n"));
        const std::optional<DrawingFile> back = readDrawingFile(written);
        QVERIFY(back);
        QCOMPARE(back->format, FileFormat::Excalidraw);
        QCOMPARE(strokeIds(back->drawing), strokeIds(d));
        QCOMPARE(back->drawing.scene.value(u"prevTextMode"_s).toString(), u"parsed"_s);
        // The same as JSON as the scene written compact.
        QCOMPARE(QJsonDocument::fromJson(written).object(), QJsonDocument::fromJson(sceneJson(d)).object());
    }

    void textMovedKeepsItsMarkdown_data()
    {
        QTest::addColumn<QString>("edit");
        QTest::addColumn<bool>("changesText");
        QTest::newRow("text moved") << u"move text"_s << false;
        QTest::newRow("shape moved with its label") << u"move shape"_s << false;
        QTest::newRow("recoloured") << u"recolour"_s << false;
        QTest::newRow("to the front") << u"front"_s << false;
        QTest::newRow("changed and changed back") << u"same text"_s << false;
        QTest::newRow("text changed") << u"change text"_s << true;
    }

    void textMovedKeepsItsMarkdown()
    {
        // The plugin reads a text's entry (its Markdown: links, transclusions) before the scene's
        // text. A text only moved, recoloured or carried by its shape keeps its entry and rawText;
        // only a text changed in Brulion gets its new text as both.
        QFETCH(QString, edit);
        QFETCH(bool, changesText);
        const QByteArray bytes = linkedPluginFile();
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        QCOMPARE(read->layout.encoding, ExcalidrawMarkdown::Encoding::Json);
        QCOMPARE(read->layout.textEntries.size(), 2);
        QVERIFY(::brulion::drawing::editable(read->drawing));
        Drawing d = read->drawing;
        EditStamp stamp;
        stamp.now = 1761700000000;
        const QString changed = u"Nowy tekst: źdźbło"_s;
        if (edit == u"move text")
            moveElements(d, {u"Tx12Ab34"_s}, QPointF(30, 0), stamp);
        else if (edit == u"move shape")
            moveElements(d, {u"Rr5Kd9mq"_s, u"Lb34Cd56"_s}, QPointF(0, -40), stamp);
        else if (edit == u"recolour")
            recolorElements(d, {u"Tx12Ab34"_s, u"Lb34Cd56"_s}, u"#e03131"_s, stamp);
        else if (edit == u"front")
            reorderElements(d, {u"Tx12Ab34"_s}, Reorder::ToFront, stamp);
        else if (edit == u"same text") {
            setText(d, u"Tx12Ab34"_s, u"Inny tekst"_s, {}, stamp);
            setText(d, u"Tx12Ab34"_s, u"Zobacz Plan ogrodu i dom"_s, {}, stamp);
        } else
            setText(d, u"Tx12Ab34"_s, changed, {}, stamp);
        const QByteArray out = writeExcalidrawMarkdown(read->layout, d, stamp.now);
        QVERIFY(d.strokes.at(indexOfElement(d, u"Tx12Ab34")).version > 3
                || d.strokes.at(indexOfElement(d, u"Lb34Cd56")).version > 3);
        const std::optional<ExcalidrawMarkdownRead> again = readExcalidrawMarkdown(out);
        QVERIFY(again);
        const QJsonObject text = elementById(again->layout.elements, u"Tx12Ab34"_s);
        const QJsonObject label = elementById(again->layout.elements, u"Lb34Cd56"_s);
        QCOMPARE(label.value(u"rawText"_s).toString(), LabelRaw);
        QVERIFY(out.contains(QByteArray(LabelRaw.toUtf8() + " ^Lb34Cd56\n")));
        if (!changesText) {
            // Every byte before the block as it was: both entries untouched.
            QCOMPARE(out.left(read->layout.sceneFrom), bytes.left(read->layout.sceneFrom));
            QCOMPARE(text.value(u"rawText"_s).toString(), LinkedRaw);
            QCOMPARE(text.value(u"originalText"_s).toString(), u"Zobacz Plan ogrodu i dom"_s);
        } else {
            QVERIFY(out.contains(
                    QByteArray("## Text Elements\n" + changed.toUtf8() + " ^Tx12Ab34\n\n" + LabelRaw.toUtf8())));
            QVERIFY(!out.contains("[[Plan ogrodu]]"));
            QCOMPARE(text.value(u"rawText"_s).toString(), changed);
        }
        // Written again from the file just written: the same bytes.
        QCOMPARE(writeExcalidrawMarkdown(again->layout, again->drawing, stamp.now + 1), out);
    }

    void rawTextAcrossSaves()
    {
        // The editor keeps its elements (with the element each was read with) across saves; after
        // a save the file written is the layout of the next one (DrawingCanvas::markSaved). The
        // raw text written once stays in later saves.
        const QByteArray bytes = linkedPluginFile();
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        EditStamp stamp;
        stamp.now = 1761700000000;
        const auto saveTwice = [&read, &stamp](const std::function<void(Drawing &)> &edit, QByteArray *first) {
            Drawing d = read->drawing;
            edit(d);
            *first = writeExcalidrawMarkdown(read->layout, d, stamp.now);
            const std::optional<ExcalidrawMarkdownRead> saved = readExcalidrawMarkdown(*first);
            if (!saved)
                return QByteArray();
            // Another stroke, the next save: against the file written.
            d.strokes.append(newStroke(u"potem"_s));
            return writeExcalidrawMarkdown(saved->layout, d, stamp.now + 1000);
        };
        const auto rawOf = [](const QByteArray &file, const QString &id) {
            const std::optional<ExcalidrawMarkdownRead> r = readExcalidrawMarkdown(file);
            return r ? elementById(r->layout.elements, id).value(u"rawText"_s) : QJsonValue();
        };

        // A text changed in Brulion: its new text (not the one read) in the second save too.
        QByteArray first;
        QByteArray second = saveTwice(
                [&stamp](Drawing &d) { setText(d, u"Tx12Ab34"_s, u"Żółw i [[link]]?"_s, {}, stamp); }, &first);
        QCOMPARE(rawOf(first, u"Tx12Ab34"_s).toString(), u"Żółw i [[link]]?"_s);
        QCOMPARE(rawOf(second, u"Tx12Ab34"_s).toString(), u"Żółw i [[link]]?"_s);
        QVERIFY(second.contains("## Text Elements\nŻółw i [[link]]? ^Tx12Ab34\n\n"));
        QCOMPARE(rawOf(second, u"Lb34Cd56"_s).toString(), LabelRaw);

        // A text new in Brulion: its rawText in the second save too.
        second = saveTwice(
                [&stamp](Drawing &d) {
                    TextStyle style;
                    d.strokes.append(newText(QPointF(40, 200), u"Nowy ąę"_s, style, {}, stamp));
                },
                &first);
        const std::optional<ExcalidrawMarkdownRead> firstRead = readExcalidrawMarkdown(first);
        QVERIFY(firstRead);
        const QString newId = firstRead->drawing.strokes.last().id;
        QCOMPARE(rawOf(first, newId).toString(), u"Nowy ąę"_s);
        QCOMPARE(rawOf(second, newId).toString(), u"Nowy ąę"_s);
        QVERIFY(!second.contains(QByteArray(" ^" + newId.toUtf8())));

        // A text moved after the first save: its links stay.
        second = saveTwice([&stamp](Drawing &d) { moveElements(d, {u"Tx12Ab34"_s}, QPointF(5, 5), stamp); }, &first);
        QCOMPARE(rawOf(second, u"Tx12Ab34"_s).toString(), LinkedRaw);
        QVERIFY(second.contains(QByteArray("## Text Elements\n" + LinkedRaw.toUtf8() + " ^Tx12Ab34\n")));

        // The file changed meanwhile (another device wrote another text), this text moved here and
        // kept as it is here (DrawingCanvas::takeExternal, "changed here wins"): the entry gets this
        // text's own Markdown back, its links included.
        Drawing theirs = read->drawing;
        setText(theirs, u"Tx12Ab34"_s, u"Inny tekst"_s, {}, stamp);
        const std::optional<ExcalidrawMarkdownRead> other =
                readExcalidrawMarkdown(writeExcalidrawMarkdown(read->layout, theirs, stamp.now));
        QVERIFY(other);
        Drawing mine = read->drawing;
        moveElements(mine, {u"Tx12Ab34"_s}, QPointF(30, 0), stamp);
        const QByteArray merged = writeExcalidrawMarkdown(other->layout, mine, stamp.now + 2000);
        QCOMPARE(rawOf(merged, u"Tx12Ab34"_s).toString(), LinkedRaw);
        QVERIFY(merged.contains(QByteArray("## Text Elements\n" + LinkedRaw.toUtf8() + " ^Tx12Ab34\n")));
        QVERIFY(!merged.contains("Inny tekst"));
    }

    void craftedFilesReadInLinearTime_data()
    {
        // Many "%%" lines in "Text Elements", many "# Drawing" headings before a far backtick: each
        // made the reader scan to the end again (quadratic — minutes for a few MB). Read on the GUI
        // thread (embeds, the picture view, the editor): it must stay linear.
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<bool>("readable");
        const QByteArray scene = "```json\n{\"type\":\"excalidraw\",\"elements\":[]}\n```\n%%";
        QTest::newRow("%% lines") << QByteArray(PluginFrontmatter + "# Excalidraw Data\n## Text Elements\n"
                                                + QByteArray("%%\n").repeated(300'000) + "%%\n## Drawing\n" + scene)
                                  << true;
        QTest::newRow("headings before a backtick")
                << QByteArray(PluginFrontmatter + QByteArray("# Drawing\nx\n").repeated(300'000) + "`") << false;
        QTest::newRow("headings before an unclosed block")
                << QByteArray(PluginFrontmatter + QByteArray("## Drawing\nż\n").repeated(300'000)
                              + "## Drawing\n```json\n{\"type\":\"excalidraw\",\"elements\":[]}\n")
                << false;
    }

    void craftedFilesReadInLinearTime()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(bool, readable);
        QElapsedTimer timer;
        timer.start();
        ReadError why = ReadError::None;
        const std::optional<ExcalidrawMarkdownRead> read = readExcalidrawMarkdown(bytes, &why);
        const qint64 ms = timer.elapsed();
        QCOMPARE(bool(read), readable);
        QCOMPARE(int(why), int(readable ? ReadError::None : ReadError::Corrupt));
        // (Linear: a few ms; quadratic took seconds for these.)
        QVERIFY2(ms < 1500, qPrintable(u"%1 ms"_s.arg(ms)));
    }

    void previewDrawingBoxes()
    {
        const std::optional<DrawingFile> f = readDrawingFile(fixture(u"obsidian_frame.excalidraw.md"_s));
        QVERIFY(f);
        QVERIFY(!::brulion::drawing::editable(f->drawing));
        const Drawing preview = previewDrawing(f->drawing);
        QVERIFY(::brulion::drawing::editable(preview));
        QCOMPARE(preview.strokes.size(), f->drawing.strokes.size() + 1);
        const Stroke &box = preview.strokes.last();
        QCOMPARE(box.type, ElementType::Rectangle);
        QCOMPARE(box.box, QRectF(360, 180, 400, 300));
        QCOMPARE(box.strokeStyle, StrokeStyle::Dashed);
        QCOMPARE(box.color, ForeignBoxColor.toString());
        QVERIFY(bounds(preview).contains(QRectF(360, 180, 400, 300)));
        // An editable drawing as it is; foreign elements without a box left out.
        const Drawing plain = previewDrawing(readDrawing(fixture(u"obsidian_freedraw.excalidraw.md"_s)).value());
        QCOMPARE(plain.strokes.size(), 2);
        Drawing odd;
        odd.foreign.append(QJsonObject{{u"type"_s, u"frame"_s}, {u"x"_s, u"zero"_s}});
        odd.foreign.append(
                QJsonObject{{u"type"_s, u"frame"_s}, {u"x"_s, 1e12}, {u"y"_s, 0}, {u"width"_s, 1}, {u"height"_s, 1}});
        odd.foreign.append(
                QJsonObject{{u"type"_s, u"frame"_s}, {u"x"_s, 50}, {u"y"_s, 40}, {u"width"_s, -20}, {u"height"_s, 10}});
        odd.foreign.append(QJsonValue(7));
        const Drawing oddPreview = previewDrawing(odd);
        QCOMPARE(oddPreview.strokes.size(), 1);
        QCOMPARE(oddPreview.strokes.first().box, QRectF(30, 40, 20, 10));
    }

    void stringifyJson_data()
    {
        QTest::addColumn<QJsonValue>("value");
        QTest::addColumn<QByteArray>("indent");
        QTest::addColumn<QByteArray>("expected");
        QTest::newRow("empty object") << QJsonValue(QJsonObject()) << QByteArray("\t") << QByteArray("{}");
        QTest::newRow("empty array") << QJsonValue(QJsonArray()) << QByteArray("\t") << QByteArray("[]");
        QTest::newRow("nested")
                << QJsonValue(QJsonObject{{u"a"_s, QJsonArray{1, QJsonObject{{u"b"_s, QJsonArray()}}}},
                                          {u"c"_s, QJsonObject()},
                                          {u"d"_s, u"Zażółć gęślą jaźń"_s},
                                          {u"e"_s, QJsonValue::Null},
                                          {u"f"_s, true},
                                          {u"g"_s, 2.5}})
                << QByteArray("\t")
                << QByteArray("{\n\t\"a\": [\n\t\t1,\n\t\t{\n\t\t\t\"b\": []\n\t\t}\n\t],\n\t\"c\": {},\n\t\"d\": "
                              "\"Zażółć gęślą jaźń\",\n\t\"e\": null,\n\t\"f\": true,\n\t\"g\": 2.5\n}");
        QTest::newRow("escapes") << QJsonValue(QJsonObject{{u"s"_s, u"a\u0001\"\\\n{[,:]}"_s}}) << QByteArray("  ")
                                 << QByteArray("{\n  \"s\": \"a\\u0001\\\"\\\\\\n{[,:]}\"\n}");
        QTest::newRow("array of arrays") << QJsonValue(QJsonArray{QJsonArray{0, 0}, QJsonArray{1.5, -2}})
                                         << QByteArray("  ")
                                         << QByteArray("[\n  [\n    0,\n    0\n  ],\n  [\n    1.5,\n    -2\n  ]\n]");
        QTest::newRow("number") << QJsonValue(5) << QByteArray("\t") << QByteArray("5");
        QTest::newRow("string") << QJsonValue(u"ą\"b"_s) << QByteArray("\t") << QByteArray("\"ą\\\"b\"");
    }

    void stringifyJson()
    {
        QFETCH(QJsonValue, value);
        QFETCH(QByteArray, indent);
        QFETCH(QByteArray, expected);
        const QByteArray out = ::brulion::drawing::stringifyJson(value, indent);
        QCOMPARE(out, expected);
        if (value.isObject())
            QCOMPARE(QJsonDocument::fromJson(out).object(), value.toObject());
        else if (value.isArray())
            QCOMPARE(QJsonDocument::fromJson(out).array(), value.toArray());
    }

    void randomInputNeverCrashes()
    {
        QRandomGenerator rng(11);
        const QList<QByteArray> fences = {"%%\n## Drawing\n```compressed-json\n", "%%\n## Drawing\n```json\n",
                                          "# Drawing\n", "## Text Elements\n", " ^abcdefgh\n\n"};
        const QByteArray alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=\n `{}[]\":,^#%";
        int read = 0;
        for (int round = 0; round < 400; ++round) {
            QByteArray bytes = PluginFrontmatter;
            const int parts = 1 + int(rng.bounded(6));
            for (int p = 0; p < parts; ++p) {
                bytes += fences.at(rng.bounded(int(fences.size())));
                const int n = int(rng.bounded(400));
                for (int i = 0; i < n; ++i)
                    bytes += round % 2 ? char(rng.bounded(256)) : alphabet.at(rng.bounded(int(alphabet.size())));
                if (rng.bounded(2))
                    bytes += "\n```\n";
            }
            if (round % 4 < 2) {
                // A real block after the noise: the file reads, its entries are wherever the noise
                // put them.
                bytes += "\n%%\n## Drawing\n```compressed-json\nN4IgLgngDgpiBcIYA8DGBDANgSwCYCd0B3EAGhADcZ8Bnb"
                         "AewDsEAmcmTGAWxkbBoQBtALrl0UKAGUw6MHHjAAvuQBm2TgIWLFQA\n```\n%%";
            }
            const std::optional<DrawingFile> f = readDrawingFile(bytes);
            if (!f)
                continue;
            ++read;
            Drawing d = f->drawing;
            d.strokes.append(newStroke(u"losowa"_s));
            const QByteArray out = writeDrawingFile(*f, d, 1);
            if (f->writable) {
                const std::optional<DrawingFile> back = readDrawingFile(out);
                QVERIFY(back.has_value());
                QCOMPARE(back->drawing.strokes.size(), d.strokes.size());
            }
        }
        // Random base64 into LZ-String: never a crash, at most the limit.
        for (int round = 0; round < 400; ++round) {
            QString s;
            const int n = int(rng.bounded(200));
            for (int i = 0; i < n; ++i)
                s.append(QLatin1Char(alphabet.at(rng.bounded(64))));
            const std::optional<QString> out = lzstring::decompressFromBase64(s, 4096);
            if (out)
                QVERIFY(out->size() <= 4096);
        }
        qInfo("%d of 400 random files read as drawings", read);
    }
};

QTEST_GUILESS_MAIN(TestDrawingExmd)
#include "tst_drawing_exmd.moc"
