// The PDF viewer's state (src/app/pdf/pdfviewmodel.h) with this build's renderer on tests/data/pdf:
// opening (Loading → Ready, three pages), a locked and a missing file, the visible pages, zoom around
// a point, detail pictures only when zoomed in, search without diacritics and case (a turtle on page
// 3), a new text cancelling the old search, a PDF without text, the search turned off, a new version
// opened at the same page, a page that failed; and the image provider's ids (pure).
#include "pdfviewmodel.h"

#include "pdfpages.h"
#include "pdfrenderpool.h"
#include "pdfviewimageprovider.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <cmath>
#include <vector>

using namespace Qt::StringLiterals;
using brulion::pdfview::PdfReaderPool;
using brulion::pdfview::PdfViewImageProvider;
using brulion::pdfview::PdfViewModel;
using brulion::pdfview::PdfVisiblePages;
using Status = PdfViewModel::Status;

namespace {

QString fixture(const QString &name)
{
    return QDir(QStringLiteral(BRULION_TEST_DATA)).filePath(u"pdf/"_s + name);
}

/// A model on `file` laid out in a `w` × `h` view, ready (or failed).
std::unique_ptr<PdfViewModel> opened(const QString &file, qreal w = 800, qreal h = 600, qreal maxFit = 1e6)
{
    auto model = std::make_unique<PdfViewModel>();
    model->setViewportWidth(w);
    model->setViewportHeight(h);
    model->setMargin(24);
    model->setGap(12);
    model->setMaxFitWidth(maxFit);
    model->setFile(file);
    model->setVersion(u"1"_s);
    return model;
}

/// A one-page PDF of a filled rectangle: no text (a scan).
QByteArray textlessPdf()
{
    const QByteArray content = "0 0 1 rg 100 100 300 300 re f";
    const QList<QByteArray> objects{"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R >>",
                                    "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content
                                            + "\nendstream"};
    QByteArray pdf = "%PDF-1.4\n";
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(pdf.size());
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
    }
    const qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const qsizetype o : std::as_const(offsets))
        pdf += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
           + QByteArray::number(xref) + "\n%%EOF\n";
    return pdf;
}

} // namespace

class TestPdfViewModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        const QByteArray name = QTest::currentTestFunction();
        if (!brulion::platform::pdfAvailable() && !name.startsWith("provider"))
            QSKIP("no PDF renderer in this build");
    }

    void cleanup()
    {
        if (QThreadPool *pool = brulion::pdfview::PdfRenderPool::pool())
            pool->waitForDone(10000);
    }

    void opens()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QCOMPARE(model->status(), Status::Loading);
        QTRY_COMPARE(model->status(), Status::Ready);
        QCOMPARE(model->pageCount(), 3);
        QCOMPARE(model->totalPages(), 3);
        QVERIFY(model->fitWidth());
        QCOMPARE(model->currentPage(), 1);
        // Fit width: the widest page (the landscape slide, once its size arrived) as wide as the view.
        QTRY_VERIFY(std::abs(model->pageRect(2).width() - (800 - 48)) < 0.5);
        QVERIFY(model->pageRect(1).width() < model->pageRect(2).width());
        QVERIFY(model->contentHeight() > model->pageRect(3).bottom());
        QVERIFY(model->pageUrl(1).toString().startsWith(u"image://pdfview/"_s));
        QVERIFY(model->pageUrl(1).toString().contains(u"page=1"_s));
        QVERIFY(model->pageUrl(4).isEmpty());
    }

    void locked()
    {
        auto model = opened(fixture(u"zaszyfrowany.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Failed);
        QVERIFY(model->locked());
        QVERIFY2(model->error().contains(u"password"_s), qPrintable(model->error()));
        QCOMPARE(model->pageCount(), 0);
    }

    void missing()
    {
        QTemporaryDir dir;
        auto model = opened(dir.filePath(u"nie ma – żółw.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Failed);
        QVERIFY(!model->locked());
        QVERIFY2(model->error().contains(u"no longer"_s), qPrintable(model->error()));
        // Not a PDF at all.
        const QString html = dir.filePath(u"strona.pdf"_s);
        QFile f(html);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<!doctype html><p>Zażółć</p>");
        f.close();
        model->setFile(html);
        QTRY_VERIFY2(model->error().contains(u"cannot be read"_s), qPrintable(model->error()));
        QCOMPARE(model->status(), Status::Failed);
    }

    void visiblePages()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s), 800, 800);
        QTRY_COMPARE(model->status(), Status::Ready);
        QAbstractListModel *pages = model->pages();
        QVERIFY(pages->rowCount() >= 1);
        QCOMPARE(pages->data(pages->index(0), PdfVisiblePages::PageRole).toInt(), 1);
        const QHash<int, QByteArray> roles = pages->roleNames();
        QCOMPARE(roles.value(PdfVisiblePages::BaseWidthRole), QByteArray("baseWidth"));
        // At the end: the last page in the rows, the first one gone.
        model->setViewport(0, model->contentHeight() - 800);
        const int rows = pages->rowCount();
        QCOMPARE(pages->data(pages->index(rows - 1), PdfVisiblePages::PageRole).toInt(), 3);
        QVERIFY(pages->data(pages->index(0), PdfVisiblePages::PageRole).toInt() > 1);
        QCOMPARE(model->currentPage(), 3);
        // The base picture never larger than a screenful.
        for (int row = 0; row < rows; ++row) {
            const qreal w = pages->data(pages->index(row), PdfVisiblePages::BaseWidthRole).toReal();
            const qreal h = pages->data(pages->index(row), PdfVisiblePages::BaseHeightRole).toReal();
            QVERIFY(w * h <= 800 * 800 * 1.01 + 2 * (w + h));
        }
    }

    void zoomKeepsAnchor()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s), 800, 600);
        QTRY_COMPARE(model->status(), Status::Ready);
        model->setViewport(0, 100);
        const QPointF centre(400, 300);
        const QRectF before = model->pageRect(1);
        const QPointF content(0 + centre.x(), 100 + centre.y());
        const qreal fx = (content.x() - before.x()) / before.width();
        const qreal fy = (content.y() - before.y()) / before.height();
        const int percent = model->zoomPercent();
        const QPointF at = model->zoomAround(2, centre);
        QVERIFY(!model->fitWidth());
        QVERIFY(model->zoomPercent() > percent * 1.9);
        QVERIFY(model->contentWidth() > 800);
        const QRectF after = model->pageRect(1);
        QVERIFY(std::abs(at.x() + centre.x() - (after.x() + fx * after.width())) < 0.5);
        QVERIFY(std::abs(at.y() + centre.y() - (after.y() + fy * after.height())) < 0.5);
        // Back to fit width.
        model->setViewport(at.x(), at.y());
        model->fitToWidth(centre);
        QVERIFY(model->fitWidth());
        QCOMPARE(model->zoomPercent(), percent);
        // Presets and limits.
        model->setScaleAround(1.0 * 96 / 72, centre);
        QCOMPARE(model->zoomPercent(), 100);
        model->zoomAround(1000, centre);
        QCOMPARE(model->zoomPercent(), 800);
    }

    void detailOnlyZoomedIn()
    {
        // A page that fits into the view: its base picture has its full resolution.
        auto model = opened(fixture(u"wyklad.pdf"_s), 800, 1200, 500);
        QTRY_COMPARE(model->status(), Status::Ready);
        model->settle();
        QVERIFY(model->detail(1).isEmpty());
        // 400 %: after the view settled, the visible part at full resolution.
        model->setScaleAround(4.0 * 96 / 72, QPointF(400, 600));
        QVERIFY(model->detail(1).isEmpty()); // (not settled at this zoom yet)
        QSignalSpy detail(model.get(), &PdfViewModel::detailChanged);
        QTRY_VERIFY(detail.size() >= 1); // 150 ms after the last move
        int withDetail = 0;
        for (int page = 1; page <= 3; ++page) {
            const QVariantMap d = model->detail(page);
            if (d.isEmpty())
                continue;
            ++withDetail;
            QVERIFY(d.value(u"url"_s).toUrl().toString().contains(u"clip="_s));
            // The visible part, 64 around it, snapped to 64 on each side.
            QVERIFY(d.value(u"width"_s).toReal() <= 800 + 256);
            QVERIFY(d.value(u"height"_s).toReal() <= 1200 + 256);
        }
        QVERIFY(withDetail >= 1);
    }

    void search()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        if (!brulion::platform::pdfTextAvailable())
            QSKIP("no PDF text in this build");
        QVERIFY(model->searchAvailable());
        QSignalSpy moved(model.get(), &PdfViewModel::moveViewport);
        model->setSearchText(u"ŻÓŁW"_s);
        QTRY_VERIFY(!model->searching() && model->searchedPages() == 3);
        QCOMPARE(model->hitCount(), 1);
        QCOMPARE(model->currentHit(), 0);
        QVERIFY(!model->hitRects(3).isEmpty());
        QVERIFY(model->hitRects(1).isEmpty());
        QVERIFY(model->hitRects(3).first().toMap().value(u"current"_s).toBool());
        // Shown at once: the view moved to page 3.
        QVERIFY(!moved.isEmpty());
        QCOMPARE(model->currentPage(), 3);
        const QPointF next = model->nextHit();
        QVERIFY(std::isfinite(next.y()));
        QVERIFY(next.y() + 600 > model->pageRect(3).top());
        model->setSearchText(u"gesla"_s);
        QTRY_VERIFY(!model->searching() && model->searchedPages() == 3 && model->hitCount() == 1);
        QVERIFY(!model->hitRects(1).isEmpty());
        QVERIFY(!model->textless());
        model->setSearchText(u"nie ma takiego słowa"_s);
        QTRY_VERIFY(!model->searching() && model->searchedPages() == 3);
        QCOMPARE(model->hitCount(), 0);
        QVERIFY(std::isnan(model->nextHit().y()));
        model->clearSearch();
        QCOMPARE(model->searchText(), QString());
        QCOMPARE(model->hitCount(), 0);
    }

    void newTextCancels()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        if (!brulion::platform::pdfTextAvailable())
            QSKIP("no PDF text in this build");
        model->setSearchText(u"gesla"_s);
        QTest::qWait(PdfViewModel::SearchDebounceMs + 20);
        model->setSearchText(u"zolw"_s);
        QTRY_VERIFY(!model->searching() && model->searchedPages() == 3 && model->searchText() == u"zolw"_s);
        QTest::qWait(100);
        QCOMPARE(model->hitCount(), 1);
        QVERIFY(model->hitRects(1).isEmpty()); // no "gęślą" of the first text arrived
    }

    void textless()
    {
        if (!brulion::platform::pdfTextAvailable())
            QSKIP("no PDF text in this build");
        QTemporaryDir dir;
        const QString path = dir.filePath(u"skan.pdf"_s);
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(textlessPdf());
        f.close();
        auto model = opened(path);
        QTRY_COMPARE(model->status(), Status::Ready);
        model->setSearchText(u"żółw"_s);
        QTRY_VERIFY(!model->searching() && model->searchedPages() == 1);
        QVERIFY(model->textless());
        QCOMPARE(model->hitCount(), 0);
    }

    void searchTurnedOff()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        model->setSearchAvailable(false);
        model->setSearchText(u"zolw"_s);
        QTest::qWait(PdfViewModel::SearchDebounceMs + 100);
        QVERIFY(!model->searching());
        QCOMPARE(model->hitCount(), 0);
    }

    void currentPageAtTheEnd()
    {
        // A tall view: the last pages cannot reach its top; at the end the last one is current,
        // a jump to a page names it while the view stays.
        auto model = opened(fixture(u"wyklad.pdf"_s), 400, 900);
        QTRY_COMPARE(model->status(), Status::Ready);
        QTRY_VERIFY(std::abs(model->pageRect(2).width() - (400 - 48)) < 0.5);
        const qreal end = model->contentHeight() - 900;
        QVERIFY(end > 0);
        model->setViewport(0, end);
        QCOMPARE(model->currentPage(), 3);
        const QPointF two = model->positionForPage(2);
        model->setViewport(two.x(), two.y());
        model->pinPage(2);
        QCOMPARE(model->currentPage(), 2);
        model->setViewport(0, two.y() - 40); // scrolled: the pin goes
        QVERIFY(model->currentPage() != 2 || model->pageRect(2).top() <= two.y() - 40 + 0.4 * 900);
        model->pinPage(99);
        QCOMPARE(model->currentPage(), 3);
    }

    void newVersionKeepsPage()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        QTRY_VERIFY(std::abs(model->pageRect(2).width() - (800 - 48)) < 0.5); // (the sizes arrived)
        const QPointF third = model->positionForPage(3);
        model->setViewport(third.x(), third.y());
        QCOMPARE(model->currentPage(), 3);
        QSignalSpy moved(model.get(), &PdfViewModel::moveViewport);
        model->setVersion(u"2"_s);
        QTRY_VERIFY(!moved.isEmpty());
        QTRY_COMPARE(model->status(), Status::Ready);
        QCOMPARE(model->currentPage(), 3);
        QVERIFY(moved.last().at(1).toReal() > model->pageRect(2).top());
    }

    void failedPage()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s), 800, 800);
        QTRY_COMPARE(model->status(), Status::Ready);
        QAbstractListModel *pages = model->pages();
        QSignalSpy changed(pages, &QAbstractItemModel::dataChanged);
        PdfReaderPool::instance().reportFailure(fixture(u"wyklad.pdf"_s), u"1"_s, 1);
        QTRY_VERIFY(pages->data(pages->index(0), PdfVisiblePages::FailedRole).toBool());
        // Another file's or version's failure is not this one's.
        PdfReaderPool::instance().reportFailure(fixture(u"wyklad.pdf"_s), u"stara"_s, 2);
        QTest::qWait(20);
        QVERIFY(!pages->data(pages->index(1), PdfVisiblePages::FailedRole).toBool());
    }

    void timingsOfALongPdf()
    {
        // docs/PERFORMANCE.md "PDF viewer": a 300-page lecture (text on every page) opened (Ready),
        // its sizes all known, the first page drawn at a phone's fit width (1080 px), a thumbnail
        // (768 px). Informative: the times are printed; only gross regressions fail.
        QTemporaryDir dir;
        const QString path = dir.filePath(u"długi wykład.pdf"_s);
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter p(&writer);
            QFont font;
            font.setPointSize(14);
            p.setFont(font);
            for (int page = 1; page <= 300; ++page) {
                if (page > 1)
                    writer.newPage();
                for (int line = 0; line < 30; ++line)
                    p.drawText(QPointF(50, 60 + line * 24),
                               u"Strona %1, wiersz %2: zażółć gęślą jaźń"_s.arg(page).arg(line));
            }
        }
        PdfReaderPool::instance().closeIdle();
        QElapsedTimer timer;
        timer.start();
        auto model = opened(path, 360, 740);
        QSignalSpy status(model.get(), &PdfViewModel::statusChanged);
        while (model->status() == Status::Loading && timer.elapsed() < 20000)
            status.wait(20000);
        const qint64 ready = timer.elapsed();
        QCOMPARE(model->status(), Status::Ready);
        QCOMPARE(model->pageCount(), 300);
        timer.restart();
        brulion::platform::PdfInfo info;
        // Another version: a reader of its own, opened cold (not the model's idle one).
        PdfReaderPool::Lease lease = PdfReaderPool::instance().acquire(path, u"t"_s, &info);
        QVERIFY(lease);
        const qint64 open = timer.elapsed();
        timer.restart();
        const QImage page = lease->render(1, QSize(1080, 1527));
        const qint64 render = timer.elapsed();
        timer.restart();
        const QImage thumb = lease->render(150, QSize(543, 768));
        const qint64 thumbnail = timer.elapsed();
        timer.restart();
        const int hits = int(lease->find(300, u"GESLA"_s).size());
        const qint64 search = timer.elapsed();
        lease = {};
        // The whole PDF searched by the model (one hit a page; the 250 ms debounce included).
        timer.restart();
        model->setSearchText(u"wiersz 29:"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!model->searching() && model->searchedPages() == 300, 60000);
        const qint64 searchAll = timer.elapsed();
        QCOMPARE(model->hitCount(), 300);
        qInfo("%s, 300 pages: Ready %lld ms, open %lld ms, page 1 at 1080 px %lld ms, a thumbnail %lld ms, "
              "search of a page %lld ms (%d hits), search of all pages %lld ms",
              qPrintable(brulion::platform::pdfBackendName()), ready, open, render, thumbnail, search, hits, searchAll);
        QVERIFY(!page.isNull());
        QVERIFY(!thumb.isNull());
        QVERIFY(ready < 10000);
    }

    void readerPoolReuses()
    {
        PdfReaderPool &pool = PdfReaderPool::instance();
        pool.closeIdle();
        const int opened = pool.openedCount();
        {
            brulion::platform::PdfInfo info;
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s, &info);
            QVERIFY(lease);
            QCOMPARE(info.pages, 3);
        }
        QCOMPARE(pool.idleCount(), 1);
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s);
            QVERIFY(lease);
            QCOMPARE(pool.idleCount(), 0);
        }
        QCOMPARE(pool.openedCount(), opened + 1);
        // A new version: the old reader is not used, and not kept.
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"w"_s);
            QVERIFY(lease);
        }
        QCOMPARE(pool.openedCount(), opened + 2);
        QCOMPARE(pool.idleCount(), 1);
        pool.forget(fixture(u"wyklad.pdf"_s));
        QCOMPARE(pool.idleCount(), 0);
        // At most MaxIdle, MaxIdlePerFile per file.
        {
            std::vector<PdfReaderPool::Lease> leases;
            for (int i = 0; i < 6; ++i)
                leases.push_back(pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s));
        }
        QCOMPARE(pool.idleCount(), PdfReaderPool::MaxIdlePerFile);
        QVERIFY(!pool.acquire(fixture(u"zaszyfrowany.pdf"_s), u"v"_s));
        pool.closeIdle();
    }

    void readerPoolSuspended()
    {
        // A sync run of Brulion's own: no reader keeps a PDF open between pages (Windows).
        PdfReaderPool &pool = PdfReaderPool::instance();
        pool.closeIdle();
        { PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s); }
        QCOMPARE(pool.idleCount(), 1);
        pool.suspendIdle();
        QVERIFY(pool.idleSuspended());
        QCOMPARE(pool.idleCount(), 0); // the idle one closed at once
        pool.suspendIdle();            // (nested: a second run, the counter)
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s);
            QVERIFY(lease); // still drawn while suspended
            QCOMPARE(lease->pageCount(), 3);
        }
        QCOMPARE(pool.idleCount(), 0); // given back = closed
        pool.resumeIdle();
        { PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s); }
        QCOMPARE(pool.idleCount(), 0);
        pool.resumeIdle();
        pool.resumeIdle(); // (an unpaired resume does nothing)
        QVERIFY(!pool.idleSuspended());
        { PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s); }
        QCOMPARE(pool.idleCount(), 1);
        pool.closeIdle();
    }

    void providerIds_data()
    {
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("valid");
        QTest::newRow("a page") << u"%2Fnotatnik%2FWyk%C5%82ad.pdf?page=2&v=1-2&w=800.00&h=1131.00"_s << true;
        QTest::newRow("decoded slashes") << u"/notatnik/Wykład ąę.pdf?page=1&v=x&w=10&h=10"_s << true;
        QTest::newRow("a detail") << u"/n/a.pdf?page=1&v=x&w=1000&h=1400&clip=64,128,640,512"_s << true;
        QTest::newRow("relative") << u"notatnik/a.pdf?page=1&v=x&w=10&h=10"_s << false;
        QTest::newRow("parent folder") << u"/notatnik/../etc/a.pdf?page=1&v=x&w=10&h=10"_s << false;
        QTest::newRow("not a PDF") << u"/notatnik/a.png?page=1&v=x&w=10&h=10"_s << false;
        QTest::newRow("no page") << u"/notatnik/a.pdf?v=x&w=10&h=10"_s << false;
        QTest::newRow("page 0") << u"/notatnik/a.pdf?page=0&v=x&w=10&h=10"_s << false;
        QTest::newRow("not a number") << u"/notatnik/a.pdf?page=1&v=x&w=nan&h=10"_s << false;
        QTest::newRow("infinite") << u"/notatnik/a.pdf?page=1&v=x&w=inf&h=10"_s << false;
        QTest::newRow("negative") << u"/notatnik/a.pdf?page=1&v=x&w=-10&h=10"_s << false;
        QTest::newRow("bad clip") << u"/notatnik/a.pdf?page=1&v=x&w=10&h=10&clip=1,2,3"_s << false;
        QTest::newRow("empty clip") << u"/notatnik/a.pdf?page=1&v=x&w=10&h=10&clip=1,2,0,3"_s << false;
        QTest::newRow("empty") << QString() << false;
    }

    void providerIds()
    {
        QFETCH(QString, id);
        QFETCH(bool, valid);
        QCOMPARE(PdfViewImageProvider::parse(id).valid, valid);
    }

    void providerPixels()
    {
        // The page at the screen's ratio (2: the requested size is twice the logical one).
        PdfViewImageProvider::Request page = PdfViewImageProvider::parse(u"/n/a.pdf?page=1&v=x&w=400&h=566"_s);
        QSize full;
        QRect clip;
        QVERIFY(PdfViewImageProvider::pixels(page, QSize(800, 1132), &full, &clip));
        QCOMPARE(full, QSize(800, 1132));
        QVERIFY(clip.isNull());
        // A detail picture: the page's full size from the clip's ratio.
        PdfViewImageProvider::Request detail =
                PdfViewImageProvider::parse(u"/n/a.pdf?page=1&v=x&w=4000&h=5660&clip=640,1280,800,600"_s);
        QVERIFY(PdfViewImageProvider::pixels(detail, QSize(1600, 1200), &full, &clip));
        QCOMPARE(full, QSize(8000, 11320));
        QCOMPARE(clip, QRect(1280, 2560, 1600, 1200));
        // Longer than MaxPdfFullSide: drawn at that size, the clip scaled with it.
        PdfViewImageProvider::Request huge =
                PdfViewImageProvider::parse(u"/n/a.pdf?page=1&v=x&w=40000&h=40000&clip=0,0,1000,1000"_s);
        QVERIFY(PdfViewImageProvider::pixels(huge, QSize(1000, 1000), &full, &clip));
        QCOMPARE(full.width(), brulion::platform::MaxPdfFullSide);
        QVERIFY(clip.width() < 1000);
        // More than MaxPdfClipPixels in one picture: refused.
        PdfViewImageProvider::Request big = PdfViewImageProvider::parse(u"/n/a.pdf?page=1&v=x&w=5000&h=5000"_s);
        QVERIFY(!PdfViewImageProvider::pixels(big, QSize(5000, 5000), &full, &clip));
        QVERIFY(!PdfViewImageProvider::pixels(PdfViewImageProvider::Request(), QSize(10, 10), &full, &clip));
    }
};

QTEST_MAIN(TestPdfViewModel)
#include "tst_pdfviewmodel.moc"
