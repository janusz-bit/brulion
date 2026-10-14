#include "dates/expr.h"

#include "dates/local.h"
#include "text/text.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

using namespace Qt::StringLiterals;

namespace brulion::dates {

namespace {

enum class Period { Week, Month, Year };
enum class OffsetUnit { Day, Week, Month, Year };

const QHash<QString, SpecialDate> &keywords()
{
    static const QHash<QString, SpecialDate> map = {
        {u"overdue"_s, SpecialDate::Overdue}, {u"zalegle"_s, SpecialDate::Overdue},
        {u"zalegly"_s, SpecialDate::Overdue}, {u"przeterminowane"_s, SpecialDate::Overdue},
        {u"none"_s, SpecialDate::None},       {u"brak"_s, SpecialDate::None},
        {u"bez"_s, SpecialDate::None},        {u"any"_s, SpecialDate::Any},
        {u"dowolna"_s, SpecialDate::Any},     {u"dowolny"_s, SpecialDate::Any},
        {u"dowolne"_s, SpecialDate::Any},     {u"jakakolwiek"_s, SpecialDate::Any},
        {u"kazda"_s, SpecialDate::Any},
    };
    return map;
}

const QHash<QString, int> &dayWords()
{
    static const QHash<QString, int> map = {
        {u"today"_s, 0},     {u"dzis"_s, 0},    {u"dzisiaj"_s, 0}, {u"tomorrow"_s, 1},     {u"jutro"_s, 1},
        {u"yesterday"_s, -1}, {u"wczoraj"_s, -1}, {u"pojutrze"_s, 2}, {u"przedwczoraj"_s, -2},
    };
    return map;
}

/// Period shift of a "which" word: this = 0, next = +1, last = -1.
const QHash<QString, int> &whichWords()
{
    static const QHash<QString, int> map = {
        {u"this"_s, 0},      {u"current"_s, 0},   {u"ten"_s, 0},       {u"ta"_s, 0},
        {u"biezacy"_s, 0},   {u"biezaca"_s, 0},   {u"obecny"_s, 0},    {u"next"_s, 1},
        {u"nastepny"_s, 1},  {u"nastepna"_s, 1},  {u"przyszly"_s, 1},  {u"przyszla"_s, 1},
        {u"kolejny"_s, 1},   {u"last"_s, -1},     {u"previous"_s, -1}, {u"prev"_s, -1},
        {u"zeszly"_s, -1},   {u"zeszla"_s, -1},   {u"poprzedni"_s, -1}, {u"poprzednia"_s, -1},
        {u"ubiegly"_s, -1},  {u"ostatni"_s, -1},
    };
    return map;
}

const QHash<QString, Period> &periodWords()
{
    static const QHash<QString, Period> map = {
        {u"week"_s, Period::Week},     {u"tydzien"_s, Period::Week}, {u"tygodnia"_s, Period::Week},
        {u"tyg"_s, Period::Week},      {u"month"_s, Period::Month},  {u"miesiac"_s, Period::Month},
        {u"miesiaca"_s, Period::Month}, {u"mies"_s, Period::Month},   {u"year"_s, Period::Year},
        {u"rok"_s, Period::Year},      {u"roku"_s, Period::Year},
    };
    return map;
}

const QHash<QString, OffsetUnit> &offsetUnits()
{
    static const QHash<QString, OffsetUnit> map = {
        {u"d"_s, OffsetUnit::Day},          {u"day"_s, OffsetUnit::Day},       {u"days"_s, OffsetUnit::Day},
        {u"dzien"_s, OffsetUnit::Day},      {u"dni"_s, OffsetUnit::Day},       {u"w"_s, OffsetUnit::Week},
        {u"t"_s, OffsetUnit::Week},         {u"week"_s, OffsetUnit::Week},     {u"weeks"_s, OffsetUnit::Week},
        {u"tyg"_s, OffsetUnit::Week},       {u"tydzien"_s, OffsetUnit::Week},  {u"tygodnie"_s, OffsetUnit::Week},
        {u"tygodni"_s, OffsetUnit::Week},   {u"m"_s, OffsetUnit::Month},       {u"month"_s, OffsetUnit::Month},
        {u"months"_s, OffsetUnit::Month},   {u"mies"_s, OffsetUnit::Month},    {u"miesiac"_s, OffsetUnit::Month},
        {u"miesiace"_s, OffsetUnit::Month}, {u"miesiecy"_s, OffsetUnit::Month}, {u"y"_s, OffsetUnit::Year},
        {u"r"_s, OffsetUnit::Year},         {u"year"_s, OffsetUnit::Year},     {u"years"_s, OffsetUnit::Year},
        {u"rok"_s, OffsetUnit::Year},       {u"lata"_s, OffsetUnit::Year},     {u"lat"_s, OffsetUnit::Year},
    };
    return map;
}

std::optional<DateRange> single(QDate date)
{
    if (!isSupportedDate(date))
        return std::nullopt;
    return DateRange{date, date};
}

/// Range of the week/month/year containing `today`, shifted by `shift` periods.
std::optional<DateRange> periodRange(Period period, int shift, QDate today, Qt::DayOfWeek weekStart)
{
    DateRange range;
    switch (period) {
    case Period::Week:
        range.from = startOfWeek(today, weekStart).addDays(7LL * shift);
        range.to = range.from.addDays(6);
        break;
    case Period::Month:
        range.from = startOfMonth(today).addMonths(shift);
        range.to = endOfMonth(range.from);
        break;
    case Period::Year: {
        const int year = today.year() + shift;
        range.from = QDate(year, 1, 1);
        range.to = QDate(year, 12, 31);
        break;
    }
    }
    if (!isSupportedDate(range.from) || !isSupportedDate(range.to))
        return std::nullopt;
    return range;
}

/// "+7d", "-2w", "+1m", "3d" (sign optional with a unit), "+7" (days), "+3-dni".
std::optional<DateRange> parseOffset(const QString &s, QDate today)
{
    static const QRegularExpression re = text::unicodeRegex(uR"(^([+-]?)([0-9]+)-?([a-z]*)\z)"_s);
    const auto m = re.match(s);
    if (!m.hasMatch())
        return std::nullopt;
    const QStringView sign = m.capturedView(1);
    const QString unitWord = m.captured(3);
    if (sign.isEmpty() && unitWord.isEmpty())
        return std::nullopt; // a bare number is not a date
    OffsetUnit unit = OffsetUnit::Day;
    if (!unitWord.isEmpty()) {
        const auto it = offsetUnits().constFind(unitWord);
        if (it == offsetUnits().cend())
            return std::nullopt;
        unit = *it;
    }
    bool ok = false;
    qint64 n = m.capturedView(2).toLongLong(&ok);
    // Anything beyond ~10 000 years cannot land in the supported range (and could overflow).
    if (!ok || n > 4'000'000)
        return std::nullopt;
    if (sign == u"-")
        n = -n;
    switch (unit) {
    case OffsetUnit::Day:
        return single(today.addDays(n));
    case OffsetUnit::Week:
        return single(today.addDays(7 * n));
    case OffsetUnit::Month:
        return single(today.addMonths(int(n)));
    case OffsetUnit::Year:
        break;
    }
    return single(today.addYears(int(n)));
}

/// "this-week", "next-month", "ten-tydzien", "przyszly-miesiac", "week" (= this week).
std::optional<DateRange> parsePeriod(const QString &s, QDate today, Qt::DayOfWeek weekStart)
{
    const QStringList parts = s.split(u'-', Qt::SkipEmptyParts);
    if (parts.size() == 1) {
        const auto period = periodWords().constFind(parts.first());
        if (period == periodWords().cend())
            return std::nullopt;
        return periodRange(*period, 0, today, weekStart);
    }
    if (parts.size() != 2)
        return std::nullopt;
    // Both word orders: "this-week" and Polish "tydzien-ten" / "tydzien-nastepny".
    const QString &a = parts.at(0);
    const QString &b = parts.at(1);
    const auto which = whichWords().contains(a) ? whichWords().constFind(a) : whichWords().constFind(b);
    const auto period = periodWords().contains(b) ? periodWords().constFind(b) : periodWords().constFind(a);
    if (which == whichWords().cend() || period == periodWords().cend())
        return std::nullopt;
    return periodRange(*period, *which, today, weekStart);
}

std::optional<DateRange> resolveSingle(const QString &s, QDate today, Qt::DayOfWeek weekStart)
{
    const auto day = dayWords().constFind(s);
    if (day != dayWords().cend())
        return single(today.addDays(*day));
    // "2026-10-15", also a date-time ("2026-10-15t09:00", or "-" where a space was) -> its date.
    static const QRegularExpression iso =
        text::unicodeRegex(uR"(^([0-9]{4}-[0-9]{2}-[0-9]{2})(?:[t-][0-9]{1,2}:[0-9]{2})?\z)"_s);
    const auto m = iso.match(s);
    if (m.hasMatch()) {
        const auto date = parseIsoDate(m.capturedView(1));
        return date ? single(*date) : std::nullopt;
    }
    if (auto offset = parseOffset(s, today))
        return offset;
    return parsePeriod(s, today, weekStart);
}

/// Trimmed, with every run of whitespace or '_' replaced by '-' ("this week" -> "this-week").
QString normalizeExpr(QStringView s)
{
    const QStringView t = s.trimmed();
    QString out;
    out.reserve(t.size());
    bool inRun = false;
    for (const QChar c : t) {
        const bool sep = c.isSpace() || c == u'_';
        if (sep && !inRun)
            out += u'-';
        else if (!sep)
            out += c;
        inRun = sep;
    }
    return out;
}

} // namespace

std::optional<DateExpr> resolveDateExpr(QStringView expr, QDate today, Qt::DayOfWeek weekStart)
{
    const QString folded = text::foldKey(expr).trimmed();
    if (folded.isEmpty())
        return std::nullopt;
    // Plain split + trim (a regex like \s*\.\.\s* would backtrack on long whitespace).
    const QStringList sides = folded.split(u".."_s);
    if (sides.size() > 2)
        return std::nullopt;
    if (sides.size() == 1) {
        const auto keyword = keywords().constFind(normalizeExpr(folded));
        if (keyword != keywords().cend())
            return DateExpr{*keyword};
    }
    if (!isSupportedDate(today))
        return std::nullopt;
    const Qt::DayOfWeek start = sanitizeWeekStart(int(weekStart));
    QList<DateRange> ranges;
    for (const QString &side : sides) {
        const QString normalized = normalizeExpr(side);
        if (normalized.isEmpty())
            return std::nullopt;
        const auto range = resolveSingle(normalized, today, start);
        if (!range)
            return std::nullopt;
        ranges.append(*range);
    }
    if (ranges.size() == 1)
        return DateExpr{ranges.first()};
    const DateRange &a = ranges.at(0);
    const DateRange &b = ranges.at(1);
    return DateExpr{a.from <= b.to ? DateRange{a.from, b.to} : DateRange{b.from, a.to}};
}

} // namespace brulion::dates
