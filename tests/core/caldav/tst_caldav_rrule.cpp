// iCalendar recurrence rules in full: parsing and formatting, and the expansion of every example of
// RFC 5545 §3.8.5.3, rules that never match or never end (hostile feeds), windows far from the
// start, COUNT and UNTIL.
#include "caldav/rrule.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

NaiveDateTime at(const QString &text)
{
    const QDate d = QDate::fromString(text.left(8), u"yyyyMMdd"_s);
    QTime t(0, 0);
    if (text.size() >= 13)
        t = QTime(text.mid(9, 2).toInt(), text.mid(11, 2).toInt());
    return {d, t};
}

QString format(const QList<NaiveDateTime> &list, bool withTime)
{
    QStringList out;
    for (const NaiveDateTime &t : list)
        out.append(withTime ? t.date.toString(u"yyyyMMdd"_s) + u'T' + t.time.toString(u"HHmm"_s)
                            : t.date.toString(u"yyyyMMdd"_s));
    return out.join(u',');
}

} // namespace

class TestCalDavRRule : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parse_data();
    void parse();
    void refused_data();
    void refused();
    void rfcExamples_data();
    void rfcExamples();
    void hostileRulesEnd();
    void windowFarFromStart();
    void countCountsFromStart();
    void allDaySeries();
};

void TestCalDavRRule::parse_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("formatted");
    QTest::newRow("daily") << u"FREQ=DAILY"_s << u"FREQ=DAILY"_s;
    QTest::newRow("lower case, spaces") << u" freq=weekly; byday=mo,fr ;interval=2"_s
                                        << u"FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,FR"_s;
    QTest::newRow("ordinals") << u"FREQ=MONTHLY;BYDAY=-1FR,+2TU"_s << u"FREQ=MONTHLY;BYDAY=-1FR,2TU"_s;
    QTest::newRow("until utc") << u"FREQ=DAILY;UNTIL=19971224T000000Z"_s << u"FREQ=DAILY;UNTIL=19971224T000000Z"_s;
    QTest::newRow("until date") << u"FREQ=YEARLY;UNTIL=20301231"_s << u"FREQ=YEARLY;UNTIL=20301231"_s;
    QTest::newRow("everything") << u"FREQ=YEARLY;COUNT=3;BYMONTH=3;BYDAY=TH;BYSETPOS=1;WKST=SU;X-FOO=1"_s
                                << u"FREQ=YEARLY;COUNT=3;BYDAY=TH;BYMONTH=3;BYSETPOS=1;WKST=SU"_s;
}

void TestCalDavRRule::parse()
{
    QFETCH(QString, text);
    QFETCH(QString, formatted);
    const auto rule = parseRRule(text);
    QVERIFY(rule);
    QCOMPARE(formatRRule(*rule), formatted);
    QCOMPARE(parseRRule(formatRRule(*rule)), rule);
}

void TestCalDavRRule::refused_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("no freq") << u"INTERVAL=2"_s;
    QTest::newRow("unknown freq") << u"FREQ=FORTNIGHTLY"_s;
    QTest::newRow("interval 0") << u"FREQ=DAILY;INTERVAL=0"_s;
    QTest::newRow("bad day") << u"FREQ=WEEKLY;BYDAY=PN"_s;
    QTest::newRow("ordinal 0") << u"FREQ=MONTHLY;BYDAY=0MO"_s;
    QTest::newRow("month 13") << u"FREQ=YEARLY;BYMONTH=13"_s;
    QTest::newRow("monthday 0") << u"FREQ=MONTHLY;BYMONTHDAY=0"_s;
    QTest::newRow("bad until") << u"FREQ=DAILY;UNTIL=jutro"_s;
    QTest::newRow("no equals") << u"FREQ=DAILY;COUNT"_s;
    QTest::newRow("polish garbage") << u"FREQ=CODZIENNIE;ŻÓŁW=1"_s;
}

void TestCalDavRRule::refused()
{
    QFETCH(QString, text);
    QString error;
    QVERIFY(!parseRRule(text, &error));
    QVERIFY(!error.isEmpty());
}

