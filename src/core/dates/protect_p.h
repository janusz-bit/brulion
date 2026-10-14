// Internal to the dates module: spans of an input in which natural-language dates must never be
// recognised — URLs, e-mail addresses, wiki links/embeds, Markdown links, inline code, tags,
// dates that already are canonical Brulion tokens ("📅 2026-10-15", "⏰! 2026-10-15 06:30",
// "(@2026-10-15 09:00)", Logseq "SCHEDULED: <2026-10-15 Thu 10:00 .+1w>"), recurrence rules
// ("🔁 co tydzień w sobotę": the weekday belongs to the rule) and property lines
// ("termin:: 2026-10-15"). Callers strip a recognised phrase from the text (quick capture), so a
// match inside "https://blog.pl/2026/10/15/post" or "[[Spotkanie w piątek]]" would corrupt it.
//
// Every pattern is linear: repeated character classes stop at the next opening delimiter, so
// unclosed brackets/backticks cannot cause quadratic rescans; e-mail addresses are found by
// expanding around each '@' instead of a regex; a 🔁 rule is looked for in a bounded window.
#pragma once

#include <QBitArray>
#include <QStringView>

namespace brulion::dates::detail {

/// Mask over `folded` (text after text::foldKey): bit i is set when UTF-16 unit i belongs to a
/// URL, link, code span, tag, e-mail address, canonical date token, 🔁 rule or property line.
QBitArray protectedMask(QStringView folded);

} // namespace brulion::dates::detail
