// The VALARMs of events as notifications (caldav/eventalarms.h): triggers relative to the start and
// the end, absolute ones, DISPLAY and AUDIO, series (every occurrence, exceptions, EXDATE), exact
// time across daylight-saving changes, limits, malformed alarms, a calendar's switch, Polish text.
#include "caldav/eventalarms.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone Warsaw("Europe/Warsaw");

QDateTime local(int y, int m, int d, int h = 0, int min = 0)
{
    return QDateTime(QDate(y, m, d), QTime(h, min));
}

QByteArray calendar(const QString &body)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"_s + body + u"END:VCALENDAR\r\n"_s).toUtf8();
}

QString vevent(const QString &uid, const QString &lines)
{
    return u"BEGIN:VEVENT\r\nUID:"_s + uid + u"\r\n"_s + lines + u"END:VEVENT\r\n"_s;
}

QString alarm(const QString &trigger, const QString &action = u"DISPLAY"_s)
{
    return u"BEGIN:VALARM\r\nACTION:"_s + action + u"\r\n"_s + trigger + u"\r\nEND:VALARM\r\n"_s;
}

/// "2026-11-20 08:45 Zebranie" (+ "!" for an alarm) of every notification.
QStringList keys(const QList<ScheduledNotification> &list)
{
    QStringList out;
    for (const ScheduledNotification &n : list)
        out.append(n.at.toString(u"yyyy-MM-dd HH:mm "_s) + n.title + (n.alarm ? u"!"_s : QString()));
    return out;
}

QList<ScheduledNotification> ring(const QString &body, const QDateTime &from, const QDateTime &to,
                                   dates::UiLocale locale = dates::UiLocale::Polish, const EventAlarmLimits &limits = {})
{
    return eventNotifications(parseCalendarData(calendar(body)), u"c-1"_s, u"Praca"_s, from, to, Warsaw, locale, limits);
}

} // namespace

class TestCalDavAlarms : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void triggers_data();
    void triggers();
    void series();
    void exactTimeAcrossDaylightSaving();
    void notificationFields();
    void limitsAndBroken();
    void storeAndSwitch();
    void subscriptionsRingWhenAsked();
    void settingsOfAnOlderFile();
};

void TestCalDavAlarms::initTestCase()
{
    if (!Warsaw.isValid())
        QSKIP("no time zone database");
}

