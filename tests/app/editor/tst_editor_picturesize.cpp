// The picture size helper (src/app/editor/picturesize.h): the size Qt's image reader reports —
// what the editor's Image shows once loaded — read from as little of the file as possible. SVGs
// are compared with Qt's reader on the whole document (units, percentages, viewBox, prologues);
// a large drawing whose root gives its size is read only at its start.
#include "picturesize.h"

#include "drawing/drawing.h"
#include "drawing/drawingfile.h"
#include "drawing/excalidrawmd.h"
#include "drawing/svgwrite.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using brulion::editor::canDecodePictureFormat;
using brulion::editor::drawingExportOf;
using brulion::editor::localDrawingSize;
using brulion::editor::localFileIsDrawing;
using brulion::editor::localPictureSize;
using brulion::editor::MaxFullSvgBytes;
using brulion::editor::pictureFormatName;
using brulion::editor::pictureSize;

namespace {

/// A buffer that counts the bytes read from it.
class CountingBuffer : public QBuffer {
public:
    explicit CountingBuffer(const QByteArray &data)
    {
        setData(data);
        open(QIODevice::ReadOnly);
    }
    qint64 bytesRead = 0;

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        const qint64 n = QBuffer::readData(data, maxSize);
        if (n > 0)
            bytesRead += n;
        return n;
    }
};

QSize qtSize(const QByteArray &data, const QByteArray &format)
{
    QBuffer buffer;
    buffer.setData(data);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, format);
    const QSize size = reader.size();
    return size.isValid() && !size.isEmpty() ? size : QSize();
}

QByteArray drawing(int rects)
{
    QByteArray body;
    for (int i = 0; i < rects; ++i)
        body += "<rect x=\"" + QByteArray::number(i % 400) + "\" y=\"" + QByteArray::number(i / 400)
                + "\" width=\"1\" height=\"1\" fill=\"#3a7\"/>\n";
    return body;
}

/// gzip (RFC 1952) of `data`: qCompress's deflate stream in a gzip frame.
QByteArray gzip(const QByteArray &data)
{
    const QByteArray zlib = qCompress(data, 9).mid(4);       // a 4-byte length, then a zlib stream
    const QByteArray deflate = zlib.mid(2, zlib.size() - 6); // without its header and Adler-32
    quint32 crc = 0xffffffffu;
    for (const char c : data) {
        crc ^= quint8(c);
        for (int k = 0; k < 8; ++k)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    QByteArray out("\x1f\x8b\x08\x00\x00\x00\x00\x00\x00\x03", 10);
    out += deflate;
    const auto le32 = [&out](quint32 v) {
        for (int i = 0; i < 4; ++i)
            out += char((v >> (8 * i)) & 0xff);
    };
    le32(crc);
    le32(quint32(data.size()));
    return out;
}

QByteArray png(QSize size)
{
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return buffer.data();
}

} // namespace

