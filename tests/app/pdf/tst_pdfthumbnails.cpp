// The thumbnails of embedded PDFs (src/app/pdf/pdfthumbnails.h) with an injected renderer that counts
// its calls and a temporary cache folder: cache keys, Loading → changed → Ready with the PNG's text
// chunks read back, a second instance on the same folder ready at once without drawing, a new mtime a
// new key, deterministic failures cached as .err (not drawn again; a failed drawing only in memory), a
// truncated or pruned PNG drawn again, pruning to the cap,
// an unwritable folder, no renderer, many asks for one key drawn once; one real case with this
// build's renderer.
#include "pdfthumbnails.h"

#include "pdfpages.h"
#include "pdfrenderpool.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <atomic>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::pdfview::PdfRenderPool;
using brulion::pdfview::PdfThumbnails;
using State = PdfThumbnails::State;

namespace {

/// A renderer drawing a page-sized grey picture (or failing with `error`), counting its calls.
struct FakeRenderer {
    std::shared_ptr<std::atomic_int> calls = std::make_shared<std::atomic_int>(0);
    QString error;
    int pages = 12;
    bool noise = false; ///< pictures that do not compress (pruning)

    PdfThumbnails::Renderer make() const
    {
        const auto counter = calls;
        const QString failure = error;
        const int count = pages;
        const bool random = noise;
        return [counter, failure, count, random](const QString &, const QString &, int, int longSide) {
            ++*counter;
            QThread::msleep(20); // (long enough for the asks of concurrentAsks to overlap)
            PdfThumbnails::Rendered r;
            r.pages = count;
            r.pagePoints = QSizeF(595.5, 842);
            if (!failure.isEmpty()) {
                r.error = failure;
                return r;
            }
            r.image = QImage(longSide * 595 / 842, longSide, QImage::Format_RGB32);
            r.image.fill(Qt::lightGray);
            if (random) {
                for (int y = 0; y < r.image.height(); ++y) {
                    auto *line = reinterpret_cast<quint32 *>(r.image.scanLine(y));
                    for (int x = 0; x < r.image.width(); ++x)
                        line[x] = QRandomGenerator::global()->generate() | 0xff000000u;
                }
            }
            return r;
        };
    }
};

void waitForPool()
{
    if (QThreadPool *pool = PdfRenderPool::pool())
        pool->waitForDone(10000);
    QCoreApplication::processEvents();
}

} // namespace

