// What every PDF backend shares (pdfpages.h); the backend itself is one of pdfpages_*.cpp.
#include "pdfpages.h"

#include <algorithm>
#include <cmath>

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

} // namespace brulion::platform
