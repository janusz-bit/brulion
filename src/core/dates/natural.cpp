// Natural-language dates. How it works: the input is folded (lower-case, no diacritics — users
// may type "srode" for "środę") keeping an offset map back to the original. A list of regex
// matchers (run on the folded text) produce candidate tokens: a date, a time, a time-of-day word
// or an exact relative date-time. Overlaps are resolved longest-first (protected spans such as
// URLs are pre-occupied), adjacent compatible tokens ("jutro" + "o 18") are merged into groups,
// weak tokens ("o 9", "pt", "the 2nd") are kept only in a clear context, and each valid group is
// resolved to a DateSpec. The vocabulary and the matchers live in natural_matchers.cpp.
// Ported from the TypeScript implementation (archive/web-v1 natural.ts).
#include "dates/natural.h"

#include "dates/local.h"
#include "dates/natural_p.h"
#include "dates/protect_p.h"

#include <QBitArray>
#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dates {

namespace {

using detail::Ctx;
using detail::Daypart;
using detail::Match;
using detail::Matcher;
using detail::Meridiem;
using detail::originalSlice;
using detail::skipSpaces;
using detail::TimeSpec;
using detail::TokenValue;
using detail::Weakness;

/// A candidate token: a matcher's value and its span in the folded text.
struct Token {
    TokenValue value;
    qsizetype start = 0;
    qsizetype end = 0;
    int prio = 0;
};

// =============================================================================================
// Times
// =============================================================================================

QTime daypartTime(Daypart part)
{
    switch (part) {
    case Daypart::Morning:
        return QTime(8, 0);
    case Daypart::Forenoon:
        return QTime(10, 0);
    case Daypart::Afternoon:
        return QTime(15, 0);
    case Daypart::Evening:
        return QTime(19, 0);
    case Daypart::Night:
        break;
    }
    return QTime(22, 0);
}

/// Final wall-clock time of a spec, applying am/pm and time-of-day words.
std::optional<QTime> resolveTime(const TimeSpec &spec)
{
    if (spec.midnight)
        return QTime(0, 0);
    if (!spec.hour)
        return spec.daypart ? std::optional<QTime>(daypartTime(*spec.daypart)) : std::nullopt;
    int h = *spec.hour;
    const int min = spec.minute.value_or(0);
    if (spec.meridiem == Meridiem::Pm && h < 12) {
        h += 12;
    } else if (spec.meridiem == Meridiem::Am && h == 12) {
        h = 0;
    } else if (!spec.meridiem && spec.daypart) {
        const Daypart part = *spec.daypart;
        // Hour 0 is unambiguous (24-hour clock): "tonight at 0:00" must not become noon.
        if ((part == Daypart::Afternoon || part == Daypart::Evening) && h >= 1 && h < 12)
            h += 12;
        else if (part == Daypart::Evening && h == 12) // "tonight at 12" = midnight
            h = 0;
        else if (part == Daypart::Night)
            h = h == 12 ? 0 : (h >= 6 && h < 12 ? h + 12 : h);
    }
    if (h < 0 || h > 23 || min < 0 || min > 59)
        return std::nullopt;
    return QTime(h, min);
}

// =============================================================================================
// Context of weak tokens
// =============================================================================================

/// Words after a bare "o 9" / "at 9" that make it read as a time.
const QSet<QString> &hourFollowers()
{
    static const QSet<QString> set = {
        u"w"_s,         u"we"_s,      u"na"_s,       u"do"_s,      u"z"_s,         u"ze"_s,     u"i"_s,
        u"a"_s,         u"oraz"_s,    u"lub"_s,      u"albo"_s,    u"u"_s,         u"przy"_s,   u"pod"_s,
        u"nad"_s,       u"przed"_s,   u"po"_s,       u"dla"_s,     u"przez"_s,     u"ale"_s,    u"bo"_s,
        u"zeby"_s,      u"gdy"_s,     u"jak"_s,      u"jesli"_s,   u"czy"_s,       u"wiec"_s,   u"tez"_s,
        u"juz"_s,       u"trzeba"_s,  u"musze"_s,    u"sie"_s,     u"mam"_s,       u"mamy"_s,   u"bede"_s,
        u"bedzie"_s,    u"bedziemy"_s, u"ide"_s,     u"idziemy"_s, u"wyjazd"_s,    u"wyjscie"_s, u"spotkanie"_s,
        u"in"_s,        u"on"_s,      u"at"_s,       u"with"_s,    u"for"_s,       u"to"_s,     u"and"_s,
        u"or"_s,        u"but"_s,     u"until"_s,    u"by"_s,      u"then"_s,      u"sharp"_s,
    };
    return set;
}

/// Words after a day-of-month ordinal that keep it a date ("on the 15th at 6pm", "15-go w biurze",
/// "do 10-go każdego miesiąca"): the hour followers without English prepositions that often
/// introduce a noun phrase ("the 2nd in line"), plus a few period words.
const QSet<QString> &ordinalFollowers()
{
    static const QSet<QString> set = [] {
        QSet<QString> s = hourFollowers();
        for (const QString &w : {u"in"_s, u"on"_s, u"with"_s, u"for"_s, u"to"_s, u"sharp"_s})
            s.remove(w);
        for (const QString &w : {u"of"_s, u"next"_s, u"this"_s, u"every"_s, u"please"_s, u"kazdego"_s, u"tego"_s,
                                 u"przyszlego"_s, u"nastepnego"_s, u"biezacego"_s, u"miesiaca"_s})
            s.insert(w);
        return s;
    }();
    return set;
}

// =============================================================================================
// Tokenising and grouping
// =============================================================================================

/// Something usable and within the supported year range (e.g. not "za 9999 lat").
bool hasValidValue(const TokenValue &v)
{
    if (v.date && !isSupportedDate(*v.date))
        return false;
    if (v.exact && !isSupportedDate(v.exact->date))
        return false;
    return v.date || v.time || v.exact;
}

bool anyBitSet(const QBitArray &bits, qsizetype from, qsizetype to)
{
    for (qsizetype i = from; i < to; ++i) {
        if (bits.testBit(i))
            return true;
    }
    return false;
}

/// All candidate tokens; overlaps resolved longest first, then leftmost, then matcher order.
/// Protected spans (URLs, links, code, tags, date tokens) are occupied from the start.
QList<Token> collectTokens(const Ctx &ctx)
{
    QList<Token> all;
    const QList<Matcher> &list = detail::naturalMatchers();
    for (int prio = 0; prio < list.size(); ++prio) {
        auto it = list.at(prio).re.globalMatch(ctx.foldedText());
        while (it.hasNext()) {
            const Match m = it.next();
            if (m.capturedLength() == 0)
                continue;
            const auto value = list.at(prio).build(m, ctx);
            if (value && hasValidValue(*value))
                all.append(Token{*value, m.capturedStart(), m.capturedEnd(), prio});
        }
    }
    std::sort(all.begin(), all.end(), [](const Token &a, const Token &b) {
        const qsizetype la = a.end - a.start;
        const qsizetype lb = b.end - b.start;
        if (la != lb)
            return la > lb;
        return a.start != b.start ? a.start < b.start : a.prio < b.prio;
    });
    QBitArray taken = detail::protectedMask(ctx.foldedText());
    QList<Token> picked;
    for (const Token &t : std::as_const(all)) {
        if (anyBitSet(taken, t.start, t.end))
            continue;
        taken.fill(true, t.start, t.end);
        picked.append(t);
    }
    std::sort(picked.begin(), picked.end(), [](const Token &a, const Token &b) { return a.start < b.start; });
    return picked;
}

struct Group {
    qsizetype start = 0;
    qsizetype end = 0;
    int tokenCount = 0;
    Weakness weak = Weakness::None; ///< of the first token (only relevant for single tokens)
    std::optional<QDate> date;
    TimeSpec time;
    bool hasTime = false;
    std::optional<DateSpec> exact;
};

Group newGroup(const Token &t)
{
    Group g;
    g.start = t.start;
    g.end = t.end;
    g.tokenCount = 1;
    g.weak = t.value.weak;
    g.date = t.value.date;
    g.hasTime = t.value.time.has_value();
    g.time = t.value.time.value_or(TimeSpec{});
    g.exact = t.value.exact;
    return g;
}

/// What may separate merged tokens: whitespace with at most one ",", ":" or dash
/// ("jutro: 18:00", "15.10 – 18:30"). Hand-written, so linear on long whitespace runs.
bool isMergeGap(const QString &s, qsizetype from, qsizetype to)
{
    qsizetype i = from;
    while (i < to && s.at(i).isSpace())
        ++i;
    if (i < to) {
        const QChar c = s.at(i);
        if (c == u',' || c == u':' || c == u'–' || c == u'—' || c == u'-')
            ++i;
    }
    while (i < to && s.at(i).isSpace())
        ++i;
    return i == to;
}

bool canMerge(const Group &g, const Token &t, const Ctx &ctx)
{
    if (g.exact || t.value.exact)
        return false;
    if (!isMergeGap(ctx.foldedText(), g.end, t.start))
        return false;
    if (t.value.date && g.date && *t.value.date != *g.date)
        return false;
    if (const auto &tt = t.value.time) {
        if (tt->hour && g.time.hour)
            return false;
        if (tt->daypart && g.time.daypart)
            return false;
    }
    // Must contribute something (two plain dates for the same day are merged too).
    return t.value.time || t.value.date;
}

void merge(Group &g, const Token &t)
{
    g.end = t.end;
    ++g.tokenCount;
    if (!g.date)
        g.date = t.value.date;
    if (const auto &tt = t.value.time) {
        g.hasTime = true;
        if (!g.time.hour)
            g.time.hour = tt->hour;
        if (!g.time.minute)
            g.time.minute = tt->minute;
        if (!g.time.meridiem)
            g.time.meridiem = tt->meridiem;
        if (!g.time.daypart)
            g.time.daypart = tt->daypart;
        g.time.midnight = g.time.midnight || tt->midnight;
    }
}

QList<Group> groupTokens(const QList<Token> &tokens, const Ctx &ctx)
{
    QList<Group> groups;
    for (const Token &t : tokens) {
        if (!groups.isEmpty() && canMerge(groups.last(), t, ctx))
            merge(groups.last(), t);
        else
            groups.append(newGroup(t));
    }
    return groups;
}

/// End of text or punctuation right after a bare hour ("o 9.", "o 9)").
bool punctuationOrEndAfter(const QString &s, qsizetype pos)
{
    const qsizetype i = skipSpaces(s, pos);
    if (i >= s.size())
        return true;
    static const QString punctuation = uR"(,.;:!?)]}"'”’»–—-)"_s;
    return punctuation.contains(s.at(i));
}

/// Whether a single weak token is acceptable in its context (constant work per token).
bool weakTokenOk(const Group &g, const Ctx &ctx)
{
    // The whole input apart from punctuation/whitespace ("pt", "  sob. ").
    if (ctx.firstContent >= g.start && ctx.contentEnd <= g.end)
        return true;
    if (g.weak == Weakness::Alone)
        return false;
    const QString &s = ctx.foldedText();
    if (punctuationOrEndAfter(s, g.end))
        return true;
    const qsizetype wordStart = skipSpaces(s, g.end);
    if (wordStart == g.end)
        return false;
    qsizetype wordEnd = wordStart;
    while (wordEnd < s.size() && s.at(wordEnd).isLetter())
        ++wordEnd;
    if (wordEnd == wordStart)
        return false;
    const QString word = s.sliced(wordStart, wordEnd - wordStart);
    if ((g.weak == Weakness::Ordinal ? ordinalFollowers() : hourFollowers()).contains(word))
        return true;
    // A Polish infinitive verb ("o 18 zadzwonić") — checked on the original text (ć vs c).
    return originalSlice(ctx, wordStart, wordEnd).toString().toLower().normalized(QString::NormalizationForm_C).endsWith(
        u'ć');
}

bool groupValid(const Group &g, const Ctx &ctx)
{
    return g.tokenCount > 1 || g.weak == Weakness::None || weakTokenOk(g, ctx);
}

/// Midnight words and small hours (00:00–05:59) said with "w nocy"/"at night"/"tonight"/
/// "wieczorem" belong to the night after a named day.
bool nightAfter(const TimeSpec &spec, QTime time)
{
    return spec.midnight
        || ((spec.daypart == Daypart::Night || spec.daypart == Daypart::Evening) && time < QTime(6, 0));
}

std::optional<DateSpec> finalize(const Group &g, const Ctx &ctx)
{
    if (g.exact)
        return g.exact;
    std::optional<QTime> time;
    if (g.hasTime) {
        time = resolveTime(g.time);
        if (!time)
            return std::nullopt;
    }
    QDate date;
    if (!g.date) {
        if (!time)
            return std::nullopt;
        date = *time > ctx.nowMinute ? ctx.today : ctx.today.addDays(1);
    } else {
        date = *g.date;
        // "dziś o północy", "jutro o 2 w nocy": the night that follows the named day.
        if (time && nightAfter(g.time, *time))
            date = date.addDays(1);
    }
    if (!isSupportedDate(date))
        return std::nullopt;
    return DateSpec{date, time};
}

bool isNonContent(QChar c)
{
    return c.isSpace() || c.isPunct();
}

Ctx makeContext(QStringView input, const QDateTime &now, Qt::DayOfWeek weekStart, UiLocale locale)
{
    Ctx ctx;
    ctx.folded = text::foldKeyWithMap(input);
    ctx.original = input;
    ctx.now = now;
    ctx.today = now.date();
    ctx.nowMinute = QTime(now.time().hour(), now.time().minute());
    ctx.weekStart = sanitizeWeekStart(int(weekStart));
    ctx.locale = locale;
    const QString &s = ctx.foldedText();
    qsizetype first = 0;
    while (first < s.size() && isNonContent(s.at(first)))
        ++first;
    qsizetype end = s.size();
    while (end > first && isNonContent(s.at(end - 1)))
        --end;
    ctx.firstContent = first;
    ctx.contentEnd = end;
    return ctx;
}

// =============================================================================================
// Stripping
// =============================================================================================

bool isStripSeparator(QChar c)
{
    return c.isSpace() || c == u',' || c == u';' || c == u':' || c == u'–' || c == u'—' || c == u'-';
}

qsizetype skipSeparatorsBack(QStringView s, qsizetype i)
{
    while (i > 0 && isStripSeparator(s.at(i - 1)))
        --i;
    return i;
}

qsizetype skipSeparatorsForward(QStringView s, qsizetype i)
{
    while (i < s.size() && isStripSeparator(s.at(i)))
        ++i;
    return i;
}

/// Closing counterpart of an opening bracket/quote, or a null QChar.
QChar closingBracket(QChar open)
{
    static const QHash<QChar, QChar> pairs = {
        {u'(', u')'}, {u'[', u']'}, {u'{', u'}'},  {u'„', u'”'}, {u'“', u'”'},
        {u'"', u'"'}, {u'\'', u'\''}, {u'«', u'»'}, {u'‘', u'’'},
    };
    return pairs.value(open);
}

/// Drop a leading run of sentence punctuation and the whitespace after it.
QStringView dropLeadingSentencePunctuation(QStringView s)
{
    qsizetype i = 0;
    while (i < s.size() && QStringView(u".!?…").contains(s.at(i)))
        ++i;
    if (i == 0)
        return s;
    while (i < s.size() && s.at(i).isSpace())
        ++i;
    return s.sliced(i);
}

/// Replace every run of two or more whitespace characters by one space.
QString collapseWhitespaceRuns(QStringView s)
{
    QString out;
    out.reserve(s.size());
    for (qsizetype i = 0; i < s.size();) {
        qsizetype j = i;
        while (j < s.size() && s.at(j).isSpace())
            ++j;
        if (j - i >= 2) {
            out += u' ';
            i = j;
        } else {
            out += s.at(i);
            ++i;
        }
    }
    return out;
}

} // namespace

