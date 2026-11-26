#pragma once

// Segmentation over a whole paragraph at once, for layout (internal to the text area).

#include "text/segmentation.h"

#include <string_view>
#include <vector>

namespace brulion::atrament::detail {

// Every grapheme cluster boundary in order: 0, …, text.size() (just {0} for empty text).
void graphemeBoundaries(std::u16string_view text, std::vector<int> &out);
// lineBreaks() given the boundaries above.
void lineBreaks(std::u16string_view text, const std::vector<int> &graphemes, std::vector<BreakKind> &out);

} // namespace brulion::atrament::detail
