// The PDF viewer's state (src/app/pdf/pdfviewmodel.h) with this build's renderer on tests/data/pdf:
// opening (Loading → Ready, three pages), a locked and a missing file, the visible pages, zoom around
// a point, detail pictures only when zoomed in, search without diacritics and case (a turtle on page
// 3), a new text cancelling the old search, a PDF without text, the search turned off, a new version
// opened at the same page, a page that failed; text selected between two points, a word, a page, its
// rectangles, older requests dropped, copied to the clipboard; the table of contents and the links
// of tests/data/pdf/spis.pdf (where a target lies); a protected PDF unlocked with its password (a
// wrong one rejected and never kept, the image provider's password held while the viewer is open and
// forgotten with it, never the thumbnails'); two pages side by side keeping the current page; a page
// jumped to before the real page sizes arrived staying current; and the
// image provider's ids (pure).
#include "pdfviewmodel.h"

#include "pdfpages.h"
#include "pdfrenderpool.h"
#include "pdfviewimageprovider.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
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
            QCOMPARE(d.value(u"box"_s).toRectF(), QRectF(d.value(u"x"_s).toReal(), d.value(u"y"_s).toReal(),
                                                         d.value(u"width"_s).toReal(), d.value(u"height"_s).toReal()));
        }
        QVERIFY(withDetail >= 1);
        // The pages' rows carry it too (detailUrl, detailBox: the QML delegates' roles).
        QAbstractListModel *pages = model->pages();
        const QHash<int, QByteArray> roles = pages->roleNames();
        const int urlRole = roles.key("detailUrl");
        const int boxRole = roles.key("detailBox");
        QVERIFY(urlRole > 0 && boxRole > 0);
        int rowsWithDetail = 0;
        for (int row = 0; row < pages->rowCount(); ++row) {
            const QModelIndex index = pages->index(row);
            const int page = pages->data(index, roles.key("page")).toInt();
            const QVariantMap d = model->detail(page);
            QCOMPARE(pages->data(index, urlRole).toUrl(), d.value(u"url"_s).toUrl());
            QCOMPARE(pages->data(index, boxRole).toRectF(), d.value(u"box"_s).toRectF());
            rowsWithDetail += d.isEmpty() ? 0 : 1;
        }
        QCOMPARE(rowsWithDetail, withDetail);
        // A new settle tells the rows.
        QSignalSpy rowsChanged(pages, &QAbstractItemModel::dataChanged);
        model->settle();
        QVERIFY(!rowsChanged.isEmpty());
        QVERIFY(rowsChanged.first().at(2).value<QList<int>>().contains(urlRole));
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
        {
            // `box`: the same rectangle as x, y, width, height (QML's typed delegates).
            const QVariantMap hit = model->hitRects(3).first().toMap();
            QCOMPARE(hit.value(u"box"_s).toRectF(),
                     QRectF(hit.value(u"x"_s).toReal(), hit.value(u"y"_s).toReal(), hit.value(u"width"_s).toReal(),
                            hit.value(u"height"_s).toReal()));
        }
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

    void jumpBeforeTheSizesKeepsPage()
    {
        // A jump right after the PDF is ready, before the real page sizes arrive (a moved PDF
        // reopened at its route's page on a slow machine): the landscape page 2 shrinks the
        // content, the view follows the new layout — and page 2 stays current where the jump puts
        // it now, instead of the last page of a view scrolled to its end.
        auto model = opened(fixture(u"wyklad.pdf"_s), 400, 900);
        PdfViewModel *m = model.get();
        bool jumped = false;
        qreal viewY = -1;
        // (The view: its Flickable follows moveViewport; the jump in the same event as Ready.)
        connect(m, &PdfViewModel::moveViewport, this, [&](qreal x, qreal y) {
            viewY = y;
            m->setViewport(x, y);
            if (jumped || m->status() != Status::Ready)
                return;
            jumped = true;
            QVERIFY(m->pageRect(2).height() > m->pageRect(2).width()); // (still the first page's size)
            const QPointF two = m->positionForPage(2);
            viewY = two.y();
            m->setViewport(two.x(), two.y());
            m->pinPage(2);
        });
        QTRY_VERIFY(jumped);
        QCOMPARE(model->currentPage(), 2);
        QTRY_VERIFY(model->pageRect(2).width() > model->pageRect(2).height()); // (the sizes arrived)
        QVERIFY(model->contentHeight() - 900 < model->pageRect(2).top()); // (page 2 cannot reach the top)
        QCOMPARE(model->currentPage(), 2);
        QVERIFY(std::abs(viewY - model->positionForPage(2).y()) < 0.5);
        // Scrolled by hand afterwards: the pin goes, as before.
        model->setViewport(0, 0);
        QCOMPARE(model->currentPage(), 1);
        model->setViewport(0, model->contentHeight() - 900);
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

    void selection()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        if (!brulion::platform::pdfSelectionAvailable()) {
            QVERIFY(!model->selectionAvailable());
            model->selectText(1, QPointF(0, 0), QPointF(1, 1));
            QTest::qWait(100);
            QVERIFY(model->selectedText().isEmpty());
            return;
        }
        QVERIFY(model->selectionAvailable());
        QSignalSpy changed(model.get(), &PdfViewModel::selectionChanged);
        // Page 1's line (baseline 120 pt of 842, from x 60): from the left margin to the right one.
        model->selectText(1, QPointF(0.01, 110.0 / 842), QPointF(0.99, 110.0 / 842));
        QTRY_COMPARE(model->selectedText().simplified(), u"Wykład 3 — Zażółć gęślą jaźń"_s);
        QCOMPARE(model->selectionPage(), 1);
        const QVariantList rects = model->selectionRects(1);
        QCOMPARE(rects.size(), 1);
        const QVariantMap r = rects.first().toMap();
        QVERIFY(r.value(u"x"_s).toReal() > 0.05 && r.value(u"x"_s).toReal() < 0.15);
        QVERIFY(r.value(u"y"_s).toReal() > 0.05 && r.value(u"y"_s).toReal() < 0.16);
        QCOMPARE(r.value(u"box"_s).toRectF(), QRectF(r.value(u"x"_s).toReal(), r.value(u"y"_s).toReal(),
                                                     r.value(u"width"_s).toReal(), r.value(u"height"_s).toReal()));
        // The floating "Copy" goes over the first rectangle, on the selection's page.
        QCOMPARE(model->selectionAnchor(), r.value(u"box"_s).toRectF());
        QCOMPARE(model->selectionPageRect(), model->pageRect(1));
        QVERIFY(model->selectionRects(2).isEmpty());
        // Copied as it is.
        QVERIFY(model->copySelection());
        QCOMPARE(QGuiApplication::clipboard()->text().simplified(), u"Wykład 3 — Zażółć gęślą jaźń"_s);
        // A drag asks with every move: only the newest answer counts.
        for (int i = 0; i < 20; ++i)
            model->selectText(3, QPointF(0.01, 110.0 / 842), QPointF(0.02 + i * 0.001, 110.0 / 842));
        model->selectText(3, QPointF(0.01, 110.0 / 842), QPointF(0.99, 110.0 / 842));
        QTRY_COMPARE(model->selectedText().simplified(), u"Strona trzecia: żółw"_s);
        QCOMPARE(model->selectionPage(), 3);
        // All of a page (Ctrl+A).
        model->clearSelection();
        model->selectAll(3);
        QTRY_COMPARE(model->selectedText().simplified(), u"Strona trzecia: żółw"_s);
        // Cleared; nothing to copy then.
        QSignalSpy pageRectChanged(model.get(), &PdfViewModel::selectionPageRectChanged);
        model->clearSelection();
        QVERIFY(model->selectedText().isEmpty());
        QCOMPARE(model->selectionPage(), 0);
        QVERIFY(model->selectionAnchor().isEmpty());
        QVERIFY(model->selectionPageRect().isEmpty());
        QVERIFY(!pageRectChanged.isEmpty());
        QVERIFY(!model->copySelection());
        // A slide without text, a page out of range, NaN: nothing.
        model->selectText(2, QPointF(0, 0), QPointF(1, 1));
        QTest::qWait(200);
        QVERIFY(model->selectedText().isEmpty());
        model->selectText(9, QPointF(0, 0), QPointF(1, 1));
        model->selectText(1, QPointF(qQNaN(), 0), QPointF(1, 1));
        QTest::qWait(100);
        QVERIFY(model->selectedText().isEmpty());
        QVERIFY(changed.size() >= 3);
        // Turned off (a renderer without it): cleared, nothing more.
        model->setSelectionAvailable(false);
        model->selectAll(1);
        QTest::qWait(100);
        QVERIFY(model->selectedText().isEmpty());
    }

    void selectWord()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        if (!brulion::platform::pdfSelectionAvailable())
            QSKIP("no PDF text selection in this build");
        // Where the search finds "gęślą" on page 1: its middle, in fractions of the page.
        model->setSearchText(u"gesla"_s);
        QTRY_VERIFY(!model->searching() && model->hitCount() == 1);
        const QVariantMap hit = model->hitRects(1).first().toMap();
        const QPointF middle(hit.value(u"x"_s).toReal() + hit.value(u"width"_s).toReal() / 2,
                             hit.value(u"y"_s).toReal() + hit.value(u"height"_s).toReal() / 2);
        model->selectWord(1, middle);
        QTRY_COMPARE(model->selectedText(), u"gęślą"_s);
        // In the margin, nothing near: nothing.
        model->selectWord(1, QPointF(0.5, 0.9));
        QTRY_VERIFY(model->selectedText().isEmpty());
    }

    void outlineAndLinks()
    {
        auto model = opened(fixture(u"spis.pdf"_s), 800, 600);
        QTRY_COMPARE(model->status(), Status::Ready);
        if (brulion::platform::pdfBackendName() == u"Android PdfRenderer"_s)
            QSKIP("no outline or links with PdfRenderer");
        // Page 1's links come as it is shown: Rozdział 2, the web page, Rozdział 3 (never the script).
        QTRY_COMPARE(model->linkRects(1).size(), 3);
        if (brulion::platform::pdfBackendName() != u"poppler"_s) {
            QTest::qWait(200);
            QVERIFY(model->outline().isEmpty()); // (poppler only)
        } else {
            QTRY_COMPARE(model->outline().size(), 5);
        }
        if (!model->outline().isEmpty()) {
            const QVariantMap chapter = model->outline().at(1).toMap();
            QCOMPARE(chapter.value(u"title"_s).toString(), u"Rozdział 2 — żółw"_s);
            QCOMPARE(chapter.value(u"page"_s).toInt(), 2);
            QVERIFY(std::abs(chapter.value(u"top"_s).toReal() - 0.5) < 0.02);
            QCOMPARE(chapter.value(u"targetTop"_s), chapter.value(u"top"_s));
            QCOMPARE(model->outline().at(2).toMap().value(u"level"_s).toInt(), 1);
            QCOMPARE(model->outline().at(4).toMap().value(u"page"_s).toInt(), 0);
            // The entry of a page: the last one leading to it or to a page before it.
            QCOMPARE(model->outlineIndexForPage(0), -1);
            for (int page = 1; page <= 4; ++page) {
                int best = -1;
                for (int i = 0; i < model->outline().size(); ++i) {
                    const int at = model->outline().at(i).toMap().value(u"page"_s).toInt();
                    if (at > 0 && at <= page)
                        best = i;
                }
                QCOMPARE(model->outlineIndexForPage(page), best);
            }
            QVERIFY(model->outlineIndexForPage(2) >= 1); // "Rozdział 2" or one after it
        }
        QCOMPARE(PdfViewModel().outlineIndexForPage(1), -1); // no outline
        // A target in the middle of page 2: a gap above it.
        const QPointF at = model->positionForTarget(2, 0.5);
        const QRectF page2 = model->pageRect(2);
        QVERIFY2(std::abs(at.y() - (page2.y() + page2.height() / 2 - 12)) < 0.5,
                 qPrintable(u"%1 vs %2"_s.arg(at.y()).arg(page2.y() + page2.height() / 2)));
        // No place: the page's top; a page out of range: clamped.
        QCOMPARE(model->positionForTarget(3, -1), model->positionForPage(3, 0));
        QCOMPARE(model->positionForTarget(9, -1), model->positionForPage(3, 0));
        const QVariantList links = model->linkRects(1);
        QCOMPARE(links.at(0).toMap().value(u"page"_s).toInt(), 2);
        QVERIFY(links.at(0).toMap().value(u"url"_s).toString().isEmpty());
        QCOMPARE(links.at(1).toMap().value(u"url"_s).toString(),
                 u"https://example.org/brulion?q=%C5%BC%C3%B3%C5%82w"_s);
        QCOMPARE(links.at(2).toMap().value(u"page"_s).toInt(), 3);
        for (const QVariant &link : links)
            QCOMPARE(link.toMap().value(u"targetTop"_s), link.toMap().value(u"top"_s));
        const QVariantMap first = links.at(0).toMap();
        QVERIFY(std::abs(first.value(u"x"_s).toReal() - 55.0 / 595) < 0.01);
        QVERIFY(std::abs(first.value(u"y"_s).toReal() - (842.0 - 724) / 842) < 0.01);
        QCOMPARE(first.value(u"box"_s).toRectF(),
                 QRectF(first.value(u"x"_s).toReal(), first.value(u"y"_s).toReal(), first.value(u"width"_s).toReal(),
                        first.value(u"height"_s).toReal()));
        QVERIFY(model->linkRects(2).isEmpty());
        // Another file: the outline and the links go.
        model->setFile(fixture(u"wyklad.pdf"_s));
        QTRY_COMPARE(model->status(), Status::Ready);
        QTRY_VERIFY(model->outline().isEmpty() && model->linkRects(1).isEmpty());
        QTRY_COMPARE(model->pageCount(), 3);
    }

    void password()
    {
        PdfReaderPool &pool = PdfReaderPool::instance();
        const QString file = fixture(u"zaszyfrowany.pdf"_s);
        auto model = opened(file);
        QTRY_COMPARE(model->status(), Status::Failed);
        QVERIFY(model->locked());
        if (!brulion::platform::pdfPasswordAvailable()) {
            QVERIFY(!model->passwordAvailable());
            QVERIFY2(model->error().contains(u"cannot open"_s), qPrintable(model->error()));
            return;
        }
        QVERIFY(model->passwordAvailable());
        QVERIFY(!model->passwordRejected());
        QVERIFY(!model->unlocked());
        // A wrong one: still locked, rejected, not kept.
        model->unlock(u"jawne"_s);
        QTRY_VERIFY(model->passwordRejected());
        QCOMPARE(model->status(), Status::Failed);
        QVERIFY(model->locked());
        QVERIFY(pool.password(file).isEmpty());
        // The right one.
        model->unlock(u"tajne"_s);
        QTRY_COMPARE(model->status(), Status::Ready);
        QVERIFY(!model->passwordRejected());
        QVERIFY(!model->locked());
        QVERIFY(model->unlocked());
        QCOMPARE(model->pageCount(), 3);
        // The image provider's pages are drawn with it meanwhile (memory only)…
        QCOMPARE(pool.password(file), u"tajne"_s);
        {
            PdfReaderPool::Lease lease = pool.acquire(file, u"1"_s, nullptr, pool.password(file));
            QVERIFY(lease);
        }
        // …the thumbnails never: no password, no reader.
        QVERIFY(!pool.acquire(file, u"1"_s));
        if (brulion::platform::pdfTextAvailable()) {
            model->setSearchText(u"zolw"_s);
            QTRY_VERIFY(!model->searching() && model->hitCount() == 1);
        }
        // A new version of the file opens with it again (the page kept).
        model->setVersion(u"2"_s);
        QTRY_COMPARE(model->status(), Status::Ready);
        QVERIFY(model->unlocked());
        // Another file forgets it, and its idle readers opened with it are closed.
        model->setFile(fixture(u"wyklad.pdf"_s));
        QVERIFY(pool.password(file).isEmpty());
        QTRY_COMPARE(model->status(), Status::Ready);
        QVERIFY(!model->unlocked());
        model->setFile(file);
        QTRY_COMPARE(model->status(), Status::Failed);
        QVERIFY(model->locked());
        QVERIFY(!model->passwordRejected());
        // The model's end forgets it too.
        model->unlock(u"tajne"_s);
        QTRY_COMPARE(model->status(), Status::Ready);
        QCOMPARE(pool.password(file), u"tajne"_s);
        model.reset();
        QVERIFY(pool.password(file).isEmpty());
        // A renderer that opens none says so.
        auto other = opened(file);
        other->setPasswordAvailable(false);
        QTRY_COMPARE(other->status(), Status::Failed);
        QVERIFY2(other->error().contains(u"cannot open"_s), qPrintable(other->error()));
    }

    void twoPages()
    {
        auto model = opened(fixture(u"wyklad.pdf"_s), 1000, 700);
        QTRY_COMPARE(model->status(), Status::Ready);
        QTRY_VERIFY(std::abs(model->pageRect(2).width() - (1000 - 48)) < 0.5); // (the slide's size arrived)
        QVERIFY(!model->twoPages());
        // Go to page 3, then two pages side by side: page 3 stays the current one, in view.
        model->setViewport(0, model->positionForPage(3).y());
        QCOMPARE(model->currentPage(), 3);
        QSignalSpy moved(model.get(), &PdfViewModel::moveViewport);
        QSignalSpy spread(model.get(), &PdfViewModel::spreadChanged);
        model->setTwoPages(true);
        QVERIFY(model->twoPages());
        QCOMPARE(spread.size(), 1);
        QCOMPARE(model->currentPage(), 3);
        QVERIFY(!moved.isEmpty());
        // Pages 1 and 2 side by side (fit width: the pair), page 3 alone below.
        QVERIFY(std::abs(model->pageRect(1).y() - model->pageRect(2).y()) < 0.5);
        QVERIFY(model->pageRect(2).left() > model->pageRect(1).right());
        QVERIFY(model->pageRect(3).top() > model->pageRect(1).bottom());
        QVERIFY(model->pageRect(2).right() <= 1000 - 24 + 0.5);
        QCOMPARE(model->pageAt(model->pageRect(2).center().x(), model->pageRect(2).center().y()), 2);
        QCOMPARE(model->pageAt(model->pageRect(1).center().x(), model->pageRect(1).center().y()), 1);
        // The cover alone: 1 | 2 3.
        model->setCoverAlone(true);
        QVERIFY(std::abs(model->pageRect(2).y() - model->pageRect(3).y()) < 0.5);
        QVERIFY(model->pageRect(2).top() > model->pageRect(1).bottom());
        QCOMPARE(model->currentPage(), 3);
        // Back to one page per row.
        model->setTwoPages(false);
        QVERIFY(model->pageRect(2).top() > model->pageRect(1).bottom());
        QVERIFY(model->pageRect(3).top() > model->pageRect(2).bottom());
        QCOMPARE(model->currentPage(), 3);
        QCOMPARE(model->pageAt(10, 10), 1);
    }

    void readerPoolPasswords()
    {
        PdfReaderPool &pool = PdfReaderPool::instance();
        pool.closeIdle();
        const QString file = fixture(u"zaszyfrowany.pdf"_s);
        if (!brulion::platform::pdfPasswordAvailable())
            QSKIP("no passwords in this build");
        // Counted holds; idle readers opened with it are reused only with it.
        pool.holdPassword(file, u"tajne"_s);
        pool.holdPassword(file, u"tajne"_s);
        {
            PdfReaderPool::Lease lease = pool.acquire(file, u"v"_s, nullptr, u"tajne"_s);
            QVERIFY(lease);
        }
        QCOMPARE(pool.idleCount(), 1);
        QVERIFY(!pool.acquire(file, u"v"_s)); // (without it: opened anew, locked)
        QCOMPARE(pool.idleCount(), 1);
        pool.releasePassword(file);
        QCOMPARE(pool.password(file), u"tajne"_s);
        QCOMPARE(pool.idleCount(), 1);
        pool.releasePassword(file);
        QVERIFY(pool.password(file).isEmpty());
        QCOMPARE(pool.idleCount(), 0); // closed with the last hold
        // Given back after the last hold: not kept open.
        {
            PdfReaderPool::Lease lease = pool.acquire(file, u"v"_s, nullptr, u"tajne"_s);
            QVERIFY(lease);
        }
        QCOMPARE(pool.idleCount(), 0);
        pool.releasePassword(file);         // (an unpaired release does nothing)
        pool.holdPassword(file, QString()); // (no password: nothing held)
        QVERIFY(pool.password(file).isEmpty());
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
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s);
        }
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
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s);
        }
        QCOMPARE(pool.idleCount(), 0);
        pool.resumeIdle();
        pool.resumeIdle(); // (an unpaired resume does nothing)
        QVERIFY(!pool.idleSuspended());
        {
            PdfReaderPool::Lease lease = pool.acquire(fixture(u"wyklad.pdf"_s), u"v"_s);
        }
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
