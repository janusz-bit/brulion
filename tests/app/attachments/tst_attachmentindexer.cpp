// The attachments' text for the whole-notebook search (src/app/attachments/attachmentindexer.h;
// docs/UI.md "Searching attachments") on a notebook in a temporary folder with the fixtures of
// tests/data (a text PDF with Polish text, a scanned PDF page, a whiteboard's picture): the PDF's text
// layer found by page with excerpts, scans and pictures by their names only while OCR is off; the
// device cache (records by content hash, the notebook's manifest) reused after a restart without
// reading a file again, a renamed file's text following it unread, a changed file read again, a
// removed one gone; turned off; OCR (skipped without Tesseract's Polish and English models):
// pictures and scanned pages found by their words, highlights and the PDF viewer's lookup, pause and
// resume, OCR languages changed, the cache cleared. Pure helpers: EXIF orientations back to the
// file's pixels, the pages' OCR resolution, which files are read.
#include "attachmentindexer.h"
#include "ocr.h"
#include "pdfpages.h"
#include "pdfrenderpool.h"

#include "testtiming.h"
#include "attachments/documenttext.h"
#include "io/zip.h"
#include "text/text.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::attachsearch;
using attachments::AttachmentSearch;
using attachments::Kind;

namespace {

QString data(const QString &name)
{
    return QStringLiteral(BRULION_TEST_DATA) + u'/' + name;
}

QByteArray bytesOf(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const AttachmentSearch::Hit *hitFor(const QList<AttachmentSearch::Hit> &hits, const QString &path)
{
    for (const AttachmentSearch::Hit &h : hits) {
        if (h.path == path)
            return &h;
    }
    return nullptr;
}

/// Wait (the event loop running) until `ready`, at most `ms` (× the sanitizer build's slowdown).
template<typename Ready>
bool waitFor(Ready ready, int ms = 60000)
{
    return QTest::qWaitFor(ready, ms * BRULION_TEST_SLOWDOWN);
}

/// The process's peak resident memory (VmHWM, KiB; 0 elsewhere than Linux).
qint64 peakKib()
{
    QFile status(u"/proc/self/status"_s);
    if (!status.open(QIODevice::ReadOnly))
        return 0;
    for (const QByteArray &line : status.readAll().split('\n')) {
        if (line.startsWith("VmHWM:"))
            return line.mid(6).trimmed().split(' ').value(0).toLongLong();
    }
    return 0;
}

/// The peak resident memory counted again from now (Linux: /proc/self/clear_refs "5").
void resetPeak()
{
    QFile refs(u"/proc/self/clear_refs"_s);
    if (refs.open(QIODevice::WriteOnly))
        refs.write("5");
}

bool ocrHere()
{
    return platform::ocrAvailable() && platform::ocrLanguages().contains(u"pol"_s)
           && platform::ocrLanguages().contains(u"eng"_s);
}

/// Every record file of the cache with its modification time (ms).
QHash<QString, qint64> records(const QString &dir)
{
    QHash<QString, qint64> out;
    for (const QFileInfo &fi : QDir(dir).entryInfoList({u"*.bat"_s}, QDir::Files))
        out.insert(fi.fileName(), fi.lastModified().toMSecsSinceEpoch());
    return out;
}

} // namespace

class TestAttachmentIndexer : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_root;
    QString m_cache;
    QString m_manifest;
    std::unique_ptr<Vault> m_vault;
    std::unique_ptr<AttachmentIndexer> m_indexer;

    const QString Lecture = u"assets/Wykład – ćwiczenia.pdf"_s;
    const QString Scan = u"assets/skan.pdf"_s;
    const QString Board = u"zdjęcia/tablica.png"_s;

    void openIndexer(bool ocr = false)
    {
        m_indexer = std::make_unique<AttachmentIndexer>();
        m_indexer->setUiLanguage(u"pl"_s);
        m_indexer->setOcrEnabled(ocr);
        m_indexer->attach(m_vault.get(), m_cache, m_manifest);
        m_indexer->start(0);
    }
    bool waitIdle(int ms = 60000)
    {
        return waitFor([this] { return m_indexer->idle(); }, ms);
    }
    /// The OCR engine's probe has come back (the indexer asks on a thread of its own).
    bool waitProbed()
    {
        return waitFor([this] { return m_indexer->ocrUnavailableReason() != u"probing"_s; }, 30000);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_root = m_dir.filePath(u"Notatki"_s);
        m_cache = m_dir.filePath(u"cache/attachment-text"_s);
        m_manifest = m_cache + u"/notebooks/test.manifest"_s;
        QVERIFY(QDir().mkpath(m_root + u"/assets"_s));
        QVERIFY(QDir().mkpath(m_root + u"/zdjęcia"_s));
        QVERIFY(QFile::copy(data(u"pdf/wyklad.pdf"_s), m_root + u'/' + Lecture));
        QVERIFY(QFile::copy(data(u"attachments/skan.pdf"_s), m_root + u'/' + Scan));
        QVERIFY(QFile::copy(data(u"attachments/tablica.png"_s), m_root + u'/' + Board));
        QFile note(m_root + u"/Notatka.md"_s);
        QVERIFY(note.open(QIODevice::WriteOnly));
        note.write("# Notatka\n- żółw w notatce\n");
        note.close();
        m_vault = std::make_unique<Vault>(m_root);
        QVERIFY(m_vault->open());
    }

