// PDF pages through Qt PDF (pdfpages.h; QPdfDocument, pdfium): Windows, the AppImage, CI's Linux
// build. Qt PDF serialises pdfium's calls itself; a reader's document is used by one thread at a
// time (PdfReaderPool), pulled to it by attachToCurrentThread().
//
// Text: QPdfDocument::getAllText() per page, matched by pdfsearch.h; a match's rectangles from
// getSelectionAtIndex() (pdfium's character indices are getAllText()'s UTF-16 units; its glyph boxes
// merged into one per line; on a /Rotate page they are the page as shown, as poppler's). Not
// QPdfSearchModel: it searches on the GUI thread with timers and only case-insensitively.
//
// Selection: getSelection() between two points (pdfium's characters nearest to them, 16 pt around)
// and getSelectionAtIndex(); the links from QPdfLinkModel, made on the reader's thread for the call
// (their locations in points, top-left origin); no outline (QPdfBookmarkModel is not safe with a
// hostile PDF, below). A password: QPdfDocument::setPassword(), when the PDF asks for one.
#include "pdfpages.h"
#include "pdfsearch.h"
#include "pdftext.h"

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfLink>
#include <QPdfLinkModel>
#include <QPdfSelection>
#include <QThread>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <limits>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

QString loadError(QPdfDocument::Error error)
{
    switch (error) {
    case QPdfDocument::Error::None: return {};
    case QPdfDocument::Error::FileNotFound: return u"File not found"_s;
    case QPdfDocument::Error::InvalidFileFormat: return u"Not a PDF"_s;
    case QPdfDocument::Error::IncorrectPassword: return u"The PDF is protected with a password"_s;
    case QPdfDocument::Error::UnsupportedSecurityScheme: return u"Unsupported security scheme"_s;
    case QPdfDocument::Error::DataNotYetAvailable: return u"Data not available"_s;
    case QPdfDocument::Error::Unknown: break;
    }
    return u"The PDF cannot be read"_s;
}

/// pdfium's boxes of a selection (a glyph or a run of glyphs each) merged into one rectangle per line.
QList<QRectF> lineRects(const QPdfSelection &selection)
{
    const QList<QPolygonF> bounds = selection.bounds();
    QList<QRectF> boxes;
    boxes.reserve(bounds.size());
    for (const QPolygonF &polygon : bounds)
        boxes.append(polygon.boundingRect());
    return pdfRangeRects(boxes, {0, boxes.size()});
}

class QtPdfReader final : public PdfReader {
public:
    explicit QtPdfReader(std::unique_ptr<QPdfDocument> doc) : m_doc(std::move(doc)) { }

    int pageCount() const override { return m_doc->pageCount(); }

    QSizeF pageSize(int page) override
    {
        return page >= 1 && page <= m_doc->pageCount() ? m_doc->pagePointSize(page - 1) : QSizeF();
    }

    QImage render(int page, const QSize &full, const QRect &clip) override
    {
        const QRect part = clipOf(full, clip);
        if (part.isEmpty() || page < 1 || page > m_doc->pageCount())
            return {};
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QImage rendered;
        if (part.size() == full) {
            rendered = m_doc->render(page - 1, full, options);
        } else {
            // The page scaled to `full`, of it the part `part` (pdfium draws only that part).
            options.setScaledSize(full);
            options.setScaledClipRect(part);
            rendered = m_doc->render(page - 1, part.size(), options);
        }
        return onWhite(rendered, part.size());
    }

    QString pageText(int page) override
    {
        if (page < 1 || page > m_doc->pageCount())
            return {};
        return m_doc->getAllText(page - 1).text();
    }

