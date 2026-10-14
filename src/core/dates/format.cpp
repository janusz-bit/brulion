#include "dates/format.h"

using namespace Qt::StringLiterals;

namespace brulion::dates {

namespace {

/// "dziś"/"today" etc. for -1…1 days from today; empty otherwise.
QString relativeDayWord(qint64 days, UiLocale locale)
{
    const bool pl = locale == UiLocale::Polish;
    if (days == -1)
        return pl ? u"wczoraj"_s : u"yesterday"_s;
    if (days == 0)
        return pl ? u"dziś"_s : u"today"_s;
    if (days == 1)
        return pl ? u"jutro"_s : u"tomorrow"_s;
    return {};
}

} // namespace

QString formatDateShort(QDate date, UiLocale locale, std::optional<int> currentYear)
{
    if (!date.isValid())
        return {};
    const QString day = QString::number(date.day());
    const QString month = monthName(date.month(), locale, NameForm::Short);
    const QString weekday = weekdayName(date.dayOfWeek(), locale, NameForm::Short);
    const bool sameYear = !currentYear || *currentYear == date.year();
    const QString year = QString::number(date.year());
    if (locale == UiLocale::Polish)
        return sameYear ? u"%1 %2 %3"_s.arg(weekday, day, month) : u"%1 %2 %3"_s.arg(day, month, year);
    return sameYear ? u"%1, %2 %3"_s.arg(weekday, month, day) : u"%1 %2, %3"_s.arg(month, day, year);
}

QString formatHuman(const DateSpec &spec, UiLocale locale, QDate today)
{
    if (!spec.isValid())
        return {};
    QString out = today.isValid() ? relativeDayWord(today.daysTo(spec.date), locale) : QString();
    if (out.isEmpty()) {
        // Without a valid "today" the year is always shown (year 0 never matches a QDate).
        out = formatDateShort(spec.date, locale, today.isValid() ? today.year() : 0);
    }
    if (spec.time) {
        out += u' ';
        out += spec.time->toString(u"HH:mm"_s);
    }
    return out;
}

} // namespace brulion::dates
