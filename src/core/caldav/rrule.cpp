#include "caldav/rrule.h"

#include <QDateTime>
#include <QHash>
#include <QStringTokenizer>
#include <QTimeZone>

#include <algorithm>
#include <array>

namespace brulion::caldav {

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QStringView, 7> WeekdayCodes = {u"MO", u"TU", u"WE", u"TH", u"FR", u"SA", u"SU"};
constexpr int MaxYear = 9999;

std::optional<int> weekdayFromCode(QStringView code)
{
    for (int d = 0; d < 7; ++d) {
        if (code.compare(WeekdayCodes[d], Qt::CaseInsensitive) == 0)
            return d + 1;
    }
    return std::nullopt;
}

/// A list of integers within [min, max] (0 refused when `noZero`), negative ones allowed when
/// `min` < 0.
bool parseIntList(QStringView value, int min, int max, bool noZero, QList<int> &out)
{
    for (const QStringView part : qTokenize(value, u',', Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = part.trimmed().toInt(&ok);
        if (!ok || n < min || n > max || (noZero && n == 0))
            return false;
        if (!out.contains(n))
            out.append(n);
    }
    return !out.isEmpty();
}

QString joinInts(const QList<int> &values)
{
    QStringList parts;
    parts.reserve(values.size());
    for (int v : values)
        parts.append(QString::number(v));
    return parts.join(u',');
}

/// The first day of the week holding `date`, weeks starting on `weekStart` (1 = Monday).
QDate weekStartOf(QDate date, int weekStart)
{
    return date.addDays(-((date.dayOfWeek() - weekStart + 7) % 7));
}

/// The first day of week 1 of `year` (RFC 5545: the first week with at least four days of the
/// year, weeks starting on `weekStart`).
QDate firstWeekStart(int year, int weekStart)
{
    const QDate jan1(year, 1, 1);
    const int offset = (jan1.dayOfWeek() - weekStart + 7) % 7;
    const QDate start = jan1.addDays(-offset);
    return 7 - offset >= 4 ? start : start.addDays(7);
}

struct Expander {
    RRule r;
    NaiveDateTime dtstart;
    bool dateOnly = false;
    ExpandLimits limits;
    int candidates = 0;

    bool dayMatches(QDate day) const
    {
        if (!r.byMonth.isEmpty() && !r.byMonth.contains(day.month()))
            return false;
        if (!r.byYearDay.isEmpty()) {
            const int pos = day.dayOfYear();
            const int neg = pos - day.daysInYear() - 1;
            if (!r.byYearDay.contains(pos) && !r.byYearDay.contains(neg))
                return false;
        }
        if (!r.byMonthDay.isEmpty()) {
            const int pos = day.day();
            const int neg = pos - day.daysInMonth() - 1;
            if (!r.byMonthDay.contains(pos) && !r.byMonthDay.contains(neg))
                return false;
        }
        if (!r.byDay.isEmpty()) {
            const bool ordinals = r.freq == RRule::Freq::Monthly
                                  || (r.freq == RRule::Freq::Yearly && r.byWeekNo.isEmpty());
            const bool inMonth = r.freq == RRule::Freq::Monthly || !r.byMonth.isEmpty();
            bool any = false;
            for (const RRule::WeekdayNum &wd : r.byDay) {
                if (wd.weekday != day.dayOfWeek())
                    continue;
                if (wd.ordinal == 0 || !ordinals) {
                    any = true;
                    break;
                }
                const int index = inMonth ? day.day() : day.dayOfYear();
                const int length = inMonth ? day.daysInMonth() : day.daysInYear();
                const int nth = (index - 1) / 7 + 1;
                const int fromEnd = -((length - index) / 7 + 1);
                if (wd.ordinal == nth || wd.ordinal == fromEnd) {
                    any = true;
                    break;
                }
            }
            if (!any)
                return false;
        }
        return true;
    }

    /// The days of the period starting at `start` (its first day), before the filters.
    QList<QDate> periodDays(const NaiveDateTime &start) const
    {
        QList<QDate> days;
        switch (r.freq) {
        case RRule::Freq::Yearly: {
            const int year = start.date.year();
            if (!r.byWeekNo.isEmpty()) {
                const QDate first = firstWeekStart(year, r.weekStart);
                const int weeks = int(first.daysTo(firstWeekStart(year + 1, r.weekStart)) / 7);
                QList<int> numbers;
                for (int n : r.byWeekNo) {
                    const int w = n > 0 ? n : weeks + n + 1;
                    if (w >= 1 && w <= weeks && !numbers.contains(w))
                        numbers.append(w);
                }
                std::ranges::sort(numbers);
                for (int w : numbers) {
                    for (int d = 0; d < 7; ++d)
                        days.append(first.addDays(7 * (w - 1) + d));
                }
            } else {
                const QDate jan1(year, 1, 1);
                const int count = jan1.daysInYear();
                days.reserve(count);
                for (int d = 0; d < count; ++d)
                    days.append(jan1.addDays(d));
            }
            break;
        }
        case RRule::Freq::Monthly: {
            const QDate first(start.date.year(), start.date.month(), 1);
            for (int d = 0; d < first.daysInMonth(); ++d)
                days.append(first.addDays(d));
            break;
        }
        case RRule::Freq::Weekly:
            for (int d = 0; d < 7; ++d)
                days.append(start.date.addDays(d));
            break;
        default: days.append(start.date); break;
        }
        return days;
    }

    /// The times of a day of the period starting at `start`.
    QList<QTime> periodTimes(const NaiveDateTime &start) const
    {
        if (dateOnly)
            return {QTime(0, 0)};
        const auto pick = [](const QList<int> &by, int own, bool periodLevel) {
            QList<int> out;
            if (periodLevel) {
                if (by.isEmpty() || by.contains(own))
                    out.append(own);
            } else {
                out = by.isEmpty() ? QList<int>{own} : by;
            }
            return out;
        };
        QList<int> hours = pick(r.byHour, start.time.hour(), r.freq <= RRule::Freq::Hourly);
        QList<int> minutes = pick(r.byMinute, start.time.minute(), r.freq <= RRule::Freq::Minutely);
        QList<int> seconds = pick(r.bySecond, start.time.second(), r.freq <= RRule::Freq::Secondly);
        std::ranges::sort(hours);
        std::ranges::sort(minutes);
        std::ranges::sort(seconds);
        QList<QTime> out;
        for (int h : hours) {
            for (int m : minutes) {
                for (int s : seconds)
                    out.append(QTime(h, m, std::min(s, 59)));
            }
        }
        return out;
    }

    /// The start of period `n` (0 = the one holding DTSTART); invalid beyond year 9999.
    NaiveDateTime periodStart(qint64 n) const
    {
        const qint64 step = n * r.interval;
        switch (r.freq) {
        case RRule::Freq::Yearly: {
            const qint64 year = dtstart.date.year() + step;
            if (year > MaxYear)
                return {};
            return {QDate(int(year), 1, 1), QTime(0, 0)};
        }
        case RRule::Freq::Monthly: {
            const qint64 months = qint64(dtstart.date.year()) * 12 + (dtstart.date.month() - 1) + step;
            if (months / 12 > MaxYear)
                return {};
            return {QDate(int(months / 12), int(months % 12) + 1, 1), QTime(0, 0)};
        }
        case RRule::Freq::Weekly: {
            const qint64 days = step * 7;
            if (days > 3'700'000)
                return {};
            const QDate d = weekStartOf(dtstart.date, r.weekStart).addDays(days);
            return d.isValid() && d.year() <= MaxYear ? NaiveDateTime{d, QTime(0, 0)} : NaiveDateTime{};
        }
        case RRule::Freq::Daily: {
            if (step > 3'700'000)
                return {};
            const QDate d = dtstart.date.addDays(step);
            return d.isValid() && d.year() <= MaxYear ? NaiveDateTime{d, QTime(0, 0)} : NaiveDateTime{};
        }
        case RRule::Freq::Hourly:
        case RRule::Freq::Minutely:
        case RRule::Freq::Secondly: {
            const qint64 unit = r.freq == RRule::Freq::Hourly ? 3600 : r.freq == RRule::Freq::Minutely ? 60 : 1;
            if (step > qint64(3'700'000) * 86400 / unit)
                return {};
            NaiveDateTime base = dtstart;
            if (r.freq == RRule::Freq::Hourly)
                base.time = QTime(base.time.hour(), 0);
            else if (r.freq == RRule::Freq::Minutely)
                base.time = QTime(base.time.hour(), base.time.minute());
            const NaiveDateTime s = base.addSecs(step * unit);
            return s.isValid() && s.date.year() <= MaxYear ? s : NaiveDateTime{};
        }
        }
        return {};
    }

    /// The first period worth looking at for a window starting at `from` (no COUNT).
    qint64 firstPeriod(const NaiveDateTime &from) const
    {
        if (from <= dtstart)
            return 0;
        qint64 units = 0;
        switch (r.freq) {
        case RRule::Freq::Yearly: units = from.date.year() - dtstart.date.year(); break;
        case RRule::Freq::Monthly:
            units = (qint64(from.date.year()) * 12 + from.date.month()) - (qint64(dtstart.date.year()) * 12 + dtstart.date.month());
            break;
        case RRule::Freq::Weekly:
            units = weekStartOf(dtstart.date, r.weekStart).daysTo(from.date) / 7;
            break;
        case RRule::Freq::Daily: units = dtstart.date.daysTo(from.date); break;
        case RRule::Freq::Hourly: units = dtstart.secsTo(from) / 3600; break;
        case RRule::Freq::Minutely: units = dtstart.secsTo(from) / 60; break;
        case RRule::Freq::Secondly: units = dtstart.secsTo(from); break;
        }
        // One period back: weeks of BYWEEKNO and a long period may reach into the window.
        return std::max<qint64>(0, units / r.interval - 1);
    }
};

} // namespace

NaiveDateTime NaiveDateTime::addSecs(qint64 seconds) const
{
    const QDateTime moved = QDateTime(date, time, QTimeZone::UTC).addSecs(seconds);
    return {moved.date(), moved.time()};
}

qint64 NaiveDateTime::secsTo(const NaiveDateTime &other) const
{
    return QDateTime(date, time, QTimeZone::UTC).secsTo(QDateTime(other.date, other.time, QTimeZone::UTC));
}

std::optional<RRule> parseRRule(QStringView value, QString *error)
{
    const auto fail = [error](const QString &why) -> std::optional<RRule> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    RRule rule;
    bool haveFreq = false;
    for (const QStringView rawPart : qTokenize(value.trimmed(), u';', Qt::SkipEmptyParts)) {
        const QStringView part = rawPart.trimmed();
        const qsizetype eq = part.indexOf(u'=');
        if (eq <= 0)
            return fail(u"malformed part %1"_s.arg(part));
        const QString key = part.first(eq).trimmed().toString().toUpper();
        const QString v = part.sliced(eq + 1).trimmed().toString().toUpper();
        if (key == u"FREQ") {
            static const QHash<QString, RRule::Freq> freqs = {
                    {u"SECONDLY"_s, RRule::Freq::Secondly}, {u"MINUTELY"_s, RRule::Freq::Minutely},
                    {u"HOURLY"_s, RRule::Freq::Hourly},     {u"DAILY"_s, RRule::Freq::Daily},
                    {u"WEEKLY"_s, RRule::Freq::Weekly},     {u"MONTHLY"_s, RRule::Freq::Monthly},
                    {u"YEARLY"_s, RRule::Freq::Yearly},
            };
            const auto it = freqs.constFind(v);
            if (it == freqs.constEnd())
                return fail(u"unknown FREQ %1"_s.arg(v));
            rule.freq = *it;
            haveFreq = true;
        } else if (key == u"INTERVAL") {
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 1 || n > 1'000'000)
                return fail(u"bad INTERVAL"_s);
            rule.interval = n;
        } else if (key == u"COUNT") {
            bool ok = false;
            const int n = v.toInt(&ok);
            if (!ok || n < 1)
                return fail(u"bad COUNT"_s);
            rule.count = n;
        } else if (key == u"UNTIL") {
            QString digits = v;
            digits.remove(u'-').remove(u':');
            RRule::Until until;
            until.utc = digits.endsWith(u'Z');
            if (until.utc)
                digits.chop(1);
            until.date = QDate::fromString(digits.left(8), u"yyyyMMdd"_s);
            if (!until.date.isValid())
                return fail(u"bad UNTIL"_s);
            if (digits.size() > 8) {
                if (digits.at(8) != u'T' || digits.size() < 13)
                    return fail(u"bad UNTIL"_s);
                const int h = digits.mid(9, 2).toInt();
                const int m = digits.mid(11, 2).toInt();
                const int s = digits.size() >= 15 ? std::min(59, digits.mid(13, 2).toInt()) : 0;
                const QTime t(h, m, s);
                if (!t.isValid())
                    return fail(u"bad UNTIL"_s);
                until.time = t;
            } else {
                until.utc = false;
            }
            rule.until = until;
        } else if (key == u"BYSECOND") {
            if (!parseIntList(v, 0, 60, false, rule.bySecond))
                return fail(u"bad BYSECOND"_s);
        } else if (key == u"BYMINUTE") {
            if (!parseIntList(v, 0, 59, false, rule.byMinute))
                return fail(u"bad BYMINUTE"_s);
        } else if (key == u"BYHOUR") {
            if (!parseIntList(v, 0, 23, false, rule.byHour))
                return fail(u"bad BYHOUR"_s);
        } else if (key == u"BYDAY") {
            for (const QStringView item : qTokenize(QStringView(v), u',', Qt::SkipEmptyParts)) {
                const QStringView t = item.trimmed();
                if (t.size() < 2)
                    return fail(u"bad BYDAY"_s);
                const auto day = weekdayFromCode(t.last(2));
                if (!day)
                    return fail(u"bad BYDAY"_s);
                int ordinal = 0;
                if (t.size() > 2) {
                    bool ok = false;
                    ordinal = t.first(t.size() - 2).toInt(&ok);
                    if (!ok || ordinal == 0 || ordinal < -53 || ordinal > 53)
                        return fail(u"bad BYDAY"_s);
                }
                const RRule::WeekdayNum wd{ordinal, *day};
                if (!rule.byDay.contains(wd))
                    rule.byDay.append(wd);
            }
            if (rule.byDay.isEmpty())
                return fail(u"bad BYDAY"_s);
        } else if (key == u"BYMONTHDAY") {
            if (!parseIntList(v, -31, 31, true, rule.byMonthDay))
                return fail(u"bad BYMONTHDAY"_s);
        } else if (key == u"BYYEARDAY") {
            if (!parseIntList(v, -366, 366, true, rule.byYearDay))
                return fail(u"bad BYYEARDAY"_s);
        } else if (key == u"BYWEEKNO") {
            if (!parseIntList(v, -53, 53, true, rule.byWeekNo))
                return fail(u"bad BYWEEKNO"_s);
        } else if (key == u"BYMONTH") {
            if (!parseIntList(v, 1, 12, false, rule.byMonth))
                return fail(u"bad BYMONTH"_s);
        } else if (key == u"BYSETPOS") {
            if (!parseIntList(v, -366, 366, true, rule.bySetPos))
                return fail(u"bad BYSETPOS"_s);
        } else if (key == u"WKST") {
            const auto day = weekdayFromCode(v);
            if (!day)
                return fail(u"bad WKST"_s);
            rule.weekStart = *day;
        }
        // Anything else (X-NAME, RSCALE, SKIP): ignored.
    }
    if (!haveFreq)
        return fail(u"no FREQ"_s);
    return rule;
}

