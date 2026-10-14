#include "dates/recurrence.h"

#include "dates/local.h"
#include "dates/vocabulary_p.h"
#include "text/text.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dates {

namespace {

using Freq = RecurrenceRule::Freq;
using detail::collapseSpaces;

/// Month-day spec value meaning "the last day of the month".
constexpr int LastDay = 0;
/// Safety cap on loop iterations of occurrencesBetween (results are capped separately).
constexpr int MaxSteps = 200'000;

const QList<int> &workdays()
{
    static const QList<int> days = {Qt::Monday, Qt::Tuesday, Qt::Wednesday, Qt::Thursday, Qt::Friday};
    return days;
}

/// Rules may come from untyped sources (JSON, UI drafts): anything else is "no rule".
bool hasKnownFreq(const RecurrenceRule &rule)
{
    switch (rule.freq) {
    case Freq::Day:
    case Freq::Week:
    case Freq::Month:
    case Freq::Year:
        return true;
    }
    return false;
}

QRegularExpression anchored(const QString &pattern)
{
    return text::unicodeRegex(u"^(?:"_s + pattern + uR"()\z)"_s);
}

// =============================================================================================
// Parsing
// =============================================================================================

bool isRuleSeparator(QChar c)
{
    return c == u' ' || c == u',' || c == u';';
}

QString trimEndSeparators(QStringView s)
{
    qsizetype end = s.size();
    while (end > 0 && isRuleSeparator(s.at(end - 1)))
        --end;
    return s.first(end).toString();
}

/// Drop leading whitespace and 🔁/🔂 marks.
QStringView stripLeadingRepeatMarks(QStringView s)
{
    qsizetype i = 0;
    while (i < s.size()) {
        if (s.at(i).isSpace()) {
            ++i;
            continue;
        }
        if (s.at(i).isHighSurrogate() && i + 1 < s.size() && s.at(i + 1).isLowSurrogate()) {
            const char32_t cp = QChar::surrogateToUcs4(s.at(i), s.at(i + 1));
            if (cp == 0x1F501 || cp == 0x1F502) {
                i += 2;
                continue;
            }
        }
        break;
    }
    return s.sliced(i);
}

struct Body {
    QString text;
    bool whenDone = false;
};

/// Split "<body> when done" (folded, whitespace-collapsed) into the body and the flag.
Body splitWhenDone(const QString &s)
{
    static const QStringList suffixes = {u"when done"_s, u"po wykonaniu"_s, u"po ukonczeniu"_s, u"od wykonania"_s,
                                         u"od ukonczenia"_s};
    for (const QString &suffix : suffixes) {
        if (!s.endsWith(suffix))
            continue;
        const qsizetype at = s.size() - suffix.size();
        if (at > 0 && !isRuleSeparator(s.at(at - 1)))
            return {s, false};
        return {trimEndSeparators(QStringView(s).first(at)), true};
    }
    return {s, false};
}

const QHash<QString, Freq> &unitWords()
{
    static const QHash<QString, Freq> map = {
        {u"day"_s, Freq::Day},        {u"days"_s, Freq::Day},        {u"dzien"_s, Freq::Day},
        {u"dni"_s, Freq::Day},        {u"dnia"_s, Freq::Day},        {u"week"_s, Freq::Week},
        {u"weeks"_s, Freq::Week},     {u"tydzien"_s, Freq::Week},    {u"tygodnie"_s, Freq::Week},
        {u"tygodni"_s, Freq::Week},   {u"tygodnia"_s, Freq::Week},   {u"month"_s, Freq::Month},
        {u"months"_s, Freq::Month},   {u"miesiac"_s, Freq::Month},   {u"miesiace"_s, Freq::Month},
        {u"miesiecy"_s, Freq::Month}, {u"miesiaca"_s, Freq::Month},  {u"year"_s, Freq::Year},
        {u"years"_s, Freq::Year},     {u"rok"_s, Freq::Year},        {u"roku"_s, Freq::Year},
        {u"lata"_s, Freq::Year},      {u"lat"_s, Freq::Year},
    };
    return map;
}

struct Adverb {
    Freq freq;
    int interval;
};

/// Single-word rules ("daily", "codziennie", "kwartalnie").
const QHash<QString, Adverb> &adverbs()
{
    static const QHash<QString, Adverb> map = {
        {u"daily"_s, {Freq::Day, 1}},          {u"codziennie"_s, {Freq::Day, 1}},
        {u"dziennie"_s, {Freq::Day, 1}},       {u"weekly"_s, {Freq::Week, 1}},
        {u"cotygodniowo"_s, {Freq::Week, 1}},  {u"tygodniowo"_s, {Freq::Week, 1}},
        {u"fortnightly"_s, {Freq::Week, 2}},   {u"biweekly"_s, {Freq::Week, 2}},
        {u"monthly"_s, {Freq::Month, 1}},      {u"comiesiecznie"_s, {Freq::Month, 1}},
        {u"miesiecznie"_s, {Freq::Month, 1}},  {u"quarterly"_s, {Freq::Month, 3}},
        {u"kwartalnie"_s, {Freq::Month, 3}},   {u"cokwartalnie"_s, {Freq::Month, 3}},
        {u"semiannually"_s, {Freq::Month, 6}}, {u"polrocznie"_s, {Freq::Month, 6}},
        {u"copolrocznie"_s, {Freq::Month, 6}}, {u"yearly"_s, {Freq::Year, 1}},
        {u"annually"_s, {Freq::Year, 1}},      {u"corocznie"_s, {Freq::Year, 1}},
        {u"rocznie"_s, {Freq::Year, 1}},
    };
    return map;
}

bool isAsciiDigits(QStringView s)
{
    return !s.isEmpty() && std::all_of(s.begin(), s.end(), [](QChar c) { return c >= u'0' && c <= u'9'; });
}

/// Interval word: digits (optionally with an English ordinal suffix, "2nd"), number words
/// ("dwa", "two") or ordinals ("drugi", "second", "other"). Absent = 1. Null when unknown or
/// above MaxRecurrenceInterval.
std::optional<int> parseCount(QStringView word, bool present)
{
    if (!present)
        return 1;
    QStringView digits = word;
    for (const QStringView suffix : {QStringView(u"st"), QStringView(u"nd"), QStringView(u"rd"), QStringView(u"th")}) {
        if (digits.endsWith(suffix)) {
            digits.chop(2);
            break;
        }
    }
    qint64 n = -1;
    if (isAsciiDigits(digits)) {
        bool ok = false;
        n = digits.toLongLong(&ok);
        if (!ok)
            return std::nullopt;
    } else {
        const QString key = word.toString();
        n = detail::numberWords().value(key, detail::ordinalWords().value(key, -1));
        if (n < 0)
            return std::nullopt;
    }
    return n <= MaxRecurrenceInterval ? std::optional<int>(int(n)) : std::nullopt;
}

QList<int> weekdayListFolded(QStringView folded)
{
    static const QRegularExpression leadingPrep = text::unicodeRegex(u"^(?:on|w|we) "_s);
    static const QRegularExpression itemPrep = text::unicodeRegex(uR"(^(?:on|w|we|the)\s+)"_s);
    static const QRegularExpression separators =
        text::unicodeRegex(uR"(\s*(?:,|&|\+|/)\s*|\s+(?:i|oraz|and|lub|or)\s+)"_s);

    // Collapse whitespace first so the separator pattern never rescans long runs.
    QString s = collapseSpaces(folded).trimmed();
    s.remove(leadingPrep);
    if (s.isEmpty())
        return {};
    QList<int> days;
    for (const QString &raw : s.split(separators)) {
        QString item = raw.trimmed();
        item.remove(itemPrep);
        if (item.endsWith(u'.'))
            item.chop(1);
        const auto word = detail::lookupWeekday(item);
        if (item.isEmpty() || !word)
            return {};
        if (!days.contains(word->day))
            days.append(word->day);
    }
    std::sort(days.begin(), days.end());
    return days;
}

struct MonthDayTail {
    bool last = false;
    int day = 0;
};

/// The "on the 15th" / "15." / "ostatniego" tail of a monthly rule.
std::optional<MonthDayTail> parseMonthDayTail(QStringView tail)
{
    static const QRegularExpression lastRe = anchored(
        uR"((?:(?:on|w)\s+)?(?:the\s+)?(?:ostatni(?:ego|m)?|last)(?:\s+(?:day|dnia|dzien|dniu))?(?:\s+(?:of the month|miesiaca))?)"_s);
    static const QRegularExpression dayRe = anchored(
        uR"((?:on\s+)?(?:the\s+)?(?:(?:dnia|w dniu)\s+)?([0-9]{1,2})(?:st|nd|rd|th|\.|-go|-ego|-tego)?(?:\s+(?:dnia|day))?(?:\s+(?:of the month|miesiaca))?)"_s);
    const QString t = tail.trimmed().toString();
    if (lastRe.match(t).hasMatch())
        return MonthDayTail{true, 0};
    const auto m = dayRe.match(t);
    if (!m.hasMatch())
        return std::nullopt;
    const int n = m.capturedView(1).toInt();
    return n >= 1 && n <= 31 ? std::optional<MonthDayTail>(MonthDayTail{false, n}) : std::nullopt;
}

/// A rule from a base frequency and an optional tail (weekday list / month day).
std::optional<RecurrenceRule> withTail(Freq freq, int interval, QStringView rawTail)
{
    QStringView tail = rawTail.trimmed();
    if (tail.startsWith(u','))
        tail = tail.sliced(1).trimmed();
    if (interval < 1)
        return std::nullopt;
    RecurrenceRule rule;
    rule.freq = freq;
    rule.interval = interval;
    if (tail.isEmpty())
        return rule;
    if (freq == Freq::Week) {
        rule.weekdays = weekdayListFolded(tail);
        return rule.weekdays.isEmpty() ? std::nullopt : std::optional<RecurrenceRule>(rule);
    }
    if (freq == Freq::Month) {
        const auto md = parseMonthDayTail(tail);
        if (!md)
            return std::nullopt;
        if (md->last)
            rule.lastDayOfMonth = true;
        else
            rule.monthDay = md->day;
        return rule;
    }
    return std::nullopt;
}

/// Interval group: digits ("2", "2nd") or a number/ordinal word (never a unit word).
QString countPattern()
{
    return uR"(([0-9]+(?:st|nd|rd|th)?|%1))"_s.arg(
        text::regexAlternation(detail::numberWords().keys() + detail::ordinalWords().keys()));
}

std::optional<RecurrenceRule> parseWorkdays(const QString &s)
{
    static const QList<QRegularExpression> fixed = {
        anchored(u"every (?:weekday|business day|working day|workday)"_s),
        anchored(u"(?:on )?weekdays"_s),
        anchored(u"(?:w )?dni (?:robocze|powszednie)"_s),
        anchored(u"(?:co|kazdego) dni(?:a|e)? (?:robocz(?:y|ego|e)|powszedni(?:ego|e)?)"_s),
        anchored(u"co dzien (?:roboczy|powszedni)"_s),
        anchored(u"(?:od )?poniedzialku do piatku"_s),
        anchored(u"(?:from )?monday (?:to|through|-) friday"_s),
    };
    static const QRegularExpression interval = anchored(
        u"(?:every|co) %1 (?:weekdays|business days|working days|dni robocze|dni roboczych)"_s.arg(countPattern()));

    RecurrenceRule rule;
    rule.freq = Freq::Day;
    rule.weekdaysOnly = true;
    if (std::any_of(fixed.cbegin(), fixed.cend(), [&s](const QRegularExpression &re) { return re.match(s).hasMatch(); }))
        return rule;
    const auto m = interval.match(s);
    if (m.hasMatch()) {
        const auto n = parseCount(m.capturedView(1), true);
        if (n && *n >= 1) {
            rule.interval = *n;
            return rule;
        }
    }
    return std::nullopt;
}

/// The rule body (folded, whitespace-collapsed, without the "when done" suffix).
std::optional<RecurrenceRule> parseBody(const QString &s)
{
    static const QString unit =
        u"(days?|weeks?|months?|years?|dzien|dni|dnia|tydzien|tygodnie|tygodni|tygodnia|miesiac|miesiace|miesiecy|miesiaca|rok|roku|lata|lat)"_s;
    // "every 2 weeks on …", "co 3 dni", "co drugi tydzien w piatek", "kazdy miesiac 15."
    static const QRegularExpression unitRule =
        anchored(u"(?:every|co|kazd(?:y|a|e|ego)|each)(?: %1)? %2(?![a-z])(.*)"_s.arg(countPattern(), unit));
    // "every quarter", "co 2 kwartaly", "co kwartal 15." -> months x 3.
    static const QRegularExpression quarterRule = anchored(
        u"(?:every|co|kazd(?:y|ego)|each)(?: %1)? (?:quarters?|kwartal(?:y|ow|u)?)(?![a-z])(.*)"_s.arg(countPattern()));
    // "every half year", "co pol roku" (+ month-day tail) -> every 6 months.
    static const QRegularExpression halfYearRule =
        anchored(u"(?:every|co|each) (?:half(?: a)?[ -]year|pol roku)(?![a-z])(.*)"_s);
    static const QRegularExpression eachRule = anchored(u"kazdego (dnia|tygodnia|miesiaca|roku)(?![a-z])(.*)"_s);
    static const QRegularExpression adverbRule = anchored(u"([a-z]+)(?![a-z])(.*)"_s);
    // "every monday and friday", "co poniedzialek", "w kazdy piatek", "w poniedzialki i czwartki",
    // "mondays"; after 🔁 a bare weekday list ("pn, pt") is read as weekly on those days too.
    static const QRegularExpression weekdayRule =
        anchored(uR"((?:(?:every|each|co|(?:w |we )?kazd(?:y|a|e|ego)|w|we|on)\s+)?(.+))"_s);

    if (auto workdayRule = parseWorkdays(s))
        return workdayRule;

    auto m = unitRule.match(s);
    if (m.hasMatch()) {
        const auto freq = unitWords().constFind(m.captured(2));
        const auto n = parseCount(m.capturedView(1), m.hasCaptured(1));
        if (freq != unitWords().cend() && n)
            return withTail(*freq, *n, m.capturedView(3));
    }
    m = quarterRule.match(s);
    if (m.hasMatch()) {
        const auto n = parseCount(m.capturedView(1), m.hasCaptured(1));
        if (n && 3LL * *n <= MaxRecurrenceInterval)
            return withTail(Freq::Month, 3 * *n, m.capturedView(2));
    }
    m = halfYearRule.match(s);
    if (m.hasMatch())
        return withTail(Freq::Month, 6, m.capturedView(1));
    m = eachRule.match(s);
    if (m.hasMatch()) {
        const auto freq = unitWords().constFind(m.captured(1));
        if (freq != unitWords().cend())
            return withTail(*freq, 1, m.capturedView(2));
    }
    m = adverbRule.match(s);
    if (m.hasMatch()) {
        const auto adverb = adverbs().constFind(m.captured(1));
        if (adverb != adverbs().cend())
            return withTail(adverb->freq, adverb->interval, m.capturedView(2));
    }
    m = weekdayRule.match(s);
    if (m.hasMatch()) {
        RecurrenceRule rule;
        rule.freq = Freq::Week;
        rule.weekdays = weekdayListFolded(m.capturedView(1));
        if (!rule.weekdays.isEmpty())
            return rule;
    }
    return std::nullopt;
}

// =============================================================================================
// Formatting
// =============================================================================================

QList<int> effectiveWeekdays(const RecurrenceRule &r)
{
    if (!r.weekdays.isEmpty())
        return r.weekdays;
    return r.weekdaysOnly ? workdays() : QList<int>();
}

QString formatEnglish(const RecurrenceRule &r)
{
    const int n = r.interval;
    const QString count = QString::number(n);
    switch (r.freq) {
    case Freq::Day:
        if (r.weekdaysOnly)
            return n == 1 ? u"every weekday"_s : u"every %1 weekdays"_s.arg(count);
        return n == 1 ? u"every day"_s : u"every %1 days"_s.arg(count);
    case Freq::Week: {
        const QString base = n == 1 ? u"every week"_s : u"every %1 weeks"_s.arg(count);
        const QList<int> days = effectiveWeekdays(r);
        if (days.isEmpty())
            return base;
        QStringList names;
        for (const int d : days)
            names.append(weekdayName(d, UiLocale::English));
        return base + u" on "_s + names.join(u", "_s);
    }
    case Freq::Month: {
        const QString base = n == 1 ? u"every month"_s : u"every %1 months"_s.arg(count);
        if (r.lastDayOfMonth)
            return base + u" on the last"_s;
        if (r.monthDay)
            return base + u" on the "_s + englishOrdinal(*r.monthDay);
        return base;
    }
    case Freq::Year:
        break;
    }
    return n == 1 ? u"every year"_s : u"every %1 years"_s.arg(count);
}

QString formatPolish(const RecurrenceRule &r)
{
    const int n = r.interval;
    const QString count = QString::number(n);
    switch (r.freq) {
    case Freq::Day:
        if (r.weekdaysOnly)
            return n == 1 ? u"w dni robocze"_s : u"co %1 dni robocze"_s.arg(count);
        return n == 1 ? u"codziennie"_s : u"co %1 dni"_s.arg(count);
    case Freq::Week: {
        const QString base = n == 1 ? u"co tydzień"_s
                                    : u"co %1 %2"_s.arg(count, polishPlural(n, u"tydzień", u"tygodnie", u"tygodni"));
        const QList<int> days = effectiveWeekdays(r);
        if (days.isEmpty())
            return base;
        QStringList names;
        for (const int d : days)
            names.append(polishWeekdayName(d, NameForm::Accusative));
        const QString prep = days.first() == Qt::Tuesday ? u"we"_s : u"w"_s;
        return base + u' ' + prep + u' ' + names.join(u", "_s);
    }
    case Freq::Month: {
        const QString base = n == 1 ? u"co miesiąc"_s
                                    : u"co %1 %2"_s.arg(count, polishPlural(n, u"miesiąc", u"miesiące", u"miesięcy"));
        if (r.lastDayOfMonth)
            return base + u" ostatniego"_s;
        if (r.monthDay)
            return base + u' ' + QString::number(*r.monthDay) + u'.';
        return base;
    }
    case Freq::Year:
        break;
    }
    return n == 1 ? u"co rok"_s : u"co %1 %2"_s.arg(count, polishPlural(n, u"rok", u"lata", u"lat"));
}

// =============================================================================================
// Occurrences
// =============================================================================================

/// Day-of-month / month taken from the series' first date.
struct SeriesContext {
    int day = 1;   ///< day of month to aim for when the rule has no explicit month day
    int month = 1; ///< month for yearly rules
};

SeriesContext contextOf(QDate date)
{
    return {date.day(), date.month()};
}

int monthDaySpec(const RecurrenceRule &r, const SeriesContext &ctx)
{
    return r.lastDayOfMonth ? LastDay : r.monthDay.value_or(ctx.day);
}

/// Concrete day for a month-day spec in a month (clamped; LastDay = last day), or nullopt
/// outside the supported years.
std::optional<QDate> dayIn(qint64 year, int month, int spec)
{
    if (year < MinSupportedYear || year > MaxSupportedYear)
        return std::nullopt;
    const int dim = QDate(int(year), month, 1).daysInMonth();
    return QDate(int(year), month, spec == LastDay ? dim : std::min(spec, dim));
}

/// Move `n` business days (Mon–Fri) forward from `date`.
QDate addBusinessDays(QDate date, int n)
{
    // Whole weeks first (5 business days = 7 calendar days), then the remainder day by day.
    const qint64 weeks = (n - 1) / 5;
    QDate day = date.addDays(weeks * 7);
    int dow = date.dayOfWeek();
    qint64 left = n - weeks * 5;
    while (left > 0) {
        day = day.addDays(1);
        dow = dow % 7 + 1;
        if (dow <= Qt::Friday)
            --left;
    }
    return day;
}

/// The next date of the series strictly after `date`.
std::optional<QDate> step(const RecurrenceRule &r, QDate date, const SeriesContext &ctx)
{
    const int n = r.interval;
    switch (r.freq) {
    case Freq::Day:
        return r.weekdaysOnly ? addBusinessDays(date, n) : date.addDays(n);
    case Freq::Week: {
        const QList<int> days = effectiveWeekdays(r);
        if (days.isEmpty())
            return date.addDays(7LL * n);
        const int dow = date.dayOfWeek();
        const auto later = std::find_if(days.cbegin(), days.cend(), [dow](int d) { return d > dow; });
        if (later != days.cend())
            return date.addDays(*later - dow);
        const QDate nextWeekMonday = date.addDays(1 - dow + 7LL * n);
        return nextWeekMonday.addDays(days.first() - 1);
    }
    case Freq::Month: {
        const int spec = monthDaySpec(r, ctx);
        const auto here = dayIn(date.year(), date.month(), spec);
        if (!here)
            return std::nullopt;
        if (*here > date)
            return here;
        const QDate target = QDate(date.year(), date.month(), 1).addMonths(n);
        return dayIn(target.year(), target.month(), spec);
    }
    case Freq::Year:
        break;
    }
    const auto here = dayIn(date.year(), ctx.month, ctx.day);
    if (!here)
        return std::nullopt;
    if (*here > date)
        return here;
    return dayIn(qint64(date.year()) + n, ctx.month, ctx.day);
}

/// step(), or nullopt when the next date is outside the supported years.
std::optional<QDate> safeStep(const RecurrenceRule &r, QDate date, const SeriesContext &ctx)
{
    const auto next = step(r, date, ctx);
    return next && isSupportedDate(*next) ? next : std::nullopt;
}

/// Business-day series: after the first step every occurrence is a weekday, and 5 steps of n
/// business days are exactly n calendar weeks, so whole blocks of 7·n days can be skipped.
std::optional<QDate> fastForwardBusinessDays(const RecurrenceRule &r, QDate anchor, QDate from,
                                             const SeriesContext &ctx)
{
    const auto first = safeStep(r, anchor, ctx);
    if (!first || *first >= from)
        return std::nullopt;
    const qint64 period = 7LL * r.interval;
    const qint64 k = first->daysTo(from) / period - 1;
    return k >= 1 ? std::optional<QDate>(first->addDays(k * period)) : std::nullopt;
}

/// Jump close to `from` for long series so old anchors don't iterate for years. Returns a series
/// position (not necessarily an occurrence) strictly before `from`, or nullopt to start at the
/// anchor. Only shifts by whole periods, which keeps the series identical.
std::optional<QDate> fastForward(const RecurrenceRule &r, QDate anchor, QDate from, const SeriesContext &ctx)
{
    if (r.freq == Freq::Day && r.weekdaysOnly)
        return fastForwardBusinessDays(r, anchor, from, ctx);
    if (r.freq == Freq::Day || r.freq == Freq::Week) {
        const qint64 period = r.freq == Freq::Day ? r.interval : 7LL * r.interval;
        const qint64 k = anchor.daysTo(from) / period - 1;
        return k >= 1 ? std::optional<QDate>(anchor.addDays(k * period)) : std::nullopt;
    }
    const qint64 periodMonths = r.freq == Freq::Month ? r.interval : 12LL * r.interval;
    const qint64 diffMonths = (qint64(from.year()) - anchor.year()) * 12 + (from.month() - anchor.month());
    const qint64 k = diffMonths / periodMonths - 1;
    if (k < 1)
        return std::nullopt;
    if (r.freq == Freq::Month) {
        const QDate target = QDate(anchor.year(), anchor.month(), 1).addMonths(int(k * periodMonths));
        return dayIn(target.year(), target.month(), monthDaySpec(r, ctx));
    }
    return dayIn(anchor.year() + k * r.interval, ctx.month, ctx.day);
}

} // namespace

