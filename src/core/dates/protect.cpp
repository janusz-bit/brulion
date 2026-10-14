#include "dates/protect_p.h"

#include "dates/recurrence.h"
#include "text/text.h"

#include <QList>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dates::detail {

namespace {

/// Protected-span patterns (they run on folded, lower-case text).
const QList<QRegularExpression> &patterns()
{
    static const QList<QRegularExpression> list = {
        // Bare URLs: "https://…", "mailto:…" and "www.…" up to the next whitespace.
        text::unicodeRegex(uR"((?<![\p{L}\p{N}])(?:https?|ftp|file|mailto):[^\s<>]*)"_s),
        text::unicodeRegex(uR"((?<![\p{L}\p{N}.])www\.[^\s<>]*)"_s),
        // Wiki links and embeds ("[[Page]]", "![[Page#^id]]", the tag form "#[[multi word]]").
        text::unicodeRegex(uR"(!?\[\[[^\[\]\n]*\]\])"_s),
        // Markdown links / images: "[text](target)".
        text::unicodeRegex(uR"(!?\[[^\[\]\n]*\]\([^()\s]*\))"_s),
        // Inline code.
        text::unicodeRegex(uR"(`[^`\n]*`)"_s),
        // Tags (SYNTAX §3): after start, whitespace or "("; not all digits ("#1" is no tag).
        text::unicodeRegex(uR"((?<![^\s(])#(?![\p{N}]+(?![\p{L}\p{N}_/-]))[\p{L}\p{N}_/-]+)"_s),
        // Canonical date tokens: 📅 ⏳ 🛫 ➕ ✅ ❌ ⏰ (⏰! alarm), optional " HH:mm".
        text::unicodeRegex(
            uR"([\x{1F4C5}\x{23F3}\x{1F6EB}\x{2795}\x{2705}\x{274C}\x{23F0}]\x{FE0F}?!?[ \t]*[0-9]{4}-[0-9]{2}-[0-9]{2}(?:[ Tt][0-9]{1,2}:[0-9]{2})?)"_s),
        // obsidian-reminder plugin: "(@2026-10-15 09:00)".
        text::unicodeRegex(uR"(\(@[0-9]{4}-[0-9]{2}-[0-9]{2}(?:[ Tt][0-9]{1,2}:[0-9]{2})?\))"_s),
        // Logseq / org-mode timestamps (SYNTAX §5): "SCHEDULED: <2026-10-15 Thu 10:00 .+1w>",
        // "DEADLINE: <2026-10-20 Tue>", inactive "[2026-10-14 Wed 09:12]" (always with a weekday,
        // so a bracketed date in prose, "[2026-10-15]", is still input).
        text::unicodeRegex(uR"(<[0-9]{4}-[0-9]{2}-[0-9]{2}(?:[ \t][^<>\n]*)?>)"_s),
        text::unicodeRegex(uR"(\[[0-9]{4}-[0-9]{2}-[0-9]{2}[ \t][^\[\]\n]*\])"_s),
        // Block / page property lines (SYNTAX §4): "termin:: 2026-10-15", "  - status:: jutro"
        // ("std::sort" is no property: "::" must be followed by whitespace or the line end).
        text::unicodeRegex(uR"(^[ \t]*(?:[-*+][ \t]+)?[a-z0-9_-]+::(?=[ \t\r\n]|\z)[^\r\n]*)"_s,
                           QRegularExpression::MultilineOption),
    };
    return list;
}

bool isEmailLocalChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'.' || c == u'_' || c == u'%' || c == u'+' || c == u'-';
}

bool isEmailDomainChar(QChar c)
{
    return c.isLetterOrNumber() || c == u'.' || c == u'-';
}

