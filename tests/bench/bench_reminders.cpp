// Benchmarks of the reminders module: upcomingNotifications() (run after every index change by
// the in-app scheduler and the Android alarm sync), notificationsBetween() and calendarItems()
// (month / year views) over vaults with 1k / 10k / 50k tasks carrying reminders (10 % recurring,
// 20 % done), plus a typical mixed vault of 10k notes. afterEdit: what one save costs the core
// with the consumers that listen to VaultIndex::changed (reminder scheduler, task list,
// calendar, a query block on the open page): "all consumers" as the app runs them (the index
// patches its lists, NotificationCache / CalendarCache / QueryCache compute the saved note only —
// L3 of docs/PERFORMANCE.md), the "(stateless)" rows as the free functions compute everything.
#include "benchdata.h"

#include "index/index.h"
#include "index/query.h"
#include "index/querycache.h"
#include "reminders/reminders.h"

#include <QTest>

#include <map>
#include <memory>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// `tasks` tasks with reminders in notes of 10 tasks each, dates within ±30 days of today.
std::unique_ptr<VaultIndex> taskVault(int tasks)
{
    auto index = std::make_unique<VaultIndex>();
    VaultIndex::Batch batch(*index);
    bench::Rng r{quint64(tasks)};
    const QDate today = bench::fixedToday();
    QList<bench::VaultFile> notes;
    for (int n = 0; n < tasks / 10; ++n) {
        QString note = u"# Zadania %1\n\n"_s.arg(n);
        for (int k = 0; k < 10; ++k) {
            const QDate due = today.addDays(r.range(-30, 30));
            const int hour = r.range(6, 22);
            const bool onTheHour = r.chance(50);
            const QString time = u"%1:%2"_s.arg(hour, 2, 10, QChar(u'0')).arg(onTheHour ? u"00"_s : u"30"_s);
            const bool done = r.chance(20);
            QString line =
                    (done ? u"- [x] "_s : u"- [ ] "_s) + bench::sentence(r, 3, 8) + u" 📅 "_s + bench::isoDate(due);
            if (r.chance(10))
                line += r.chance(50) ? u" 🔁 every week"_s : u" 🔁 co miesiąc"_s;
            const bool urgent = r.chance(5);
            const int earlier = r.below(2);
            line += (urgent ? u" ⏰! "_s : u" ⏰ "_s) + bench::isoDate(due.addDays(-earlier)) + u' ' + time;
            if (done)
                line += u" ✅ "_s + bench::isoDate(today.addDays(-r.below(5)));
            note += line + u'\n';
        }
        notes.append({u"Zadania/Lista %1.md"_s.arg(n), note});
    }
    bench::reportInput(u"taskVault %1 tasks"_s.arg(tasks), notes);
    for (const bench::VaultFile &f : std::as_const(notes))
        index->update(f.path, f.content);
    return index;
}

} // namespace

class BenchReminders : public QObject {
    Q_OBJECT

    std::map<int, std::unique_ptr<VaultIndex>> m_indexes;

    const VaultIndex &tasksIndex(int tasks)
    {
        auto &idx = m_indexes[tasks];
        if (!idx)
            idx = taskVault(tasks);
        return *idx;
    }

    const VaultIndex &mixedIndex()
    {
        auto &idx = m_indexes[-1];
        if (!idx) {
            idx = std::make_unique<VaultIndex>();
            VaultIndex::Batch batch(*idx);
            for (const bench::VaultFile &f : bench::makeVault(10000))
                idx->update(f.path, f.content);
        }
        return *idx;
    }

    static void addRows()
    {
        QTest::addColumn<int>("tasks");
        for (int n : bench::sizes({1000, 10000, 50000}, {1000, 10000}))
            QTest::newRow(qPrintable(u"%1 tasks"_s.arg(n))) << n;
        QTest::newRow("mixed vault 10000 notes") << -1;
    }

    const VaultIndex &indexFor(int tasks) { return tasks < 0 ? mixedIndex() : tasksIndex(tasks); }

private Q_SLOTS:
    void upcomingNotifications_data() { addRows(); }
    void upcomingNotifications()
    {
        QFETCH(int, tasks);
        const VaultIndex &idx = indexFor(tasks);
        const QDateTime now = bench::fixedNow();
        QBENCHMARK {
            const auto list = brulion::upcomingNotifications(idx, now, 14, 64);
            Q_UNUSED(list);
        }
    }

    void notificationsMonth_data() { addRows(); }
    void notificationsMonth()
    {
        QFETCH(int, tasks);
        const VaultIndex &idx = indexFor(tasks);
        const QDateTime from = bench::fixedNow();
        QBENCHMARK {
            const auto list = brulion::notificationsBetween(idx, from, from.addDays(31));
            Q_UNUSED(list);
        }
    }