    void cleanup()
    {
        // (Each test leaves the indexer it made running for the next one only when it says so.)
    }

    void helpers()
    {
        QVERIFY(isSearchableAttachment(u"a/b.pdf"_s));
        QVERIFY(isSearchableAttachment(u"Zdjęcie.JPG"_s));
        QVERIFY(isSearchableAttachment(u"x.webp"_s));
        QVERIFY(isSearchableAttachment(u"umowy/Umowa.docx"_s));
        QVERIFY(isSearchableAttachment(u"Wykład.PPTX"_s));
        QVERIFY(isSearchableAttachment(u"opis.odt"_s));
        QVERIFY(isSearchableAttachment(u"książka.epub"_s));
        QVERIFY(!isSearchableAttachment(u"makro.docm"_s));
        QVERIFY(!isSearchableAttachment(u"stare.doc"_s));
        QVERIFY(!isSearchableAttachment(u".trash/Umowa.docx"_s));
        QVERIFY(!isSearchableAttachment(u"Umowa.sync-conflict-20260101-120000-ABCDEFG.docx"_s));
        QVERIFY(!isSearchableAttachment(u"rysunek.svg"_s));
        QVERIFY(!isSearchableAttachment(u"anim.gif"_s));
        QVERIFY(!isSearchableAttachment(u"Notatka.md"_s));
        QVERIFY(!isSearchableAttachment(u".trash/stary.pdf"_s));
        QVERIFY(!isSearchableAttachment(u"wykład.sync-conflict-20260101-120000-ABCDEFG.pdf"_s));

        // ~300 dpi, the long side within 1000…4200.
        const QSize a4 = ocrPagePixels(QSizeF(595.28, 841.89));
        QVERIFY2(qAbs(a4.width() - 2480) <= 1 && qAbs(a4.height() - 3508) <= 1,
                 qPrintable(u"%1×%2"_s.arg(a4.width()).arg(a4.height())));
        QCOMPARE(std::max(ocrPagePixels(QSizeF(5000, 3000)).width(), ocrPagePixels(QSizeF(5000, 3000)).height()), 4200);
        QCOMPARE(std::max(ocrPagePixels(QSizeF(50, 30)).width(), ocrPagePixels(QSizeF(50, 30)).height()), 1000);
        QVERIFY(ocrPagePixels(QSizeF()).isEmpty());
    }

