// Vocabulary and regex matchers of the natural-language date parser (see natural.cpp for the
// pipeline). Patterns run on folded, lower-case text; each matcher has a builder that turns a
// regex match into a candidate token or rejects it. Ported from archive/web-v1 natural.ts.
#include "dates/local.h"
#include "dates/natural_p.h"
#include "dates/vocabulary_p.h"

#include <QHash>
#include <QSet>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::dates::detail {

namespace {

enum class WeekdayMode { Next, This, NextWeek, Last };
enum class Unit { Day, Week, Month, Year, Hour, Minute, Quarter };

// =============================================================================================
// Vocabulary (folded)
// =============================================================================================

const QHash<QString, int> &dayWords()
{
    static const QHash<QString, int> words = {
        {u"dzis"_s, 0},
        {u"dzisiaj"_s, 0},
        {u"today"_s, 0},
        {u"jutro"_s, 1},
        {u"tomorrow"_s, 1},
        {u"tmrw"_s, 1},
        {u"pojutrze"_s, 2},
        {u"day after tomorrow"_s, 2},
        {u"the day after tomorrow"_s, 2},
        {u"wczoraj"_s, -1},
        {u"yesterday"_s, -1},
        {u"przedwczoraj"_s, -2},
        {u"day before yesterday"_s, -2},
        {u"the day before yesterday"_s, -2},
        // Genitive forms, only after "do"/"od"/"na": "do jutra".
        {u"jutra"_s, 1},
        {u"pojutrza"_s, 2},
    };
    return words;
}

bool needsPreposition(const QString &dayWord)
{
    return dayWord == u"jutra" || dayWord == u"pojutrza";
}

const QHash<QString, WeekdayMode> &weekdayModes()
{
    static const QHash<QString, WeekdayMode> modes = [] {
        QHash<QString, WeekdayMode> m;
        const auto add = [&m](WeekdayMode mode, const QStringList &words) {
            for (const QString &w : words)
                m.insert(w, mode);
        };
        add(WeekdayMode::NextWeek, {u"przyszly"_s, u"przyszla"_s, u"przyszle"_s, u"przyszlego"_s, u"przyszlej"_s,
                                    u"nastepny"_s, u"nastepna"_s, u"nastepne"_s, u"nastepnego"_s, u"nastepnej"_s,
                                    u"kolejny"_s, u"kolejna"_s, u"kolejne"_s, u"next"_s});
        add(WeekdayMode::This, {u"ten"_s, u"ta"_s, u"te"_s, u"tego"_s, u"tej"_s, u"this"_s, u"coming"_s,
                                u"this coming"_s});
        add(WeekdayMode::Next, {u"najblizszy"_s, u"najblizsza"_s, u"najblizsze"_s, u"najblizszego"_s,
                                u"najblizszej"_s});
        add(WeekdayMode::Last, {u"zeszly"_s, u"zeszla"_s, u"zeszle"_s, u"zeszlego"_s, u"zeszlej"_s, u"ubiegly"_s,
                                u"ubiegla"_s, u"ubiegle"_s, u"ubieglego"_s, u"ubieglej"_s, u"poprzedni"_s,
                                u"poprzednia"_s, u"poprzednie"_s, u"poprzedniego"_s, u"poprzedniej"_s, u"last"_s,
                                u"previous"_s, u"past"_s});
        return m;
    }();
    return modes;
}

const QHash<QString, Unit> &units()
{
    static const QHash<QString, Unit> map = [] {
        QHash<QString, Unit> m;
        const auto add = [&m](Unit unit, const QStringList &words) {
            for (const QString &w : words)
                m.insert(w, unit);
        };
        add(Unit::Day, {u"dzien"_s, u"dni"_s, u"dnia"_s, u"day"_s, u"days"_s, u"d"_s});
        add(Unit::Week, {u"tydzien"_s, u"tygodnie"_s, u"tygodni"_s, u"tygodnia"_s, u"tyg"_s, u"week"_s, u"weeks"_s,
                         u"wk"_s, u"wks"_s});
        add(Unit::Month, {u"miesiac"_s, u"miesiace"_s, u"miesiecy"_s, u"miesiaca"_s, u"mies"_s, u"month"_s,
                          u"months"_s});
        add(Unit::Year, {u"rok"_s, u"lata"_s, u"lat"_s, u"roku"_s, u"year"_s, u"years"_s, u"yr"_s, u"yrs"_s});
        add(Unit::Hour, {u"godzina"_s, u"godzine"_s, u"godziny"_s, u"godzin"_s, u"godz"_s, u"h"_s, u"hour"_s,
                         u"hours"_s, u"hr"_s, u"hrs"_s});
        add(Unit::Minute, {u"minuta"_s, u"minute"_s, u"minuty"_s, u"minut"_s, u"min"_s, u"minutes"_s, u"mins"_s});
        add(Unit::Quarter, {u"kwadrans"_s, u"kwadranse"_s, u"kwadransow"_s, u"kwadransa"_s});
        return m;
    }();
    return map;
}

/// Unit words that may stand without a number, in the case the governing word requires:
/// accusative after "za" and before "temu" ("za tydzień", "tydzień temu"), genitive after
/// "w ciągu" ("w ciągu godziny"). So the idiom "za dnia" (= in daylight) is no offset.
const QSet<QString> &bareUnitsAccusative()
{
    static const QSet<QString> set = {u"dzien"_s, u"tydzien"_s, u"miesiac"_s, u"rok"_s,
                                      u"godzine"_s, u"minute"_s, u"kwadrans"_s};
    return set;
}

const QSet<QString> &bareUnitsGenitive()
{
    static const QSet<QString> set = {u"dnia"_s, u"tygodnia"_s, u"miesiaca"_s, u"roku"_s,
                                      u"godziny"_s, u"minuty"_s, u"kwadransa"_s};
    return set;
}

const QSet<QString> &noBareUnits()
{
    static const QSet<QString> set;
    return set;
}

/// Which bare unit words a relative-offset preposition accepts (English needs a number or "a").
const QSet<QString> &bareUnitsAfter(const QString &preposition)
{
    if (preposition == u"za")
        return bareUnitsAccusative();
    if (preposition == u"w ciagu")
        return bareUnitsGenitive();
    return noBareUnits();
}

/// A fraction of a unit: an exact offset in minutes, or a calendar offset in months.
struct Fraction {
    int minutes = 0;
    int months = 0;
};

const QStringList &fractionWords()
{
    static const QStringList words = {u"pol"_s, u"half an"_s, u"half a"_s, u"poltorej"_s, u"poltora"_s};
    return words;
}

std::optional<Fraction> fractionValue(const QString &word, Unit unit)
{
    const bool pol = word == u"pol";
    if (unit == Unit::Hour) {
        if (pol || word == u"half an")
            return Fraction{30, 0};
        if (word == u"poltorej" || word == u"poltora")
            return Fraction{90, 0};
    } else if (unit == Unit::Year) {
        if (pol || word == u"half a")
            return Fraction{0, 6};
        if (word == u"poltora")
            return Fraction{0, 18};
    } else if (unit == Unit::Day && (pol || word == u"half a")) {
        return Fraction{720, 0};
    }
    return std::nullopt;
}

struct DaypartEntry {
    TimeSpec spec;
    Weakness weak = Weakness::None;
    bool today = false; ///< "tonight", "this morning" also fix the date to today
};

DaypartEntry daypartEntry(Daypart part, Weakness weak = Weakness::None, bool today = false)
{
    DaypartEntry e;
    e.spec.daypart = part;
    e.weak = weak;
    e.today = today;
    return e;
}

DaypartEntry clockEntry(int hour, bool midnight = false)
{
    DaypartEntry e;
    e.spec.hour = hour;
    e.spec.minute = 0;
    e.spec.midnight = midnight;
    return e;
}

const QHash<QString, DaypartEntry> &dayparts()
{
    static const QHash<QString, DaypartEntry> map = {
        {u"rano"_s, daypartEntry(Daypart::Morning)},
        {u"rankiem"_s, daypartEntry(Daypart::Morning)},
        {u"z rana"_s, daypartEntry(Daypart::Morning)},
        {u"przed poludniem"_s, daypartEntry(Daypart::Forenoon)},
        {u"po poludniu"_s, daypartEntry(Daypart::Afternoon)},
        {u"popoludniu"_s, daypartEntry(Daypart::Afternoon)},
        {u"wieczorem"_s, daypartEntry(Daypart::Evening)},
        {u"wieczor"_s, daypartEntry(Daypart::Evening, Weakness::Alone)},
        {u"w nocy"_s, daypartEntry(Daypart::Night)},
        {u"noca"_s, daypartEntry(Daypart::Night)},
        {u"w poludnie"_s, clockEntry(12)},
        {u"w samo poludnie"_s, clockEntry(12)},
        {u"o polnocy"_s, clockEntry(0, true)},
        {u"in the morning"_s, daypartEntry(Daypart::Morning)},
        {u"in the afternoon"_s, daypartEntry(Daypart::Afternoon)},
        {u"in the evening"_s, daypartEntry(Daypart::Evening)},
        {u"at night"_s, daypartEntry(Daypart::Night)},
        {u"morning"_s, daypartEntry(Daypart::Morning, Weakness::Alone)},
        {u"afternoon"_s, daypartEntry(Daypart::Afternoon, Weakness::Alone)},
        {u"evening"_s, daypartEntry(Daypart::Evening, Weakness::Alone)},
        {u"night"_s, daypartEntry(Daypart::Night, Weakness::Alone)},
        {u"noon"_s, clockEntry(12)},
        {u"at noon"_s, clockEntry(12)},
        {u"midday"_s, clockEntry(12)},
        {u"midnight"_s, clockEntry(0, true)},
        {u"at midnight"_s, clockEntry(0, true)},
        {u"tonight"_s, daypartEntry(Daypart::Evening, Weakness::None, true)},
        {u"this morning"_s, daypartEntry(Daypart::Morning, Weakness::None, true)},
        {u"this afternoon"_s, daypartEntry(Daypart::Afternoon, Weakness::None, true)},
        {u"this evening"_s, daypartEntry(Daypart::Evening, Weakness::None, true)},
    };
    return map;
}

/// Time-of-day phrases that may follow a bare hour: "7 rano", "8 wieczorem", "11 w nocy".
const QStringList &hourDayparts()
{
    static const QStringList list = {u"rano"_s,           u"rankiem"_s,          u"z rana"_s,
                                     u"przed poludniem"_s, u"po poludniu"_s,     u"popoludniu"_s,
                                     u"wieczorem"_s,      u"w nocy"_s,           u"noca"_s,
                                     u"in the morning"_s, u"in the afternoon"_s, u"in the evening"_s,
                                     u"at night"_s};
    return list;
}

// =============================================================================================
// Small helpers
// =============================================================================================

QString capKey(const Match &m, int n)
{
    return collapseSpaces(m.capturedView(n));
}

/// Captured text of an optional group, or nullopt when the group did not participate.
std::optional<QString> optionalCap(const Match &m, int n)
{
    return m.hasCaptured(n) ? std::optional<QString>(m.captured(n)) : std::nullopt;
}

TokenValue dateToken(QDate date, Weakness weak = Weakness::None)
{
    TokenValue v;
    v.date = date;
    v.weak = weak;
    return v;
}

TokenValue timeToken(const TimeSpec &spec, Weakness weak = Weakness::None)
{
    TokenValue v;
    v.time = spec;
    v.weak = weak;
    return v;
}

std::optional<TokenValue> optionalDateToken(std::optional<QDate> date, Weakness weak = Weakness::None)
{
    return date ? std::optional<TokenValue>(dateToken(*date, weak)) : std::nullopt;
}

// =============================================================================================
// Resolution
// =============================================================================================

QDate weekdayDate(int day, WeekdayMode mode, const Ctx &ctx)
{
    const int dow = ctx.today.dayOfWeek();
    switch (mode) {
    case WeekdayMode::Next: {
        const int ahead = (day - dow + 7) % 7;
        return ctx.today.addDays(ahead == 0 ? 7 : ahead);
    }
    case WeekdayMode::This:
        return ctx.today.addDays((day - dow + 7) % 7);
    case WeekdayMode::Last: {
        const int back = (dow - day + 7) % 7;
        return ctx.today.addDays(-(back == 0 ? 7 : back));
    }
    case WeekdayMode::NextWeek:
        break;
    }
    return startOfWeek(ctx.today, ctx.weekStart).addDays(7 + (day - int(ctx.weekStart) + 7) % 7);
}

/// A day/month without a year: this year, or the next year (with that day) when already past.
std::optional<QDate> yearless(int month, int day, const Ctx &ctx)
{
    if (month < 1 || month > 12 || day < 1 || day > QDate(2000, month, 1).daysInMonth())
        return std::nullopt;
    int year = ctx.today.year();
    for (int i = 0; i < 9; ++i, ++year) {
        const QDate d(year, month, day);
        if (d.isValid() && d >= ctx.today)
            return d;
    }
    return std::nullopt;
}

std::optional<QDate> explicitDate(int year, int month, int day)
{
    const QDate d(year, month, day);
    return d.isValid() ? std::optional<QDate>(d) : std::nullopt;
}

int fullYear(QStringView digits)
{
    return digits.size() == 2 ? 2000 + digits.toInt() : digits.toInt();
}

/// Next date (today or later) with the given day of month.
std::optional<QDate> nextMonthDay(int day, const Ctx &ctx)
{
    if (day < 1 || day > 31)
        return std::nullopt;
    QDate month = startOfMonth(ctx.today);
    for (int i = 0; i < 13; ++i, month = month.addMonths(1)) {
        if (day <= month.daysInMonth()) {
            const QDate d(month.year(), month.month(), day);
            if (d >= ctx.today)
                return d;
        }
    }
    return std::nullopt;
}

std::optional<int> parseCount(const std::optional<QString> &word)
{
    if (!word)
        return std::nullopt;
    bool ok = false;
    const int n = word->toInt(&ok);
    if (ok)
        return n;
    if (*word == u"a" || *word == u"an")
        return 1;
    const auto it = detail::numberWords().constFind(*word);
    return it == detail::numberWords().cend() ? std::nullopt : std::optional<int>(*it);
}

TokenValue exactAfterMinutes(qint64 minutes, const Ctx &ctx)
{
    const QDateTime at = ctx.now.addSecs(minutes * 60);
    TokenValue v;
    if (at.isValid())
        v.exact = DateSpec{at.date(), QTime(at.time().hour(), at.time().minute())};
    return v;
}

/// Shift by a count of units: day-level units give a date, hours/minutes an exact date-time.
TokenValue offsetValue(Unit unit, int n, const Ctx &ctx)
{
    switch (unit) {
    case Unit::Day:
        return dateToken(ctx.today.addDays(n));
    case Unit::Week:
        return dateToken(ctx.today.addDays(7LL * n));
    case Unit::Month:
        return dateToken(ctx.today.addMonths(n));
    case Unit::Year:
        return dateToken(ctx.today.addYears(n));
    case Unit::Hour:
        return exactAfterMinutes(60LL * n, ctx);
    case Unit::Minute:
        return exactAfterMinutes(n, ctx);
    case Unit::Quarter:
        break;
    }
    return exactAfterMinutes(15LL * n, ctx);
}

/// Value of "za N <unit>" / "N <unit> temu". `count` may be missing only for the bare unit
/// words in `bare` ("za tydzień").
std::optional<TokenValue> relativeValue(const std::optional<QString> &count, const QString &unitWord, int sign,
                                        const QSet<QString> &bare, const Ctx &ctx)
{
    const auto unit = units().constFind(unitWord);
    if (unit == units().cend())
        return std::nullopt;
    std::optional<int> n = parseCount(count);
    if (!n) {
        if (count || !bare.contains(unitWord))
            return std::nullopt;
        n = 1;
    }
    return offsetValue(*unit, sign * *n, ctx);
}

QDate periodStart(const QString &word, const Ctx &ctx)
{
    if (word == u"tydzien" || word == u"tygodnia" || word == u"tygodniu" || word == u"week")
        return startOfWeek(ctx.today, ctx.weekStart).addDays(7);
    if (word == u"miesiac" || word == u"miesiaca" || word == u"miesiacu" || word == u"month")
        return startOfMonth(ctx.today).addMonths(1);
    return QDate(ctx.today.year() + 1, 1, 1);
}

QDate periodEnd(const QString &word, bool next, const Ctx &ctx)
{
    if (word == u"tygodnia" || word == u"week")
        return endOfWeek(ctx.today, ctx.weekStart).addDays(next ? 7 : 0);
    if (word == u"miesiaca" || word == u"month")
        return endOfMonth(next ? startOfMonth(ctx.today).addMonths(1) : ctx.today);
    return QDate(ctx.today.year() + (next ? 1 : 0), 12, 31);
}

/// Hour + optional minutes + optional am/pm (empty views = absent). Null when out of range.
std::optional<TimeSpec> timeSpec(QStringView hour, QStringView minute, QStringView meridiem)
{
    bool ok = false;
    const int h = hour.toInt(&ok);
    if (!ok)
        return std::nullopt;
    const int min = minute.isEmpty() ? 0 : minute.toInt();
    if (min > 59)
        return std::nullopt;
    TimeSpec spec;
    spec.hour = h;
    spec.minute = min;
    if (!meridiem.isEmpty()) {
        if (h < 1 || h > 12)
            return std::nullopt;
        spec.meridiem = meridiem.startsWith(u'a') ? Meridiem::Am : Meridiem::Pm;
    } else if (h > 23) {
        return std::nullopt;
    }
    return spec;
}

// =============================================================================================
// Context checks against false positives
// =============================================================================================

/// "12.05 zł", "15 kg": units/currencies after a number mean it is a quantity, not a date.
bool followedByQuantity(const Ctx &ctx, qsizetype end)
{
    static const QRegularExpression re = text::unicodeRegex(
        uR"(\s*(?:zl|pln|gr|eur|euro|usd|chf|gbp|kg|dag|g|km|m|cm|mm|l|ml|szt|sztuk|osob|osoby|razy|procent|x|[%$€£°])(?![\p{L}]))"_s);
    return re.match(ctx.foldedText(), end, QRegularExpression::NormalMatch, QRegularExpression::AnchorAtOffsetMatchOption)
        .hasMatch();
}

/// "$ 15.10": a currency sign (then optional whitespace) right before `start`.
bool precededByCurrency(const Ctx &ctx, qsizetype start)
{
    qsizetype i = start - 1;
    while (i >= 0 && ctx.foldedText().at(i).isSpace())
        --i;
    if (i < 0)
        return false;
    const QChar c = ctx.foldedText().at(i);
    return c == u'$' || c == u'€' || c == u'£';
}

/// Numeric dates must not be amounts ("12.05 zł", "$15.10").
bool numericDateOk(const Match &m, const Ctx &ctx)
{
    return !followedByQuantity(ctx, m.capturedEnd()) && !precededByCurrency(ctx, m.capturedStart());
}

/// "o 15 sie …": the folded text before `start` ends with the hour preposition "o"/"at" + space.
bool precededByHourPreposition(const Ctx &ctx, qsizetype start)
{
    const QString &t = ctx.foldedText();
    qsizetype i = start;
    while (i > 0 && t.at(i - 1).isSpace())
        --i;
    if (i == start)
        return false;
    for (const QStringView word : {QStringView(u"o"), QStringView(u"at")}) {
        const qsizetype w = i - word.size();
        if (w >= 0 && QStringView(t).sliced(w, word.size()) == word && (w == 0 || !t.at(w - 1).isLetterOrNumber()))
            return true;
    }
    return false;
}

/// Whitespace followed by a letter at `pos` ("o 15 sie spotkamy").
bool followedByWord(const Ctx &ctx, qsizetype pos)
{
    const qsizetype i = skipSpaces(ctx.foldedText(), pos);
    return i > pos && i < ctx.foldedText().size() && ctx.foldedText().at(i).isLetter();
}

/// Whether the month word (group 2) of a "day + month" match is really another Polish word that
/// only looks like a month after folding: "się" ("o 15 się spotkamy"), "mają" ("5 mają wolne"),
/// or "sie" typed without diacritics after an hour and before another word ("o 15 sie spotkamy").
bool monthWordCollides(const Match &m, const Ctx &ctx)
{
    const QString original = originalSlice(ctx, m.capturedStart(2), m.capturedEnd(2))
                                 .toString()
                                 .toLower()
                                 .normalized(QString::NormalizationForm_C);
    if (original == u"się" || original == u"mają")
        return true;
    if (m.capturedView(2) != u"sie" || m.hasCaptured(3))
        return false;
    return precededByHourPreposition(ctx, m.capturedStart()) && followedByWord(ctx, m.capturedEnd());
}

// =============================================================================================
// Token builders (one per matcher; group numbers refer to the matcher's pattern)
// =============================================================================================

std::optional<TokenValue> buildDayWord(const Match &m, const Ctx &ctx)
{
    const QString word = capKey(m, 2);
    if (needsPreposition(word) && !m.hasCaptured(1))
        return std::nullopt;
    const auto it = dayWords().constFind(word);
    if (it == dayWords().cend())
        return std::nullopt;
    return dateToken(ctx.today.addDays(*it));
}

std::optional<TokenValue> buildWeekday(const Match &m, const Ctx &ctx)
{
    const auto word = detail::lookupWeekday(m.capturedView(3));
    if (!word)
        return std::nullopt;
    const WeekdayMode mode = m.hasCaptured(2) ? weekdayModes().value(capKey(m, 2), WeekdayMode::Next)
                                              : WeekdayMode::Next;
    const bool weak = word->abbr && !m.hasCaptured(1) && !m.hasCaptured(2);
    return dateToken(weekdayDate(word->day, mode, ctx), weak ? Weakness::Alone : Weakness::None);
}

std::optional<TokenValue> buildPeriodStartIn(const Match &m, const Ctx &ctx)
{
    return dateToken(periodStart(capKey(m, 2), ctx));
}

std::optional<TokenValue> buildPeriodStartNext(const Match &m, const Ctx &ctx)
{
    return dateToken(periodStart(capKey(m, 1), ctx));
}

std::optional<TokenValue> buildPeriodEnd(const Match &m, const Ctx &ctx)
{
    const QStringView which = m.capturedView(1);
    const bool next = which == u"przyszlego" || which == u"nastepnego" || which == u"next";
    return dateToken(periodEnd(capKey(m, 2), next, ctx));
}

std::optional<TokenValue> buildWeekend(const Match &m, const Ctx &ctx)
{
    const QStringView which = m.capturedView(2);
    const bool next = which == u"przyszly" || which == u"nastepny" || which == u"next";
    const int dow = ctx.today.dayOfWeek();
    QDate date;
    if (next)
        date = weekdayDate(Qt::Saturday, WeekdayMode::NextWeek, ctx);
    else if (dow == Qt::Saturday || dow == Qt::Sunday)
        date = ctx.today;
    else
        date = weekdayDate(Qt::Saturday, WeekdayMode::Next, ctx);
    const bool weak = !m.hasCaptured(1) && !m.hasCaptured(2);
    return dateToken(date, weak ? Weakness::Alone : Weakness::None);
}

std::optional<TokenValue> buildRelative(const Match &m, const Ctx &ctx)
{
    const std::optional<QString> count = m.hasCaptured(2) ? optionalCap(m, 2) : optionalCap(m, 3);
    return relativeValue(count, m.captured(4), 1, bareUnitsAfter(capKey(m, 1)), ctx);
}

std::optional<TokenValue> buildFraction(const Match &m, const Ctx &ctx)
{
    const auto unit = units().constFind(m.captured(2));
    if (unit == units().cend())
        return std::nullopt;
    const auto frac = fractionValue(capKey(m, 1), *unit);
    if (!frac)
        return std::nullopt;
    if (frac->minutes > 0)
        return exactAfterMinutes(frac->minutes, ctx);
    return dateToken(ctx.today.addMonths(frac->months));
}

std::optional<TokenValue> buildAgo(const Match &m, const Ctx &ctx)
{
    const std::optional<QString> count = m.hasCaptured(1) ? optionalCap(m, 1) : optionalCap(m, 2);
    return relativeValue(count, m.captured(3), -1, bareUnitsAccusative(), ctx);
}

std::optional<TokenValue> buildIsoDate(const Match &m, const Ctx &)
{
    const auto date = explicitDate(m.capturedView(1).toInt(), m.capturedView(3).toInt(), m.capturedView(4).toInt());
    if (!date)
        return std::nullopt;
    TokenValue v = dateToken(*date);
    if (m.hasCaptured(5)) {
        TimeSpec spec;
        spec.hour = m.capturedView(5).toInt();
        spec.minute = m.capturedView(6).toInt();
        v.time = spec;
    }
    return v;
}

std::optional<TokenValue> buildDayMonthNumeric(const Match &m, const Ctx &ctx)
{
    if (!numericDateOk(m, ctx))
        return std::nullopt;
    const QStringView a = m.capturedView(1);
    const QStringView b = m.capturedView(3);
    const QChar sep = m.capturedView(2).at(0);
    const bool hasYear = m.hasCaptured(4);
    if (!hasYear) {
        // Without a year require two-digit months ("15.10", not decimals like "12.5"), and for
        // slashes two-digit parts (not fractions like "1/2" or scores like "8/10").
        if (sep == u'.' && b.size() != 2)
            return std::nullopt;
        if (sep == u'/' && (a.size() != 2 || b.size() != 2))
            return std::nullopt;
    }
    int day = a.toInt();
    int month = b.toInt();
    // Slashes: "10/15" can only be month/day; ambiguous ones follow the locale.
    if (sep == u'/' && ((ctx.locale == UiLocale::English && day <= 12 && month <= 12) || (month > 12 && day <= 12)))
        std::swap(day, month);
    return optionalDateToken(hasYear ? explicitDate(fullYear(m.capturedView(4)), month, day)
                                     : yearless(month, day, ctx));
}

std::optional<TokenValue> buildDashDate(const Match &m, const Ctx &ctx)
{
    if (!numericDateOk(m, ctx))
        return std::nullopt;
    return optionalDateToken(
        explicitDate(m.capturedView(3).toInt(), m.capturedView(2).toInt(), m.capturedView(1).toInt()));
}

std::optional<TokenValue> buildDayMonthName(const Match &m, const Ctx &ctx)
{
    const auto word = detail::lookupMonth(m.capturedView(2));
    if (!word || monthWordCollides(m, ctx))
        return std::nullopt;
    const int day = m.capturedView(1).toInt();
    return optionalDateToken(m.hasCaptured(3) ? explicitDate(m.capturedView(3).toInt(), word->month, day)
                                              : yearless(word->month, day, ctx));
}

std::optional<TokenValue> buildMonthDay(const Match &m, const Ctx &ctx)
{
    const auto word = detail::lookupMonth(m.capturedView(1));
    if (!word)
        return std::nullopt;
    const int day = m.capturedView(2).toInt();
    return optionalDateToken(m.hasCaptured(3) ? explicitDate(m.capturedView(3).toInt(), word->month, day)
                                              : yearless(word->month, day, ctx));
}

std::optional<TokenValue> buildOrdinalDay(const Match &m, const Ctx &ctx)
{
    const int day = (m.hasCaptured(2) ? m.capturedView(2) : m.capturedView(3)).toInt();
    // "the 2nd bug", "on the 3rd floor", "na 2-go piętrze" are ordinals, not dates.
    return optionalDateToken(nextMonthDay(day, ctx), Weakness::Ordinal);
}

std::optional<TokenValue> buildAtHour(const Match &m, const Ctx &)
{
    // "9h30": minutes after the "h" (never together with "9:30h15").
    const QStringView suffix = m.capturedView(6);
    const QStringView hMinutes = suffix.size() == 3 && suffix.startsWith(u'h') ? suffix.sliced(1) : QStringView();
    if (!hMinutes.isEmpty() && m.hasCaptured(4))
        return std::nullopt;
    const auto spec = timeSpec(m.capturedView(3), m.hasCaptured(4) ? m.capturedView(4) : hMinutes, m.capturedView(5));
    if (!spec)
        return std::nullopt;
    const bool bare = !m.hasCaptured(2) && !m.hasCaptured(4) && !m.hasCaptured(5) && !m.hasCaptured(6);
    return timeToken(*spec, bare ? Weakness::Hour : Weakness::None);
}

std::optional<TokenValue> buildHourDaypart(const Match &m, const Ctx &)
{
    // "10.10 rano" without "o" is a date (10 Oct) followed by "rano", not 10:10.
    if (m.capturedView(3) == u"." && !m.hasCaptured(1))
        return std::nullopt;
    auto spec = timeSpec(m.capturedView(2), m.capturedView(4), {});
    const auto entry = dayparts().constFind(capKey(m, 5));
    if (!spec || entry == dayparts().cend() || !entry->spec.daypart)
        return std::nullopt;
    const Daypart part = *entry->spec.daypart;
    if ((part == Daypart::Morning || part == Daypart::Forenoon) && *spec->hour > 12)
        return std::nullopt;
    spec->daypart = part;
    return timeToken(*spec);
}

std::optional<TokenValue> buildHourWord(const Match &m, const Ctx &)
{
    const auto spec = timeSpec(m.capturedView(1), m.capturedView(2), {});
    return spec ? std::optional<TokenValue>(timeToken(*spec)) : std::nullopt;
}

std::optional<TokenValue> buildClockOrMeridiem(const Match &m, const Ctx &)
{
    const auto spec = timeSpec(m.capturedView(1), m.capturedView(2), m.capturedView(3));
    return spec ? std::optional<TokenValue>(timeToken(*spec)) : std::nullopt;
}

std::optional<TokenValue> buildDaypart(const Match &m, const Ctx &ctx)
{
    const auto entry = dayparts().constFind(capKey(m, 1));
    if (entry == dayparts().cend())
        return std::nullopt;
    TokenValue v = timeToken(entry->spec, entry->weak);
    if (entry->today)
        v.date = ctx.today;
    return v;
}

// =============================================================================================
// Matchers (patterns run on folded, lower-case text; order = priority on equal length)
// =============================================================================================

QList<Matcher> buildMatchers()
{
    /// Word start / end (Unicode-aware: `\b` does not understand Polish letters, see text.h).
    const QString L = uR"((?<![\p{L}\p{N}]))"_s;
    const QString R = uR"((?![\p{L}\p{N}]))"_s;
    /// Start of a number that is not part of a longer number like "1.2.3" or "12:15.10".
    const QString NL = uR"((?<![\p{L}\p{N}])(?<![\p{N}][.,/:-]))"_s;
    /// End of a number that does not continue ("15.10.5" / "15.104").
    const QString NR = uR"((?![\p{N}])(?![.,/:-]\p{N}))"_s;
    const QString meridiem = uR"(a\.m\.|p\.m\.|a\.m|p\.m|am|pm)"_s;
    /// Prepositions that may precede a date and are included in the match ("do 15.10", "on Oct 15").
    const QString datePrep = uR"(na|do|od|dnia|w\s+dniu|on|by|until|till)"_s;
    const QString weekdayPrep = u"w|we|na|do|od|on|by|until|till|for"_s;

    QStringList singularWeekdays;
    for (auto it = detail::weekdayWords().cbegin(); it != detail::weekdayWords().cend(); ++it) {
        if (!it->plural)
            singularWeekdays.append(it.key());
    }
    QStringList englishMonths;
    for (auto it = detail::monthWords().cbegin(); it != detail::monthWords().cend(); ++it) {
        if (it->english)
            englishMonths.append(it.key());
    }
    const QString numberAlt = text::regexAlternation(detail::numberWords().keys() + QStringList{u"an"_s, u"a"_s});
    const QString unitAlt = text::regexAlternation(units().keys());

    const auto re = [](const QString &pattern) { return text::unicodeRegex(pattern); };
    return {
        // dziś / jutro / pojutrze / wczoraj / today / tomorrow (+ "na", "do", "on", …)
        {re(uR"(%1(?:(na|do|od|on|by|for|until|till)\s+)?(%2)%3)"_s.arg(L, phraseAlternation(dayWords().keys()), R)),
         buildDayWord},
        // Weekdays: "w piątek", "we wtorek", "w przyszłą środę", "next friday", "pt"
        {re(uR"(%1(?:(%2)\s+)?(?:(%3)\s+)?(%4)%5)"_s.arg(L, weekdayPrep, phraseAlternation(weekdayModes().keys()),
                                                           text::regexAlternation(singularWeekdays), R)),
         buildWeekday},
        // "w przyszłym tygodniu", "w następnym miesiącu", "w przyszłym roku"
        {re(uR"(%1(?:w|we)\s+(przyszlym|nastepnym|kolejnym)\s+(tygodniu|miesiacu|roku)%2)"_s.arg(L, R)),
         buildPeriodStartIn},
        // "przyszły tydzień", "do przyszłego tygodnia", "next week/month/year"
        {re(uR"(%1(?:(?:na|do|od|by|until)\s+)?(?:przyszly|nastepny|przyszlego|nastepnego|next)\s+(tydzien|tygodnia|miesiac|miesiaca|rok|roku|week|month|year)%2)"_s
                .arg(L, R)),
         buildPeriodStartNext},
        // "koniec miesiąca", "do końca tygodnia", "end of the month", "end of next week"
        {re(uR"(%1(?:(?:na|do|w|przed|by|at|until|till|before)\s+)?(?:the\s+)?(?:koniec|konca|koncu|koncem|end\s+of)\s+(?:(tego|biezacego|przyszlego|nastepnego|the|this|next)\s+)?(tygodnia|miesiaca|roku|week|month|year)%2)"_s
                .arg(L, R)),
         buildPeriodEnd},
        // Weekend: "w weekend", "na weekend", "this weekend", "w przyszły weekend"
        {re(uR"(%1(?:(w|we|na|on|over|at)\s+)?(?:(the|ten|this|przyszly|nastepny|najblizszy|next)\s+)?(weekend|weekendzie|weekendu)%2)"_s
                .arg(L, R)),
         buildWeekend},
        // Relative: "za 2 dni", "za tydzień", "za 3 godziny", "w ciągu godziny", "in 2 weeks", "in an hour"
        {re(uR"(%1(za|w\s+ciagu|in|within)\s+(?:([0-9]{1,4})\s*|(%2)\s+)?(%3)%4)"_s.arg(L, numberAlt, unitAlt, R)),
         buildRelative},
        // Fractions: "za pół godziny", "za półtorej godziny", "in half an hour", "za pół roku"
        {re(uR"(%1(?:za|w\s+ciagu|in|within)\s+(%2)\s+(godziny|godzine|hour|roku|year|dnia|day)%3)"_s.arg(
             L, phraseAlternation(fractionWords()), R)),
         buildFraction},
        // Ago: "2 dni temu", "tydzień temu", "3 weeks ago"
        {re(uR"(%1(?:([0-9]{1,4})\s*|(%2)\s+)?(%3)\s+(?:temu|ago)%4)"_s.arg(L, numberAlt, unitAlt, R)), buildAgo},
        // ISO: "2026-10-15", "2026/10/15", "2026-10-15T18:30"
        {re(uR"(%1(?:(?:%2)\s+)?([0-9]{4})([-/.])([0-9]{1,2})\2([0-9]{1,2})(?:t([0-9]{1,2}):([0-9]{2}))?%3)"_s.arg(
             NL, datePrep, NR)),
         buildIsoDate},
        // Day.month[.year]: "15.10", "15.10.2026", "15/10", "15.10.26r."
        {re(uR"(%1(?:(?:%2)\s+)?([0-9]{1,2})([./])([0-9]{1,2})(?:\2([0-9]{4}|[0-9]{2})(?:\s?r\.?(?![\p{L}]))?)?%3)"_s
                .arg(NL, datePrep, NR)),
         buildDayMonthNumeric},
        // Day-month-year with dashes: "15-10-2026"
        {re(uR"(%1(?:(?:%2)\s+)?([0-9]{1,2})-([0-9]{1,2})-([0-9]{4})%3)"_s.arg(NL, datePrep, NR)), buildDashDate},
        // Day + month name: "15 października", "15 paź 2026", "1st of October", "15th Oct"
        {re(uR"(%1(?:(?:%2)\s+)?([0-9]{1,2})(?:\.|-go|-ego|-tego|st|nd|rd|th)?(?:\s+of)?\s*(%3)(?![\p{L}])(?:\.?,?\s+([0-9]{4})(?![\p{N}])(?:\s?r\.?(?![\p{L}]))?)?)"_s
                .arg(L, datePrep, text::regexAlternation(detail::monthWords().keys()))),
         buildDayMonthName},
        // English month first: "Oct 15", "October 15th, 2026", "on May 3"
        {re(uR"(%1(?:(?:on|by|until|till)\s+)?(%2)\.?\s+([0-9]{1,2})(?:st|nd|rd|th)?(?![\p{L}\p{N}])(?:,?\s+([0-9]{4})(?![\p{N}]))?)"_s
                .arg(L, text::regexAlternation(englishMonths))),
         buildMonthDay},
        // Day of month only: "the 15th", "on the 3rd", "by the 15th", "15-go", "do 20-tego"
        {re(uR"(%1(?:(on|by|until|till|na|do|od|dnia)\s+)?(?:the\s+([0-9]{1,2})(?:st|nd|rd|th)|([0-9]{1,2})-(?:go|ego|tego))%2)"_s
                .arg(L, R)),
         buildOrdinalDay},
        // "o 9", "o 9:30", "o 18.30", "o godz. 9", "at 9", "at 6pm", "o 18h", "o 9h30"
        {re(uR"(%1(o|at)\s+(?:(godz\.?|godzinie|godzina)\s*)?([0-9]{1,2})(?:[:.]([0-9]{2}))?(?![\p{N}])(?![:.]\p{N})(?:\s*(%2)(?![\p{L}]))?(?:\s*(h(?:[0-9]{2})?|godz\.?)(?![\p{L}\p{N}]))?(?![\p{L}]))"_s
                .arg(L, meridiem)),
         buildAtHour},
        // Hour + time of day: "7 rano", "o 7 rano", "8 wieczorem", "6:30 rano", "7 in the morning"
        {re(uR"(%1(?:(o|at)\s+(?:(?:godz\.?|godzinie|godzina)\s*)?)?([0-9]{1,2})(?:([:.])([0-9]{2}))?(?![\p{N}])\s+(%2)%3)"_s
                .arg(NL, phraseAlternation(hourDayparts()), R)),
         buildHourDaypart},
        // "godz. 18", "godzina 9:30"
        {re(uR"(%1(?:godz\.?|godzina|godzinie)\s*([0-9]{1,2})(?:[:.]([0-9]{2}))?(?![\p{N}])(?![:.]\p{N}))"_s.arg(L)),
         buildHourWord},
        // "9:30", "18:30", "6:30pm"
        {re(uR"(%1([0-9]{1,2}):([0-9]{2})(?![\p{N}])(?!:\p{N})(?:\s*(%2)(?![\p{L}]))?)"_s.arg(NL, meridiem)),
         buildClockOrMeridiem},
        // "6pm", "6 pm", "6.30 p.m."
        {re(uR"(%1([0-9]{1,2})(?:[.]([0-9]{2}))?\s*(%2)(?![\p{L}]))"_s.arg(NL, meridiem)), buildClockOrMeridiem},
        // "6 o'clock"
        {re(uR"(%1([0-9]{1,2})\s*o['’]?clock%2)"_s.arg(NL, R)), buildHourWord},
        // Times of day: "rano", "wieczorem", "w południe", "po południu", "in the evening", "noon", "tonight"
        {re(uR"(%1(%2)%3)"_s.arg(L, phraseAlternation(dayparts().keys()), R)), buildDaypart},
    };
}

} // namespace

const QList<Matcher> &naturalMatchers()
{
    static const QList<Matcher> list = buildMatchers();
    return list;
}

} // namespace brulion::dates::detail