QString formatRRule(const RRule &rule)
{
    static constexpr std::array<QStringView, 7> freqNames = {u"SECONDLY", u"MINUTELY", u"HOURLY", u"DAILY",
                                                             u"WEEKLY",   u"MONTHLY",  u"YEARLY"};
    QStringList parts{u"FREQ="_s + freqNames[int(rule.freq)].toString()};
    if (rule.until) {
        QString until = rule.until->date.toString(u"yyyyMMdd"_s);
        if (rule.until->time)
            until += u'T' + rule.until->time->toString(u"HHmmss"_s) + (rule.until->utc ? u"Z"_s : QString());
        parts.append(u"UNTIL="_s + until);
    }
    if (rule.count)
        parts.append(u"COUNT="_s + QString::number(*rule.count));
    if (rule.interval > 1)
        parts.append(u"INTERVAL="_s + QString::number(rule.interval));
    if (!rule.bySecond.isEmpty())
        parts.append(u"BYSECOND="_s + joinInts(rule.bySecond));
    if (!rule.byMinute.isEmpty())
        parts.append(u"BYMINUTE="_s + joinInts(rule.byMinute));
    if (!rule.byHour.isEmpty())
        parts.append(u"BYHOUR="_s + joinInts(rule.byHour));
    if (!rule.byDay.isEmpty()) {
        QStringList days;
        for (const RRule::WeekdayNum &wd : rule.byDay)
            days.append((wd.ordinal ? QString::number(wd.ordinal) : QString()) + WeekdayCodes[wd.weekday - 1].toString());
        parts.append(u"BYDAY="_s + days.join(u','));
    }
    if (!rule.byMonthDay.isEmpty())
        parts.append(u"BYMONTHDAY="_s + joinInts(rule.byMonthDay));
    if (!rule.byYearDay.isEmpty())
        parts.append(u"BYYEARDAY="_s + joinInts(rule.byYearDay));
    if (!rule.byWeekNo.isEmpty())
        parts.append(u"BYWEEKNO="_s + joinInts(rule.byWeekNo));
    if (!rule.byMonth.isEmpty())
        parts.append(u"BYMONTH="_s + joinInts(rule.byMonth));
    if (!rule.bySetPos.isEmpty())
        parts.append(u"BYSETPOS="_s + joinInts(rule.bySetPos));
    if (rule.weekStart != 1)
        parts.append(u"WKST="_s + WeekdayCodes[rule.weekStart - 1].toString());
    return parts.join(u';');
}

