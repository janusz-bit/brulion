// Changes of CalDAV events as one decision (caldav/eventchange.h): an edit's times resolved against
// the event (moved keeping its length, a day or a time alone, all-day and timed both ways, across a
// daylight-saving change), the event's fields as the dialog shows them (a series, an occurrence, a
// moved one, a left-out one, another zone, all-day, a rule Brulion cannot show), the change an
// intent makes of a calendar (created — twice is once —, changed, one occurrence changed or left
// out, deleted; what a subscription, a read-only, a tasks-only or a hidden calendar refuses), the
// outbox's bookkeeping, intents as JSON (malformed ones refused) and the inbox's files. Polish text.
#include "caldav/eventchange.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::caldav;
using namespace Qt::StringLiterals;

namespace {

const QTimeZone Warsaw("Europe/Warsaw");
const QString Home = u"/dav/praca/"_s;

/// "2026-11-20 09:30/2026-11-20 10:15" (timed) or "2026-11-20/2026-11-22" (all-day, last day).
QString describe(const EventEdit &e)
{
    if (e.allDay())
        return e.startDate.toString(Qt::ISODate) + u'/' + e.endDate.toString(Qt::ISODate);
    return e.startDate.toString(Qt::ISODate) + u' ' + e.startTime->toString(u"HH:mm"_s) + u'/'
           + e.endDate.toString(Qt::ISODate) + u' ' + e.endTime.value_or(QTime()).toString(u"HH:mm"_s);
}

/// The fields of an event as describe() writes them ("" none).
std::optional<EventFields> fieldsOf(const QString &spec)
{
    if (spec.isEmpty())
        return std::nullopt;
    EventFields f;
    f.edit.title = u"Spotkanie zespołu"_s;
    f.edit.ruleChanged = false;
    f.edit.reminderChanged = false;
    const QStringList parts = spec.split(u'/');
    const QStringList a = parts.at(0).split(u' ');
    const QStringList b = parts.at(1).split(u' ');
    f.edit.startDate = QDate::fromString(a.at(0), Qt::ISODate);
    f.edit.endDate = QDate::fromString(b.at(0), Qt::ISODate);
    if (a.size() > 1) {
        f.edit.startTime = QTime::fromString(a.at(1), u"HH:mm"_s);
        f.edit.endTime = QTime::fromString(b.at(1), u"HH:mm"_s);
    }
    return f;
}

/// "" none, "14:00" a time alone, "2026-11-23" a day, "2026-11-23 14:00" both.
std::optional<EventMoment> momentOf(const QString &text)
{
    if (text.isEmpty())
        return std::nullopt;
    if (!text.contains(u'-'))
        return EventMoment(QTime::fromString(text, u"HH:mm"_s));
    const QStringList parts = text.split(u' ');
    DateSpec d{QDate::fromString(parts.at(0), Qt::ISODate), std::nullopt};
    if (parts.size() > 1)
        d.time = QTime::fromString(parts.at(1), u"HH:mm"_s);
    return EventMoment(d);
}

QString vevent(const QString &uid, const QString &lines)
{
    return u"BEGIN:VEVENT\r\nUID:"_s + uid + u"\r\nDTSTAMP:20261001T000000Z\r\n"_s + lines + u"END:VEVENT\r\n"_s;
}

QByteArray ics(const QString &body)
{
    return (u"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:-//Test//EN\r\n"_s + body + u"END:VCALENDAR\r\n"_s).toUtf8();
}

/// A weekly stand-up from 14 October (10:00–10:15 in Warsaw) with its 21 October occurrence moved
/// to the 23rd and its 28 October one left out, a call set in New York's time, a holiday of three
/// days, a series Brulion's rules cannot show, a coffee just before the clocks change.
CalendarState praca()
{
    CalendarState s;
    const auto add = [&s](const QString &name, const QString &uid, const QString &body) {
        s.resources.insert(Home + name, Resource{u"\"1\""_s, uid, u"VEVENT"_s, ics(body)});
    };
    add(u"stand.ics"_s, u"stand"_s,
        vevent(u"stand"_s, u"DTSTART;TZID=Europe/Warsaw:20261014T100000\r\nDTEND;TZID=Europe/Warsaw:20261014T101500\r\n"
                           u"RRULE:FREQ=WEEKLY\r\nEXDATE;TZID=Europe/Warsaw:20261028T100000\r\n"
                           u"SUMMARY:Zażółć gęślą jaźń\r\nLOCATION:Sala B\r\n"
                           u"BEGIN:VALARM\r\nACTION:DISPLAY\r\nTRIGGER:-PT10M\r\nEND:VALARM\r\n"_s)
                + vevent(u"stand"_s,
                         u"RECURRENCE-ID;TZID=Europe/Warsaw:20261021T100000\r\n"
                         u"DTSTART;TZID=Europe/Warsaw:20261023T120000\r\nDTEND;TZID=Europe/Warsaw:20261023T130000\r\n"
                         u"SUMMARY:Zażółć (przeniesione)\r\n"_s));
    add(u"nowy-jork.ics"_s, u"nowy-jork"_s,
        vevent(u"nowy-jork"_s,
               u"DTSTART;TZID=America/New_York:20261120T090000\r\n"
               u"DTEND;TZID=America/New_York:20261120T100000\r\nSUMMARY:Rozmowa z Nowym Jorkiem\r\n"_s));
    add(u"urlop.ics"_s, u"urlop"_s,
        vevent(u"urlop"_s,
               u"DTSTART;VALUE=DATE:20261123\r\nDTEND;VALUE=DATE:20261126\r\nSUMMARY:Urlop w Zakopanem\r\n"_s));
    add(u"dziwny.ics"_s, u"dziwny"_s,
        vevent(u"dziwny"_s, u"DTSTART:20261001T080000Z\r\nDTEND:20261001T090000Z\r\nRRULE:FREQ=WEEKLY;COUNT=4\r\n"
                            u"SUMMARY:Cztery razy\r\n"_s));
    add(u"pierwszy.ics"_s, u"pierwszy"_s,
        vevent(u"pierwszy"_s, u"DTSTART:20261005T080000\r\nDTEND:20261005T083000\r\nRRULE:FREQ=WEEKLY\r\n"
                              u"EXDATE:20261005T080000,20261012T080000\r\nSUMMARY:Bez pierwszego\r\n"_s));
    add(u"odwolane.ics"_s, u"odwolane"_s,
        vevent(u"odwolane"_s, u"DTSTART:20261106T120000\r\nSTATUS:CANCELLED\r\nSUMMARY:Odwołane\r\n"_s));
    add(u"zaproszenie.ics"_s, u"zaproszenie"_s,
        vevent(u"zaproszenie"_s, u"RECURRENCE-ID:20261110T100000\r\nDTSTART:20261110T110000\r\n"
                                 u"DTEND:20261110T120000\r\nSUMMARY:Jedno spotkanie z serii\r\n"_s));
    add(u"kawa.ics"_s, u"kawa"_s,
        vevent(u"kawa"_s, u"DTSTART;TZID=Europe/Warsaw:20261021T100000\r\nDTEND;TZID=Europe/Warsaw:20261021T103000\r\n"
                          u"SUMMARY:Kawa z Łucją\r\n"_s));
    return s;
}

CalendarEntry pracaEntry()
{
    CalendarEntry c;
    c.id = u"c-1"_s;
    c.account = u"a-1"_s;
    c.url = u"https://dav.example.pl/dav/praca/"_s;
    c.name = u"Praca"_s;
    return c;
}

TodoContext context()
{
    TodoContext ctx;
    ctx.now = QDateTime(QDate(2026, 11, 20), QTime(10, 0), Warsaw);
    ctx.zone = Warsaw;
    return ctx;
}

EventIntent intent(EventIntent::Kind kind, const QString &uid, const QString &instance = {})
{
    EventIntent i;
    i.kind = kind;
    i.id = newIntentId(QDateTime(QDate(2026, 11, 20), QTime(9, 0), QTimeZone::UTC));
    i.calendarId = u"c-1"_s;
    i.uid = uid;
    i.instance = instanceFromText(instance);
    i.at = QDateTime(QDate(2026, 11, 20), QTime(9, 0), QTimeZone::UTC);
    return i;
}

/// The occurrences of `uid` in [from, to] as "2026-10-23 12:00 Title".
QStringList shown(const CalendarState &state, const QString &uid, QDate from, QDate to)
{
    const CalendarData data = eventsOf(state);
    QStringList out;
    for (const Occurrence &o : occurrences(data, from, to, Warsaw)) {
        if (o.uid == uid)
            out.append(o.start.date.toString(Qt::ISODate) + u' ' + o.start.time.toString(u"HH:mm"_s) + u' '
                       + data.events.at(o.event).summary);
    }
    return out;
}

} // namespace

