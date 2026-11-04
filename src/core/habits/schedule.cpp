// Habit schedules: parsing, formatting, due days and periods (see habits.h).
#include "habits/habits.h"

#include "dates/recurrence.h"
#include "text/text.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace brulion::habits {

namespace {

using Kind = Schedule::Kind;
using Period = Schedule::Period;
using Freq = dates::RecurrenceRule::Freq;

/// A count written as a number or a word ("3", "trzy", "raz", "twice"); 0 when not one.
int countOf(QStringView word)
{
    bool ok = false;
    const int n = word.toInt(&ok);
    if (ok)
        return n;
    static const QHash<QString, int> words = {
            {u"raz"_s, 1},    {u"jeden"_s, 1}, {u"jednego"_s, 1}, {u"dwa"_s, 2},    {u"trzy"_s, 3},
            {u"cztery"_s, 4}, {u"piec"_s, 5},  {u"szesc"_s, 6},   {u"siedem"_s, 7}, {u"once"_s, 1},
            {u"one"_s, 1},    {u"twice"_s, 2}, {u"two"_s, 2},     {u"thrice"_s, 3}, {u"three"_s, 3},
            {u"four"_s, 4},   {u"five"_s, 5},  {u"six"_s, 6},     {u"seven"_s, 7},
    };
    return words.value(word.toString(), 0);
}

std::optional<Period> periodOf(QStringView word)
{
    static const QHash<QString, Period> words = {
            {u"tydzien"_s, Period::Week},   {u"tygodniu"_s, Period::Week},  {u"tygodnie"_s, Period::Week},
            {u"tygodni"_s, Period::Week},   {u"tydz"_s, Period::Week},      {u"tyg"_s, Period::Week},
            {u"week"_s, Period::Week},      {u"weeks"_s, Period::Week},     {u"wk"_s, Period::Week},
            {u"miesiac"_s, Period::Month},  {u"miesiacu"_s, Period::Month}, {u"miesiace"_s, Period::Month},
            {u"miesiecy"_s, Period::Month}, {u"mies"_s, Period::Month},     {u"month"_s, Period::Month},
            {u"months"_s, Period::Month},   {u"mo"_s, Period::Month},       {u"rok"_s, Period::Year},
            {u"roku"_s, Period::Year},      {u"lata"_s, Period::Year},      {u"lat"_s, Period::Year},
            {u"year"_s, Period::Year},      {u"years"_s, Period::Year},     {u"yr"_s, Period::Year},
    };
    const auto it = words.constFind(word.toString());
    if (it == words.constEnd())
        return std::nullopt;
    return *it;
}

int daysPerPeriod(Period period)
{
    switch (period) {
    case Period::Week: return 7;
    case Period::Month: return 31;
    case Period::Year: return 366;
    }
    return 0;
}

/// "3 razy w tygodniu", "3x/tydz", "raz na 2 tygodnie", "twice a month", "3 days a week".
std::optional<Schedule> parseTimes(const QString &folded)
{
    static const QRegularExpression re(
            uR"(^(\d+|[a-z]+)\s*(?:x|×|razy|raz|times|time|dni|dzien|days|day)?\s*)"
            uR"((?:/|(?:w|na|a|an|per|every|each|in|in a|in an)(?=\s|\d))\s*(?:(\d+)\s*)?([a-z]+)$)"_s);
    const QRegularExpressionMatch m = re.match(folded);
    if (!m.hasMatch())
        return std::nullopt;
    const int times = countOf(m.capturedView(1));
    const auto period = periodOf(m.capturedView(3));
    if (times < 1 || !period)
        return std::nullopt;
    int interval = 1;
    if (m.hasCaptured(2)) {
        bool ok = false;
        interval = m.capturedView(2).toInt(&ok);
        if (!ok || interval < 1 || interval > MaxInterval)
            return std::nullopt;
    }
    if (qint64(times) > qint64(daysPerPeriod(*period)) * interval)
        return std::nullopt;
    Schedule s;
    s.kind = Kind::Times;
    s.times = times;
    s.period = *period;
    s.interval = interval;
    return s;
}

std::optional<Schedule> fromRule(const dates::RecurrenceRule &rule)
{
    if (rule.interval < 1 || rule.interval > MaxInterval)
        return std::nullopt;
    Schedule s;
    s.interval = rule.interval;
    switch (rule.freq) {
    case Freq::Day:
        if (rule.weekdaysOnly) {
            if (rule.interval != 1)
                return std::nullopt; // every 2nd business day: no habit schedule
            s.kind = Kind::Weekdays;
            s.weekdays = {1, 2, 3, 4, 5};
            return s;
        }
        s.kind = Kind::Days;
        return s;
    case Freq::Week:
        if (!rule.weekdays.isEmpty() || rule.weekdaysOnly) {
            s.kind = Kind::Weekdays;
            s.weekdays = rule.weekdays.isEmpty() ? QList<int>{1, 2, 3, 4, 5} : rule.weekdays;
            return s;
        }
        s.kind = Kind::Times;
        s.period = Period::Week;
        return s;
    case Freq::Month:
        if (rule.lastDayOfMonth || rule.monthDay) {
            s.kind = Kind::MonthDay;
            s.monthDay = rule.lastDayOfMonth ? 0 : *rule.monthDay;
            return s;
        }
        s.kind = Kind::Times;
        s.period = Period::Month;
        return s;
    case Freq::Year:
        s.kind = Kind::Times;
        s.period = Period::Year;
        return s;
    }
    return std::nullopt;
}

QString formatTimes(const Schedule &s, dates::UiLocale locale)
{
    if (locale == dates::UiLocale::Polish) {
        const QString count = s.times == 1 ? u"raz"_s : QString::number(s.times) + u" razy"_s;
        if (s.interval == 1) {
            switch (s.period) {
            case Period::Week: return count + u" w tygodniu"_s;
            case Period::Month: return count + u" w miesiącu"_s;
            case Period::Year: return count + u" w roku"_s;
            }
        }
        QString unit;
        switch (s.period) {
        case Period::Week: unit = dates::polishPlural(s.interval, u"tydzień", u"tygodnie", u"tygodni"); break;
        case Period::Month: unit = dates::polishPlural(s.interval, u"miesiąc", u"miesiące", u"miesięcy"); break;
        case Period::Year: unit = dates::polishPlural(s.interval, u"rok", u"lata", u"lat"); break;
        }
        return count + u" na "_s + QString::number(s.interval) + u' ' + unit;
    }
    const QString count = s.times == 1 ? u"once"_s : s.times == 2 ? u"twice"_s : QString::number(s.times) + u" times"_s;
    QString unit;
    switch (s.period) {
    case Period::Week: unit = u"week"_s; break;
    case Period::Month: unit = u"month"_s; break;
    case Period::Year: unit = u"year"_s; break;
    }
    if (s.interval == 1)
        return count + u" a "_s + unit;
    return count + u" every "_s + QString::number(s.interval) + u' ' + unit + u's';
}

/// Months from the month of `a` to the month of `b`.
int monthsBetween(QDate a, QDate b)
{
    return (b.year() - a.year()) * 12 + (b.month() - a.month());
}

} // namespace

