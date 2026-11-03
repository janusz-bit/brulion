// PDF pages through poppler-qt6 (pdfpages.h): the Nix build (nixpkgs has Qt PDF only inside
// QtWebEngine). poppler applies a page's /Rotate in pageSizeF(), when it renders and in the text
// boxes of textList().
//
// Text: textList()'s words (their characters' boxes in points), a space after a word that has one,
// a line break after the last word of a line; matched by pdfsearch.h, selected by pdftext.h (the
// carets nearest to two points). The outline and the links: poppler's own (their positions come
// normalized, the top-left origin, rotation applied). A password: tried as the owner's and the
// user's, in UTF-8 (AES-256, PDF 2.0) and, when that fails, in Latin-1 (older encryption).
#include "pdfpages.h"
#include "pdfsearch.h"
#include "pdftext.h"

#include "log/categories.h"

#include <poppler-link.h>
#include <poppler-qt6.h>

#include <algorithm>
#include <cmath>
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

    PdfTextSelection selectText(int page, QPointF from, QPointF to) override
    {
        load(page);
        return selection(pdfRangeBetween(m_boxes, from, to));
    }

    PdfTextSelection selectRange(int page, int start, int length) override
    {
        load(page);
        const qsizetype from = std::clamp<qsizetype>(start, 0, m_text.size());
        const qsizetype to = std::clamp<qsizetype>(qsizetype(start) + std::max(length, 0), from, m_text.size());
        return selection({from, to - from});
    }

    QList<PdfOutlineItem> outline() override
    {
        QList<PdfOutlineItem> out;
        addOutline(m_doc->outline(), 0, out);
        return out;
    }

    QList<PdfLink> links(int page) override
    {
        QList<PdfLink> out;
        const std::unique_ptr<Poppler::Page> p = pageAt(page);
        if (!p)
            return out;
        const QSizeF points = p->pageSizeF();
        if (!(points.width() > 0) || !(points.height() > 0))
            return out;
        for (const std::unique_ptr<Poppler::Link> &link : p->links()) {
            if (out.size() >= MaxPdfLinksPerPage)
                break;
            const QRectF area = link->linkArea().normalized();
            if (area.isEmpty() || !std::isfinite(area.x()) || !std::isfinite(area.y()) || !std::isfinite(area.width())
                || !std::isfinite(area.height()))
                continue;
            PdfLink out1;
            out1.rect = QRectF(area.x() * points.width(), area.y() * points.height(), area.width() * points.width(),
                               area.height() * points.height());
            if (link->linkType() == Poppler::Link::Goto) {
                const auto *go = static_cast<const Poppler::LinkGoto *>(link.get());
                if (go->isExternal())
                    continue; // another file
                if (!target(go->destination(), out1.page, out1.top))
                    continue;
            } else if (link->linkType() == Poppler::Link::Browse) {
                out1.url = static_cast<const Poppler::LinkBrowse *>(link.get())->url();
                if (!pdfLinkUrlAllowed(out1.url))
                    continue;
            } else {
                continue; // launch, JavaScript, forms, media: never
            }
            out.append(out1);
        }
        return out;
    }

private:
    std::unique_ptr<Poppler::Page> pageAt(int page) const
    {
        return page >= 1 && page <= m_doc->numPages() ? m_doc->page(page - 1) : nullptr;
    }

    /// The page and the top (a fraction, −1 none) of a destination; a named one looked up. False
    /// when it names no page of this PDF.
    bool target(const Poppler::LinkDestination &destination, int &page, qreal &top)
    {
        page = destination.pageNumber();
        top = destination.isChangeTop() ? destination.top() : -1;
        if (page <= 0 && !destination.destinationName().isEmpty()) {
            if (const std::unique_ptr<Poppler::LinkDestination> named =
                        m_doc->linkDestination(destination.destinationName())) {
                page = named->pageNumber();
                top = named->isChangeTop() ? named->top() : -1;
            }
        }
        if (page < 1 || page > m_doc->numPages()) {
            page = 0;
            return false;
        }
        top = std::isfinite(top) && top >= 0 ? std::min<qreal>(top, 1) : -1;
        return true;
    }

    void addOutline(const QVector<Poppler::OutlineItem> &items, int level, QList<PdfOutlineItem> &out)
    {
        for (const Poppler::OutlineItem &item : items) {
            if (out.size() >= MaxPdfOutlineItems)
                return;
            if (item.isNull())
                continue;
            PdfOutlineItem entry;
            entry.title = pdfCleanTitle(item.name());
            entry.level = level;
            if (item.externalFileName().isEmpty()) {
                if (const QSharedPointer<const Poppler::LinkDestination> d = item.destination())
                    target(*d, entry.page, entry.top);
            }
            if (!entry.title.isEmpty() || entry.page > 0)
                out.append(entry);
            if (level + 1 < MaxPdfOutlineDepth && item.hasChildren())
                addOutline(item.children(), level + 1, out);
        }
    }

    PdfTextSelection selection(PdfTextRange range) const
    {
        PdfTextSelection out;
        if (range.length <= 0 || range.start < 0 || range.start + range.length > m_text.size())
            return out;
        out.start = int(range.start);
        out.length = int(range.length);
        out.text = pdfCopyText(QStringView(m_text).sliced(range.start, range.length));
        out.rects = pdfRangeRects(m_boxes, range);
        return out;
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
    quietPoppler();
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(file);
    if (!doc) {
        out.error = u"The PDF cannot be read"_s;
        return nullptr;
    }
    if (doc->isLocked() && !password.isEmpty() && password.size() <= MaxPdfPasswordLength) {
        // As the owner's or the user's password: UTF-8 (AES-256), else Latin-1 (RC4, AES-128).
        const QByteArray utf8 = password.toUtf8();
        doc->unlock(utf8, utf8); // (returns whether it is still locked)
        const QByteArray latin1 = password.toLatin1();
        if (doc->isLocked() && latin1 != utf8)
            doc->unlock(latin1, latin1);
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