class TestCalDavEventChange : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void editedTimes_data();
    void editedTimes();
    void editedTexts();
    void fields_data();
    void fields();
    void created();
    void createdTwiceIsOnce();
    void changed();
    void occurrenceChanged();
    void occurrenceOfTheFirstEvent();
    void seriesMadeAllDay();
    void paths_data();
    void paths();
    void uids();
    void acrossTheClockChange();
    void deleted();
    void refused_data();
    void refused();
    void writable();
    void outbox();
    void pendingOverlay();
    void instances_data();
    void instances();
    void intentJson();
    void malformedIntents_data();
    void malformedIntents();
    void inbox();
};

void TestCalDavEventChange::initTestCase()
{
    if (!Warsaw.isValid())
        QSKIP("no time zone database");
}

void TestCalDavEventChange::editedTimes_data()
{
    QTest::addColumn<QString>("current");
    QTest::addColumn<QString>("start");
    QTest::addColumn<QString>("end");
    QTest::addColumn<int>("allDay");       // -1 not given
    QTest::addColumn<QString>("expected"); // describe(), or a problem code
    const QString timed = u"2026-11-20 09:30/2026-11-20 10:15"_s;
    const QString days = u"2026-11-20/2026-11-22"_s;
    const QString night = u"2026-11-20 22:00/2026-11-21 02:00"_s;
    QTest::newRow("new at a time: an hour")
            << "" << "2026-12-01 10:00" << "" << -1 << "2026-12-01 10:00/2026-12-01 11:00";
    QTest::newRow("new on a day: all-day") << "" << "2026-12-01" << "" << -1 << "2026-12-01/2026-12-01";
    QTest::newRow("new over three days") << "" << "2026-12-01" << "2026-12-03" << -1 << "2026-12-01/2026-12-03";
    QTest::newRow("new with an end time")
            << "" << "2026-12-01 10:00" << "11:30" << -1 << "2026-12-01 10:00/2026-12-01 11:30";
    QTest::newRow("new over midnight") << "" << "2026-12-01 23:00" << "01:00" << -1
                                       << "2026-12-01 23:00/2026-12-02 01:00";
    QTest::newRow("new with an end day and time")
            << "" << "2026-12-01 22:00" << "2026-12-02 02:00" << -1 << "2026-12-01 22:00/2026-12-02 02:00";
    QTest::newRow("new ending another day: an hour later then")
            << "" << "2026-12-01 10:00" << "2026-12-02" << -1 << "2026-12-01 10:00/2026-12-02 11:00";
    QTest::newRow("new at a time by the flag") << "" << "2026-12-01" << "" << 0 << "2026-12-01 09:00/2026-12-01 10:00";
    QTest::newRow("new without a day") << "" << "" << "" << -1 << "no_start";
    QTest::newRow("new at a time alone") << "" << "10:00" << "" << -1 << "no_start";
    QTest::newRow("new ending before it starts") << "" << "2026-12-03" << "2026-12-01" << -1 << "end_before_start";
    QTest::newRow("new ending before its time")
            << "" << "2026-12-01 10:00" << "2026-12-01 09:00" << -1 << "end_before_start";
    QTest::newRow("new all-day at a time") << "" << "2026-12-01 10:00" << "" << 1 << "all_day_with_time";
    QTest::newRow("new all-day ending at a time")
            << "" << "2026-12-01" << "2026-12-02 10:00" << -1 << "all_day_with_time";
    QTest::newRow("nothing given: kept") << timed << "" << "" << -1 << timed;
    QTest::newRow("moved to a day: its time and length")
            << timed << "2026-11-23" << "" << -1 << "2026-11-23 09:30/2026-11-23 10:15";
    QTest::newRow("moved to a time: its length")
            << timed << "2026-11-23 14:00" << "" << -1 << "2026-11-23 14:00/2026-11-23 14:45";
    QTest::newRow("a time alone: its day") << timed << "08:00" << "" << -1 << "2026-11-20 08:00/2026-11-20 08:45";
    QTest::newRow("late: over midnight") << timed << "23:50" << "" << -1 << "2026-11-20 23:50/2026-11-21 00:35";
    QTest::newRow("its end at its start: no length")
            << timed << "" << "09:30" << -1 << "2026-11-20 09:30/2026-11-20 09:30";
    QTest::newRow("its end alone") << timed << "" << "12:00" << -1 << "2026-11-20 09:30/2026-11-20 12:00";
    QTest::newRow("its end as a day: its end time")
            << timed << "" << "2026-11-21" << -1 << "2026-11-20 09:30/2026-11-21 10:15";
    QTest::newRow("its end before its start") << timed << "" << "2026-11-19 12:00" << -1 << "end_before_start";
    QTest::newRow("timed to all-day") << timed << "" << "" << 1 << "2026-11-20/2026-11-20";
    QTest::newRow("all-day yet a time") << timed << "10:00" << "" << 1 << "all_day_with_time";
    QTest::newRow("over the night, moved") << night << "2026-11-27" << "" << -1 << "2026-11-27 22:00/2026-11-28 02:00";
    QTest::newRow("over the night, all-day: both days") << night << "" << "" << 1 << "2026-11-20/2026-11-21";
    QTest::newRow("all-day: kept") << days << "" << "" << -1 << days;
    QTest::newRow("all-day moved: its days") << days << "2026-12-01" << "" << -1 << "2026-12-01/2026-12-03";
    QTest::newRow("all-day at a time") << days << "2026-11-20 10:00" << "" << -1 << "2026-11-20 10:00/2026-11-20 11:00";
    QTest::newRow("all-day at a time alone") << days << "10:00" << "" << -1 << "2026-11-20 10:00/2026-11-20 11:00";
    QTest::newRow("all-day to timed by the flag") << days << "" << "" << 0 << "2026-11-20 09:00/2026-11-20 10:00";
    QTest::newRow("all-day, a later last day") << days << "" << "2026-11-25" << -1 << "2026-11-20/2026-11-25";
    QTest::newRow("all-day, a last day before") << days << "" << "2026-11-19" << -1 << "end_before_start";
    // The wall clock, never an instant: a week later is 10:00 again although the clocks went back.
    QTest::newRow("across the clock change")
            << "2026-10-21 10:00/2026-10-21 11:00" << "2026-10-28" << "" << -1 << "2026-10-28 10:00/2026-10-28 11:00";
}

