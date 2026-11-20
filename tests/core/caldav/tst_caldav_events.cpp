// Events of calendars Brulion shows: VEVENTs parsed within limits and expanded into a range's
// occurrences on the device's clock — time zones (UTC, IANA, a VTIMEZONE of the file's own, Windows
// names, floating), all-day and multi-day events, recurrence with EXDATE, RDATE, moved and
// cancelled instances, daylight-saving changes on both sides, Polish text, malformed and hostile
// data.
#include "caldav/events.h"

#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone Warsaw("Europe/Warsaw");

QByteArray calendar(const QString &body, const QString &extra = {})
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\nX-WR-CALNAME:Święta i spotkania\r\n"_s + extra + body
            + u"END:VCALENDAR\r\n"_s)
            .toUtf8();
}

QString vevent(const QString &uid, const QString &lines)
{
    return u"BEGIN:VEVENT\r\nUID:"_s + uid + u"\r\n"_s + lines + u"END:VEVENT\r\n"_s;
}

QString stamp(const NaiveDateTime &t)
{
    return t.date.toString(u"yyyy-MM-dd"_s) + u' ' + t.time.toString(u"HH:mm"_s);
}

QStringList starts(const CalendarData &data, QDate from, QDate to)
{
    QStringList out;
    for (const Occurrence &o : occurrences(data, from, to, Warsaw))
        out.append(stamp(o.start) + u' ' + data.events.at(o.event).summary);
    return out;
}

const QString CustomZone = u"BEGIN:VTIMEZONE\r\nTZID:Customized Time Zone\r\n"
                           u"BEGIN:STANDARD\r\nDTSTART:16010101T030000\r\nTZOFFSETFROM:+0200\r\nTZOFFSETTO:+0100\r\n"
                           u"RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=10\r\nEND:STANDARD\r\n"
                           u"BEGIN:DAYLIGHT\r\nDTSTART:16010101T020000\r\nTZOFFSETFROM:+0100\r\nTZOFFSETTO:+0200\r\n"
                           u"RRULE:FREQ=YEARLY;BYDAY=-1SU;BYMONTH=3\r\nEND:DAYLIGHT\r\nEND:VTIMEZONE\r\n"
                           u"BEGIN:VTIMEZONE\r\nTZID:Brulion/Indie\r\n"
                           u"BEGIN:STANDARD\r\nDTSTART:19700101T000000\r\nTZOFFSETFROM:+0530\r\nTZOFFSETTO:+0530\r\n"
                           u"END:STANDARD\r\nEND:VTIMEZONE\r\n"_s;

} // namespace

class TestCalDavEvents : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void zones_data();
    void zones();
    void vtimezoneGapAndRepeat();
    void allDayAndMultiDay();
    void recurrenceWithExceptions();
    void rdatesAndUntil();
    void daylightSavingOnBothSides();
    void polishText();
    void malformed();
    void limits();
    void colors_data();
    void colors();
};

void TestCalDavEvents::initTestCase()
{
    if (!Warsaw.isValid())
        QSKIP("no time zone database");
}

void TestCalDavEvents::zones_data()
{
    QTest::addColumn<QString>("start");
    QTest::addColumn<QString>("expected");
    QTest::newRow("utc winter") << u"DTSTART:20261120T170000Z"_s << u"2026-11-20 18:00"_s;
    QTest::newRow("utc summer") << u"DTSTART:20260715T160000Z"_s << u"2026-07-15 18:00"_s;
    QTest::newRow("new york") << u"DTSTART;TZID=America/New_York:20261120T120000"_s << u"2026-11-20 18:00"_s;
    QTest::newRow("mozilla prefix") << u"DTSTART;TZID=/mozilla.org/20050126_1/Europe/London:20261120T120000"_s
                                    << u"2026-11-20 13:00"_s;
    QTest::newRow("windows name") << u"DTSTART;TZID=\"Central European Standard Time\":20261120T100000"_s
                                  << u"2026-11-20 10:00"_s;
    QTest::newRow("own vtimezone winter") << u"DTSTART;TZID=\"Customized Time Zone\":20261120T100000"_s
                                          << u"2026-11-20 10:00"_s;
    QTest::newRow("own vtimezone summer") << u"DTSTART;TZID=\"Customized Time Zone\":20260715T100000"_s
                                          << u"2026-07-15 10:00"_s;
    QTest::newRow("own fixed offset") << u"DTSTART;TZID=Brulion/Indie:20261120T120000"_s << u"2026-11-20 07:30"_s;
    QTest::newRow("floating") << u"DTSTART:20261120T100000"_s << u"2026-11-20 10:00"_s;
    QTest::newRow("unknown zone") << u"DTSTART;TZID=Nigdzie/Nic:20261120T100000"_s << u"2026-11-20 10:00"_s;
    QTest::newRow("date across midnight") << u"DTSTART;TZID=Pacific/Auckland:20261121T080000"_s
                                          << u"2026-11-20 20:00"_s;
}