std::optional<Schedule> parseSchedule(QStringView text)
{
    QString folded = text::foldKey(text).simplified();
    // A sentence's full stop is no part of a count or a weekday list ("co miesiąc 15." keeps its
    // own: the recurrence parser reads the text as written).
    while (folded.endsWith(u'.'))
        folded.chop(1);
    folded = folded.trimmed();
    if (folded.isEmpty() || folded == u"tak" || folded == u"yes" || folded == u"true")
        return Schedule{};
    if (auto times = parseTimes(folded))
        return times;
    const QList<int> weekdays = dates::parseWeekdayList(folded);
    if (!weekdays.isEmpty()) {
        Schedule s;
        s.kind = Kind::Weekdays;
        s.weekdays = weekdays;
        return s;
    }
    if (const auto rule = dates::parseRecurrence(text))
        return fromRule(*rule);
    return std::nullopt;
}

QString formatSchedule(const Schedule &s, dates::UiLocale locale)
{
    const bool polish = locale == dates::UiLocale::Polish;
    dates::RecurrenceRule rule;
    rule.interval = s.interval;
    switch (s.kind) {
    case Kind::Days: rule.freq = Freq::Day; break;
    case Kind::Weekdays:
        if (s.weekdays == QList<int>{1, 2, 3, 4, 5} && s.interval == 1)
            return polish ? u"w dni robocze"_s : u"every weekday"_s;
        if (s.interval == 1) {
            QStringList names;
            for (const int d : s.weekdays)
                names.append(dates::weekdayName(d, locale, dates::NameForm::Short));
            return names.join(u", "_s);
        }
        rule.freq = Freq::Week;
        rule.weekdays = s.weekdays;
        break;
    case Kind::MonthDay:
        rule.freq = Freq::Month;
        if (s.monthDay == 0)
            rule.lastDayOfMonth = true;
        else
            rule.monthDay = s.monthDay;
        break;
    case Kind::Times:
        if (s.times == 1 && s.interval > 1) {
            rule.freq = s.period == Period::Week ? Freq::Week : s.period == Period::Month ? Freq::Month : Freq::Year;
            break;
        }
        return formatTimes(s, locale);
    }
    return dates::formatRecurrence(rule, locale);
}

QDate periodStart(Period period, QDate day, Qt::DayOfWeek weekStart)
{
    if (!day.isValid())
        return {};
    switch (period) {
    case Period::Week: return day.addDays(-((day.dayOfWeek() - int(weekStart) + 7) % 7));
    case Period::Month: return QDate(day.year(), day.month(), 1);
    case Period::Year: return QDate(day.year(), 1, 1);
    }
    return {};
}

bool isScheduled(const Schedule &s, QDate start, QDate day, Qt::DayOfWeek weekStart)
{
    if (!start.isValid() || !day.isValid() || day < start)
        return false;
    const int interval = std::max(1, s.interval);
    switch (s.kind) {
    case Kind::Days: return start.daysTo(day) % interval == 0;
    case Kind::Weekdays: {
        if (!s.weekdays.contains(day.dayOfWeek()))
            return false;
        const qint64 weeks =
                periodStart(Period::Week, start, weekStart).daysTo(periodStart(Period::Week, day, weekStart)) / 7;
        return weeks % interval == 0;
    }
    case Kind::MonthDay: {
        if (monthsBetween(start, day) % interval != 0)
            return false;
        const int last = day.daysInMonth();
        const int target = s.monthDay <= 0 ? last : std::min(s.monthDay, last);
        return day.day() == target;
    }
    case Kind::Times: return false;
    }
    return false;
}

} // namespace brulion::habits