void TestCalDavEventChange::editedTimes()
{
    QFETCH(QString, current);
    QFETCH(QString, start);
    QFETCH(QString, end);
    QFETCH(int, allDay);
    QFETCH(QString, expected);
    EventPatch patch;
    if (current.isEmpty())
        patch.title = u"Wizyta u dentysty — ząb"_s;
    patch.times.start = momentOf(start);
    patch.times.end = momentOf(end);
    if (allDay >= 0)
        patch.times.allDay = allDay == 1;
    EventProblem problem = EventProblem::None;
    const auto edit = editedFields(fieldsOf(current), patch, &problem);
    if (expected.contains(u'_')) {
        QVERIFY(!edit);
        QCOMPARE(problemCode(problem), expected);
        return;
    }
    QVERIFY2(edit, qPrintable(problemCode(problem)));
    QCOMPARE(problem, EventProblem::None);
    QCOMPARE(describe(*edit), expected);
}

void TestCalDavEventChange::editedTexts()
{
    std::optional<EventFields> current = fieldsOf(u"2026-11-20 09:30/2026-11-20 10:15"_s);
    current->edit.location = u"Sala B"_s;
    current->edit.description = u"Plan na grudzień"_s;
    current->edit.rule = u"every week"_s;
    current->ruleText = u"FREQ=WEEKLY"_s;
    current->edit.reminderMinutes = 10;
    // Nothing given: all kept, the rule and reminder not rewritten.
    auto edit = editedFields(current, {});
    QVERIFY(edit);
    QCOMPARE(edit->title, u"Spotkanie zespołu"_s);
    QCOMPARE(edit->location, u"Sala B"_s);
    QCOMPARE(edit->rule, u"every week"_s);
    QVERIFY(!edit->ruleChanged);
    QCOMPARE(edit->reminderMinutes, 10);
    QVERIFY(!edit->reminderChanged);
    // Each one given is set: "" removes; the rule in its canonical form.
    EventPatch patch;
    patch.title = u"Zebranie: żółta kartka"_s;
    patch.location = QString();
    patch.description = u"Druga linia\nżółw"_s;
    patch.rule = u"co 2 tygodnie"_s;
    patch.reminderMinutes = -1;
    edit = editedFields(current, patch);
    QVERIFY(edit);
    QCOMPARE(edit->title, u"Zebranie: żółta kartka"_s);
    QVERIFY(edit->location.isEmpty());
    QCOMPARE(edit->description, u"Druga linia\nżółw"_s);
    QCOMPARE(edit->rule, u"every 2 weeks"_s);
    QVERIFY(edit->ruleChanged);
    QCOMPARE(edit->reminderMinutes, -1);
    QVERIFY(edit->reminderChanged);
    // The same rule given again is written again (a rule the server wrote in its own words, too).
    patch = {};
    patch.rule = u""_s;
    edit = editedFields(current, patch);
    QVERIFY(edit && edit->ruleChanged && edit->rule.isEmpty());
    // Refused: a title of spaces, a rule nobody understands, a reminder before the epoch.
    EventProblem problem = EventProblem::None;
    patch = {};
    patch.title = u"   "_s;
    QVERIFY(!editedFields(current, patch, &problem));
    QCOMPARE(problem, EventProblem::NoTitle);
    patch = {};
    patch.rule = u"co kiedyś"_s;
    QVERIFY(!editedFields(current, patch, &problem));
    QCOMPARE(problem, EventProblem::UnknownRule);
    // A new event: its rule and reminder are what was given.
    patch = {};
    patch.title = u"Basen"_s;
    patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 2), QTime(7, 0)});
    patch.reminderMinutes = 30;
    edit = editedFields(std::nullopt, patch);
    QVERIFY(edit);
    QCOMPARE(edit->reminderMinutes, 30);
    QVERIFY(edit->rule.isEmpty());
}

void TestCalDavEventChange::fields_data()
{
    QTest::addColumn<QString>("uid");
    QTest::addColumn<QString>("instance");
    QTest::addColumn<QString>("expected"); // describe(), or a problem code
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("rule");
    QTest::addColumn<int>("reminder");
    QTest::addColumn<bool>("recurring");
    QTest::newRow("the series") << "stand" << "" << "2026-10-14 10:00/2026-10-14 10:15" << "Zażółć gęślą jaźń"
                                << "every week" << 10 << true;
    QTest::newRow("an occurrence") << "stand" << "20261104T100000@Europe/Warsaw" << "2026-11-04 10:00/2026-11-04 10:15"
                                   << "Zażółć gęślą jaźń" << "every week" << 10 << true;
    QTest::newRow("the moved one") << "stand" << "20261021T100000@Europe/Warsaw" << "2026-10-23 12:00/2026-10-23 13:00"
                                   << "Zażółć (przeniesione)" << "" << -1 << true;
    QTest::newRow("the left-out one") << "stand" << "20261028T100000@Europe/Warsaw" << "no_occurrence" << "" << "" << -1
                                      << false;
    QTest::newRow("never one") << "stand" << "20261015T100000@Europe/Warsaw" << "no_occurrence" << "" << "" << -1
                               << false;
    QTest::newRow("New York's time on Warsaw's clock") << "nowy-jork" << "" << "2026-11-20 15:00/2026-11-20 16:00"
                                                       << "Rozmowa z Nowym Jorkiem" << "" << -1 << false;
    QTest::newRow("all-day: its last day")
            << "urlop" << "" << "2026-11-23/2026-11-25" << "Urlop w Zakopanem" << "" << -1 << false;
    QTest::newRow("an occurrence of no series") << "urlop" << "20261123" << "no_occurrence" << "" << "" << -1 << false;
    QTest::newRow("a rule Brulion cannot show")
            << "dziwny" << "" << "2026-10-01 10:00/2026-10-01 11:00" << "Cztery razy"
            << "" << -1 << true;
    QTest::newRow("no such event") << "brak" << "" << "not_found" << "" << "" << -1 << false;
    QTest::newRow("a series without its first day") << "pierwszy" << "" << "2026-10-05 08:00/2026-10-05 08:30"
                                                    << "Bez pierwszego" << "every week" << -1 << true;
    QTest::newRow("cancelled") << "odwolane" << "" << "no_occurrence" << "" << "" << -1 << false;
    QTest::newRow("a lone exception: itself") << "zaproszenie" << "" << "2026-11-10 11:00/2026-11-10 12:00"
                                              << "Jedno spotkanie z serii" << "" << -1 << true;
    QTest::newRow("a lone exception: another occurrence") << "zaproszenie" << "20261117T100000" << "no_occurrence"
                                                          << "" << "" << -1 << false;
}

