#include "markdown/tokens.h"

#include "dates/local.h"
#include "dates/recurrence.h"
#include "markdown/tokens_p.h"

#include <QRegularExpression>

#include <algorithm>

namespace brulion::markdown {

using namespace detail;
using namespace Qt::StringLiterals;

namespace tokens {

QStringView priorityEmoji(Priority priority)
{
    switch (priority) {
    case Priority::Highest: return PriorityHighest;
    case Priority::High: return PriorityHigh;
    case Priority::Medium: return PriorityMedium;
    case Priority::Low: return PriorityLow;
    case Priority::Lowest: return PriorityLowest;
    case Priority::None: break;
    }
    return {};
}

} // namespace tokens

namespace detail {

CodePoint codePointAt(QStringView s, qsizetype i)
{
    if (i < 0 || i >= s.size())
        return {};
    const QChar c = s[i];
    if (c.isHighSurrogate() && i + 1 < s.size() && s[i + 1].isLowSurrogate())
        return {QChar::surrogateToUcs4(c, s[i + 1]), 2};
    return {c.unicode(), 1};
}

QStringView trimEnd(QStringView text)
{
    qsizetype end = text.size();
    while (end > 0 && text[end - 1].isSpace())
        --end;
    return text.first(end);
}

QString removeRanges(QStringView text, QList<Range> ranges)
{
    if (ranges.isEmpty())
        return text.toString();
    std::ranges::sort(ranges);
    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    for (const Range &r : std::as_const(ranges)) {
        qsizetype from = std::clamp(r.first, last, text.size());
        qsizetype to = std::clamp(r.second, from, text.size());
        if (to <= last)
            continue;
        while (from > last && isSpaceOrTab(text[from - 1]))
            --from;
        const bool atLineStart = from == 0 || isLineBreak(text[from - 1]);
        if (atLineStart) {
            while (to < text.size() && isSpaceOrTab(text[to]))
                ++to;
        }
        out += text.sliced(last, from - last);
        last = to;
    }
    out += text.sliced(last);
    return out;
}

} // namespace detail

namespace {

// Code points of the signifiers (see tokens:: for the glyphs).
constexpr char32_t CpDue = 0x1F4C5;          // 📅
constexpr char32_t CpDueAlt1 = 0x1F4C6;      // 📆
constexpr char32_t CpDueAlt2 = 0x1F5D3;      // 🗓
constexpr char32_t CpScheduled = 0x23F3;     // ⏳
constexpr char32_t CpScheduledAlt = 0x231B;  // ⌛
constexpr char32_t CpStart = 0x1F6EB;        // 🛫
constexpr char32_t CpCreated = 0x2795;       // ➕
constexpr char32_t CpDone = 0x2705;          // ✅
constexpr char32_t CpCancelled = 0x274C;     // ❌
constexpr char32_t CpRecurrence = 0x1F501;   // 🔁
constexpr char32_t CpReminder = 0x23F0;      // ⏰
constexpr char32_t CpHighest = 0x1F53A;      // 🔺
constexpr char32_t CpHigh = 0x23EB;          // ⏫
constexpr char32_t CpMedium = 0x1F53C;       // 🔼
constexpr char32_t CpLow = 0x1F53D;          // 🔽
constexpr char32_t CpLowest = 0x23EC;        // ⏬
constexpr char32_t CpTaskId = 0x1F194;       // 🆔
constexpr char32_t CpDependsOn = 0x26D4;     // ⛔
constexpr char32_t CpOnCompletion = 0x1F3C1; // 🏁
constexpr char16_t VariationSelector16 = 0xFE0F;

/// The kind of a date token's signifier (reminders included).
std::optional<TokenKind> dateEmojiKind(char32_t cp)
{
    switch (cp) {
    case CpDue:
    case CpDueAlt1:
    case CpDueAlt2: return TokenKind::Due;
    case CpScheduled:
    case CpScheduledAlt: return TokenKind::Scheduled;
    case CpStart: return TokenKind::Start;
    case CpCreated: return TokenKind::Created;
    case CpDone: return TokenKind::Done;
    case CpCancelled: return TokenKind::Cancelled;
    case CpReminder: return TokenKind::Reminder;
    default: return std::nullopt;
    }
}

Priority priorityOfEmoji(char32_t cp)
{
    switch (cp) {
    case CpHighest: return Priority::Highest;
    case CpHigh: return Priority::High;
    case CpMedium: return Priority::Medium;
    case CpLow: return Priority::Low;
    case CpLowest: return Priority::Lowest;
    default: return Priority::None;
    }
}

/// Every code point that starts a token (used to stop recurrence rules).
bool isSignifier(char32_t cp)
{
    return dateEmojiKind(cp) || priorityOfEmoji(cp) != Priority::None || cp == CpRecurrence || cp == CpTaskId
           || cp == CpDependsOn || cp == CpOnCompletion;
}

bool isBlockIdChar(QChar c)
{
    return isAsciiLetter(c) || isAsciiDigit(c) || c == u'-';
}

/// Can a token start with `c`? Every signifier is at U+2000 or above.
bool mayStartToken(QChar c)
{
    const char16_t u = c.unicode();
    return u >= 0x2000 || u == u'(' || u == u'[' || u == u'S' || u == u'D';
}

qsizetype skipBlanks(QStringView s, qsizetype i)
{
    while (isSpaceOrTab(charAt(s, i)))
        ++i;
    return i;
}

/// Position after an optional variation selector at `i`.
qsizetype skipVariationSelector(QStringView s, qsizetype i)
{
    return charAt(s, i).unicode() == VariationSelector16 ? i + 1 : i;
}

/// "YYYY-MM-DD" shape at `p` (digits and dashes only; validity checked separately).
bool isDateShapeAt(QStringView s, qsizetype p)
{
    if (p < 0 || p + 10 > s.size())
        return false;
    for (qsizetype k = 0; k < 10; ++k) {
        const QChar c = s[p + k];
        if (k == 4 || k == 7) {
            if (c != u'-')
                return false;
        } else if (!isAsciiDigit(c)) {
            return false;
        }
    }
    return true;
}

/// `\d{1,2}:\d{2}` at `p`: end position or -1.
qsizetype clockEndAt(QStringView s, qsizetype p)
{
    qsizetype i = p;
    while (i - p < 2 && isAsciiDigit(charAt(s, i)))
        ++i;
    if (i == p || charAt(s, i) != u':' || !isAsciiDigit(charAt(s, i + 1)) || !isAsciiDigit(charAt(s, i + 2)))
        return -1;
    return i + 3;
}

bool isDigitOrColon(QChar c)
{
    return isAsciiDigit(c) || c == u':';
}

/// A date with an optional time at `p`: `DATE(?:(?:[ \t]+|T)(\d{1,2}:\d{2}))?(?![\d:])`.
struct DateTimeMatch {
    qsizetype end = -1; ///< end of the whole match, -1 when there is none
    qsizetype timeFrom = -1;
    qsizetype timeTo = -1;
};

DateTimeMatch matchDateTime(QStringView s, qsizetype p)
{
    if (!isDateShapeAt(s, p))
        return {};
    const qsizetype dateEnd = p + 10;
    qsizetype t = -1;
    if (charAt(s, dateEnd) == u'T')
        t = dateEnd + 1;
    else if (isSpaceOrTab(charAt(s, dateEnd)))
        t = skipBlanks(s, dateEnd);
    if (t >= 0) {
        const qsizetype tEnd = clockEndAt(s, t);
        if (tEnd >= 0 && !isDigitOrColon(charAt(s, tEnd)))
            return {tEnd, t, tEnd};
    }
    if (isDigitOrColon(charAt(s, dateEnd)))
        return {};
    return {dateEnd, -1, -1};
}

/// Fill the date / time fields of a date-like token; false when the date is invalid.
bool fillDate(Token &tok, QStringView s, qsizetype dateFrom, const DateTimeMatch &m)
{
    const auto date = dates::parseIsoDate(s.sliced(dateFrom, 10));
    if (!date)
        return false;
    tok.date = *date;
    tok.value = s.sliced(dateFrom, 10).toString();
    tok.valueFrom = dateFrom;
    tok.valueTo = m.end;
    tok.to = m.end;
    if (m.timeFrom >= 0) {
        const auto time = dates::parseClockTime(s.sliced(m.timeFrom, m.timeTo - m.timeFrom));
        if (time) {
            tok.time = time;
        } else {
            // Invalid time ("25:00"): the token ends after the date, the rest stays text.
            tok.to = dateFrom + 10;
            tok.valueTo = tok.to;
        }
    }
    return true;
}

/// Where a recurrence rule text ends: the next token, a tag, a trailing block id, a comment,
/// a planning keyword or a line break.
qsizetype recurrenceStop(QStringView s, qsizetype from)
{
    const qsizetype n = s.size();
    for (qsizetype k = from; k < n; ++k) {
        const QChar c = s[k];
        if (isLineBreak(c))
            return k;
        if (c.isSurrogate() || c.unicode() >= 0x2000) {
            if (isSignifier(codePointAt(s, k).value))
                return k;
            continue;
        }
        switch (c.unicode()) {
        case u'(':
            // \(@\d{4}-
            if (charAt(s, k + 1) == u'@' && isAsciiDigit(charAt(s, k + 2)) && isAsciiDigit(charAt(s, k + 3))
                && isAsciiDigit(charAt(s, k + 4)) && isAsciiDigit(charAt(s, k + 5)) && charAt(s, k + 6) == u'-')
                return k;
            break;
        case u'[':
            if (charAt(s, k + 1) == u'#'
                && (charAt(s, k + 2) == u'A' || charAt(s, k + 2) == u'B' || charAt(s, k + 2) == u'C')
                && charAt(s, k + 3) == u']')
                return k;
            break;
        case u' ':
        case u'\t': {
            const QChar next = charAt(s, k + 1);
            if (next == u'#') {
                // A tag: `#` and a letter or number (Letter_* / Number_* categories), `_` or `[`.
                const char32_t cp = codePointAt(s, k + 2).value;
                if (QChar::isLetterOrNumber(cp) || cp == U'_' || cp == U'[')
                    return k;
            } else if (next == u'^') {
                qsizetype j = k + 2;
                while (isBlockIdChar(charAt(s, j)))
                    ++j;
                if (j > k + 2) {
                    j = skipBlanks(s, j);
                    if (j == n || isLineBreak(s[j]))
                        return k;
                }
            }
            break;
        }
        case u'%':
            if (charAt(s, k + 1) == u'%')
                return k;
            break;
        case u'S':
            if (s.sliced(k).startsWith(u"SCHEDULED:"))
                return k;
            break;
        case u'D':
            if (s.sliced(k).startsWith(u"DEADLINE:"))
                return k;
            break;
        default: break;
        }
    }
    return n;
}

Token recurrenceToken(QStringView s, qsizetype from, qsizetype emojiEnd)
{
    const qsizetype end = recurrenceStop(s, emojiEnd);
    const QStringView raw = s.sliced(emojiEnd, end - emojiEnd);
    const QStringView value = raw.trimmed();
    const qsizetype lead = value.isEmpty() ? 0 : value.data() - raw.data();
    Token tok;
    tok.kind = TokenKind::Recurrence;
    tok.from = from;
    tok.valueFrom = emojiEnd + lead;
    tok.valueTo = tok.valueFrom + value.size();
    tok.to = value.isEmpty() ? emojiEnd : tok.valueTo;
    tok.value = value.toString();
    if (!value.isEmpty())
        tok.canonical = dates::canonicalRecurrence(value);
    return tok;
}

/// Positions cached while scanning forward (keeps hostile input linear).
struct ScanCache {
    qsizetype planningClose = -1; ///< next '>' or line break at or after some position (-1 = unknown)
};

/// The first '>' or line break at or after `from` (or the string length), cached.
qsizetype planningCloseAt(QStringView s, qsizetype from, ScanCache &cache)
{
    if (cache.planningClose >= from)
        return cache.planningClose;
    qsizetype close = from;
    while (close < s.size() && s[close] != u'>' && !isLineBreak(s[close]))
        ++close;
    cache.planningClose = close;
    return close;
}

/// One token starting at `i` (any kind), or nullopt. `next` receives the position where
/// scanning continues (past a syntactically matched but invalid token, too).
std::optional<Token> tokenAt(QStringView s, qsizetype i, qsizetype &next, ScanCache &cache)
{
    next = i + 1;
    const QChar c = s[i];
    if (c == u'(') {
        // \(@(\d{4}-\d{2}-\d{2})(?:[ \t]+(\d{1,2}:\d{2}))?\)
        if (charAt(s, i + 1) != u'@' || !isDateShapeAt(s, i + 2))
            return std::nullopt;
        qsizetype j = i + 12;
        qsizetype timeFrom = -1;
        qsizetype timeTo = -1;
        if (isSpaceOrTab(charAt(s, j))) {
            const qsizetype t = skipBlanks(s, j);
            const qsizetype tEnd = clockEndAt(s, t);
            if (tEnd >= 0 && charAt(s, tEnd) == u')') {
                timeFrom = t;
                timeTo = tEnd;
                j = tEnd;
            }
        }
        if (charAt(s, j) != u')')
            return std::nullopt;
        next = j + 1;
        Token tok;
        tok.kind = TokenKind::ReminderCompat;
        tok.from = i;
        if (!fillDate(tok, s, i + 2, {j, timeFrom, timeTo}))
            return std::nullopt;
        if (timeFrom >= 0 && !tok.time)
            return std::nullopt;
        tok.to = j + 1;
        tok.valueTo = j;
        return tok;
    }
    if (c == u'[') {
        const QChar p = charAt(s, i + 2);
        if (charAt(s, i + 1) != u'#' || (p != u'A' && p != u'B' && p != u'C') || charAt(s, i + 3) != u']')
            return std::nullopt;
        next = i + 4;
        Token tok;
        tok.kind = TokenKind::LogseqPriority;
        tok.from = i;
        tok.to = i + 4;
        tok.priority = p == u'A' ? Priority::High : p == u'B' ? Priority::Medium : Priority::Low;
        return tok;
    }
    if (c == u'S' || c == u'D') {
        const QStringView rest = s.sliced(i);
        const bool scheduled = rest.startsWith(u"SCHEDULED:");
        if (!scheduled && !rest.startsWith(u"DEADLINE:"))
            return std::nullopt;
        const qsizetype open = skipBlanks(s, i + (scheduled ? 10 : 9));
        if (charAt(s, open) != u'<')
            return std::nullopt;
        const qsizetype close = planningCloseAt(s, open + 1, cache);
        if (charAt(s, close) != u'>')
            return std::nullopt;
        next = close + 1;
        const auto ts = parseLogseqTimestamp(s.sliced(open + 1, close - open - 1));
        if (!ts)
            return std::nullopt;
        Token tok;
        tok.kind = TokenKind::Planning;
        tok.from = i;
        tok.to = close + 1;
        tok.valueFrom = open + 1;
        tok.valueTo = close;
        tok.date = ts->date;
        tok.value = ts->date.toString(u"yyyy-MM-dd"_s);
        tok.time = ts->time;
        tok.repeater = ts->repeater;
        tok.field = scheduled ? TokenKind::Scheduled : TokenKind::Due;
        return tok;
    }

    const CodePoint cp = codePointAt(s, i);
    const qsizetype afterEmoji = skipVariationSelector(s, i + cp.length);
    if (const auto kind = dateEmojiKind(cp.value)) {
        const bool alarm = kind == TokenKind::Reminder && charAt(s, afterEmoji) == u'!'; // ⏰!
        const qsizetype dateFrom = skipBlanks(s, alarm ? afterEmoji + 1 : afterEmoji);
        const DateTimeMatch m = matchDateTime(s, dateFrom);
        if (m.end < 0)
            return std::nullopt;
        next = m.end;
        Token tok;
        tok.kind = *kind;
        tok.from = i;
        tok.alarm = alarm;
        if (!fillDate(tok, s, dateFrom, m))
            return std::nullopt;
        return tok;
    }
    if (cp.value == CpRecurrence) {
        Token tok = recurrenceToken(s, i, afterEmoji);
        next = std::max(tok.to, afterEmoji);
        return tok;
    }
    if (const Priority p = priorityOfEmoji(cp.value); p != Priority::None) {
        next = afterEmoji;
        Token tok;
        tok.kind = TokenKind::Priority;
        tok.from = i;
        tok.to = afterEmoji;
        tok.priority = p;
        return tok;
    }
    if (cp.value == CpTaskId || cp.value == CpDependsOn || cp.value == CpOnCompletion) {
        const qsizetype v = skipBlanks(s, afterEmoji);
        qsizetype j = v;
        if (cp.value == CpOnCompletion) {
            while (isAsciiLetter(charAt(s, j)))
                ++j;
        } else {
            while (isTaskIdChar(charAt(s, j)))
                ++j;
            if (cp.value == CpDependsOn && j > v) {
                // (?:[ \t]*,[ \t]*ID)*
                for (;;) {
                    const qsizetype comma = skipBlanks(s, j);
                    if (charAt(s, comma) != u',')
                        break;
                    const qsizetype idStart = skipBlanks(s, comma + 1);
                    qsizetype idEnd = idStart;
                    while (isTaskIdChar(charAt(s, idEnd)))
                        ++idEnd;
                    if (idEnd == idStart)
                        break;
                    j = idEnd;
                }
            }
        }
        if (j == v)
            return std::nullopt;
        next = j;
        Token tok;
        tok.kind = cp.value == CpTaskId      ? TokenKind::TaskId
                   : cp.value == CpDependsOn ? TokenKind::DependsOn
                                             : TokenKind::OnCompletion;
        tok.from = i;
        tok.to = j;
        tok.valueFrom = v;
        tok.valueTo = j;
        tok.value = s.sliced(v, j - v).toString();
        return tok;
    }
    return std::nullopt;
}

} // namespace

bool Token::hasDate() const
{
    switch (kind) {
    case TokenKind::Due:
    case TokenKind::Scheduled:
    case TokenKind::Start:
    case TokenKind::Created:
    case TokenKind::Done:
    case TokenKind::Cancelled:
    case TokenKind::Reminder:
    case TokenKind::ReminderCompat:
    case TokenKind::Planning: return true;
    default: return false;
    }
}

QList<Token> findTokens(QStringView masked)
{
    QList<Token> out;
    if (std::ranges::none_of(masked, mayStartToken)) // a cheap pre-check: most text has no token
        return out;
    ScanCache cache;
    for (qsizetype i = 0; i < masked.size();) {
        if (!mayStartToken(masked[i])) {
            ++i;
            continue;
        }
        qsizetype next = i + 1;
        if (auto tok = tokenAt(masked, i, next, cache))
            out.append(std::move(*tok));
        i = std::max(next, i + 1);
    }
    return out;
}

QString recurrenceOf(const Token &token)
{
    return token.canonical.isEmpty() ? token.value : token.canonical;
}

std::optional<LogseqTimestamp> parseLogseqTimestamp(QStringView inner)
{
    // Trimmed: trailing whitespace changes no match below ((?=\s|$) holds either way).
    const QStringView s = inner.trimmed();
    if (!isDateShapeAt(s, 0))
        return std::nullopt;
    const auto date = dates::parseIsoDate(s.first(10));
    if (!date)
        return std::nullopt;
    LogseqTimestamp out;
    out.date = *date;
    const QStringView rest = s.sliced(10);
    static const QRegularExpression timeRe(u"(?:^|\\s)(\\d{1,2}:\\d{2})(?:-\\d{1,2}:\\d{2})?(?=\\s|$)"_s);
    if (const auto m = timeRe.matchView(rest); m.hasMatch())
        out.time = dates::parseClockTime(m.capturedView(1));
    static const QRegularExpression repeaterRe(u"(?:^|\\s)(\\.\\+|\\+\\+|\\+)(\\d+[hdwmy])(?=\\s|$)"_s,
                                               QRegularExpression::CaseInsensitiveOption);
    if (const auto m = repeaterRe.matchView(rest); m.hasMatch())
        out.repeater = m.captured(1) + m.captured(2);
    return out;
}

std::optional<BlockIdMatch> blockIdAtEnd(QStringView line)
{
    if (!line.contains(u'^'))
        return std::nullopt;
    const QStringView trimmed = trimEndBlanks(line);
    qsizetype j = trimmed.size();
    while (j > 0 && isBlockIdChar(trimmed[j - 1]))
        --j;
    if (j == trimmed.size() || j == 0 || trimmed[j - 1] != u'^')
        return std::nullopt;
    const qsizetype caret = j - 1;
    if (caret > 0 && !isSpaceOrTab(trimmed[caret - 1]))
        return std::nullopt;
    const qsizetype from = caret > 0 ? caret - 1 : 0;
    return BlockIdMatch{trimmed.sliced(j).toString(), from, line.size()};
}

QString standaloneBlockId(QStringView line)
{
    const QStringView t = trimEndBlanks(line.sliced(indentOf(line).chars));
    if (t.size() < 2 || t[0] != u'^' || !isValidBlockId(t.sliced(1)))
        return {};
    return t.sliced(1).toString();
}

bool isValidBlockId(QStringView id)
{
    return !id.isEmpty() && std::ranges::all_of(id, isBlockIdChar);
}

QString stripTokens(QStringView text)
{
    const QString masked = maskInline(text);
    QList<Range> ranges;
    for (const Token &t : findTokens(masked))
        ranges.append({t.from, t.to});
    if (masked.contains(u'^')) {
        for (const text::Line &line : text::splitLines(masked)) {
            if (const auto id = blockIdAtEnd(line.text))
                ranges.append({line.start + id->from, line.start + id->to});
        }
    }
    const QString stripped = removeRanges(text, std::move(ranges));
    QString out;
    out.reserve(stripped.size());
    for (const text::Line &line : text::splitLines(stripped)) {
        out += trimEnd(line.text);
        out += line.eol;
    }
    return out.trimmed();
}

QString formatReminderToken(const DateSpec &at, bool alarm)
{
    return (alarm ? tokens::Alarm : tokens::Reminder).toString() + u' ' + at.toString();
}

} // namespace brulion::markdown
