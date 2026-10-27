// upcomingNotifications(), notificationsBetween(), ids and native ids.
#include "markdown/edit.h"
#include "reminders/reminders.h"
#include "reminders/reminders_p.h"
#include "reminderstest.h"
#include "text/text.h"
#include "centraleuropeantime.h"

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSet>
#include <QTest>
#include <QTimeZone>

#include <ctime>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

namespace {

const QDateTime Now = dt(2026, 10, 14, 8, 0);

QList<ScheduledNotification> upcoming(const Files &files, const QDateTime &now = Now, int horizon = 14, int max = 64,
                                      int *dropped = nullptr, dates::UiLocale locale = dates::UiLocale::Polish)
{
    VaultIndex index;
    fill(index, files);
    return upcomingNotifications(index, now, horizon, max, dropped, locale);
}

QList<ScheduledNotification> upcomingOf(const QString &content, const QDateTime &now = Now, int horizon = 14)
{
    return upcoming({{u"Notatka.md"_s, content}}, now, horizon);
}

QList<ScheduledNotification> withTitle(const QList<ScheduledNotification> &list, const QString &title)
{
    QList<ScheduledNotification> out;
    for (const ScheduledNotification &n : list) {
        if (n.title == title)
            out.append(n);
    }
    return out;
}

} // namespace