void TestCalDavEvents::zones()
{
    QFETCH(QString, start);
    QFETCH(QString, expected);
    const CalendarData data = parseCalendarData(calendar(vevent(u"a"_s, start + u"\r\nSUMMARY:Spotkanie\r\n"_s), CustomZone));
    QCOMPARE(data.events.size(), 1);
    const QList<Occurrence> list = occurrences(data, QDate(2026, 7, 1), QDate(2026, 12, 31), Warsaw);
    QCOMPARE(list.size(), 1);
    QCOMPARE(stamp(list.first().start), expected);
    QVERIFY(!list.first().allDay);
}

void TestCalDavEvents::vtimezoneGapAndRepeat()
{
    const CalendarData data = parseCalendarData(calendar(QString(), CustomZone));
    const ZoneDefinition *zone = data.zones.definition(u"Customized Time Zone"_s);
    QVERIFY(zone);
    // 02:30 on the last Sunday of March does not exist: it is taken in the offset before (+1), so
    // it lands at 03:30 summer time.
    const auto gap = zone->toUtc({QDate(2026, 3, 29), QTime(2, 30)});
    QVERIFY(gap);
    QCOMPARE(QDateTime::fromMSecsSinceEpoch(*gap, QTimeZone::UTC), QDateTime(QDate(2026, 3, 29), QTime(1, 30), QTimeZone::UTC));
    // 02:30 on the last Sunday of October happens twice: the first one (summer time) is taken.
    const auto repeated = zone->toUtc({QDate(2026, 10, 25), QTime(2, 30)});
    QVERIFY(repeated);
    QCOMPARE(QDateTime::fromMSecsSinceEpoch(*repeated, QTimeZone::UTC), QDateTime(QDate(2026, 10, 25), QTime(0, 30), QTimeZone::UTC));
    // Offsets on both sides of the changes.
    QCOMPARE(zone->offsetAtUtc(QDateTime(QDate(2026, 3, 29), QTime(0, 59), QTimeZone::UTC).toMSecsSinceEpoch()), 3600);
    QCOMPARE(zone->offsetAtUtc(QDateTime(QDate(2026, 3, 29), QTime(1, 0), QTimeZone::UTC).toMSecsSinceEpoch()), 7200);
    QCOMPARE(zone->offsetAtUtc(QDateTime(QDate(2026, 10, 25), QTime(1, 0), QTimeZone::UTC).toMSecsSinceEpoch()), 3600);
    QCOMPARE(parseUtcOffset(u"-0330"), -12600);
    QCOMPARE(parseUtcOffset(u"+013045"), 5445);
    QVERIFY(!parseUtcOffset(u"0100"));
    QVERIFY(!parseUtcOffset(u"+2500"));
}

void TestCalDavEvents::allDayAndMultiDay()
{
    const CalendarData data = parseCalendarData(calendar(
            vevent(u"urlop"_s, u"DTSTART;VALUE=DATE:20261120\r\nDTEND;VALUE=DATE:20261123\r\nSUMMARY:Urlop w górach\r\n"_s)
            + vevent(u"jeden"_s, u"DTSTART;VALUE=DATE:20261125\r\nSUMMARY:Imieniny\r\n"_s)
            + vevent(u"noc"_s, u"DTSTART:20261121T220000\r\nDTEND:20261122T020000\r\nSUMMARY:Nocna zmiana\r\n"_s)
            + vevent(u"dwa"_s, u"DTSTART;VALUE=DATE:20261127\r\nDURATION:P2D\r\nSUMMARY:Konferencja\r\n"_s)));
    QCOMPARE(data.events.size(), 4);
    QVERIFY(data.events.first().allDay());
    // Shown on every day it covers: a window of the third day still has it.
    QList<Occurrence> list = occurrences(data, QDate(2026, 11, 22), QDate(2026, 11, 22), Warsaw);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).lastDay(), QDate(2026, 11, 22));
    QVERIFY(list.at(0).allDay);
    QCOMPARE(data.events.at(list.at(1).event).summary, u"Nocna zmiana"_s);
    QCOMPARE(list.at(1).lastDay(), QDate(2026, 11, 22));
    // The day after the exclusive end has none of them.
    list = occurrences(data, QDate(2026, 11, 23), QDate(2026, 11, 24), Warsaw);
    QVERIFY(list.isEmpty());
    // A date without an end lasts one day; a duration of days, as many.
    list = occurrences(data, QDate(2026, 11, 25), QDate(2026, 11, 30), Warsaw);
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.at(0).end.date, QDate(2026, 11, 26));
    QCOMPARE(list.at(1).lastDay(), QDate(2026, 11, 28));
}

