// Local date helpers, names, vocabulary, human formatting and query date expressions.
// Test cases ported from archive/web-v1 (local/names/format/expr tests).
#include "dates/dates.h"
#include "dates/vocabulary_p.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>

using namespace brulion;
using namespace brulion::dates;
using namespace Qt::StringLiterals;

namespace {

/// Wednesday, 14 October 2026.
const QDate Today(2026, 10, 14);

QDate d(QStringView iso)
{
    const auto date = parseIsoDate(iso);
    return date.value_or(QDate());
}

QString describe(const std::optional<DateSpec> &spec)
{
    return spec ? spec->toString() : u"null"_s;
}

/// "from..to", "overdue"/"none"/"any" or "null".
QString describe(const std::optional<DateExpr> &expr)
{
    if (!expr)
        return u"null"_s;
    if (const auto *range = std::get_if<DateRange>(&*expr)) {
        return range->from.toString(u"yyyy-MM-dd"_s) + u".."_s + range->to.toString(u"yyyy-MM-dd"_s);
    }
    switch (std::get<SpecialDate>(*expr)) {
    case SpecialDate::Overdue:
        return u"overdue"_s;
    case SpecialDate::None:
        return u"none"_s;
    case SpecialDate::Any:
        break;
    }
    return u"any"_s;
}

QString dayRange(const QString &iso)
{
    return iso + u".."_s + iso;
}

QString expr(QStringView text, QDate today = Today, Qt::DayOfWeek weekStart = Qt::Monday)
{
    return describe(resolveDateExpr(text, today, weekStart));
}

DateSpec spec(QStringView text)
{
    return parseDateSpec(text).value_or(DateSpec{});
}

} // namespace