void TestCalDavAlarms::triggers_data()
{
    QTest::addColumn<QString>("lines");
    QTest::addColumn<QStringList>("expected");

    const QString when = u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nDTEND;TZID=Europe/Warsaw:20261120T103000\r\nSUMMARY:Zażółć gęślą jaźń\r\n"_s;
    QTest::newRow("before the start") << when + alarm(u"TRIGGER:-PT15M"_s)
                                      << QStringList{u"2026-11-20 08:45 Zażółć gęślą jaźń"_s};
    QTest::newRow("at the start") << when + alarm(u"TRIGGER:PT0S"_s) << QStringList{u"2026-11-20 09:00 Zażółć gęślą jaźń"_s};
    QTest::newRow("after the start") << when + alarm(u"TRIGGER:PT5M"_s) << QStringList{u"2026-11-20 09:05 Zażółć gęślą jaźń"_s};
    QTest::newRow("related to the end") << when + alarm(u"TRIGGER;RELATED=END:-PT10M"_s)
                                        << QStringList{u"2026-11-20 10:20 Zażółć gęślą jaźń"_s};
    QTest::newRow("a day and hours before") << when + alarm(u"TRIGGER:-P1DT2H"_s)
                                            << QStringList{u"2026-11-19 07:00 Zażółć gęślą jaźń"_s};
    QTest::newRow("weeks before") << when + alarm(u"TRIGGER:-P1W"_s) << QStringList{u"2026-11-13 09:00 Zażółć gęślą jaźń"_s};
    QTest::newRow("audio is loud") << when + alarm(u"TRIGGER:-PT30M"_s, u"AUDIO"_s)
                                   << QStringList{u"2026-11-20 08:30 Zażółć gęślą jaźń!"_s};
    QTest::newRow("absolute in UTC") << when + alarm(u"TRIGGER;VALUE=DATE-TIME:20261120T070000Z"_s)
                                     << QStringList{u"2026-11-20 08:00 Zażółć gęślą jaźń"_s};
    QTest::newRow("absolute floating") << when + alarm(u"TRIGGER;VALUE=DATE-TIME:20261119T180000"_s)
                                       << QStringList{u"2026-11-19 18:00 Zażółć gęślą jaźń"_s};
    QTest::newRow("two alarms, in time order")
            << when + alarm(u"TRIGGER:-PT5M"_s) + alarm(u"TRIGGER:-PT1H"_s, u"AUDIO"_s)
            << QStringList{u"2026-11-20 08:00 Zażółć gęślą jaźń!"_s, u"2026-11-20 08:55 Zażółć gęślą jaźń"_s};
    QTest::newRow("EMAIL and PROCEDURE ring nothing")
            << when + alarm(u"TRIGGER:-PT5M"_s, u"EMAIL"_s) + alarm(u"TRIGGER:-PT6M"_s, u"PROCEDURE"_s) << QStringList{};
    QTest::newRow("no ACTION is a reminder")
            << when + u"BEGIN:VALARM\r\nTRIGGER:-PT5M\r\nEND:VALARM\r\n"_s << QStringList{u"2026-11-20 08:55 Zażółć gęślą jaźń"_s};
    QTest::newRow("no alarm") << when << QStringList{};
    QTest::newRow("malformed trigger") << when + alarm(u"TRIGGER:-PT1X5M"_s) + alarm(u"TRIGGER:"_s) << QStringList{};
    QTest::newRow("a trigger too far away") << when + alarm(u"TRIGGER:-P400D"_s) << QStringList{};
    QTest::newRow("all-day: the evening before")
            << u"DTSTART;VALUE=DATE:20261121\r\nSUMMARY:Urodziny\r\n"_s + alarm(u"TRIGGER:-PT6H"_s)
            << QStringList{u"2026-11-20 18:00 Urodziny"_s};
    QTest::newRow("all-day: at nine on the day") << u"DTSTART;VALUE=DATE:20261121\r\nSUMMARY:Urodziny\r\n"_s + alarm(u"TRIGGER:PT9H"_s)
                                                 << QStringList{u"2026-11-21 09:00 Urodziny"_s};
    QTest::newRow("a UTC event, shown in Warsaw")
            << u"DTSTART:20261120T080000Z\r\nSUMMARY:UTC\r\n"_s + alarm(u"TRIGGER:-PT15M"_s) << QStringList{u"2026-11-20 08:45 UTC"_s};
    QTest::newRow("a floating event is on the device's clock")
            << u"DTSTART:20261120T090000\r\nSUMMARY:Pływające\r\n"_s + alarm(u"TRIGGER:-PT15M"_s)
            << QStringList{u"2026-11-20 08:45 Pływające"_s};
    QTest::newRow("a cancelled event rings nothing")
            << when + u"STATUS:CANCELLED\r\n"_s + alarm(u"TRIGGER:-PT15M"_s) << QStringList{};
    QTest::newRow("no title") << u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\n"_s + alarm(u"TRIGGER:-PT15M"_s)
                              << QStringList{u"2026-11-20 08:45 (bez tytułu)"_s};
}

void TestCalDavAlarms::triggers()
{
    QFETCH(QString, lines);
    QFETCH(QStringList, expected);
    const QList<ScheduledNotification> list =
            ring(vevent(u"u"_s, lines), local(2026, 11, 1), local(2026, 12, 31));
    QCOMPARE(keys(list), expected);
    for (const ScheduledNotification &n : list)
        QVERIFY(!n.id.isEmpty());
}