void TestCalDavEventChange::fields()
{
    QFETCH(QString, uid);
    QFETCH(QString, instance);
    QFETCH(QString, expected);
    QFETCH(QString, title);
    QFETCH(QString, rule);
    QFETCH(int, reminder);
    QFETCH(bool, recurring);
    const CalendarData data = eventsOf(praca());
    EventProblem problem = EventProblem::None;
    const auto f = eventFields(data, uid, instanceFromText(instance), Warsaw, &problem);
    if (expected.contains(u'_')) {
        QVERIFY(!f);
        QCOMPARE(problemCode(problem), expected);
        return;
    }
    QVERIFY2(f, qPrintable(problemCode(problem)));
    QCOMPARE(problem, EventProblem::None);
    QCOMPARE(describe(f->edit), expected);
    QCOMPARE(f->edit.title, title);
    QCOMPARE(f->edit.rule, rule);
    QCOMPARE(f->edit.reminderMinutes, reminder);
    QCOMPARE(f->recurring, recurring);
    QVERIFY(!f->edit.ruleChanged); // what the event is, not an edit of it
    QVERIFY(!f->edit.reminderChanged);
    QCOMPARE(f->path, Home + uid + u".ics"_s);
    QCOMPARE(data.events.at(f->event).uid, uid);
    if (uid == u"dziwny")
        QCOMPARE(f->ruleText, u"FREQ=WEEKLY;COUNT=4"_s);
}

void TestCalDavEventChange::created()
{
    EventIntent i = intent(EventIntent::Kind::Create, u"brulion-event-obiad"_s);
    i.patch.title = u"Obiad z Zosią"_s;
    i.patch.location = u"Kraków, ul. Łobzowska"_s;
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 1), QTime(13, 0)});
    i.patch.times.end = EventMoment(QTime(14, 30));
    i.patch.rule = u"co tydzień"_s;
    i.patch.reminderMinutes = 15;
    EventProblem problem = EventProblem::None;
    EventEdit result;
    const auto change = plannedChange(pracaEntry(), praca(), i, context(), &problem, &result);
    QVERIFY2(change, qPrintable(problemCode(problem)));
    QCOMPARE(change->kind, EventChange::Kind::Create);
    QCOMPARE(change->uid, u"brulion-event-obiad"_s);
    QCOMPARE(change->path, Home + u"brulion-event-obiad.ics"_s);
    QCOMPARE(change->at, i.at);
    const QString text = QString::fromUtf8(change->data);
    QVERIFY2(text.contains(u"SUMMARY:Obiad z Zosią"_s), qPrintable(text));
    QVERIFY(text.contains(u"LOCATION:Kraków\\, ul. Łobzowska"_s));
    QVERIFY(text.contains(u"DTSTART;TZID=Europe/Warsaw:20261201T130000"_s));
    QVERIFY(text.contains(u"DTEND;TZID=Europe/Warsaw:20261201T143000"_s));
    QVERIFY(text.contains(u"RRULE:FREQ=WEEKLY"_s));
    QVERIFY(text.contains(u"TRIGGER:-PT15M"_s));
    QVERIFY(text.contains(u"BEGIN:VTIMEZONE"_s));
    QCOMPARE(describe(result), u"2026-12-01 13:00/2026-12-01 14:30"_s);
    // All-day: dates, no zone.
    i.patch = {};
    i.patch.title = u"Wigilia"_s;
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 24), std::nullopt});
    const auto day = plannedChange(pracaEntry(), praca(), i, context());
    QVERIFY(day);
    QVERIFY(day->data.contains("DTSTART;VALUE=DATE:20261224"));
    QVERIFY(day->data.contains("DTEND;VALUE=DATE:20261225"));
}

void TestCalDavEventChange::createdTwiceIsOnce()
{
    // The run took an intent, then could not delete its file: taken again, it changes the event it
    // made instead of making a second one.
    EventIntent i = intent(EventIntent::Kind::Create, u"brulion-event-raz"_s);
    i.patch.title = u"Spacer nad Wisłą"_s;
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 5), QTime(15, 0)});
    CalendarState state = praca();
    auto first = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(first);
    addToOutbox(state, *first);
    auto again = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(again);
    QCOMPARE(again->kind, EventChange::Kind::Update);
    QCOMPARE(again->path, first->path);
    addToOutbox(state, *again);
    QCOMPARE(state.outbox.size(), 1);
    QCOMPARE(state.outbox.first().kind, EventChange::Kind::Create); // still new to the server
    QCOMPARE(shown(state, u"brulion-event-raz"_s, QDate(2026, 12, 1), QDate(2026, 12, 31)),
             QStringList{u"2026-12-05 15:00 Spacer nad Wisłą"_s});
}

void TestCalDavEventChange::changed()
{
    // The title alone: the event stays at its instant (written on the device's clock now).
    EventIntent i = intent(EventIntent::Kind::Change, u"nowy-jork"_s);
    i.patch.title = u"Rozmowa z Nowym Jorkiem (przełożona)"_s;
    EventEdit result;
    auto change = plannedChange(pracaEntry(), praca(), i, context(), nullptr, &result);
    QVERIFY(change);
    QCOMPARE(result.title, i.patch.title);
    QCOMPARE(describe(result), u"2026-11-20 15:00/2026-11-20 16:00"_s);
    QCOMPARE(change->kind, EventChange::Kind::Update);
    QCOMPARE(change->path, Home + u"nowy-jork.ics"_s);
    QVERIFY(change->data.contains("SUMMARY:Rozmowa z Nowym Jorkiem (przełożona)"));
    QVERIFY(change->data.contains("DTSTART;TZID=Europe/Warsaw:20261120T150000"));
    QVERIFY(change->data.contains("DTEND;TZID=Europe/Warsaw:20261120T160000"));
    // The whole series moved by an hour: its rule, its exception and its left-out day stay.
    i = intent(EventIntent::Kind::Change, u"stand"_s);
    i.patch.times.start = EventMoment(QTime(11, 0));
    CalendarState state = praca();
    change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 10, 14), QDate(2026, 11, 4)),
             (QStringList{u"2026-10-14 11:00 Zażółć gęślą jaźń"_s, u"2026-10-23 12:00 Zażółć (przeniesione)"_s,
                          u"2026-11-04 11:00 Zażółć gęślą jaźń"_s}));
    QVERIFY(change->data.contains("RRULE:FREQ=WEEKLY"));
    QVERIFY(change->data.contains("TRIGGER:-PT10M"));
    // A series renamed only: its left-out days stay as the server wrote them.
    i = intent(EventIntent::Kind::Change, u"pierwszy"_s);
    i.patch.title = u"Bez dwóch pierwszych"_s;
    change = plannedChange(pracaEntry(), praca(), i, context());
    QVERIFY(change);
    QVERIFY2(change->data.contains("EXDATE:20261005T080000,20261012T080000"), change->data.constData());
    // An event without a length moved: still without one.
    CalendarState chwila;
    chwila.resources.insert(
            Home + u"chwila.ics"_s,
            Resource{u"\"1\""_s, u"chwila"_s, u"VEVENT"_s,
                     ics(vevent(u"chwila"_s, u"DTSTART;TZID=Europe/Warsaw:20261021T100000\r\n"
                                             u"DTEND;TZID=Europe/Warsaw:20261021T100000\r\nSUMMARY:Chwila\r\n"_s))});
    i = intent(EventIntent::Kind::Change, u"chwila"_s);
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 10, 22), std::nullopt});
    change = plannedChange(pracaEntry(), chwila, i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("DTEND;TZID=Europe/Warsaw:20261022T100000\r\n"));
    // An all-day event made a timed one.
    i = intent(EventIntent::Kind::Change, u"urlop"_s);
    i.patch.times.start = EventMoment(QTime(8, 0));
    i.patch.times.end = EventMoment(DateSpec{QDate(2026, 11, 25), QTime(16, 0)});
    change = plannedChange(pracaEntry(), praca(), i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("DTSTART;TZID=Europe/Warsaw:20261123T080000"));
    QVERIFY(change->data.contains("DTEND;TZID=Europe/Warsaw:20261125T160000"));
}