    void calendarMonth_data() { addRows(); }
    void calendarMonth()
    {
        QFETCH(int, tasks);
        const VaultIndex &idx = indexFor(tasks);
        const QDate from(2026, 9, 28);
        QBENCHMARK {
            const auto items = brulion::calendarItems(idx, from, from.addDays(41));
            Q_UNUSED(items);
        }
    }

    void afterEdit_data()
    {
        QTest::addColumn<int>("tasks");
        QTest::addColumn<QString>("then");
        for (int n : {-1, 10000}) {
            const QString name = n < 0 ? u"mixed vault 10000 notes"_s : u"%1 tasks"_s.arg(n);
            for (const QString &then :
                 {u"nothing"_s, u"reminders()"_s, u"tasks()"_s, u"upcomingNotifications (stateless)"_s,
                  u"upcoming (cache)"_s, u"calendarMonth (stateless)"_s, u"calendarMonth (cache)"_s,
                  u"query open due this week (stateless)"_s, u"query open due this week (cache)"_s,
                  u"all consumers (stateless)"_s, u"all consumers"_s})
                QTest::newRow(qPrintable(u"%1, update + %2"_s.arg(name, then))) << n << then;
        }
    }
    void afterEdit()
    {
        QFETCH(int, tasks);
        QFETCH(QString, then);
        // The shared fixture, edited back and forth (left as it was at the end).
        VaultIndex &idx = const_cast<VaultIndex &>(indexFor(tasks));
        const QStringList paths = idx.paths();
        const QString path = tasks < 0 ? u"journals/2026-10-17.md"_s : paths.value(paths.size() / 2);
        const QString original = idx.content(path);
        QVERIFY(!original.isNull());
        const QString edited = original + u"\n- [ ] nowe zadanie 📅 2026-10-20 ⏰ 2026-10-19 18:00\n"_s;
        const QDateTime now = bench::fixedNow();
        const QDate from(2026, 9, 28);
        const auto parsed = parseQuery(u"status:open due:<=+7d sort:due"_s);
        QVERIFY(std::holds_alternative<Query>(parsed));
        const Query query = std::get<Query>(parsed);
        const bool stateless = then == u"all consumers (stateless)";
        const bool cached = then == u"all consumers";
        // The caches as the app keeps them (the scheduler's days, a month of the calendar, a
        // query), synced once before the loop: the loop measures what one save costs them.
        NotificationCache notifications;
        notifications.setDays(now.date().addDays(-ReminderScheduler::HistoryDays - 1),
                              now.date().addDays(ReminderScheduler::DefaultHorizonDays + 1));
        CalendarCache calendar;
        QueryCache queryCache;
        const qsizetype warm = notifications.upcoming(idx, now, 14, 64).size()
                               + calendar.items(idx, from, from.addDays(41)).size()
                               + queryCache.run(query, idx, now.date()).items.size() + idx.tasks().size()
                               + idx.reminders().size();
        QVERIFY(warm >= 0);
        int flip = 0;
        qsizetype sink = 0;
        QBENCHMARK {
            idx.update(path, (flip++ & 1) ? original : edited);
            if (then == u"reminders()")
                sink += idx.reminders().size();
            if (stateless || then == u"upcomingNotifications (stateless)")
                sink += brulion::upcomingNotifications(idx, now, 14, 64).size();
            if (cached || then == u"upcoming (cache)")
                sink += notifications.upcoming(idx, now, 14, 64).size();
            if (stateless || cached || then == u"tasks()")
                sink += idx.tasks().size();
            if (stateless || then == u"calendarMonth (stateless)")
                sink += brulion::calendarItems(idx, from, from.addDays(41)).size();
            if (cached || then == u"calendarMonth (cache)")
                sink += calendar.items(idx, from, from.addDays(41)).size();
            if (stateless || then == u"query open due this week (stateless)")
                sink += runQuery(query, idx, now.date()).items.size();
            if (cached || then == u"query open due this week (cache)")
                sink += queryCache.run(query, idx, now.date()).items.size();
        }
        idx.update(path, original);
        QVERIFY(sink >= 0);
    }

    void calendarYear_data() { addRows(); }
    void calendarYear()
    {
        QFETCH(int, tasks);
        const VaultIndex &idx = indexFor(tasks);
        QBENCHMARK {
            const auto items = brulion::calendarItems(idx, QDate(2026, 1, 1), QDate(2026, 12, 31));
            Q_UNUSED(items);
        }
    }
};

QTEST_GUILESS_MAIN(BenchReminders)
#include "bench_reminders.moc"
