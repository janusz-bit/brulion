// PDF pages through Qt PDF (pdfpages.h; QPdfDocument, pdfium): Windows, the AppImage, CI's Linux
// build. Qt PDF serialises pdfium's calls itself; a reader's document is used by one thread at a
// time (PdfReaderPool), pulled to it by attachToCurrentThread().
//
// Text: QPdfDocument::getAllText() per page, matched by pdfsearch.h; a match's rectangles from
// getSelectionAtIndex() (pdfium's character indices are getAllText()'s UTF-16 units; its glyph boxes
// merged into one per line; on a /Rotate page they are the page as shown, as poppler's). Not
// QPdfSearchModel: it searches on the GUI thread with timers and only case-insensitively.
#include "pdfpages.h"
#include "pdfsearch.h"

#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfSelection>
#include <QThread>

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
        QList<PdfTextHit> out;
        const QString text = pageText(page);
        for (const PdfTextRange &range : findPdfText(text, query, std::min(maxHits, MaxPdfHitsPerPage))) {
            PdfTextHit hit;
            hit.start = int(range.start);
            hit.length = int(range.length);
            const QPdfSelection selection = m_doc->getSelectionAtIndex(page - 1, hit.start, hit.length);
            // pdfium gives a box per glyph: one rectangle per line.
            QList<QRectF> boxes;
            for (const QPolygonF &polygon : selection.bounds())
                boxes.append(polygon.boundingRect());
            hit.rects = pdfRangeRects(boxes, {0, boxes.size()});
            out.append(hit);
        }
        return out;
    }

    void attachToCurrentThread() override { m_doc->moveToThread(QThread::currentThread()); }
    void detachFromThread() override { m_doc->moveToThread(nullptr); }

private:
    std::unique_ptr<QPdfDocument> m_doc;
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

std::unique_ptr<PdfReader> PdfReader::open(const QString &file, PdfInfo *info)
{
    PdfInfo local;
    PdfInfo &out = info ? *info : local;
    out = PdfInfo();
    auto doc = std::make_unique<QPdfDocument>();
    const QPdfDocument::Error error = doc->load(file);
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