void TestCalDavAlarms::series()
{
    const QString weekly = vevent(u"s"_s, u"DTSTART;TZID=Europe/Warsaw:20261102T090000\r\nDTEND;TZID=Europe/Warsaw:20261102T094500\r\n"
                                          u"RRULE:FREQ=WEEKLY;COUNT=5\r\nEXDATE;TZID=Europe/Warsaw:20261116T090000\r\n"
                                          u"SUMMARY:Stand-up\r\n"_s
                                          + alarm(u"TRIGGER:-PT10M"_s)
                                          + alarm(u"TRIGGER;VALUE=DATE-TIME:20261101T120000Z"_s)); // once: ignored here
    QCOMPARE(keys(ring(weekly, local(2026, 11, 1), local(2026, 12, 31))),
             (QStringList{u"2026-11-02 08:50 Stand-up"_s, u"2026-11-09 08:50 Stand-up"_s, u"2026-11-23 08:50 Stand-up"_s,
                          u"2026-11-30 08:50 Stand-up"_s}));
    // An exception has its own alarms (and a moved time).
    const QString moved = vevent(u"s"_s, u"RECURRENCE-ID;TZID=Europe/Warsaw:20261123T090000\r\nDTSTART;TZID=Europe/Warsaw:20261123T140000\r\n"
                                         u"SUMMARY:Stand-up (po południu)\r\n"_s
                                         + alarm(u"TRIGGER:-PT1H"_s, u"AUDIO"_s));
    QCOMPARE(keys(ring(weekly + moved, local(2026, 11, 20), local(2026, 12, 31))),
             (QStringList{u"2026-11-23 13:00 Stand-up (po południu)!"_s, u"2026-11-30 08:50 Stand-up"_s}));
    // Every occurrence has an id of its own; the same look gives the same ids.
    const QList<ScheduledNotification> a = ring(weekly, local(2026, 11, 1), local(2026, 12, 31));
    const QList<ScheduledNotification> b = ring(weekly, local(2026, 11, 1), local(2026, 12, 31));
    QSet<QString> ids;
    for (const ScheduledNotification &n : a)
        ids.insert(n.id);
    QCOMPARE(ids.size(), a.size());
    QCOMPARE(a, b);
    // A THISANDFUTURE exception: the later occurrences ring by its alarms, at its time.
    const QString range = vevent(u"s"_s, u"RECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Europe/Warsaw:20261123T090000\r\n"
                                         u"DTSTART;TZID=Europe/Warsaw:20261123T100000\r\nSUMMARY:Stand-up (później)\r\n"_s
                                         + alarm(u"TRIGGER:-PT30M"_s));
    QCOMPARE(keys(ring(weekly + range, local(2026, 11, 20), local(2026, 12, 31))),
             (QStringList{u"2026-11-23 09:30 Stand-up (później)"_s, u"2026-11-30 09:30 Stand-up (później)"_s}));
}

void TestCalDavAlarms::exactTimeAcrossDaylightSaving()
{
    // 25 October 2026: the clocks go back at 03:00 (an hour is repeated). Eight hours before 06:00
    // that morning is 23:00 the evening before (not 22:00: nine hours of the wall clock).
    const QString night = vevent(u"n"_s, u"DTSTART;TZID=Europe/Warsaw:20261025T060000\r\nSUMMARY:Lot\r\n"_s + alarm(u"TRIGGER:-PT8H"_s));
    QCOMPARE(keys(ring(night, local(2026, 10, 1), local(2026, 10, 31))), QStringList{u"2026-10-24 23:00 Lot"_s});
    // A trigger in days is on the wall clock: a day before 09:00 is 09:00.
    const QString days = vevent(u"d"_s, u"DTSTART;TZID=Europe/Warsaw:20261026T090000\r\nSUMMARY:Dzień\r\n"_s + alarm(u"TRIGGER:-P1D"_s));
    QCOMPARE(keys(ring(days, local(2026, 10, 1), local(2026, 10, 31))), QStringList{u"2026-10-25 09:00 Dzień"_s});
    // A series over the change keeps its wall-clock time.
    const QString series = vevent(u"w"_s, u"DTSTART;TZID=Europe/Warsaw:20261017T200000\r\nRRULE:FREQ=WEEKLY;COUNT=3\r\nSUMMARY:Chór\r\n"_s
                                          + alarm(u"TRIGGER:-PT30M"_s));
    QCOMPARE(keys(ring(series, local(2026, 10, 1), local(2026, 11, 1))),
             (QStringList{u"2026-10-17 19:30 Chór"_s, u"2026-10-24 19:30 Chór"_s, u"2026-10-31 19:30 Chór"_s}));
}

