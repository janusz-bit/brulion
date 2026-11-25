// The rules of scheduled conversations (schedule/schedule.h): Polish and English phrases — a rule
// of days with a time before or after it, a rule alone (at 9:00), intervals, single runs relative
// to now —, what is refused (nothing, nonsense, too often, a time passed, "when done"), the rules
// in words in both languages; next runs (later today, tomorrow, the next weekday, a month's day,
// an interval in elapsed time), a run missed while the app was closed due once and then the
// series going on from it; daylight-saving edges in Central European time (a time the spring
// skips runs at the first minute after the gap, a time the autumn repeats runs once, an interval
// across a change counts real hours); the JSON kept on the device (round trips, malformed rows
// and duplicate ids left out, at most MaxSchedules).
#include "schedule/schedule.h"

#include "centraleuropeantime.h"
#include "dates/local.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::schedule;
using Kind = When::Kind;
using Freq = dates::RecurrenceRule::Freq;

Q_DECLARE_METATYPE(brulion::schedule::WhenError)

namespace {

/// Sunday 2026-11-22, 10:00 local.
const QDateTime Now(QDate(2026, 11, 22), QTime(10, 0));

DateSpec at(const QString &text)
{
    return dates::parseDateSpec(text).value_or(DateSpec{});
}

QString next(const When &when, const QString &after)
{
    const auto n = nextRun(when, at(after));
    return n ? n->toString() : u"none"_s;
}

} // namespace