// =============================================================================================
// Public API
// =============================================================================================

QList<NaturalDate> findNaturalDates(QStringView input, const QDateTime &now, Qt::DayOfWeek weekStart, UiLocale locale)
{
    if (input.isEmpty() || !now.isValid() || !isSupportedDate(now.date()))
        return {};
    const Ctx ctx = makeContext(input, now, weekStart, locale);
    QList<NaturalDate> out;
    for (const Group &g : groupTokens(collectTokens(ctx), ctx)) {
        if (!groupValid(g, ctx))
            continue;
        const auto spec = finalize(g, ctx);
        if (!spec)
            continue;
        const qsizetype a = ctx.folded.toOriginal(g.start);
        const qsizetype b = ctx.folded.toOriginal(g.end);
        out.append(NaturalDate{*spec, a, b - a});
    }
    return out;
}

std::optional<NaturalDate> parseNaturalDate(QStringView input, const QDateTime &now, Qt::DayOfWeek weekStart,
                                            UiLocale locale)
{
    const QList<NaturalDate> all = findNaturalDates(input, now, weekStart, locale);
    return all.isEmpty() ? std::nullopt : std::optional<NaturalDate>(all.first());
}

QString stripNaturalDate(QStringView input, const NaturalDate &found)
{
    const qsizetype start = std::clamp<qsizetype>(found.start, 0, input.size());
    const qsizetype end = std::clamp<qsizetype>(found.end(), start, input.size());
    qsizetype cut = skipSeparatorsBack(input, start);
    qsizetype resume = skipSeparatorsForward(input, end);
    if (cut > 0 && resume < input.size()) {
        const QChar close = closingBracket(input.at(cut - 1));
        if (!close.isNull() && close == input.at(resume)) {
            cut = skipSeparatorsBack(input, cut - 1);
            resume = skipSeparatorsForward(input, resume + 1);
        }
    }
    const QStringView before = input.first(cut);
    const QStringView after = input.sliced(resume);
    if (before.isEmpty())
        return dropLeadingSentencePunctuation(after).trimmed().toString();
    if (after.isEmpty())
        return before.trimmed().toString();
    // No space before closing punctuation or after an opening bracket ("Kupić (2 l)").
    const bool glueless = QStringView(u".!?…)]}”»’").contains(after.front())
                       || QStringView(u"([{„“«‘").contains(before.back());
    const QString joined = before.toString() + (glueless ? QString() : u" "_s) + after.toString();
    return collapseWhitespaceRuns(joined).trimmed();
}

} // namespace brulion::dates