// =============================================================================================
// Public API
// =============================================================================================

RecurrenceRule normalizeRule(const RecurrenceRule &rule)
{
    RecurrenceRule out;
    out.freq = rule.freq;
    out.interval = std::clamp(rule.interval, 1, MaxRecurrenceInterval);
    if (rule.freq == Freq::Week) {
        for (const int d : rule.weekdays) {
            if (d >= Qt::Monday && d <= Qt::Sunday && !out.weekdays.contains(d))
                out.weekdays.append(d);
        }
        std::sort(out.weekdays.begin(), out.weekdays.end());
    }
    if (rule.freq == Freq::Month) {
        if (rule.lastDayOfMonth)
            out.lastDayOfMonth = true;
        else if (rule.monthDay && *rule.monthDay >= 1 && *rule.monthDay <= 31)
            out.monthDay = rule.monthDay;
    }
    out.weekdaysOnly =
        rule.weekdaysOnly && (rule.freq == Freq::Day || (rule.freq == Freq::Week && out.weekdays.isEmpty()));
    out.whenDone = rule.whenDone;
    return out;
}

std::optional<RecurrenceRule> parseRecurrence(QStringView input)
{
    // Folding also drops the emoji variation selector (U+FE0F is a combining mark).
    const QString folded = collapseSpaces(stripLeadingRepeatMarks(text::foldKey(input)));
    // Trailing separators are tolerated ("every week on Monday, Friday,").
    const Body body = splitWhenDone(trimEndSeparators(folded).trimmed());
    if (body.text.isEmpty())
        return std::nullopt;
    auto rule = parseBody(body.text);
    if (!rule)
        return std::nullopt;
    rule->whenDone = body.whenDone;
    return normalizeRule(*rule);
}