/// Mark e-mail addresses ("jan.jutro@firma.pl"). Linear: expansions stop at the next '@'.
void markEmails(QStringView text, QBitArray &mask)
{
    for (qsizetype at = text.indexOf(u'@'); at >= 0; at = text.indexOf(u'@', at + 1)) {
        qsizetype start = at;
        while (start > 0 && isEmailLocalChar(text.at(start - 1)))
            --start;
        qsizetype end = at + 1;
        while (end < text.size() && isEmailDomainChar(text.at(end)))
            ++end;
        // Trailing dots end the sentence, not the domain.
        while (end > at + 1 && text.at(end - 1) == u'.')
            --end;
        // A domain needs a dot ("a@b" is no address).
        if (start < at && text.sliced(at + 1, end - at - 1).contains(u'.'))
            mask.fill(true, start, end);
    }
}

/// The Obsidian Tasks recurrence signifier 🔁 (U+1F501, a surrogate pair in UTF-16).
constexpr QStringView RepeatMark = u"\U0001F501";
/// Longest recurrence rule looked at after 🔁 ("co 99999 tygodni w poniedziałek, wtorek, środę,
/// czwartek, piątek, sobotę, niedzielę po wykonaniu" has ~100 units) and its most words.
constexpr qsizetype MaxRuleLength = 200;
constexpr qsizetype MaxRuleWords = 24;

/// A rule ends at a line break or at the next emoji (📅 ⏳ ✅ ⏰ 🔁 🔺 … are other task tokens).
bool endsRecurrenceRule(QChar c)
{
    return c == u'\n' || c == u'\r' || c.isSurrogate() || c.category() == QChar::Symbol_Other;
}

/// Ends (exclusive) of the words after a 🔁 that may still belong to its rule, shortest first.
QList<qsizetype> ruleWordEnds(QStringView text, qsizetype ruleStart)
{
    QList<qsizetype> ends;
    const qsizetype limit = std::min(text.size(), ruleStart + MaxRuleLength);
    for (qsizetype i = ruleStart; i < limit && ends.size() < MaxRuleWords; ++i) {
        const QChar c = text.at(i);
        if (endsRecurrenceRule(c))
            break;
        if (c.isSpace())
            continue;
        // A word cut off by `limit` is no candidate ("every weekday" must not end as "every week").
        const bool wordEnds = i + 1 == text.size() || text.at(i + 1).isSpace() || endsRecurrenceRule(text.at(i + 1));
        if (wordEnds)
            ends.append(i + 1);
    }
    return ends;
}

/// Mark "🔁 <rule>" (SYNTAX §5/§7): the weekday in "🔁 co tydzień w sobotę" belongs to the rule and
/// is no due date — stripping it would silently change the rule. The span is the emoji plus the
/// longest run of words after it that parses as a rule, so a phrase typed after the rule stays
/// findable ("🔁 co tydzień jutro" -> "jutro"). Bounded work per emoji, hence linear overall.
void markRecurrenceRules(QStringView text, QBitArray &mask)
{
    for (qsizetype at = text.indexOf(RepeatMark); at >= 0; at = text.indexOf(RepeatMark, at + RepeatMark.size())) {
        const qsizetype ruleStart = at + RepeatMark.size();
        qsizetype end = ruleStart;
        const QList<qsizetype> ends = ruleWordEnds(text, ruleStart);
        for (auto it = ends.crbegin(); it != ends.crend(); ++it) {
            if (parseRecurrence(text.sliced(ruleStart, *it - ruleStart))) {
                end = *it;
                break;
            }
        }
        mask.fill(true, at, end);
    }
}

} // namespace

QBitArray protectedMask(QStringView folded)
{
    QBitArray mask(folded.size());
    for (const QRegularExpression &re : patterns()) {
        auto it = re.globalMatchView(folded);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (m.capturedLength() > 0)
                mask.fill(true, m.capturedStart(), m.capturedEnd());
        }
    }
    markEmails(folded, mask);
    markRecurrenceRules(folded, mask);
    return mask;
}

} // namespace brulion::dates::detail