    QList<PdfTextHit> find(int page, QStringView query, int maxHits) override
    {
        const QString text = pageText(page);
        const QList<PdfTextRange> ranges = findPdfText(text, query, std::min(maxHits, MaxPdfHitsPerPage));
        QList<PdfTextHit> out;
        out.reserve(ranges.size());
        for (const PdfTextRange &range : ranges) {
            const int start = int(range.start);
            const int length = int(range.length);
            out.append(PdfTextHit{.start = start,
                                  .length = length,
                                  .rects = lineRects(m_doc->getSelectionAtIndex(page - 1, start, length))});
        }
        return out;
    }

    PdfTextSelection selectText(int page, QPointF from, QPointF to) override
    {
        if (page < 1 || page > m_doc->pageCount() || !std::isfinite(from.x()) || !std::isfinite(from.y())
            || !std::isfinite(to.x()) || !std::isfinite(to.y()))
            return {};
        // pdfium finds a character within 16 pt of a point only: a point in a margin is moved onto
        // its nearest line first (as poppler's nearest caret), so a drag may end anywhere; the two
        // go in reading order (pdfium drops the last character of a selection made backwards).
        loadLines(page);
        qsizetype lineA = pdfNearestBox(m_lines, from);
        qsizetype lineB = pdfNearestBox(m_lines, to);
        if (lineA < 0 || lineB < 0)
            return {};
        QPointF a = onLine(from, lineA);
        QPointF b = onLine(to, lineB);
        // Above all the text: its first character; below it: its last.
        const auto edge = [this](QPointF p, QPointF &snapped, qsizetype &line) {
            if (p.y() < m_textTop) {
                line = 0;
                snapped = QPointF(m_lines.first().left() + 0.5, m_lines.first().center().y());
            } else if (p.y() > m_textBottom) {
                line = m_lines.size() - 1;
                snapped = QPointF(m_lines.last().right() - 0.5, m_lines.last().center().y());
            }
        };
        edge(from, a, lineA);
        edge(to, b, lineB);
        if (lineB < lineA || (lineB == lineA && b.x() < a.x()))
            std::swap(a, b);
        if (a == b)
            return {};
        return selection(m_doc->getSelection(page - 1, a, b));
    }

    PdfTextSelection selectRange(int page, int start, int length) override
    {
        if (page < 1 || page > m_doc->pageCount())
            return {};
        loadLines(page);
        const qsizetype size = m_text.size();
        const qsizetype from = std::clamp<qsizetype>(start, 0, size);
        const qsizetype to = std::clamp<qsizetype>(qsizetype(start) + std::max(length, 0), from, size);
        if (to <= from)
            return {};
        return selection(m_doc->getSelectionAtIndex(page - 1, int(from), int(to - from)));
    }

    // No outline(): Qt PDF's QPdfBookmarkModel follows a PDF's outline without a guard against
    // loops (an entry its own /First child recursed until the stack overflowed in tst_pdfpages'
    // loopingOutline) — a hostile PDF would crash the app. PdfReader's default: none.

    QList<PdfLink> links(int page) override
    {
        QList<PdfLink> out;
        if (page < 1 || page > m_doc->pageCount())
            return out;
        QPdfLinkModel model;
        model.setDocument(m_doc.get());
        model.setPage(page - 1);
        const int rows = model.rowCount(QModelIndex());
        for (int row = 0; row < rows && out.size() < MaxPdfLinksPerPage; ++row) {
            const QPdfLink link = model.data(model.index(row), int(QPdfLinkModel::Role::Link)).value<QPdfLink>();
            // (isValid() is a page link's: an address is no "valid" QPdfLink.)
            PdfLink entry;
            const QString url = link.url().toString(QUrl::FullyEncoded);
            if (!url.isEmpty()) {
                if (!pdfLinkUrlAllowed(url))
                    continue;
                entry.url = url;
            } else if (!target(link.page(), link.location(), entry.page, entry.top)) {
                continue;
            }
            // One entry per rectangle (a link may cover the ends of two lines).
            const QList<QRectF> rects = link.rectangles();
            for (const QRectF &rect : rects) {
                entry.rect = rect.normalized();
                if (pdfUsableBox(entry.rect) && out.size() < MaxPdfLinksPerPage)
                    out.append(entry);
            }
        }
        return out;
    }