QString formatRecurrence(const RecurrenceRule &rule, UiLocale locale)
{
    if (!hasKnownFreq(rule))
        return {};
    const RecurrenceRule r = normalizeRule(rule);
    const QString base = locale == UiLocale::Polish ? formatPolish(r) : formatEnglish(r);
    if (!r.whenDone)
        return base;
    return base + (locale == UiLocale::Polish ? u" po wykonaniu"_s : u" when done"_s);
}

QString canonicalRecurrence(QStringView input)
{
    const auto rule = parseRecurrence(input);
    return rule ? formatRecurrence(*rule, UiLocale::English) : QString();
}

QList<int> parseWeekdayList(QStringView input)
{
    return weekdayListFolded(text::foldKey(input));
}

std::optional<RecurrenceRule> repeaterToRule(QStringView repeater)
{
    static const QRegularExpression re = anchored(uR"((\.\+|\+\+|\+)([0-9]+)([A-Za-z]))"_s);
    const auto m = re.match(repeater.trimmed().toString());
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const qint64 interval = m.capturedView(2).toLongLong(&ok);
    if (!ok || interval < 1 || interval > MaxRecurrenceInterval)
        return std::nullopt;
    RecurrenceRule rule;
    const QChar unit = m.capturedView(3).at(0).toLower();
    if (unit == u'd')
        rule.freq = Freq::Day;
    else if (unit == u'w')
        rule.freq = Freq::Week;
    else if (unit == u'm')
        rule.freq = Freq::Month;
    else if (unit == u'y')
        rule.freq = Freq::Year;
    else
        return std::nullopt;
    rule.interval = int(interval);
    rule.whenDone = m.capturedView(1) == u".+";
    return rule;
}

