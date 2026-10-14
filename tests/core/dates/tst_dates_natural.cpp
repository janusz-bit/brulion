// Natural-language dates, stripping and protected spans. Test cases ported from archive/web-v1
// (natural.test.ts, protect.test.ts). "now" is always injected: Wednesday 14 Oct 2026, 10:00.
#include "dates/natural.h"
#include "dates/protect_p.h"
#include "text/text.h"

#include <QElapsedTimer>
#include <QTest>
#include <QTimeZone>

using namespace brulion;
using namespace brulion::dates;
using namespace Qt::StringLiterals;

namespace {

/// Wednesday, 14 October 2026, 10:00 (local time).
const QDateTime Now(QDate(2026, 10, 14), QTime(10, 0));

/// "yyyy-MM-dd[ HH:mm]" of the first phrase, or "null".
QString when(QStringView input, const QDateTime &now = Now, Qt::DayOfWeek weekStart = Qt::Monday,
             UiLocale locale = UiLocale::Polish)
{
    const auto r = parseNaturalDate(input, now, weekStart, locale);
    return r ? r->spec.toString() : u"null"_s;
}

/// The matched substring of the first phrase, or "null".
QString matched(QStringView input, const QDateTime &now = Now)
{
    const auto r = parseNaturalDate(input, now);
    return r ? input.sliced(r->start, r->length).toString() : u"null"_s;
}

/// Strip the first phrase (fails the test when there is none).
QString strip(QStringView input)
{
    const auto r = parseNaturalDate(input, Now);
    if (!r)
        return u"<no date>"_s;
    return stripNaturalDate(input, *r);
}

/// The protected substrings of `input` (folded first, like the parser does).
QStringList protectedSpans(QStringView input)
{
    const QString folded = text::foldKey(input);
    const QBitArray mask = detail::protectedMask(folded);
    QStringList out;
    qsizetype start = -1;
    for (qsizetype i = 0; i <= folded.size(); ++i) {
        const bool on = i < folded.size() && mask.testBit(i);
        if (on && start < 0)
            start = i;
        if (!on && start >= 0) {
            out.append(folded.sliced(start, i - start));
            start = -1;
        }
    }
    return out;
}

void addRows(std::initializer_list<std::pair<QString, QString>> rows)
{
    for (const auto &[input, expected] : rows)
        QTest::newRow(input.toUtf8().constData()) << input << expected;
}

void addRows(std::initializer_list<std::tuple<QString, QString, QString>> rows)
{
    for (const auto &[input, expected, match] : rows)
        QTest::newRow(input.toUtf8().constData()) << input << expected << match;
}

void inputExpectedColumns()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
}

void inputExpectedMatchColumns()
{
    inputExpectedColumns();
    QTest::addColumn<QString>("match");
}

} // namespace