    void attachToCurrentThread() override { m_doc->moveToThread(QThread::currentThread()); }
    void detachFromThread() override { m_doc->moveToThread(nullptr); }

private:
    /// The page's text and its lines (lineRects() of all of it), kept for the last page asked.
    void loadLines(int page)
    {
        if (page == m_linesPage)
            return;
        m_linesPage = page;
        const QPdfSelection all = m_doc->getAllText(page - 1);
        m_text = all.text();
        m_lines = lineRects(all);
        m_textTop = std::numeric_limits<qreal>::infinity();
        m_textBottom = -std::numeric_limits<qreal>::infinity();
        for (const QRectF &line : std::as_const(m_lines)) {
            m_textTop = std::min(m_textTop, line.top());
            m_textBottom = std::max(m_textBottom, line.bottom());
        }
    }

    /// `point` moved onto line `line`, inside it (its ends half a point in).
    QPointF onLine(QPointF point, qsizetype line) const
    {
        const QRectF r = m_lines.at(line);
        return QPointF(std::clamp(point.x(), r.left() + 0.5, std::max(r.left() + 0.5, r.right() - 0.5)),
                       r.center().y());
    }

    PdfTextSelection selection(const QPdfSelection &selected) const
    {
        const QString picked = selected.text();
        if (!selected.isValid() || picked.isEmpty() || selected.startIndex() < 0)
            return {};
        return {.start = selected.startIndex(),
                .length = int(picked.size()),
                .text = pdfCopyText(picked),
                .rects = lineRects(selected)};
    }

    /// A destination (0-based page, a location in points) as a page (1-based) and a top (a
    /// fraction, −1: none); false for a page outside this PDF.
    bool target(int index, QPointF location, int &page, qreal &top) const
    {
        if (index < 0 || index >= m_doc->pageCount())
            return false;
        page = index + 1;
        const qreal height = m_doc->pagePointSize(index).height();
        top = height > 0 && std::isfinite(location.y()) && location.y() > 0
                      ? std::clamp<qreal>(location.y() / height, 0, 1)
                      : -1;
        return true;
    }

    std::unique_ptr<QPdfDocument> m_doc;
    int m_linesPage = 0;
    QString m_text;
    QList<QRectF> m_lines;
    qreal m_textTop = 0;
    qreal m_textBottom = 0;
};

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"Qt PDF"_s;
}

bool pdfTextAvailable()
{
    return true;
}

bool pdfSelectionAvailable()
{
    return true;
}

bool pdfPasswordAvailable()
{
    return true;
}

std::unique_ptr<PdfReader> PdfReader::open(const QString &file, PdfInfo *info, const QString &password)
{
    PdfInfo local;
    PdfInfo &out = info ? *info : local;
    out = PdfInfo();
    auto doc = std::make_unique<QPdfDocument>();
    QPdfDocument::Error error = doc->load(file);
    if (error == QPdfDocument::Error::IncorrectPassword && !password.isEmpty()
        && password.size() <= MaxPdfPasswordLength) {
        // (Only when it asks for one: pdfium tries a password given as the user's and the owner's,
        // never the empty user password of a PDF protected by its owner's alone.)
        doc->setPassword(password);
        error = doc->load(file);
    }
    if (error != QPdfDocument::Error::None || doc->pageCount() <= 0) {
        out.locked = error == QPdfDocument::Error::IncorrectPassword;
        out.error = error == QPdfDocument::Error::None ? u"The PDF has no pages"_s : loadError(error);
        return nullptr;
    }
    out.pages = doc->pageCount();
    out.pagePoints = {doc->pagePointSize(0)};
    // (Owned by the opening thread; a lease gives it back to no thread, the next one pulls it.)
    return std::make_unique<QtPdfReader>(std::move(doc));
}

} // namespace brulion::platform