class TestSchedule : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        test::useCentralEuropeanTime();
        if (const QString problem = test::centralEuropeanTimeProblem(); !problem.isEmpty())
            QSKIP(qPrintable(problem));
    }

    void parsesRules_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("english");
        QTest::addColumn<QString>("described"); // describeWhen() in the text's language
        QTest::addColumn<QString>("first");     // the first run after Now

        // Rules of days with a time.
        QTest::newRow("codziennie o 7:30") << u"codziennie o 7:30"_s << false << u"codziennie o 7:30"_s
                                           << u"2026-11-23 07:30"_s;
        QTest::newRow("codziennie o 18 (today)") << u"Codziennie o 18:00"_s << false << u"codziennie o 18:00"_s
                                                 << u"2026-11-22 18:00"_s;
        QTest::newRow("w poniedziałki") << u"w poniedziałki o 9:00"_s << false
                                        << u"co tydzień w poniedziałek o 9:00"_s << u"2026-11-23 09:00"_s;
        QTest::newRow("without diacritics") << u"w poniedzialki i czwartki o 9"_s << false
                                            << u"co tydzień w poniedziałek, czwartek o 9:00"_s
                                            << u"2026-11-23 09:00"_s;
        QTest::newRow("time first") << u"o 6:45 w dni robocze"_s << false << u"w dni robocze o 6:45"_s
                                    << u"2026-11-23 06:45"_s;
        QTest::newRow("w każdy piątek wieczorem") << u"w każdy piątek o 20"_s << false
                                                  << u"co tydzień w piątek o 20:00"_s << u"2026-11-27 20:00"_s;
        QTest::newRow("co miesiąc 15.") << u"co miesiąc 15. o 8:00"_s << false << u"co miesiąc 15. o 8:00"_s
                                        << u"2026-12-15 08:00"_s;
        QTest::newRow("a rule alone: at nine") << u"codziennie"_s << false << u"codziennie o 9:00"_s
                                               << u"2026-11-23 09:00"_s;
        QTest::newRow("every day at") << u"every day at 7:30"_s << true << u"every day at 7:30"_s
                                      << u"2026-11-23 07:30"_s;
        QTest::newRow("daily 6pm") << u"daily at 6pm"_s << true << u"every day at 18:00"_s << u"2026-11-22 18:00"_s;
        QTest::newRow("on mondays") << u"on mondays and fridays at 9am"_s << true
                                    << u"every week on Monday, Friday at 9:00"_s << u"2026-11-23 09:00"_s;
        QTest::newRow("weekdays at 8") << u"weekdays at 8:00"_s << true << u"every weekday at 8:00"_s
                                       << u"2026-11-23 08:00"_s;
        // Intervals.
        QTest::newRow("co 3 godziny") << u"co 3 godziny"_s << false << u"co 3 godziny"_s << u"2026-11-22 13:00"_s;
        QTest::newRow("co godzinę") << u"co godzinę"_s << false << u"co godzinę"_s << u"2026-11-22 11:00"_s;
        QTest::newRow("co 5 godzin") << u"co 5 godzin"_s << false << u"co 5 godzin"_s << u"2026-11-22 15:00"_s;
        QTest::newRow("co pół godziny") << u"co pół godziny"_s << false << u"co 30 minut"_s
                                        << u"2026-11-22 10:30"_s;
        QTest::newRow("co 90 minut") << u"co 90 minut"_s << false << u"co 90 minut"_s << u"2026-11-22 11:30"_s;
        QTest::newRow("co 2h") << u"co 2h"_s << false << u"co 2 godziny"_s << u"2026-11-22 12:00"_s;
        QTest::newRow("every 2 hours") << u"every 2 hours"_s << true << u"every 2 hours"_s << u"2026-11-22 12:00"_s;
        QTest::newRow("hourly") << u"hourly"_s << true << u"every hour"_s << u"2026-11-22 11:00"_s;
        QTest::newRow("every 15 minutes") << u"every 15 minutes"_s << true << u"every 15 minutes"_s
                                          << u"2026-11-22 10:15"_s;
        // Once.
        QTest::newRow("jutro o 8") << u"jutro o 8"_s << false << u"jednorazowo: jutro o 8:00"_s
                                   << u"2026-11-23 08:00"_s;
        QTest::newRow("w piątek o 18 (once)") << u"w piątek o 18"_s << false
                                              << u"jednorazowo: pt 27 lis o 18:00"_s << u"2026-11-27 18:00"_s;
        QTest::newRow("a day alone: at nine") << u"pojutrze"_s << false << u"jednorazowo: wt 24 lis o 9:00"_s
                                              << u"2026-11-24 09:00"_s;
        QTest::newRow("a date") << u"2026-12-01 18:00"_s << false << u"jednorazowo: wt 1 gru o 18:00"_s
                                << u"2026-12-01 18:00"_s;
        QTest::newRow("za 3 godziny") << u"za 3 godziny"_s << false << u"jednorazowo: dziś o 13:00"_s
                                      << u"2026-11-22 13:00"_s;
        QTest::newRow("tomorrow 8am") << u"tomorrow at 8am"_s << true << u"once: tomorrow at 8:00"_s
                                      << u"2026-11-23 08:00"_s;
        QTest::newRow("on monday (once)") << u"on monday at 9"_s << true << u"once: tomorrow at 9:00"_s
                                          << u"2026-11-23 09:00"_s;
    }

    void parsesRules()
    {
        QFETCH(QString, text);
        QFETCH(bool, english);
        QFETCH(QString, described);
        QFETCH(QString, first);
        const dates::UiLocale locale = english ? dates::UiLocale::English : dates::UiLocale::Polish;
        WhenError error = WhenError::Empty;
        const std::optional<When> when = parseWhen(text, Now, locale, &error);
        QVERIFY2(when, qPrintable(text));
        QCOMPARE(error, WhenError::None);
        QVERIFY(when->isValid());
        QCOMPARE(describeWhen(*when, locale, Now.date()), described);
        QCOMPARE(next(*when, u"2026-11-22 10:00"_s), first);
        // Kept and read back as it was.
        QCOMPARE(whenFromJson(toJson(*when)), when);
    }

    void refuses_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<WhenError>("error");
        QTest::newRow("nothing") << u"  "_s << WhenError::Empty;
        QTest::newRow("nonsense") << u"kiedyś, jak będzie ładna pogoda"_s << WhenError::NotUnderstood;
        QTest::newRow("words after a date") << u"jutro o 8 i potem jeszcze"_s << WhenError::NotUnderstood;
        QTest::newRow("co 5 minut") << u"co 5 minut"_s << WhenError::TooOften;
        QTest::newRow("every minute") << u"every minute"_s << WhenError::TooOften;
        QTest::newRow("co 0 godzin") << u"co 0 godzin"_s << WhenError::TooOften;
        QTest::newRow("too long an interval") << u"co 800 godzin"_s << WhenError::TooOften;
        QTest::newRow("passed") << u"dziś o 8"_s << WhenError::Past;
        QTest::newRow("yesterday") << u"wczoraj o 20"_s << WhenError::Past;
        QTest::newRow("when done") << u"codziennie po wykonaniu"_s << WhenError::WhenDone;
    }

    void refuses()
    {
        QFETCH(QString, text);
        QFETCH(WhenError, error);
        WhenError got = WhenError::None;
        QVERIFY(!parseWhen(text, Now, dates::UiLocale::Polish, &got));
        QCOMPARE(got, error);
    }

    void monthWithoutDay()
    {
        // "co miesiąc" runs on the day it was set up, not wherever the runs go.
        const auto when = parseWhen(u"co miesiąc o 7"_s, Now);
        QVERIFY(when);
        QCOMPARE(when->rule.monthDay, std::optional<int>(22));
        QCOMPARE(next(*when, u"2026-11-22 10:00"_s), u"2026-12-22 07:00"_s);
        QCOMPARE(next(*when, u"2026-12-30 12:00"_s), u"2027-01-22 07:00"_s); // (a late run: back on the 22nd)
        // The 31st in a short month: its last day.
        const auto last = parseWhen(u"co miesiąc o 7"_s, QDateTime(QDate(2026, 1, 31), QTime(8, 0)));
        QVERIFY(last);
        QCOMPARE(next(*last, u"2026-01-31 08:00"_s), u"2026-02-28 07:00"_s);
    }

    void nextRuns_data()
    {
        QTest::addColumn<QString>("rule");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("expected");
        QTest::newRow("later today") << u"codziennie o 18"_s << u"2026-11-22 17:59"_s << u"2026-11-22 18:00"_s;
        QTest::newRow("at the minute: tomorrow") << u"codziennie o 18"_s << u"2026-11-22 18:00"_s
                                                 << u"2026-11-23 18:00"_s;
        QTest::newRow("Friday after Friday's run") << u"w piątki o 7"_s << u"2026-11-27 07:00"_s
                                                   << u"2026-12-04 07:00"_s;
        QTest::newRow("weekdays over the weekend") << u"w dni robocze o 8"_s << u"2026-11-27 08:00"_s
                                                   << u"2026-11-30 08:00"_s;
        QTest::newRow("weekdays: Saturday's catch-up") << u"w dni robocze o 8"_s << u"2026-11-28 11:00"_s
                                                       << u"2026-11-30 08:00"_s;
        QTest::newRow("every 2 days") << u"co 2 dni o 9"_s << u"2026-11-22 09:00"_s << u"2026-11-24 09:00"_s;
        QTest::newRow("New Year") << u"codziennie o 0:30"_s << u"2026-12-31 23:00"_s << u"2027-01-01 00:30"_s;
        QTest::newRow("interval over midnight") << u"co 3 godziny"_s << u"2026-11-22 22:00"_s
                                                << u"2026-11-23 01:00"_s;
    }

    void nextRuns()
    {
        QFETCH(QString, rule);
        QFETCH(QString, after);
        QFETCH(QString, expected);
        const auto when = parseWhen(rule, Now);
        QVERIFY2(when, qPrintable(rule));
        QCOMPARE(next(*when, after), expected);
    }

    void once()
    {
        const auto when = parseWhen(u"jutro o 8"_s, Now);
        QVERIFY(when);
        QCOMPARE(when->kind, Kind::Once);
        QCOMPARE(next(*when, u"2026-11-22 10:00"_s), u"2026-11-23 08:00"_s);
        QCOMPARE(next(*when, u"2026-11-23 08:00"_s), u"none"_s); // (it ran)
        QCOMPARE(next(*when, u"2026-11-25 10:00"_s), u"none"_s);
    }

    void missedRunsCatchUpOnce()
    {
        Schedule s;
        s.id = u"s1"_s;
        s.prompt = u"Co mam dziś do zrobienia?"_s;
        s.when = *parseWhen(u"codziennie o 7:30"_s, Now);
        s.created = at(u"2026-11-22 10:00"_s);
        QVERIFY(!s.isDue(Now));
        QVERIFY(!s.isDue(QDateTime(QDate(2026, 11, 23), QTime(7, 29))));
        QVERIFY(s.isDue(QDateTime(QDate(2026, 11, 23), QTime(7, 30))));
        // It ran on Monday; the app was closed until Saturday: one run due, not five.
        s.lastRun = at(u"2026-11-23 07:30"_s);
        const QDateTime saturday(QDate(2026, 11, 28), QTime(12, 5));
        QVERIFY(s.isDue(saturday));
        s.lastRun = schedule::wallClock(saturday); // (the runner notes the run's own time)
        QVERIFY(!s.isDue(saturday.addSecs(60)));
        QCOMPARE(s.next()->toString(), u"2026-11-29 07:30"_s);
        // Off: never due.
        s.enabled = false;
        QVERIFY(!s.isDue(QDateTime(QDate(2026, 12, 5), QTime(8, 0))));
        // A single run missed while the app was closed: due once at the next start, then done.
        Schedule one = s;
        one.enabled = true;
        one.lastRun = {};
        one.when = *parseWhen(u"jutro o 8"_s, Now);
        QVERIFY(one.isDue(QDateTime(QDate(2026, 11, 26), QTime(9, 0))));
        one.lastRun = at(u"2026-11-26 09:00"_s);
        QVERIFY(!one.isDue(QDateTime(QDate(2026, 12, 26), QTime(9, 0))));
        QVERIFY(!one.next());
    }

    void daylightSaving()
    {
        // 2026-03-29: 02:00 CET becomes 03:00 CEST — 02:30 never shows on the clock.
        const auto night = parseWhen(u"codziennie o 2:30"_s, QDateTime(QDate(2026, 3, 27), QTime(12, 0)));
        QVERIFY(night);
        QCOMPARE(next(*night, u"2026-03-28 02:30"_s), u"2026-03-29 02:30"_s);
        Schedule s;
        s.id = u"noc"_s;
        s.prompt = u"Podsumuj dzień"_s;
        s.when = *night;
        s.created = at(u"2026-03-28 12:00"_s);
        // The clock shows 01:59, then 03:00: due at the first minute after the gap.
        QVERIFY(!s.isDue(QDateTime(QDate(2026, 3, 29), QTime(1, 59))));
        const QDateTime afterGap = QDateTime(QDate(2026, 3, 29), QTime(1, 59)).addSecs(60);
        QCOMPARE(afterGap.time(), QTime(3, 0));
        QVERIFY(s.isDue(afterGap));
        // 2026-10-25: 03:00 CEST becomes 02:00 CET — 02:30 comes twice, it runs once.
        s.created = at(u"2026-10-24 12:00"_s);
        const QDateTime first = QDateTime(QDate(2026, 10, 25), QTime(2, 30)); // (the first of them)
        QVERIFY(s.isDue(first));
        s.lastRun = schedule::wallClock(first);
        const QDateTime second = first.addSecs(3600);
        QCOMPARE(second.time(), QTime(2, 30));
        QVERIFY(!s.isDue(second));
        QCOMPARE(s.next()->toString(), u"2026-10-26 02:30"_s);
        // An interval counts the hours that pass: 3 hours after 01:00 on the spring day is 05:00.
        const auto every = parseWhen(u"co 3 godziny"_s, Now);
        QVERIFY(every);
        QCOMPARE(next(*every, u"2026-03-29 01:00"_s), u"2026-03-29 05:00"_s);
        QCOMPARE(next(*every, u"2026-10-25 01:00"_s), u"2026-10-25 03:00"_s);
    }

    void keptOnTheDevice()
    {
        Schedule s;
        s.id = u"s7f3a"_s;
        s.title = u"Plan dnia"_s;
        s.prompt = u"Zaplanuj mi dzień: zadania, spotkania, przypomnienia."_s;
        s.partner = u"acp:claude"_s;
        s.whenText = u"w dni robocze o 7:30"_s;
        s.when = *parseWhen(s.whenText, Now);
        s.continues = true;
        s.conversation = u"AI/2026-11-23 Zaplanuj mi dzień.md"_s;
        s.created = at(u"2026-11-22 10:00"_s);
        s.lastRun = at(u"2026-11-23 07:30"_s);
        s.lastConversation = s.conversation;
        s.by = u"Claude Code"_s;
        s.parent = u"AI/2026-11-22 Plan tygodnia.md"_s;
        Schedule off = s;
        off.id = u"s2"_s;
        off.enabled = false;
        off.continues = false;
        off.conversation.clear();
        off.lastRun = {};
        off.when = *parseWhen(u"co 3 godziny"_s, Now);
        const QByteArray json = serializeSchedules({s, off});
        QCOMPARE(parseSchedules(json), (QList<Schedule>{s, off}));

        // Malformed rows left out, an id kept once; anything but an array: none.
        QJsonArray rows = QJsonDocument::fromJson(json).array();
        QJsonObject noPrompt = toJson(s);
        noPrompt.insert(u"id"_s, u"s3"_s);
        noPrompt.insert(u"prompt"_s, u"   "_s);
        QJsonObject badRule = toJson(s);
        badRule.insert(u"id"_s, u"s4"_s);
        badRule.insert(u"rule"_s, QJsonObject{{u"kind"_s, u"interval"_s}, {u"minutes"_s, 1}});
        QJsonObject badId = toJson(s);
        badId.insert(u"id"_s, u"../../etc"_s);
        QJsonObject noCreated = toJson(s);
        noCreated.insert(u"id"_s, u"s5"_s);
        noCreated.insert(u"created"_s, u"wczoraj"_s);
        rows << noPrompt << badRule << badId << noCreated << toJson(s) << u"tekst"_s << 7;
        QCOMPARE(parseSchedules(QJsonDocument(rows).toJson()), (QList<Schedule>{s, off}));
        QVERIFY(parseSchedules("{\"id\": \"s1\"}").isEmpty());
        QVERIFY(parseSchedules("nie json").isEmpty());
        // At most MaxSchedules.
        QJsonArray many;
        for (int i = 0; i < MaxSchedules + 5; ++i) {
            QJsonObject row = toJson(off);
            row.insert(u"id"_s, u"s%1"_s.arg(i));
            many << row;
        }
        QCOMPARE(parseSchedules(QJsonDocument(many).toJson()).size(), MaxSchedules);
    }

    // A webhook (docs/UI.md "Scheduled conversations" → "Webhooks"): its secret kept with the
    // schedule, a rule with no time of its own (only the webhook starts it), never due by itself.
    void webhooks()
    {
        Schedule s;
        s.id = u"s9"_s;
        s.prompt = u"Ktoś zadzwonił do drzwi: sprawdź kamerę."_s;
        s.when.kind = Kind::None;
        s.created = at(u"2026-11-22 10:00"_s);
        s.hook = QString(43, u'x');
        QVERIFY(s.when.isValid());
        QVERIFY(!s.next());
        QVERIFY(!s.isDue(Now.addYears(1)));
        QCOMPARE(describeWhen(s.when, dates::UiLocale::Polish), u"tylko przez webhook"_s);
        QCOMPARE(describeWhen(s.when, dates::UiLocale::English), u"only by its webhook"_s);
        QCOMPARE(toJson(s.when), (QJsonObject{{u"kind"_s, u"none"_s}}));
        QCOMPARE(whenFromJson(toJson(s.when)), s.when);
        s.lastHook = at(u"2026-11-23 18:05"_s);
        // A timed one with a webhook too; its runs by the webhook never move the rule.
        Schedule timed = s;
        timed.id = u"s10"_s;
        timed.whenText = u"codziennie o 7:30"_s;
        timed.when = *parseWhen(timed.whenText, Now);
        timed.hook = u"Ab-_0123456789Ab-_0123456789Ab-_0123456789A"_s;
        QCOMPARE(timed.next()->toString(), u"2026-11-23 07:30"_s);
        QCOMPARE(parseSchedules(serializeSchedules({s, timed})), (QList<Schedule>{s, timed}));
        // A secret of another shape is none; a rule with no time and no webhook never runs: left out.
        QJsonObject badHook = toJson(timed);
        badHook.insert(u"hook"_s, u"za krótki"_s);
        const QList<Schedule> read = parseSchedules(QJsonDocument(QJsonArray{badHook}).toJson());
        QCOMPARE(read.size(), 1);
        QVERIFY(read.first().hook.isEmpty());
        QJsonObject noHook = toJson(s);
        noHook.remove(u"hook"_s);
        QVERIFY(parseSchedules(QJsonDocument(QJsonArray{noHook}).toJson()).isEmpty());
        // Nothing written is no rule (the app takes "" as "only by its webhook" when it has one).
        WhenError error = WhenError::None;
        QVERIFY(!parseWhen(u""_s, Now, dates::UiLocale::Polish, &error));
        QCOMPARE(error, WhenError::Empty);
    }
};

QTEST_GUILESS_MAIN(TestSchedule)
#include "tst_schedule.moc"