class TestDatesNatural : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void documentedExample()
    {
        const auto r = parseNaturalDate(u"Zadzwoń do mamy jutro o 18", Now);
        QVERIFY(r);
        QCOMPARE(r->spec, (DateSpec{QDate(2026, 10, 15), QTime(18, 0)}));
        QCOMPARE(r->start, 16);
        QCOMPARE(r->length, 10);
        QCOMPARE(r->end(), 26);
    }

    // ---- relative day words --------------------------------------------------------------
    void dayWords_data()
    {
        inputExpectedColumns();
        addRows({
            {u"dziś"_s, u"2026-10-14"_s},
            {u"dzisiaj"_s, u"2026-10-14"_s},
            {u"dzis"_s, u"2026-10-14"_s},
            {u"DZIŚ"_s, u"2026-10-14"_s},
            {u"jutro"_s, u"2026-10-15"_s},
            {u"Jutro"_s, u"2026-10-15"_s},
            {u"pojutrze"_s, u"2026-10-16"_s},
            {u"wczoraj"_s, u"2026-10-13"_s},
            {u"przedwczoraj"_s, u"2026-10-12"_s},
            {u"today"_s, u"2026-10-14"_s},
            {u"tomorrow"_s, u"2026-10-15"_s},
            {u"yesterday"_s, u"2026-10-13"_s},
            {u"the day after tomorrow"_s, u"2026-10-16"_s},
            {u"day before yesterday"_s, u"2026-10-12"_s},
            {u"do jutra"_s, u"2026-10-15"_s},
            {u"od jutra"_s, u"2026-10-15"_s},
            {u"na dziś"_s, u"2026-10-14"_s},
            {u"jutra nie będzie"_s, u"null"_s}, // the genitive needs a preposition
            {u"jutrzejszy"_s, u"null"_s},
            {u"todays"_s, u"null"_s},
            {u"dzisiejszy"_s, u"null"_s},
        });
    }
    void dayWords()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void prepositionIncluded()
    {
        const auto r = parseNaturalDate(u"Kupić mleko na jutro", Now);
        QVERIFY(r);
        QCOMPARE(r->start, 12);
        QCOMPARE(matched(u"Kupić mleko na jutro"), u"na jutro"_s);
    }

    // ---- weekdays ------------------------------------------------------------------------
    void weekdays_data()
    {
        inputExpectedColumns();
        addRows({
            {u"poniedziałek"_s, u"2026-10-19"_s},
            {u"w poniedziałek"_s, u"2026-10-19"_s},
            {u"wtorek"_s, u"2026-10-20"_s},
            {u"we wtorek"_s, u"2026-10-20"_s},
            {u"środa"_s, u"2026-10-21"_s},
            {u"w środę"_s, u"2026-10-21"_s},
            {u"w srode"_s, u"2026-10-21"_s},
            {u"czwartek"_s, u"2026-10-15"_s},
            {u"w czwartek"_s, u"2026-10-15"_s},
            {u"piątek"_s, u"2026-10-16"_s},
            {u"w piątek"_s, u"2026-10-16"_s},
            {u"W PIĄTEK"_s, u"2026-10-16"_s},
            {u"w piatek"_s, u"2026-10-16"_s},
            {u"sobota"_s, u"2026-10-17"_s},
            {u"w sobotę"_s, u"2026-10-17"_s},
            {u"niedziela"_s, u"2026-10-18"_s},
            {u"w niedzielę"_s, u"2026-10-18"_s},
            {u"do piątku"_s, u"2026-10-16"_s},
            {u"monday"_s, u"2026-10-19"_s},
            {u"on Friday"_s, u"2026-10-16"_s},
            {u"wednesday"_s, u"2026-10-21"_s},
            // Abbreviations alone.
            {u"pn"_s, u"2026-10-19"_s},
            {u"pon"_s, u"2026-10-19"_s},
            {u"wt"_s, u"2026-10-20"_s},
            {u"śr"_s, u"2026-10-21"_s},
            {u"czw"_s, u"2026-10-15"_s},
            {u"pt"_s, u"2026-10-16"_s},
            {u"pt."_s, u"2026-10-16"_s},
            {u"sob"_s, u"2026-10-17"_s},
            {u"nd"_s, u"2026-10-18"_s},
            {u"ndz"_s, u"2026-10-18"_s},
            {u"fri"_s, u"2026-10-16"_s},
            {u"w pt"_s, u"2026-10-16"_s},
            // Abbreviations in prose only next to a time.
            {u"Pt. Pan Tadeusz"_s, u"null"_s},
            {u"I sat down"_s, u"null"_s},
            {u"Wed nothing"_s, u"null"_s},
            {u"pt 18:00"_s, u"2026-10-16 18:00"_s},
            {u"sat 10am"_s, u"2026-10-17 10:00"_s},
            // "przyszły/następny/next" = that day in next week.
            {u"w przyszły poniedziałek"_s, u"2026-10-19"_s},
            {u"w przyszły piątek"_s, u"2026-10-23"_s},
            {u"w przyszłą środę"_s, u"2026-10-21"_s},
            {u"w następną sobotę"_s, u"2026-10-24"_s},
            {u"w przyszłą niedzielę"_s, u"2026-10-25"_s},
            {u"przyszły czwartek"_s, u"2026-10-22"_s},
            {u"następny piątek"_s, u"2026-10-23"_s},
            {u"do przyszłego piątku"_s, u"2026-10-23"_s},
            {u"next friday"_s, u"2026-10-23"_s},
            {u"next wednesday"_s, u"2026-10-21"_s},
            {u"next sunday"_s, u"2026-10-25"_s},
            // "ten/this" includes today, "zeszły/last" looks back.
            {u"ten piątek"_s, u"2026-10-16"_s},
            {u"w tę środę"_s, u"2026-10-14"_s},
            {u"this wednesday"_s, u"2026-10-14"_s},
            {u"w najbliższy wtorek"_s, u"2026-10-20"_s},
            {u"w zeszły piątek"_s, u"2026-10-09"_s},
            {u"w ubiegłą środę"_s, u"2026-10-07"_s},
            {u"last tuesday"_s, u"2026-10-13"_s},
        });
    }
    void weekdays()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void weekStart()
    {
        QCOMPARE(when(u"next sunday", Now, Qt::Sunday), u"2026-10-18"_s);
        QCOMPARE(when(u"next saturday", Now, Qt::Sunday), u"2026-10-24"_s);
        QCOMPARE(when(u"w przyszły poniedziałek", Now, Qt::Sunday), u"2026-10-19"_s);
        QCOMPARE(when(u"w przyszłym tygodniu", Now, Qt::Sunday), u"2026-10-18"_s);
        QCOMPARE(when(u"koniec tygodnia", Now, Qt::Sunday), u"2026-10-17"_s);
        // A malformed week start means Monday.
        const auto bad = static_cast<Qt::DayOfWeek>(42);
        QCOMPARE(when(u"w przyszły poniedziałek", Now, bad), u"2026-10-19"_s);
        QCOMPARE(when(u"next week", Now, bad), u"2026-10-19"_s);
        QCOMPARE(when(u"koniec tygodnia", Now, bad), u"2026-10-18"_s);
    }

    // ---- relative offsets ----------------------------------------------------------------
    void offsets_data()
    {
        inputExpectedColumns();
        addRows({
            {u"za 1 dzień"_s, u"2026-10-15"_s},
            {u"za dzień"_s, u"2026-10-15"_s},
            {u"za 5 dni"_s, u"2026-10-19"_s},
            {u"za dwa dni"_s, u"2026-10-16"_s},
            {u"za tydzień"_s, u"2026-10-21"_s},
            {u"za tydzien"_s, u"2026-10-21"_s},
            {u"za 2 tygodnie"_s, u"2026-10-28"_s},
            {u"za trzy tygodnie"_s, u"2026-11-04"_s},
            {u"za 5 tygodni"_s, u"2026-11-18"_s},
            {u"za miesiąc"_s, u"2026-11-14"_s},
            {u"za 2 miesiące"_s, u"2026-12-14"_s},
            {u"za 5 miesięcy"_s, u"2027-03-14"_s},
            {u"za rok"_s, u"2027-10-14"_s},
            {u"za 2 lata"_s, u"2028-10-14"_s},
            {u"za 5 lat"_s, u"2031-10-14"_s},
            {u"za pół roku"_s, u"2027-04-14"_s},
            {u"za półtora roku"_s, u"2028-04-14"_s},
            {u"w ciągu 3 dni"_s, u"2026-10-17"_s},
            {u"in 2 days"_s, u"2026-10-16"_s},
            {u"in two days"_s, u"2026-10-16"_s},
            {u"in a week"_s, u"2026-10-21"_s},
            {u"in 2 weeks"_s, u"2026-10-28"_s},
            {u"in 3 months"_s, u"2027-01-14"_s},
            {u"in 1 year"_s, u"2027-10-14"_s},
            {u"2 dni temu"_s, u"2026-10-12"_s},
            {u"tydzień temu"_s, u"2026-10-07"_s},
            {u"3 weeks ago"_s, u"2026-09-23"_s},
            // Exact date-times.
            {u"za godzinę"_s, u"2026-10-14 11:00"_s},
            {u"za 3 godziny"_s, u"2026-10-14 13:00"_s},
            {u"za 5 godzin"_s, u"2026-10-14 15:00"_s},
            {u"za 15 godzin"_s, u"2026-10-15 01:00"_s},
            {u"za 2h"_s, u"2026-10-14 12:00"_s},
            {u"za minutę"_s, u"2026-10-14 10:01"_s},
            {u"za 2 minuty"_s, u"2026-10-14 10:02"_s},
            {u"za 30 minut"_s, u"2026-10-14 10:30"_s},
            {u"za 90 min"_s, u"2026-10-14 11:30"_s},
            {u"za pół godziny"_s, u"2026-10-14 10:30"_s},
            {u"za półtorej godziny"_s, u"2026-10-14 11:30"_s},
            {u"za pół dnia"_s, u"2026-10-14 22:00"_s},
            {u"za kwadrans"_s, u"2026-10-14 10:15"_s},
            {u"za trzy kwadranse"_s, u"2026-10-14 10:45"_s},
            {u"in 3 hours"_s, u"2026-10-14 13:00"_s},
            {u"in an hour"_s, u"2026-10-14 11:00"_s},
            {u"in 45 minutes"_s, u"2026-10-14 10:45"_s},
            {u"in half an hour"_s, u"2026-10-14 10:30"_s},
            {u"2 hours ago"_s, u"2026-10-14 08:00"_s},
            // Range and incomplete phrases.
            {u"za 9999 lat"_s, u"null"_s},
            {u"za 100 lat"_s, u"2126-10-14"_s},
            {u"za dni"_s, u"null"_s},
            {u"in week"_s, u"null"_s},
            {u"dni temu"_s, u"null"_s},
        });
    }
    void offsets()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void offsetCrossesMidnightAndDropsSeconds()
    {
        const QDateTime late(QDate(2026, 12, 31), QTime(22, 30, 45));
        QCOMPARE(when(u"za 3 godziny", late), u"2027-01-01 01:30"_s);
    }
    void offsetUsesElapsedTimeAcrossDst()
    {
        // 25 Oct 2026, 01:30 in Warsaw: clocks go back at 03:00 CEST -> 02:00 CET, so three
        // elapsed hours later the wall clock shows 03:30 (not 04:30).
        const QTimeZone warsaw("Europe/Warsaw");
        if (!warsaw.isValid())
            QSKIP("Time zone data for Europe/Warsaw is not available");
        const QDateTime beforeChange(QDate(2026, 10, 25), QTime(1, 30), warsaw);
        QCOMPARE(when(u"za 3 godziny", beforeChange), u"2026-10-25 03:30"_s);
        // Calendar offsets ignore DST: tomorrow is the next calendar day.
        QCOMPARE(when(u"jutro o 9", beforeChange), u"2026-10-26 09:00"_s);
    }
    void exactNotMerged()
    {
        const auto r = parseNaturalDate(u"za 3 godziny jutro", Now);
        QVERIFY(r);
        QCOMPARE(matched(u"za 3 godziny jutro"), u"za 3 godziny"_s);
        QCOMPARE(r->spec.time, std::optional<QTime>(QTime(13, 0)));
    }

    // ---- explicit dates ------------------------------------------------------------------
    void explicitDates_data()
    {
        inputExpectedColumns();
        addRows({
            {u"15 października"_s, u"2026-10-15"_s},
            {u"15 października 2027"_s, u"2027-10-15"_s},
            {u"15 października 2027 r."_s, u"2027-10-15"_s},
            {u"15 paź"_s, u"2026-10-15"_s},
            {u"15 paz"_s, u"2026-10-15"_s},
            {u"15 październik"_s, u"2026-10-15"_s},
            {u"14 października"_s, u"2026-10-14"_s},
            {u"13 października"_s, u"2027-10-13"_s},
            {u"31 grudnia"_s, u"2026-12-31"_s},
            {u"3 maja"_s, u"2027-05-03"_s},
            {u"1 stycznia"_s, u"2027-01-01"_s},
            {u"29 lutego"_s, u"2028-02-29"_s},
            {u"15-go października"_s, u"2026-10-15"_s},
            {u"1 sty"_s, u"2027-01-01"_s},
            {u"1 lut"_s, u"2027-02-01"_s},
            {u"1 mar"_s, u"2027-03-01"_s},
            {u"1 kwi"_s, u"2027-04-01"_s},
            {u"1 maj"_s, u"2027-05-01"_s},
            {u"1 cze"_s, u"2027-06-01"_s},
            {u"1 lip"_s, u"2027-07-01"_s},
            {u"1 sie"_s, u"2027-08-01"_s},
            {u"1 wrz"_s, u"2027-09-01"_s},
            {u"1 paź"_s, u"2027-10-01"_s},
            {u"1 lis"_s, u"2026-11-01"_s},
            {u"1 gru"_s, u"2026-12-01"_s},
            {u"15.10"_s, u"2026-10-15"_s},
            {u"15.10.2026"_s, u"2026-10-15"_s},
            {u"15.10.2026r."_s, u"2026-10-15"_s},
            {u"15.10.27"_s, u"2027-10-15"_s},
            {u"1.10"_s, u"2027-10-01"_s},
            {u"01.11"_s, u"2026-11-01"_s},
            {u"15/10"_s, u"2026-10-15"_s},
            {u"15/10/2026"_s, u"2026-10-15"_s},
            {u"15-10-2026"_s, u"2026-10-15"_s},
            {u"2026-10-15"_s, u"2026-10-15"_s},
            {u"2026/10/15"_s, u"2026-10-15"_s},
            {u"do 20.10"_s, u"2026-10-20"_s},
            {u"Oct 15"_s, u"2026-10-15"_s},
            {u"15 Oct"_s, u"2026-10-15"_s},
            {u"October 15th"_s, u"2026-10-15"_s},
            {u"October 15, 2027"_s, u"2027-10-15"_s},
            {u"1st of November"_s, u"2026-11-01"_s},
            {u"May 3"_s, u"2027-05-03"_s},
            {u"sept 1"_s, u"2027-09-01"_s},
            {u"the 20th"_s, u"2026-10-20"_s},
            {u"on the 1st"_s, u"2026-11-01"_s},
            {u"20-go"_s, u"2026-10-20"_s},
            // Impossible dates.
            {u"31 lutego"_s, u"null"_s},
            {u"31.04"_s, u"null"_s},
            {u"32.10"_s, u"null"_s},
            {u"15.13.2026"_s, u"null"_s},
            {u"2026-02-30"_s, u"null"_s},
            {u"29.02.2027"_s, u"null"_s},
        });
    }
    void explicitDates()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void slashDatesByLocale()
    {
        QCOMPARE(when(u"10/11"), u"2026-11-10"_s);
        QCOMPARE(when(u"10/11", Now, Qt::Monday, UiLocale::English), u"2027-10-11"_s); // Oct 11 already passed
        QCOMPARE(when(u"11/10", Now, Qt::Monday, UiLocale::English), u"2026-11-10"_s);
        QCOMPARE(when(u"10/15"), u"2026-10-15"_s);
        QCOMPARE(when(u"15/10", Now, Qt::Monday, UiLocale::English), u"2026-10-15"_s);
    }
    void datesInsideSentences()
    {
        const auto r = parseNaturalDate(u"Spotkanie 15.10 w biurze", Now);
        QVERIFY(r);
        QCOMPARE(r->start, 10);
        QCOMPARE(matched(u"Spotkanie 15.10 w biurze"), u"15.10"_s);
        QCOMPARE(when(u"Urodziny Ani 3 maja!"), u"2027-05-03"_s);
        QCOMPARE(matched(u"Urodziny Ani 3 maja!"), u"3 maja"_s);
    }

    // ---- false positives -----------------------------------------------------------------
    void falsePositives_data()
    {
        QTest::addColumn<QString>("input");
        for (const QString &s : {u"majonez"_s, u"Maj był ciepły"_s, u"wersja 1.2.3"_s, u"IP 192.168.1.10"_s,
                                 u"pi to 3.14"_s, u"kosztuje 12.5 zł"_s, u"cena 12.05 zł"_s, u"rabat 10.10 %"_s,
                                 u"koszt $15.10"_s, u"1/2 szklanki"_s, u"ocena 8/10"_s, u"podwyżka o 5 zł"_s,
                                 u"zwiększyć o 10%"_s, u"przesunąć o 2 dni"_s, u"rozmowa o 5 rzeczach"_s,
                                 u"zapisało się 15 osób"_s, u"Dobry wieczór"_s, u"Good morning"_s, u"pokój 12"_s,
                                 u"o 25"_s, u"13pm"_s, u"waga 15.10 kg"_s, u"€ 12.10"_s})
            QTest::newRow(s.toUtf8().constData()) << s;
        QTest::newRow("empty") << QString();
    }
    void falsePositives()
    {
        QFETCH(QString, input);
        QCOMPARE(when(input), u"null"_s);
    }

    // ---- times ---------------------------------------------------------------------------
    void times_data()
    {
        inputExpectedColumns();
        addRows({
            {u"o 9"_s, u"2026-10-15 09:00"_s},
            {u"o 11"_s, u"2026-10-14 11:00"_s},
            {u"o 9:30"_s, u"2026-10-15 09:30"_s},
            {u"o 18.30"_s, u"2026-10-14 18:30"_s},
            {u"o godz. 18"_s, u"2026-10-14 18:00"_s},
            {u"o godzinie 7"_s, u"2026-10-15 07:00"_s},
            {u"godz. 18"_s, u"2026-10-14 18:00"_s},
            {u"o 18h"_s, u"2026-10-14 18:00"_s},
            {u"9:30"_s, u"2026-10-15 09:30"_s},
            {u"18:30"_s, u"2026-10-14 18:30"_s},
            {u"10:00"_s, u"2026-10-15 10:00"_s},
            {u"10:01"_s, u"2026-10-14 10:01"_s},
            {u"6pm"_s, u"2026-10-14 18:00"_s},
            {u"6 pm"_s, u"2026-10-14 18:00"_s},
            {u"6:30pm"_s, u"2026-10-14 18:30"_s},
            {u"6:30 p.m."_s, u"2026-10-14 18:30"_s},
            {u"11am"_s, u"2026-10-14 11:00"_s},
            {u"12am"_s, u"2026-10-15 00:00"_s},
            {u"12pm"_s, u"2026-10-14 12:00"_s},
            {u"at 9"_s, u"2026-10-15 09:00"_s},
            {u"at 9:15"_s, u"2026-10-15 09:15"_s},
            {u"5 o’clock"_s, u"2026-10-15 05:00"_s},
            {u"5 o'clock"_s, u"2026-10-15 05:00"_s},
            {u"noon"_s, u"2026-10-14 12:00"_s},
            {u"at noon"_s, u"2026-10-14 12:00"_s},
            {u"midnight"_s, u"2026-10-15 00:00"_s},
            {u"rano"_s, u"2026-10-15 08:00"_s},
            {u"przed południem"_s, u"2026-10-15 10:00"_s},
            {u"w południe"_s, u"2026-10-14 12:00"_s},
            {u"po południu"_s, u"2026-10-14 15:00"_s},
            {u"po poludniu"_s, u"2026-10-14 15:00"_s},
            {u"wieczorem"_s, u"2026-10-14 19:00"_s},
            {u"w nocy"_s, u"2026-10-14 22:00"_s},
            {u"o północy"_s, u"2026-10-15 00:00"_s},
            {u"in the evening"_s, u"2026-10-14 19:00"_s},
            {u"tonight"_s, u"2026-10-14 19:00"_s},
            {u"this morning"_s, u"2026-10-14 08:00"_s},
            {u"o 7 rano"_s, u"2026-10-15 07:00"_s},
            {u"o 8 wieczorem"_s, u"2026-10-14 20:00"_s},
            {u"o 3 po południu"_s, u"2026-10-14 15:00"_s},
            {u"o 10 w nocy"_s, u"2026-10-14 22:00"_s},
            {u"o 2 w nocy"_s, u"2026-10-15 02:00"_s},
            {u"o 12 w nocy"_s, u"2026-10-15 00:00"_s},
            {u"rano o 7"_s, u"2026-10-15 07:00"_s},
        });
        QTest::newRow("o 9 with a no-break space") << u"o\u00A09"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("NBSP sentence") << u"Spotkanie o\u00A09 w\u00A0biurze"_s << u"2026-10-15 09:00"_s;
        QTest::newRow("NBSP weekday") << u"w\u00A0piątek o\u00A018"_s << u"2026-10-16 18:00"_s;
    }
    void times()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void bareHourNeedsContext()
    {
        const auto r = parseNaturalDate(u"Spotkanie o 9 w biurze", Now);
        QVERIFY(r);
        QCOMPARE(r->spec.toString(), u"2026-10-15 09:00"_s);
        QCOMPARE(matched(u"Spotkanie o 9 w biurze"), u"o 9"_s);
        QCOMPARE(when(u"Zadzwoń o 18."), u"2026-10-14 18:00"_s);
        QCOMPARE(when(u"o 18, potem kino"), u"2026-10-14 18:00"_s);
        QCOMPARE(when(u"o 18 zadzwonić do Ani"), u"2026-10-14 18:00"_s);
        QCOMPARE(when(u"o 15 się spotkamy"), u"2026-10-14 15:00"_s);
        QCOMPARE(when(u"meet at 7 with Bob"), u"2026-10-15 07:00"_s);
        QCOMPARE(when(u"o 9 kotów"), u"null"_s);
    }

    // ---- date + time combinations --------------------------------------------------------
    void combinations_data()
    {
        inputExpectedColumns();
        addRows({
            {u"jutro o 18"_s, u"2026-10-15 18:00"_s},
            {u"jutro o 7:30"_s, u"2026-10-15 07:30"_s},
            {u"o 18 jutro"_s, u"2026-10-15 18:00"_s},
            {u"dziś o 9"_s, u"2026-10-14 09:00"_s},
            {u"jutro rano"_s, u"2026-10-15 08:00"_s},
            {u"dzisiaj wieczorem"_s, u"2026-10-14 19:00"_s},
            {u"jutro wieczorem o 8"_s, u"2026-10-15 20:00"_s},
            {u"w piątek wieczorem"_s, u"2026-10-16 19:00"_s},
            {u"w piątek o 10"_s, u"2026-10-16 10:00"_s},
            {u"o 10 w piątek"_s, u"2026-10-16 10:00"_s},
            {u"15.10 o 18:30"_s, u"2026-10-15 18:30"_s},
            {u"15.10.2026 18:30"_s, u"2026-10-15 18:30"_s},
            {u"15 października o 9"_s, u"2026-10-15 09:00"_s},
            {u"2026-10-15 18:30"_s, u"2026-10-15 18:30"_s},
            {u"2026-10-15T18:30"_s, u"2026-10-15 18:30"_s},
            {u"jutro, o 18"_s, u"2026-10-15 18:00"_s},
            {u"tomorrow at 6pm"_s, u"2026-10-15 18:00"_s},
            {u"tomorrow morning"_s, u"2026-10-15 08:00"_s},
            {u"tonight at 9"_s, u"2026-10-14 21:00"_s},
            {u"Oct 15 6pm"_s, u"2026-10-15 18:00"_s},
            {u"next friday at 5pm"_s, u"2026-10-23 17:00"_s},
            {u"za 2 dni o 9"_s, u"2026-10-16 09:00"_s},
            {u"w piątek 16.10 o 18"_s, u"2026-10-16 18:00"_s},
            {u"w przyszły piątek o 17:45"_s, u"2026-10-23 17:45"_s},
        });
    }
    void combinations()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
        QCOMPARE(matched(input), input);
    }
    void combinationEdges()
    {
        QCOMPARE(when(u"dziś o 8"), u"2026-10-14 08:00"_s); // explicit date kept although past
        const auto r = parseNaturalDate(u"jutro 20.10", Now);
        QVERIFY(r);
        QCOMPARE(r->spec.toString(), u"2026-10-15"_s);
        QCOMPARE(matched(u"jutro 20.10"), u"jutro"_s); // two different dates are not merged
    }

    // ---- periods -------------------------------------------------------------------------
    void periods_data()
    {
        inputExpectedColumns();
        addRows({
            {u"koniec miesiąca"_s, u"2026-10-31"_s},
            {u"na koniec miesiąca"_s, u"2026-10-31"_s},
            {u"do końca miesiąca"_s, u"2026-10-31"_s},
            {u"koniec tygodnia"_s, u"2026-10-18"_s},
            {u"do końca przyszłego tygodnia"_s, u"2026-10-25"_s},
            {u"koniec roku"_s, u"2026-12-31"_s},
            {u"do końca przyszłego roku"_s, u"2027-12-31"_s},
            {u"end of month"_s, u"2026-10-31"_s},
            {u"by the end of the month"_s, u"2026-10-31"_s},
            {u"end of next month"_s, u"2026-11-30"_s},
            {u"w przyszłym tygodniu"_s, u"2026-10-19"_s},
            {u"w przyszłym miesiącu"_s, u"2026-11-01"_s},
            {u"w przyszłym roku"_s, u"2027-01-01"_s},
            {u"next week"_s, u"2026-10-19"_s},
            {u"next month"_s, u"2026-11-01"_s},
            {u"next year"_s, u"2027-01-01"_s},
            {u"przyszły tydzień"_s, u"2026-10-19"_s},
            {u"w weekend"_s, u"2026-10-17"_s},
            {u"na weekend"_s, u"2026-10-17"_s},
            {u"this weekend"_s, u"2026-10-17"_s},
            {u"w przyszły weekend"_s, u"2026-10-24"_s},
            {u"weekend"_s, u"2026-10-17"_s},
            {u"Udany weekend!"_s, u"null"_s},
        });
    }
    void periods()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void periodEdges()
    {
        QCOMPARE(when(u"w weekend", QDateTime(QDate(2026, 10, 18), QTime(9, 0))), u"2026-10-18"_s);
        QCOMPARE(when(u"koniec miesiąca", QDateTime(QDate(2028, 2, 10), QTime(0, 0))), u"2028-02-29"_s);
    }

    // ---- offsets and Unicode -------------------------------------------------------------
    void unicodeOffsets()
    {
        const QString input = u"Zażółć gęślą jaźń w środę o 9:15"_s;
        const auto r = parseNaturalDate(input, Now);
        QVERIFY(r);
        QCOMPARE(r->spec.toString(), u"2026-10-21 09:15"_s);
        QCOMPARE(input.sliced(r->start, r->length), u"w środę o 9:15"_s);
    }
    void decomposedInput()
    {
        const QString phrase = u"w s\u0301rode\u0328"_s; // "w środę" with combining marks (NFD)
        const QString input = u"Termin: "_s + phrase;
        const auto r = parseNaturalDate(input, Now);
        QVERIFY(r);
        QCOMPARE(r->spec.date, QDate(2026, 10, 21));
        QCOMPARE(input.sliced(r->start, r->length), phrase);
        QCOMPARE(r->start, 8);
    }
    void emojiBeforePhrase()
    {
        const QString input = u"🎂 urodziny 3 maja"_s;
        const auto r = parseNaturalDate(input, Now);
        QVERIFY(r);
        QCOMPARE(r->start, 12);
        QCOMPARE(input.sliced(r->start, r->length), u"3 maja"_s);
    }
    void loneSurrogate()
    {
        const QString input = QString(QChar(0xD800)) + u" jutro"_s;
        QCOMPARE(when(input), u"2026-10-15"_s);
        QCOMPARE(matched(input), u"jutro"_s);
    }
    void invalidNow()
    {
        QCOMPARE(when(u"jutro", QDateTime()), u"null"_s);
        QVERIFY(findNaturalDates(u"jutro", QDateTime()).isEmpty());
    }

    // ---- findNaturalDates ----------------------------------------------------------------
    void findAll()
    {
        const QString input = u"jutro o 9 i 20.10 wieczorem"_s;
        const auto all = findNaturalDates(input, Now);
        QCOMPARE(all.size(), 2);
        QCOMPARE(input.sliced(all.at(0).start, all.at(0).length), u"jutro o 9"_s);
        QCOMPARE(all.at(0).spec.toString(), u"2026-10-15 09:00"_s);
        QCOMPARE(input.sliced(all.at(1).start, all.at(1).length), u"20.10 wieczorem"_s);
        QCOMPARE(all.at(1).spec.toString(), u"2026-10-20 19:00"_s);
        QVERIFY(findNaturalDates(QString(), Now).isEmpty());
    }
    void rangesStayApart()
    {
        const auto times = findNaturalDates(u"12:30 - 14:00", Now);
        QCOMPARE(times.size(), 2);
        QCOMPARE(times.at(0).spec.time, std::optional<QTime>(QTime(12, 30)));
        QCOMPARE(times.at(1).spec.time, std::optional<QTime>(QTime(14, 0)));
        const auto dates = findNaturalDates(u"15.10 - 20.10", Now);
        QCOMPARE(dates.size(), 2);
        QCOMPARE(dates.at(0).spec.date, QDate(2026, 10, 15));
        QCOMPARE(dates.at(1).spec.date, QDate(2026, 10, 20));
    }

    // ---- stripNaturalDate ----------------------------------------------------------------
    void strip_data()
    {
        inputExpectedColumns();
        addRows({
            {u"Zadzwoń do mamy jutro o 18"_s, u"Zadzwoń do mamy"_s},
            {u"jutro o 18 zadzwonić do mamy"_s, u"zadzwonić do mamy"_s},
            {u"Kupić mleko na jutro"_s, u"Kupić mleko"_s},
            {u"Spotkanie 15.10 w biurze"_s, u"Spotkanie w biurze"_s},
            {u"Zadzwoń, jutro o 18, do mamy"_s, u"Zadzwoń do mamy"_s},
            {u"Zadzwoń jutro."_s, u"Zadzwoń."_s},
            {u"jutro"_s, QString()},
            {u"Finish the report by the 15th"_s, u"Finish the report"_s},
            // Brackets/quotes that only wrapped the phrase.
            {u"Kupić mleko (jutro)"_s, u"Kupić mleko"_s},
            {u"Kupić (jutro) mleko"_s, u"Kupić mleko"_s},
            {u"Zadzwoń (w piątek o 18) do mamy"_s, u"Zadzwoń do mamy"_s},
            {u"(jutro) Kupić mleko"_s, u"Kupić mleko"_s},
            {u"Kupić mleko [jutro]"_s, u"Kupić mleko"_s},
            {u"Kupić mleko „jutro”"_s, u"Kupić mleko"_s},
            {u"Termin \"jutro\" mija"_s, u"Termin mija"_s},
            {u"(jutro)"_s, QString()},
            {u"Kupić mleko (jutro, 2 l)"_s, u"Kupić mleko (2 l)"_s},
            {u"Zakupy (mleko) jutro"_s, u"Zakupy (mleko)"_s},
            {u"jutro. Kupić mleko"_s, u"Kupić mleko"_s},
        });
    }
    void strip()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(::strip(input), expected);
    }
    void stripLinear()
    {
        QElapsedTimer t;
        t.start();
        const QString input = QString(50'000, u' ') + u"x jutro"_s;
        QCOMPARE(stripNaturalDate(input, NaturalDate{{}, input.size() - 5, 5}), u"x"_s);
        const QString commas = u"a jutro"_s + u" ,"_s.repeated(25'000) + u"b"_s;
        QCOMPARE(stripNaturalDate(commas, NaturalDate{{}, 2, 5}), u"a b"_s);
        QCOMPARE(stripNaturalDate(u"abc", NaturalDate{{}, 10, 5}), u"abc"_s); // out of range is harmless
        QVERIFY2(t.elapsed() < 5000, qPrintable(QString::number(t.elapsed())));
    }

    // ---- review regressions --------------------------------------------------------------
    void sieAndMaja()
    {
        QCOMPARE(when(u"o 15 sie spotkamy"), u"2026-10-14 15:00"_s);
        QCOMPARE(when(u"o 18 sie zobaczymy"), u"2026-10-14 18:00"_s);
        QCOMPARE(when(u"jutro o 15 sie spotkamy"), u"2026-10-15 15:00"_s);
        QCOMPARE(matched(u"jutro o 15 sie spotkamy"), u"jutro o 15"_s);
        QCOMPARE(when(u"urodziny 15 sie"), u"2027-08-15"_s);
        QCOMPARE(when(u"15 sie 2027"), u"2027-08-15"_s);
        QCOMPARE(when(u"Pamiętaj o 15 sierpnia"), u"2027-08-15"_s);
        QCOMPARE(when(u"Oni 5 mają wolne"), u"null"_s);
        QCOMPARE(when(u"Jutro o 5 mają spotkanie"), u"2026-10-15 05:00"_s);
        QCOMPARE(when(u"Pamiętaj o 5 maja"), u"2027-05-05"_s);
        QCOMPARE(when(u"urodziny 3 maja"), u"2027-05-03"_s);
    }
    void hourWithDaypart_data()
    {
        inputExpectedMatchColumns();
        addRows({
            {u"7 rano"_s, u"2026-10-15 07:00"_s, u"7 rano"_s},
            {u"jutro 7 rano"_s, u"2026-10-15 07:00"_s, u"jutro 7 rano"_s},
            {u"Pobudka jutro 6:30 rano"_s, u"2026-10-15 06:30"_s, u"jutro 6:30 rano"_s},
            {u"8 wieczorem"_s, u"2026-10-14 20:00"_s, u"8 wieczorem"_s},
            {u"w piątek 9 wieczorem"_s, u"2026-10-16 21:00"_s, u"w piątek 9 wieczorem"_s},
            {u"3 po południu"_s, u"2026-10-14 15:00"_s, u"3 po południu"_s},
            {u"11 w nocy"_s, u"2026-10-14 23:00"_s, u"11 w nocy"_s},
            {u"tomorrow 7 in the morning"_s, u"2026-10-15 07:00"_s, u"tomorrow 7 in the morning"_s},
            {u"2 kg rano"_s, u"2026-10-15 08:00"_s, u"rano"_s},
            {u"Jutro: 18:00"_s, u"2026-10-15 18:00"_s, u"Jutro: 18:00"_s},
            {u"jutro - 18:00"_s, u"2026-10-15 18:00"_s, u"jutro - 18:00"_s},
            {u"15.10 – 18:30"_s, u"2026-10-15 18:30"_s, u"15.10 – 18:30"_s},
            {u"Fri - 6pm"_s, u"2026-10-16 18:00"_s, u"Fri - 6pm"_s},
        });
    }
    void hourWithDaypart()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QFETCH(QString, match);
        QCOMPARE(when(input), expected);
        QCOMPARE(matched(input), match);
    }
    void followingNight_data()
    {
        inputExpectedColumns();
        addRows({
            {u"dziś o północy"_s, u"2026-10-15 00:00"_s},
            {u"jutro o północy"_s, u"2026-10-16 00:00"_s},
            {u"w piątek o północy"_s, u"2026-10-17 00:00"_s},
            {u"15.10 o północy"_s, u"2026-10-16 00:00"_s},
            {u"tonight at midnight"_s, u"2026-10-15 00:00"_s},
            {u"tomorrow at midnight"_s, u"2026-10-16 00:00"_s},
            {u"dziś o 2 w nocy"_s, u"2026-10-15 02:00"_s},
            {u"jutro o 12 w nocy"_s, u"2026-10-16 00:00"_s},
            {u"wieczorem o północy"_s, u"2026-10-15 00:00"_s},
            {u"this evening at midnight"_s, u"2026-10-15 00:00"_s},
            {u"tonight at 0:00"_s, u"2026-10-15 00:00"_s},
            {u"tonight at 1am"_s, u"2026-10-15 01:00"_s},
            {u"jutro w nocy"_s, u"2026-10-15 22:00"_s},
            {u"jutro o 11 w nocy"_s, u"2026-10-15 23:00"_s},
            {u"jutro o 0:00"_s, u"2026-10-15 00:00"_s},
            {u"o północy"_s, u"2026-10-15 00:00"_s},
            {u"o 2 w nocy"_s, u"2026-10-15 02:00"_s},
            // 12 with an evening word is midnight, not noon.
            {u"tonight at 12"_s, u"2026-10-15 00:00"_s},
            {u"this evening at 12"_s, u"2026-10-15 00:00"_s},
            {u"wieczorem o 12"_s, u"2026-10-15 00:00"_s},
            {u"dziś wieczorem o 12"_s, u"2026-10-15 00:00"_s},
            {u"12 wieczorem"_s, u"2026-10-15 00:00"_s},
            {u"jutro wieczorem o 12"_s, u"2026-10-16 00:00"_s},
            {u"tonight at 11"_s, u"2026-10-14 23:00"_s},
            {u"tonight at 12pm"_s, u"2026-10-14 12:00"_s},
            {u"po południu o 12"_s, u"2026-10-14 12:00"_s},
        });
    }
    void followingNight()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(when(input), expected);
    }
    void endOfSupportedRange()
    {
        const QDateTime lastDay(QDate(9999, 12, 31), QTime(10, 0));
        const auto all = findNaturalDates(u"jutro, za tydzień, za 2 godziny, w przyszłym roku, next friday", lastDay);
        for (const auto &r : all)
            QVERIFY(r.spec.date.year() <= 9999);
        QCOMPARE(when(u"jutro", lastDay), u"null"_s);
        QCOMPARE(when(u"dziś", lastDay), u"9999-12-31"_s);
        QCOMPARE(when(u"za 2 godziny", lastDay), u"9999-12-31 12:00"_s);
        QCOMPARE(when(u"o północy", lastDay), u"null"_s);
        QCOMPARE(when(u"jutro", QDateTime(QDate(10000, 1, 1), QTime(0, 0))), u"null"_s);
    }
    void longInputsStayFast()
    {
        QElapsedTimer t;
        t.start();
        QVERIFY(!findNaturalDates(u"15.10 "_s.repeated(20'000), Now).isEmpty());
        QVERIFY(findNaturalDates(u"o 9 x "_s.repeated(20'000), Now).isEmpty());
        QCOMPARE(findNaturalDates(u"jutro "_s.repeated(20'000), Now).size(), 1);
        // Quadratic scans would take minutes here; linear ones well under the limit.
        QVERIFY2(t.elapsed() < 30'000, qPrintable(QString::number(t.elapsed())));
    }
    void longWhitespaceGaps()
    {
        QElapsedTimer t;
        t.start();
        const QString gap(40'000, u' ');
        const QString input = u"jutro"_s + gap + u"x o 18 w biurze"_s;
        const auto all = findNaturalDates(input, Now);
        QCOMPARE(all.size(), 2);
        QCOMPARE(input.sliced(all.at(0).start, all.at(0).length), u"jutro"_s);
        QCOMPARE(input.sliced(all.at(1).start, all.at(1).length), u"o 18"_s);
        QCOMPARE(when(QString(u"jutro"_s + gap + u"18:00"_s)), u"2026-10-15 18:00"_s);
        QVERIFY2(t.elapsed() < 10'000, qPrintable(QString::number(t.elapsed())));
    }

    // ---- second review regressions -------------------------------------------------------
    void ordinalsBeforeNouns_data()
    {
        QTest::addColumn<QString>("input");
        for (const QString &s : {u"Fix the 2nd bug"_s, u"Read the 3rd chapter"_s, u"the 21st century"_s,
                                 u"Meeting on the 3rd floor"_s, u"Zadanie 2-go stopnia"_s,
                                 u"Mieszkanie na 2-go piętrze"_s})
            QTest::newRow(s.toUtf8().constData()) << s;
    }
    void ordinalsBeforeNouns()
    {
        QFETCH(QString, input);
        QCOMPARE(when(input), u"null"_s);
    }
    void dayOfMonthPhrases_data()
    {
        inputExpectedMatchColumns();
        addRows({
            {u"the 15th"_s, u"2026-10-15"_s, u"the 15th"_s},
            {u"Pay rent on the 1st"_s, u"2026-11-01"_s, u"on the 1st"_s},
            {u"Pay rent on the 1st."_s, u"2026-11-01"_s, u"on the 1st"_s},
            {u"Finish the report by the 15th"_s, u"2026-10-15"_s, u"by the 15th"_s},
            {u"Keep it until the 20th"_s, u"2026-10-20"_s, u"until the 20th"_s},
            {u"the 15th at 6pm"_s, u"2026-10-15 18:00"_s, u"the 15th at 6pm"_s},
            {u"Call on the 20th and email Tom"_s, u"2026-10-20"_s, u"on the 20th"_s},
            {u"the 1st of every month"_s, u"2026-11-01"_s, u"the 1st"_s},
            {u"Spotkanie 15-go w biurze"_s, u"2026-10-15"_s, u"15-go"_s},
            {u"do 20-tego zapłacić czynsz"_s, u"2026-10-20"_s, u"do 20-tego"_s},
            {u"czynsz do 20-tego"_s, u"2026-10-20"_s, u"do 20-tego"_s},
            {u"20-go"_s, u"2026-10-20"_s, u"20-go"_s},
            {u"Czynsz płatny do 10-go każdego miesiąca"_s, u"2026-11-10"_s, u"do 10-go"_s},
            {u"na 15-go mamy spotkanie"_s, u"2026-10-15"_s, u"na 15-go"_s},
            // Bare units in the right grammatical case.
            {u"za dzień"_s, u"2026-10-15"_s, u"za dzień"_s},
            {u"za tydzień"_s, u"2026-10-21"_s, u"za tydzień"_s},
            {u"za rok"_s, u"2027-10-14"_s, u"za rok"_s},
            {u"za godzinę"_s, u"2026-10-14 11:00"_s, u"za godzinę"_s},
            {u"za minutę"_s, u"2026-10-14 10:01"_s, u"za minutę"_s},
            {u"za kwadrans"_s, u"2026-10-14 10:15"_s, u"za kwadrans"_s},
            {u"w ciągu tygodnia"_s, u"2026-10-21"_s, u"w ciągu tygodnia"_s},
            {u"w ciągu godziny"_s, u"2026-10-14 11:00"_s, u"w ciągu godziny"_s},
            {u"w ciągu kwadransa"_s, u"2026-10-14 10:15"_s, u"w ciągu kwadransa"_s},
            {u"tydzień temu"_s, u"2026-10-07"_s, u"tydzień temu"_s},
            {u"rok temu"_s, u"2025-10-14"_s, u"rok temu"_s},
            // "9h30" = hours and minutes.
            {u"o 9h30"_s, u"2026-10-15 09:30"_s, u"o 9h30"_s},
            {u"jutro o 18h15"_s, u"2026-10-15 18:15"_s, u"jutro o 18h15"_s},
            {u"o 18h"_s, u"2026-10-14 18:00"_s, u"o 18h"_s},
        });
    }
    void dayOfMonthPhrases()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QFETCH(QString, match);
        QCOMPARE(when(input), expected);
        QCOMPARE(matched(input), match);
    }
    void genitiveUnitAfterZa_data()
    {
        QTest::addColumn<QString>("input");
        for (const QString &s : {u"za dnia"_s, u"Wrócę do domu za dnia"_s, u"za tygodnia"_s, u"za roku"_s,
                                 u"za godziny"_s, u"za minuty"_s, u"o 9h3"_s, u"o 9h305"_s})
            QTest::newRow(s.toUtf8().constData()) << s;
    }
    void genitiveUnitAfterZa()
    {
        QFETCH(QString, input);
        QCOMPARE(when(input), u"null"_s);
    }

    // ---- protected Markdown/URL spans ----------------------------------------------------
    void neverInsideProtectedSpans_data()
    {
        QTest::addColumn<QString>("input");
        for (const QString &s : {u"Przeczytać https://blog.example.com/2026/10/15/wpis"_s,
                                 u"Read https://news.com/tomorrow-show later"_s,
                                 u"Obejrzeć https://yt.com/watch?v=abc&t=1:30"_s, u"www.example.com/2026/10/15"_s,
                                 u"Notatka do [[2026-10-15]]"_s, u"Zobacz [[Spotkanie w piątek]]"_s,
                                 u"Zobacz ![[2026-10-15]]"_s, u"Link [jutro](https://x.pl/2026-10-15)"_s,
                                 u"Kod `o 9`"_s, u"Mail jan.jutro@x.pl"_s, u"Tag #jutro"_s, u"Tag #2026-10-15"_s,
                                 u"Tag #[[w piątek]]"_s, u"Zadanie 📅 2026-10-15"_s, u"Zadanie ⏰ 2026-10-15 09:00"_s,
                                 u"Zadanie ⏰! 2026-10-15 06:30"_s, u"Zadanie (@2026-10-15 09:00)"_s,
                                 // Recurrence rules (SYNTAX §5/§7): their weekdays/days are no due dates.
                                 u"Podlać kwiaty 🔁 co tydzień w sobotę"_s, u"Czynsz 🔁 every month on the 15th"_s,
                                 u"Trening 🔁 every week on Monday, Friday when done"_s,
                                 u"Raport 🔁 co 2 tygodnie we wtorek i czwartek"_s,
                                 // Logseq planning timestamps (SYNTAX §5).
                                 u"SCHEDULED: <2026-10-15 Thu 10:00 .+1w>"_s, u"DEADLINE: <2026-10-20 Tue>"_s,
                                 u"Zadanie [2026-10-15 Thu]"_s,
                                 // Block properties (SYNTAX §4).
                                 u"termin:: 2026-10-15"_s, u"  - status:: do zrobienia jutro"_s})
            QTest::newRow(s.toUtf8().constData()) << s;
    }
    void neverInsideProtectedSpans()
    {
        QFETCH(QString, input);
        QCOMPARE(when(input), u"null"_s);
    }
    void nextToProtectedSpans_data()
    {
        inputExpectedMatchColumns(); // expected = text after stripping
        addRows({
            {u"Przeczytać https://blog.example.com/2026/10/15/wpis jutro"_s,
             u"Przeczytać https://blog.example.com/2026/10/15/wpis"_s, u"jutro"_s},
            {u"Sprawdź https://x.pl/a?d=15.10 jutro o 9"_s, u"Sprawdź https://x.pl/a?d=15.10"_s, u"jutro o 9"_s},
            {u"[[Projekty/Dom]] w piątek"_s, u"[[Projekty/Dom]]"_s, u"w piątek"_s},
            {u"Zobacz [[Spotkanie w piątek]] jutro"_s, u"Zobacz [[Spotkanie w piątek]]"_s, u"jutro"_s},
            {u"Kod `o 9` jutro"_s, u"Kod `o 9`"_s, u"jutro"_s},
            {u"((abc123)) jutro"_s, u"((abc123))"_s, u"jutro"_s},
            {u"#praca jutro o 9"_s, u"#praca"_s, u"jutro o 9"_s},
            {u"Zadanie 📅 2026-10-15 przypomnij jutro"_s, u"Zadanie 📅 2026-10-15 przypomnij"_s, u"jutro"_s},
            // Only the recurrence rule after 🔁 is protected, not the text that follows it.
            {u"Zadanie 🔁 every week on Monday jutro"_s, u"Zadanie 🔁 every week on Monday"_s, u"jutro"_s},
            {u"Kupić mleko 🔁 co tydzień jutro o 9"_s, u"Kupić mleko 🔁 co tydzień"_s, u"jutro o 9"_s},
            {u"Czynsz 🔁 co miesiąc 15. do jutra"_s, u"Czynsz 🔁 co miesiąc 15."_s, u"do jutra"_s},
            {u"Basen 🔁 co tydzień w piątek, jutro"_s, u"Basen 🔁 co tydzień w piątek"_s, u"jutro"_s},
            {u"Basen 🔁 co tydzień w piątek 📅 2026-10-16 jutro"_s, u"Basen 🔁 co tydzień w piątek 📅 2026-10-16"_s,
             u"jutro"_s},
            {u"🔁 jutro"_s, u"🔁"_s, u"jutro"_s}, // no rule at all: nothing to protect
            {u"Zadanie SCHEDULED: <2026-10-15 Thu 10:00 .+1w> jutro"_s,
             u"Zadanie SCHEDULED: <2026-10-15 Thu 10:00 .+1w>"_s, u"jutro"_s},
            {u"termin:: 2026-10-15\nzadzwonić jutro"_s, u"termin:: 2026-10-15\nzadzwonić"_s, u"jutro"_s},
            {u"Raport [2026-10-15]"_s, u"Raport"_s, u"2026-10-15"_s}, // no org timestamp (no weekday)
            {u"std::sort jutro"_s, u"std::sort"_s, u"jutro"_s},     // no property line
        });
    }
    void nextToProtectedSpans()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QFETCH(QString, match);
        QCOMPARE(matched(input), match);
        QCOMPARE(::strip(input), expected);
    }
    void dateRightAfterUrl()
    {
        QCOMPARE(when(u"https://x.pl 15.10"), u"2026-10-15"_s);
    }
    void protectedSpansStayLinear()
    {
        QElapsedTimer t;
        t.start();
        QCOMPARE(findNaturalDates(QString(u"[["_s.repeated(20'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(u"[a"_s.repeated(20'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(u"`a"_s.repeated(20'000), Now).size(), 0);
        QCOMPARE(findNaturalDates(QString(u"a@"_s.repeated(20'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(QString(40'000, u'a') + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(u"#a "_s.repeated(20'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(u"🔁 "_s.repeated(20'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(u"🔁 a"_s + u" a"_s.repeated(30'000) + u" jutro"_s), Now).size(), 1);
        QCOMPARE(findNaturalDates(QString(u"🔁 co tydzień w piątek "_s.repeated(5'000)), Now).size(), 0);
        // Unclosed "<" protects nothing: every ISO date is found (the last one merged with "jutro").
        QCOMPARE(findNaturalDates(QString(u"<2026-10-15 "_s.repeated(10'000) + u"jutro"_s), Now).size(), 10'000);
        QVERIFY2(t.elapsed() < 30'000, qPrintable(QString::number(t.elapsed())));
    }

    // ---- protectedMask (internal) --------------------------------------------------------
    void protectedMask_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QStringList>("spans");
        QTest::newRow("url") << u"Czytaj https://blog.pl/2026/10/15/wpis teraz"_s
                             << QStringList{u"https://blog.pl/2026/10/15/wpis"_s};
        QTest::newRow("url case") << u"HTTPS://Example.com/Jutro"_s << QStringList{u"https://example.com/jutro"_s};
        QTest::newRow("mailto") << u"mailto:jan@x.pl"_s << QStringList{u"mailto:jan@x.pl"_s};
        QTest::newRow("www") << u"www.zażółć.pl/2026/10/15 i dalej"_s << QStringList{u"www.zazolc.pl/2026/10/15"_s};
        QTest::newRow("wiki") << u"Zobacz [[Spotkanie w piątek]] dziś"_s << QStringList{u"[[spotkanie w piatek]]"_s};
        QTest::newRow("embed") << u"Osadź ![[Notatka#^abc123]]"_s << QStringList{u"![[notatka#^abc123]]"_s};
        QTest::newRow("md links") << u"Link [jutro](https://x.pl/a) i ![img](a.png)"_s
                                  << QStringList{u"[jutro](https://x.pl/a)"_s, u"![img](a.png)"_s};
        QTest::newRow("code") << u"Kod `o 9` i `jutro`"_s << QStringList{u"`o 9`"_s, u"`jutro`"_s};
        QTest::newRow("tags") << u"#praca i #Łódź/2026-10 oraz (#jutro)"_s
                              << QStringList{u"#praca"_s, u"#lodz/2026-10"_s, u"#jutro"_s};
        QTest::newRow("tag link") << u"#[[w piątek]]"_s << QStringList{u"[[w piatek]]"_s};
        QTest::newRow("email") << u"Napisz do jan.jutro@firma.pl."_s << QStringList{u"jan.jutro@firma.pl"_s};
        QTest::newRow("task tokens") << u"Zadanie 📅 2026-10-15 i ⏳ 2026-10-16"_s
                                     << QStringList{u"📅 2026-10-15"_s, u"⏳ 2026-10-16"_s};
        QTest::newRow("alarm") << u"Budzik ⏰! 2026-10-15 06:30 rano"_s << QStringList{u"⏰! 2026-10-15 06:30"_s};
        QTest::newRow("variation selector") << u"Budzik \u23F0\uFE0F 2026-10-15T06:30"_s
                                            << QStringList{u"⏰ 2026-10-15t06:30"_s};
        QTest::newRow("reminder plugin") << u"Plugin (@2026-10-15 09:00)"_s << QStringList{u"(@2026-10-15 09:00)"_s};
        QTest::newRow("recurrence") << u"Basen 🔁 co tydzień w piątek, jutro o 9"_s
                                    << QStringList{u"🔁 co tydzien w piatek,"_s};
        QTest::newRow("recurrence until next token")
            << u"Raport 🔁 every 2 weeks on Monday when done 📅 2026-10-19"_s
            << QStringList{u"🔁 every 2 weeks on monday when done"_s, u"📅 2026-10-19"_s};
        QTest::newRow("recurrence until line end") << u"🔁 co miesiąc ostatniego\njutro"_s
                                                   << QStringList{u"🔁 co miesiac ostatniego"_s};
        QTest::newRow("recurrence without rule") << u"🔁 jutro"_s << QStringList{u"🔁"_s};
        QTest::newRow("logseq planning") << u"SCHEDULED: <2026-10-15 Thu 10:00 .+1w>\nDEADLINE: <2026-10-20 Tue>"_s
                                         << QStringList{u"<2026-10-15 thu 10:00 .+1w>"_s, u"<2026-10-20 tue>"_s};
        QTest::newRow("org inactive timestamp") << u"Utworzono [2026-10-14 Wed 09:12]"_s
                                                << QStringList{u"[2026-10-14 wed 09:12]"_s};
        QTest::newRow("properties") << u"Spotkanie\ntermin:: 2026-10-15\n  - Status:: jutro\nkoniec"_s
                                    << QStringList{u"termin:: 2026-10-15"_s, u"  - status:: jutro"_s};
        QTest::newRow("property crlf") << u"id:: 6a1f\r\njutro"_s << QStringList{u"id:: 6a1f"_s};
        QTest::newRow("empty property") << u"termin::\njutro"_s << QStringList{u"termin::"_s};
        for (const QString &s : {u"Zadzwoń jutro o 18"_s, u"issue#12 and #1 or # heading"_s, u"a@b and x@y. too"_s,
                                 u"[niezamknięty link"_s, u"`niezamknięty kod"_s, u"[[niezamknięty wiki"_s,
                                 u"cena 5 zł @ sklep"_s, u"📅 bez daty"_s, u"Notatka: jutro"_s, u"std::sort jutro"_s,
                                 u"<15.10 jutro>"_s, u"[2026 jutro]"_s, u"a::b jutro"_s, u"key::"_s.repeated(3)})
            QTest::newRow(QString(u"nothing: "_s + s).toUtf8().constData()) << s << QStringList();
        QTest::newRow("nothing: empty") << QString() << QStringList();
    }
    void protectedMask()
    {
        QFETCH(QString, input);
        QFETCH(QStringList, spans);
        QCOMPARE(protectedSpans(input), spans);
    }
    void protectedMaskLinear()
    {
        QElapsedTimer t;
        t.start();
        const QStringList inputs = {
            u"[["_s.repeated(30'000),
            u"[a"_s.repeated(30'000),
            u"`a"_s.repeated(30'000),
            u"a@"_s.repeated(30'000),
            u"a@"_s + QString(60'000, u'.') + u"b"_s,
            u"a."_s.repeated(30'000) + u"@x"_s,
            u"#"_s + QString(60'000, u'1'),
            u"#1 "_s.repeated(20'000),
            u"http:"_s.repeated(12'000),
            u"🔁 a"_s.repeated(20'000),
            u"🔁"_s + u" a"_s.repeated(40'000),
            u"<2026-10-15 "_s.repeated(10'000),
            u"[2026-10-15 "_s.repeated(10'000),
            u"a:: "_s.repeated(20'000),
            u"\na::"_s.repeated(20'000),
        };
        for (const QString &s : inputs)
            detail::protectedMask(s);
        QVERIFY2(t.elapsed() < 10'000, qPrintable(QString::number(t.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestDatesNatural)
#include "tst_dates_natural.moc"