void TestCalDavEventChange::occurrenceChanged()
{
    // 4 November alone, an hour later: an override of its own; the series unchanged.
    EventIntent i = intent(EventIntent::Kind::Change, u"stand"_s, u"20261104T100000@Europe/Warsaw"_s);
    i.patch.times.start = EventMoment(QTime(11, 0));
    i.patch.title = u"Zażółć — wyjątkowo później"_s;
    CalendarState state = praca();
    auto change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QCOMPARE(change->kind, EventChange::Kind::Update);
    QVERIFY(change->data.contains("RECURRENCE-ID;TZID=Europe/Warsaw:20261104T100000"));
    QVERIFY(change->data.contains("DTSTART;TZID=Europe/Warsaw:20261104T110000"));
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 11, 1), QDate(2026, 11, 11)),
             (QStringList{u"2026-11-04 11:00 Zażółć — wyjątkowo później"_s, u"2026-11-11 10:00 Zażółć gęślą jaźń"_s}));
    // The moved one again: its override changes, no second one.
    i = intent(EventIntent::Kind::Change, u"stand"_s, u"20261021T100000@Europe/Warsaw"_s);
    i.patch.title = u"Zażółć (przeniesione na piątek)"_s;
    change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QCOMPARE(QString::fromUtf8(change->data).count(u"RECURRENCE-ID;TZID=Europe/Warsaw:20261021T100000"_s), 1);
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 10, 19), QDate(2026, 10, 25)),
             QStringList{u"2026-10-23 12:00 Zażółć (przeniesione na piątek)"_s});
}

void TestCalDavEventChange::occurrenceOfTheFirstEvent()
{
    // The series is the calendar's only event: still an exception of its own, the series kept.
    CalendarState state;
    state.resources.insert(Home + u"stand.ics"_s, praca().resources.value(Home + u"stand.ics"_s));
    EventIntent i = intent(EventIntent::Kind::Change, u"stand"_s, u"20261111T100000@Europe/Warsaw"_s);
    i.patch.title = u"Tylko ten jeden"_s;
    const auto change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("RECURRENCE-ID;TZID=Europe/Warsaw:20261111T100000"));
    QVERIFY(change->data.contains("SUMMARY:Zażółć gęślą jaźń\r\n"));
    // A lone exception (an invitation to one meeting) changed as it is.
    i = intent(EventIntent::Kind::Change, u"zaproszenie"_s, u"20261110T100000"_s);
    i.patch.title = u"Jedno spotkanie — przeniesione"_s;
    i.patch.times.start = EventMoment(QTime(15, 0));
    const auto lone = plannedChange(pracaEntry(), praca(), i, context());
    QVERIFY(lone);
    QCOMPARE(QString::fromUtf8(lone->data).count(u"BEGIN:VEVENT"_s), 1);
    QVERIFY(lone->data.contains("SUMMARY:Jedno spotkanie — przeniesione"));
    QVERIFY(lone->data.contains("DTSTART;TZID=Europe/Warsaw:20261110T150000"));
}

void TestCalDavEventChange::seriesMadeAllDay()
{
    // The stand-up's series all-day from now on: its left-out day and its exception become days.
    EventIntent i = intent(EventIntent::Kind::Change, u"stand"_s);
    i.patch.times.allDay = true;
    CalendarState state = praca();
    auto change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("DTSTART;VALUE=DATE:20261014"));
    QVERIFY(change->data.contains("EXDATE;VALUE=DATE:20261028"));
    QVERIFY(change->data.contains("RECURRENCE-ID;VALUE=DATE:20261021"));
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 10, 14), QDate(2026, 11, 4)),
             (QStringList{u"2026-10-14 00:00 Zażółć gęślą jaźń"_s, u"2026-10-23 12:00 Zażółć (przeniesione)"_s,
                          u"2026-11-04 00:00 Zażółć gęślą jaźń"_s}));
    // And back at 08:15, a day later: they follow again, at the new time.
    i = intent(EventIntent::Kind::Change, u"stand"_s);
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 10, 15), QTime(8, 15)});
    change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("EXDATE;TZID=Europe/Warsaw:20261029T081500"));
    QVERIFY(change->data.contains("RECURRENCE-ID;TZID=Europe/Warsaw:20261022T081500"));
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 10, 14), QDate(2026, 11, 5)),
             (QStringList{u"2026-10-15 08:15 Zażółć gęślą jaźń"_s, u"2026-10-23 12:00 Zażółć (przeniesione)"_s,
                          u"2026-11-05 08:15 Zażółć gęślą jaźń"_s}));
}

void TestCalDavEventChange::paths_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<QString>("uid");
    QTest::addColumn<QString>("path");
    QTest::newRow("as it is") << "https://dav.example.pl/dav/praca/" << "brulion-event-a1_b.2"
                              << "/dav/praca/brulion-event-a1_b.2.ics";
    QTest::newRow("no slash") << "https://dav.example.pl/dav/praca" << "x" << "/dav/praca/x.ics";
    QTest::newRow("unsafe letters") << "https://dav.example.pl/dav/praca/" << "spotkanie@serwer.pl/1 ż"
                                    << "/dav/praca/spotkanie-serwer.pl-1-ż.ics";
    QTest::newRow("encoded folder") << "https://dav.example.pl/dav/m%C3%B3j/" << "x" << "/dav/mój/x.ics";
    QTest::newRow("no uid") << "https://dav.example.pl/dav/praca/" << "" << "/dav/praca/event.ics";
    QTest::newRow("a long uid") << "https://dav.example.pl/dav/praca/" << QString(200, u'a')
                                << u"/dav/praca/"_s + QString(120, u'a') + u".ics"_s;
}

void TestCalDavEventChange::paths()
{
    QFETCH(QString, url);
    QFETCH(QString, uid);
    QFETCH(QString, path);
    CalendarEntry c = pracaEntry();
    c.url = url;
    QCOMPARE(newEventPath(c, uid), path);
}

void TestCalDavEventChange::uids()
{
    static const QRegularExpression form(u"^brulion-event-[a-z0-9]{16}$"_s);
    const QString a = newEventUid();
    const QString b = newEventUid();
    QVERIFY2(form.match(a).hasMatch(), qPrintable(a));
    QVERIFY(a != b);
    static const QRegularExpression id(u"^20261120T090102003-[a-z0-9]{8}$"_s);
    const QString i = newIntentId(QDateTime(QDate(2026, 11, 20), QTime(10, 1, 2, 3), QTimeZone("Europe/Warsaw")));
    QVERIFY2(id.match(i).hasMatch(), qPrintable(i));
}