void TestCalDavAlarms::notificationFields()
{
    const QString event = vevent(u"x"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nSUMMARY:Spotkanie z Łucją\r\n"
                                         u"LOCATION:Sala Żółta\r\n"_s
                                         + alarm(u"TRIGGER:-PT15M"_s));
    const QList<ScheduledNotification> list = ring(event, local(2026, 11, 20), local(2026, 11, 20, 23, 59));
    QCOMPARE(list.size(), 1);
    const ScheduledNotification &n = list.first();
    QCOMPARE(n.title, u"Spotkanie z Łucją"_s);
    QVERIFY2(n.body.startsWith(u"Praca · "_s), qPrintable(n.body));
    QVERIFY2(n.body.contains(u"09:00"_s) && n.body.endsWith(u" · Sala Żółta"_s), qPrintable(n.body));
    QCOMPARE(n.path, u"calendar:2026-11-20"_s);
    QCOMPARE(n.taskLine, -1);
    QVERIFY(!n.alarm);
    QCOMPARE(n.at, local(2026, 11, 20, 8, 45));
    // English.
    const QList<ScheduledNotification> en =
            ring(vevent(u"y"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\n"_s + alarm(u"TRIGGER:-PT15M"_s)), local(2026, 11, 20),
                 local(2026, 11, 20, 23, 59), dates::UiLocale::English);
    QCOMPARE(en.first().title, u"(no title)"_s);
    // A long title is cut.
    const QList<ScheduledNotification> longTitle =
            ring(vevent(u"z"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nSUMMARY:"_s + QString(300, u'ż') + u"\r\n"_s
                                        + alarm(u"TRIGGER:-PT15M"_s)),
                 local(2026, 11, 20), local(2026, 11, 20, 23, 59));
    QCOMPARE(longTitle.first().title.size(), int(ScheduledNotification::MaxTitleLength));
}

void TestCalDavAlarms::limitsAndBroken()
{
    // The window is inclusive at both ends.
    const QString event = vevent(u"w"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nSUMMARY:Okno\r\n"_s + alarm(u"TRIGGER:-PT15M"_s));
    QCOMPARE(ring(event, local(2026, 11, 20, 8, 45), local(2026, 11, 20, 8, 45)).size(), 1);
    QCOMPARE(ring(event, local(2026, 11, 20, 8, 46), local(2026, 11, 21)).size(), 0);
    QCOMPARE(ring(event, local(2026, 11, 1), local(2026, 11, 20, 8, 44)).size(), 0);
    QCOMPARE(ring(event, local(2026, 11, 21), local(2026, 11, 1)).size(), 0); // backwards
    // At most a few alarms of one event, at most so many in all.
    QString many = u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nSUMMARY:Dużo\r\n"_s;
    for (int i = 1; i <= 9; ++i)
        many += alarm(u"TRIGGER:-PT%1M"_s.arg(i));
    EventAlarmLimits limits;
    limits.maxPerEvent = 3;
    QCOMPARE(ring(vevent(u"m"_s, many), local(2026, 11, 1), local(2026, 12, 1), dates::UiLocale::Polish, limits).size(), 3);
    const QString daily = vevent(u"d"_s, u"DTSTART;TZID=Europe/Warsaw:20261101T090000\r\nRRULE:FREQ=DAILY\r\nSUMMARY:Codziennie\r\n"_s
                                         + alarm(u"TRIGGER:-PT5M"_s));
    limits.maxTotal = 10;
    QCOMPARE(ring(daily, local(2026, 11, 1), local(2027, 11, 1), dates::UiLocale::Polish, limits).size(), 10);
    // Broken data: an unparseable RRULE, a series without alarms, garbage — nothing, no crash.
    QVERIFY(ring(vevent(u"b"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nRRULE:FREQ=NIGDY\r\nSUMMARY:Zły\r\n"_s
                                + alarm(u"TRIGGER:-PT5M"_s)),
                 local(2026, 11, 1), local(2026, 12, 1))
                .size()
        <= 1);
    QVERIFY(eventNotifications(parseCalendarData("not a calendar"), u"c"_s, u"C"_s, local(2026, 11, 1), local(2026, 12, 1), Warsaw)
                    .isEmpty());
    QVERIFY(eventNotifications(CalendarData(), u"c"_s, u"C"_s, QDateTime(), QDateTime(), Warsaw).isEmpty());
}

void TestCalDavAlarms::storeAndSwitch()
{
    CalendarStore store{QString()};
    CalendarSettings settings;
    Account account;
    account.id = u"a-1"_s;
    settings.accounts.append(account);
    for (const QString &id : {u"c-1"_s, u"c-2"_s, u"c-3"_s}) {
        CalendarEntry c;
        c.id = id;
        c.account = account.id;
        c.name = u"Kalendarz "_s + id;
        settings.calendars.append(c);
    }
    settings.calendars[1].alarms = false;      // switched off
    settings.calendars[2].showEvents = false;  // events not shown: nothing rings either
    QVERIFY(store.saveSettings(settings));
    const QString body = vevent(u"e"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T090000\r\nSUMMARY:Wspólne\r\n"_s + alarm(u"TRIGGER:-PT15M"_s));
    for (const QString &id : {u"c-1"_s, u"c-2"_s, u"c-3"_s}) {
        CalendarState state;
        state.resources.insert(u"/cal/e.ics"_s, Resource{u"\"1\""_s, u"e"_s, u"VEVENT"_s, calendar(body)});
        state.resources.insert(u"/cal/t.ics"_s, Resource{u"\"2\""_s, u"t"_s, u"VTODO"_s, QByteArray("BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n")});
        QVERIFY(store.saveState(id, state));
    }
    const QList<ScheduledNotification> list = eventNotifications(store, local(2026, 11, 1), local(2026, 12, 1), Warsaw);
    QCOMPARE(list.size(), 1);
    QCOMPARE(list.first().path, u"calendar:2026-11-20"_s);
    QVERIFY(list.first().body.startsWith(u"Kalendarz c-1 · "_s));
    // The switch is stored.
    const CalendarSettings back = CalendarSettings::fromJson(settings.toJson());
    QVERIFY(back.calendar(u"c-1"_s)->alarms);
    QVERIFY(!back.calendar(u"c-2"_s)->alarms);
    // An older file without the key: on.
    QJsonObject json = settings.toJson();
    QJsonArray cals = json.value(u"calendars"_s).toArray();
    QJsonObject first = cals.at(1).toObject();
    first.remove(u"alarms"_s);
    cals[1] = first;
    json.insert(u"calendars"_s, cals);
    QVERIFY(CalendarSettings::fromJson(json).calendar(u"c-2"_s)->alarms);
}

void TestCalDavAlarms::subscriptionsRingWhenAsked()
{
    // An ICS subscription's events ring once its alarms are on (off by default: someone else's
    // calendar), from the feed as last fetched.
    CalendarStore store{QString()};
    CalendarSettings settings;
    for (const QString &id : {u"s-1"_s, u"s-2"_s, u"s-3"_s}) { // (s-3: alarms on, never fetched)
        Subscription sub;
        sub.id = id;
        sub.url = u"https://example.pl/"_s + id + u".ics"_s;
        sub.name = u"Plan "_s + id;
        settings.subscriptions.append(sub);
    }
    settings.subscriptions[0].alarms = true;
    settings.subscriptions[2].alarms = true;
    QVERIFY(store.saveSettings(settings));
    const QByteArray feed = calendar(
            vevent(u"lekcja"_s, u"DTSTART;TZID=Europe/Warsaw:20261120T080000\r\nSUMMARY:Łacina\r\n"_s
                                        + alarm(u"TRIGGER:-PT10M"_s)));
    for (const QString &id : {u"s-1"_s, u"s-2"_s})
        QVERIFY(store.saveFeed(id, &feed, FeedMeta{}));
    const QList<ScheduledNotification> list = eventNotifications(store, local(2026, 11, 1), local(2026, 12, 1), Warsaw);
    QCOMPARE(keys(list), QStringList{u"2026-11-20 07:50 Łacina"_s});
    QVERIFY(list.first().body.startsWith(u"Plan s-1 · "_s));
    // The switch is stored; an older file without it: off.
    const CalendarSettings back = CalendarSettings::fromJson(settings.toJson());
    QVERIFY(back.subscription(u"s-1"_s)->alarms);
    QVERIFY(!back.subscription(u"s-2"_s)->alarms);
    QJsonObject json = settings.toJson();
    QJsonArray subs = json.value(u"subscriptions"_s).toArray();
    QJsonObject first = subs.at(0).toObject();
    first.remove(u"alarms"_s);
    subs[0] = first;
    json.insert(u"subscriptions"_s, subs);
    QVERIFY(!CalendarSettings::fromJson(json).subscription(u"s-1"_s)->alarms);
}

void TestCalDavAlarms::settingsOfAnOlderFile()
{
    // A calendars.json with only what an older version (or a hand) wrote: the defaults; values
    // out of range clamped; entries without what they need left out.
    const QJsonObject json = QJsonDocument::fromJson(R"({
        "accounts": [{"id": "a-1"}, {"id": "a-2", "interval": 1, "conflicts": "both"}, {"id": "a-3", "interval": 99999}, {}],
        "calendars": [{"id": "c-1", "account": "a-1"}, {"id": "c-2", "account": "a-nie-ma"}, {"account": "a-1"}],
        "subscriptions": [{"id": "s-1", "url": "https://example.pl/a.ics"}, {"id": "s-2", "url": "https://example.pl/b.ics", "refresh": 1},
                          {"id": "s-3", "url": "https://example.pl/c.ics", "refresh": 99999}, {"id": "s-4"}, {"url": "https://example.pl/d.ics"}]
    })").object();
    const CalendarSettings s = CalendarSettings::fromJson(json);
    QCOMPARE(s.accounts.size(), 3);
    QCOMPARE(s.accounts.at(0).intervalMinutes, 15);
    QCOMPARE(s.accounts.at(0).conflicts, ConflictPolicy::Newer);
    QCOMPARE(s.accounts.at(1).intervalMinutes, 5);
    QCOMPARE(s.accounts.at(1).conflicts, ConflictPolicy::KeepBoth);
    QCOMPARE(s.accounts.at(2).intervalMinutes, 1440);
    QCOMPARE(s.calendars.size(), 1);
    const CalendarEntry &c = s.calendars.first();
    QVERIFY(c.showEvents);
    QVERIFY(c.alarms);
    QVERIFY(c.visible);
    QVERIFY(!c.readOnly);
    QVERIFY(!c.syncTasks);
    QCOMPARE(s.subscriptions.size(), 3);
    QVERIFY(s.subscriptions.at(0).visible);
    QVERIFY(!s.subscriptions.at(0).alarms);
    QCOMPARE(s.subscriptions.at(0).refreshMinutes, 360);
    QCOMPARE(s.subscriptions.at(1).refreshMinutes, 15);
    QCOMPARE(s.subscriptions.at(2).refreshMinutes, 10'080);
}

QTEST_GUILESS_MAIN(TestCalDavAlarms)
#include "tst_caldav_alarms.moc"
