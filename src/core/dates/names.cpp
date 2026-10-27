#include "dates/names.h"

#include <array>

namespace brulion::dates {

namespace {

using Names7 = std::array<const char16_t *, 7>;   // Monday … Sunday
using Names12 = std::array<const char16_t *, 12>; // January … December

constexpr Names7 PlWeekdays = {u"poniedziałek", u"wtorek", u"środa", u"czwartek", u"piątek", u"sobota", u"niedziela"};
constexpr Names7 PlWeekdaysGen = {u"poniedziałku", u"wtorku", u"środy", u"czwartku", u"piątku", u"soboty", u"niedzieli"};
constexpr Names7 PlWeekdaysAcc = {u"poniedziałek", u"wtorek", u"środę", u"czwartek", u"piątek", u"sobotę", u"niedzielę"};
constexpr Names7 PlWeekdaysShort = {u"pn", u"wt", u"śr", u"czw", u"pt", u"sob", u"nd"};
constexpr Names7 EnWeekdays = {u"Monday", u"Tuesday", u"Wednesday", u"Thursday", u"Friday", u"Saturday", u"Sunday"};
constexpr Names7 EnWeekdaysShort = {u"Mon", u"Tue", u"Wed", u"Thu", u"Fri", u"Sat", u"Sun"};

constexpr Names12 PlMonths = {u"styczeń", u"luty",     u"marzec",   u"kwiecień",    u"maj",      u"czerwiec",
                              u"lipiec",  u"sierpień", u"wrzesień", u"październik", u"listopad", u"grudzień"};
constexpr Names12 PlMonthsGen = {u"stycznia", u"lutego",   u"marca",    u"kwietnia",     u"maja",      u"czerwca",
                                 u"lipca",    u"sierpnia", u"września", u"października", u"listopada", u"grudnia"};
constexpr Names12 PlMonthsShort = {u"sty", u"lut", u"mar", u"kwi", u"maj", u"cze",
                                   u"lip", u"sie", u"wrz", u"paź", u"lis", u"gru"};
constexpr Names12 EnMonths = {u"January", u"February", u"March",     u"April",   u"May",      u"June",
                              u"July",    u"August",   u"September", u"October", u"November", u"December"};
constexpr Names12 EnMonthsShort = {u"Jan", u"Feb", u"Mar", u"Apr", u"May", u"Jun",
                                   u"Jul", u"Aug", u"Sep", u"Oct", u"Nov", u"Dec"};

template<std::size_t N>
QString pick(const std::array<const char16_t *, N> &names, int oneBased)
{
    if (oneBased < 1 || oneBased > int(N))
        return {};
    return QString::fromUtf16(names[std::size_t(oneBased - 1)]);
}

const Names7 &polishWeekdayTable(NameForm form)
{
    switch (form) {
    case NameForm::Genitive:
        return PlWeekdaysGen;
    case NameForm::Accusative:
        return PlWeekdaysAcc;
    case NameForm::Short:
        return PlWeekdaysShort;
    case NameForm::Nominative:
        break;
    }
    return PlWeekdays;
}

const Names12 &polishMonthTable(NameForm form)
{
    switch (form) {
    case NameForm::Genitive:
        return PlMonthsGen;
    case NameForm::Short:
        return PlMonthsShort;
    case NameForm::Nominative:
    case NameForm::Accusative:
        break;
    }
    return PlMonths;
}

} // namespace

QString polishWeekdayName(int dayOfWeek, NameForm form)
{
    return pick(polishWeekdayTable(form), dayOfWeek);
}

QString polishMonthName(int month, NameForm form)
{
    return pick(polishMonthTable(form), month);
}

QString weekdayName(int dayOfWeek, UiLocale locale, NameForm form)
{
    if (locale == UiLocale::Polish)
        return polishWeekdayName(dayOfWeek, form);
    return pick(form == NameForm::Short ? EnWeekdaysShort : EnWeekdays, dayOfWeek);
}

QString monthName(int month, UiLocale locale, NameForm form)
{
    if (locale == UiLocale::Polish)
        return polishMonthName(month, form);
    return pick(form == NameForm::Short ? EnMonthsShort : EnMonths, month);
}

QString longDate(QDate date, UiLocale locale)
{
    if (!date.isValid())
        return {};
    const QString weekday = weekdayName(date.dayOfWeek(), locale, NameForm::Nominative);
    const QString day = QString::number(date.day());
    const QString year = QString::number(date.year());
    if (locale == UiLocale::English)
        return QStringLiteral("%1, %2 %3, %4").arg(weekday, monthName(date.month(), locale, NameForm::Nominative), day, year);
    return QStringLiteral("%1, %2 %3 %4").arg(weekday, day, monthName(date.month(), locale, NameForm::Genitive), year);
}

QString englishOrdinal(int n)
{
    const int mod100 = n % 100;
    const int mod10 = n % 10;
    const char16_t *suffix = u"th";
    if (mod100 < 11 || mod100 > 13) {
        if (mod10 == 1)
            suffix = u"st";
        else if (mod10 == 2)
            suffix = u"nd";
        else if (mod10 == 3)
            suffix = u"rd";
    }
    return QString::number(n) + QString::fromUtf16(suffix);
}

QString polishPlural(qint64 n, QStringView one, QStringView few, QStringView many)
{
    if (n == 1)
        return one.toString();
    const qint64 mod10 = n % 10;
    const qint64 mod100 = n % 100;
    const bool isFew = mod10 >= 2 && mod10 <= 4 && (mod100 < 12 || mod100 > 14);
    return (isFew ? few : many).toString();
}

} // namespace brulion::dates