    /// DOCX, PPTX, ODT and EPUB files of a notebook of their own: read without any PDF renderer or OCR,
    /// by slide / part, the cache reused, a rename followed, a change read again; a damaged, an
    /// encrypted, a bomb-like and a too large file found by its name only.
    void documents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.filePath(u"Notatki"_s);
        const QString cache = dir.filePath(u"cache/attachment-text"_s);
        const QString manifest = cache + u"/notebooks/doc.manifest"_s;
        QVERIFY(QDir().mkpath(root + u"/Dokumenty"_s));
        const auto put = [&root](const QString &path, const QByteArray &bytes) {
            QFile f(root + u'/' + path);
            QVERIFY(QDir().mkpath(QFileInfo(f).absolutePath()));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(bytes), bytes.size());
        };
        const QString wns = u"xmlns:w='http://schemas.openxmlformats.org/wordprocessingml/2006/main'"_s;
        const QString ans = u"xmlns:a='http://schemas.openxmlformats.org/drawingml/2006/main' "
                            "xmlns:p='http://schemas.openxmlformats.org/presentationml/2006/main'"_s;
        const auto entry = [](const QString &path, const QString &xml) { return io::FileEntry{path, xml.toUtf8()}; };
        const auto word = [&](const QString &text) {
            return io::writeZip({entry(u"word/document.xml"_s,
                                       u"<w:document "_s + wns + u"><w:body><w:p><w:r><w:t>"_s + text
                                               + u"</w:t></w:r></w:p></w:body></w:document>"_s)});
        };
        const auto slide = [&](const QString &text) {
            return u"<p:sld "_s + ans + u"><p:cSld><p:spTree><p:sp><p:txBody><a:p><a:r><a:t>"_s + text
                   + u"</a:t></a:r></a:p></p:txBody></p:sp></p:spTree></p:cSld></p:sld>"_s;
        };
        const QString Contract = u"Dokumenty/Umowa o dzieło.docx"_s;
        const QString Slides = u"Wykłady/Algebra.pptx"_s;
        const QString Text = u"Dokumenty/Opis.odt"_s;
        const QString Book = u"Książki/Powieść.epub"_s;
        const QString Broken = u"Dokumenty/Zepsuty.docx"_s;
        const QString Locked = u"Dokumenty/Szyfrowany.docx"_s;
        const QString Bomb = u"Dokumenty/Bomba.docx"_s;
        const QString Huge = u"Dokumenty/Ogromny.docx"_s;
        put(Contract, word(u"Umowa o dzieło: żółw zielony przechodzi przez łąkę"_s));
        put(Slides,
            io::writeZip({entry(u"ppt/slides/slide1.xml"_s, slide(u"Wstęp do algebry"_s)),
                          entry(u"ppt/slides/slide2.xml"_s, slide(u"Macierze, wyznaczniki i gęślą jaźń"_s))}));
        put(Text,
            io::writeZip({entry(u"content.xml"_s,
                                u"<office:document-content xmlns:office='urn:oasis:names:tc:opendocument:xmlns:office:1.0' "
                                 "xmlns:text='urn:oasis:names:tc:opendocument:xmlns:text:1.0'><office:body><office:text>"
                                 "<text:p>Opis łódki wąskiej</text:p></office:text></office:body></office:document-content>"_s)}));
        put(Book,
            io::writeZip({{u"mimetype"_s, "application/epub+zip"},
                          entry(u"a.xhtml"_s, u"<html><body><p>Pewien wąż spotkał jeża</p></body></html>"_s)}));
        put(Broken, QByteArray("to nie jest archiwum ZIP, tylko tekst z ąęś"));
        put(Locked, QByteArray("\xd0\xcf\x11\xe0\xa1\xb1\x1a\xe1") + QByteArray(2000, '\0'));
        QString bomb = u"<w:document "_s + wns + u"><w:body><w:p><w:r><w:t>"_s + QString(64 << 20, u'a')
                       + u"</w:t></w:r></w:p></w:body></w:document>"_s;
        put(Bomb, io::writeZip({entry(u"word/document.xml"_s, bomb)}));
        bomb.clear();
        {
            // A file over the limit: sparse where the file system allows (else the test skips this part).
            QFile huge(root + u'/' + Huge);
            QVERIFY(huge.open(QIODevice::WriteOnly));
            huge.write(word(u"ukryty żółw"_s));
            if (!huge.resize(attachments::DocumentLimits().maxFileBytes + 1000))
                QVERIFY(huge.resize(100)); // (then it is small: read as damaged)
        }
        Vault vault(root);
        QVERIFY(vault.open());
        const auto open = [&] {
            auto indexer = std::make_unique<AttachmentIndexer>();
            indexer->setUiLanguage(u"pl"_s);
            indexer->attach(&vault, cache, manifest);
            indexer->start(0);
            return indexer;
        };
        std::unique_ptr<AttachmentIndexer> indexer = open();
        QVERIFY(waitFor([&] { return indexer->idle(); }));
        QCOMPARE(indexer->state(), u"idle"_s);
        QCOMPARE(indexer->indexedFiles(), 8);
        // A text and a slide, by Polish words, diacritics optional.
        QList<AttachmentSearch::Hit> hits = indexer->search(u"zolw lake"_s, 10);
        const AttachmentSearch::Hit *contract = hitFor(hits, Contract);
        QVERIFY(contract);
        QCOMPARE(contract->kind, Kind::Docx);
        QCOMPARE(contract->page, 1);
        QCOMPARE(contract->pageCount, 1);
        QVERIFY(!contract->ocr && !contract->nameOnly);
        QVERIFY2(contract->excerpt.contains(u"żółw zielony"_s), qPrintable(contract->excerpt));
        QVERIFY2(!contract->find.isEmpty(), "what to show the hit with");
        hits = indexer->search(u"gesla jazn"_s, 10);
        const AttachmentSearch::Hit *slides = hitFor(hits, Slides);
        QVERIFY(slides);
        QCOMPARE(slides->kind, Kind::Pptx);
        QCOMPARE(slides->page, 2); // (the second slide)
        QCOMPARE(slides->pageCount, 2);
        QVERIFY(slides->excerpt.contains(u"Macierze"_s));
        QCOMPARE(hitFor(indexer->search(u"lodki"_s, 10), Text)->kind, Kind::Odt);
        QCOMPARE(hitFor(indexer->search(u"jeza waz"_s, 10), Book)->kind, Kind::Epub);
        // Kinds: documents are not PDFs; the filter's words.
        QVERIFY(hitFor(indexer->search(u"zolw"_s, 10, attachments::Scope::Documents), Contract));
        QVERIFY(!hitFor(indexer->search(u"zolw"_s, 10, attachments::Scope::Pdf), Contract));
        QVERIFY(!hitFor(indexer->search(u"zolw"_s, 10, attachments::Scope::Pictures), Contract));
        // The page as the cache holds it.
        const std::optional<attachments::Page> page = indexer->page(Slides, 2);
        QVERIFY(page && page->source == attachments::PageSource::TextLayer && page->text.contains(u"wyznaczniki"_s));
        // A damaged, an encrypted, a bomb-like and a too large file: found by name only, nothing of
        // their bytes in the index.
        for (const QString &path : {Broken, Locked, Bomb, Huge}) {
            const AttachmentSearch::Hit *hit =
                    hitFor(indexer->search(QFileInfo(path).completeBaseName(), 10), path);
            QVERIFY2(hit && hit->nameOnly, qPrintable(path));
        }
        QVERIFY(!hitFor(indexer->search(u"ukryty"_s, 10), Huge));
        QVERIFY(!hitFor(indexer->search(u"aaaaaaaa"_s, 10), Bomb));
        // Records: one per readable content (and the damaged ones'), kept after the indexer goes.
        const QHash<QString, qint64> before = records(cache);
        QVERIFY(before.size() >= 5);
        indexer.reset();
        QVERIFY(QFileInfo::exists(manifest));

        // Again: nothing read again.
        QTest::qWait(20);
        indexer = open();
        QVERIFY(waitFor([&] { return indexer->idle(); }));
        QVERIFY(hitFor(indexer->search(u"zolw"_s, 10), Contract));
        QCOMPARE(records(cache), before);

        // Renamed: the text follows.
        const QString moved = u"Dokumenty/Umowa (stara).docx"_s;
        QVERIFY(vault.rename(Contract, moved));
        QVERIFY(waitFor([&] { return hitFor(indexer->search(u"zolw"_s, 10), moved) != nullptr; }));
        QVERIFY(!hitFor(indexer->search(u"zolw"_s, 10), Contract));
        QVERIFY(waitFor([&] { return indexer->idle(); }));
        QCOMPARE(records(cache), before);
        // Changed: read again.
        QVERIFY(vault.writeBinary(moved, word(u"Aneks: kowalska jaźń i wąż"_s)));
        QVERIFY(waitFor([&] { return hitFor(indexer->search(u"kowalska"_s, 10), moved) != nullptr; }));
        QVERIFY(!hitFor(indexer->search(u"zolw"_s, 10), moved));
        // Removed: gone.
        QVERIFY(vault.remove(moved));
        QVERIFY(waitFor([&] { return !indexer->index().contains(moved); }));
        // Turned off: nothing found.
        indexer->setEnabled(false);
        QVERIFY(indexer->search(u"algebry"_s, 10).isEmpty());
        indexer->setEnabled(true);
        QVERIFY(waitFor([&] { return indexer->idle(); }));
        QVERIFY(hitFor(indexer->search(u"algebry"_s, 10), Slides));
        QVERIFY(AttachmentIndexer::documentTextAvailable());
    }

    void orientations_data()
    {
        QTest::addColumn<int>("transformation");
        using T = QImageIOHandler::Transformation;
        QTest::newRow("none") << int(T::TransformationNone);
        QTest::newRow("mirror") << int(T::TransformationMirror);
        QTest::newRow("flip") << int(T::TransformationFlip);
        QTest::newRow("rotate180") << int(T::TransformationRotate180);
        QTest::newRow("rotate90") << int(T::TransformationRotate90);
        QTest::newRow("mirrorAndRotate90") << int(T::TransformationMirrorAndRotate90);
        QTest::newRow("flipAndRotate90") << int(T::TransformationFlipAndRotate90);
        QTest::newRow("rotate270") << int(T::TransformationRotate270);
    }
    void orientations()
    {
        // A 5 × 3 picture with one marked pixel, turned the way Qt's reader turns a picture with that
        // EXIF orientation (mirror / flip, then 90° clockwise; 270° alone): the marked pixel's place
        // in the turned picture maps back onto its place in the file.
        QFETCH(int, transformation);
        const auto t = QImageIOHandler::Transformations::fromInt(transformation);
        QImage raw(5, 3, QImage::Format_RGB32);
        raw.fill(Qt::white);
        raw.setPixel(1, 0, qRgb(255, 0, 0));
        QImage shown = raw;
        if (t == QImageIOHandler::TransformationRotate270) {
            shown = raw.transformed(QTransform().rotate(270));
        } else {
            shown = shown.flipped(
                    (t.testFlag(QImageIOHandler::TransformationMirror) ? Qt::Horizontal : Qt::Orientations())
                    | (t.testFlag(QImageIOHandler::TransformationFlip) ? Qt::Vertical : Qt::Orientations()));
            if (t.testFlag(QImageIOHandler::TransformationRotate90))
                shown = shown.transformed(QTransform().rotate(90));
        }
        QPoint at(-1, -1);
        for (int y = 0; y < shown.height(); ++y) {
            for (int x = 0; x < shown.width(); ++x) {
                if (shown.pixel(x, y) == qRgb(255, 0, 0))
                    at = QPoint(x, y);
            }
        }
        QVERIFY(at.x() >= 0);
        const QRectF box(qreal(at.x()) / shown.width(), qreal(at.y()) / shown.height(), 1.0 / shown.width(),
                         1.0 / shown.height());
        const QRectF back = unorientedBox(box, transformation);
        QVERIFY2(qAbs(back.left() - 1.0 / 5) < 1e-9 && qAbs(back.top()) < 1e-9 && qAbs(back.width() - 1.0 / 5) < 1e-9
                         && qAbs(back.height() - 1.0 / 3) < 1e-9,
                 qPrintable(u"%1,%2 %3×%4"_s.arg(back.left()).arg(back.top()).arg(back.width()).arg(back.height())));
    }

    void textLayer()
    {
        if (!platform::pdfTextAvailable())
            QSKIP("this build reads no PDF text");
        openIndexer();
        QVERIFY(waitIdle());
        QCOMPARE(m_indexer->state(), u"idle"_s);
        // No PDF kept open once read (Windows refuses to rename or replace an open file).
        QCOMPARE(pdfview::PdfReaderPool::instance().idleCount(), 0);
        // Page 3 of the lecture, diacritics optional.
        QList<AttachmentSearch::Hit> hits = m_indexer->search(u"zolw"_s, 10);
        const AttachmentSearch::Hit *lecture = hitFor(hits, Lecture);
        QVERIFY(lecture);
        QCOMPARE(lecture->kind, Kind::Pdf);
        QCOMPARE(lecture->page, 3);
        QCOMPARE(lecture->pageCount, 3);
        QVERIFY2(lecture->excerpt.contains(u"żółw"_s), qPrintable(lecture->excerpt));
        QCOMPARE(lecture->find, u"żółw"_s);
        QVERIFY(!lecture->ocr);
        // A phrase on page 1.
        hits = m_indexer->search(u"gesla jazn"_s, 10);
        QVERIFY(hitFor(hits, Lecture) && hitFor(hits, Lecture)->page == 1);
        // The scan and the picture: by their names only while OCR is off.
        QVERIFY(!hitFor(m_indexer->search(u"calka"_s, 10), Scan));
        hits = m_indexer->search(u"tablica"_s, 10);
        QVERIFY(hitFor(hits, Board) && hitFor(hits, Board)->nameOnly);
        QVERIFY(hitFor(m_indexer->search(u"skan"_s, 10), Scan));
        // Kinds.
        QVERIFY(!hitFor(m_indexer->search(u"zolw"_s, 10, attachments::Scope::Pictures), Lecture));
        QCOMPARE(m_indexer->indexedFiles(), 3);
        QCOMPARE(m_indexer->indexedPages(), 2); // (the lecture's pages 1 and 3: the slide has no text)
        // The page as the cache holds it.
        const std::optional<attachments::Page> page = m_indexer->page(Lecture, 3);
        QVERIFY(page && page->source == attachments::PageSource::TextLayer);
        QVERIFY(m_indexer->page(Lecture, 2)
                && m_indexer->page(Lecture, 2)->source == attachments::PageSource::NeedsOcr);
        // Records of the two PDFs, the manifest once the indexer goes.
        QCOMPARE(records(m_cache).size(), 2);
        m_indexer.reset();
        QVERIFY(QFileInfo::exists(m_manifest));
    }

    void reusedAfterRestart()
    {
        if (!platform::pdfTextAvailable())
            QSKIP("this build reads no PDF text");
        const QHash<QString, qint64> before = records(m_cache);
        QVERIFY(!before.isEmpty());
        QTest::qWait(20); // (a record written again would have a later time)
        openIndexer();
        QVERIFY(waitIdle());
        QVERIFY(hitFor(m_indexer->search(u"zolw"_s, 10), Lecture));
        QCOMPARE(records(m_cache), before); // nothing read again
    }

    void followsChanges()
    {
        if (!platform::pdfTextAvailable())
            QSKIP("this build reads no PDF text");
        QVERIFY(m_indexer);
        const QHash<QString, qint64> before = records(m_cache);
        QSignalSpy changed(m_indexer.get(), &AttachmentIndexer::contentChanged);
        // Renamed: the text follows, nothing read.
        const QString moved = u"Archiwum/Wykład 3.pdf"_s;
        QVERIFY(m_vault->createFolder(u"Archiwum"_s));
        QVERIFY(m_vault->rename(Lecture, moved));
        QVERIFY(waitFor([&] { return hitFor(m_indexer->search(u"zolw"_s, 10), moved) != nullptr; }));
        QVERIFY(!hitFor(m_indexer->search(u"zolw"_s, 10), Lecture));
        QVERIFY(waitIdle());
        QCOMPARE(records(m_cache), before);
        QVERIFY(changed.count() > 0 || changed.wait());
        // Changed: read again (another text).
        QVERIFY(m_vault->writeBinary(moved, bytesOf(data(u"attachments/skan.pdf"_s))));
        QVERIFY(waitFor([&] { return hitFor(m_indexer->search(u"zolw"_s, 10), moved) == nullptr; }));
        QVERIFY(waitIdle());
        // Removed: gone.
        QVERIFY(m_vault->remove(moved));
        QVERIFY(waitFor([&] { return !m_indexer->index().contains(moved); }));
        // Back as it was for the next tests.
        QVERIFY(m_vault->writeBinary(Lecture, bytesOf(data(u"pdf/wyklad.pdf"_s))));
        QVERIFY(waitFor([&] { return hitFor(m_indexer->search(u"zolw"_s, 10), Lecture) != nullptr; }));
        QVERIFY(waitIdle());
    }

    void turnedOff()
    {
        QVERIFY(m_indexer || platform::pdfTextAvailable() == false);
        if (!m_indexer)
            openIndexer();
        m_indexer->setEnabled(false);
        QCOMPARE(m_indexer->state(), u"off"_s);
        QVERIFY(m_indexer->search(u"tablica"_s, 10).isEmpty());
        QCOMPARE(m_indexer->indexedFiles(), 0);
        m_indexer->setEnabled(true);
        QVERIFY(waitIdle());
        QVERIFY(hitFor(m_indexer->search(u"tablica"_s, 10), Board));
    }

    void ocr()
    {
        if (!m_indexer)
            openIndexer();
        QVERIFY(waitProbed());
        if (!ocrHere())
            QSKIP("Tesseract with Polish and English models is not available here");
        QVERIFY(m_indexer->ocrAvailable());
        QCOMPARE(m_indexer->ocrLanguages(), u"pol+eng"_s);
        QVERIFY(m_indexer->ocrInstalledLanguages().contains(u"pol"_s));
        // Paused first: nothing is recognised.
        m_indexer->setPaused(true);
        QCOMPARE(m_indexer->state(), u"paused"_s);
        m_indexer->setOcrEnabled(true);
        QTest::qWait(1500 * BRULION_TEST_SLOWDOWN);
        QCOMPARE(m_indexer->pagesRecognized(), 0);
        QVERIFY(!hitFor(m_indexer->search(u"tablica zolw"_s, 10), Board)
                || hitFor(m_indexer->search(u"tablica zolw"_s, 10), Board)->nameOnly);
        m_indexer->setPaused(false);
        QVERIFY(waitIdle(300000));

        // The whiteboard by its words, OCR's, with highlights in the picture's own pixels.
        QList<AttachmentSearch::Hit> hits = m_indexer->search(u"powoli"_s, 10);
        const AttachmentSearch::Hit *board = hitFor(hits, Board);
        QVERIFY(board);
        QVERIFY(board->ocr);
        QCOMPARE(board->kind, Kind::Picture);
        QVERIFY2(board->excerpt.contains(u"powoli"_s), qPrintable(board->excerpt));
        const QVariantList rects = m_indexer->matchRects(Board, 1, u"zolw"_s);
        QVERIFY(!rects.isEmpty());
        const QVariantMap r = rects.first().toMap();
        QVERIFY(r.value(u"x"_s).toReal() >= 0 && r.value(u"y"_s).toReal() > 0.4 && r.value(u"width"_s).toReal() > 0.05);
        QVERIFY(m_indexer->recognizedText(Board).contains(u"Spotkanie"_s));
        QVERIFY(m_indexer->matchRects(Board, 1, u"nieobecne"_s).isEmpty());

        // The scanned page, and the PDF viewer's lookup of it.
        hits = m_indexer->search(u"calka oznaczona"_s, 10);
        const AttachmentSearch::Hit *scan = hitFor(hits, Scan);
        QVERIFY(scan);
        QVERIFY(scan->ocr);
        QCOMPARE(scan->page, 1);
        const std::optional<FileStat> st = m_vault->stat(Scan);
        QVERIFY(st);
        const QString file = m_vault->absolutePath(Scan);
        std::optional<attachments::Page> page = m_indexer->pageOfFile(file, st->size, st->mtime, 1);
        QVERIFY(page && page->source == attachments::PageSource::Ocr);
        QVERIFY(text::foldKey(page->text).contains(u"calka"_s));
        QVERIFY(!m_indexer->pageOfFile(file, st->size, st->mtime + 1, 1));
        const AttachmentIndexer::PageLookup lookup = m_indexer->pageLookup();
        QVERIFY(lookup(file, st->size, st->mtime, 1));
        // The lecture's slide (no text) was read too; its text pages kept theirs.
        QCOMPARE(m_indexer->page(Lecture, 3)->source, attachments::PageSource::TextLayer);
        QCOMPARE(m_indexer->page(Lecture, 2)->source, attachments::PageSource::Ocr);

        // Another OCR language: read again in it (only the OCR pages).
        m_indexer->setOcrLanguagesSetting(u"pol"_s);
        QCOMPARE(m_indexer->ocrLanguages(), u"pol"_s);
        QVERIFY(waitIdle(300000));
        QCOMPARE(m_indexer->page(Board, 1)->ocrLanguages, u"pol"_s);

        // Restart: the OCR text comes from the cache, nothing read again.
        const QHash<QString, qint64> before = records(m_cache);
        m_indexer.reset();
        QVERIFY(!lookup(file, st->size, st->mtime, 1)); // (the lookup outlives the indexer)
        openIndexer(true);
        m_indexer->setOcrLanguagesSetting(u"pol"_s);
        QVERIFY(waitProbed());
        QVERIFY(waitIdle(300000));
        QVERIFY(hitFor(m_indexer->search(u"powoli"_s, 10), Board));
        QCOMPARE(records(m_cache), before);
    }

    void sidewaysPhoto()
    {
        if (!m_indexer)
            openIndexer();
        QVERIFY(waitProbed());
        if (!ocrHere())
            QSKIP("Tesseract with Polish and English models is not available here");
        m_indexer->setOcrEnabled(true);
        const QString photo = u"zdjęcia/obrocone.jpg"_s;
        QVERIFY(m_vault->writeBinary(photo, bytesOf(data(u"attachments/obrocone.jpg"_s))));
        QVERIFY(waitFor([&] { return hitFor(m_indexer->search(u"pitagorasa"_s, 10), photo) != nullptr; }, 300000));
        // The first line of the upright picture lies along the file's left edge, bottom up: its box
        // in the file's own pixels (which the picture view shows) is tall and narrow, at the left.
        const QVariantList rects = m_indexer->matchRects(photo, 1, u"notatki"_s);
        QVERIFY2(!rects.isEmpty(), qPrintable(m_indexer->recognizedText(photo)));
        const QVariantMap r = rects.first().toMap();
        QVERIFY2(r.value(u"x"_s).toReal() < 0.3 && r.value(u"height"_s).toReal() > r.value(u"width"_s).toReal(),
                 qPrintable(u"%1 %2 %3 %4"_s.arg(r.value(u"x"_s).toReal())
                                    .arg(r.value(u"y"_s).toReal())
                                    .arg(r.value(u"width"_s).toReal())
                                    .arg(r.value(u"height"_s).toReal())));
    }

    void largePdf()
    {
        // docs/PERFORMANCE.md "Searching attachments": BRULION_ATTACHMENT_BENCH=1 tst_attachmentindexer largePdf
        if (qEnvironmentVariableIsEmpty("BRULION_ATTACHMENT_BENCH"))
            QSKIP("a measurement: BRULION_ATTACHMENT_BENCH=1");
        if (!platform::pdfTextAvailable())
            QSKIP("this build reads no PDF text");
        QTemporaryDir dir;
        const QString root = dir.filePath(u"Notatki"_s);
        QVERIFY(QDir().mkpath(root + u"/assets"_s));
        // A 1000-page text PDF (a semester's slides as text: ~120 words a page) and a 100 MB scan
        // (60 A4 pages, each a 150 dpi picture of noisy grey — what a scanner makes).
        QElapsedTimer t;
        t.start();
        {
            QPdfWriter writer(root + u"/assets/Skrypt.pdf"_s);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter p(&writer);
            QFont font;
            font.setPointSize(9);
            p.setFont(font);
            const QStringList words{u"całka"_s,  u"pochodna"_s,    u"macierz"_s, u"wyznacznik"_s,
                                    u"żółw"_s,   u"granica"_s,     u"funkcja"_s, u"ciąg"_s,
                                    u"szereg"_s, u"twierdzenie"_s, u"dowód"_s,   u"lemat"_s};
            QRandomGenerator rng(7);
            for (int page = 1; page <= 1000; ++page) {
                if (page > 1)
                    writer.newPage();
                for (int line = 0; line < 12; ++line) {
                    QString text = u"Strona %1:"_s.arg(page);
                    for (int w = 0; w < 10; ++w)
                        text += u' ' + words.at(rng.bounded(int(words.size()))) + QString::number(rng.bounded(50));
                    p.drawText(QPointF(40, 60 + line * 40), text);
                }
            }
        }
        {
            QPdfWriter writer(root + u"/assets/Skan.pdf"_s);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter p(&writer);
            QImage scan(1240, 1754, QImage::Format_Grayscale8);
            QRandomGenerator rng(11);
            QFont font;
            font.setPixelSize(30);
            for (int page = 1; page <= 60; ++page) {
                if (page > 1)
                    writer.newPage();
                for (int y = 0; y < scan.height(); ++y) {
                    uchar *line = scan.scanLine(y);
                    for (int x = 0; x < scan.width(); ++x)
                        line[x] = uchar(200 + rng.bounded(56));
                }
                QPainter onScan(&scan);
                onScan.setFont(font);
                onScan.setPen(Qt::black);
                for (int row = 0; row < 24; ++row)
                    onScan.drawText(
                            QPointF(90, 120 + row * 64),
                            u"Wykład %1, akapit %2: całka oznaczona funkcji ciągłej na przedziale"_s.arg(page).arg(
                                    row));
                onScan.end();
                p.drawImage(QRectF(0, 0, 595.28, 841.89), scan);
            }
        }
        const qint64 textBytes = QFileInfo(root + u"/assets/Skrypt.pdf"_s).size();
        const qint64 scanBytes = QFileInfo(root + u"/assets/Skan.pdf"_s).size();
        qInfo("written in %lld ms: Skrypt.pdf %.1f MB (1000 pages), Skan.pdf %.1f MB (60 pages)", qint64(t.elapsed()),
              textBytes / 1e6, scanBytes / 1e6);

        Vault vault(root);
        QVERIFY(vault.open());
        AttachmentIndexer indexer;
        indexer.setUiLanguage(u"pl"_s);
        indexer.attach(&vault, dir.filePath(u"cache"_s), dir.filePath(u"cache/notebooks/x.manifest"_s));
        resetPeak();
        const qint64 before = peakKib();
        t.start();
        indexer.start(0);
        QVERIFY(waitFor([&] { return indexer.idle() && indexer.indexedFiles() == 2; }, 600000));
        const qint64 cold = t.elapsed();
        qInfo("text layers read and indexed: %lld ms (%.0f pages/s), peak RSS +%lld MiB, %d pages with text, cache %s",
              qint64(cold), 1060.0 * 1000 / std::max<qint64>(1, cold), (peakKib() - before) / 1024,
              indexer.indexedPages(), qPrintable(indexer.cacheSizeText()));
        // Searching it.
        QList<qint64> samples;
        for (const QString &q : {u"zolw"_s, u"calka pochodna"_s, u"wyzn"_s, u"twierdzenei"_s, u"strona 777"_s}) {
            QElapsedTimer s;
            s.start();
            const QList<AttachmentSearch::Hit> hits = indexer.search(q, 100);
            samples.append(s.nsecsElapsed() / 1000);
            QVERIFY(!hits.isEmpty());
        }
        qInfo("search (with excerpts): median %lld µs over 5 queries", brulion::test::medianOf(samples));
        // What the GUI thread does per event-loop turn: 24 pages merged (PagesPerPost).
        {
            QByteArray recordBytes;
            {
                QFile pdfFile(root + u"/assets/Skrypt.pdf"_s);
                QVERIFY(pdfFile.open(QIODevice::ReadOnly));
                QFile record(dir.filePath(u"cache"_s) + u'/'
                             + attachments::recordFileName(*attachments::hashFile(pdfFile)));
                QVERIFY(record.open(QIODevice::ReadOnly));
                recordBytes = record.readAll();
            }
            const std::optional<attachments::Record> record = attachments::decodeRecord(recordBytes);
            QVERIFY(record);
            AttachmentSearch::Prepared prepared = AttachmentSearch::prepare(u"assets/Skrypt.pdf"_s, *record);
            AttachmentSearch index;
            AttachmentSearch::Prepared first = prepared;
            first.pages.resize(24);
            QList<qint64> turns;
            QElapsedTimer turn;
            turn.start();
            index.set(std::move(first));
            turns.append(turn.nsecsElapsed() / 1000);
            for (qsizetype i = 24; i < prepared.pages.size(); i += 24) {
                turn.restart();
                for (qsizetype k = i; k < std::min<qsizetype>(i + 24, prepared.pages.size()); ++k)
                    index.setPage(u"assets/Skrypt.pdf"_s, prepared.pages.at(k));
                turns.append(turn.nsecsElapsed() / 1000);
            }
            qInfo("GUI thread per turn (24 pages merged): median %lld µs, longest %lld µs",
                  brulion::test::medianOf(turns), *std::ranges::max_element(turns));
        }
        // Again from the cache (a restart).
        indexer.detach();
        AttachmentIndexer again;
        again.attach(&vault, dir.filePath(u"cache"_s), dir.filePath(u"cache/notebooks/x.manifest"_s));
        t.start();
        again.start(0);
        QVERIFY(waitFor([&] { return again.idle() && again.indexedFiles() == 2; }, 600000));
        qInfo("from the cache after a restart: %lld ms", qint64(t.elapsed()));
        // OCR of a few scanned pages.
        QVERIFY(waitFor([&] { return again.ocrUnavailableReason() != u"probing"_s; }, 30000));
        if (ocrHere()) {
            again.setOcrEnabled(true);
            resetPeak();
            const qint64 base = peakKib();
            t.start();
            QVERIFY(waitFor([&] { return again.pagesRecognized() >= 10; }, 600000));
            qInfo("OCR (pol+eng, one thread): %.0f ms a page over 10 noisy A4 pages at 300 dpi, peak RSS +%lld MiB",
                  double(t.elapsed()) / 10, (peakKib() - base) / 1024);
            again.setPaused(true);
        }
        again.detach();
    }

    void clearCache()
    {
        if (!m_indexer)
            openIndexer();
        QVERIFY(waitIdle(300000));
        m_indexer->clearCache();
        QVERIFY(waitIdle(300000));
        QVERIFY(hitFor(m_indexer->search(u"tablica"_s, 10), Board));
        if (platform::pdfTextAvailable())
            QVERIFY(hitFor(m_indexer->search(u"zolw"_s, 10), Lecture));
        m_indexer->detach();
        QCOMPARE(m_indexer->indexedFiles(), 0);
        QVERIFY(m_indexer->search(u"tablica"_s, 10).isEmpty());
    }
};

QTEST_MAIN(TestAttachmentIndexer)
#include "tst_attachmentindexer.moc"