void TestCalDavEventChange::acrossTheClockChange()
{
    // A coffee on 21 October moved a week on, past the change to winter time: still 10:00.
    EventIntent i = intent(EventIntent::Kind::Change, u"kawa"_s);
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 10, 28), std::nullopt});
    CalendarState state = praca();
    const auto change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QVERIFY(change->data.contains("DTSTART;TZID=Europe/Warsaw:20261028T100000"));
    QVERIFY(change->data.contains("DTEND;TZID=Europe/Warsaw:20261028T103000"));
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"kawa"_s, QDate(2026, 10, 1), QDate(2026, 10, 31)),
             QStringList{u"2026-10-28 10:00 Kawa z Łucją"_s});
}

void TestCalDavEventChange::deleted()
{
    CalendarState state = praca();
    // One occurrence left out: an EXDATE; leaving it out again changes nothing.
    EventIntent i = intent(EventIntent::Kind::Delete, u"stand"_s, u"20261104T100000@Europe/Warsaw"_s);
    auto change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QCOMPARE(change->kind, EventChange::Kind::Update);
    QVERIFY(change->data.contains("EXDATE;TZID=Europe/Warsaw:20261104T100000"));
    const QByteArray twice = excludeOccurrence(change->data, u"stand"_s, *i.instance, context());
    QCOMPARE(QString::fromUtf8(twice).count(u"20261104T100000"_s), 1);
    addToOutbox(state, *change);
    QCOMPARE(shown(state, u"stand"_s, QDate(2026, 11, 1), QDate(2026, 11, 11)),
             QStringList{u"2026-11-11 10:00 Zażółć gęślą jaźń"_s});
    EventProblem problem = EventProblem::None;
    QVERIFY(!plannedChange(pracaEntry(), state, i, context(), &problem));
    QCOMPARE(problem, EventProblem::NoOccurrence);
    // The moved occurrence left out: its override goes too.
    i = intent(EventIntent::Kind::Delete, u"stand"_s, u"20261021T100000@Europe/Warsaw"_s);
    change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QVERIFY(!change->data.contains("RECURRENCE-ID"));
    QVERIFY(change->data.contains("EXDATE;TZID=Europe/Warsaw:20261021T100000"));
    // The whole event: its member deleted.
    i = intent(EventIntent::Kind::Delete, u"urlop"_s);
    change = plannedChange(pracaEntry(), state, i, context());
    QVERIFY(change);
    QCOMPARE(change->kind, EventChange::Kind::Delete);
    QCOMPARE(change->path, Home + u"urlop.ics"_s);
    QCOMPARE(change->uid, u"urlop"_s);
    addToOutbox(state, *change);
    QVERIFY(shown(state, u"urlop"_s, QDate(2026, 11, 1), QDate(2026, 11, 30)).isEmpty());
    QVERIFY(!plannedChange(pracaEntry(), state, i, context(), &problem));
    QCOMPARE(problem, EventProblem::NotFound);
}

void TestCalDavEventChange::refused_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("uid");
    QTest::addColumn<QString>("instance");
    QTest::addColumn<QString>("calendar"); // "", "read-only", "tasks", "hidden"
    QTest::addColumn<QString>("title");    // "-" none given
    QTest::addColumn<QString>("start");
    QTest::addColumn<QString>("end");
    QTest::addColumn<QString>("rule"); // "-" none given
    QTest::addColumn<QString>("problem");
    const int create = int(EventIntent::Kind::Create);
    const int change = int(EventIntent::Kind::Change);
    const int del = int(EventIntent::Kind::Delete);
    QTest::newRow("read-only") << create << "x" << "" << "read-only" << "Nowe" << "2026-12-01" << "" << "-"
                               << "read_only";
    QTest::newRow("tasks only") << create << "x" << "" << "tasks" << "Nowe" << "2026-12-01" << "" << "-" << "no_events";
    QTest::newRow("events hidden") << change << "stand" << "" << "hidden" << "Inne" << "" << "" << "-"
                                   << "events_hidden";
    QTest::newRow("no title") << create << "x" << "" << "" << "-" << "2026-12-01" << "" << "-" << "no_title";
    QTest::newRow("no day") << create << "x" << "" << "" << "Nowe" << "" << "" << "-" << "no_start";
    QTest::newRow("no such event") << change << "brak" << "" << "" << "Inne" << "" << "" << "-" << "not_found";
    QTest::newRow("no such event to delete") << del << "brak" << "" << "" << "-" << "" << "" << "-" << "not_found";
    QTest::newRow("no such occurrence") << change << "stand" << "20261015T100000@Europe/Warsaw" << "" << "Inne" << ""
                                        << ""
                                        << "-" << "no_occurrence";
    QTest::newRow("an occurrence's own rule") << change << "stand" << "20261104T100000@Europe/Warsaw" << "" << "-" << ""
                                              << "" << "every day" << "occurrence_rule";
    QTest::newRow("its end before its start") << change << "kawa" << "" << "" << "-" << "" << "2026-10-20 12:00" << "-"
                                              << "end_before_start";
    QTest::newRow("a rule nobody knows") << create << "x" << "" << "" << "Nowe" << "2026-12-01" << ""
                                         << "raz na ruski rok"
                                         << "unknown_rule";
}

void TestCalDavEventChange::refused()
{
    QFETCH(int, kind);
    QFETCH(QString, uid);
    QFETCH(QString, instance);
    QFETCH(QString, calendar);
    QFETCH(QString, title);
    QFETCH(QString, start);
    QFETCH(QString, end);
    QFETCH(QString, rule);
    QFETCH(QString, problem);
    CalendarEntry entry = pracaEntry();
    entry.readOnly = calendar == u"read-only";
    if (calendar == u"tasks")
        entry.components = {u"VTODO"_s};
    entry.showEvents = calendar != u"hidden";
    EventIntent i = intent(EventIntent::Kind(kind), uid, instance);
    if (title != u"-")
        i.patch.title = title;
    if (rule != u"-")
        i.patch.rule = rule;
    i.patch.times.start = momentOf(start);
    i.patch.times.end = momentOf(end);
    EventProblem got = EventProblem::None;
    QVERIFY(!plannedChange(entry, praca(), i, context(), &got));
    QCOMPARE(problemCode(got), problem);
    QVERIFY(!problemText(got).isEmpty());
}

void TestCalDavEventChange::writable()
{
    CalendarSettings s;
    CalendarEntry ok = pracaEntry();
    CalendarEntry ro = pracaEntry();
    ro.id = u"c-2"_s;
    ro.readOnly = true;
    CalendarEntry tasks = pracaEntry();
    tasks.id = u"c-3"_s;
    tasks.components = {u"VTODO"_s};
    CalendarEntry hidden = pracaEntry();
    hidden.id = u"c-4"_s;
    hidden.showEvents = false;
    CalendarEntry both = pracaEntry();
    both.id = u"c-5"_s;
    both.components = {u"VTODO"_s, u"VEVENT"_s};
    s.calendars = {ok, ro, tasks, hidden, both};
    Subscription sub;
    sub.id = u"s-1"_s;
    sub.name = u"Święta"_s;
    s.subscriptions = {sub};
    QCOMPARE(writableProblem(s, u"c-1"_s), EventProblem::None);
    QCOMPARE(writableProblem(s, u"c-2"_s), EventProblem::ReadOnly);
    QCOMPARE(writableProblem(s, u"c-3"_s), EventProblem::NoEvents);
    QCOMPARE(writableProblem(s, u"c-4"_s), EventProblem::EventsHidden);
    QCOMPARE(writableProblem(s, u"c-5"_s), EventProblem::None);
    QCOMPARE(writableProblem(s, u"s-1"_s), EventProblem::Subscription);
    QCOMPARE(writableProblem(s, u"c-9"_s), EventProblem::NoCalendar);
    QCOMPARE(problemCode(EventProblem::Subscription), u"subscription"_s);
}