void TestCalDavEvents::recurrenceWithExceptions()
{
    const QString series = vevent(u"stand-up"_s, u"DTSTART;TZID=Europe/Warsaw:20261102T090000\r\n"
                                                u"DTEND;TZID=Europe/Warsaw:20261102T091500\r\n"
                                                u"RRULE:FREQ=WEEKLY;COUNT=5\r\n"
                                                u"EXDATE;TZID=Europe/Warsaw:20261109T090000\r\n"
                                                u"SUMMARY:Stand-up\r\n"_s)
                           + vevent(u"stand-up"_s, u"RECURRENCE-ID;TZID=Europe/Warsaw:20261116T090000\r\n"
                                                  u"DTSTART;TZID=Europe/Warsaw:20261116T140000\r\n"
                                                  u"DTEND;TZID=Europe/Warsaw:20261116T143000\r\n"
                                                  u"SUMMARY:Stand-up (przeniesiony)\r\n"_s)
                           + vevent(u"stand-up"_s, u"RECURRENCE-ID:20261123T080000Z\r\n"
                                                  u"DTSTART:20261123T080000Z\r\nSTATUS:CANCELLED\r\nSUMMARY:Stand-up\r\n"_s);
    const CalendarData data = parseCalendarData(calendar(series));
    QCOMPARE(data.events.size(), 3);
    QCOMPARE(starts(data, QDate(2026, 11, 1), QDate(2026, 12, 31)),
             (QStringList{u"2026-11-02 09:00 Stand-up"_s, u"2026-11-16 14:00 Stand-up (przeniesiony)"_s,
                          u"2026-11-30 09:00 Stand-up"_s}));
    const QList<Occurrence> list = occurrences(data, QDate(2026, 11, 1), QDate(2026, 11, 3), Warsaw);
    QCOMPARE(list.size(), 1);
    QVERIFY(list.first().recurring);
    QVERIFY(list.first().instance);
    QCOMPARE(list.first().end.time, QTime(9, 15));
    // A cancelled series shows nothing.
    const CalendarData cancelled = parseCalendarData(
            calendar(vevent(u"x"_s, u"DTSTART:20261102T090000Z\r\nRRULE:FREQ=DAILY\r\nSTATUS:CANCELLED\r\nSUMMARY:Nie\r\n"_s)));
    QVERIFY(occurrences(cancelled, QDate(2026, 11, 1), QDate(2026, 11, 30), Warsaw).isEmpty());
    // An override without its series (an invitation to one meeting of a series) shows alone.
    const CalendarData orphan = parseCalendarData(calendar(vevent(
            u"o"_s, u"RECURRENCE-ID:20261105T100000Z\r\nDTSTART:20261105T110000Z\r\nSUMMARY:Jedno spotkanie\r\n"_s)));
    QCOMPARE(starts(orphan, QDate(2026, 11, 1), QDate(2026, 11, 30)), QStringList{u"2026-11-05 12:00 Jedno spotkanie"_s});
}

void TestCalDavEvents::rdatesAndUntil()
{
    const CalendarData data = parseCalendarData(calendar(vevent(
            u"r"_s, u"DTSTART;TZID=Europe/Warsaw:20261103T180000\r\nRRULE:FREQ=WEEKLY;UNTIL=20261117T170000Z\r\n"
                    u"RDATE;TZID=Europe/Warsaw:20261121T120000,20261128T120000\r\n"
                    u"RDATE;VALUE=PERIOD:20261205T100000Z/PT1H\r\nSUMMARY:Chór\r\n"_s)));
    QCOMPARE(starts(data, QDate(2026, 11, 1), QDate(2026, 12, 31)),
             (QStringList{u"2026-11-03 18:00 Chór"_s, u"2026-11-10 18:00 Chór"_s, u"2026-11-17 18:00 Chór"_s,
                          u"2026-11-21 12:00 Chór"_s, u"2026-11-28 12:00 Chór"_s, u"2026-12-05 11:00 Chór"_s}));
    // An unparseable RRULE: DTSTART only.
    const CalendarData broken = parseCalendarData(
            calendar(vevent(u"b"_s, u"DTSTART:20261103T180000Z\r\nRRULE:FREQ=CO-TYDZIEŃ\r\nSUMMARY:Zepsute\r\n"_s)));
    QCOMPARE(broken.events.first().rruleText, u"FREQ=CO-TYDZIEŃ"_s);
    QCOMPARE(starts(broken, QDate(2026, 11, 1), QDate(2026, 12, 31)).size(), 1);
    // A series from 1995 asked for 2026 only computes the window.
    const CalendarData old = parseCalendarData(
            calendar(vevent(u"o"_s, u"DTSTART;VALUE=DATE:19950321\r\nRRULE:FREQ=YEARLY\r\nSUMMARY:Urodziny Ani\r\n"_s)));
    QCOMPARE(starts(old, QDate(2026, 1, 1), QDate(2026, 12, 31)), QStringList{u"2026-03-21 00:00 Urodziny Ani"_s});
}