void TestCalDavRRule::rfcExamples_data()
{
    QTest::addColumn<QString>("dtstart");
    QTest::addColumn<QString>("rule");
    QTest::addColumn<QString>("until"); ///< local, "" none
    QTest::addColumn<QString>("to");    ///< end of the window
    QTest::addColumn<int>("first");     ///< how many results to compare
    QTest::addColumn<QString>("expected");
    QTest::addColumn<int>("total");     ///< -1: not checked

    QTest::newRow("daily count") << u"19970902T0900"_s << u"FREQ=DAILY;COUNT=10"_s << QString() << u"20000101"_s << 10
                                 << u"19970902,19970903,19970904,19970905,19970906,19970907,19970908,19970909,19970910,19970911"_s
                                 << 10;
    QTest::newRow("daily until") << u"19970902T0900"_s << u"FREQ=DAILY"_s << u"19971223T1900"_s << u"20000101"_s << 2
                                 << u"19970902,19970903"_s << 113;
    QTest::newRow("every other day") << u"19970902T0900"_s << u"FREQ=DAILY;INTERVAL=2"_s << QString() << u"19970913"_s
                                     << 6 << u"19970902,19970904,19970906,19970908,19970910,19970912"_s << 6;
    QTest::newRow("every 10 days") << u"19970902T0900"_s << u"FREQ=DAILY;INTERVAL=10;COUNT=5"_s << QString()
                                   << u"20000101"_s << 5 << u"19970902,19970912,19970922,19971002,19971012"_s << 5;
    QTest::newRow("january yearly") << u"19980101T0900"_s
                                    << u"FREQ=YEARLY;BYMONTH=1;BYDAY=SU,MO,TU,WE,TH,FR,SA"_s << u"20000131T0900"_s
                                    << u"20010101"_s << 3 << u"19980101,19980102,19980103"_s << 93;
    QTest::newRow("january daily") << u"19980101T0900"_s << u"FREQ=DAILY;BYMONTH=1"_s << u"20000131T0900"_s
                                   << u"20010101"_s << 2 << u"19980101,19980102"_s << 93;
    QTest::newRow("weekly count") << u"19970902T0900"_s << u"FREQ=WEEKLY;COUNT=10"_s << QString() << u"20000101"_s << 10
                                  << u"19970902,19970909,19970916,19970923,19970930,19971007,19971014,19971021,19971028,19971104"_s
                                  << 10;
    QTest::newRow("weekly tu th until") << u"19970902T0900"_s << u"FREQ=WEEKLY;WKST=SU;BYDAY=TU,TH"_s
                                        << u"19971007T0000"_s << u"20000101"_s << 10
                                        << u"19970902,19970904,19970909,19970911,19970916,19970918,19970923,19970925,19970930,19971002"_s
                                        << 10;
    QTest::newRow("every other week mo we fr") << u"19970901T0900"_s
                                               << u"FREQ=WEEKLY;INTERVAL=2;WKST=SU;BYDAY=MO,WE,FR"_s
                                               << u"19971224T0000"_s << u"20000101"_s << 6
                                               << u"19970901,19970903,19970905,19970915,19970917,19970919"_s << 25;
    QTest::newRow("every other week tu th count") << u"19970902T0900"_s
                                                  << u"FREQ=WEEKLY;INTERVAL=2;COUNT=8;WKST=SU;BYDAY=TU,TH"_s
                                                  << QString() << u"20000101"_s << 8
                                                  << u"19970902,19970904,19970916,19970918,19970930,19971002,19971014,19971016"_s
                                                  << 8;
    QTest::newRow("first friday") << u"19970905T0900"_s << u"FREQ=MONTHLY;COUNT=10;BYDAY=1FR"_s << QString()
                                  << u"20000101"_s << 10
                                  << u"19970905,19971003,19971107,19971205,19980102,19980206,19980306,19980403,19980501,19980605"_s
                                  << 10;
    QTest::newRow("first and last sunday") << u"19970907T0900"_s << u"FREQ=MONTHLY;INTERVAL=2;COUNT=10;BYDAY=1SU,-1SU"_s
                                           << QString() << u"20000101"_s << 10
                                           << u"19970907,19970928,19971102,19971130,19980104,19980125,19980301,19980329,19980503,19980531"_s
                                           << 10;
    QTest::newRow("second to last monday") << u"19970922T0900"_s << u"FREQ=MONTHLY;COUNT=6;BYDAY=-2MO"_s << QString()
                                           << u"20000101"_s << 6
                                           << u"19970922,19971020,19971117,19971222,19980119,19980216"_s << 6;
    QTest::newRow("third to last day") << u"19970928T0900"_s << u"FREQ=MONTHLY;BYMONTHDAY=-3"_s << QString()
                                       << u"19980301"_s << 6 << u"19970928,19971029,19971128,19971229,19980129,19980226"_s
                                       << 6;
    QTest::newRow("2nd and 15th") << u"19970902T0900"_s << u"FREQ=MONTHLY;COUNT=10;BYMONTHDAY=2,15"_s << QString()
                                  << u"20000101"_s << 10
                                  << u"19970902,19970915,19971002,19971015,19971102,19971115,19971202,19971215,19980102,19980115"_s
                                  << 10;
    QTest::newRow("first and last day") << u"19970930T0900"_s << u"FREQ=MONTHLY;COUNT=10;BYMONTHDAY=1,-1"_s
                                        << QString() << u"20000101"_s << 10
                                        << u"19970930,19971001,19971031,19971101,19971130,19971201,19971231,19980101,19980131,19980201"_s
                                        << 10;
    QTest::newRow("every 18 months") << u"19970910T0900"_s
                                     << u"FREQ=MONTHLY;INTERVAL=18;COUNT=10;BYMONTHDAY=10,11,12,13,14,15"_s << QString()
                                     << u"20010101"_s << 10
                                     << u"19970910,19970911,19970912,19970913,19970914,19970915,19990310,19990311,19990312,19990313"_s
                                     << 10;
    QTest::newRow("every tuesday every other month") << u"19970902T0900"_s << u"FREQ=MONTHLY;INTERVAL=2;BYDAY=TU"_s
                                                     << QString() << u"19980101"_s << 6
                                                     << u"19970902,19970909,19970916,19970923,19970930,19971104"_s << 9;
    QTest::newRow("june and july") << u"19970610T0900"_s << u"FREQ=YEARLY;COUNT=10;BYMONTH=6,7"_s << QString()
                                   << u"20100101"_s << 10
                                   << u"19970610,19970710,19980610,19980710,19990610,19990710,20000610,20000710,20010610,20010710"_s
                                   << 10;
    QTest::newRow("every other year jan feb mar") << u"19970310T0900"_s
                                                  << u"FREQ=YEARLY;INTERVAL=2;COUNT=10;BYMONTH=1,2,3"_s << QString()
                                                  << u"20100101"_s << 10
                                                  << u"19970310,19990110,19990210,19990310,20010110,20010210,20010310,20030110,20030210,20030310"_s
                                                  << 10;
    QTest::newRow("year days") << u"19970101T0900"_s << u"FREQ=YEARLY;INTERVAL=3;COUNT=10;BYYEARDAY=1,100,200"_s
                               << QString() << u"20100101"_s << 10
                               << u"19970101,19970410,19970719,20000101,20000409,20000718,20030101,20030410,20030719,20060101"_s
                               << 10;
    QTest::newRow("20th monday") << u"19970519T0900"_s << u"FREQ=YEARLY;BYDAY=20MO"_s << QString() << u"20000101"_s << 3
                                 << u"19970519,19980518,19990517"_s << 3;
    QTest::newRow("week 20 monday") << u"19970512T0900"_s << u"FREQ=YEARLY;BYWEEKNO=20;BYDAY=MO"_s << QString()
                                    << u"20000101"_s << 3 << u"19970512,19980511,19990517"_s << 3;
    QTest::newRow("thursdays in march") << u"19970313T0900"_s << u"FREQ=YEARLY;BYMONTH=3;BYDAY=TH"_s << QString()
                                        << u"20000101"_s << 11
                                        << u"19970313,19970320,19970327,19980305,19980312,19980319,19980326,19990304,19990311,19990318,19990325"_s
                                        << 11;
    QTest::newRow("thursdays in summer") << u"19970605T0900"_s << u"FREQ=YEARLY;BYDAY=TH;BYMONTH=6,7,8"_s << QString()
                                         << u"19980101"_s << 4 << u"19970605,19970612,19970619,19970626"_s << 13;
    // DTSTART is no Friday the 13th but always counts (RFC 5545 excludes it with an EXDATE).
    QTest::newRow("friday 13th") << u"19970902T0900"_s << u"FREQ=MONTHLY;BYDAY=FR;BYMONTHDAY=13"_s << QString()
                                 << u"20001101"_s << 6 << u"19970902,19980213,19980313,19981113,19990813,20001013"_s << 6;
    QTest::newRow("saturday after first sunday") << u"19970913T0900"_s
                                                 << u"FREQ=MONTHLY;BYDAY=SA;BYMONTHDAY=7,8,9,10,11,12,13"_s << QString()
                                                 << u"19980401"_s << 7
                                                 << u"19970913,19971011,19971108,19971213,19980110,19980207,19980307"_s << 7;
    QTest::newRow("election day") << u"19961105T0900"_s
                                  << u"FREQ=YEARLY;INTERVAL=4;BYMONTH=11;BYDAY=TU;BYMONTHDAY=2,3,4,5,6,7,8"_s << QString()
                                  << u"20050101"_s << 3 << u"19961105,20001107,20041102"_s << 3;
    QTest::newRow("third tu we th") << u"19970904T0900"_s << u"FREQ=MONTHLY;COUNT=3;BYDAY=TU,WE,TH;BYSETPOS=3"_s
                                    << QString() << u"20000101"_s << 3 << u"19970904,19971007,19971106"_s << 3;
    QTest::newRow("second to last weekday") << u"19970929T0900"_s
                                            << u"FREQ=MONTHLY;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-2"_s << QString()
                                            << u"19980401"_s << 7
                                            << u"19970929,19971030,19971127,19971230,19980129,19980226,19980330"_s << 7;
    QTest::newRow("wkst monday") << u"19970805T0900"_s << u"FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=MO"_s
                                 << QString() << u"20000101"_s << 4 << u"19970805,19970810,19970819,19970824"_s << 4;
    QTest::newRow("wkst sunday") << u"19970805T0900"_s << u"FREQ=WEEKLY;INTERVAL=2;COUNT=4;BYDAY=TU,SU;WKST=SU"_s
                                 << QString() << u"20000101"_s << 4 << u"19970805,19970817,19970819,19970831"_s << 4;
    QTest::newRow("no 30th of february") << u"20070115T0900"_s << u"FREQ=MONTHLY;BYMONTHDAY=15,30;COUNT=5"_s
                                         << QString() << u"20100101"_s << 5
                                         << u"20070115,20070130,20070215,20070315,20070330"_s << 5;
    QTest::newRow("leap day yearly") << u"20240229T0900"_s << u"FREQ=YEARLY;COUNT=3"_s << QString() << u"21000101"_s
                                     << 3 << u"20240229,20280229,20320229"_s << 3;
}