void TestCalDavEventChange::outbox()
{
    CalendarState state;
    EventChange create;
    create.kind = EventChange::Kind::Create;
    create.uid = u"n"_s;
    create.path = Home + u"n.ics"_s;
    create.data = ics(vevent(u"n"_s, u"DTSTART:20261201T090000Z\r\nSUMMARY:Pierwsza\r\n"_s));
    create.at = QDateTime(QDate(2026, 11, 20), QTime(9, 0), QTimeZone::UTC);
    addToOutbox(state, create);
    QCOMPARE(state.outbox.size(), 1);
    QCOMPARE(state.resources.value(create.path).data, create.data);
    QCOMPARE(state.resources.value(create.path).kind, u"VEVENT"_s);
    // Changed before it was sent: still one creation, with the new text.
    EventChange edit = create;
    edit.kind = EventChange::Kind::Update;
    edit.data = ics(vevent(u"n"_s, u"DTSTART:20261201T090000Z\r\nSUMMARY:Druga — źdźbło\r\n"_s));
    addToOutbox(state, edit);
    QCOMPARE(state.outbox.size(), 1);
    QCOMPARE(state.outbox.first().kind, EventChange::Kind::Create);
    QCOMPARE(state.outbox.first().data, edit.data);
    // Deleted before it was sent: nothing left to send or show.
    EventChange del = create;
    del.kind = EventChange::Kind::Delete;
    del.data.clear();
    addToOutbox(state, del);
    QVERIFY(state.outbox.isEmpty());
    QVERIFY(state.resources.isEmpty());
    // A member the server has: changed twice, sent once with the server's ETag.
    state.resources.insert(Home + u"s.ics"_s, Resource{u"\"7\""_s, u"s"_s, u"VEVENT"_s, create.data});
    EventChange a;
    a.kind = EventChange::Kind::Update;
    a.uid = u"s"_s;
    a.path = Home + u"s.ics"_s;
    a.data = edit.data;
    addToOutbox(state, a);
    addToOutbox(state, a);
    QCOMPARE(state.outbox.size(), 1);
    QCOMPARE(state.outbox.first().ifMatch, u"\"7\""_s);
    QCOMPARE(state.resources.value(a.path).etag, u"\"7\""_s);
    // Then deleted: the deletion waits, with that ETag.
    EventChange gone = a;
    gone.kind = EventChange::Kind::Delete;
    addToOutbox(state, gone);
    QCOMPARE(state.outbox.size(), 1);
    QCOMPARE(state.outbox.first().kind, EventChange::Kind::Delete);
    QCOMPARE(state.outbox.first().ifMatch, u"\"7\""_s);
    QVERIFY(!state.resources.contains(a.path));
}

void TestCalDavEventChange::pendingOverlay()
{
    // What brulion-cli left and the app has not taken yet, in its order: a new event then renamed,
    // one stand-up left out, a change of another calendar's event, one that cannot be made.
    EventIntent create = intent(EventIntent::Kind::Create, u"brulion-event-nowe"_s);
    create.patch.title = u"Próba chóru"_s;
    create.patch.times.start = EventMoment(DateSpec{QDate(2026, 11, 26), QTime(18, 0)});
    EventIntent rename = intent(EventIntent::Kind::Change, u"brulion-event-nowe"_s);
    rename.patch.title = u"Próba chóru — kolędy"_s;
    const EventIntent skip = intent(EventIntent::Kind::Delete, u"stand"_s, u"20261104T100000@Europe/Warsaw"_s);
    EventIntent elsewhere = intent(EventIntent::Kind::Delete, u"kawa"_s);
    elsewhere.calendarId = u"c-2"_s;
    EventIntent broken = intent(EventIntent::Kind::Change, u"brak"_s);
    broken.patch.title = u"Nic"_s;
    const CalendarState after =
            withIntents(praca(), pracaEntry(), {create, rename, skip, elsewhere, broken}, context());
    QCOMPARE(shown(after, u"brulion-event-nowe"_s, QDate(2026, 11, 1), QDate(2026, 11, 30)),
             QStringList{u"2026-11-26 18:00 Próba chóru — kolędy"_s});
    QCOMPARE(shown(after, u"stand"_s, QDate(2026, 11, 1), QDate(2026, 11, 11)),
             QStringList{u"2026-11-11 10:00 Zażółć gęślą jaźń"_s});
    QCOMPARE(shown(after, u"kawa"_s, QDate(2026, 10, 1), QDate(2026, 10, 31)).size(), 1);
    QCOMPARE(after.outbox.size(), 2);
}

void TestCalDavEventChange::instances_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("valid");
    QTest::newRow("in a zone") << "20261118T093000@Europe/Warsaw" << true;
    QTest::newRow("UTC") << "20261118T083000Z" << true;
    QTest::newRow("floating") << "20261118T093000" << true;
    QTest::newRow("a day") << "20261118" << true;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("words") << "jutro" << false;
    QTest::newRow("no month 13") << "20261318" << false;
    QTest::newRow("no hour 25") << "20261118T253000" << false;
}

void TestCalDavEventChange::instances()
{
    QFETCH(QString, text);
    QFETCH(bool, valid);
    const auto v = instanceFromText(text);
    QCOMPARE(v.has_value(), valid);
    if (valid)
        QCOMPARE(instanceText(*v), text);
}

void TestCalDavEventChange::intentJson()
{
    // Every field, Polish text, an occurrence in a zone, times as the wall clock.
    EventIntent i = intent(EventIntent::Kind::Change, u"stand"_s, u"20261104T100000@Europe/Warsaw"_s);
    i.patch.title = u"Zażółć gęślą jaźń"_s;
    i.patch.location = QString();
    i.patch.description = u"Dwie\nlinie"_s;
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 11, 4), QTime(11, 0)});
    i.patch.times.end = EventMoment(QTime(12, 15));
    i.patch.times.allDay = false;
    i.patch.reminderMinutes = -1;
    const QJsonObject json = i.toJson();
    QCOMPARE(json.value(u"kind"_s).toString(), u"change"_s);
    QCOMPARE(json.value(u"start"_s).toString(), u"2026-11-04T11:00"_s);
    QCOMPARE(json.value(u"end"_s).toString(), u"12:15"_s);
    QCOMPARE(json.value(u"instance"_s).toString(), u"20261104T100000@Europe/Warsaw"_s);
    QVERIFY(json.contains(u"location"_s));
    QVERIFY(!json.contains(u"rule"_s)); // not given: kept
    const auto back = EventIntent::fromJson(QJsonDocument::fromJson(QJsonDocument(json).toJson()).object());
    QVERIFY(back);
    QCOMPARE(back->toJson(), json);
    QCOMPARE(back->patch, i.patch);
    QCOMPARE(back->at, i.at);
    QCOMPARE(instanceText(*back->instance), u"20261104T100000@Europe/Warsaw"_s);
    // A new all-day event and a deletion.
    EventIntent c = intent(EventIntent::Kind::Create, u"brulion-event-x"_s);
    c.patch.title = u"Wigilia"_s;
    c.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 24), std::nullopt});
    c.patch.rule = u"every year"_s;
    QCOMPARE(EventIntent::fromJson(c.toJson())->patch, c.patch);
    QCOMPARE(c.toJson().value(u"start"_s).toString(), u"2026-12-24"_s);
    // At the limits: still an intent.
    EventIntent longest = intent(EventIntent::Kind::Change, QString(1000, u'u'));
    longest.patch.title = QString(2000, u'ż');
    longest.patch.reminderMinutes = 366 * 1440;
    QVERIFY(EventIntent::fromJson(longest.toJson()));
    const EventIntent d = intent(EventIntent::Kind::Delete, u"urlop"_s);
    const auto dBack = EventIntent::fromJson(d.toJson());
    QVERIFY(dBack);
    QCOMPARE(dBack->kind, EventIntent::Kind::Delete);
    QVERIFY(!dBack->instance);
}

