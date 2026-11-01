// PDFs on the real Workspace (src/app/shell/workspacepdf.cpp, docs/UI.md "PDF viewer"): isPdf and a
// file's version; "Otwórz w innej aplikacji" through a recording hook (a PDF of the notebook handed
// over; a path leaving the notebook, a missing file, a folder, HTML named .pdf, a text file refused
// with the message; no app / a blocked browser tab told); "Rysuj na tej stronie" (the drawing stored
// and embedded right after the PDF's block, byte for byte; a second one with a free name; no note or
// a deleted one → today's journal; a failed item refused); the drawings of a page found again; the
// thumbnails relayed by vault path with this build's renderer (a locked PDF failing with its reason).
#include "workspace.h"

#include "drawing/backdrop.h"
#include "drawing/drawing.h"
#include "drawing/svgwrite.h"
#include "openexternal.h"
#include "pdfpages.h"
#include "pdfrenderpool.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace Qt::StringLiterals;
using brulion::platform::OpenResult;
using brulion::shell::Workspace;

namespace {

bool writeFile(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
}

QByteArray fixture(const QString &name)
{
    QFile f(QDir(QStringLiteral(BRULION_TEST_DATA)).filePath(u"pdf/"_s + name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString readFile(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

/// What DrawingBackdrops.prepare(pdf, [page], true) delivers: a drawing on a page's raster.
QVariantMap preparedDrawing(const QString &pdf, int page, const QString &name)
{
    QImage image(120, 170, QImage::Format_RGB32);
    image.fill(Qt::white);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    const brulion::drawing::BackdropImage raster{png, u"image/png"_s, image.size()};
    const std::optional<brulion::drawing::Drawing> d = brulion::drawing::newDrawingOnBackdrop(
            raster, brulion::drawing::BackdropSource{pdf, page}, brulion::drawing::randomEditStamp(1'790'000'000'000));
    return {{u"svg"_s, d ? brulion::drawing::toSvg(*d) : QByteArray()},
            {u"path"_s, pdf},
            {u"page"_s, page},
            {u"name"_s, name},
            {u"error"_s, QString()}};
}

} // namespace

class TestWorkspacePdf : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

    struct Opened {
        QString root;
        std::unique_ptr<Workspace> ws;
    };
    Opened open(const QString &name)
    {
        Opened o;
        o.root = m_tmp.filePath(name);
        writeFile(o.root + u"/assets/wykład.pdf"_s, fixture(u"wyklad.pdf"_s));
        writeFile(o.root + u"/assets/sejf.pdf"_s, fixture(u"zaszyfrowany.pdf"_s));
        writeFile(o.root + u"/assets/strona.pdf"_s, "<!doctype html><p>Zażółć gęślą jaźń</p>");
        writeFile(o.root + u"/notatka.txt"_s, "%PDF-1.4 to jednak tekst");
        QDir().mkpath(o.root + u"/assets/katalog.pdf"_s);
        writeFile(o.root + u"/Wykład – ćwiczenia.md"_s, "- Wstęp\n- Slajdy ![[wykład.pdf]]\n  - uwaga\n- Dalej\n");
        o.ws = std::make_unique<Workspace>();
        o.ws->setClock([] { return QDateTime(QDate(2026, 11, 1), QTime(10, 0)); });
        o.ws->setPdfThumbnailCacheDir(m_tmp.filePath(name + u"-thumbs"_s));
        if (!o.ws->open(o.root, m_tmp.filePath(name + u"-state"_s)))
            return {};
        o.ws->finishIndexing();
        return o;
    }

private Q_SLOTS:
    void cleanup()
    {
        brulion::platform::setOpenFileHook({});
        if (QThreadPool *pool = brulion::pdfview::PdfRenderPool::pool())
            pool->waitForDone(10000);
    }

    void namesAndVersions()
    {
        Opened o = open(u"wersje"_s);
        QVERIFY(o.ws);
        QVERIFY(o.ws->isPdf(u"assets/wykład.pdf"_s));
        QVERIFY(o.ws->isPdf(u"Ćwiczenia.PDF"_s));
        QVERIFY(!o.ws->isPdf(u"Wykład – ćwiczenia.md"_s));
        QCOMPARE(o.ws->pdfViewable(), brulion::platform::pdfAvailable());
        QCOMPARE(o.ws->pdfSearchable(), brulion::platform::pdfTextAvailable());
        const QString before = o.ws->fileVersion(u"assets/wykład.pdf"_s);
        QVERIFY(before.contains(u'-'));
        QVERIFY(before.endsWith(u'-' + QString::number(fixture(u"wyklad.pdf"_s).size())));
        QVERIFY(o.ws->fileVersion(u"assets/brak.pdf"_s).isEmpty());
        QVERIFY(o.ws->fileVersion(u"assets/katalog.pdf"_s).isEmpty());
        QVERIFY(o.ws->writeAttachment(u"assets/wykład.pdf"_s, fixture(u"tylko-wlasciciel.pdf"_s)));
        QVERIFY(o.ws->fileVersion(u"assets/wykład.pdf"_s) != before);
    }

    void openInOtherApp_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<bool>("handed");
        QTest::newRow("a PDF of the notebook") << u"assets/wykład.pdf"_s << true;
        QTest::newRow("a locked PDF") << u"assets/sejf.pdf"_s << true;
        QTest::newRow("leaves the notebook") << u"../poza.pdf"_s << false;
        QTest::newRow("missing") << u"assets/brak.pdf"_s << false;
        QTest::newRow("a folder") << u"assets/katalog.pdf"_s << false;
        QTest::newRow("HTML named .pdf") << u"assets/strona.pdf"_s << false;
        QTest::newRow("a text file") << u"notatka.txt"_s << false;
        QTest::newRow("empty") << QString() << false;
    }

    void openInOtherApp()
    {
        QFETCH(QString, path);
        QFETCH(bool, handed);
        Opened o = open(u"otworz"_s);
        QVERIFY(o.ws);
        writeFile(m_tmp.filePath(u"poza.pdf"_s), fixture(u"wyklad.pdf"_s));
        QStringList calls;
        brulion::platform::setOpenFileHook([&calls](const QString &file, const QString &mime) {
            calls.append(file + u'|' + mime);
            return OpenResult::Opened;
        });
        QSignalSpy errors(o.ws.get(), &Workspace::errorOccurred);
        QCOMPARE(o.ws->openInOtherApp(path), handed);
        if (handed) {
            QCOMPARE(calls, QStringList{o.ws->absoluteFilePath(path) + u"|application/pdf"_s});
            QVERIFY(errors.isEmpty());
        } else {
            QVERIFY(calls.isEmpty());
            if (!path.isEmpty()) {
                QCOMPARE(errors.size(), 1);
                QVERIFY(errors.first().first().toString().contains(u"is not a PDF file"_s));
            }
        }
    }

    void openInOtherAppOutcomes()
    {
        Opened o = open(u"wyniki"_s);
        QVERIFY(o.ws);
        QSignalSpy errors(o.ws.get(), &Workspace::errorOccurred);
        QSignalSpy notices(o.ws.get(), &Workspace::noticeOccurred);
        brulion::platform::setOpenFileHook([](const QString &, const QString &) { return OpenResult::NoApp; });
        QVERIFY(!o.ws->openInOtherApp(u"assets/wykład.pdf"_s));
        QCOMPARE(errors.size(), 1);
        QVERIFY(errors.last().first().toString().contains(u"No app"_s));
        brulion::platform::setOpenFileHook([](const QString &, const QString &) { return OpenResult::Blocked; });
        QVERIFY(o.ws->openInOtherApp(u"assets/wykład.pdf"_s));
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.last().first().toString().contains(u"downloaded"_s));
        brulion::platform::setOpenFileHook([](const QString &, const QString &) { return OpenResult::Failed; });
        QVERIFY(!o.ws->openInOtherApp(u"assets/wykład.pdf"_s));
        QCOMPARE(errors.size(), 2);
        QVERIFY(errors.last().first().toString().contains(u"wykład.pdf"_s));
    }

    void drawingAfterThePdf()
    {
        Opened o = open(u"rysunek"_s);
        QVERIFY(o.ws);
        const QString note = u"Wykład – ćwiczenia.md"_s;
        const QVariantMap out = o.ws->addDrawingOnPdfPage(
                u"assets/wykład.pdf"_s, preparedDrawing(u"assets/wykład.pdf"_s, 2, u"wykład"_s), note, 1);
        QCOMPARE(out.value(u"path"_s).toString(), u"assets/wykład p. 2.svg"_s);
        QCOMPARE(out.value(u"note"_s).toString(), note);
        QVERIFY(!out.value(u"journal"_s).toBool());
        QVERIFY(QFileInfo(o.root + u"/assets/wykład p. 2.svg"_s).isFile());
        const QString target = o.ws->linkTarget(u"assets/wykład p. 2.svg"_s, note);
        QVERIFY(!target.isEmpty());
        // Right after the PDF's block (after its child), the rest byte for byte.
        QCOMPARE(readFile(o.root + u"/"_s + note),
                 u"- Wstęp\n- Slajdy ![[wykład.pdf]]\n  - uwaga\n- ![[%1]]\n- Dalej\n"_s.arg(target));
        // The same page again: a free name.
        const QVariantMap again = o.ws->addDrawingOnPdfPage(
                u"assets/wykład.pdf"_s, preparedDrawing(u"assets/wykład.pdf"_s, 2, u"wykład"_s), note, 1);
        QCOMPARE(again.value(u"path"_s).toString(), u"assets/wykład p. 2 2.svg"_s);
        // The drawings of page 2 the note embeds (the backdrop's marker), none of page 3.
        const QVariantList onTwo = o.ws->drawingsOnPdfPage(u"assets/wykład.pdf"_s, 2, note);
        QCOMPARE(onTwo.size(), 2);
        QCOMPARE(onTwo.first().toMap().value(u"path"_s).toString(), u"assets/wykład p. 2 2.svg"_s);
        QCOMPARE(onTwo.first().toMap().value(u"name"_s).toString(), u"wykład p. 2 2"_s);
        QVERIFY(o.ws->drawingsOnPdfPage(u"assets/wykład.pdf"_s, 3, note).isEmpty());
        QVERIFY(o.ws->drawingsOnPdfPage(u"assets/sejf.pdf"_s, 2, note).isEmpty());
        QVERIFY(o.ws->drawingsOnPdfPage(u"assets/wykład.pdf"_s, 2, u"brak.md"_s).isEmpty());
    }

    void drawingIntoTheJournal()
    {
        Opened o = open(u"dziennik"_s);
        QVERIFY(o.ws);
        // Opened from the tree: no note.
        const QVariantMap out = o.ws->addDrawingOnPdfPage(
                u"assets/wykład.pdf"_s, preparedDrawing(u"assets/wykład.pdf"_s, 1, u"wykład"_s), QString(), -1);
        QVERIFY(out.value(u"journal"_s).toBool());
        const QString journal = out.value(u"note"_s).toString();
        QCOMPARE(journal, o.ws->journalPath(u"2026-11-01"_s));
        QVERIFY(readFile(o.root + u"/"_s + journal).contains(u"![["_s));
        QVERIFY(readFile(o.root + u"/"_s + journal).contains(u"p. 1"_s));
        // The note went meanwhile: the journal too.
        const QVariantMap gone = o.ws->addDrawingOnPdfPage(
                u"assets/wykład.pdf"_s, preparedDrawing(u"assets/wykład.pdf"_s, 3, u"wykład"_s), u"Usunięta.md"_s, 4);
        QVERIFY(gone.value(u"journal"_s).toBool());
        QVERIFY(readFile(o.root + u"/"_s + journal).contains(u"p. 3"_s));
    }

    void failedItemRefused()
    {
        Opened o = open(u"blad"_s);
        QVERIFY(o.ws);
        QSignalSpy errors(o.ws.get(), &Workspace::errorOccurred);
        QVariantMap broken = preparedDrawing(u"assets/sejf.pdf"_s, 1, u"sejf"_s);
        broken.insert(u"error"_s, u"locked"_s);
        QVERIFY(o.ws->addDrawingOnPdfPage(u"assets/sejf.pdf"_s, broken, u"Wykład – ćwiczenia.md"_s, 1).isEmpty());
        QCOMPARE(errors.size(), 1);
        QVERIFY(o.ws->addDrawingOnPdfPage(u"assets/sejf.pdf"_s, {}, u"Wykład – ćwiczenia.md"_s, 1).isEmpty());
        QCOMPARE(readFile(o.root + u"/Wykład – ćwiczenia.md"_s),
                 u"- Wstęp\n- Slajdy ![[wykład.pdf]]\n  - uwaga\n- Dalej\n"_s);
    }

    void thumbnails()
    {
        Opened o = open(u"miniatury"_s);
        QVERIFY(o.ws);
        if (!brulion::platform::pdfAvailable()) {
            QCOMPARE(o.ws->pdfThumbnail(u"assets/wykład.pdf"_s, 1).value(u"state"_s).toString(), u"unsupported"_s);
            return;
        }
        QSignalSpy changed(o.ws.get(), &Workspace::pdfThumbnailChanged);
        QCOMPARE(o.ws->pdfThumbnail(u"assets/wykład.pdf"_s, 1).value(u"state"_s).toString(), u"loading"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 20000);
        QCOMPARE(changed.first().first().toString(), u"assets/wykład.pdf"_s);
        const QVariantMap ready = o.ws->pdfThumbnail(u"assets/wykład.pdf"_s, 1);
        QCOMPARE(ready.value(u"state"_s).toString(), u"ready"_s);
        QVERIFY(ready.value(u"url"_s).toUrl().isLocalFile());
        QVERIFY(ready.value(u"url"_s).toUrl().toLocalFile().startsWith(m_tmp.filePath(u"miniatury-thumbs"_s)));
        QCOMPARE(ready.value(u"pages"_s).toInt(), 3);
        QVERIFY(ready.value(u"width"_s).toInt() > 0);
        QVERIFY(ready.value(u"pageHeight"_s).toDouble() > ready.value(u"pageWidth"_s).toDouble());
        // A locked PDF: failed, with its reason.
        changed.clear();
        o.ws->pdfThumbnail(u"assets/sejf.pdf"_s, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 20000);
        const QVariantMap locked = o.ws->pdfThumbnail(u"assets/sejf.pdf"_s, 1);
        QCOMPARE(locked.value(u"state"_s).toString(), u"failed"_s);
        QVERIFY(locked.value(u"locked"_s).toBool());
        QVERIFY(locked.value(u"error"_s).toString().contains(u"password"_s));
        // A missing file fails at once.
        QCOMPARE(o.ws->pdfThumbnail(u"assets/brak.pdf"_s, 1).value(u"state"_s).toString(), u"failed"_s);
        // The file changed: told again (a new mtime is a new thumbnail).
        changed.clear();
        QVERIFY(o.ws->writeAttachment(u"assets/wykład.pdf"_s, fixture(u"tylko-wlasciciel.pdf"_s)));
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 5000);
        QCOMPARE(changed.first().first().toString(), u"assets/wykład.pdf"_s);
    }
};

QTEST_MAIN(TestWorkspacePdf)
#include "tst_workspace_pdf.moc"
