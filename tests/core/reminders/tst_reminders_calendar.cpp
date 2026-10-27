// calendarItems(): journals, task dates (with recurring expansion), done dates and reminders,
// sorted by date, timed items first.
#include "reminders/reminders.h"
#include "reminderstest.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

namespace {

QString kindName(CalendarItem::Kind kind)
{
    switch (kind) {
    case CalendarItem::Journal: return u"journal"_s;
    case CalendarItem::Due: return u"due"_s;
    case CalendarItem::Scheduled: return u"scheduled"_s;
    case CalendarItem::Start: return u"start"_s;
    case CalendarItem::Reminder: return u"reminder"_s;
    case CalendarItem::Done: return u"done"_s;
    }
    return {};
}

QString statusName(TaskStatus status)
{
    switch (status) {
    case TaskStatus::Todo: return u"todo"_s;
    case TaskStatus::Doing: return u"doing"_s;
    case TaskStatus::Done: return u"done"_s;
    case TaskStatus::Cancelled: return u"cancelled"_s;
    case TaskStatus::Deferred: return u"deferred"_s;
    }
    return {};
}

/// "2026-10-14 17:30 reminder Title (todo) 🔁 !"
QString describe(const CalendarItem &item)
{
    QString s = item.date.toString(u"yyyy-MM-dd"_s);
    if (item.time)
        s += u' ' + item.time->toString(u"HH:mm"_s);
    s += u' ' + kindName(item.kind) + u' ' + item.title;
    if (item.status)
        s += u" ("_s + statusName(*item.status) + u')';
    if (item.recurring)
        s += u" 🔁"_s;
    if (item.alarm)
        s += u" !"_s;
    return s;
}

QStringList describe(const QList<CalendarItem> &items)
{
    QStringList out;
    for (const CalendarItem &item : items)
        out.append(describe(item));
    return out;
}

Files octoberVault()
{
    return {
            {u"journals/2026-10-12.md"_s, u"- Spotkanie zespołu #praca"_s},
            {u"journals/2026-10-14.md"_s, lines({u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30"_s,
                                                 u"- [x] Zadzwonić do hydraulika 📅 2026-10-13 ✅ 2026-10-14"_s,
                                                 u"- ⏰! 2026-10-15 06:30 Pobudka na lotnisko"_s})},
            {u"journals/2026-11-02.md"_s, u"- listopad"_s},
            {u"Projekty/Dom.md"_s, lines({u"- [/] Malowanie salonu 📅 2026-10-16 ⏳ 2026-10-15 🛫 2026-10-14"_s,
                                          u"- [ ] Spotkanie z [[Jan Kowalski|Jankiem]] 📅 2026-10-20 18:00"_s,
                                          u"- [ ] Sprzątanie 🔁 every week 📅 2026-10-03"_s,
                                          u"- [x] Stara seria 🔁 every day 📅 2026-10-05 ✅ 2026-10-05"_s,
                                          u"- [-] Anulowane 📅 2026-10-22"_s})},
            {u"Zdrowie.md"_s, lines({u"- [ ] Leki 🔁 every day ⏰ 2026-10-28 08:00"_s, u"\t- ⏰ 2026-10-29 08:10"_s})},
    };
}

QList<CalendarItem> calendarOf(const Files &files, QDate from, QDate to)
{
    VaultIndex index;
    fill(index, files);
    return calendarItems(index, from, to);
}

QList<CalendarItem> calendarOf(const QString &content, QDate from = QDate(2026, 10, 1), QDate to = QDate(2026, 10, 31))
{
    return calendarOf({{u"Notatka.md"_s, content}}, from, to);
}

} // namespace

