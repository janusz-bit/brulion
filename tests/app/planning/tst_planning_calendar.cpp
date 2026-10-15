// CalendarModel: month / week / agenda ranges for both week starts, Polish and English titles,
// entries per day (journals, due / scheduled / reminders / alarms / done, timed first), overdue
// tasks, recurring series, a month with a DST change, marks for the date picker, navigation and
// how the model updates (dataChanged instead of resets).
#include "calendarmodel.h"
#include "planningtest.h"

#include <QSignalSpy>
#include <QTest>

#include <cstdlib>
#include <ctime>

using namespace Qt::StringLiterals;
using brulion::VaultConfig;
using brulion::planning::CalendarModel;
using brulion::planning::test::makeIndex;

namespace {

QVariant role(const CalendarModel &m, int row, CalendarModel::Role r)
{
    return m.data(m.index(row, 0), r);
}

QVariantList itemsOn(const CalendarModel &m, const QString &date)
{
    const int row = m.rowForDate(date);
    return row < 0 ? QVariantList() : role(m, row, CalendarModel::ItemsRole).toList();
}

QStringList kindsOf(const QVariantList &items, const QString &key = u"variant"_s)
{
    QStringList out;
    for (const QVariant &v : items)
        out << v.toMap().value(key).toString();
    return out;
}

} // namespace

