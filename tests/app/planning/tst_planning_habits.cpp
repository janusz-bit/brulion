// HabitListModel (src/app/planning/habitlistmodel.h): rows per definition with today's check,
// streaks, rates, period progress and the month grid; the month moving every grid; weekday heads
// and offsets by weekStart; Polish and English labels; problems (a schedule not understood, a name
// defined twice); incremental updates (an edit of one daily note computes only its habit's row).
#include "habitlistmodel.h"
#include "planningtest.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::planning;
using brulion::planning::test::makeIndex;

class TestPlanningHabits : public QObject {
    Q_OBJECT

    static test::Files files()
    {
        return {
                {u"Nawyki.md"_s, u"- Medytacja\n  habit:: codziennie\n- Bieganie\n  habit:: pn, śr, pt\n"
                                 "- Czytanie\n  habit:: 3 razy w tygodniu\n- Coś\n  habit:: kiedy mi się zachce\n"_s},
                {u"Zzz/Inne.md"_s, u"- medytacja\n  habit:: co 2 dni\n"_s},
                {u"journals/2026-11-02.md"_s,
                 u"- [x] Medytacja #nawyk\n- [x] Bieganie #nawyk\n- [x] Czytanie #nawyk\n"_s},
                {u"journals/2026-11-03.md"_s, u"- [x] Medytacja #nawyk\n- [x] Czytanie #nawyk\n"_s},
                {u"journals/2026-11-04.md"_s,
                 u"- [ ] Medytacja #nawyk\n- [-] Bieganie #nawyk\n- [x] Spacer #nawyk\n"_s},
        };
    }

    static void setUp(HabitListModel &m, VaultIndex *index)
    {
        m.setRefreshDelay(0);
        m.setToday(u"2026-11-04"_s);
        m.setVaultIndex(index);
    }

private Q_SLOTS:
    void rows()
    {
        auto index = makeIndex(files());
        HabitListModel m;
        setUp(m, index.get());
        QCOMPARE(m.rowCount(), 5);
        const QVariantMap med = m.get(m.rowOfKey(u"medytacja"_s));
        QCOMPARE(med.value(u"name"_s).toString(), u"Medytacja"_s);
        QCOMPARE(med.value(u"scheduleLabel"_s).toString(), u"codziennie"_s);
        QCOMPARE(med.value(u"todayMark"_s).toString(), u"open"_s);
        QVERIFY(med.value(u"todayRecorded"_s).toBool());
        QVERIFY(med.value(u"dueToday"_s).toBool());
        QCOMPARE(med.value(u"currentStreak"_s).toInt(), 2);
        QCOMPARE(med.value(u"streakLabel"_s).toString(), u"2 day(s)"_s); // no translator loaded
        QCOMPARE(med.value(u"rateLabel"_s).toString(), u"100%"_s);
        QCOMPARE(med.value(u"path"_s).toString(), u"Nawyki.md"_s);
        QCOMPARE(med.value(u"line"_s).toInt(), 0);
        QVERIFY(!med.value(u"isPage"_s).toBool());

        const QVariantMap run = m.get(m.rowOfKey(u"bieganie"_s));
        QCOMPARE(run.value(u"todayMark"_s).toString(), u"skipped"_s);
        QVERIFY(!run.value(u"dueToday"_s).toBool());
        QCOMPARE(run.value(u"currentStreak"_s).toInt(), 1);

        const QVariantMap read = m.get(m.rowOfKey(u"czytanie"_s));
        QCOMPARE(read.value(u"periodDone"_s).toInt(), 2);
        QCOMPARE(read.value(u"periodTarget"_s).toInt(), 3);
        QCOMPARE(read.value(u"periodLabel"_s).toString(), u"2 of 3 this week"_s);
        QCOMPARE(read.value(u"streakUnit"_s).toString(), u"weeks"_s);

        const QVariantMap bad = m.get(3);
        QCOMPARE(bad.value(u"problem"_s).toString(), u"schedule"_s);
        QVERIFY(!bad.value(u"valid"_s).toBool());
        const QVariantMap dup = m.get(4);
        QCOMPARE(dup.value(u"problem"_s).toString(), u"duplicate"_s);
        QCOMPARE(dup.value(u"path"_s).toString(), u"Zzz/Inne.md"_s);
        QCOMPARE(m.rowOfKey(u"medytacja"_s), 0); // the first definition, not the duplicate

        QCOMPARE(m.problemCount(), 2);
        QCOMPARE(m.doneTodayCount(), 0);
        QCOMPARE(m.dueCount(), 2); // Medytacja, Czytanie (Bieganie skipped)
        QCOMPARE(m.habitsPage(), u"Nawyki.md"_s);
        QCOMPARE(m.undefinedNames(), QStringList{u"spacer"_s});
        QVERIFY(m.hasHabit(u"MEDYTACJA"_s));
        QVERIFY(!m.hasHabit(u"Spacer"_s));
    }

