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

} // namespace brulion::platform
