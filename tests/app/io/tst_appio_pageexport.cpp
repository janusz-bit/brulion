// One page out of the notebook (ContentExportDialog's ExportController, scope "page"): the page's
// file as it is ("markdown-file", page scope only), Markdown with its attachments, PDF and DOCX
// with the page's pictures, the paper size, and printing — on the desktop into a PDF file without
// the dialog (setPrintToFile), read back with the platform's PDF reader (its text, Polish letters
// and all) where it has one. QTEST_MAIN: the PDF and Word documents need the app's fonts.
#include "exportcontroller.h"
#include "workspace.h"

#include "io/zip.h"
#include "pdfpages.h"

#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::ioviews::ExportController;

namespace {

bool writeRaw(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(bytes);
    return true;
}

QByteArray png()
{
    QImage image(120, 60, QImage::Format_RGB32);
    image.fill(Qt::darkGreen);
    QByteArray out;
    QBuffer buffer(&out);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return out;
}

const QByteArray Page = "# Remont kuchni\n"
                        "\n"
                        "Zażółć gęślą jaźń: [[Zakupy]] i ![[assets/blat.png]]\n"
                        "\n"
                        "- [ ] Kupić blat 📅 2026-11-20\n"
                        "- [x] Zmierzyć ścianę\n";

} // namespace

