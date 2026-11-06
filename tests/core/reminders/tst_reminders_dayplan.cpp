// dayPlan(): one day's due / scheduled / reminder entries, one row per block (the earliest time,
// all flags), timed rows first, journals / start / done dates left out.
#include "reminders/reminders.h"
#include "reminderstest.h"

#include <QTest>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

namespace {

/// "17:30 Odebrać paczkę [due reminder !] (todo) 🔁"
QString describe(const DayPlanEntry &e)
{
    QStringList flags;
    if (e.due)
        flags << u"due"_s;
    if (e.scheduled)
        flags << u"scheduled"_s;
    if (e.reminder)
        flags << u"reminder"_s;
    if (e.alarm)
        flags << u"!"_s;
    QString s =
            (e.time ? e.time->toString(u"HH:mm"_s) : u"--:--"_s) + u' ' + e.title + u" ["_s + flags.join(u' ') + u']';
    if (e.status)
        s += *e.status == TaskStatus::Done        ? u" (done)"_s
             : *e.status == TaskStatus::Cancelled ? u" (cancelled)"_s
                                                  : u" (open)"_s;
    if (e.recurring)
        s += u" 🔁"_s;
    return s;
}

QStringList planOf(const Files &files, QDate day)
{
    VaultIndex index;
    fill(index, files);
    QStringList out;
    for (const DayPlanEntry &e : dayPlan(calendarItems(index, day.addDays(-1), day.addDays(1)), day))
        out.append(describe(e));
    return out;
}

} // namespace

class TestRemindersDayPlan : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void day_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("empty") << QString() << QStringList();
        QTest::newRow("only a journal and a start date")
                << lines({u"- notatka"_s, u"- [ ] Malowanie 🛫 2026-10-14"_s}) << QStringList();
        QTest::newRow("due and reminder of one task: one row at the reminder")
                << u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30"_s
                << QStringList{u"17:30 Odebrać paczkę [due reminder] (open)"_s};
        QTest::newRow("timed first, by time; all-day after in document order")
                << lines({u"- [ ] Zakupy 📅 2026-10-14"_s, u"- ⏰! 2026-10-14 06:30 Pobudka"_s,
                          u"- [ ] Spotkanie z ekipą 📅 2026-10-14 18:00"_s, u"- [ ] Faktura ⏳ 2026-10-14"_s,
                          u"- ⏰ 2026-10-14 09:15 Zadzwonić do mamy"_s})
                << QStringList{u"06:30 Pobudka [reminder !]"_s, u"09:15 Zadzwonić do mamy [reminder]"_s,
                               u"18:00 Spotkanie z ekipą [due] (open)"_s, u"--:-- Zakupy [due] (open)"_s,
                               u"--:-- Faktura [scheduled] (open)"_s};
        QTest::newRow("two reminders on one block: the earlier time")
                << u"- [ ] Leki ⏰ 2026-10-14 20:00 ⏰ 2026-10-14 08:00"_s
                << QStringList{u"08:00 Leki [reminder] (open)"_s};
        QTest::newRow("other days left out")
                << lines({u"- [ ] Wczoraj 📅 2026-10-13"_s, u"- [ ] Jutro 📅 2026-10-15 ⏰ 2026-10-15 10:00"_s,
                          u"- [ ] Dziś ⏳ 2026-10-14 📅 2026-10-15"_s})
                << QStringList{u"--:-- Dziś [scheduled] (open)"_s};
        QTest::newRow("a done task stays, done dates do not") << u"- [x] Kupić taśmę 📅 2026-10-14 ✅ 2026-10-14"_s
                                                              << QStringList{u"--:-- Kupić taśmę [due] (done)"_s};
        QTest::newRow("a series on the day") << u"- [ ] Podlać kwiaty 🔁 every day ⏰ 2026-10-10 07:45"_s
                                             << QStringList{u"07:45 Podlać kwiaty [reminder] (open) 🔁"_s};
        QTest::newRow("malformed dates are no entries")
                << lines({u"- [ ] Zły termin 📅 2026-13-40"_s, u"- ⏰ jutro"_s}) << QStringList();
    }
    void day()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        QCOMPARE(planOf({{u"Notatka.md"_s, content}}, QDate(2026, 10, 14)), expected);
    }

    // Blocks of different notes on the same line are different rows.
    void sameLineInTwoNotes()
    {
        const QStringList plan =
                planOf({{u"A.md"_s, u"- [ ] Ąę 📅 2026-10-14 09:00"_s}, {u"B.md"_s, u"- [ ] Źż ⏰ 2026-10-14 09:00"_s}},
                       QDate(2026, 10, 14));
        QCOMPARE(plan.size(), 2);
    }

    // Items of the day given directly (no index): an invalid day gives nothing, the merge keeps
    // the first entry's title and takes a status from a later one.
    void direct()
    {
        CalendarItem reminder;
        reminder.date = QDate(2026, 10, 14);
        reminder.time = QTime(17, 30);
        reminder.kind = CalendarItem::Reminder;
        reminder.title = u"Odebrać paczkę"_s;
        reminder.path = u"journals/2026-10-14.md"_s;
        reminder.alarm = true;
        CalendarItem due = reminder;
        due.kind = CalendarItem::Due;
        due.time.reset();
        due.alarm = false;
        due.title = u"inny tytuł"_s;
        due.status = TaskStatus::Doing;
        const QList<DayPlanEntry> plan = dayPlan({reminder, due}, QDate(2026, 10, 14));
        QCOMPARE(plan.size(), 1);
        QCOMPARE(plan.first().title, u"Odebrać paczkę"_s);
        QCOMPARE(plan.first().time, QTime(17, 30));
        QVERIFY(plan.first().alarm && plan.first().due && plan.first().reminder && !plan.first().scheduled);
        QCOMPARE(plan.first().status, TaskStatus::Doing);
        QVERIFY(dayPlan({reminder, due}, QDate()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRemindersDayPlan)
#include "tst_reminders_dayplan.moc"
