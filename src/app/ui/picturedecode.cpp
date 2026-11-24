#include "picturedecode.h"

#include <algorithm>
#include <cmath>

namespace brulion::ui {

int PictureDecode::sideFor(qreal shownWidth, qreal shownHeight, qreal devicePixelRatio)
{
    const qreal ratio = devicePixelRatio > 0 && std::isfinite(devicePixelRatio) ? devicePixelRatio : 1;
    // (fmax: a side that is no number counts as none.)
    const qreal longer = std::fmax(shownWidth, shownHeight) * ratio;
    if (!(longer > MinSide)) // (also both no numbers)
        return MinSide;
    if (longer >= MaxSide)
        return MaxSide;
    const int pixels = int(std::ceil(longer));
    const int step = pixels <= 512 ? 64 : 128;
    return std::min(MaxSide, (pixels + step - 1) / step * step);
}

} // namespace brulion::ui
