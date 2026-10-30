// PDF pages through poppler-qt6 (pdfpages.h): the Nix build (nixpkgs has Qt PDF only inside
// QtWebEngine). poppler applies a page's /Rotate in pageSizeF() and when it renders.
#include "pdfpages.h"

#include "log/categories.h"

#include <poppler-qt6.h>

#include <QPainter>

#include <algorithm>
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

/// The document of `file`; null and `info` filled when it cannot be read.
std::unique_ptr<Poppler::Document> load(const QString &file, PdfInfo *info)
{
    quietPoppler();
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(file);
    info->pages = -1;
    if (!doc) {
        info->error = u"The PDF cannot be read"_s;
        return nullptr;
    }
    if (doc->isLocked()) {
        info->locked = true;
        info->error = u"The PDF is protected with a password"_s;
        return nullptr;
    }
    if (doc->numPages() <= 0) {
        info->error = u"The PDF has no pages"_s;
        return nullptr;
    }
    doc->setRenderHint(Poppler::Document::Antialiasing, true);
    doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
    doc->setPaperColor(Qt::white);
    return doc;
}

} // namespace

bool pdfAvailable()
{
    return true;
}

QString pdfBackendName()
{
    return u"poppler"_s;
}

PdfInfo pdfInfo(const QString &file, bool pageSizes)
{
    PdfInfo info;
    const std::unique_ptr<Poppler::Document> doc = load(file, &info);
    if (!doc)
        return info;
    info.pages = doc->numPages();
    if (!pageSizes)
        return info;
    info.pagePoints.reserve(info.pages);
    for (int i = 0; i < info.pages; ++i) {
        const std::unique_ptr<Poppler::Page> page = doc->page(i);
        info.pagePoints.append(page ? page->pageSizeF() : QSizeF());
    }
    return info;
}

QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error)
{
    PdfInfo info;
    const std::unique_ptr<Poppler::Document> doc = load(file, &info);
    const std::unique_ptr<Poppler::Page> p =
            doc && page >= 1 && page <= doc->numPages() ? doc->page(page - 1) : nullptr;
    if (!p) {
        if (error)
            *error = doc ? u"No page %1"_s.arg(page) : info.error;
        return {};
    }
    const QSizeF points = p->pageSizeF();
    const QSize size = pdfPagePixels(points, maxSide);
    if (size.isEmpty()) {
        if (error)
            *error = u"Page %1 has no size"_s.arg(page);
        return {};
    }
    // (At least 18 dpi: poppler makes no picture of less than a pixel; it is scaled down below.)
    const double dpi =
            std::max(18.0, 72.0 * std::max(size.width(), size.height()) / std::max(points.width(), points.height()));
    const QImage rendered = p->renderToImage(dpi, dpi);
    if (rendered.isNull()) {
        if (error)
            *error = u"Page %1 cannot be rendered"_s.arg(page);
        return {};
    }
    // Exactly the size asked for (the resolution rounds), on white.
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(QPoint(0, 0), size), rendered);
    painter.end();
    return out;
}

} // namespace brulion::platform