class TestDates : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ==== local.h =======================================================================
    void parseDateSpec_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected"); // "null" when rejected
        QTest::newRow("date") << u"2026-10-14"_s << u"2026-10-14"_s;
        QTest::newRow("space") << u"2026-10-14 18:30"_s << u"2026-10-14 18:30"_s;
        QTest::newRow("T") << u"2026-10-14T18:30"_s << u"2026-10-14 18:30"_s;
        QTest::newRow("one-digit hour") << u"2026-10-15 9:05"_s << u"2026-10-15 09:05"_s;
        QTest::newRow("seconds") << u"2026-10-15T09:00:59"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("fraction") << u"2026-10-15T09:00:59.123"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("whitespace") << u"  2026-10-15   18:30 "_s << u"2026-10-15 18:30"_s;
        QTest::newRow("leap day") << u"2028-02-29"_s << u"2028-02-29"_s;
        QTest::newRow("small year kept literal") << u"0099-03-01"_s << u"0099-03-01"_s;
        QTest::newRow("last day") << u"9999-12-31 23:59"_s << u"9999-12-31 23:59"_s;
        QTest::newRow("empty") << QString() << u"null"_s;
        QTest::newRow("Feb 30") << u"2026-02-30"_s << u"null"_s;
        QTest::newRow("month 13") << u"2026-13-01"_s << u"null"_s;
        QTest::newRow("polish order") << u"14.10.2026"_s << u"null"_s;
        QTest::newRow("hour 25") << u"2026-10-14T25:00"_s << u"null"_s;
        QTest::newRow("hour 24") << u"2026-10-15 24:00"_s << u"null"_s;
        QTest::newRow("minute 60") << u"2026-10-15 10:60"_s << u"null"_s;
        QTest::newRow("word") << u"jutro"_s << u"null"_s;
        QTest::newRow("short month") << u"2026-1-01"_s << u"null"_s;
        QTest::newRow("no minutes") << u"2026-10-14T9"_s << u"null"_s;
        QTest::newRow("trailing junk") << u"2026-10-14 18:30x"_s << u"null"_s;
        QTest::newRow("year 0") << u"0000-01-01"_s << u"null"_s;
        QTest::newRow("bad seconds") << u"2026-10-14 18:30:5"_s << u"null"_s;
        QTest::newRow("glued") << u"2026-10-1418:30"_s << u"null"_s;
    }
    void parseDateSpec()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(describe(dates::parseDateSpec(input)), expected);
    }

    void parseIsoDate_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<bool>("valid");
        QTest::newRow("plain") << u"2026-10-14"_s << true;
        QTest::newRow("leap") << u"2028-02-29"_s << true;
        QTest::newRow("not leap") << u"2026-02-29"_s << false;
        QTest::newRow("century") << u"2100-02-29"_s << false;
        QTest::newRow("400") << u"2000-02-29"_s << true;
        QTest::newRow("month 13") << u"2026-13-01"_s << false;
        QTest::newRow("month 0") << u"2026-00-10"_s << false;
        QTest::newRow("Apr 31") << u"2026-04-31"_s << false;
        QTest::newRow("short") << u"2026-1-01"_s << false;
        QTest::newRow("space") << u" 2026-10-14"_s << false;
        QTest::newRow("date-time") << u"2026-10-14T09:00"_s << false;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("unicode digits") << u"٢٠٢٦-10-14"_s << false;
    }
    void parseIsoDate()
    {
        QFETCH(QString, input);
        QFETCH(bool, valid);
        QCOMPARE(dates::parseIsoDate(input).has_value(), valid);
    }

    void parseClockTime_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("HH:mm") << u"09:30"_s << u"09:30"_s;
        QTest::newRow("H:mm") << u"9:30"_s << u"09:30"_s;
        QTest::newRow("max") << u"23:59"_s << u"23:59"_s;
        QTest::newRow("spaces") << u" 7:05 "_s << u"07:05"_s;
        QTest::newRow("minute 60") << u"23:60"_s << QString();
        QTest::newRow("hour 24") << u"24:00"_s << QString();
        QTest::newRow("word") << u"rano"_s << QString();
        QTest::newRow("one-digit minute") << u"7:5"_s << QString();
        QTest::newRow("three-digit hour") << u"123:00"_s << QString();
        QTest::newRow("seconds") << u"09:30:00"_s << QString();
    }
    void parseClockTime()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        const auto t = dates::parseClockTime(input);
        QCOMPARE(t ? t->toString(u"HH:mm"_s) : QString(), expected);
    }

    void supportedRange()
    {
        QVERIFY(isSupportedDate(QDate(1, 1, 1)));
        QVERIFY(isSupportedDate(QDate(9999, 12, 31)));
        QVERIFY(!isSupportedDate(QDate(10000, 1, 1)));
        QVERIFY(!isSupportedDate(QDate(-1, 12, 31)));
        QVERIFY(!isSupportedDate(QDate()));
    }

    void compareDateSpecs()
    {
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-14"), spec(u"2026-10-15")) < 0);
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-15"), spec(u"2026-10-14")) > 0);
        QCOMPARE(dates::compareDateSpecs(spec(u"2026-10-14"), spec(u"2026-10-14")), 0);
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-14"), spec(u"2026-10-14 00:00")) < 0);
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-14 00:00"), spec(u"2026-10-14")) > 0);
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-14 09:00"), spec(u"2026-10-14 18:00")) < 0);
        QVERIFY(dates::compareDateSpecs(spec(u"2026-10-14 23:59"), spec(u"2026-10-15")) < 0);
        QCOMPARE(dates::compareDateSpecs(spec(u"2026-10-14 09:00"), spec(u"2026-10-14T09:00")), 0);
        // Invalid specs sort last.
        QList<DateSpec> list = {DateSpec{}, spec(u"2026-10-15"), spec(u"2026-10-14 10:00"), spec(u"2026-10-14")};
        std::sort(list.begin(), list.end(),
                  [](const DateSpec &a, const DateSpec &b) { return dates::compareDateSpecs(a, b) < 0; });
        QCOMPARE(list.at(0), spec(u"2026-10-14"));
        QCOMPARE(list.at(1), spec(u"2026-10-14 10:00"));
        QCOMPARE(list.at(2), spec(u"2026-10-15"));
        QVERIFY(!list.at(3).isValid());
        QCOMPARE(dates::compareDateSpecs(DateSpec{}, DateSpec{}), 0);
    }

    void weekBoundaries_data()
    {
        QTest::addColumn<QDate>("date");
        QTest::addColumn<int>("weekStart");
        QTest::addColumn<QDate>("start");
        QTest::addColumn<QDate>("end");
        QTest::newRow("wed, monday") << d(u"2026-10-14") << int(Qt::Monday) << d(u"2026-10-12") << d(u"2026-10-18");
        QTest::newRow("sun, monday") << d(u"2026-10-18") << int(Qt::Monday) << d(u"2026-10-12") << d(u"2026-10-18");
        QTest::newRow("mon, monday") << d(u"2026-10-12") << int(Qt::Monday) << d(u"2026-10-12") << d(u"2026-10-18");
        QTest::newRow("wed, sunday") << d(u"2026-10-14") << int(Qt::Sunday) << d(u"2026-10-11") << d(u"2026-10-17");
        QTest::newRow("sun, sunday") << d(u"2026-10-18") << int(Qt::Sunday) << d(u"2026-10-18") << d(u"2026-10-24");
        QTest::newRow("wed, saturday") << d(u"2026-10-14") << int(Qt::Saturday) << d(u"2026-10-10")
                                       << d(u"2026-10-16");
        QTest::newRow("across year") << d(u"2027-01-01") << int(Qt::Monday) << d(u"2026-12-28") << d(u"2027-01-03");
        QTest::newRow("malformed = monday") << d(u"2026-10-14") << 42 << d(u"2026-10-12") << d(u"2026-10-18");
    }
    void weekBoundaries()
    {
        QFETCH(QDate, date);
        QFETCH(int, weekStart);
        QFETCH(QDate, start);
        QFETCH(QDate, end);
        QCOMPARE(startOfWeek(date, static_cast<Qt::DayOfWeek>(weekStart)), start);
        QCOMPARE(endOfWeek(date, static_cast<Qt::DayOfWeek>(weekStart)), end);
    }
    void monthBoundaries()
    {
        QCOMPARE(startOfMonth(d(u"2026-10-14")), d(u"2026-10-01"));
        QCOMPARE(endOfMonth(d(u"2026-10-14")), d(u"2026-10-31"));
        QCOMPARE(endOfMonth(d(u"2028-02-03")), d(u"2028-02-29"));
        QCOMPARE(endOfMonth(d(u"2026-02-03")), d(u"2026-02-28"));
        QCOMPARE(endOfMonth(d(u"2026-04-30")), d(u"2026-04-30"));
        QVERIFY(!startOfMonth(QDate()).isValid());
        QVERIFY(!endOfWeek(QDate()).isValid());
    }
    void sanitizeWeekStart()
    {
        QCOMPARE(dates::sanitizeWeekStart(1), Qt::Monday);
        QCOMPARE(dates::sanitizeWeekStart(0), Qt::Sunday); // JavaScript / config convention
        QCOMPARE(dates::sanitizeWeekStart(7), Qt::Sunday); // Qt convention
        QCOMPARE(dates::sanitizeWeekStart(6), Qt::Saturday);
        QCOMPARE(dates::sanitizeWeekStart(-1), Qt::Monday);
        QCOMPARE(dates::sanitizeWeekStart(8), Qt::Monday);
    }

    // ==== names.h =======================================================================
    void weekdayNames()
    {
        QCOMPARE(polishWeekdayName(3), u"środa"_s);
        QCOMPARE(polishWeekdayName(3, NameForm::Accusative), u"środę"_s);
        QCOMPARE(polishWeekdayName(3, NameForm::Genitive), u"środy"_s);
        QCOMPARE(polishWeekdayName(3, NameForm::Short), u"śr"_s);
        QCOMPARE(polishWeekdayName(7, NameForm::Accusative), u"niedzielę"_s);
        QCOMPARE(polishWeekdayName(1, NameForm::Genitive), u"poniedziałku"_s);
        QCOMPARE(polishWeekdayName(5, NameForm::Short), u"pt"_s);
        QCOMPARE(weekdayName(5, UiLocale::Polish), u"piątek"_s);
        QCOMPARE(weekdayName(3, UiLocale::English), u"Wednesday"_s);
        QCOMPARE(weekdayName(3, UiLocale::English, NameForm::Genitive), u"Wednesday"_s);
        QCOMPARE(weekdayName(7, UiLocale::English, NameForm::Short), u"Sun"_s);
        QCOMPARE(weekdayName(0, UiLocale::English), QString());
        QCOMPARE(polishWeekdayName(8), QString());
    }
    void monthNames()
    {
        QCOMPARE(polishMonthName(10), u"październik"_s);
        QCOMPARE(polishMonthName(10, NameForm::Genitive), u"października"_s);
        QCOMPARE(polishMonthName(10, NameForm::Accusative), u"październik"_s);
        QCOMPARE(polishMonthName(10, NameForm::Short), u"paź"_s);
        QCOMPARE(polishMonthName(9, NameForm::Genitive), u"września"_s);
        QCOMPARE(monthName(5, UiLocale::English), u"May"_s);
        QCOMPARE(monthName(10, UiLocale::English, NameForm::Short), u"Oct"_s);
        QCOMPARE(monthName(13, UiLocale::English, NameForm::Short), QString());
        QCOMPARE(monthName(0, UiLocale::Polish), QString());
    }
    void longDate_data()
    {
        QTest::addColumn<QDate>("date");
        QTest::addColumn<bool>("english");
        QTest::addColumn<QString>("expected");
        QTest::newRow("pl") << QDate(2026, 10, 27) << false << u"wtorek, 27 października 2026"_s;
        QTest::newRow("pl, genitive of a soft month") << QDate(2026, 3, 1) << false << u"niedziela, 1 marca 2026"_s;
        QTest::newRow("pl, diacritics") << QDate(2027, 9, 15) << false << u"środa, 15 września 2027"_s;
        QTest::newRow("pl, February 29") << QDate(2028, 2, 29) << false << u"wtorek, 29 lutego 2028"_s;
        QTest::newRow("en") << QDate(2026, 10, 27) << true << u"Tuesday, October 27, 2026"_s;
        QTest::newRow("en, January 1") << QDate(2027, 1, 1) << true << u"Friday, January 1, 2027"_s;
        QTest::newRow("invalid") << QDate() << false << QString();
        QTest::newRow("invalid, en") << QDate(2026, 2, 30) << true << QString();
    }
    void longDate()
    {
        QFETCH(QDate, date);
        QFETCH(bool, english);
        QFETCH(QString, expected);
        QCOMPARE(dates::longDate(date, english ? UiLocale::English : UiLocale::Polish), expected);
    }
    void englishOrdinal()
    {
        QStringList got;
        for (const int n : {1, 2, 3, 4, 11, 12, 13, 21, 22, 23, 31, 101, 111, 112})
            got.append(dates::englishOrdinal(n));
        QCOMPARE(got, QStringList({u"1st"_s, u"2nd"_s, u"3rd"_s, u"4th"_s, u"11th"_s, u"12th"_s, u"13th"_s, u"21st"_s,
                                   u"22nd"_s, u"23rd"_s, u"31st"_s, u"101st"_s, u"111th"_s, u"112th"_s}));
    }
    void polishPlural_data()
    {
        QTest::addColumn<int>("n");
        QTest::addColumn<QString>("expected");
        QTest::newRow("1") << 1 << u"tydzień"_s;
        QTest::newRow("2") << 2 << u"tygodnie"_s;
        QTest::newRow("4") << 4 << u"tygodnie"_s;
        QTest::newRow("5") << 5 << u"tygodni"_s;
        QTest::newRow("0") << 0 << u"tygodni"_s;
        QTest::newRow("12") << 12 << u"tygodni"_s;
        QTest::newRow("14") << 14 << u"tygodni"_s;
        QTest::newRow("22") << 22 << u"tygodnie"_s;
        QTest::newRow("25") << 25 << u"tygodni"_s;
        QTest::newRow("104") << 104 << u"tygodnie"_s;
        QTest::newRow("112") << 112 << u"tygodni"_s;
    }
    void polishPlural()
    {
        QFETCH(int, n);
        QFETCH(QString, expected);
        QCOMPARE(dates::polishPlural(n, u"tydzień", u"tygodnie", u"tygodni"), expected);
    }

    // ==== vocabulary (internal) =========================================================
    void vocabularyWeekdays()
    {
        using detail::WeekdayWord;
        QCOMPARE(detail::lookupWeekday(u"srode"), std::optional<WeekdayWord>(WeekdayWord{3, false, false}));
        QCOMPARE(detail::lookupWeekday(u"niedziele")->day, 7);
        QCOMPARE(detail::lookupWeekday(u"pt"), std::optional<WeekdayWord>(WeekdayWord{5, true, false}));
        QCOMPARE(detail::lookupWeekday(u"poniedzialki"), std::optional<WeekdayWord>(WeekdayWord{1, false, true}));
        QCOMPARE(detail::lookupWeekday(u"thurs")->day, 4);
        QVERIFY(!detail::lookupWeekday(u"środę")); // keys are folded
        QVERIFY(!detail::lookupWeekday(u""));
    }
    void vocabularyMonths()
    {
        using detail::MonthWord;
        QCOMPARE(detail::lookupMonth(u"pazdziernika"), std::optional<MonthWord>(MonthWord{10, false, false}));
        QCOMPARE(detail::lookupMonth(u"paz"), std::optional<MonthWord>(MonthWord{10, false, true}));
        QCOMPARE(detail::lookupMonth(u"oct"), std::optional<MonthWord>(MonthWord{10, true, true}));
        QCOMPARE(detail::lookupMonth(u"maj")->month, 5);
        QCOMPARE(detail::lookupMonth(u"may")->month, 5);
        QCOMPARE(detail::lookupMonth(u"sept")->month, 9);
        QVERIFY(!detail::lookupMonth(u"constructor"));
    }
    void vocabularyHelpers()
    {
        QCOMPARE(detail::phraseAlternation({u"rano"_s, u"w nocy"_s, u"po poludniu"_s}),
                 uR"(po\s+poludniu|w\s+nocy|rano)"_s);
        QCOMPARE(detail::collapseSpaces(u"w \t ciagu  godziny"), u"w ciagu godziny"_s);
        QCOMPARE(detail::collapseSpaces(u" a "), u" a "_s);
        QCOMPARE(detail::numberWords().value(u"dwie"_s), 2);
        QCOMPARE(detail::ordinalWords().value(u"other"_s), 2);
    }

    // ==== format.h ======================================================================
    void formatHumanPolish_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("today") << u"2026-10-14"_s << u"dziś"_s;
        QTest::newRow("tomorrow") << u"2026-10-15"_s << u"jutro"_s;
        QTest::newRow("yesterday") << u"2026-10-13"_s << u"wczoraj"_s;
        QTest::newRow("today time") << u"2026-10-14 09:00"_s << u"dziś 09:00"_s;
        QTest::newRow("tomorrow time") << u"2026-10-15 18:30"_s << u"jutro 18:30"_s;
        QTest::newRow("yesterday time") << u"2026-10-13 23:59"_s << u"wczoraj 23:59"_s;
        QTest::newRow("fri") << u"2026-10-16"_s << u"pt 16 paź"_s;
        QTest::newRow("sat") << u"2026-10-17"_s << u"sob 17 paź"_s;
        QTest::newRow("sun") << u"2026-10-18"_s << u"nd 18 paź"_s;
        QTest::newRow("mon") << u"2026-10-19"_s << u"pn 19 paź"_s;
        QTest::newRow("tue") << u"2026-10-20"_s << u"wt 20 paź"_s;
        QTest::newRow("wed") << u"2026-10-21"_s << u"śr 21 paź"_s;
        QTest::newRow("thu") << u"2026-10-22"_s << u"czw 22 paź"_s;
        QTest::newRow("fri time") << u"2026-10-16 09:00"_s << u"pt 16 paź 09:00"_s;
        QTest::newRow("january") << u"2026-01-05"_s << u"pn 5 sty"_s;
        QTest::newRow("may") << u"2026-05-01"_s << u"pt 1 maj"_s;
        QTest::newRow("next year") << u"2027-10-17"_s << u"17 paź 2027"_s;
        QTest::newRow("last year") << u"2025-12-31"_s << u"31 gru 2025"_s;
        QTest::newRow("next year time") << u"2027-01-01 00:30"_s << u"1 sty 2027 00:30"_s;
    }
    void formatHumanPolish()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(formatHuman(spec(input), UiLocale::Polish, Today), expected);
    }
    void formatHumanEnglish_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("today") << u"2026-10-14"_s << u"today"_s;
        QTest::newRow("tomorrow") << u"2026-10-15"_s << u"tomorrow"_s;
        QTest::newRow("yesterday") << u"2026-10-13"_s << u"yesterday"_s;
        QTest::newRow("today time") << u"2026-10-14 09:00"_s << u"today 09:00"_s;
        QTest::newRow("fri") << u"2026-10-16"_s << u"Fri, Oct 16"_s;
        QTest::newRow("sat time") << u"2026-10-17 18:30"_s << u"Sat, Oct 17 18:30"_s;
        QTest::newRow("next year") << u"2027-10-17"_s << u"Oct 17, 2027"_s;
        QTest::newRow("september") << u"2026-09-01"_s << u"Tue, Sep 1"_s;
    }
    void formatHumanEnglish()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(formatHuman(spec(input), UiLocale::English, Today), expected);
    }
    void formatHumanEdges()
    {
        QCOMPARE(formatHuman(DateSpec{}, UiLocale::Polish, Today), QString());
        // Relative words across the year boundary.
        const QDate newYearsEve(2026, 12, 31);
        QCOMPARE(formatHuman(spec(u"2027-01-01"), UiLocale::Polish, newYearsEve), u"jutro"_s);
        QCOMPARE(formatHuman(spec(u"2027-01-02"), UiLocale::Polish, newYearsEve), u"2 sty 2027"_s);
        // Without a valid "today": no relative words, the year is always shown.
        QCOMPARE(formatHuman(spec(u"2026-10-14"), UiLocale::Polish, QDate()), u"14 paź 2026"_s);
        QCOMPARE(formatHuman(spec(u"2026-10-16 09:00"), UiLocale::English, QDate()), u"Oct 16, 2026 09:00"_s);
    }
    void formatDateShort()
    {
        QCOMPARE(dates::formatDateShort(d(u"2026-10-14"), UiLocale::Polish, 2026), u"śr 14 paź"_s);
        QCOMPARE(dates::formatDateShort(d(u"2026-10-14"), UiLocale::English), u"Wed, Oct 14"_s);
        QCOMPARE(dates::formatDateShort(d(u"2026-10-14"), UiLocale::English, 2025), u"Oct 14, 2026"_s);
        QCOMPARE(dates::formatDateShort(d(u"2026-10-18"), UiLocale::Polish, 2026), u"nd 18 paź"_s);
        QCOMPARE(dates::formatDateShort(d(u"2026-09-30"), UiLocale::Polish, 2027), u"30 wrz 2026"_s);
        QCOMPARE(dates::formatDateShort(QDate(), UiLocale::Polish, 2026), QString());
    }

    // ==== expr.h ========================================================================
    void resolveDateExpr_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        const auto day = [](const char16_t *iso) { return dayRange(QString::fromUtf16(iso)); };
        QTest::newRow("today") << u"today"_s << day(u"2026-10-14");
        QTest::newRow("tomorrow") << u"tomorrow"_s << day(u"2026-10-15");
        QTest::newRow("yesterday") << u"yesterday"_s << day(u"2026-10-13");
        QTest::newRow("dziś") << u"dziś"_s << day(u"2026-10-14");
        QTest::newRow("dzis") << u"dzis"_s << day(u"2026-10-14");
        QTest::newRow("dzisiaj") << u"dzisiaj"_s << day(u"2026-10-14");
        QTest::newRow("DZIŚ") << u"DZIŚ"_s << day(u"2026-10-14");
        QTest::newRow("jutro") << u"jutro"_s << day(u"2026-10-15");
        QTest::newRow("wczoraj") << u"wczoraj"_s << day(u"2026-10-13");
        QTest::newRow("pojutrze") << u"pojutrze"_s << day(u"2026-10-16");
        QTest::newRow("przedwczoraj") << u"przedwczoraj"_s << day(u"2026-10-12");
        QTest::newRow("iso") << u"2026-10-20"_s << day(u"2026-10-20");
        QTest::newRow("iso T") << u"2026-10-20T09:00"_s << day(u"2026-10-20");
        QTest::newRow("iso space") << u"2026-10-20 09:00"_s << day(u"2026-10-20");
        QTest::newRow("+7d") << u"+7d"_s << day(u"2026-10-21");
        QTest::newRow("-2d") << u"-2d"_s << day(u"2026-10-12");
        QTest::newRow("7d") << u"7d"_s << day(u"2026-10-21");
        QTest::newRow("+7") << u"+7"_s << day(u"2026-10-21");
        QTest::newRow("-2w") << u"-2w"_s << day(u"2026-09-30");
        QTest::newRow("+2t") << u"+2t"_s << day(u"2026-10-28");
        QTest::newRow("+1m") << u"+1m"_s << day(u"2026-11-14");
        QTest::newRow("+1y") << u"+1y"_s << day(u"2027-10-14");
        QTest::newRow("+1r") << u"+1r"_s << day(u"2027-10-14");
        QTest::newRow("+0d") << u"+0d"_s << day(u"2026-10-14");
        QTest::newRow("+3 dni") << u"+3 dni"_s << day(u"2026-10-17");
        QTest::newRow("+2 tygodnie") << u"+2 tygodnie"_s << day(u"2026-10-28");
        QTest::newRow("this-week") << u"this-week"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("next-week") << u"next-week"_s << u"2026-10-19..2026-10-25"_s;
        QTest::newRow("last-week") << u"last-week"_s << u"2026-10-05..2026-10-11"_s;
        QTest::newRow("this-month") << u"this-month"_s << u"2026-10-01..2026-10-31"_s;
        QTest::newRow("next-month") << u"next-month"_s << u"2026-11-01..2026-11-30"_s;
        QTest::newRow("last-month") << u"last-month"_s << u"2026-09-01..2026-09-30"_s;
        QTest::newRow("this-year") << u"this-year"_s << u"2026-01-01..2026-12-31"_s;
        QTest::newRow("next-year") << u"next-year"_s << u"2027-01-01..2027-12-31"_s;
        QTest::newRow("last-year") << u"last-year"_s << u"2025-01-01..2025-12-31"_s;
        QTest::newRow("week") << u"week"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("this week") << u"this week"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("THIS_WEEK") << u"THIS_WEEK"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("ten-tydzień") << u"ten-tydzień"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("ten tydzien") << u"ten tydzien"_s << u"2026-10-12..2026-10-18"_s;
        QTest::newRow("tydzień-następny") << u"tydzień-następny"_s << u"2026-10-19..2026-10-25"_s;
        QTest::newRow("następny-tydzień") << u"następny-tydzień"_s << u"2026-10-19..2026-10-25"_s;
        QTest::newRow("przyszły-tydzień") << u"przyszły-tydzień"_s << u"2026-10-19..2026-10-25"_s;
        QTest::newRow("zeszły-tydzień") << u"zeszły-tydzień"_s << u"2026-10-05..2026-10-11"_s;
        QTest::newRow("ten-miesiąc") << u"ten-miesiąc"_s << u"2026-10-01..2026-10-31"_s;
        QTest::newRow("następny-miesiąc") << u"następny-miesiąc"_s << u"2026-11-01..2026-11-30"_s;
        QTest::newRow("ten-rok") << u"ten-rok"_s << u"2026-01-01..2026-12-31"_s;
        QTest::newRow("today..+7d") << u"today..+7d"_s << u"2026-10-14..2026-10-21"_s;
        QTest::newRow("today .. next-week") << u"today .. next-week"_s << u"2026-10-14..2026-10-25"_s;
        QTest::newRow("+7d..today") << u"+7d..today"_s << u"2026-10-14..2026-10-21"_s;
        QTest::newRow("iso range") << u"2026-10-01..2026-10-31"_s << u"2026-10-01..2026-10-31"_s;
        QTest::newRow("overdue") << u"overdue"_s << u"overdue"_s;
        QTest::newRow("zaległe") << u"zaległe"_s << u"overdue"_s;
        QTest::newRow("zalegle") << u"zalegle"_s << u"overdue"_s;
        QTest::newRow("none") << u"none"_s << u"none"_s;
        QTest::newRow("brak") << u"brak"_s << u"none"_s;
        QTest::newRow("any") << u"any"_s << u"any"_s;
        QTest::newRow("dowolna") << u"dowolna"_s << u"any"_s;
        QTest::newRow("ANY") << u"ANY"_s << u"any"_s;
        for (const QString &bad : {QString(), u"   "_s, u"someday"_s, u"7"_s, u"+7q"_s, u"2026-13-01"_s,
                                   u"2026-02-30"_s, u"this-decade"_s, u"next-next-week"_s, u"constructor"_s,
                                   u"toString"_s, u"a..b..c"_s, u"today.."_s, u"..today"_s, u"overdue..today"_s})
            QTest::newRow(QString(u"null: '"_s + bad + u"'"_s).toUtf8().constData()) << bad << u"null"_s;
    }
    void resolveDateExpr()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(expr(input), expected);
    }
    void resolveDateExprWeekStart()
    {
        QCOMPARE(expr(u"this-week", Today, Qt::Sunday), u"2026-10-11..2026-10-17"_s);
        QCOMPARE(expr(u"next-week", Today, Qt::Sunday), u"2026-10-18..2026-10-24"_s);
        // A malformed week start (e.g. from a synced config) means Monday.
        QCOMPARE(expr(u"this-week", Today, static_cast<Qt::DayOfWeek>(42)), u"2026-10-12..2026-10-18"_s);
        QCOMPARE(expr(u"next-week", Today, static_cast<Qt::DayOfWeek>(-3)), u"2026-10-19..2026-10-25"_s);
    }
    void resolveDateExprBoundaries()
    {
        const QDate jan31(2026, 1, 31);
        QCOMPARE(expr(u"next-month", jan31), u"2026-02-01..2026-02-28"_s);
        QCOMPARE(expr(u"last-month", jan31), u"2025-12-01..2025-12-31"_s);
        QCOMPARE(expr(u"+1m", jan31), dayRange(u"2026-02-28"_s));
        const QDate sunday(2026, 10, 18);
        QCOMPARE(expr(u"this-week", sunday), u"2026-10-12..2026-10-18"_s);
        QCOMPARE(expr(u"this-week", sunday, Qt::Sunday), u"2026-10-18..2026-10-24"_s);
        QCOMPARE(expr(u"today", QDate()), u"null"_s);
        QCOMPARE(expr(u"overdue", QDate()), u"overdue"_s); // keywords need no date
    }
    void resolveDateExprRange()
    {
        QCOMPARE(expr(u"+9999y"), u"null"_s);
        QCOMPARE(expr(u"+99999999999999999999d"), u"null"_s);
        QCOMPARE(expr(u"-3000y"), u"null"_s);
        QCOMPARE(expr(u"today..+9999y"), u"null"_s);
        QCOMPARE(expr(u"+7973y"), dayRange(u"9999-10-14"_s));
        const QDate lastDay(9999, 12, 31);
        QCOMPARE(expr(u"tomorrow", lastDay), u"null"_s);
        QCOMPARE(expr(u"next-week", lastDay), u"null"_s);
        QCOMPARE(expr(u"next-year", lastDay), u"null"_s);
        QCOMPARE(expr(u"today", lastDay), dayRange(u"9999-12-31"_s));
        QCOMPARE(expr(u"this-year", lastDay), u"9999-01-01..9999-12-31"_s);
    }
    void resolveDateExprLinear()
    {
        QElapsedTimer t;
        t.start();
        QCOMPARE(expr(QString(u"a"_s + QString(40'000, u' ') + u"b"_s)), u"null"_s);
        QCOMPARE(expr(QString(u"today"_s + QString(40'000, u' ') + u".."_s + QString(40'000, u' ') + u"+7d"_s)),
                 u"2026-10-14..2026-10-21"_s);
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    }
    void dateRangeContains()
    {
        const DateRange r{d(u"2026-10-12"), d(u"2026-10-18")};
        QVERIFY(r.contains(d(u"2026-10-12")));
        QVERIFY(r.contains(d(u"2026-10-18")));
        QVERIFY(!r.contains(d(u"2026-10-19")));
    }
};

QTEST_GUILESS_MAIN(TestDates)
#include "tst_dates.moc"