QString ruleToRepeater(const RecurrenceRule &rule)
{
    if (!hasKnownFreq(rule))
        return {};
    const RecurrenceRule r = normalizeRule(rule);
    if (!r.weekdays.isEmpty() || r.monthDay || r.lastDayOfMonth || r.weekdaysOnly)
        return {};
    QChar unit = u'y';
    if (r.freq == Freq::Day)
        unit = u'd';
    else if (r.freq == Freq::Week)
        unit = u'w';
    else if (r.freq == Freq::Month)
        unit = u'm';
    return (r.whenDone ? u".+"_s : u"+"_s) + QString::number(r.interval) + unit;
}

std::optional<DateSpec> nextOccurrence(const RecurrenceRule &rule, const DateSpec &from,
                                       std::optional<QDate> completedOn)
{
    if (!isSupportedDate(from.date) || !hasKnownFreq(rule))
        return std::nullopt;
    const RecurrenceRule r = normalizeRule(rule);
    const bool fromCompletion = r.whenDone && completedOn && isSupportedDate(*completedOn);
    const QDate base = fromCompletion ? *completedOn : from.date;
    const auto next = safeStep(r, base, contextOf(base));
    if (!next)
        return std::nullopt;
    return DateSpec{*next, from.time};
}

QList<DateSpec> occurrencesBetween(const RecurrenceRule &rule, const DateSpec &anchor, QDate from, QDate to, int max)
{
    if (!isSupportedDate(anchor.date) || !from.isValid() || !to.isValid() || from > to || anchor.date > to
        || max <= 0 || !hasKnownFreq(rule))
        return {};
    const RecurrenceRule r = normalizeRule(rule);
    const SeriesContext ctx = contextOf(anchor.date);
    QList<DateSpec> out;
    if (anchor.date >= from)
        out.append(anchor);
    QDate cur = anchor.date;
    if (anchor.date < from) {
        if (const auto ahead = fastForward(r, anchor.date, from, ctx))
            cur = *ahead;
    }
    for (int i = 0; i < MaxSteps && out.size() < max; ++i) {
        const auto next = safeStep(r, cur, ctx);
        if (!next || *next > to || *next <= cur)
            break;
        if (*next >= from)
            out.append(DateSpec{*next, anchor.time});
        cur = *next;
    }
    return out;
}

} // namespace brulion::dates