    void monthGrid()
    {
        auto index = makeIndex(files());
        HabitListModel m;
        m.setLocale(u"pl"_s);
        setUp(m, index.get());
        QVERIFY(m.currentMonth());
        QCOMPARE(m.monthLabel(), u"listopad 2026"_s);
        QCOMPARE(m.monthOffset(), 6); // 1 Nov 2026 is a Sunday, weeks from Monday
        QCOMPARE(m.weekdayNames().first(), u"pn"_s);
        const QVariantList days = m.get(0).value(u"days"_s).toList();
        QCOMPARE(days.size(), 30);
        QCOMPARE(days[1].toMap().value(u"state"_s).toString(), u"done"_s);
        QCOMPARE(days[3].toMap().value(u"state"_s).toString(), u"due"_s);
        QVERIFY(days[3].toMap().value(u"today"_s).toBool());
        QCOMPARE(days[4].toMap().value(u"state"_s).toString(), u"future"_s);
        QCOMPARE(days[0].toMap().value(u"state"_s).toString(), u"outside"_s);

        m.setWeekStart(7);
        QCOMPARE(m.monthOffset(), 0);
        QCOMPARE(m.weekdayNames().first(), u"nd"_s);

        QSignalSpy spy(&m, &HabitListModel::monthChanged);
        m.previousMonth();
        QCOMPARE(m.month(), u"2026-10"_s);
        QVERIFY(!m.currentMonth());
        QCOMPARE(m.get(0).value(u"days"_s).toList().size(), 31);
        m.nextMonth();
        m.nextMonth();
        QCOMPARE(m.month(), u"2026-12"_s);
        m.thisMonth();
        QVERIFY(m.currentMonth());
        QVERIFY(spy.count() >= 4);
        // Across the year.
        m.setMonth(u"2027-01"_s);
        m.previousMonth();
        QCOMPARE(m.month(), u"2026-12"_s);
        m.setMonth(u"bzdura"_s);
        QVERIFY(m.currentMonth());
    }

    void incremental()
    {
        auto index = makeIndex(files());
        HabitListModel m;
        setUp(m, index.get());
        QSignalSpy changed(&m, &QAbstractItemModel::dataChanged);
        QSignalSpy reset(&m, &QAbstractItemModel::modelReset);
        // Ticking Medytacja in today's note: its row (and its duplicate definition's) computed again.
        index->update(u"journals/2026-11-04.md"_s,
                      u"- [x] Medytacja #nawyk ✅ 2026-11-04\n- [-] Bieganie #nawyk\n- [x] Spacer #nawyk\n"_s);
        m.refresh();
        QCOMPARE(m.lastRecomputed(), 2);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(reset.count(), 0);
        QCOMPARE(m.get(0).value(u"todayMark"_s).toString(), u"done"_s);
        QCOMPARE(m.get(0).value(u"currentStreak"_s).toInt(), 3);
        QCOMPARE(m.doneTodayCount(), 1);
        // An unrelated page: nothing.
        index->update(u"Projekt.md"_s, u"- coś\n"_s);
        m.refresh();
        QCOMPARE(m.lastRecomputed(), 0);
        // A new habit: the rows are reset.
        index->update(u"Zzz/Inne.md"_s, u"- Joga\n  habit:: codziennie\n"_s);
        m.refresh();
        QCOMPARE(reset.count(), 1);
        QCOMPARE(m.rowCount(), 5);
        QVERIFY(m.hasHabit(u"Joga"_s));
        // Another day: every row.
        m.setToday(u"2026-11-05"_s);
        QCOMPARE(m.lastRecomputed(), 5);
        QCOMPARE(m.get(0).value(u"currentStreak"_s).toInt(), 3);
        QVERIFY(m.get(0).value(u"dueToday"_s).toBool());
        // No index: no rows.
        m.setVaultIndex(nullptr);
        QCOMPARE(m.rowCount(), 0);
    }

    void describe()
    {
        HabitListModel m;
        m.setLocale(u"pl"_s);
        QCOMPARE(m.describeSchedule(u"3x/tydz"_s), u"3 razy w tygodniu"_s);
        QCOMPARE(m.describeSchedule(QString()), u"codziennie"_s);
        QVERIFY(m.describeSchedule(u"co drugi wtorek"_s).isEmpty());
        m.setLocale(u"en"_s);
        QCOMPARE(m.describeSchedule(u"pn, pt"_s), u"Mon, Fri"_s);
    }
};

QTEST_MAIN(TestPlanningHabits)
#include "tst_planning_habits.moc"