class TestAppIoPageExport : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<brulion::shell::Workspace> open()
    {
        const QString root = m_tmp.filePath(u"Notatnik %1"_s.arg(++m_n));
        writeRaw(root + u"/Projekty/Remont kuchni.md"_s, Page);
        writeRaw(root + u"/Zakupy.md"_s, "- mleko\n");
        writeRaw(root + u"/assets/blat.png"_s, png());
        auto ws = std::make_unique<brulion::shell::Workspace>();
        ws->setClock([] { return QDateTime(QDate(2026, 11, 3), QTime(9, 30)); });
        ws->sync()->setSyncthingDiscovery(false);
        QString error;
        if (!ws->open(root, root + u"-state"_s, &error))
            qWarning() << error;
        ws->finishIndexing();
        return ws;
    }

    static void forPage(ExportController &c, const QString &format)
    {
        c.setScope(u"page"_s);
        c.setPage(u"Projekty/Remont kuchni.md"_s);
        c.setFormat(format);
    }

    static bool waitState(ExportController &c, const QString &state, int ms = 20000)
    {
        QElapsedTimer t;
        t.start();
        while (c.state() != state && t.elapsed() < ms * BRULION_TEST_SLOWDOWN)
            QTest::qWait(10);
        if (c.state() != state)
            qWarning() << "state" << c.state() << "error" << c.errorText();
        return c.state() == state;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        QStandardPaths::setTestModeEnabled(true);
    }

    void markdownFile()
    {
        auto ws = open();
        ExportController c;
        c.setWorkspace(ws.get());
        c.setFormat(u"markdown-file"_s);
        QCOMPARE(c.format(), u"markdown-file"_s);
        // The whole notebook as one Markdown file: no.
        QTRY_VERIFY(!c.summary().value(u"valid"_s).toBool());
        QVERIFY(!c.summary().value(u"error"_s).toString().isEmpty());
        forPage(c, u"markdown-file"_s);
        QTRY_VERIFY(c.summary().value(u"valid"_s).toBool());
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.result().size(), 1);
        QCOMPARE(c.fileName(), u"Remont kuchni.md"_s);
        QCOMPARE(c.result().first().data, Page);
        // Saved where the dialog says.
        const QString target = m_tmp.filePath(u"zapis/Remont kuchni.md"_s);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QVERIFY(c.saveTo(QUrl::fromLocalFile(target)));
        QFile saved(target);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), Page);
        QCOMPARE(c.state(), u"saved"_s);
    }

    void markdownWithAttachments()
    {
        auto ws = open();
        ExportController c;
        c.setWorkspace(ws.get());
        forPage(c, u"markdown-zip"_s);
        QTRY_COMPARE(c.summary().value(u"attachments"_s).toInt(), 1);
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QStringList paths;
        for (const brulion::io::FileEntry &f : brulion::io::readZip(c.result().first().data))
            paths.append(f.path);
        QVERIFY2(paths.contains(u"Remont kuchni/Projekty/Remont kuchni.md"_s), qPrintable(paths.join(u", "_s)));
        QVERIFY(paths.contains(u"Remont kuchni/assets/blat.png"_s));
        QVERIFY(!paths.contains(u"Remont kuchni/Zakupy.md"_s));
    }

    void paperFormats_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QByteArray>("magic");
        QTest::newRow("pdf") << u"pdf"_s << QByteArray("%PDF-");
        QTest::newRow("docx") << u"docx"_s << QByteArray("PK");
    }
    void paperFormats()
    {
        QFETCH(QString, format);
        QFETCH(QByteArray, magic);
        auto ws = open();
        ExportController c;
        c.setWorkspace(ws.get());
        forPage(c, format);
        QVERIFY(c.webPicturesApply());
        QTRY_VERIFY(c.summary().value(u"valid"_s).toBool());
        c.prepare();
        QVERIFY(waitState(c, u"ready"_s));
        QCOMPARE(c.fileName(), u"Remont kuchni-2026-11-03."_s + format);
        const QByteArray data = c.result().first().data;
        QVERIFY(data.startsWith(magic));
        QVERIFY2(c.warnings().isEmpty(), qPrintable(c.warnings().join(u'\n')));
        if (format == u"docx") {
            QStringList paths;
            for (const brulion::io::FileEntry &f : brulion::io::readZip(data))
                paths.append(f.path);
            QVERIFY(paths.contains(u"word/media/image1.png"_s)); // the page's picture came along
        }
    }

    void paperSize()
    {
        ExportController c;
        QVERIFY(c.paperSize() == u"a4" || c.paperSize() == u"letter");
        // (The default follows the region: Letter on a US machine, so setting it changes nothing.)
        const bool startsLetter = c.paperSize() == u"letter";
        QSignalSpy options(&c, &ExportController::optionsChanged);
        c.setPaperSize(u"LETTER"_s);
        QCOMPARE(c.paperSize(), u"letter"_s);
        c.setPaperSize(u"nie-ma"_s);
        QCOMPARE(c.paperSize(), u"a4"_s);
        QCOMPARE(options.size(), startsLetter ? 1 : 2);
    }

    void printToFile()
    {
        auto ws = open();
        ExportController c;
        c.setWorkspace(ws.get());
        forPage(c, u"markdown-file"_s); // printing does not care about the format
        QTRY_VERIFY(c.summary().value(u"valid"_s).toBool());
#ifdef BRULION_PRINT_DIALOG
        QVERIFY(c.printAvailable());
        const QString pdf = m_tmp.filePath(u"wydruk.pdf"_s);
        c.setPrintToFile(pdf);
        QSignalSpy printed(&c, &ExportController::printed);
        c.print();
        QVERIFY(waitState(c, u"printed"_s));
        QCOMPARE(printed.size(), 1);
        QVERIFY(printed.first().first().toString().contains(u"Remont kuchni"_s));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().startsWith("%PDF-"));
        file.close();
        if (!brulion::platform::pdfTextAvailable())
            QSKIP("No PDF reader with text in this build");
        brulion::platform::PdfInfo info;
        auto reader = brulion::platform::PdfReader::open(pdf, &info);
        QVERIFY2(reader, qPrintable(info.error));
        QCOMPARE(reader->pageCount(), 1);
        const QString text = reader->pageText(1);
        QVERIFY2(text.contains(u"Zażółć gęślą jaźń"_s), qPrintable(text));
        QVERIFY(text.contains(u"Remont kuchni"_s));
        QVERIFY(text.contains(u"Kupić blat"_s));
        QVERIFY(!text.contains(u"mleko"_s)); // the linked page is not printed
        // Letter when asked: 612 × 792 points.
        c.setPaperSize(u"letter"_s);
        c.print();
        QVERIFY(waitState(c, u"printed"_s));
        reader = brulion::platform::PdfReader::open(pdf, &info);
        QVERIFY(reader);
        QVERIFY(qAbs(reader->pageSize(1).width() - 612) < 1);
        QVERIFY(qAbs(reader->pageSize(1).height() - 792) < 1);
#else
        QVERIFY(!c.printAvailable());
        c.print();
        QCOMPARE(c.state(), u"error"_s);
#endif
    }
};

QTEST_MAIN(TestAppIoPageExport)
#include "tst_appio_pageexport.moc"