QList<NaiveDateTime> expandRRule(const RRule &rule, const NaiveDateTime &dtstart, bool dateOnly,
                                 const NaiveDateTime &from, const NaiveDateTime &to,
                                 const std::optional<NaiveDateTime> &until, const ExpandLimits &limits,
                                 bool *truncated)
{
    if (truncated)
        *truncated = false;
    QList<NaiveDateTime> out;
    if (!dtstart.isValid() || !(from < to))
        return out;
    Expander ex;
    ex.r = rule;
    ex.dtstart = dtstart;
    ex.dateOnly = dateOnly;
    ex.limits = limits;
    RRule &r = ex.r;
    r.interval = std::max(1, r.interval);
    if (dateOnly) {
        ex.dtstart.time = QTime(0, 0);
        r.byHour.clear();
        r.byMinute.clear();
        r.bySecond.clear();
        if (r.freq < RRule::Freq::Daily)
            r.freq = RRule::Freq::Daily; // an hourly all-day series: one a day at most
    }
    // RFC 5545: what the rule leaves open comes from DTSTART.
    if (r.byWeekNo.isEmpty() && r.byYearDay.isEmpty() && r.byMonthDay.isEmpty() && r.byDay.isEmpty()) {
        if (r.freq == RRule::Freq::Yearly) {
            if (r.byMonth.isEmpty())
                r.byMonth = {ex.dtstart.date.month()};
            r.byMonthDay = {ex.dtstart.date.day()};
        } else if (r.freq == RRule::Freq::Monthly) {
            r.byMonthDay = {ex.dtstart.date.day()};
        } else if (r.freq == RRule::Freq::Weekly) {
            r.byDay = {RRule::WeekdayNum{0, ex.dtstart.date.dayOfWeek()}};
        }
    }
    if (!dateOnly) {
        if (r.byHour.isEmpty() && r.freq > RRule::Freq::Hourly)
            r.byHour = {ex.dtstart.time.hour()};
        if (r.byMinute.isEmpty() && r.freq > RRule::Freq::Minutely)
            r.byMinute = {ex.dtstart.time.minute()};
        if (r.bySecond.isEmpty() && r.freq > RRule::Freq::Secondly)
            r.bySecond = {ex.dtstart.time.second()};
    }

    const NaiveDateTime start = ex.dtstart;
    const auto beyondUntil = [&until](const NaiveDateTime &t) { return until && t > *until; };
    int emitted = 0; // occurrences counted for COUNT
    const auto take = [&](const NaiveDateTime &t) -> bool { // false: stop
        if (beyondUntil(t))
            return false;
        ++emitted;
        if (t >= from && t < to) {
            if (out.size() >= limits.maxOccurrences) {
                if (truncated)
                    *truncated = true;
                return false;
            }
            out.append(t);
        }
        if (r.count && emitted >= *r.count)
            return false;
        return t < to;
    };
    // DTSTART is always the first occurrence.
    if (!take(start))
        return out;

    qint64 period = r.count ? 0 : ex.firstPeriod(from);
    int periods = 0;
    while (true) {
        if (++periods > limits.maxPeriods) {
            if (truncated)
                *truncated = true;
            break;
        }
        const NaiveDateTime pstart = ex.periodStart(period++);
        if (!pstart.isValid())
            break;
        // Every candidate of a period lies after its first day (less a week: BYWEEKNO).
        if (NaiveDateTime{pstart.date.addDays(-7), QTime(0, 0)} >= to || beyondUntil({pstart.date.addDays(-7), QTime(0, 0)}))
            break;
        QList<NaiveDateTime> set;
        const QList<QTime> times = ex.periodTimes(pstart);
        if (times.isEmpty())
            continue;
        for (const QDate day : ex.periodDays(pstart)) {
            if (++ex.candidates > limits.maxCandidates)
                break;
            if (!ex.dayMatches(day))
                continue;
            for (const QTime t : times)
                set.append(NaiveDateTime{day, t});
        }
        if (ex.candidates > limits.maxCandidates) {
            if (truncated)
                *truncated = true;
            break;
        }
        if (set.isEmpty())
            continue;
        std::ranges::sort(set);
        if (!r.bySetPos.isEmpty()) {
            QList<NaiveDateTime> picked;
            const qsizetype n = set.size();
            for (int pos : r.bySetPos) {
                const qsizetype i = pos > 0 ? pos - 1 : n + pos;
                if (i >= 0 && i < n && !picked.contains(set.at(i)))
                    picked.append(set.at(i));
            }
            std::ranges::sort(picked);
            set = picked;
        }
        bool stop = false;
        for (const NaiveDateTime &t : std::as_const(set)) {
            if (t <= start)
                continue; // before the series, or DTSTART itself (counted already)
            if (!take(t)) {
                stop = true;
                break;
            }
        }
        if (stop)
            break;
    }
    return out;
}

} // namespace brulion::caldav