class TestCalendarModel : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Local wall-clock dates must not depend on the zone; a zone with DST makes sure of it.
        qputenv("TZ", "Europe/Warsaw");
        tzset();
    }

    void monthGridMondayStart()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        QCOMPARE(m.rowCount(), 42);
        QCOMPARE(m.rangeStart(), u"2026-09-28"_s); // Monday before 1 October (a Thursday)
        QCOMPARE(m.rangeEnd(), u"2026-11-08"_s);
        QCOMPARE(m.title(), u"Październik 2026"_s);
        QCOMPARE(m.anchorDate(), u"2026-10-15"_s);
        QVERIFY(m.showingToday());
        QCOMPARE(role(m, 0, CalendarModel::InMonthRole).toBool(), false);
        QCOMPARE(role(m, 0, CalendarModel::DayOfWeekRole).toInt(), 1);
        const int today = m.rowForDate(u"2026-10-15"_s);
        QCOMPARE(today, 17);
        QVERIFY(role(m, today, CalendarModel::IsTodayRole).toBool());
        QVERIFY(role(m, today, CalendarModel::IsSelectedRole).toBool());
        QVERIFY(role(m, today, CalendarModel::InMonthRole).toBool());
        QVERIFY(role(m, today - 1, CalendarModel::IsPastRole).toBool());
        QCOMPARE(role(m, today, CalendarModel::LabelRole).toString(), u"czw 15 paź"_s);
        QCOMPARE(role(m, today, CalendarModel::LongLabelRole).toString(), u"czwartek, 15 października"_s);
        QCOMPARE(role(m, today, CalendarModel::WeekNumberRole).toInt(), 42);
        QVERIFY(role(m, m.rowForDate(u"2026-10-17"_s), CalendarModel::IsWeekendRole).toBool());
    }

    void monthGridSundayStart()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(7);
        QCOMPARE(m.effectiveWeekStart(), 7);
        QCOMPARE(m.rangeStart(), u"2026-09-27"_s);
        QCOMPARE(role(m, 0, CalendarModel::DayOfWeekRole).toInt(), 7);
        // JavaScript's Sunday (0) is sanitised too; 0 itself means "the vault's setting".
        m.setWeekStart(0);
        QCOMPARE(m.effectiveWeekStart(), 1);
    }

    void weekStartFromVaultConfig()
    {
        VaultConfig config;
        config.weekStartsOn = Qt::Sunday;
        auto index = makeIndex({}, config);
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        QCOMPARE(m.effectiveWeekStart(), 7);
        QCOMPARE(m.rangeStart(), u"2026-09-27"_s);
    }

    void monthStartingOnWeekStart()
    {
        // February 2027 starts on a Monday: the grid starts with that day (no leading week).
        CalendarModel m;
        m.setToday(u"2027-02-10"_s);
        m.setWeekStart(1);
        QCOMPARE(m.rangeStart(), u"2027-02-01"_s);
        QCOMPARE(m.title(), u"Luty 2027"_s);
    }

    void englishTitles()
    {
        CalendarModel m;
        m.setLocale(u"en"_s);
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        QCOMPARE(m.title(), u"October 2026"_s);
        m.setMode(u"week"_s);
        QCOMPARE(m.title(), u"October 12–18, 2026"_s);
        m.setAnchorDate(u"2026-09-30"_s);
        QCOMPARE(m.title(), u"September 28 – October 4, 2026"_s);
        m.setAnchorDate(u"2026-12-31"_s);
        QCOMPARE(m.title(), u"December 28, 2026 – January 3, 2027"_s);
        QCOMPARE(role(m, 3, CalendarModel::LabelRole).toString(), u"Thu, Dec 31"_s);
    }

    void weekMode()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        m.setMode(u"week"_s);
        QCOMPARE(m.rowCount(), 7);
        QCOMPARE(m.rangeStart(), u"2026-10-12"_s);
        QCOMPARE(m.rangeEnd(), u"2026-10-18"_s);
        QCOMPARE(m.title(), u"12–18 października 2026"_s);
        QVERIFY(role(m, 0, CalendarModel::InMonthRole).toBool());
        m.setAnchorDate(u"2026-09-30"_s);
        QCOMPARE(m.title(), u"28 września – 4 października 2026"_s);
        m.setAnchorDate(u"2026-12-31"_s);
        QCOMPARE(m.title(), u"28 grudnia 2026 – 3 stycznia 2027"_s);
        m.setAnchorDate(u"2026-10-15"_s);
        QCOMPARE(m.shortTitle(), u"12–18 paź"_s);
        m.setAnchorDate(u"2026-09-30"_s);
        QCOMPARE(m.shortTitle(), u"28 wrz – 4 paź"_s);
        m.setAnchorDate(u"2026-12-31"_s);
        QCOMPARE(m.shortTitle(), u"28 gru – 3 sty 2027"_s);
        m.setMode(u"nonsense"_s);
        QCOMPARE(m.mode(), u"month"_s);
    }

    void entriesPerDay()
    {
        auto index = makeIndex({
                {u"journals/2026-10-15.md"_s, u"- Dzisiejszy wpis\n"_s},
                {u"Dom.md"_s,
                 u"- [ ] Zamówić farbę 📅 2026-10-16\n"
                 "- [ ] Zaległa rata 📅 2026-10-10\n"
                 "- [x] Odebrać paczkę 📅 2026-10-14 ✅ 2026-10-14\n"
                 "- [ ] Zerwać panele ⏳ 2026-10-16\n"
                 "- Spotkanie z ekipą ⏰ 2026-10-16 17:30\n"
                 "- Pobudka na pociąg ⏰! 2026-10-16 05:45\n"
                 "- [ ] Zadzwonić do **Ani** [[Kontakty|w sprawie]] remontu 📅 2026-10-16 08:15\n"_s},
        });
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        m.setVaultIndex(index.get());

        const int today = m.rowForDate(u"2026-10-15"_s);
        QVERIFY(role(m, today, CalendarModel::HasJournalRole).toBool());
        QCOMPARE(role(m, today, CalendarModel::JournalPathRole).toString(), u"journals/2026-10-15.md"_s);
        QCOMPARE(role(m, today, CalendarModel::ItemCountRole).toInt(), 0); // journals are not items
        QVERIFY(role(m, today, CalendarModel::KindsRole).toStringList().contains(u"journal"_s));

        // The 16th: timed first by time (alarm 05:45, due 08:15, reminder 17:30), then all-day.
        const QVariantList items = itemsOn(m, u"2026-10-16"_s);
        QCOMPARE(items.size(), 5);
        QCOMPARE(kindsOf(items, u"time"_s), (QStringList{u"05:45"_s, u"08:15"_s, u"17:30"_s, QString(), QString()}));
        QCOMPARE(kindsOf(items), (QStringList{u"alarm"_s, u"due"_s, u"reminder"_s, u"due"_s, u"scheduled"_s}));
        const QVariantMap alarm = items.at(0).toMap();
        QCOMPARE(alarm.value(u"icon"_s).toString(), u"alarm-clock"_s);
        QCOMPARE(alarm.value(u"minutes"_s).toInt(), 5 * 60 + 45);
        QCOMPARE(alarm.value(u"isTask"_s).toBool(), false);
        QCOMPARE(alarm.value(u"pageTitle"_s).toString(), u"Dom"_s);
        QCOMPARE(alarm.value(u"line"_s).toInt(), 5);
        // Titles are plain text (no Markdown, link display text).
        QCOMPARE(items.at(1).toMap().value(u"title"_s).toString(), u"Zadzwonić do Ani w sprawie remontu"_s);
        QCOMPARE(role(m, m.rowForDate(u"2026-10-16"_s), CalendarModel::TimedCountRole).toInt(), 3);

        // Overdue: open task due before today; a done task's due date is "done".
        const QVariantList tenth = itemsOn(m, u"2026-10-10"_s);
        QCOMPARE(kindsOf(tenth), QStringList{u"overdue"_s});
        QVERIFY(tenth.first().toMap().value(u"overdue"_s).toBool());
        const QVariantList fourteenth = itemsOn(m, u"2026-10-14"_s);
        QCOMPARE(kindsOf(fourteenth), (QStringList{u"done"_s, u"done"_s}));
        QCOMPARE(kindsOf(fourteenth, u"kind"_s), (QStringList{u"due"_s, u"done"_s}));

        QCOMPARE(m.overdueCount(), 1);
        const QVariantMap overdue = m.overdue().first().toMap();
        QCOMPARE(overdue.value(u"title"_s).toString(), u"Zaległa rata"_s);
        QCOMPARE(overdue.value(u"dueLabel"_s).toString(), u"sob 10 paź"_s);
        QCOMPARE(overdue.value(u"line"_s).toInt(), 1);

        // Week mode hours follow the timed entries.
        m.setMode(u"week"_s);
        QCOMPARE(m.firstHour(), 5);
        QCOMPARE(m.lastHour(), 17);
        QCOMPARE(m.itemCount(), 7); // the 14th and the 16th are in this week
    }

    void dayItemsAndMarks()
    {
        auto index = makeIndex({
                {u"journals/2026-10-15.md"_s, u"- wpis ⏰ 2026-10-15 12:00\n"_s},
                {u"Dom.md"_s, u"- [ ] Zamówić farbę 📅 2026-10-15\n- [ ] Stare 📅 2026-10-01\n"_s},
        });
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        const QVariantList day = m.dayItems(u"2026-10-15"_s);
        QCOMPARE(kindsOf(day, u"kind"_s), (QStringList{u"journal"_s, u"reminder"_s, u"due"_s}));
        QCOMPARE(day.first().toMap().value(u"path"_s).toString(), u"journals/2026-10-15.md"_s);
        // Any day, also outside the range.
        QCOMPARE(m.dayItems(u"2027-01-01"_s).size(), 0);
        QCOMPARE(m.dayItems(u"bogus"_s).size(), 0);

        const QVariantMap marks = m.marks(u"2026-10-01"_s, u"2026-10-31"_s);
        QStringList kinds = marks.value(u"2026-10-15"_s).toStringList();
        kinds.sort();
        QCOMPARE(kinds, (QStringList{u"due"_s, u"journal"_s, u"reminder"_s}));
        QCOMPARE(marks.value(u"2026-10-01"_s).toStringList(), QStringList{u"overdue"_s});
        QVERIFY(m.marks(u"2026-10-31"_s, u"2026-10-01"_s).isEmpty());
    }

    void agendaMode()
    {
        auto index = makeIndex({
                {u"Dom.md"_s, u"- [ ] A 📅 2026-10-13\n- [ ] B 📅 2026-10-20\n- [ ] C 📅 2026-11-20\n"_s},
        });
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        m.setVaultIndex(index.get());
        m.setMode(u"agenda"_s);
        QCOMPARE(m.rangeStart(), u"2026-10-12"_s);
        QCOMPARE(m.rangeEnd(), u"2026-11-08"_s);
        // Days with entries, plus today (even when empty).
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(role(m, 0, CalendarModel::DateRole).toString(), u"2026-10-13"_s);
        QCOMPARE(role(m, 1, CalendarModel::DateRole).toString(), u"2026-10-15"_s);
        QCOMPARE(role(m, 1, CalendarModel::ItemCountRole).toInt(), 0);
        QCOMPARE(role(m, 2, CalendarModel::DateRole).toString(), u"2026-10-20"_s);
        m.next();
        QCOMPARE(m.rangeStart(), u"2026-11-09"_s);
        QCOMPARE(m.rowCount(), 1); // only the 20th of November; today is outside
        QCOMPARE(m.title(), u"9 listopada – 6 grudnia 2026"_s);
    }

    void navigation()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setAnchorDate(u"2026-01-31"_s);
        m.next();
        QCOMPARE(m.anchorDate(), u"2026-02-28"_s); // clamped to the month's length
        m.previous();
        QCOMPARE(m.anchorDate(), u"2026-01-28"_s);
        m.goToday();
        QCOMPARE(m.anchorDate(), u"2026-10-15"_s);
        m.setMode(u"week"_s);
        m.next();
        QCOMPARE(m.anchorDate(), u"2026-10-22"_s);
        QVERIFY(!m.showingToday());
        m.moveAnchor(-8);
        QCOMPARE(m.anchorDate(), u"2026-10-14"_s);
        m.setAnchorDate(u"not a date"_s);
        QCOMPARE(m.anchorDate(), u"2026-10-14"_s);
    }

    void selectionInsideRangeDoesNotReset()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&m, &QAbstractItemModel::dataChanged);
        m.setAnchorDate(u"2026-10-20"_s);
        QCOMPARE(resets.count(), 0);
        QCOMPARE(changes.count(), 2); // the old and the new selected day
        QVERIFY(role(m, m.rowForDate(u"2026-10-20"_s), CalendarModel::IsSelectedRole).toBool());
        QVERIFY(!role(m, m.rowForDate(u"2026-10-15"_s), CalendarModel::IsSelectedRole).toBool());
        // Another month: a new range.
        m.setAnchorDate(u"2026-11-20"_s);
        QCOMPARE(resets.count(), 1);
        QCOMPARE(m.title(), u"Listopad 2026"_s);
    }

    void indexChangesUpdateRows()
    {
        auto index = makeIndex({{u"Dom.md"_s, u"- [ ] A 📅 2026-10-16\n"_s}});
        CalendarModel m;
        m.setRefreshDelay(0);
        m.setToday(u"2026-10-15"_s);
        m.setVaultIndex(index.get());
        QCOMPARE(itemsOn(m, u"2026-10-16"_s).size(), 1);
        QSignalSpy resets(&m, &QAbstractItemModel::modelReset);
        QSignalSpy changes(&m, &QAbstractItemModel::dataChanged);
        index->update(u"Dom.md"_s, u"- [ ] A 📅 2026-10-16\n- [ ] B 📅 2026-10-16\n"_s);
        QTRY_COMPARE(itemsOn(m, u"2026-10-16"_s).size(), 2);
        QCOMPARE(resets.count(), 0);
        QVERIFY(changes.count() >= 1);
        // The index goes away (vault closed): empty days, no crash.
        index.reset();
        m.refresh();
        QCOMPARE(itemsOn(m, u"2026-10-16"_s).size(), 0);
        QCOMPARE(m.rowCount(), 42);
    }

    void recurringEntries()
    {
        auto index = makeIndex({
                {u"Dom.md"_s,
                 u"- [ ] Wynieść śmieci 🔁 every week on Monday 📅 2026-10-05\n"
                 "- Czynsz ⏰ 2026-10-01 08:00 🔁 co miesiąc\n"
                 "- [x] Zrobione cykliczne 🔁 every day 📅 2026-10-12 ✅ 2026-10-12\n"_s},
        });
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        m.setWeekStart(1);
        m.setVaultIndex(index.get());
        // Weekly: 5, 12, 19, 26 October and 2 November are in the grid.
        for (const QString &d : {u"2026-10-05"_s, u"2026-10-12"_s, u"2026-10-19"_s, u"2026-10-26"_s, u"2026-11-02"_s}) {
            const QVariantList items = itemsOn(m, d);
            QVERIFY2(!items.isEmpty(), qPrintable(d));
            QVERIFY(items.first().toMap().value(u"recurring"_s).toBool());
        }
        // Only the written (current) instance is overdue, not the virtual 12 October one.
        QCOMPARE(kindsOf(itemsOn(m, u"2026-10-05"_s)), QStringList{u"overdue"_s});
        QCOMPARE(kindsOf(itemsOn(m, u"2026-10-12"_s)).first(), u"due"_s);
        QCOMPARE(m.overdueCount(), 1);
        // Monthly reminder with a Polish rule: 1 October and 1 November.
        QCOMPARE(kindsOf(itemsOn(m, u"2026-10-01"_s)), QStringList{u"reminder"_s});
        QCOMPARE(kindsOf(itemsOn(m, u"2026-11-01"_s)), QStringList{u"reminder"_s});
        // A done recurring task is not expanded: nothing on the 13th.
        QVERIFY(itemsOn(m, u"2026-10-13"_s).isEmpty());
    }

    void dstMonths_data()
    {
        QTest::addColumn<QString>("anchor");
        QTest::addColumn<QString>("dstDay");
        QTest::newRow("spring forward") << u"2026-03-10"_s << u"2026-03-29"_s;
        QTest::newRow("fall back") << u"2026-10-10"_s << u"2026-10-25"_s;
    }

    void dstMonths()
    {
        QFETCH(QString, anchor);
        QFETCH(QString, dstDay);
        // 02:30 does not exist on the spring day and exists twice on the autumn day: a wall-clock
        // reminder still shows once, on its day, at its written time.
        auto index = makeIndex({
                {u"Zegar.md"_s, u"- Nocna zmiana ⏰ %1 02:30\n- [ ] Termin 📅 %1\n"_s.arg(dstDay)},
        });
        CalendarModel m;
        m.setToday(anchor);
        m.setWeekStart(1);
        m.setVaultIndex(index.get());
        QCOMPARE(m.rowCount(), 42);
        QDate expected = QDate::fromString(m.rangeStart(), Qt::ISODate);
        for (int row = 0; row < m.rowCount(); ++row) {
            QCOMPARE(role(m, row, CalendarModel::DateRole).toString(), expected.toString(Qt::ISODate));
            expected = expected.addDays(1);
        }
        const QVariantList items = itemsOn(m, dstDay);
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.first().toMap().value(u"time"_s).toString(), u"02:30"_s);
        QCOMPARE(items.first().toMap().value(u"minutes"_s).toInt(), 150);
        // Week mode around the change: 7 days, Monday to Sunday.
        m.setMode(u"week"_s);
        m.setAnchorDate(dstDay);
        QCOMPARE(m.rowCount(), 7);
        QCOMPARE(role(m, 6, CalendarModel::DateRole).toString(), dstDay); // both changes are on a Sunday
    }

    void emptyIndexAndNoIndex()
    {
        CalendarModel none;
        QCOMPARE(none.rowCount(), 42);
        QCOMPARE(none.overdueCount(), 0);
        auto index = makeIndex({});
        CalendarModel m;
        m.setVaultIndex(index.get());
        QCOMPARE(m.itemCount(), 0);
        m.setMode(u"agenda"_s);
        QCOMPARE(m.rowCount(), 1); // today only
    }

    void unpinnedAnchorFollowsToday()
    {
        CalendarModel m;
        m.setToday(u"2026-10-15"_s);
        QSignalSpy anchor(&m, &CalendarModel::anchorDateChanged);
        m.setToday(u"2026-10-14"_s);
        QCOMPARE(anchor.count(), 1);
        QCOMPARE(m.anchorDate(), u"2026-10-14"_s);
        // A chosen day stays.
        m.setAnchorDate(u"2026-10-20"_s);
        anchor.clear();
        m.setToday(u"2026-10-13"_s);
        QCOMPARE(anchor.count(), 0);
        QCOMPARE(m.anchorDate(), u"2026-10-20"_s);
    }

    void clockWhenTodayIsEmpty()
    {
        CalendarModel m;
        m.setClock([] { return QDate(2030, 5, 20); });
        QCOMPARE(m.anchorDate(), u"2030-05-20"_s);
        QCOMPARE(m.title(), u"Maj 2030"_s);
    }
};

QTEST_MAIN(TestCalendarModel)
#include "tst_planning_calendar.moc"
