// PDF pages through poppler-qt6 (pdfpages.h): the Nix build (nixpkgs has Qt PDF only inside
// QtWebEngine). poppler applies a page's /Rotate in pageSizeF(), when it renders and in the text
// boxes of textList().
//
// Text: textList()'s words (their characters' boxes in points), a space after a word that has one,
// a line break after the last word of a line; matched by pdfsearch.h.
#include "pdfpages.h"
#include "pdfsearch.h"

#include "log/categories.h"

#include <poppler-qt6.h>

#include <memory>
#include <mutex>

using namespace Qt::StringLiterals;

namespace brulion::platform {

namespace {

/// poppler's complaints about a broken file (qDebug by default) go to the platform's category at
/// debug level: a hostile PDF must not fill the event log.
void quietPoppler()
{
    static std::once_flag once;
    std::call_once(once, [] {
        Poppler::setDebugErrorFunction(
                [](const QString &message, const QVariant &) {
                    qCDebug(brulion::logging::lcPlatform).noquote() << "poppler:" << message;
                },
                QVariant());
    });
}

class PopplerReader final : public PdfReader {
public:
    explicit PopplerReader(std::unique_ptr<Poppler::Document> doc) : m_doc(std::move(doc)) { }

    int pageCount() const override { return m_doc->numPages(); }

    QSizeF pageSize(int page) override
    {
        const std::unique_ptr<Poppler::Page> p = pageAt(page);
        return p ? p->pageSizeF() : QSizeF();
    }

    QImage render(int page, const QSize &full, const QRect &clip) override
    {
        const QRect part = clipOf(full, clip);
        const std::unique_ptr<Poppler::Page> p = part.isEmpty() ? nullptr : pageAt(page);
        if (!p)
            return {};
        const QSizeF points = p->pageSizeF();
        if (!(points.width() > 0) || !(points.height() > 0))
            return {};
        // Each axis at the resolution that makes the page exactly `full` (a hostile 1000000 ×
        // 0.001 pt page too: poppler draws only the part asked for, whatever its resolution).
        const double xres = 72.0 * full.width() / points.width();
        const double yres = 72.0 * full.height() / points.height();
        const QImage rendered = part.size() == full
                                        ? p->renderToImage(xres, yres)
                                        : p->renderToImage(xres, yres, part.x(), part.y(), part.width(), part.height());
        // Exactly the size asked for (the resolution rounds), on white.
        return onWhite(rendered, part.size());
    }

    QString pageText(int page) override
    {
        load(page);
        return m_text;
    }

    QList<PdfTextHit> find(int page, QStringView query, int maxHits) override
    {
        QList<PdfTextHit> out;
        load(page);
        for (const PdfTextRange &range : findPdfText(m_text, query, std::min(maxHits, MaxPdfHitsPerPage))) {
            PdfTextHit hit;
            hit.start = int(range.start);
            hit.length = int(range.length);
            hit.rects = pdfRangeRects(m_boxes, range);
            out.append(hit);
        }
        return out;
    }

private:
    std::unique_ptr<Poppler::Page> pageAt(int page) const
    {
        return page >= 1 && page <= m_doc->numPages() ? m_doc->page(page - 1) : nullptr;
    }

    /// The page's text and a box for each of its units (kept for the last page asked: a search
    /// reads the text, then finds in it).
    void load(int page)
    {
        if (page == m_textPage)
            return;
        m_textPage = page;
        m_text.clear();
        m_boxes.clear();
        const std::unique_ptr<Poppler::Page> p = pageAt(page);
        if (!p)
            return;
        const std::vector<std::unique_ptr<Poppler::TextBox>> words = p->textList();
        for (const std::unique_ptr<Poppler::TextBox> &word : words) {
            const QString text = word->text();
            const QRectF wordBox = word->boundingBox();
            // charBoundingBox() counts characters (code points); a pair of surrogates shares one.
            int character = 0;
            for (qsizetype i = 0; i < text.size(); ++i) {
                QRectF box = word->charBoundingBox(character);
                if (box.isEmpty())
                    box = wordBox;
                m_text.append(text.at(i));
                m_boxes.append(box);
                if (!(text.at(i).isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()))
                    ++character;
            }
            if (!word->nextWord()) {
                m_text.append(u'\n'); // the line ends
                m_boxes.append(QRectF());
            } else if (word->hasSpaceAfter()) {
                m_text.append(u' ');
                m_boxes.append(QRectF());
            }
        }
    }

    std::unique_ptr<Poppler::Document> m_doc;
    int m_textPage = 0;
    QString m_text;
    QList<QRectF> m_boxes;
};

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"poppler"_s;
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
    quietPoppler();
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(file);
    if (!doc) {
        out.error = u"The PDF cannot be read"_s;
        return nullptr;
    }
    if (doc->isLocked()) {
        out.locked = true;
        out.error = u"The PDF is protected with a password"_s;
        return nullptr;
    }
    if (doc->numPages() <= 0) {
        out.error = u"The PDF has no pages"_s;
        return nullptr;
    }
    doc->setRenderHint(Poppler::Document::Antialiasing, true);
    doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    doc->setPaperColor(Qt::white);
    out.pages = doc->numPages();
    const std::unique_ptr<Poppler::Page> first = doc->page(0);
    out.pagePoints = {first ? first->pageSizeF() : QSizeF()};
    return std::make_unique<PopplerReader>(std::move(doc));
}

} // namespace brulion::platform
