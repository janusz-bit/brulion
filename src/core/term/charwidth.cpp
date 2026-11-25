#include "charwidth.h"

#include "charwidth_table_p.h"

#include <algorithm>
#include <iterator>

namespace brulion::term {

namespace {

template<std::size_t N>
bool inRanges(const detail::CodeRange (&ranges)[N], char32_t cp)
{
    if (cp < ranges[0].first || cp > ranges[N - 1].last)
        return false;
    const auto *it = std::upper_bound(std::begin(ranges), std::end(ranges), cp,
                                      [](char32_t value, const detail::CodeRange &r) { return value < r.first; });
    return it != std::begin(ranges) && cp <= std::prev(it)->last;
}

} // namespace

int charWidth(char32_t cp)
{
    // The common case first: Latin, Polish and everything else below the first combining mark.
    if (cp < 0x0300)
        return 1;
    if (inRanges(detail::ZeroWidthRanges, cp))
        return 0;
    if (cp >= 0x1100 && inRanges(detail::WideRanges, cp))
        return 2;
    return 1;
}

} // namespace brulion::term
