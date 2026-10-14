// Internal token helpers of the markdown module.
#pragma once

#include "markdown/lines_p.h"
#include "markdown/tokens.h"

namespace brulion::markdown::detail {

/// Remove [from, to) ranges from `text`, also dropping the whitespace that separated each
/// removed range from the preceding text (or, at a line start, the whitespace after it), so
/// "a 📅 2026-10-15 b" becomes "a b". Ranges may be unsorted; overlapping ranges are merged.
QString removeRanges(QStringView text, QList<Range> ranges);

/// `text` with trailing whitespace (any Unicode space) removed.
QStringView trimEnd(QStringView text);

/// Code point at `i` (surrogate pairs combined) and its length in UTF-16 units.
struct CodePoint {
    char32_t value = 0;
    int length = 0;
};
CodePoint codePointAt(QStringView s, qsizetype i);

} // namespace brulion::markdown::detail