class TestPdfThumbnails : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cacheKey_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<qint64>("size");
        QTest::addColumn<qint64>("mtime");
        QTest::addColumn<int>("page");
        QTest::addColumn<int>("side");
        QTest::addColumn<bool>("same");
        const QString base = u"/notatnik/assets/Wykład ąę.pdf"_s;
        QTest::newRow("same inputs") << base << qint64(1000) << qint64(5) << 1 << 768 << true;
        QTest::newRow("another file") << u"/notatnik/assets/Wykład ąę 2.pdf"_s << qint64(1000) << qint64(5) << 1 << 768
                                      << false;
        QTest::newRow("another size") << base << qint64(1001) << qint64(5) << 1 << 768 << false;
        QTest::newRow("another mtime") << base << qint64(1000) << qint64(6) << 1 << 768 << false;
        QTest::newRow("another page") << base << qint64(1000) << qint64(5) << 2 << 768 << false;
        QTest::newRow("another side") << base << qint64(1000) << qint64(5) << 1 << 1024 << false;
        // NFD spelling of the same name: another key (the vault normalises to NFC first).
        QTest::newRow("NFD name") << u"/notatnik/assets/Wykład a\u0328e\u0328.pdf"_s << qint64(1000) << qint64(5) << 1
                                  << 768 << false;
    }

    void cacheKey()
    {
        QFETCH(QString, file);
        QFETCH(qint64, size);
        QFETCH(qint64, mtime);
        QFETCH(int, page);
        QFETCH(int, side);
        QFETCH(bool, same);
        const QString a = PdfThumbnails::cacheKey(u"/notatnik/assets/Wykład ąę.pdf"_s, 1000, 5, 1, 768);
        const QString b = PdfThumbnails::cacheKey(file, size, mtime, page, side);
        QCOMPARE(a.size(), 40); // SHA-1 hex
        QCOMPARE(a == b, same);
    }

    void longSides()
    {
        QCOMPARE(PdfThumbnails::longSideForRatio(1), 512);
        QCOMPARE(PdfThumbnails::longSideForRatio(1.5), 768);
        QCOMPARE(PdfThumbnails::longSideForRatio(2), 1024);
        QCOMPARE(PdfThumbnails::longSideForRatio(3), 1280);
        QCOMPARE(PdfThumbnails::longSideForRatio(3.5), 1536);
        QCOMPARE(PdfThumbnails::longSideForRatio(0), 512);
        QCOMPARE(PdfThumbnails::longSideForRatio(qQNaN()), 512);
    }

    void readyAndCachedOnDisk()
    {
        QTemporaryDir dir;
        FakeRenderer renderer;
        const QString pdf = u"/notatnik/Wykład.pdf"_s;
        {
            PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
            QSignalSpy changed(&thumbs, &PdfThumbnails::changed);
            QCOMPARE(thumbs.entry(pdf, 1000, 5, 1).state, State::Loading);
            QTRY_COMPARE(changed.size(), 1);
            QCOMPARE(changed.first().first().toString(), pdf);
            const PdfThumbnails::Entry e = thumbs.entry(pdf, 1000, 5, 1);
            QCOMPARE(e.state, State::Ready);
            QVERIFY(QFileInfo(e.file).isFile());
            QCOMPARE(e.pixels.height(), PdfThumbnails::DefaultLongSide);
            QCOMPARE(e.pages, 12);
            QVERIFY(qAbs(e.pagePoints.width() - 595.5) < 0.01);
            // The text chunks, read without decoding.
            QImageReader reader(e.file);
            QCOMPARE(reader.text(u"Brulion-Pages"_s), u"12"_s);
            QCOMPARE(reader.text(u"Brulion-PageHeight"_s), u"842"_s);
            QCOMPARE(renderer.calls->load(), 1);
        }
        // A new instance (a restart): ready at once, nothing drawn.
        PdfThumbnails again(dir.path(), nullptr, renderer.make());
        const PdfThumbnails::Entry e = again.entry(pdf, 1000, 5, 1);
        QCOMPARE(e.state, State::Ready);
        QCOMPARE(e.pages, 12);
        QCOMPARE(renderer.calls->load(), 1);
        // The file changed (another mtime): drawn again.
        QCOMPARE(again.entry(pdf, 1000, 6, 1).state, State::Loading);
        waitForPool();
        QTRY_COMPARE(again.entry(pdf, 1000, 6, 1).state, State::Ready);
        QCOMPARE(renderer.calls->load(), 2);
    }

    void failuresCached_data()
    {
        QTest::addColumn<QString>("error");
        QTest::addColumn<bool>("persisted");
        QTest::newRow("locked") << u"locked"_s << true;
        QTest::newRow("unreadable") << u"unreadable"_s << true;
        QTest::newRow("no pages") << u"nopages"_s << true;
        // A failed drawing (out of memory, a lost Android session): tried again after a restart.
        QTest::newRow("render") << u"render"_s << false;
    }

    void failuresCached()
    {
        QFETCH(QString, error);
        QFETCH(bool, persisted);
        QTemporaryDir dir;
        FakeRenderer renderer;
        renderer.error = error;
        renderer.pages = 0;
        const QString pdf = u"/notatnik/Sejf źdźbło.pdf"_s;
        {
            PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
            QSignalSpy changed(&thumbs, &PdfThumbnails::changed);
            thumbs.entry(pdf, 10, 1, 1);
            QTRY_COMPARE(changed.size(), 1);
            const PdfThumbnails::Entry e = thumbs.entry(pdf, 10, 1, 1);
            QCOMPARE(e.state, State::Failed);
            QCOMPARE(e.error, error);
            QCOMPARE(QDir(dir.path()).entryList({u"*.err"_s}).size(), persisted ? 1 : 0);
            // Remembered in memory either way: not drawn again on every scroll.
            QCOMPARE(thumbs.entry(pdf, 10, 1, 1).state, State::Failed);
            QCOMPARE(renderer.calls->load(), 1);
        }
        PdfThumbnails again(dir.path(), nullptr, renderer.make());
        const PdfThumbnails::Entry e = again.entry(pdf, 10, 1, 1);
        if (persisted) {
            QCOMPARE(e.state, State::Failed);
            QCOMPARE(e.error, error);
            QCOMPARE(renderer.calls->load(), 1);
        } else {
            QCOMPARE(e.state, State::Loading);
            waitForPool();
            QTRY_COMPARE(again.entry(pdf, 10, 1, 1).state, State::Failed);
            QCOMPARE(renderer.calls->load(), 2);
        }
    }

    void prunedPngDrawnAgain()
    {
        QTemporaryDir dir;
        FakeRenderer renderer;
        const QString pdf = u"/notatnik/Wykład żółw.pdf"_s;
        PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
        thumbs.entry(pdf, 3, 3, 1);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(pdf, 3, 3, 1).state, State::Ready);
        const QString png = thumbs.entry(pdf, 3, 3, 1).file;
        QVERIFY(QFile::remove(png)); // what the pruning does to the oldest
        // The memory entry is not trusted any more: drawn again, never a URL of a missing file.
        QCOMPARE(thumbs.entry(pdf, 3, 3, 1).state, State::Loading);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(pdf, 3, 3, 1).state, State::Ready);
        QVERIFY(QFileInfo(thumbs.entry(pdf, 3, 3, 1).file).isFile());
        QCOMPARE(renderer.calls->load(), 2);
    }

    void truncatedPngDrawnAgain()
    {
        QTemporaryDir dir;
        FakeRenderer renderer;
        const QString pdf = u"/notatnik/Ćwiczenia.pdf"_s;
        const QString key = PdfThumbnails::cacheKey(pdf, 7, 7, 1, PdfThumbnails::DefaultLongSide);
        {
            QFile f(QDir(dir.path()).filePath(key + u".png"_s));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("\x89PNG\r\n"); // the start of a header only
        }
        PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
        QCOMPARE(thumbs.entry(pdf, 7, 7, 1).state, State::Loading);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(pdf, 7, 7, 1).state, State::Ready);
        QCOMPARE(renderer.calls->load(), 1);
    }

    void pruning()
    {
        QTemporaryDir dir;
        FakeRenderer renderer;
        renderer.noise = true;
        PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
        thumbs.setLongSide(256);
        thumbs.setCacheBytes(1024 * 1024);
        QSignalSpy changed(&thumbs, &PdfThumbnails::changed);
        for (int i = 0; i < 40; ++i)
            thumbs.entry(u"/notatnik/plik %1.pdf"_s.arg(i), 100, i, 1);
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 40, 20000);
        const auto total = [&dir] {
            qint64 sum = 0;
            for (const QFileInfo &f : QDir(dir.path()).entryInfoList(QDir::Files))
                sum += f.size();
            return sum;
        };
        QVERIFY(total() > 1024 * 1024);
        // 2 s after the last store, on the pool: the oldest go, the newest stay.
        QTRY_VERIFY_WITH_TIMEOUT(total() <= 1024 * 1024, 10000);
        QCOMPARE(thumbs.entry(u"/notatnik/plik 39.pdf"_s, 100, 39, 1).state, State::Ready);
        // The pruned ones are no Ready entries with a missing PNG.
        for (int i = 0; i < 40; ++i) {
            const PdfThumbnails::Entry e = thumbs.entry(u"/notatnik/plik %1.pdf"_s.arg(i), 100, i, 1);
            QVERIFY2(e.state != State::Ready || QFileInfo(e.file).isFile(), qPrintable(QString::number(i)));
        }
        waitForPool();
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).size() > 0);
    }

    void unwritableFolder()
    {
        QTemporaryDir dir;
        // A file where the folder should be: nothing can be written there.
        const QString blocked = dir.filePath(u"plik"_s);
        {
            QFile f(blocked);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        FakeRenderer renderer;
        PdfThumbnails thumbs(blocked + u"/pdf-thumbnails"_s, nullptr, renderer.make());
        thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).state, State::Failed);
        QCOMPARE(thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).error, u"cache"_s);
    }

    void inMemoryOnly()
    {
        FakeRenderer renderer;
        PdfThumbnails thumbs(QString(), nullptr, renderer.make());
        thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).state, State::Ready);
        QVERIFY(thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).file.isEmpty());
    }

    void unsupported()
    {
        FakeRenderer renderer;
        renderer.error = u"unsupported"_s;
        PdfThumbnails thumbs(QString(), nullptr, renderer.make());
        thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).state, State::Unsupported);
        // The default renderer where the build has none: at once.
        if (!brulion::platform::pdfAvailable()) {
            PdfThumbnails plain{QString()};
            QCOMPARE(plain.entry(u"/notatnik/a.pdf"_s, 1, 1, 1).state, State::Unsupported);
        }
        QCOMPARE(thumbs.entry(QString(), 1, 1, 1).state, State::Failed);
    }

    void concurrentAsks()
    {
        QTemporaryDir dir;
        FakeRenderer renderer;
        PdfThumbnails thumbs(dir.path(), nullptr, renderer.make());
        for (int i = 0; i < 10; ++i)
            QCOMPARE(thumbs.entry(u"/notatnik/jeden.pdf"_s, 5, 5, 3).state, State::Loading);
        QCOMPARE(thumbs.pendingCount(), 1);
        waitForPool();
        QTRY_COMPARE(thumbs.entry(u"/notatnik/jeden.pdf"_s, 5, 5, 3).state, State::Ready);
        QCOMPARE(renderer.calls->load(), 1);
    }

    void realRenderer()
    {
        if (!brulion::platform::pdfAvailable())
            QSKIP("no PDF renderer in this build");
        QTemporaryDir dir;
        const QString pdf = dir.filePath(u"Wykład – ćwiczenia.pdf"_s);
        {
            QPdfWriter writer(pdf);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter p(&writer);
            p.fillRect(QRectF(100, 100, 200, 200), Qt::red);
        }
        const QFileInfo info(pdf);
        PdfThumbnails thumbs(dir.filePath(u"cache"_s));
        thumbs.setLongSide(512);
        QSignalSpy changed(&thumbs, &PdfThumbnails::changed);
        thumbs.entry(pdf, info.size(), info.lastModified().toMSecsSinceEpoch(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(changed.size(), 1, 20000);
        const PdfThumbnails::Entry e = thumbs.entry(pdf, info.size(), info.lastModified().toMSecsSinceEpoch(), 1);
        QCOMPARE(e.state, State::Ready);
        QCOMPARE(e.pages, 1);
        QCOMPARE(e.pixels.height(), 512);
        const QImage image(e.file);
        QVERIFY(!image.isNull());
        // White page, red square at (100..300 of 595 pt) × (100..300 of 842 pt).
        QCOMPARE(image.pixelColor(5, 5), QColor(Qt::white));
        const QColor red = image.pixelColor(image.width() * 200 / 595, image.height() * 200 / 842);
        QVERIFY2(red.red() > 200 && red.green() < 60, qPrintable(red.name()));
        // A page beyond the end draws page 1; a file that is no PDF fails.
        const QString broken = dir.filePath(u"zepsuty.pdf"_s);
        {
            QFile f(broken);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("<html>Zażółć</html>");
        }
        thumbs.entry(broken, 20, 1, 1);
        QTRY_COMPARE_WITH_TIMEOUT(thumbs.entry(broken, 20, 1, 1).state, State::Failed, 20000);
        QCOMPARE(thumbs.entry(broken, 20, 1, 1).error, u"unreadable"_s);
        thumbs.entry(pdf, info.size(), info.lastModified().toMSecsSinceEpoch(), 999);
        QTRY_COMPARE_WITH_TIMEOUT(thumbs.entry(pdf, info.size(), info.lastModified().toMSecsSinceEpoch(), 999).state,
                                  State::Ready, 20000);
    }
};

QTEST_MAIN(TestPdfThumbnails)
#include "tst_pdfthumbnails.moc"