class TestRemindersNotifications : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void fixtureVault()
    {
        const QList<ScheduledNotification> list = upcoming(vault());
        // 1 parcel + 1 alarm + 1 date-only + 15 daily + 4 weekly + 1 painting + 1 (@…) = 24.
        QCOMPARE(list.size(), 24);
        const QStringList expected = {
                u"2026-10-14 08:00 Łyknąć witaminy"_s,     u"2026-10-14 17:30 Odebrać paczkę"_s,
                u"2026-10-15 06:30 Pobudka na lotnisko"_s, u"2026-10-15 08:00 Łyknąć witaminy"_s,
                u"2026-10-15 12:00 Malowanie salonu"_s,    u"2026-10-16 08:00 Łyknąć witaminy"_s,
                u"2026-10-16 09:00 Bez godziny"_s,         u"2026-10-16 18:00 Siłownia z Łukaszem"_s,
                u"2026-10-17 08:00 Łyknąć witaminy"_s,     u"2026-10-18 08:00 Łyknąć witaminy"_s,
                u"2026-10-19 08:00 Łyknąć witaminy"_s,     u"2026-10-19 18:00 Siłownia z Łukaszem"_s,
                u"2026-10-20 07:30 Spotkanie z ekipą"_s,   u"2026-10-20 08:00 Łyknąć witaminy"_s,
        };
        QCOMPARE(keys(list).first(expected.size()), expected);
        QCOMPARE(keys(list).last(), u"2026-10-28 08:00 Łyknąć witaminy"_s);
        QCOMPARE(withTitle(list, u"Łyknąć witaminy"_s).size(), 15);
        QCOMPARE(withTitle(list, u"Siłownia z Łukaszem"_s).size(), 4);
        // Done / cancelled tasks, past reminders and code never appear.
        for (const ScheduledNotification &n : list) {
            QVERIFY(!n.title.contains(u"hydraulika"_s));
            QVERIFY(!n.title.contains(u"Anulowane"_s));
            QVERIFY(!n.title.contains(u"Wczorajsze"_s));
            QVERIFY(!n.title.contains(u"w kodzie"_s));
            QCOMPARE(n.ringMinutes, ScheduledNotification::DefaultRingMinutes);
        }
    }

    void fields()
    {
        const QList<ScheduledNotification> list = upcoming(vault());
        const ScheduledNotification parcel = withTitle(list, u"Odebrać paczkę"_s).value(0);
        QCOMPARE(parcel.path, u"journals/2026-10-14.md"_s);
        QCOMPARE(parcel.line, 0);
        QCOMPARE(parcel.at, dt(2026, 10, 14, 17, 30));
        QVERIFY(!parcel.alarm);
        QCOMPARE(parcel.body, u"2026-10-14 · termin: dziś"_s);

        const ScheduledNotification alarm = withTitle(list, u"Pobudka na lotnisko"_s).value(0);
        QVERIFY(alarm.alarm);
        QCOMPARE(alarm.line, 3);
        QCOMPARE(alarm.body, u"2026-10-14"_s);

        // The due date is relative to the occurrence's day.
        const ScheduledNotification painting = withTitle(list, u"Malowanie salonu"_s).value(0);
        QCOMPARE(painting.body, u"Dom · termin: jutro"_s);
        QCOMPARE(painting.path, u"Projekty/Dom.md"_s);

        const ScheduledNotification compat = withTitle(list, u"Spotkanie z ekipą"_s).value(0);
        QCOMPARE(compat.at, dt(2026, 10, 20, 7, 30));
        QCOMPARE(compat.line, 1);
        QVERIFY(!compat.alarm);

        // Date-only reminders use the default reminder time (09:00).
        QCOMPARE(withTitle(list, u"Bez godziny"_s).value(0).at, dt(2026, 10, 16, 9, 0));
    }

    void englishBody()
    {
        const QList<ScheduledNotification> list = upcoming(vault(), Now, 14, 64, nullptr, dates::UiLocale::English);
        QCOMPARE(withTitle(list, u"Odebrać paczkę"_s).value(0).body, u"2026-10-14 · due today"_s);
        QCOMPARE(withTitle(list, u"Malowanie salonu"_s).value(0).body, u"Dom · due tomorrow"_s);
    }

    void dueWithTime()
    {
        const auto list = upcomingOf(u"- [ ] Oddać raport 📅 2026-10-20 15:00 ⏰ 2026-10-20 09:00"_s);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().body, u"Notatka · termin: dziś 15:00"_s);
    }

    void configuredDefaultTime()
    {
        VaultConfig config;
        config.defaultReminderTime = QTime(7, 15);
        VaultIndex index(config);
        fill(index, vault());
        const auto list = upcomingNotifications(index, Now);
        QCOMPARE(withTitle(list, u"Bez godziny"_s).value(0).at, dt(2026, 10, 16, 7, 15));
    }

    void window_data()
    {
        QTest::addColumn<QDateTime>("now");
        QTest::addColumn<int>("horizon");
        QTest::addColumn<bool>("included");
        QTest::newRow("exactly now") << dt(2026, 10, 14, 8, 0) << 14 << true;
        QTest::newRow("59 s ago (grace minute)") << dt(2026, 10, 14, 8, 0, 59) << 14 << true;
        QTest::newRow("60 s ago") << dt(2026, 10, 14, 8, 1, 0) << 14 << true;
        QTest::newRow("61 s ago") << dt(2026, 10, 14, 8, 1, 1) << 14 << false;
        QTest::newRow("yesterday") << dt(2026, 10, 15, 8, 0) << 14 << false;
        QTest::newRow("at the horizon") << dt(2026, 10, 4, 8, 0) << 10 << true;
        QTest::newRow("just beyond the horizon") << dt(2026, 10, 4, 7, 59) << 10 << false;
        QTest::newRow("horizon 0, now") << dt(2026, 10, 14, 8, 0) << 0 << true;
        QTest::newRow("horizon 0, later") << dt(2026, 10, 14, 7, 0) << 0 << false;
        QTest::newRow("negative horizon = 0") << dt(2026, 10, 14, 8, 0) << -5 << true;
    }

    void window()
    {
        QFETCH(QDateTime, now);
        QFETCH(int, horizon);
        QFETCH(bool, included);
        const auto list = upcomingOf(u"- Termin ⏰ 2026-10-14 08:00"_s, now, horizon);
        QCOMPARE(list.size(), included ? 1 : 0);
    }

    void doneAndCancelledSkipped_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("count");
        QTest::newRow("todo") << u"- [ ] Zadanie ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("doing") << u"- [/] Zadanie ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("deferred") << u"- [>] Zadanie ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("done") << u"- [x] Zadanie ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("done X") << u"- [X] Zadanie ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("cancelled") << u"- [-] Zadanie ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("Logseq DONE") << u"- DONE Zadanie ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("Logseq CANCELED") << u"- CANCELED Zadanie ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("Logseq LATER") << u"- LATER Zadanie ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("done recurring") << u"- [x] Zadanie 🔁 every day ⏰ 2026-10-14 12:00"_s << 0;
        QTest::newRow("paragraph") << u"Akapit ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("heading") << u"# Nagłówek ⏰ 2026-10-14 12:00"_s << 1;
        QTest::newRow("inline code") << u"- Kod `⏰ 2026-10-14 12:00`"_s << 0;
        QTest::newRow("fenced code") << u"```\n⏰ 2026-10-14 12:00\n```"_s << 0;
        QTest::newRow("hidden comment") << u"- Zadanie %%⏰ 2026-10-14 12:00%%"_s << 0;
        QTest::newRow("malformed date") << u"- Zadanie ⏰ 2026-13-45 12:00"_s << 0;
    }

    void doneAndCancelledSkipped()
    {
        QFETCH(QString, content);
        QFETCH(int, count);
        QCOMPARE(upcomingOf(content).size(), count);
    }

    void recurringDaily()
    {
        const auto list = upcomingOf(u"- [ ] Leki 🔁 codziennie ⏰ 2026-10-01 21:00"_s, Now, 3);
        QCOMPARE(keys(list),
                 QStringList({u"2026-10-14 21:00 Leki"_s, u"2026-10-15 21:00 Leki"_s, u"2026-10-16 21:00 Leki"_s}));
        // Every occurrence has its own id.
        QCOMPARE(QSet<QString>({list[0].id, list[1].id, list[2].id}).size(), 3);
    }

    void recurringWeekly()
    {
        // Anchor Monday 2026-10-05; Mondays and Fridays.
        const auto list = upcomingOf(u"- Basen 🔁 co tydzień w poniedziałek, piątek ⏰ 2026-10-05 07:00"_s, Now, 14);
        QCOMPARE(keys(list), QStringList({u"2026-10-16 07:00 Basen"_s, u"2026-10-19 07:00 Basen"_s,
                                          u"2026-10-23 07:00 Basen"_s, u"2026-10-26 07:00 Basen"_s}));
    }

    void recurringVariants_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("horizon");
        QTest::addColumn<QStringList>("dates");
        QTest::newRow("every weekday") << u"- Praca 🔁 every weekday ⏰ 2026-10-16 09:00"_s << 8
                                       << QStringList(
                                                  {u"2026-10-16"_s, u"2026-10-19"_s, u"2026-10-20"_s, u"2026-10-21"_s});
        QTest::newRow("every 2 days") << u"- Podlać 🔁 co 2 dni ⏰ 2026-10-13 10:00"_s << 6
                                      << QStringList({u"2026-10-15"_s, u"2026-10-17"_s, u"2026-10-19"_s});
        QTest::newRow("monthly, last day") << u"- Czynsz 🔁 every month on the last ⏰ 2026-09-30 10:00"_s << 60
                                           << QStringList({u"2026-10-31"_s, u"2026-11-30"_s});
        QTest::newRow("when done is ignored")
                << u"- [ ] Fryzjer 🔁 every day when done ⏰ 2026-10-13 10:00"_s << 1 << QStringList({u"2026-10-14"_s});
        QTest::newRow("anchor in the future") << u"- Urlop 🔁 every day ⏰ 2026-10-20 10:00"_s << 8
                                              << QStringList({u"2026-10-20"_s, u"2026-10-21"_s});
        QTest::newRow("invalid rule = one-off") << u"- Dziwne 🔁 every month on the 2nd Tuesday ⏰ 2026-10-15 10:00"_s
                                                << 30 << QStringList({u"2026-10-15"_s});
        QTest::newRow("invalid rule, past")
                << u"- Dziwne 🔁 co jakiś czas ⏰ 2026-10-10 10:00"_s << 30 << QStringList();
        QTest::newRow("Logseq repeater")
                << u"- TODO Podlać kwiaty ⏰ 2026-10-14 19:00\n  SCHEDULED: <2026-10-14 Wed .+1d>"_s << 2
                << QStringList({u"2026-10-14"_s, u"2026-10-15"_s});
        QTest::newRow("recurring task rule on the line")
                << u"- [ ] Faktura 📅 2026-10-20 🔁 co miesiąc ⏰! 2026-10-19 08:00"_s << 40
                << QStringList({u"2026-10-19"_s, u"2026-11-19"_s});
    }

    void recurringTaskKeepsOffset_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("horizon");
        QTest::addColumn<QStringList>("expected");
        // A reminder of a recurring task keeps its distance in days to the task's reference date
        // (due, else scheduled, else start) — exactly what the next instance written on
        // completion gets (SYNTAX §5, markdown::setTaskStatus) — instead of following the rule
        // from its own date.
        QTest::newRow("monthly on the 15th, the evening before")
                << u"- [ ] Czynsz 📅 2026-10-15 🔁 co miesiąc 15. ⏰ 2026-10-14 18:00"_s << 60
                << QStringList({u"2026-10-14 18:00"_s, u"2026-11-14 18:00"_s});
        QTest::newRow("Monday and Friday, the evening before")
                << u"- [ ] Basen 🔁 every week on Monday, Friday 📅 2026-10-16 ⏰ 2026-10-15 20:00"_s << 12
                << QStringList({u"2026-10-15 20:00"_s, u"2026-10-18 20:00"_s, u"2026-10-22 20:00"_s,
                                u"2026-10-25 20:00"_s});
        QTest::newRow("scheduled is the reference without a due date")
                << u"- [ ] Raport 🔁 every month on the last ⏳ 2026-10-31 ⏰ 2026-10-30 09:00"_s << 80
                << QStringList({u"2026-10-30 09:00"_s, u"2026-11-29 09:00"_s, u"2026-12-30 09:00"_s});
        QTest::newRow("start is the reference without due or scheduled")
                << u"- [ ] Kurs 🔁 every week on Monday 🛫 2026-10-19 ⏰ 2026-10-18 19:00"_s << 12
                << QStringList({u"2026-10-18 19:00"_s, u"2026-10-25 19:00"_s});
        QTest::newRow("Logseq DEADLINE is the reference")
                << u"- TODO Basen ⏰ 2026-10-15 20:00\n  DEADLINE: <2026-10-16 Fri .+1w>"_s << 9
                << QStringList({u"2026-10-15 20:00"_s, u"2026-10-22 20:00"_s});
        QTest::newRow("several reminders, each with its own distance")
                << u"- [ ] Wywóz 📅 2026-10-16 🔁 every month on the 16th ⏰ 2026-10-15 19:00 ⏰ 2026-10-16 07:00"_s
                << 40
                << QStringList({u"2026-10-15 19:00"_s, u"2026-10-16 07:00"_s, u"2026-11-15 19:00"_s,
                                u"2026-11-16 07:00"_s});
        // A 🔁 that is not the task's own rule (not on its first line): the reminder's own series.
        QTest::newRow("rule on a later line of a one-off task")
                << u"- [ ] Zadanie 📅 2026-10-16\n  przypomnij 🔁 every week on Friday ⏰ 2026-10-15 07:00"_s << 10
                << QStringList({u"2026-10-15 07:00"_s, u"2026-10-16 07:00"_s, u"2026-10-23 07:00"_s});
    }

    void recurringTaskKeepsOffset()
    {
        QFETCH(QString, content);
        QFETCH(int, horizon);
        QFETCH(QStringList, expected);
        QStringList got;
        for (const ScheduledNotification &n : upcomingOf(content, Now, horizon))
            got.append(n.at.toString(u"yyyy-MM-dd HH:mm"_s));
        QCOMPARE(got, expected);
    }

    void recurringTaskMatchesCompletion()
    {
        // The occurrences planned before completing a recurring task equal those of the next
        // instance written by markdown::setTaskStatus() (same ids: nothing fires twice).
        const QString content = u"- [ ] Czynsz 📅 2026-10-15 🔁 co miesiąc 15. ⏰ 2026-10-14 18:00\n"_s;
        const auto before = upcomingOf(content, dt(2026, 10, 14, 19, 0), 60);
        const QString completed = markdown::setTaskStatus(content, 0, TaskStatus::Done, QDate(2026, 10, 14));
        QVERIFY2(completed.contains(u"⏰ 2026-11-14 18:00"_s), qPrintable(completed));
        const auto after = upcomingOf(completed, dt(2026, 10, 14, 19, 0), 60);
        QCOMPARE(keys(after), keys(before));
        QCOMPARE(after.value(0).id, before.value(0).id);
    }

    void recurringVariants()
    {
        QFETCH(QString, content);
        QFETCH(int, horizon);
        QFETCH(QStringList, dates);
        QStringList got;
        for (const ScheduledNotification &n : upcomingOf(content, Now, horizon))
            got.append(n.at.date().toString(u"yyyy-MM-dd"_s));
        QCOMPARE(got, dates);
    }

    void stableIds()
    {
        const QString content =
                lines({u"- [ ] Leki 🔁 every day ⏰ 2026-10-14 21:00"_s, u"- Zażółć gęślą jaźń ⏰ 2026-10-15 10:00"_s});
        const auto before = upcomingOf(content, Now, 2);
        // Lines added above: same ids, new lines.
        const auto shifted = upcomingOf(u"# Nagłówek\n\nWstęp\n\n"_s + content, Now, 2);
        QCOMPARE(shifted.size(), before.size());
        for (qsizetype i = 0; i < before.size(); ++i) {
            QCOMPARE(shifted[i].id, before[i].id);
            QCOMPARE(shifted[i].nativeId, before[i].nativeId);
            QCOMPARE(shifted[i].line, before[i].line + 4);
        }
        // Recomputing later keeps the ids of the same occurrences.
        QCOMPARE(keys(before), QStringList({u"2026-10-14 21:00 Leki"_s, u"2026-10-15 10:00 Zażółć gęślą jaźń"_s,
                                            u"2026-10-15 21:00 Leki"_s}));
        const auto later = upcomingOf(content, dt(2026, 10, 14, 22, 0), 2);
        QCOMPARE(later.first().at, dt(2026, 10, 15, 10, 0));
        QCOMPARE(later.first().id, before[1].id);
        QCOMPARE(upcomingOf(u"- Zażółć gęślą jaźń ⏰ 2026-10-15 10:00"_s).first().id, before[1].id);

        // Text, time or path changes give new ids.
        const auto edited = upcomingOf(u"- Zażółć gęślą jaźń! ⏰ 2026-10-15 10:00"_s);
        QVERIFY(edited.first().id != before[1].id);
        const auto moved = upcomingOf(u"- Zażółć gęślą jaźń ⏰ 2026-10-15 10:01"_s);
        QVERIFY(moved.first().id != before[1].id);
        const auto other = upcoming({{u"Inna.md"_s, u"- Zażółć gęślą jaźń ⏰ 2026-10-15 10:00"_s}});
        QVERIFY(other.first().id != before[1].id);
    }

    void idFormat()
    {
        const QString id = notificationId(u"a/Notatka.md"_s, dt(2026, 10, 15, 9, 30, 45), u"Zażółć"_s);
        QCOMPARE(id, text::fnv1a64Hex((u"a/Notatka.md|2026-10-15T09:30|"_s + text::contentHash(u"Zażółć")).toUtf8()));
        QCOMPARE(id.size(), 16);
        QCOMPARE(upcomingOf(u"- Zażółć ⏰ 2026-10-15 09:30"_s).first().id,
                 notificationId(u"Notatka.md"_s, dt(2026, 10, 15, 9, 30), u"Zażółć"_s));
    }

    void nativeIdsUnique()
    {
        QStringList many;
        for (int i = 0; i < 1000; ++i)
            many.append(u"- Przypomnienie nr %1 ⏰ 2026-10-14 12:00"_s.arg(i));
        VaultIndex index;
        fill(index, {{u"Dużo.md"_s, many.join(u'\n')}});
        const auto list = upcomingNotifications(index, Now, 14, 2000);
        QCOMPARE(list.size(), 1000);
        QSet<int> seen;
        QSet<QString> ids;
        for (const ScheduledNotification &n : list) {
            QVERIFY(n.nativeId > 0);
            QVERIFY(n.nativeId <= 0x7FFF'FFFF);
            seen.insert(n.nativeId);
            ids.insert(n.id);
        }
        QCOMPARE(seen.size(), 1000);
        QCOMPARE(ids.size(), 1000);
    }

    void nativeIdProbing()
    {
        QList<ScheduledNotification> list(4);
        list[0].id = u"abc"_s;
        list[1].id = u"abc"_s;
        list[2].id = u"abc"_s;
        list[3].id = u"xyz"_s;
        assignNativeIds(list);
        const int base = nativeIdFor(u"abc"_s);
        QCOMPARE(base, int(text::fnv1a64(QByteArrayView("abc")) & 0x7FFF'FFFF));
        QCOMPARE(list[0].nativeId, base);
        QCOMPARE(list[1].nativeId, base + 1);
        QCOMPARE(list[2].nativeId, base + 2);
        QCOMPARE(list[3].nativeId, nativeIdFor(u"xyz"_s));

        using reminders_detail::probeNativeId;
        QCOMPARE(probeNativeId(5, {}), 5);
        QCOMPARE(probeNativeId(5, {5, 6}), 7);
        QCOMPARE(probeNativeId(0x7FFF'FFFF, {0x7FFF'FFFF}), 1);
        QCOMPARE(probeNativeId(0x7FFF'FFFF, {0x7FFF'FFFF, 1, 2}), 3);
        QCOMPARE(probeNativeId(0, {}), 1);
        QCOMPARE(probeNativeId(-7, {1}), 2);
    }

    void maxAndDropped()
    {
        int dropped = -1;
        const auto five = upcoming(vault(), Now, 14, 5, &dropped);
        QCOMPARE(five.size(), 5);
        QCOMPARE(dropped, 19);
        QCOMPARE(keys(five), keys(upcoming(vault())).first(5)); // the soonest win
        QCOMPARE(upcoming(vault(), Now, 14, 0, &dropped).size(), 0);
        QCOMPARE(dropped, 24);
        QCOMPARE(upcoming(vault(), Now, 14, -1, &dropped).size(), 0);
        upcoming(vault(), Now, 14, 100, &dropped);
        QCOMPARE(dropped, 0);
    }

    void sortedByTimeThenId()
    {
        const auto list = upcomingOf(lines({u"- Zet ⏰ 2026-10-14 12:00"_s, u"- Alfa ⏰ 2026-10-14 12:00"_s,
                                            u"- Wcześniej ⏰ 2026-10-14 11:59"_s}));
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].title, u"Wcześniej"_s);
        QVERIFY(list[1].id < list[2].id);
    }

    void duplicatesMerged()
    {
        // The same text and time twice in one block (reminder + alarm): one notification, alarm.
        const auto list = upcomingOf(u"- Pobudka ⏰ 2026-10-14 12:00 ⏰! 2026-10-14 12:00"_s);
        QCOMPARE(list.size(), 1);
        QVERIFY(list.first().alarm);
        // Several reminders at different times in one block.
        QCOMPARE(upcomingOf(u"- Dwa ⏰ 2026-10-14 12:00 ⏰ 2026-10-14 13:00"_s).size(), 2);
    }

    void titles_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("title");
        QTest::newRow("tokens stripped") << u"- [ ] Kupić mleko 📅 2026-10-14 ⏫ #zakupy ⏰ 2026-10-14 12:00 ^abc123"_s
                                         << u"Kupić mleko #zakupy"_s;
        QTest::newRow("wiki alias") << u"- Spotkanie z [[Jan Kowalski|Jankiem]] ⏰ 2026-10-14 12:00"_s
                                    << u"Spotkanie z Jankiem"_s;
        QTest::newRow("wiki heading") << u"- Czytaj [[Projekty/Dom#Plan]] ⏰ 2026-10-14 12:00"_s
                                      << u"Czytaj Projekty/Dom > Plan"_s;
        QTest::newRow("markdown link and url")
                << u"- Zobacz [stronę](https://example.com) i https://brulion.app ⏰ 2026-10-14 12:00"_s
                << u"Zobacz stronę i https://brulion.app"_s;
        QTest::newRow("emphasis") << u"- **Ważne** ==bardzo== ~~nie~~ `kod` i 2*3 ⏰ 2026-10-14 12:00"_s
                                  << u"Ważne bardzo nie kod i 2*3"_s;
        QTest::newRow("block ref") << u"- Patrz ((plan01)) ⏰ 2026-10-14 12:00\n\nPlan **remontu** ^plan01"_s
                                   << u"Patrz Plan remontu"_s;
        QTest::newRow("unknown block ref") << u"- Patrz ((brak)) ⏰ 2026-10-14 12:00"_s << u"Patrz ((brak))"_s;
        QTest::newRow("multi-line block")
                << u"- Pierwsza linia ⏰ 2026-10-14 12:00\n  druga   linia"_s << u"Pierwsza linia druga linia"_s;
        QTest::newRow("empty text: page title") << u"- ⏰ 2026-10-14 12:00"_s << u"Notatka"_s;
        QTest::newRow("empty task: page title") << u"- [ ] ⏰ 2026-10-14 12:00"_s << u"Notatka"_s;
        QTest::newRow("diacritics") << u"- Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ ⏰ 2026-10-14 12:00"_s
                                    << u"Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ"_s;
        QTest::newRow("code spans kept verbatim")
                << u"- Uruchom `__init__` i `a**b**c` ⏰ 2026-10-14 12:00"_s << u"Uruchom __init__ i a**b**c"_s;
        QTest::newRow("double backtick span") << u"- Znak ``a ` b`` ⏰ 2026-10-14 12:00"_s << u"Znak a ` b"_s;
        QTest::newRow("intraword underscores")
                << u"- Plik snake__case__name.py ⏰ 2026-10-14 12:00"_s << u"Plik snake__case__name.py"_s;
        QTest::newRow("nested emphasis") << u"- **==Pilne==** i ~~**stare**~~ ⏰ 2026-10-14 12:00"_s
                                         << u"Pilne i stare"_s;
        QTest::newRow("urls kept verbatim")
                << u"- Pobierz https://example.com/?t=YWJj==&s=ZGVm== i <https://x.pl/**a**> ⏰ 2026-10-14 12:00"_s
                << u"Pobierz https://example.com/?t=YWJj==&s=ZGVm== i https://x.pl/**a**"_s;
        QTest::newRow("link texts without markup")
                << u"- Zobacz [**ważne**](https://example.com/a==b==c) i [[Strona|==Ala==]] ⏰ 2026-10-14 12:00"_s
                << u"Zobacz ważne i Ala"_s;
        QTest::newRow("page names kept verbatim")
                << u"- Czytaj [[__init__]] ⏰ 2026-10-14 12:00"_s << u"Czytaj __init__"_s;
        QTest::newRow("unmatched markers") << u"- 2 ** 3 == 8 i a**b ⏰ 2026-10-14 12:00"_s << u"2 ** 3 == 8 i a**b"_s;
    }

    void plainTextIsLinear()
    {
        // Untrusted notes: a huge block full of unmatched emphasis openers must not take
        // quadratic time (the title is computed every time the reminder is planned).
        QString text;
        text.reserve(200'000);
        for (int i = 0; i < 50'000; ++i)
            text += u"**a "_s;
        QElapsedTimer timer;
        timer.start();
        const QString plain = reminders_detail::plainText(text);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(u"plainText took %1 ms"_s.arg(elapsed)));
        QCOMPARE(plain, text.trimmed());
    }

    void manyRemindersInOneHugeBlock()
    {
        // Untrusted notes: one 100 KB block carrying 5000 reminders. Its text is read once per
        // block, not once per reminder occurrence (that would be quadratic).
        QString content = u"- "_s;
        for (int i = 0; i < 20'000; ++i)
            content += u"słowo "_s;
        for (int i = 0; i < 5'000; ++i) {
            const QDateTime at = dt(2026, 10, 14, 8, 0).addDays(i / 600).addSecs(60 * (i % 600));
            content += u"⏰ "_s + at.toString(u"yyyy-MM-dd HH:mm "_s);
        }
        VaultIndex index;
        fill(index, {{u"Duży.md"_s, content}});
        QCOMPARE(index.note(u"Duży.md"_s)->reminders.size(), 5'000);
        QElapsedTimer timer;
        timer.start();
        const auto list = upcomingNotifications(index, Now, 14, 1000);
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(u"upcomingNotifications took %1 ms"_s.arg(elapsed)));
        QCOMPARE(list.size(), 1000);
        QCOMPARE(list.first().title.size(), ScheduledNotification::MaxTitleLength);
    }

    void blockRefTextIsBounded()
    {
        // Many references to one huge block must not multiply its text: each reference shows
        // at most a title's worth of it.
        const QString big = QString(20'000, u'ż');
        QString refs;
        for (int i = 0; i < 2'000; ++i)
            refs += u"((duzy)) "_s;
        VaultIndex index;
        fill(index, {{u"Duży.md"_s, big + u" ^duzy"_s}, {u"a.md"_s, u"- "_s + refs + u"⏰ 2026-10-14 12:00"_s}});
        const QString plain = reminders_detail::plainText(index.note(u"a.md"_s)->blocks.at(0).text, &index);
        QVERIFY2(plain.size() < 2'000 * (ScheduledNotification::MaxTitleLength + 2),
                 qPrintable(QString::number(plain.size())));
        QVERIFY(plain.startsWith(QString(ScheduledNotification::MaxTitleLength - 1, u'ż') + u'…'));
        const auto list = upcomingNotifications(index, Now);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().title.size(), ScheduledNotification::MaxTitleLength);
    }

    void titles()
    {
        QFETCH(QString, content);
        QFETCH(QString, title);
        const auto list = upcomingOf(content);
        QCOMPARE(list.size(), 1);
        QCOMPARE(list.first().title, title);
    }

    void longTitleElided()
    {
        const QString longText = QString(200, u'ż');
        const auto list = upcomingOf(u"- "_s + longText + u" ⏰ 2026-10-14 12:00"_s);
        const QString title = list.first().title;
        QCOMPARE(title.size(), ScheduledNotification::MaxTitleLength);
        QVERIFY(title.endsWith(u'…'));
        QCOMPARE(title.chopped(1), QString(119, u'ż'));

        // Never split a surrogate pair.
        QString emoji;
        for (int i = 0; i < 100; ++i)
            emoji += u"😀"_s;
        const QString cut = upcomingOf(u"- "_s + emoji + u" ⏰ 2026-10-14 12:00"_s).first().title;
        QVERIFY(cut.size() <= ScheduledNotification::MaxTitleLength);
        QVERIFY(cut.endsWith(u'…'));
        QVERIFY(cut.at(cut.size() - 2).isLowSurrogate());

        QCOMPARE(reminders_detail::elide(u"abc def"_s, 5), u"abc…"_s);
        QCOMPARE(reminders_detail::elide(u"abc"_s, 3), u"abc"_s);
        QCOMPARE(reminders_detail::elide(u"abc"_s, 0), QString());
    }

    void bareBulletBelongsToParent()
    {
        const QString content = lines({u"- [ ] Leki 🔁 every day ⏰ 2026-10-13 08:00"_s, u"\t- ⏰ 2026-10-14 08:10"_s,
                                       u"- Akapit **nadrzędny**"_s, u"  - ⏰! 2026-10-14 09:00"_s});
        const auto list = upcomingOf(content, Now, 0);
        QCOMPARE(keys(list), QStringList({u"2026-10-14 08:00 Leki"_s}));
        const auto day = upcomingOf(content, Now, 1);
        QCOMPARE(keys(day), QStringList({u"2026-10-14 08:00 Leki"_s, u"2026-10-14 08:10 Leki"_s,
                                         u"2026-10-14 09:00 Akapit nadrzędny"_s, u"2026-10-15 08:00 Leki"_s}));
        QCOMPARE(day[1].line, 1);
        QVERIFY(day[1].id != day[0].id);
        QVERIFY(day[2].alarm);

        // The snoozed bullet stops with its task.
        const QString done = QString(content).replace(u"- [ ] Leki"_s, u"- [x] Leki"_s);
        QCOMPARE(keys(upcomingOf(done, Now, 1)), QStringList({u"2026-10-14 09:00 Akapit nadrzędny"_s}));
    }

    void nestedBareBullets()
    {
        // A recurring bare bullet under a task, and a snoozed occurrence under it: both belong to
        // the task (through the bare bullet), so both stop when it is done.
        const QString content =
                lines({u"- [ ] Leki"_s, u"  - ⏰ 2026-10-14 08:00 🔁 every day"_s, u"    - ⏰ 2026-10-14 08:10"_s});
        QCOMPARE(keys(upcomingOf(content, Now, 1)),
                 QStringList({u"2026-10-14 08:00 Leki"_s, u"2026-10-14 08:10 Leki"_s, u"2026-10-15 08:00 Leki"_s}));
        QVERIFY(upcomingOf(QString(content).replace(u"- [ ]"_s, u"- [x]"_s), Now, 1).isEmpty());
    }

    void taskLine_data()
    {
        // Every reminder of the day (2026-10-14), in order; the line of the task "Done" would
        // complete.
        QTest::addColumn<QString>("content");
        QTest::addColumn<QList<int>>("taskLines");
        QTest::newRow("task") << u"- [ ] Zadzwonić do Żanety ⏰ 2026-10-14 09:00"_s << QList<int>{0};
        QTest::newRow("below a heading") << lines({u"# Dom"_s, u""_s, u"- [/] Pomalować ścianę ⏰ 2026-10-14 09:00"_s})
                                         << QList<int>{2};
        QTest::newRow("logseq") << u"- TODO Kupić żółty ser ⏰ 2026-10-14 09:00"_s << QList<int>{0};
        QTest::newRow("continuation line")
                << lines({u"- [ ] Napisać raport"_s, u"  dla Łucji ⏰ 2026-10-14 09:00"_s}) << QList<int>{0};
        QTest::newRow("nested task") << lines({u"- Projekt źródłowy"_s, u"  - [ ] Ułożyć plan ⏰ 2026-10-14 09:00"_s})
                                     << QList<int>{1};
        QTest::newRow("bare bullet under a task")
                << lines({u"- [ ] Leki"_s, u"  - ⏰ 2026-10-14 09:00"_s}) << QList<int>{0};
        QTest::newRow("nested bare bullets")
                << lines({u"- Wstęp"_s, u"- [ ] Leki"_s, u"  - ⏰ 2026-10-14 09:00 🔁 every day"_s,
                          u"    - ⏰ 2026-10-14 09:10"_s})
                << QList<int>{1, 1};
        QTest::newRow("recurring task") << lines(
                {u"Wstęp"_s, u""_s, u"- [ ] Łyknąć witaminy 🔁 every day ⏰ 2026-10-10 12:00"_s})
                                        << QList<int>{2};
        QTest::newRow("plain bullet") << u"- Kupić chleb ⏰ 2026-10-14 09:00"_s << QList<int>{-1};
        QTest::newRow("paragraph") << u"Akapit o źrebaku ⏰ 2026-10-14 09:00"_s << QList<int>{-1};
        QTest::newRow("bare bullet under a plain item")
                << lines({u"- Akapit"_s, u"  - ⏰ 2026-10-14 09:00"_s}) << QList<int>{-1};
        QTest::newRow("bare bullet in a quote")
                << lines({u"- [ ] Zadanie obok"_s, u""_s, u"> Cytat o zadaniu"_s, u"> - ⏰ 2026-10-14 09:00"_s})
                << QList<int>{-1};
        QTest::newRow("child with text under a task")
                << lines({u"- [ ] Zadanie"_s, u"  - Osobna notatka ⏰ 2026-10-14 09:00"_s}) << QList<int>{-1};
        QTest::newRow("crlf") << u"Wstęp\r\n\r\n- [ ] Żółw ⏰ 2026-10-14 09:00\r\n- Ćma ⏰ 2026-10-14 10:00\r\n"_s
                              << QList<int>{2, -1};
    }

    void taskLine()
    {
        QFETCH(QString, content);
        QFETCH(QList<int>, taskLines);
        QList<int> actual;
        for (const ScheduledNotification &n : upcomingOf(content, Now, 1)) {
            if (n.at.date() == Today)
                actual.append(n.taskLine);
        }
        QCOMPARE(actual, taskLines);
    }

    void bareBulletInQuoteIsIndependent()
    {
        // Inside a quote the list's container is not a parent item: page title, fires on its own.
        QCOMPARE(keys(upcomingOf(u"> Cytat o zadaniu\n> - ⏰ 2026-10-14 12:00"_s)),
                 QStringList({u"2026-10-14 12:00 Notatka"_s}));
    }

    void childWithTextIsIndependent()
    {
        const QString content = lines({u"- [x] Gotowe zadanie"_s, u"  - Osobna notatka ⏰ 2026-10-14 12:00"_s});
        QCOMPARE(keys(upcomingOf(content)), QStringList({u"2026-10-14 12:00 Osobna notatka"_s}));
    }

    void notificationsBetweenWindow()
    {
        VaultIndex index;
        fill(index, vault());
        const auto list = notificationsBetween(index, dt(2026, 10, 13, 0, 0), dt(2026, 10, 14, 23, 59));
        QStringList got = keys(list);
        got.sort();
        QCOMPARE(got,
                 QStringList({u"2026-10-13 08:00 Wczorajsze przypomnienie"_s, u"2026-10-13 08:00 Łyknąć witaminy"_s,
                              u"2026-10-14 08:00 Łyknąć witaminy"_s, u"2026-10-14 17:30 Odebrać paczkę"_s}));
        QCOMPARE(list.size(), 4);
        for (const ScheduledNotification &n : list)
            QVERIFY(n.nativeId > 0);
        QVERIFY(notificationsBetween(index, dt(2026, 10, 15, 0, 0), dt(2026, 10, 14, 0, 0)).isEmpty());
        QVERIFY(notificationsBetween(index, QDateTime(), dt(2026, 10, 14, 0, 0)).isEmpty());
    }

    void emptyAndInvalid()
    {
        VaultIndex index;
        QVERIFY(upcomingNotifications(index, Now).isEmpty());
        fill(index, vault());
        int dropped = -1;
        QVERIFY(upcomingNotifications(index, QDateTime(), 14, 64, &dropped).isEmpty());
        QCOMPARE(dropped, 0);
    }

    void otherTimeZones()
    {
        // `now` given in another time zone (e.g. currentDateTimeUtc()) is the same instant: the
        // window is taken in local wall-clock time, where reminders live. (UTC-12 / UTC+14 differ
        // from almost any local zone; the expected list is the local one either way.)
        VaultIndex index;
        fill(index, {{u"a.md"_s, lines({u"- Rano ⏰ 2026-10-28 07:30"_s, u"- Na początku ⏰ 2026-10-14 08:00"_s})}});
        const auto local = upcomingNotifications(index, Now, 14);
        QCOMPARE(keys(local), QStringList({u"2026-10-14 08:00 Na początku"_s, u"2026-10-28 07:30 Rano"_s}));
        for (const int hours : {-12, 14}) {
            const QTimeZone zone = QTimeZone::fromSecondsAheadOfUtc(hours * 3600);
            const QDateTime other = Now.toTimeZone(zone);
            QCOMPARE(upcomingNotifications(index, other, 14), local);
            QCOMPARE(notificationsBetween(index, dt(2026, 10, 28, 0, 0).toTimeZone(zone),
                                          dt(2026, 10, 28, 23, 59).toTimeZone(zone)),
                     local.mid(1));
            for (const ScheduledNotification &n : upcomingNotifications(index, other, 14))
                QCOMPARE(n.at.timeSpec(), Qt::LocalTime);
            QCOMPARE(reminders_detail::formatStamp(dt(2026, 10, 14, 8, 5, 7, 123).toTimeZone(zone)),
                     u"2026-10-14T08:05:07.123"_s);
        }
    }

    void daylightSavingTime()
    {
        // Central European rules as a POSIX TZ string (no time zone database needed).
        const QByteArray oldTz = qgetenv("TZ");
        brulion::test::useCentralEuropeanTime("CET-1CEST,M3.5.0,M10.5.0/3");
        const auto restore = qScopeGuard([&oldTz] {
            if (oldTz.isNull())
                qunsetenv("TZ");
            else
                qputenv("TZ", oldTz);
            tzset();
        });
        if (const QString problem = brulion::test::centralEuropeanTimeProblem(); !problem.isEmpty())
            QSKIP(qPrintable(problem));

        // Spring forward (2027-03-28 02:00 -> 03:00): the 02:30 occurrence in the gap still
        // fires once, at the end of the gap, between its neighbours.
        const auto spring = upcomingOf(u"- Leki 🔁 every day ⏰ 2027-03-27 02:30"_s, dt(2027, 3, 27, 0, 0), 3);
        QCOMPARE(spring.size(), 3);
        QCOMPARE(spring.at(1).at.date(), QDate(2027, 3, 28));
        QVERIFY(spring.at(1).at.isValid());
        QCOMPARE(spring.at(0).at.secsTo(spring.at(1).at), 24 * 3600);
        QCOMPARE(spring.at(2).at, dt(2027, 3, 29, 2, 30));
        QCOMPARE(QSet<QString>({spring.at(0).id, spring.at(1).id, spring.at(2).id}).size(), 3);

        // Fall back (2026-10-25 03:00 -> 02:00): the repeated 02:30 fires once.
        QCOMPARE(upcomingOf(u"- Noc ⏰ 2026-10-25 02:30"_s, dt(2026, 10, 25, 0, 0), 1).size(), 1);

        // The horizon counts calendar days: 1 day from Saturday noon is Sunday noon (25 h).
        const auto horizon = upcomingOf(u"- Obiad ⏰ 2026-10-25 11:30"_s, dt(2026, 10, 24, 12, 0), 1);
        QCOMPARE(keys(horizon), QStringList({u"2026-10-25 11:30 Obiad"_s}));
    }

    void stampsRoundTrip()
    {
        using namespace reminders_detail;
        const QDateTime t = dt(2026, 10, 14, 8, 5, 7, 123);
        QCOMPARE(formatStamp(t), u"2026-10-14T08:05:07.123"_s);
        QCOMPARE(parseStamp(formatStamp(t)), t);
        QVERIFY(!parseStamp(u"wczoraj"_s).isValid());
        QVERIFY(!parseStamp(u"2026-13-01T08:00:00.000"_s).isValid());
        QVERIFY(!parseStamp(u""_s).isValid());
        QCOMPARE(formatStamp(QDateTime()), QString());
    }
};

QTEST_GUILESS_MAIN(TestRemindersNotifications)
#include "tst_reminders_notifications.moc"
