// Weekly and monthly notes (periodic/periodic.h): ISO weeks (week 53, the week-year that differs
// from the calendar year at both ends, years with 52 and 53 weeks), period starts, ends and
// shifts, the file name formats both ways (the defaults, custom ones, nested folders, Polish and
// English month names, formats that cannot work), paths inside the period's folder (letter case,
// other folders, daily notes), malformed names, labels and the phrases a user types.
#include "periodic/periodic.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::periodic;

Q_DECLARE_METATYPE(brulion::periodic::Period)

namespace {

const QDateTime Now{QDate(2026, 11, 3), QTime(10, 0)}; // a Tuesday of ISO week 45

} // namespace

class TestPeriodic : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void isoWeeks_data()
    {
        QTest::addColumn<QDate>("date");
        QTest::addColumn<int>("year");
        QTest::addColumn<int>("week");
        QTest::addColumn<QDate>("monday");
        QTest::newRow("an ordinary Tuesday") << QDate(2026, 11, 3) << 2026 << 45 << QDate(2026, 11, 2);
        QTest::newRow("a Sunday ends its week") << QDate(2026, 11, 8) << 2026 << 45 << QDate(2026, 11, 2);
        QTest::newRow("1 Jan in the previous year's week 53") << QDate(2027, 1, 1) << 2026 << 53 << QDate(2026, 12, 28);
        QTest::newRow("3 Jan still week 53") << QDate(2027, 1, 3) << 2026 << 53 << QDate(2026, 12, 28);
        QTest::newRow("4 Jan always week 1") << QDate(2027, 1, 4) << 2027 << 1 << QDate(2027, 1, 4);
        QTest::newRow("30 Dec in the next year's week 1") << QDate(2024, 12, 30) << 2025 << 1 << QDate(2024, 12, 30);
        QTest::newRow("31 Dec 2025 in 2026-W01") << QDate(2025, 12, 31) << 2026 << 1 << QDate(2025, 12, 29);
        QTest::newRow("leap year 2020 has week 53") << QDate(2020, 12, 31) << 2020 << 53 << QDate(2020, 12, 28);
        QTest::newRow("3 Jan 2021 in 2020-W53") << QDate(2021, 1, 3) << 2020 << 53 << QDate(2020, 12, 28);
        QTest::newRow("29 Feb") << QDate(2024, 2, 29) << 2024 << 9 << QDate(2024, 2, 26);
        QTest::newRow("year 1") << QDate(1, 1, 8) << 1 << 2 << QDate(1, 1, 8);
    }
    void isoWeeks()
    {
        QFETCH(QDate, date);
        QFETCH(int, year);
        QFETCH(int, week);
        QFETCH(QDate, monday);
        QCOMPARE(isoWeek(date), (IsoWeek{year, week}));
        QCOMPARE(isoWeekStart(year, week), monday);
        QCOMPARE(periodStart(Period::Week, date), monday);
        QCOMPARE(periodEnd(Period::Week, date), monday.addDays(6));
    }

    void weeksInYear()
    {
        QCOMPARE(isoWeeksInYear(2026), 53); // starts on a Thursday
        QCOMPARE(isoWeeksInYear(2020), 53); // a leap year starting on a Wednesday
        QCOMPARE(isoWeeksInYear(2027), 52);
        QCOMPARE(isoWeeksInYear(2025), 52);
        QCOMPARE(isoWeeksInYear(2032), 53);
        QCOMPARE(isoWeeksInYear(0), 0);
        QCOMPARE(isoWeeksInYear(10000), 0);
        QVERIFY(!isoWeekStart(2027, 53).isValid());
        QVERIFY(!isoWeekStart(2026, 54).isValid());
        QVERIFY(!isoWeekStart(2026, 0).isValid());
        QVERIFY(!isoWeekStart(2026, -1).isValid());
        QCOMPARE(isoWeek(QDate()), IsoWeek{});
    }

    void months()
    {
        QCOMPARE(periodStart(Period::Month, QDate(2026, 11, 30)), QDate(2026, 11, 1));
        QCOMPARE(periodEnd(Period::Month, QDate(2024, 2, 3)), QDate(2024, 2, 29));
        QCOMPARE(periodEnd(Period::Month, QDate(2026, 12, 1)), QDate(2026, 12, 31));
        QCOMPARE(shiftPeriod(Period::Month, QDate(2026, 1, 31), 1), QDate(2026, 2, 1));
        QCOMPARE(shiftPeriod(Period::Month, QDate(2026, 12, 15), 1), QDate(2027, 1, 1));
        QCOMPARE(shiftPeriod(Period::Month, QDate(2026, 1, 15), -1), QDate(2025, 12, 1));
        QCOMPARE(shiftPeriod(Period::Week, QDate(2026, 12, 31), 1), QDate(2027, 1, 4));
        QCOMPARE(shiftPeriod(Period::Week, QDate(2027, 1, 4), -1), QDate(2026, 12, 28));
        QCOMPARE(shiftPeriod(Period::Week, QDate(2026, 11, 4), 0), QDate(2026, 11, 2));
        QVERIFY(!shiftPeriod(Period::Month, QDate(9999, 12, 5), 1).isValid());
        QVERIFY(!periodStart(Period::Week, QDate()).isValid());
    }

    void names_data()
    {
        QTest::addColumn<Period>("period");
        QTest::addColumn<QString>("format");
        QTest::addColumn<QDate>("date");
        QTest::addColumn<QString>("name");
        const Period W = Period::Week;
        const Period M = Period::Month;
        QTest::newRow("week default") << W << u"yyyy-'W'ww"_s << QDate(2026, 11, 3) << u"2026-W45"_s;
        QTest::newRow("week 53 named by its week-year") << W << u"yyyy-'W'ww"_s << QDate(2027, 1, 2) << u"2026-W53"_s;
        QTest::newRow("week 1 of next year") << W << u"yyyy-'W'ww"_s << QDate(2024, 12, 31) << u"2025-W01"_s;
        QTest::newRow("week without padding") << W << u"yyyy-'W'w"_s << QDate(2026, 1, 7) << u"2026-W2"_s;
        QTest::newRow("Polish words") << W << u"'Tydzień' ww yyyy"_s << QDate(2026, 3, 4) << u"Tydzień 10 2026"_s;
        QTest::newRow("quote in a literal") << W << u"yyyy-'W'ww 'o''k'"_s << QDate(2026, 3, 4) << u"2026-W10 o'k"_s;
        QTest::newRow("nested folders") << W << u"yyyy/'W'ww"_s << QDate(2026, 11, 3) << u"2026/W45"_s;
        QTest::newRow("two-digit year") << W << u"yy'w'ww"_s << QDate(2026, 11, 3) << u"26w45"_s;
        QTest::newRow("month default") << M << u"yyyy-MM"_s << QDate(2026, 11, 30) << u"2026-11"_s;
        QTest::newRow("month name") << M << u"MMMM yyyy"_s << QDate(2026, 2, 14) << u"February 2026"_s;
        QTest::newRow("month nested") << M << u"yyyy/MM"_s << QDate(2026, 2, 14) << u"2026/02"_s;
        QTest::newRow("month with literal") << M << u"yyyy-MM 'miesiąc'"_s << QDate(2026, 5, 1) << u"2026-05 miesiąc"_s;
    }
    void names()
    {
        QFETCH(Period, period);
        QFETCH(QString, format);
        QFETCH(QDate, date);
        QFETCH(QString, name);
        QVERIFY(isUsableFormat(period, format));
        QCOMPARE(nameFor(period, date, format), name);
        QCOMPARE(dateFromName(period, name, format), periodStart(period, date));
        // Letter case aside (a file system that ignores it, a name typed by hand).
        QCOMPARE(dateFromName(period, name.toLower(), format), periodStart(period, date));
    }

    void unusableFormats_data()
    {
        QTest::addColumn<Period>("period");
        QTest::addColumn<QString>("format");
        const Period W = Period::Week;
        const Period M = Period::Month;
        QTest::newRow("week empty") << W << QString();
        QTest::newRow("week blank") << W << u"   "_s;
        QTest::newRow("no week") << W << u"yyyy"_s;
        QTest::newRow("no year") << W << u"'W'ww"_s;
        QTest::newRow("unquoted W") << W << u"yyyy-Www"_s;
        QTest::newRow("Qt day token") << W << u"yyyy-ww-dd"_s;
        QTest::newRow("three w") << W << u"yyyy-'W'www"_s;
        QTest::newRow("three y") << W << u"yyy-'W'ww"_s;
        QTest::newRow("unclosed quote") << W << u"yyyy-'W ww"_s;
        QTest::newRow("leading slash") << W << u"/yyyy-'W'ww"_s;
        QTest::newRow("dot folder") << W << u"'.'yyyy/'W'ww"_s;
        QTest::newRow("month empty") << M << QString();
        QTest::newRow("month with day") << M << u"yyyy-MM-dd"_s;
        QTest::newRow("month with weekday") << M << u"yyyy-MM ddd"_s;
        QTest::newRow("year alone") << M << u"yyyy"_s;
        QTest::newRow("month alone") << M << u"MM"_s;
        QTest::newRow("month backslash") << M << u"yyyy\\MM"_s;
    }
    void unusableFormats()
    {
        QFETCH(Period, period);
        QFETCH(QString, format);
        QVERIFY(!isUsableFormat(period, format));
        // The default is used instead.
        VaultConfig c;
        (period == Period::Week ? c.weeklyFileFormat : c.monthlyFileFormat) = format;
        QCOMPARE(fileFormat(period, c), defaultFileFormat(period));
    }

    void malformedNames_data()
    {
        QTest::addColumn<Period>("period");
        QTest::addColumn<QString>("name");
        const Period W = Period::Week;
        const Period M = Period::Month;
        QTest::newRow("week 54") << W << u"2026-W54"_s;
        QTest::newRow("week 53 of a 52-week year") << W << u"2027-W53"_s;
        QTest::newRow("week 0") << W << u"2026-W00"_s;
        QTest::newRow("unpadded week") << W << u"2026-W5"_s;
        QTest::newRow("three-digit week") << W << u"2026-W045"_s;
        QTest::newRow("no W") << W << u"2026-45"_s;
        QTest::newRow("a day") << W << u"2026-11-03"_s;
        QTest::newRow("trailing text") << W << u"2026-W45 kopia"_s;
        QTest::newRow("leading blank") << W << u" 2026-W45"_s;
        QTest::newRow("letters") << W << u"abcd-Wef"_s;
        QTest::newRow("empty") << W << QString();
        QTest::newRow("Arabic-Indic digits") << W << u"٢٠٢٦-W45"_s;
        QTest::newRow("month 13") << M << u"2026-13"_s;
        QTest::newRow("month 00") << M << u"2026-00"_s;
        QTest::newRow("unpadded month") << M << u"2026-1"_s;
        QTest::newRow("a day, not a month") << M << u"2026-11-03"_s;
        QTest::newRow("a week, not a month") << M << u"2026-W45"_s;
        QTest::newRow("year 0") << M << u"0000-01"_s;
        QTest::newRow("month empty") << M << QString();
    }
    void malformedNames()
    {
        QFETCH(Period, period);
        QFETCH(QString, name);
        QVERIFY(!dateFromName(period, name, defaultFileFormat(period)));
    }

    void inconsistentParts()
    {
        // Every part of a name must name the same week.
        QVERIFY(!dateFromName(Period::Week, u"2026/2027-W45"_s, u"yyyy/yyyy-'W'ww"_s));
        QCOMPARE(dateFromName(Period::Week, u"2026/2026-W45"_s, u"yyyy/yyyy-'W'ww"_s), QDate(2026, 11, 2));
        QVERIFY(!dateFromName(Period::Week, u"2026-W45-W46"_s, u"yyyy-'W'ww-'W'ww"_s));
        QVERIFY(!dateFromName(Period::Week, u"2026 27-W45"_s, u"yyyy yy-'W'ww"_s));
        QVERIFY(!dateFromName(Period::Month, u"2026/2027-11"_s, u"yyyy/yyyy-MM"_s));
        QCOMPARE(dateFromName(Period::Month, u"2026/2026-11"_s, u"yyyy/yyyy-MM"_s), QDate(2026, 11, 1));
    }

    void monthNamesInPolish()
    {
        // Written in English (as the daily notes), read in English or Polish.
        QCOMPARE(nameFor(Period::Month, QDate(2026, 10, 9), u"MMMM yyyy"_s), u"October 2026"_s);
        QCOMPARE(dateFromName(Period::Month, u"październik 2026"_s, u"MMMM yyyy"_s), QDate(2026, 10, 1));
        QCOMPARE(dateFromName(Period::Month, u"października 2026"_s, u"MMMM yyyy"_s), QDate(2026, 10, 1));
        QCOMPARE(dateFromName(Period::Month, u"Październik 2026"_s, u"MMMM yyyy"_s), QDate(2026, 10, 1));
        QCOMPARE(dateFromName(Period::Month, u"Oct 2026"_s, u"MMM yyyy"_s), QDate(2026, 10, 1));
        QVERIFY(!dateFromName(Period::Month, u"Pazdziernik 2026"_s, u"MMMM yyyy"_s)); // a misspelled name
        QVERIFY(!dateFromName(Period::Month, u"Smarch 2026"_s, u"MMMM yyyy"_s));
    }

    void paths_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<int>("period"); // -1: none, 0 week, 1 month
        QTest::addColumn<QDate>("start");
        QTest::newRow("weekly") << u"journals/2026-W45.md"_s << 0 << QDate(2026, 11, 2);
        QTest::newRow("weekly, folder in another case") << u"Journals/2026-w45.md"_s << 0 << QDate(2026, 11, 2);
        QTest::newRow("week 53") << u"journals/2020-W53.md"_s << 0 << QDate(2020, 12, 28);
        QTest::newRow("monthly") << u"journals/2026-11.md"_s << 1 << QDate(2026, 11, 1);
        QTest::newRow("a daily note") << u"journals/2026-11-03.md"_s << -1 << QDate();
        QTest::newRow("another folder") << u"notes/2026-W45.md"_s << -1 << QDate();
        QTest::newRow("a subfolder") << u"journals/stare/2026-W45.md"_s << -1 << QDate();
        QTest::newRow("not Markdown") << u"journals/2026-W45.txt"_s << -1 << QDate();
        QTest::newRow("a folder name alone") << u"journals.md"_s << -1 << QDate();
        QTest::newRow("a page") << u"journals/Żółw.md"_s << -1 << QDate();
        QTest::newRow("week 54") << u"journals/2026-W54.md"_s << -1 << QDate();
    }
    void paths()
    {
        QFETCH(QString, path);
        QFETCH(int, period);
        QFETCH(QDate, start);
        const VaultConfig c;
        const auto note = periodicNoteFromPath(path, c);
        QCOMPARE(note.has_value(), period >= 0);
        if (note) {
            QCOMPARE(int(note->period), period);
            QCOMPARE(note->start, start);
        }
    }

    void pathsWithConfig()
    {
        VaultConfig c;
        QCOMPARE(pathFor(Period::Week, QDate(2027, 1, 1), c), u"journals/2026-W53.md"_s);
        QCOMPARE(pathFor(Period::Month, QDate(2027, 1, 1), c), u"journals/2027-01.md"_s);
        QVERIFY(pathFor(Period::Week, QDate(), c).isEmpty());
        c.weeklyFolder = u"Dziennik/Tygodnie/"_s;
        c.weeklyFileFormat = u"yyyy/'Tydzień' ww"_s;
        c.monthlyFolder = QString(); // the notebook's root
        c.monthlyFileFormat = u"MMMM yyyy"_s;
        QCOMPARE(pathFor(Period::Week, QDate(2026, 11, 3), c), u"Dziennik/Tygodnie/2026/Tydzień 45.md"_s);
        QCOMPARE(dateFromPath(Period::Week, u"Dziennik/Tygodnie/2026/Tydzień 45.md"_s, c), QDate(2026, 11, 2));
        QVERIFY(!dateFromPath(Period::Week, u"journals/2026-W45.md"_s, c));
        QCOMPARE(pathFor(Period::Month, QDate(2026, 11, 3), c), u"November 2026.md"_s);
        QCOMPARE(dateFromPath(Period::Month, u"listopad 2026.md"_s, c), QDate(2026, 11, 1));
        QCOMPARE(folder(Period::Week, c), u"Dziennik/Tygodnie"_s);
        // An unusable format falls back to the default.
        c.weeklyFileFormat = u"ww"_s;
        QCOMPARE(pathFor(Period::Week, QDate(2026, 11, 3), c), u"Dziennik/Tygodnie/2026-W45.md"_s);
    }

    void periodNames()
    {
        QCOMPARE(parsePeriodName(u"week"), Period::Week);
        QCOMPARE(parsePeriodName(u" Tydzień "), Period::Week);
        QCOMPARE(parsePeriodName(u"tygodniowa"), Period::Week);
        QCOMPARE(parsePeriodName(u"MONTH"), Period::Month);
        QCOMPARE(parsePeriodName(u"miesiąc"), Period::Month);
        QCOMPARE(parsePeriodName(u"miesięczna"), Period::Month);
        QVERIFY(!parsePeriodName(u"day"));
        QVERIFY(!parsePeriodName(u""));
        QCOMPARE(periodName(Period::Week), u"week"_s);
        QCOMPARE(periodName(Period::Month), u"month"_s);
    }

    void labels()
    {
        QCOMPARE(periodLabel(Period::Week, QDate(2026, 11, 3), dates::UiLocale::Polish),
                 u"Tydzień 45 (2–8 listopada 2026)"_s);
        QCOMPARE(periodLabel(Period::Week, QDate(2026, 11, 3), dates::UiLocale::English), u"Week 45 (Nov 2–8, 2026)"_s);
        QCOMPARE(periodLabel(Period::Week, QDate(2026, 10, 28), dates::UiLocale::Polish),
                 u"Tydzień 44 (26 października – 1 listopada 2026)"_s);
        QCOMPARE(periodLabel(Period::Week, QDate(2026, 10, 28), dates::UiLocale::English),
                 u"Week 44 (Oct 26 – Nov 1, 2026)"_s);
        QCOMPARE(periodLabel(Period::Week, QDate(2027, 1, 1), dates::UiLocale::Polish),
                 u"Tydzień 53 (28 grudnia 2026 – 3 stycznia 2027)"_s);
        QCOMPARE(periodLabel(Period::Month, QDate(2026, 11, 3), dates::UiLocale::Polish), u"Listopad 2026"_s);
        QCOMPARE(periodLabel(Period::Month, QDate(2026, 11, 3), dates::UiLocale::English), u"November 2026"_s);
        QCOMPARE(periodLabel(Period::Month, QDate(2026, 6, 3), dates::UiLocale::Polish), u"Czerwiec 2026"_s);
        QVERIFY(periodLabel(Period::Month, QDate(), dates::UiLocale::Polish).isEmpty());
    }

    void phrases_data()
    {
        QTest::addColumn<Period>("period");
        QTest::addColumn<QString>("phrase");
        QTest::addColumn<QDate>("start"); // invalid: not understood
        const Period W = Period::Week;
        const Period M = Period::Month;
        const QDate thisWeek(2026, 11, 2);
        QTest::newRow("empty") << W << QString() << thisWeek;
        QTest::newRow("ten tydzień") << W << u"ten tydzień"_s << thisWeek;
        QTest::newRow("bez znaków") << W << u"TEN TYDZIEN"_s << thisWeek;
        QTest::newRow("tydzień") << W << u"tydzień"_s << thisWeek;
        QTest::newRow("this week") << W << u"this week"_s << thisWeek;
        QTest::newRow("w tym tygodniu") << W << u"w tym tygodniu"_s << thisWeek;
        QTest::newRow("przyszły tydzień") << W << u"przyszły tydzień"_s << QDate(2026, 11, 9);
        QTest::newRow("w przyszłym tygodniu") << W << u"w przyszłym tygodniu"_s << QDate(2026, 11, 9);
        QTest::newRow("next week") << W << u"next week"_s << QDate(2026, 11, 9);
        QTest::newRow("zeszły tydzień") << W << u"zeszły tydzień"_s << QDate(2026, 10, 26);
        QTest::newRow("poprzedni tydzień") << W << u"poprzedni  tydzień"_s << QDate(2026, 10, 26);
        QTest::newRow("last week") << W << u"last week"_s << QDate(2026, 10, 26);
        QTest::newRow("ISO") << W << u"2026-W53"_s << QDate(2026, 12, 28);
        QTest::newRow("ISO lower, no dash") << W << u"2025w01"_s << QDate(2024, 12, 30);
        QTest::newRow("W alone") << W << u"W10"_s << QDate(2026, 3, 2);
        QTest::newRow("w with year") << W << u"w1 2027"_s << QDate(2027, 1, 4);
        QTest::newRow("tydzień 45") << W << u"tydzień 45"_s << thisWeek;
        QTest::newRow("tydz. 45 2026") << W << u"tydz. 45 2026"_s << thisWeek;
        QTest::newRow("week 45, 2026") << W << u"week 45, 2026"_s << thisWeek;
        QTest::newRow("week 53 of a 52-week year") << W << u"2027-W53"_s << QDate();
        QTest::newRow("week 0") << W << u"W0"_s << QDate();
        QTest::newRow("a day: its week") << W << u"2027-01-01"_s << QDate(2026, 12, 28);
        QTest::newRow("jutro") << W << u"jutro"_s << thisWeek;
        QTest::newRow("za 2 tygodnie") << W << u"za 2 tygodnie"_s << QDate(2026, 11, 16);
        QTest::newRow("15 października") << W << u"15 października"_s << QDate(2027, 10, 11);
        QTest::newRow("text around a date") << W << u"jutro albo kiedyś"_s << QDate();
        QTest::newRow("nonsense") << W << u"żółw"_s << QDate();
        QTest::newRow("ten miesiąc") << M << u"ten miesiąc"_s << QDate(2026, 11, 1);
        QTest::newRow("bieżący miesiąc") << M << u"bieżący miesiąc"_s << QDate(2026, 11, 1);
        QTest::newRow("this month") << M << u"This Month"_s << QDate(2026, 11, 1);
        QTest::newRow("następny miesiąc") << M << u"następny miesiąc"_s << QDate(2026, 12, 1);
        QTest::newRow("w przyszłym miesiącu") << M << u"w przyszłym miesiącu"_s << QDate(2026, 12, 1);
        QTest::newRow("last month") << M << u"last month"_s << QDate(2026, 10, 1);
        QTest::newRow("ubiegły miesiąc") << M << u"ubiegły miesiąc"_s << QDate(2026, 10, 1);
        QTest::newRow("2026-11") << M << u"2026-11"_s << QDate(2026, 11, 1);
        QTest::newRow("11.2026") << M << u"11.2026"_s << QDate(2026, 11, 1);
        QTest::newRow("3/2027") << M << u"3/2027"_s << QDate(2027, 3, 1);
        QTest::newRow("listopad") << M << u"listopad"_s << QDate(2026, 11, 1);
        QTest::newRow("styczeń 2027") << M << u"styczeń 2027"_s << QDate(2027, 1, 1);
        QTest::newRow("stycznia 2027") << M << u"stycznia 2027"_s << QDate(2027, 1, 1);
        QTest::newRow("Nov 2025") << M << u"Nov 2025"_s << QDate(2025, 11, 1);
        QTest::newRow("paź.") << M << u"paź."_s << QDate(2026, 10, 1);
        QTest::newRow("February") << M << u"February"_s << QDate(2026, 2, 1);
        QTest::newRow("month 13") << M << u"2026-13"_s << QDate();
        QTest::newRow("a day: its month") << M << u"2026-02-14"_s << QDate(2026, 2, 1);
        QTest::newRow("pojutrze") << M << u"pojutrze"_s << QDate(2026, 11, 1);
        QTest::newRow("a week word for a month") << M << u"ten tydzień"_s << QDate();
        QTest::newRow("nonsense month") << M << u"smarch"_s << QDate();
    }
    void phrases()
    {
        QFETCH(Period, period);
        QFETCH(QString, phrase);
        QFETCH(QDate, start);
        const auto got = parsePeriodPhrase(period, phrase, Now);
        QCOMPARE(got.has_value(), start.isValid());
        if (got)
            QCOMPARE(*got, start);
    }

    void phraseWithVaultFormat()
    {
        VaultConfig c;
        c.weeklyFileFormat = u"'Tydzień' ww yyyy"_s;
        QCOMPARE(parsePeriodPhrase(Period::Week, u"Tydzień 10 2026", Now, Qt::Monday, dates::UiLocale::Polish, &c),
                 QDate(2026, 3, 2));
        c.monthlyFileFormat = u"MMMM yyyy"_s;
        QCOMPARE(parsePeriodPhrase(Period::Month, u"październik 2026", Now, Qt::Monday, dates::UiLocale::Polish, &c),
                 QDate(2026, 10, 1));
        QVERIFY(!parsePeriodPhrase(Period::Week, u"x", QDateTime()));
    }
};

QTEST_GUILESS_MAIN(TestPeriodic)
#include "tst_periodic.moc"