void TestCalDavEvents::daylightSavingOnBothSides()
{
    // Europe changes on Oct 25, America on Nov 1: a New York meeting at noon is 18:00, then 17:00
    // for a week, then 18:00 again in Warsaw.
    const CalendarData ny = parseCalendarData(calendar(
            vevent(u"ny"_s, u"DTSTART;TZID=America/New_York:20261014T120000\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\nSUMMARY:NY\r\n"_s)));
    QCOMPARE(starts(ny, QDate(2026, 10, 1), QDate(2026, 11, 30)),
             (QStringList{u"2026-10-14 18:00 NY"_s, u"2026-10-21 18:00 NY"_s, u"2026-10-28 17:00 NY"_s,
                          u"2026-11-04 18:00 NY"_s}));
    // A Warsaw series keeps its wall-clock time across the change.
    const CalendarData local = parseCalendarData(calendar(vevent(
            u"w"_s, u"DTSTART;TZID=Europe/Warsaw:20261019T090000\r\nRRULE:FREQ=WEEKLY;COUNT=2\r\nSUMMARY:Basen\r\n"_s)));
    QCOMPARE(starts(local, QDate(2026, 10, 1), QDate(2026, 11, 30)),
             (QStringList{u"2026-10-19 09:00 Basen"_s, u"2026-10-26 09:00 Basen"_s}));
    // The file's own VTIMEZONE does the same without the system's database.
    const CalendarData own = parseCalendarData(calendar(
            vevent(u"c"_s, u"DTSTART;TZID=\"Customized Time Zone\":20261019T090000\r\nRRULE:FREQ=WEEKLY;COUNT=2\r\nSUMMARY:Basen\r\n"_s),
            CustomZone));
    QCOMPARE(starts(own, QDate(2026, 10, 1), QDate(2026, 11, 30)),
             (QStringList{u"2026-10-19 09:00 Basen"_s, u"2026-10-26 09:00 Basen"_s}));
}

void TestCalDavEvents::polishText()
{
    const CalendarData data = parseCalendarData(calendar(vevent(
            u"p"_s, u"DTSTART:20261120T170000Z\r\nSUMMARY:Spotkanie z Łukaszem\\, pokój 3\r\n"
                    u"DESCRIPTION:Zabrać:\\n- żółtą teczkę\\n- ćwiczenia\r\nLOCATION:Kraków\\; ul. Długa 5\r\n"
                    u"CATEGORIES:Praca,Ważne\r\nCOLOR:tomato\r\n"_s)));
    QCOMPARE(data.name, u"Święta i spotkania"_s);
    const Event &e = data.events.first();
    QCOMPARE(e.summary, u"Spotkanie z Łukaszem, pokój 3"_s);
    QCOMPARE(e.description, u"Zabrać:\n- żółtą teczkę\n- ćwiczenia"_s);
    QCOMPARE(e.location, u"Kraków; ul. Długa 5"_s);
    QCOMPARE(e.categories, (QStringList{u"Praca"_s, u"Ważne"_s}));
    QCOMPARE(e.color, u"#ff6347"_s);
}