class TestRemindersCalendar : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void month()
    {
        const auto items = calendarOf(octoberVault(), QDate(2026, 10, 1), QDate(2026, 10, 31));
        const QStringList expected = {
                u"2026-10-03 due Sprzątanie (todo) 🔁"_s,
                u"2026-10-05 due Stara seria (done) 🔁"_s,
                u"2026-10-05 done Stara seria (done) 🔁"_s,
                u"2026-10-10 due Sprzątanie (todo) 🔁"_s,
                u"2026-10-12 journal 2026-10-12"_s,
                u"2026-10-13 due Zadzwonić do hydraulika (done)"_s,
                u"2026-10-14 17:30 reminder Odebrać paczkę (todo)"_s,
                u"2026-10-14 journal 2026-10-14"_s,
                u"2026-10-14 start Malowanie salonu (doing)"_s,
                u"2026-10-14 due Odebrać paczkę (todo)"_s,
                u"2026-10-14 done Zadzwonić do hydraulika (done)"_s,
                u"2026-10-15 06:30 reminder Pobudka na lotnisko !"_s,
                u"2026-10-15 scheduled Malowanie salonu (doing)"_s,
                u"2026-10-16 due Malowanie salonu (doing)"_s,
                u"2026-10-17 due Sprzątanie (todo) 🔁"_s,
                u"2026-10-20 18:00 due Spotkanie z Jankiem (todo)"_s,
                u"2026-10-22 due Anulowane (cancelled)"_s,
                u"2026-10-24 due Sprzątanie (todo) 🔁"_s,
                u"2026-10-28 08:00 reminder Leki (todo) 🔁"_s,
                u"2026-10-29 08:00 reminder Leki (todo) 🔁"_s,
                u"2026-10-29 08:10 reminder Leki (todo)"_s,
                u"2026-10-30 08:00 reminder Leki (todo) 🔁"_s,
                u"2026-10-31 08:00 reminder Leki (todo) 🔁"_s,
                u"2026-10-31 due Sprzątanie (todo) 🔁"_s,
        };
        QCOMPARE(describe(items), expected);
    }

    void locations()
    {
        const auto items = calendarOf(octoberVault(), QDate(2026, 10, 14), QDate(2026, 10, 15));
        QCOMPARE(items.size(), 7);
        const CalendarItem &reminder = items.at(0);
        QCOMPARE(reminder.kind, CalendarItem::Reminder);
        QCOMPARE(reminder.path, u"journals/2026-10-14.md"_s);
        QCOMPARE(reminder.line, 0);
        const CalendarItem &journal = items.at(1);
        QCOMPARE(journal.kind, CalendarItem::Journal);
        QCOMPARE(journal.line, 0);
        QVERIFY(!journal.status);
        QVERIFY(!journal.time);
        const CalendarItem &done = items.at(4);
        QCOMPARE(done.kind, CalendarItem::Done);
        QCOMPARE(done.line, 1);
        QCOMPARE(items.at(5).line, 2); // the alarm
        QCOMPARE(items.at(6).path, u"Projekty/Dom.md"_s);
        QCOMPARE(items.at(6).line, 0);
    }

    void rangeLimits()
    {
        // Inclusive at both ends.
        QCOMPARE(describe(calendarOf(octoberVault(), QDate(2026, 10, 12), QDate(2026, 10, 12))),
                 QStringList({u"2026-10-12 journal 2026-10-12"_s}));
        QCOMPARE(calendarOf(octoberVault(), QDate(2026, 10, 3), QDate(2026, 10, 5)).size(), 3);
        QCOMPARE(describe(calendarOf(octoberVault(), QDate(2026, 11, 1), QDate(2026, 11, 30))).first(2),
                 QStringList({u"2026-11-01 08:00 reminder Leki (todo) 🔁"_s,
                              u"2026-11-02 08:00 reminder Leki (todo) 🔁"_s}));
        QVERIFY(calendarOf(octoberVault(), QDate(2026, 10, 31), QDate(2026, 10, 1)).isEmpty());
        QVERIFY(calendarOf(octoberVault(), QDate(), QDate(2026, 10, 1)).isEmpty());
        QVERIFY(calendarOf(octoberVault(), QDate(2026, 10, 1), QDate()).isEmpty());
        QVERIFY(calendarOf(Files(), QDate(2026, 10, 1), QDate(2026, 10, 31)).isEmpty());
    }

    void recurringExpansion_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("deferred expands")
                << u"- [>] Odłożone 🔁 every 10 days 📅 2026-10-01"_s
                << QStringList({u"2026-10-01 due Odłożone (deferred) 🔁"_s, u"2026-10-11 due Odłożone (deferred) 🔁"_s,
                                u"2026-10-21 due Odłożone (deferred) 🔁"_s,
                                u"2026-10-31 due Odłożone (deferred) 🔁"_s});
        QTest::newRow("cancelled does not") << u"- [-] Anulowana seria 🔁 every 10 days 📅 2026-10-01 ❌ 2026-10-02"_s
                                            << QStringList({u"2026-10-01 due Anulowana seria (cancelled) 🔁"_s});
        QTest::newRow("all dates of a recurring task")
                << u"- [ ] Raport 🔁 co 2 tygodnie 📅 2026-10-09 ⏳ 2026-10-07"_s
                << QStringList({u"2026-10-07 scheduled Raport (todo) 🔁"_s, u"2026-10-09 due Raport (todo) 🔁"_s,
                                u"2026-10-21 scheduled Raport (todo) 🔁"_s, u"2026-10-23 due Raport (todo) 🔁"_s});
        QTest::newRow("timed due date") << u"- [ ] Kino 🔁 every week on Friday 📅 2026-10-23 19:30"_s
                                        << QStringList({u"2026-10-23 19:30 due Kino (todo) 🔁"_s,
                                                        u"2026-10-30 19:30 due Kino (todo) 🔁"_s});
        QTest::newRow("Logseq repeater") << u"- TODO Podlać kwiaty\n  SCHEDULED: <2026-10-29 Thu 10:00 .+1d>"_s
                                         << QStringList({u"2026-10-29 10:00 scheduled Podlać kwiaty (todo) 🔁"_s,
                                                         u"2026-10-30 10:00 scheduled Podlać kwiaty (todo) 🔁"_s,
                                                         u"2026-10-31 10:00 scheduled Podlać kwiaty (todo) 🔁"_s});
        QTest::newRow("monthly from an earlier month") << u"- [ ] Czynsz 🔁 every month on the last 📅 2026-08-31"_s
                                                       << QStringList({u"2026-10-31 due Czynsz (todo) 🔁"_s});
        QTest::newRow("reminder on a done task: written date only")
                << u"- [x] Leki 🔁 every day ⏰ 2026-10-30 08:00 ✅ 2026-10-30"_s
                << QStringList({u"2026-10-30 08:00 reminder Leki (done) 🔁"_s, u"2026-10-30 done Leki (done) 🔁"_s});
        QTest::newRow("reminder series outside tasks")
                << u"## Basen 🔁 co tydzień w środę ⏰! 2026-10-14 07:00"_s
                << QStringList({u"2026-10-14 07:00 reminder Basen 🔁 !"_s, u"2026-10-21 07:00 reminder Basen 🔁 !"_s,
                                u"2026-10-28 07:00 reminder Basen 🔁 !"_s});
        // Other dates of a recurring task keep their distance to the reference date (due, else
        // scheduled, else start), like the next instance written on completion (SYNTAX §5).
        QTest::newRow("scheduled keeps its distance to the due date")
                << u"- [ ] Raport 🔁 every week on Friday 📅 2026-10-09 ⏳ 2026-10-07"_s
                << QStringList({u"2026-10-07 scheduled Raport (todo) 🔁"_s, u"2026-10-09 due Raport (todo) 🔁"_s,
                                u"2026-10-14 scheduled Raport (todo) 🔁"_s, u"2026-10-16 due Raport (todo) 🔁"_s,
                                u"2026-10-21 scheduled Raport (todo) 🔁"_s, u"2026-10-23 due Raport (todo) 🔁"_s,
                                u"2026-10-28 scheduled Raport (todo) 🔁"_s, u"2026-10-30 due Raport (todo) 🔁"_s});
        QTest::newRow("start keeps its distance to the scheduled date")
                << u"- [ ] Kurs 🔁 every month on the last ⏳ 2026-09-30 🛫 2026-09-28"_s
                << QStringList({u"2026-10-29 start Kurs (todo) 🔁"_s, u"2026-10-31 scheduled Kurs (todo) 🔁"_s});
        QTest::newRow("reminder keeps its distance to the due date")
                << u"- [ ] Czynsz 🔁 co miesiąc 15. 📅 2026-10-15 ⏰ 2026-10-14 18:00"_s
                << QStringList({u"2026-10-14 18:00 reminder Czynsz (todo) 🔁"_s, u"2026-10-15 due Czynsz (todo) 🔁"_s});
        QTest::newRow("invalid rule: no expansion")
                << u"- [ ] Dziwne 🔁 co jakiś czas 📅 2026-10-14"_s << QStringList({u"2026-10-14 due Dziwne (todo)"_s});
    }

    void recurringExpansion()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, expected);
        QCOMPARE(describe(calendarOf(content)), expected);
    }

    void writtenDates()
    {
        // Every item names the date as written; an expanded series' items differ from it.
        const auto items = calendarOf(u"- [ ] Śmieci 🔁 every week 📅 2026-10-06 ⏳ 2026-10-05\n"
                                      u"- Leki ⏰ 2026-10-07 🔁 every day\n"
                                      u"- [x] Zrobione 📅 2026-10-02 ✅ 2026-10-03\n"_s,
                                      QDate(2026, 10, 1), QDate(2026, 10, 14));
        int occurrences = 0;
        for (const CalendarItem &item : items) {
            QVERIFY(item.written.has_value());
            switch (item.kind) {
            case CalendarItem::Due:
                QCOMPARE(*item.written, (DateSpec{item.title == u"Zrobione"_s ? QDate(2026, 10, 2) : QDate(2026, 10, 6),
                                                  std::nullopt}));
                break;
            case CalendarItem::Scheduled: QCOMPARE(*item.written, (DateSpec{QDate(2026, 10, 5), std::nullopt})); break;
            case CalendarItem::Reminder:
                // Without a time: the default reminder time, as the index reads it.
                QCOMPARE(item.written->date, QDate(2026, 10, 7));
                QVERIFY(item.written->time.has_value());
                break;
            case CalendarItem::Done: QCOMPARE(*item.written, (DateSpec{QDate(2026, 10, 3), std::nullopt})); break;
            default: break;
            }
            if (item.date != item.written->date)
                ++occurrences;
        }
        // Due 13th, scheduled 12th, reminders 8th … 14th: series items, not the written dates.
        QCOMPARE(occurrences, 1 + 1 + 7);
        const auto journals =
                calendarOf({{u"journals/2026-10-05.md"_s, u"- dzień"_s}}, QDate(2026, 10, 5), QDate(2026, 10, 5));
        QCOMPARE(journals.size(), 1);
        QVERIFY(!journals.first().written.has_value());
    }

    void seriesAreCapped()
    {
        const auto items =
                calendarOf(u"- [ ] Codziennie 🔁 every day 📅 2026-01-01"_s, QDate(2026, 1, 1), QDate(2030, 12, 31));
        QCOMPARE(items.size(), 500);
        QCOMPARE(items.first().date, QDate(2026, 1, 1));
        QCOMPARE(items.last().date, QDate(2026, 1, 1).addDays(499));
    }

    void sortingIsStable()
    {
        const QString content = lines({u"- [ ] Pierwsze 📅 2026-10-14"_s, u"- Drugie ⏰ 2026-10-14 12:00"_s,
                                       u"- [ ] Trzecie 📅 2026-10-14"_s, u"- Czwarte ⏰ 2026-10-14 12:00"_s,
                                       u"- [ ] Piąte 📅 2026-10-14 08:00"_s, u"- Szóste ⏰ 2026-10-14 07:00"_s});
        QCOMPARE(describe(calendarOf(content)),
                 QStringList({u"2026-10-14 07:00 reminder Szóste"_s, u"2026-10-14 08:00 due Piąte (todo)"_s,
                              u"2026-10-14 12:00 reminder Drugie"_s, u"2026-10-14 12:00 reminder Czwarte"_s,
                              u"2026-10-14 due Pierwsze (todo)"_s, u"2026-10-14 due Trzecie (todo)"_s}));
    }

    void titles_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("title");
        QTest::newRow("plain text") << u"- [ ] Zadzwonić do **[[Ludzie/Łucja|Łucji]]** #praca 📅 2026-10-14"_s
                                    << u"Zadzwonić do Łucji #praca"_s;
        QTest::newRow("empty task text") << u"- [ ] 📅 2026-10-14"_s << u"Notatka"_s;
        QTest::newRow("bare reminder bullet") << u"- ⏰ 2026-10-14 09:00"_s << u"Notatka"_s;
        QTest::newRow("long titles are not elided")
                << u"- "_s + QString(150, u'ź') + u" ⏰ 2026-10-14 09:00"_s << QString(150, u'ź');
    }

    void titles()
    {
        QFETCH(QString, content);
        QFETCH(QString, title);
        const auto items = calendarOf(content);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.first().title, title);
    }

    void manyRemindersInOneHugeBlock()
    {
        // Untrusted notes: one 100 KB block with 3000 reminders — its title is computed once.
        QString content = u"- "_s;
        for (int i = 0; i < 20'000; ++i)
            content += u"słowo "_s;
        for (int i = 0; i < 3'000; ++i) {
            const QDateTime at = dt(2026, 10, 1 + i / 100, 8, 0).addSecs(60 * (i % 100));
            content += u"⏰ "_s + at.toString(u"yyyy-MM-dd HH:mm "_s);
        }
        VaultIndex index;
        fill(index, {{u"Duży.md"_s, content}});
        QElapsedTimer timer;
        timer.start();
        const auto items = calendarItems(index, QDate(2026, 10, 1), QDate(2026, 10, 31));
        const qint64 elapsed = timer.elapsed();
        QCOMPARE(items.size(), 3'000);
        QVERIFY2(elapsed < 1000 * BRULION_TEST_SLOWDOWN, qPrintable(u"calendarItems took %1 ms"_s.arg(elapsed)));
        QVERIFY(items.first().title.startsWith(u"słowo słowo"_s));
    }

    void nestedBareBullet()
    {
        // A snoozed occurrence under a recurring bare bullet under a task: the task's title and
        // status.
        const QString content = lines({u"- [x] Leki ✅ 2026-10-13"_s, u"  - ⏰ 2026-10-14 08:00 🔁 every day"_s,
                                       u"    - ⏰ 2026-10-14 08:10"_s});
        QCOMPARE(describe(calendarOf(content, QDate(2026, 10, 14), QDate(2026, 10, 14))),
                 QStringList(
                         {u"2026-10-14 08:00 reminder Leki (done) 🔁"_s, u"2026-10-14 08:10 reminder Leki (done)"_s}));
    }

    void journalsOnly()
    {
        Files files;
        for (int day = 1; day <= 31; ++day)
            files.append({u"journals/2026-10-%1.md"_s.arg(day, 2, 10, QLatin1Char('0')), u"- wpis"_s});
        const auto items = calendarOf(files, QDate(2026, 10, 10), QDate(2026, 10, 19));
        QCOMPARE(items.size(), 10);
        for (qsizetype i = 0; i < items.size(); ++i) {
            QCOMPARE(items[i].kind, CalendarItem::Journal);
            QCOMPARE(items[i].date, QDate(2026, 10, 10).addDays(i));
        }
    }
};

QTEST_GUILESS_MAIN(TestRemindersCalendar)
#include "tst_reminders_calendar.moc"