void TestCalDavEventChange::malformedIntents_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QJsonValue>("value"); // undefined: the key removed
    QTest::newRow("an unknown kind") << "kind" << QJsonValue(u"move"_s);
    QTest::newRow("a later version") << "version" << QJsonValue(2);
    QTest::newRow("no calendar") << "calendar" << QJsonValue(QJsonValue::Undefined);
    QTest::newRow("no UID") << "uid" << QJsonValue(u""_s);
    QTest::newRow("a day that is none") << "start" << QJsonValue(u"2026-13-45"_s);
    QTest::newRow("an hour that is none") << "end" << QJsonValue(u"25:00"_s);
    QTest::newRow("a time in words") << "start" << QJsonValue(u"jutro o 10"_s);
    QTest::newRow("a day at an hour that is none") << "start" << QJsonValue(u"2026-11-04T25:61"_s);
    QTest::newRow("an occurrence that is none") << "instance" << QJsonValue(u"wczoraj"_s);
    QTest::newRow("a title too long") << "title" << QJsonValue(QString(2001, u'ż'));
    QTest::newRow("a UID too long") << "uid" << QJsonValue(QString(1001, u'u'));
    QTest::newRow("a description too long") << "description" << QJsonValue(QString(20'001, u'a'));
    QTest::newRow("a number for a title") << "title" << QJsonValue(7);
    QTest::newRow("a reminder of years") << "reminderMinutes" << QJsonValue(10'000'000);
    QTest::newRow("a reminder in words") << "reminderMinutes" << QJsonValue(u"kwadrans"_s);
    QTest::newRow("no time it was asked") << "at" << QJsonValue(u"kiedyś"_s);
}

void TestCalDavEventChange::malformedIntents()
{
    QFETCH(QString, key);
    QFETCH(QJsonValue, value);
    EventIntent i = intent(EventIntent::Kind::Change, u"stand"_s, u"20261104T100000@Europe/Warsaw"_s);
    i.patch.title = u"Zażółć"_s;
    i.patch.times.start = EventMoment(DateSpec{QDate(2026, 11, 4), QTime(11, 0)});
    i.patch.times.end = EventMoment(QTime(12, 0));
    QJsonObject json = i.toJson();
    QVERIFY(EventIntent::fromJson(json));
    if (value.isUndefined())
        json.remove(key);
    else
        json.insert(key, value);
    QVERIFY(!EventIntent::fromJson(json));
}

void TestCalDavEventChange::inbox()
{
    QTemporaryDir dir;
    const QString folder = dir.filePath(u"calendars"_s);
    QVERIFY(pendingIntents(folder).isEmpty()); // no folder yet: nothing waits
    EventIntent later = intent(EventIntent::Kind::Delete, u"urlop"_s);
    later.id = newIntentId(QDateTime(QDate(2026, 11, 20), QTime(9, 5), QTimeZone::UTC));
    EventIntent first = intent(EventIntent::Kind::Create, u"brulion-event-ą"_s);
    first.id = newIntentId(QDateTime(QDate(2026, 11, 20), QTime(9, 1), QTimeZone::UTC));
    first.patch.title = u"Łąka"_s;
    first.patch.times.start = EventMoment(DateSpec{QDate(2026, 12, 1), std::nullopt});
    QVERIFY(first.id.startsWith(u"20261120T090100000-"_s));
    QVERIFY(first.id != newIntentId(QDateTime(QDate(2026, 11, 20), QTime(9, 1), QTimeZone::UTC)));
    QVERIFY(postIntent(folder, later));
    QVERIFY(postIntent(folder, first));
    const QString file = inboxFolder(folder) + u'/' + first.id + u".json"_s;
    QVERIFY(QFile::exists(file));
#ifndef Q_OS_WIN
    for (const QString &path : {file, inboxFolder(folder), folder}) {
        QCOMPARE(QFile::permissions(path)
                         & (QFile::ReadGroup | QFile::ReadOther | QFile::WriteGroup | QFile::WriteOther),
                 QFile::Permissions());
    }
#endif
    // A broken file and an unfinished one (no .json yet) beside them.
    QFile broken(inboxFolder(folder) + u"/20261120T090300000-zepsuty.json"_s);
    QVERIFY(broken.open(QIODevice::WriteOnly));
    broken.write("{\"kind\": \"create\", \"calend");
    broken.close();
    QFile partial(inboxFolder(folder) + u"/20261120T090400000-x.json.Ab12Cd"_s);
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.close();
    const QList<PendingIntent> pending = pendingIntents(folder);
    QCOMPARE(pending.size(), 3);
    QCOMPARE(pending.at(0).file, first.id + u".json"_s);
    QVERIFY(pending.at(0).intent);
    QCOMPARE(pending.at(0).intent->toJson(), first.toJson());
    QVERIFY(!pending.at(1).intent);
    QCOMPARE(pending.at(2).intent->uid, u"urlop"_s);
    QCOMPARE(pendingIntents(folder, 1).size(), 1);
    // Reading takes nothing away; none or a negative number asked: none.
    QCOMPARE(pendingIntents(folder).size(), 3);
    QVERIFY(pendingIntents(folder, 0).isEmpty());
    QVERIFY(pendingIntents(folder, -5).isEmpty());
    QVERIFY(pendingIntents({}).isEmpty());
    // Written once: the same id again, no folder, an id that is no file name, a folder that is a
    // file — refused.
    QVERIFY(!postIntent(folder, first));
    QVERIFY(!postIntent({}, later));
    EventIntent odd = later;
    odd.id = u"../poza"_s;
    QVERIFY(!postIntent(folder, odd));
    QFile plain(dir.filePath(u"plik"_s));
    QVERIFY(plain.open(QIODevice::WriteOnly));
    plain.close();
    odd.id = newIntentId(QDateTime(QDate(2026, 11, 20), QTime(9, 6), QTimeZone::UTC));
    QVERIFY(!postIntent(dir.filePath(u"plik"_s), odd));
    QVERIFY(postIntent(folder, odd));
}

QTEST_GUILESS_MAIN(TestCalDavEventChange)
#include "tst_caldav_eventchange.moc"
