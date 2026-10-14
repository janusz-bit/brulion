// Internal to the dates module: shared model of the natural-language date parser, split between
// natural.cpp (tokenising, grouping, resolution, public API) and natural_matchers.cpp
// (vocabulary and the regex matchers that produce candidate tokens). Not part of the public API.
#pragma once

#include "dates/names.h"
#include "text/text.h"
#include "types.h"

#include <QDateTime>
#include <QList>
#include <QRegularExpression>
#include <QStringView>

#include <optional>

namespace brulion::dates::detail {

enum class Daypart { Morning, Forenoon, Afternoon, Evening, Night };
enum class Meridiem { Am, Pm };
/// Why a token is ambiguous: Alone = accepted only as the whole input ("pt", "evening");
/// Hour = a bare "o 9" that needs a time-ish context; Ordinal = a day-of-month ordinal that is
/// no date when a noun follows ("the 2nd bug").
enum class Weakness { None, Alone, Hour, Ordinal };

struct TimeSpec {
    std::optional<int> hour;
    std::optional<int> minute;
    std::optional<Meridiem> meridiem;
    std::optional<Daypart> daypart;
    bool midnight = false; ///< a midnight word: with a named day it means the end of that day
};

struct TokenValue {
    std::optional<QDate> date;
    std::optional<TimeSpec> time;
    std::optional<DateSpec> exact; ///< exact date-time of "za 3 godziny"; never merged
    Weakness weak = Weakness::None;
};

/// Parsing context: the folded input with its offset map and the reference date/time.
struct Ctx {
    text::FoldedText folded;
    QStringView original;
    QDateTime now;
    QDate today;
    QTime nowMinute; ///< now without seconds
    Qt::DayOfWeek weekStart = Qt::Monday;
    UiLocale locale = UiLocale::Polish;
    qsizetype firstContent = 0; ///< first folded unit that is not whitespace/punctuation
    qsizetype contentEnd = 0;   ///< just after the last such unit

    const QString &foldedText() const { return folded.text; }
};

using Match = QRegularExpressionMatch;
using Build = std::optional<TokenValue> (*)(const Match &, const Ctx &);

struct Matcher {
    QRegularExpression re;
    Build build = nullptr;
};

/// Original-text slice for a folded range.
inline QStringView originalSlice(const Ctx &ctx, qsizetype start, qsizetype end)
{
    const qsizetype a = ctx.folded.toOriginal(start);
    const qsizetype b = ctx.folded.toOriginal(end);
    return ctx.original.sliced(a, b - a);
}

/// Index of the first non-whitespace character at or after `i`.
inline qsizetype skipSpaces(const QString &s, qsizetype i)
{
    while (i < s.size() && s.at(i).isSpace())
        ++i;
    return i;
}

/// All matchers, in priority order (a lower index wins between equally long candidates).
const QList<Matcher> &naturalMatchers();

} // namespace brulion::dates::detail