void TestCalDavRRule::rfcExamples()
{
    QFETCH(QString, dtstart);
    QFETCH(QString, rule);
    QFETCH(QString, until);
    QFETCH(QString, to);
    QFETCH(int, first);
    QFETCH(QString, expected);
    QFETCH(int, total);
    const auto r = parseRRule(rule);
    QVERIFY(r);
    const NaiveDateTime start = at(dtstart);
    std::optional<NaiveDateTime> u;
    if (!until.isEmpty())
        u = at(until);
    bool truncated = false;
    const QList<NaiveDateTime> list = expandRRule(*r, start, false, start, at(to), u, {}, &truncated);
    QVERIFY(!truncated);
    QCOMPARE(format(list.mid(0, first), false), expected);
    if (total >= 0)
        QCOMPARE(int(list.size()), total);
    for (const NaiveDateTime &t : list)
        QCOMPARE(t.time, start.time);
}

void TestCalDavRRule::hostileRulesEnd()
{
    // A rule no date ever matches: only DTSTART, and quickly.
    QElapsedTimer timer;
    timer.start();
    const auto never = parseRRule(u"FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=30"_s);
    QVERIFY(never);
    const NaiveDateTime start = at(u"20260101T1000"_s);
    bool truncated = false;
    QList<NaiveDateTime> list = expandRRule(*never, start, false, start, at(u"99991231"_s), std::nullopt, {}, &truncated);
    QCOMPARE(list.size(), 1);
    // Every second for a year: cut at the limit of results.
    const auto everySecond = parseRRule(u"FREQ=SECONDLY"_s);
    ExpandLimits limits;
    limits.maxOccurrences = 500;
    list = expandRRule(*everySecond, start, false, start, at(u"20270101"_s), std::nullopt, limits, &truncated);
    QCOMPARE(list.size(), 500);
    QVERIFY(truncated);
    QCOMPARE(list.last(), start.addSecs(499));
    // A huge interval goes beyond year 9999 and stops.
    const auto huge = parseRRule(u"FREQ=DAILY;INTERVAL=1000000"_s);
    list = expandRRule(*huge, start, false, start, at(u"99991231"_s), std::nullopt, {}, &truncated);
    QCOMPARE(list.size(), 3); // a million days is 2738 years: 2026, 4764, 7502 — then beyond 9999
    QVERIFY(timer.elapsed() < 5000);
}

