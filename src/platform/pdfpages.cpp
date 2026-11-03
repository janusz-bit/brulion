// What every PDF backend shares (pdfpages.h): the size and resolution math, render()'s checks, and
// pdfInfo() / renderPdfPage() on top of the backend's PdfReader (one of pdfpages_*.cpp).
#include "pdfpages.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace brulion::platform {

QSize pdfPagePixels(const QSizeF &points, int maxSide)
{
    if (!(points.width() > 0) || !(points.height() > 0) || !std::isfinite(points.width())
        || !std::isfinite(points.height()))
        return {};
    const int side = std::clamp(maxSide, 1, MaxPdfPageSide);
    const double k = side / std::max(points.width(), points.height());
    return QSize(std::clamp(int(std::lround(points.width() * k)), 1, side),
                 std::clamp(int(std::lround(points.height() * k)), 1, side));
}

double pdfRenderDpi(const QSizeF &points, const QSize &pixels)
{
    if (!(points.width() > 0) || !(points.height() > 0) || !std::isfinite(points.width())
        || !std::isfinite(points.height()) || pixels.isEmpty())
        return 0;
    // Both sides at least as many pixels as asked (a thin strip's one pixel across)…
    const double dpi = 72.0 * std::max(pixels.width() / points.width(), pixels.height() / points.height());
    // …but never a picture longer than MaxPdfPageSide (a hostile 1000000 × 0.001 pt page).
    return std::min(dpi, 72.0 * MaxPdfPageSide / std::max(points.width(), points.height()));
}

QRect PdfReader::clipOf(const QSize &full, const QRect &clip)
{
    if (full.isEmpty() || std::max(full.width(), full.height()) > MaxPdfFullSide)
        return {};
    const QRect page(QPoint(0, 0), full);
    const QRect wanted = clip.isEmpty() ? page : clip.intersected(page);
    if (wanted.isEmpty() || qint64(wanted.width()) * wanted.height() > MaxPdfClipPixels)
        return {};
    return wanted;
}

QImage PdfReader::onWhite(const QImage &rendered, const QSize &size)
{
    if (rendered.isNull() || size.isEmpty())
        return {};
    // pdfium leaves what a page does not paint transparent; poppler's resolution rounds.
    QImage out(size, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (rendered.size() == size)
        painter.drawImage(0, 0, rendered);
    else
        painter.drawImage(QRect(QPoint(0, 0), size), rendered);
    painter.end();
    return out;
}

PdfTextSelection PdfReader::selectText(int page, QPointF from, QPointF to)
{
    Q_UNUSED(page);
    Q_UNUSED(from);
    Q_UNUSED(to);
    return {};
}

PdfTextSelection PdfReader::selectRange(int page, int start, int length)
{
    Q_UNUSED(page);
    Q_UNUSED(start);
    Q_UNUSED(length);
    return {};
}

QList<PdfOutlineItem> PdfReader::outline()
{
    return {};
}

QList<PdfLink> PdfReader::links(int page)
{
    Q_UNUSED(page);
    return {};
}

PdfInfo pdfInfo(const QString &file, bool pageSizes)
{
    PdfInfo info;
    const std::unique_ptr<PdfReader> reader = PdfReader::open(file, &info);
    if (!reader)
        return info;
    if (pageSizes) {
        info.pagePoints.clear();
        info.pagePoints.reserve(info.pages);
        for (int i = 1; i <= info.pages; ++i)
            info.pagePoints.append(reader->pageSize(i));
    }
    return info;
}

QImage renderPdfPage(const QString &file, int page, int maxSide, QString *error)
{
    PdfInfo info;
    const std::unique_ptr<PdfReader> reader = PdfReader::open(file, &info);
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return QImage();
    };
    if (!reader)
        return fail(info.error);
    if (page < 1 || page > reader->pageCount())
        return fail(u"No page %1"_s.arg(page));
    const QSize size = pdfPagePixels(reader->pageSize(page), maxSide);
    if (size.isEmpty())
        return fail(u"Page %1 has no size"_s.arg(page));
    const QImage image = reader->render(page, size);
    return image.isNull() ? fail(u"Page %1 cannot be rendered"_s.arg(page)) : image;
}

} // namespace brulion::platform