void TestCalDavEvents::malformed()
{
    // Garbage lines, an event without a UID or a start, an unterminated event.
    const QByteArray data = "BEGIN:VCALENDAR\r\nto nie jest linia\r\nBEGIN:VEVENT\r\nSUMMARY:Bez UID\r\n"
                            "DTSTART:20261120T100000Z\r\nEND:VEVENT\r\nBEGIN:VEVENT\r\nUID:x\r\nSUMMARY:Bez daty\r\n"
                            "END:VEVENT\r\nBEGIN:VEVENT\r\nUID:y\r\nDTSTART:jutro\r\nEND:VEVENT\r\n"
                            "BEGIN:VEVENT\r\nUID:z\r\nDTSTART:20261121T100000Z\r\nSUMMARY:Niedokończone\r\n";
    const CalendarData parsed = parseCalendarData(data);
    QVERIFY(parsed.malformed);
    QCOMPARE(parsed.skipped, 3);
    QCOMPARE(parsed.events.size(), 1);
    QCOMPARE(parsed.events.first().summary, u"Niedokończone"_s);
    // Not a calendar at all: nothing.
    QVERIFY(parseCalendarData("<html><body>Zaloguj się</body></html>").events.isEmpty());
    QVERIFY(parseCalendarData(QByteArray()).events.isEmpty());
    // Binary garbage does not crash.
    QByteArray noise(4096, '\0');
    for (int i = 0; i < noise.size(); ++i)
        noise[i] = char((i * 7919) % 256);
    QVERIFY(parseCalendarData(noise).events.isEmpty());
    // An invalid range gives nothing.
    QVERIFY(occurrences(parsed, QDate(2026, 12, 1), QDate(2026, 11, 1), Warsaw).isEmpty());
}

void TestCalDavEvents::limits()
{
    // More events than allowed: cut, and said.
    QString many;
    for (int i = 0; i < 300; ++i)
        many += vevent(u"e%1"_s.arg(i), u"DTSTART:20261120T100000Z\r\nSUMMARY:Zdarzenie %1\r\n"_s.arg(i));
    ParseLimits limits;
    limits.maxEvents = 100;
    CalendarData data;
    appendCalendarData(data, calendar(many), {}, limits);
    QCOMPARE(data.events.size(), 100);
    QVERIFY(data.truncated);
    // Larger than allowed: refused before parsing.
    limits.maxBytes = 1000;
    CalendarData big;
    appendCalendarData(big, calendar(many), {}, limits);
    QVERIFY(big.events.isEmpty());
    QVERIFY(big.truncated);
    // A series every minute: at most maxOccurrences.
    const CalendarData minutes = parseCalendarData(
            calendar(vevent(u"m"_s, u"DTSTART:20261101T000000Z\r\nRRULE:FREQ=MINUTELY\r\nSUMMARY:Co minutę\r\n"_s)));
    ExpandLimits expand;
    expand.maxOccurrences = 300;
    bool truncated = false;
    QCOMPARE(occurrences(minutes, QDate(2026, 11, 1), QDate(2026, 11, 30), Warsaw, expand, &truncated).size(), 300);
    QVERIFY(truncated);
    // An event lasting fifty years counts as one year.
    const CalendarData endless = parseCalendarData(
            calendar(vevent(u"l"_s, u"DTSTART;VALUE=DATE:20260101\r\nDTEND;VALUE=DATE:20760101\r\nSUMMARY:Długo\r\n"_s)));
    QCOMPARE(occurrences(endless, QDate(2027, 6, 1), QDate(2027, 6, 2), Warsaw).size(), 0);
    QCOMPARE(occurrences(endless, QDate(2026, 6, 1), QDate(2026, 6, 2), Warsaw).size(), 1);
    // A summary of a megabyte is cut.
    const QString huge(1'000'000, u'ż');
    const CalendarData long_ = parseCalendarData(calendar(vevent(u"h"_s, u"DTSTART:20261120T100000Z\r\nSUMMARY:"_s + huge + u"\r\n"_s)));
    QCOMPARE(long_.events.first().summary.size(), 20'000);
}

void TestCalDavEvents::colors_data()
{
    QTest::addColumn<QString>("value");
    QTest::addColumn<QString>("expected");
    QTest::newRow("hex") << u"#1A2B3C"_s << u"#1a2b3c"_s;
    QTest::newRow("apple alpha") << u"#FF2968FF"_s << u"#ff2968"_s;
    QTest::newRow("short") << u"#f80"_s << u"#ff8800"_s;
    QTest::newRow("css name") << u"Turquoise"_s << u"#40e0d0"_s;
    QTest::newRow("garbage") << u"zielony"_s << QString();
    QTest::newRow("empty") << QString() << QString();
}

void TestCalDavEvents::colors()
{
    QFETCH(QString, value);
    QFETCH(QString, expected);
    QCOMPARE(normalizedColor(value), expected);
}

QTEST_GUILESS_MAIN(TestCalDavEvents)
#include "tst_caldav_events.moc"