class TestPictureSize : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (!QImageReader::supportedImageFormats().contains("svg"))
            QSKIP("Qt's SVG image format plugin is not available");
    }

    void svgLikeQt_data()
    {
        QTest::addColumn<QByteArray>("svg");
        QTest::addColumn<QSize>("expected"); // invalid: only "as Qt says"
        QTest::addColumn<bool>("sized");     // Qt gives it a size
        const QByteArray ns = "xmlns=\"http://www.w3.org/2000/svg\"";
        const QByteArray rect = "<rect width=\"120\" height=\"30\" fill=\"#9fc3e6\"/>";
        QTest::newRow("width, height, viewBox")
                << "<svg " + ns + " width=\"400\" height=\"100\" viewBox=\"0 0 400 100\">" + rect + "</svg>"
                << QSize(400, 100) << true;
        QTest::newRow("viewBox only") << "<svg " + ns + " viewBox=\"0 0 50 20\">" + rect + "</svg>" << QSize(50, 20)
                                      << true;
        QTest::newRow("viewBox with commas, negative origin")
                << "<svg " + ns + " viewBox=\"-10,-10,640,480\">" + rect + "</svg>" << QSize(640, 480) << true;
        QTest::newRow("width only + viewBox")
                << "<svg " + ns + " width=\"300\" viewBox=\"0 0 60 30\">" + rect + "</svg>" << QSize(60, 30) << true;
        QTest::newRow("percent of the viewBox")
                << "<svg " + ns + " width=\"100%\" height=\"50%\" viewBox=\"0 0 300 200\">" + rect + "</svg>"
                << QSize(300, 100) << true;
        QTest::newRow("px units") << "<svg " + ns + " width=\"64px\" height=\"32px\">" + rect + "</svg>"
                                  << QSize(64, 32) << true;
        QTest::newRow("millimetres (Inkscape)")
                << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n<!-- Created with Inkscape -->\n<svg "
                           + ns
                           + " xmlns:sodipodi=\"http://sodipodi.sourceforge.net/DTD/sodipodi-0.dtd\" width=\"210mm\" "
                             "height=\"297mm\" viewBox=\"0 0 210 297\" version=\"1.1\"><sodipodi:namedview id=\"n\"/>"
                           + rect + "</svg>"
                << QSize() << true;
        QTest::newRow("centimetres and inches")
                << "<svg " + ns + " width=\"5cm\" height=\"2in\">" + rect + "</svg>" << QSize() << true;
        QTest::newRow("points") << "<svg " + ns + " width=\"200pt\" height=\"100pt\">" + rect + "</svg>" << QSize()
                                << true;
        QTest::newRow("Illustrator prologue with entities")
                << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!-- Generator: Adobe Illustrator -->\n<!DOCTYPE svg "
                   "PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\" "
                   "[\n<!ENTITY ns_svg \"http://www.w3.org/2000/svg\">\n]>\n<svg version=\"1.1\" xmlns=\"&ns_svg;\" "
                   "x=\"0px\" y=\"0px\" width=\"595.3px\" height=\"841.9px\" viewBox=\"0 0 595.3 841.9\" "
                   "xml:space=\"preserve\">"
                           + rect + "</svg>"
                << QSize() << true;
        QTest::newRow("prefixed root") << QByteArray("<svg:svg xmlns:svg=\"http://www.w3.org/2000/svg\" width=\"48\" "
                                                     "height=\"24\"><svg:rect width=\"5\" height=\"5\"/></svg:svg>")
                                       << QSize(48, 24) << true;
        QTest::newRow("byte order mark") << "\xef\xbb\xbf<svg " + ns + " width=\"33\" height=\"11\">" + rect + "</svg>"
                                         << QSize(33, 11) << true;
        QTest::newRow("Polish title, entities in attributes")
                << "<svg " + ns + " width=\"7&#48;\" height=\"35\"><title>Trasa — Połonina Wetlińska, źródło</title>"
                           + rect + "</svg>"
                << QSize(70, 35) << true;
        // The size comes from the drawing (no viewBox, no absolute size): the whole document.
        QTest::newRow("drawing's bounds") << "<svg " + ns + ">" + rect + "</svg>" << QSize() << true;
        QTest::newRow("percent without viewBox")
                << "<svg " + ns + " width=\"50%\" height=\"40\">" + rect + "</svg>" << QSize() << true;
        // Not a usable picture.
        QTest::newRow("not XML") << QByteArray("ceci n'est pas une image") << QSize() << false;
        QTest::newRow("another root") << QByteArray("<html><body>nie</body></html>") << QSize() << false;
        QTest::newRow("broken before the root")
                << QByteArray("<?xml version=\"1.0\"?><!-- <svg width=\"9\" height=\"9\"> --") << QSize() << false;
        QTest::newRow("empty") << QByteArray() << QSize() << false;
        QTest::newRow("zero size") << "<svg " + ns + " width=\"0\" height=\"0\"/>" << QSize() << false;
    }

    void svgLikeQt()
    {
        QFETCH(QByteArray, svg);
        QFETCH(QSize, expected);
        QFETCH(bool, sized);
        CountingBuffer buffer(svg);
        const QSize size = pictureSize(&buffer, u"rysunek.svg"_s);
        // Exactly what Qt's reader says about the whole document (also when that is "unknown").
        QCOMPARE(size, qtSize(svg, "svg"));
        QCOMPARE(size.isValid(), sized);
        if (expected.isValid())
            QCOMPARE(size, expected);
    }

    void largeSvgReadAtItsStart()
    {
        // A 2.4 MB drawing: its root has the size, the rest is never read.
        const QByteArray svg =
                "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1600\" height=\"900\" viewBox=\"0 0 1600 900\">"
                + drawing(40000) + "</svg>";
        QVERIFY(svg.size() > 2'000'000);
        CountingBuffer buffer(svg);
        QCOMPARE(pictureSize(&buffer, u"Diagram.SVG"_s), QSize(1600, 900));
        QVERIFY2(buffer.bytesRead <= 64 * 1024, qPrintable(QString::number(buffer.bytesRead)));
    }

    void largeSvgSizedByItsContentIsUnknown()
    {
        // Sized by its drawing and too large to parse on the GUI thread: unknown (the picture takes
        // its size when it has loaded) — read only up to the root and not parsed whole.
        const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\">" + drawing(40000) + "</svg>";
        QVERIFY(svg.size() > MaxFullSvgBytes);
        CountingBuffer buffer(svg);
        QVERIFY(!pictureSize(&buffer, u"duży.svg"_s).isValid());
        QVERIFY2(buffer.bytesRead <= 64 * 1024, qPrintable(QString::number(buffer.bytesRead)));
        // A small one is parsed whole, like Qt does.
        const QByteArray small = "<svg xmlns=\"http://www.w3.org/2000/svg\">" + drawing(100) + "</svg>";
        QVERIFY(small.size() < MaxFullSvgBytes);
        CountingBuffer smallBuffer(small);
        QCOMPARE(pictureSize(&smallBuffer, u"mały.svg"_s), qtSize(small, "svg"));
    }

    void svgz()
    {
        // Qt reads an .svgz whole (gzip, or plain): small ones only, else unknown.
        const QByteArray svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"20\"/>";
        const QByteArray z = gzip(svg);
        CountingBuffer buffer(z);
        const QSize size = pictureSize(&buffer, u"skompresowany.svgz"_s);
        QCOMPARE(size, qtSize(z, "svgz"));
        if (QImageReader::supportedImageFormats().contains("svgz"))
            QCOMPARE(size, QSize(80, 20));
        const QByteArray big = gzip("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"80\" height=\"20\">"
                                    + drawing(40000) + "</svg>");
        if (big.size() > MaxFullSvgBytes) {
            CountingBuffer bigBuffer(big);
            QVERIFY(!pictureSize(&bigBuffer, u"duży.svgz"_s).isValid());
            QCOMPARE(bigBuffer.bytesRead, 0);
        }
    }

    void raster_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QSize>("expected");
        QTest::newRow("png") << png(QSize(37, 21)) << u"zdjęcie.png"_s << QSize(37, 21);
        QTest::newRow("png named .jpg") << png(QSize(8, 300)) << u"źle nazwany.jpg"_s << QSize(8, 300);
        // The header is enough (the editor shows a broken picture's error at that size).
        QTest::newRow("truncated png") << png(QSize(640, 480)).left(64) << u"ucięty.png"_s << QSize(640, 480);
        QTest::newRow("not a picture") << QByteArray("tekst") << u"plik.png"_s << QSize();
        QTest::newRow("empty") << QByteArray() << u"pusty.png"_s << QSize();
    }

    void raster()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, name);
        QFETCH(QSize, expected);
        CountingBuffer buffer(data);
        QCOMPARE(pictureSize(&buffer, name), expected);
    }

    void localFileCachedByChange()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.filePath(u"łąka.svg"_s);
        const auto write = [&](const QByteArray &data) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(f.write(data), data.size());
        };
        write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"100\"/>");
        QCOMPARE(localPictureSize(file), QSize(400, 100));
        // Replaced by another shape (another length too): read again.
        write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"400\"><!-- wysoka --></svg>");
        QCOMPARE(localPictureSize(file), QSize(100, 400));
        QVERIFY(!localPictureSize(dir.filePath(u"brak.png"_s)).isValid());
        QVERIFY(!localPictureSize(dir.path()).isValid());
    }

    void drawingFiles_data()
    {
        // Drawings by their name and start (the editor's embeds, Workspace::isDrawing): an SVG with
        // a scene, a .excalidraw scene, an Obsidian Excalidraw plugin file (any .md name).
        QTest::addColumn<QString>("fixture"); // tests/data/drawing ("" inline)
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("drawing");
        QTest::newRow("svg drawing") << u"excalidraw_freedraw.svg"_s << QByteArray() << u"Rysunek.svg"_s << true;
        QTest::newRow(".excalidraw") << u"excalidraw_freedraw.excalidraw"_s << QByteArray() << u"Szkic.excalidraw"_s
                                     << true;
        QTest::newRow("plugin file") << u"obsidian_freedraw.excalidraw.md"_s << QByteArray()
                                     << u"Szkic ćwiczeń.excalidraw.md"_s << true;
        QTest::newRow("plugin file, any name") << u"obsidian_mixed.excalidraw.md"_s << QByteArray() << u"Plan.md"_s
                                               << true;
        QTest::newRow("plugin file named .txt") << u"obsidian_mixed.excalidraw.md"_s << QByteArray() << u"Plan.txt"_s
                                                << false;
        QTest::newRow("a note") << QString() << QByteArray("---\ntytul: Szkic\n---\n# Szkic ąę\n") << u"Szkic.md"_s
                                << false;
        QTest::newRow("a note naming the key in its text")
                << QString() << QByteArray("# Notatka\n\nexcalidraw-plugin: parsed\n") << u"Notatka.md"_s << false;
        QTest::newRow("JSON, no scene") << QString() << QByteArray("{\"type\": \"lista\"}") << u"dane.excalidraw"_s
                                        << false;
        QTest::newRow("a picture") << QString() << QByteArray("<svg xmlns=\"http://www.w3.org/2000/svg\"/>")
                                   << u"ikona.svg"_s << false;
    }
    void drawingFiles()
    {
        QFETCH(QString, fixture);
        QFETCH(QByteArray, bytes);
        QFETCH(QString, name);
        QFETCH(bool, drawing);
        if (!fixture.isEmpty()) {
            QFile f(QStringLiteral(BRULION_TEST_DATA "/drawing/") + fixture);
            QVERIFY(f.open(QIODevice::ReadOnly));
            bytes = f.readAll();
        }
        QTemporaryDir dir;
        const QString file = dir.filePath(name);
        QFile f(file);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(bytes);
        f.close();
        QCOMPARE(localFileIsDrawing(file), drawing);
        // The preview's size of a .excalidraw or plugin drawing: its scene's picture rect.
        const QSize size = localDrawingSize(file);
        if (drawing && !name.endsWith(u".svg"_s)) {
            const QRectF rect = brulion::drawing::pictureRect(*brulion::drawing::readDrawing(bytes));
            QCOMPARE(size, rect.size().toSize());
        } else if (!drawing && !name.endsWith(u".txt"_s)) {
            QVERIFY(!size.isValid() || size.isEmpty());
        }
    }

    void drawingSizeFollowsTheFile()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(u"Szkic ćwiczeń.excalidraw.md"_s);
        QFile in(QStringLiteral(BRULION_TEST_DATA "/drawing/obsidian_freedraw.excalidraw.md"));
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray bytes = in.readAll();
        const auto write = [&](const QByteArray &data) {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(f.write(data), data.size());
        };
        write(bytes);
        const std::optional<brulion::drawing::ExcalidrawMarkdownRead> read = brulion::drawing::readExcalidrawMarkdown(bytes);
        QVERIFY(read);
        const QSize before = localDrawingSize(file);
        QCOMPARE(before, brulion::drawing::pictureRect(read->drawing).size().toSize());
        // A stroke far to the right: the file (and its size) changes, the preview grows.
        brulion::drawing::Drawing wider = read->drawing;
        brulion::drawing::Stroke s;
        s.id = u"daleko"_s;
        s.points = {{1500, 300, 0.5}, {1600, 320, 0.5}};
        wider.strokes.append(s);
        write(brulion::drawing::writeExcalidrawMarkdown(read->layout, wider, 1));
        const QSize after = localDrawingSize(file);
        QVERIFY2(after.width() > before.width() + 900, qPrintable(u"%1 → %2"_s.arg(before.width()).arg(after.width())));
        // Emptied: the empty drawing's size.
        write(brulion::drawing::writeExcalidrawMarkdown(read->layout, brulion::drawing::Drawing(), 1));
        QCOMPARE(localDrawingSize(file), QSize(brulion::drawing::EmptyWidth, brulion::drawing::EmptyHeight));
        QVERIFY(!localDrawingSize(dir.filePath(u"brak.excalidraw.md"_s)).isValid());
    }

    void largeDrawingSizeIsCached()
    {
        // A plugin drawing of some 1 MB of scene (dense handwriting): its size read once per
        // version — the embeds ask on the GUI thread while the page scrolls (docs/UI.md
        // "Scrolling performance").
        QFile in(QStringLiteral(BRULION_TEST_DATA "/drawing/obsidian_freedraw.excalidraw.md"));
        QVERIFY(in.open(QIODevice::ReadOnly));
        const std::optional<brulion::drawing::ExcalidrawMarkdownRead> read =
                brulion::drawing::readExcalidrawMarkdown(in.readAll());
        QVERIFY(read);
        brulion::drawing::Drawing d;
        for (int k = 0; k < 1500; ++k) {
            brulion::drawing::Stroke s;
            s.id = u"kreska-"_s + QString::number(k);
            s.simulatePressure = false;
            for (int i = 0; i < 40; ++i)
                s.points.append({(k % 30) * 40.0 + i * 1.5, (k / 30) * 32.0 + 8 * std::sin(i / 3.0), 0.3 + 0.01 * i});
            brulion::drawing::normalizeStroke(s);
            d.strokes.append(s);
        }
        QTemporaryDir dir;
        const QString file = dir.filePath(u"Notatki z wykładu.excalidraw.md"_s);
        QFile out(file);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(brulion::drawing::writeExcalidrawMarkdown(read->layout, d, 1));
        out.close();
        QElapsedTimer timer;
        timer.start();
        const QSize size = localDrawingSize(file);
        const qint64 first = timer.nsecsElapsed();
        timer.restart();
        QCOMPARE(localDrawingSize(file), size);
        const qint64 again = timer.nsecsElapsed();
        QCOMPARE(size, brulion::drawing::pictureRect(d).size().toSize());
        // Where the first time goes: reading the file (LZ-String, JSON), the picture's rect.
        QFile f(file);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        timer.restart();
        const std::optional<brulion::drawing::DrawingFile> parsed = brulion::drawing::readDrawingFile(bytes);
        const qint64 parse = timer.nsecsElapsed();
        QVERIFY(parsed);
        timer.restart();
        const QRectF rect = brulion::drawing::pictureRect(parsed->drawing);
        const qint64 bounds = timer.nsecsElapsed();
        QCOMPARE(rect.size().toSize(), size);
        qInfo("%lld KB plugin file: its size %.1f ms the first time (read %.1f ms, rect %.1f ms), %.3f ms cached",
              qlonglong(QFileInfo(file).size() / 1024), first / 1e6, parse / 1e6, bounds / 1e6, again / 1e6);
        QVERIFY(again < first);
    }

    void drawingExportsStandIn()
    {
        // A plugin drawing Brulion does not draw (a frame): the plugin's export beside it is its
        // preview — its size then.
        QTemporaryDir dir;
        const QString file = dir.filePath(u"Ramka.excalidraw.md"_s);
        QVERIFY(QFile::copy(QStringLiteral(BRULION_TEST_DATA "/drawing/obsidian_frame.excalidraw.md"), file));
        QVERIFY(drawingExportOf(file).isEmpty());
        const QSize own = localDrawingSize(file);
        QVERIFY(own.width() >= 400 && own.height() >= 300); // the frame's box (400 × 300) and margins
        QFile svg(dir.filePath(u"Ramka.excalidraw.svg"_s));
        QVERIFY(svg.open(QIODevice::WriteOnly));
        svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"640\" height=\"480\"/>");
        svg.close();
        QCOMPARE(drawingExportOf(file), svg.fileName());
        QCOMPARE(localDrawingSize(file), QSize(640, 480));
        QVERIFY(QFile::remove(svg.fileName()));
        QCOMPARE(localDrawingSize(file), own);
        // "Ramka.png" (the plugin's other naming), only for .md files.
        QImage png(30, 20, QImage::Format_RGB32);
        png.fill(Qt::white);
        QVERIFY(png.save(dir.filePath(u"Ramka.png"_s)));
        QCOMPARE(drawingExportOf(file), dir.filePath(u"Ramka.png"_s));
        QCOMPARE(localDrawingSize(file), QSize(30, 20));
        QVERIFY(drawingExportOf(dir.filePath(u"Ramka.excalidraw"_s)).isEmpty());
        // An editable drawing is drawn by Brulion: no export stands in.
        const QString editable = dir.filePath(u"Szkic.excalidraw.md"_s);
        QVERIFY(QFile::copy(QStringLiteral(BRULION_TEST_DATA "/drawing/obsidian_freedraw.excalidraw.md"), editable));
        QVERIFY(png.save(dir.filePath(u"Szkic.png"_s)));
        QVERIFY(localDrawingSize(editable) != QSize(30, 20));
    }

    void pictureFormatNames_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("format");
        QTest::addColumn<bool>("qtDecodes"); // QtGui's own decoders, always there
        QTest::newRow("png") << u"assets/żółw.png"_s << u"PNG"_s << true;
        QTest::newRow("upper case") << u"ŁĄKA.JPG"_s << u"JPG"_s << true;
        QTest::newRow("jpeg") << u"zdjęcia/łąka.jpeg"_s << u"JPEG"_s << true;
        QTest::newRow("webp") << u"assets/łąka.webp"_s << u"WEBP"_s << false;
        QTest::newRow("a dot in a folder only") << u"v1.2/plik"_s << QString() << false;
        QTest::newRow("a hidden file") << u"assets/.webp"_s << QString() << false;
        QTest::newRow("a dot at the end") << u"assets/plik."_s << QString() << false;
        QTest::newRow("no extension") << u"assets/plik"_s << QString() << false;
        QTest::newRow("empty") << QString() << QString() << false;
    }
    void pictureFormatNames()
    {
        // The picture views name a format this device cannot decode (Workspace::canDecodePicture,
        // EditorHost::canDecodePicture: the same helpers).
        QFETCH(QString, path);
        QFETCH(QString, format);
        QFETCH(bool, qtDecodes);
        QCOMPARE(pictureFormatName(path), format);
        QCOMPARE(canDecodePictureFormat(path),
                 !format.isEmpty() && QImageReader::supportedImageFormats().contains(format.toLower().toLatin1()));
        if (qtDecodes)
            QVERIFY(canDecodePictureFormat(path));
    }
};

QTEST_MAIN(TestPictureSize)
#include "tst_editor_picturesize.moc"
