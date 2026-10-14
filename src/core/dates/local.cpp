#include "dates/local.h"

namespace brulion::dates {

namespace {

bool isAsciiDigit(QChar c)
{
    return c >= u'0' && c <= u'9';
}

/// Value of `count` ASCII digits at `pos`, or -1 when they are not all digits / out of range.
int digitsAt(QStringView s, qsizetype pos, qsizetype count)
{
    if (pos < 0 || pos + count > s.size())
        return -1;
    int value = 0;
    for (qsizetype i = pos; i < pos + count; ++i) {
        if (!isAsciiDigit(s.at(i)))
            return -1;
        value = value * 10 + (s.at(i).unicode() - u'0');
    }
    return value;
}

/// "YYYY-MM-DD" at the start of `s` (exactly 10 characters consumed), else invalid QDate.
QDate isoDatePrefix(QStringView s)
{
    if (s.size() < 10 || s.at(4) != u'-' || s.at(7) != u'-')
        return {};
    const int y = digitsAt(s, 0, 4);
    const int m = digitsAt(s, 5, 2);
    const int d = digitsAt(s, 8, 2);
    if (y < 0 || m < 0 || d < 0)
        return {};
    const QDate date(y, m, d);
    return isSupportedDate(date) ? date : QDate();
}

/// "H:mm" / "HH:mm" with optional ":ss" or ":ss.fff" (dropped) covering all of `s`.
std::optional<QTime> lenientTime(QStringView s)
{
    const qsizetype colon = s.indexOf(u':');
    if (colon < 1 || colon > 2)
        return std::nullopt;
    const int h = digitsAt(s, 0, colon);
    const int m = digitsAt(s, colon + 1, 2);
    if (h < 0 || m < 0 || h > 23 || m > 59)
        return std::nullopt;
    QStringView rest = s.sliced(colon + 3);
    if (!rest.isEmpty()) {
        if (rest.size() < 3 || rest.at(0) != u':' || digitsAt(rest, 1, 2) < 0)
            return std::nullopt;
        rest = rest.sliced(3);
        if (!rest.isEmpty()) {
            if (rest.size() < 2 || rest.at(0) != u'.' || digitsAt(rest, 1, rest.size() - 1) < 0)
                return std::nullopt;
        }
    }
    return QTime(h, m);
}

} // namespace

bool isSupportedDate(QDate date)
{
    return date.isValid() && date.year() >= MinSupportedYear && date.year() <= MaxSupportedYear;
}

std::optional<DateSpec> parseDateSpec(QStringView text)
{
    const QStringView s = text.trimmed();
    const QDate date = isoDatePrefix(s);
    if (!date.isValid())
        return std::nullopt;
    if (s.size() == 10)
        return DateSpec{date, std::nullopt};

    QStringView rest = s.sliced(10);
    if (rest.at(0) == u'T') {
        rest = rest.sliced(1);
    } else if (rest.at(0).isSpace()) {
        qsizetype i = 0;
        while (i < rest.size() && rest.at(i).isSpace())
            ++i;
        rest = rest.sliced(i);
    } else {
        return std::nullopt;
    }
    const auto time = lenientTime(rest);
    if (!time)
        return std::nullopt;
    return DateSpec{date, time};
}

std::optional<QDate> parseIsoDate(QStringView text)
{
    if (text.size() != 10)
        return std::nullopt;
    const QDate date = isoDatePrefix(text);
    return date.isValid() ? std::optional<QDate>(date) : std::nullopt;
}

std::optional<QTime> parseClockTime(QStringView text)
{
    const QStringView s = text.trimmed();
    const qsizetype colon = s.indexOf(u':');
    if (colon < 1 || colon > 2 || s.size() != colon + 3)
        return std::nullopt;
    return lenientTime(s);
}

int compareDateSpecs(const DateSpec &a, const DateSpec &b)
{
    if (!a.isValid() || !b.isValid())
        return int(!a.isValid()) - int(!b.isValid());
    if (a.date != b.date)
        return a.date < b.date ? -1 : 1;
    if (a.time == b.time)
        return 0;
    if (!a.time)
        return -1;
    if (!b.time)
        return 1;
    return *a.time < *b.time ? -1 : 1;
}

QDate startOfWeek(QDate date, Qt::DayOfWeek weekStart)
{
    if (!date.isValid())
        return {};
    const int start = sanitizeWeekStart(int(weekStart));
    return date.addDays(-((date.dayOfWeek() - start + 7) % 7));
}

QDate endOfWeek(QDate date, Qt::DayOfWeek weekStart)
{
    const QDate start = startOfWeek(date, weekStart);
    return start.isValid() ? start.addDays(6) : QDate();
}

QDate startOfMonth(QDate date)
{
    return date.isValid() ? QDate(date.year(), date.month(), 1) : QDate();
}

QDate endOfMonth(QDate date)
{
    return date.isValid() ? QDate(date.year(), date.month(), date.daysInMonth()) : QDate();
}

Qt::DayOfWeek sanitizeWeekStart(int value)
{
    if (value == 0 || value == 7)
        return Qt::Sunday;
    if (value >= 1 && value <= 6)
        return Qt::DayOfWeek(value);
    return Qt::Monday;
}

} // namespace brulion::dates