void TestCalDavRRule::windowFarFromStart()
{
    // A daily stand-up since 1990, shown in November 2026: only the window is computed.
    const auto daily = parseRRule(u"FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"_s);
    const NaiveDateTime start = at(u"19900102T0930"_s);
    ExpandLimits limits;
    limits.maxPeriods = 50; // the weeks before November 2026 are skipped, not walked
    bool truncated = false;
    const QList<NaiveDateTime> list =
            expandRRule(*daily, start, false, at(u"20261116"_s), at(u"20261123"_s), std::nullopt, limits, &truncated);
    QVERIFY(!truncated);
    QCOMPARE(format(list, true), u"20261116T0930,20261117T0930,20261118T0930,20261119T0930,20261120T0930"_s);
}

void TestCalDavRRule::countCountsFromStart()
{
    // COUNT=5 of a weekly series: a window after the fifth week shows nothing.
    const auto rule = parseRRule(u"FREQ=WEEKLY;COUNT=5"_s);
    const NaiveDateTime start = at(u"20261102T1800"_s);
    QCOMPARE(expandRRule(*rule, start, false, at(u"20261201"_s), at(u"20270101"_s), std::nullopt).size(), 0);
    QCOMPARE(format(expandRRule(*rule, start, false, at(u"20261125"_s), at(u"20270101"_s), std::nullopt), true),
             u"20261130T1800"_s);
    // Hourly with BYHOUR at a fixed minute, and minutes of a day.
    const auto twenty = parseRRule(u"FREQ=DAILY;BYHOUR=9,10,11,12,13,14,15,16;BYMINUTE=0,20,40"_s);
    const NaiveDateTime nine = at(u"19970902T0900"_s);
    QCOMPARE(expandRRule(*twenty, nine, false, nine, at(u"19970903"_s), std::nullopt).size(), 24);
    const auto quarter = parseRRule(u"FREQ=MINUTELY;INTERVAL=15;COUNT=6"_s);
    QCOMPARE(format(expandRRule(*quarter, nine, false, nine, at(u"19970903"_s), std::nullopt), true),
             u"19970902T0900,19970902T0915,19970902T0930,19970902T0945,19970902T1000,19970902T1015"_s);
    const auto ninety = parseRRule(u"FREQ=MINUTELY;INTERVAL=90;COUNT=4"_s);
    QCOMPARE(format(expandRRule(*ninety, nine, false, nine, at(u"19970903"_s), std::nullopt), true),
             u"19970902T0900,19970902T1030,19970902T1200,19970902T1330"_s);
    const auto hours = parseRRule(u"FREQ=HOURLY;INTERVAL=3"_s);
    QCOMPARE(format(expandRRule(*hours, nine, false, nine, at(u"19970903"_s), at(u"19970902T1700"_s)), true),
             u"19970902T0900,19970902T1200,19970902T1500"_s);
}

void TestCalDavRRule::allDaySeries()
{
    // An all-day series ignores times; an hourly all-day rule is one a day.
    const auto rule = parseRRule(u"FREQ=HOURLY;BYHOUR=8,12"_s);
    const NaiveDateTime start{QDate(2026, 11, 20), QTime(0, 0)};
    const QList<NaiveDateTime> list = expandRRule(*rule, start, true, start, at(u"20261123"_s), std::nullopt);
    QCOMPARE(format(list, true), u"20261120T0000,20261121T0000,20261122T0000"_s);
    // The last day of every month (BYMONTHDAY=-1), from a 31st.
    const auto last = parseRRule(u"FREQ=MONTHLY;BYMONTHDAY=-1;COUNT=4"_s);
    const NaiveDateTime jan31{QDate(2026, 1, 31), QTime(0, 0)};
    QCOMPARE(format(expandRRule(*last, jan31, true, jan31, at(u"20270101"_s), std::nullopt), false),
             u"20260131,20260228,20260331,20260430"_s);
}

QTEST_GUILESS_MAIN(TestCalDavRRule)
#include "tst_caldav_rrule.moc"
