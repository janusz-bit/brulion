// CalendarCache and NotificationCache: kept across random edit sequences they give exactly what
// calendarItems(), notificationsBetween() and upcomingNotifications() give at every step (month,
// week, one day, the 2026-10-25 change to winter time; windows moving across midnight; another
// language), and after one edit they cost a fraction of computing everything again.
#include "reminderstest.h"

#include "centraleuropeantime.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <algorithm>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

namespace {

const QStringList Paths = {u"journals/2026-10-14.md"_s, u"journals/2026-10-25.md"_s, u"Zdrowie.md"_s,
                           u"Projekty/Dom.md"_s,        u"Ż.md"_s,                  u"żółw.md"_s,
                           u"x/Ala.md"_s};

const QStringList Lines = {
        u"- [ ] Odebrać paczkę 📅 2026-10-14 ⏰ 2026-10-14 17:30"_s,
        u"- [x] Zadzwonić do hydraulika ⏰ 2026-10-14 09:00 ✅ 2026-10-14"_s,
        u"- [-] Anulowane spotkanie ⏰ 2026-10-15 10:00"_s,
        u"- ⏰! 2026-10-15 06:30 Pobudka na lotnisko"_s,
        u"- [ ] Łyknąć **witaminy** 🔁 every day ⏰ 2026-10-10 08:00"_s,
        u"- Siłownia 🔁 co tydzień ⏰ 2026-10-12 18:00"_s,
        u"- [ ] Czynsz 📅 2026-10-15 🔁 every month on the 15th ⏰ 2026-10-14 18:00"_s,
        u"  - ⏰ 2026-10-16 07:00"_s, // a bare bullet: its parent's text and task
        u"- [ ] Zmiana czasu ⏰ 2026-10-25 02:30"_s,
        u"- Spotkanie z ekipą (@2026-10-20 07:30)"_s,
        u"- [ ] zobacz ((blok1)) 📅 2026-10-16 ⏰ 2026-10-16 09:15"_s,
        u"- Treść bloku z żółwiem ^blok1"_s,
        u"- inna treść ^blok1"_s,
        u"- zepsute ⏰ 2026-13-45 🔁 co nigdy"_s,
        u"- [/] w toku ⏳ 2026-10-13 🛫 2026-10-12 📅 2026-10-17"_s,
        u"- [ ] z linkiem [[Ż|żółwiem]] ⏰ 2026-10-14 12:00"_s,
        u"Akapit z przypomnieniem ⏰ 2026-10-14 20:00"_s,
        u"- [ ] co tydzień 📅 2026-10-12 🔁 every week ⏰ 2026-10-11 19:00 ⏰! 2026-10-12 07:00"_s,
};

QString randomContent(QRandomGenerator &rng)
{
    QString out;
    const int n = 1 + rng.bounded(5);
    for (int i = 0; i < n; ++i)
        out += Lines[rng.bounded(int(Lines.size()))] + u'\n';
    return out;
}

QStringList describe(const QList<CalendarItem> &items)
{
    QStringList out;
    for (const CalendarItem &i : items) {
        out.append(u"%1 %2 %3 %4|%5|%6|%7|%8|%9"_s.arg(
                i.date.toString(Qt::ISODate), i.time ? i.time->toString(u"HH:mm"_s) : u"-"_s,
                QString::number(int(i.kind)), i.title, i.path, QString::number(i.line),
                i.status ? QString::number(int(*i.status)) : u"-"_s,
                QString::number(int(i.recurring)) + QString::number(int(i.alarm)) + i.rule,
                i.written ? i.written->toString() : u"-"_s));
    }
    return out;
}

QStringList describe(const QList<ScheduledNotification> &list)
{
    QStringList out;
    for (const ScheduledNotification &n : list) {
        out.append(u"%1 %2|%3|%4|%5|%6|%7|%8|%9"_s.arg(
                n.at.toString(u"yyyy-MM-dd HH:mm"_s), n.id, QString::number(n.nativeId), n.title, n.body,
                QString::number(int(n.alarm)), n.path, QString::number(n.line), QString::number(n.taskLine)));
    }
    return out;
}

double median(QList<qint64> values)
{
    std::sort(values.begin(), values.end());
    return double(values.at(values.size() / 2));
}

} // namespace

class TestRemindersCache : public QObject {
    Q_OBJECT

    QByteArray m_oldTz;

private Q_SLOTS:
    void initTestCase()
    {
        m_oldTz = qgetenv("TZ");
        brulion::test::useCentralEuropeanTime("CET-1CEST,M3.5.0,M10.5.0/3");
    }
    void cleanupTestCase()
    {
        if (m_oldTz.isNull())
            qunsetenv("TZ");
        else
            qputenv("TZ", m_oldTz);
        tzset();
    }

    void cachesEqualStatelessFunctions_data()
    {
        QTest::addColumn<quint32>("seed");
        for (const quint32 seed : {1u, 7u, 42u, 2026u})
            QTest::newRow(qPrintable(u"seed %1"_s.arg(seed))) << seed;
    }
    void cachesEqualStatelessFunctions()
    {
        QFETCH(quint32, seed);
        struct Range {
            QDate from, to;
            CalendarCache cache;
        };
        Range ranges[] = {
                {QDate(2026, 9, 28), QDate(2026, 11, 8), {}},   // a month grid
                {QDate(2026, 10, 12), QDate(2026, 10, 18), {}}, // a week
                {QDate(2026, 10, 14), QDate(2026, 10, 14), {}}, // one day
                {QDate(2026, 10, 24), QDate(2026, 10, 26), {}}, // over the change to winter time
        };
        NotificationCache notes;
        QDateTime now = dt(2026, 10, 14, 12, 0);
        notes.setDays(now.date().addDays(-8), now.date().addDays(15));
        dates::UiLocale locale = dates::UiLocale::Polish;
        QRandomGenerator rng(seed);
        VaultIndex index;
        QHash<QString, QString> files;
        int partial = 0;
        for (int step = 0; step < 120; ++step) {
            const QString path = Paths[rng.bounded(int(Paths.size()))];
            const int op = rng.bounded(100);
            QString what;
            if (op < 70) {
                files.insert(path, randomContent(rng));
                index.update(path, files.value(path));
                what = u"update "_s + path;
            } else if (op < 85) {
                files.remove(path);
                index.remove(path);
                what = u"remove "_s + path;
            } else {
                const QString to = Paths[rng.bounded(int(Paths.size()))];
                if (!files.contains(path) || to == path)
                    continue;
                files.insert(to, files.take(path));
                index.rename(path, to, files.value(to));
                what = u"rename "_s + path + u" -> "_s + to;
            }
            if (step % 10 == 9) { // the window moves on across midnight
                now = now.addDays(1);
                notes.setDays(now.date().addDays(-8), now.date().addDays(15));
                what += u", next day"_s;
            }
            if (step % 7 == 6) {
                locale = locale == dates::UiLocale::Polish ? dates::UiLocale::English : dates::UiLocale::Polish;
                what += u", other language"_s;
            }
            const QByteArray ctx = u"step %1: %2"_s.arg(step).arg(what).toUtf8();
            for (Range &r : ranges) {
                QVERIFY2(describe(r.cache.items(index, r.from, r.to)) == describe(calendarItems(index, r.from, r.to)),
                         ctx.constData());
                if (!r.cache.lastSync().full)
                    ++partial;
            }
            QVERIFY2(describe(notes.between(index, now.addDays(-1), now.addDays(2), locale))
                             == describe(notificationsBetween(index, now.addDays(-1), now.addDays(2), locale)),
                     ctx.constData());
            int droppedCached = -1;
            int dropped = -1;
            QVERIFY2(describe(notes.upcoming(index, now, 14, 5, &droppedCached, locale))
                             == describe(upcomingNotifications(index, now, 14, 5, &dropped, locale)),
                     ctx.constData());
            QCOMPARE(droppedCached, dropped);
            // Outside the kept days: computed directly, the same.
            QVERIFY2(describe(notes.upcoming(index, now, 30, 64, nullptr, locale))
                             == describe(upcomingNotifications(index, now, 30, 64, nullptr, locale)),
                     ctx.constData());
            QVERIFY2(describe(notes.between(index, now.addDays(-20), now, locale))
                             == describe(notificationsBetween(index, now.addDays(-20), now, locale)),
                     ctx.constData());
        }
        QVERIFY(partial > 200);
        QVERIFY(notes.computedNotes() > 0);
    }

    void edgeCases()
    {
        VaultIndex index;
        fill(index, vault());
        CalendarCache calendar;
        QVERIFY(calendar.items(index, QDate(2026, 10, 15), QDate(2026, 10, 14)).isEmpty()); // from > to
        QVERIFY(calendar.items(index, QDate(), QDate(2026, 10, 14)).isEmpty());
        QCOMPARE(describe(calendar.items(index, Today, Today)), describe(calendarItems(index, Today, Today)));
        QVERIFY(calendar.lastSync().full);
        QCOMPARE(describe(calendar.items(index, Today, Today)), describe(calendarItems(index, Today, Today)));
        QVERIFY(!calendar.lastSync().any());
        // A ((id)) title follows the block it shows, in another note.
        index.update(u"Bloki.md"_s, u"- Kupić **mleko** ^mleko"_s);
        index.update(u"Lista.md"_s, u"- [ ] ((mleko)) 📅 2026-10-14"_s);
        QVERIFY(describe(calendar.items(index, Today, Today)).join(u'\n').contains(u"Kupić mleko"_s));
        index.update(u"Bloki.md"_s, u"- Kupić **chleb** ^mleko"_s);
        const QList<CalendarItem> items = calendar.items(index, Today, Today);
        QCOMPARE(calendar.lastSync().changed, QList<VaultPath>({u"Bloki.md"_s, u"Lista.md"_s}));
        QCOMPARE(describe(items), describe(calendarItems(index, Today, Today)));
        QVERIFY(describe(items).join(u'\n').contains(u"Kupić chleb"_s));
        calendar.clear();
        calendar.items(index, Today, Today);
        QVERIFY(calendar.lastSync().full);

        // Notifications: without days every request is computed directly; invalid windows.
        NotificationCache notes;
        const QDateTime now = dt(2026, 10, 14, 8, 0);
        QCOMPARE(describe(notes.upcoming(index, now)), describe(upcomingNotifications(index, now)));
        QVERIFY(!notes.lastSync().any());
        QCOMPARE(notes.computedNotes(), 0);
        QVERIFY(notes.between(index, now, now.addSecs(-1)).isEmpty());
        QVERIFY(notes.upcoming(index, QDateTime()).isEmpty());
        int dropped = -1;
        QVERIFY(notes.upcoming(index, now, 14, 0, &dropped).isEmpty());
        QVERIFY(dropped > 0);
        notes.setDays(Today.addDays(-1), Today.addDays(20));
        QCOMPARE(notes.firstDay(), Today.addDays(-1));
        QCOMPARE(notes.lastDay(), Today.addDays(20));
        QCOMPARE(describe(notes.upcoming(index, now)), describe(upcomingNotifications(index, now)));
        QVERIFY(notes.lastSync().full);
        const int computed = notes.computedNotes();
        QCOMPARE(computed, int(index.paths().size()));
        // A UTC "now" is converted to local time first.
        QCOMPARE(describe(notes.upcoming(index, now.toUTC())), describe(upcomingNotifications(index, now)));
        QCOMPARE(notes.computedNotes(), computed);
        // One edit: one note computed again.
        index.update(u"Zdrowie.md"_s, u"- [ ] Łyknąć witaminy D 🔁 every day ⏰ 2026-10-10 08:30"_s);
        QCOMPARE(describe(notes.upcoming(index, now)), describe(upcomingNotifications(index, now)));
        QCOMPARE(notes.computedNotes(), computed + 1);
        // Another language: everything.
        notes.upcoming(index, now, 14, 64, nullptr, dates::UiLocale::English);
        QVERIFY(notes.lastSync().full);
        notes.clear();
        notes.upcoming(index, now, 14, 64, nullptr, dates::UiLocale::English);
        QVERIFY(notes.lastSync().full);
        // A daily series longer than the occurrences a series gives (500): asked as it is asked.
        index.update(u"Codziennie.md"_s, u"- codziennie 🔁 every day ⏰ 2025-01-01 07:00"_s);
        NotificationCache wide;
        wide.setDays(QDate(2025, 1, 1), QDate(2027, 12, 31));
        const QDateTime later = dt(2026, 10, 14, 0, 0);
        QCOMPARE(describe(wide.between(index, later, later.addDays(30))),
                 describe(notificationsBetween(index, later, later.addDays(30))));
    }

    // After one edit the caches compute one note: at least four times cheaper than computing
    // everything again (medians of five; machine-independent).
    void oneEditCostsAFraction()
    {
        VaultIndex index;
        {
            VaultIndex::Batch batch(index);
            for (int i = 0; i < 3000; ++i) {
                const QDate due = Today.addDays(i % 41 - 20);
                QString text = u"- [ ] zadanie %1 o żółwiu 📅 %2 ⏰ %2 09:00\n- notatka %1\n"_s.arg(i).arg(
                        due.toString(Qt::ISODate));
                if (i % 10 == 0)
                    text += u"- [ ] cykliczne %1 🔁 every week 📅 %2 ⏰ %2 18:00\n"_s.arg(i).arg(due.toString(Qt::ISODate));
                index.update(u"Notatki/n%1.md"_s.arg(i), text);
            }
        }
        const QString path = u"Notatki/n1500.md"_s;
        const QString original = index.content(path);
        const QString edited = original + u"- [ ] nowe ⏰ 2026-10-15 18:00\n"_s;
        const QDate from(2026, 9, 28);
        const QDate to = from.addDays(41);
        const QDateTime now = dt(2026, 10, 14, 12, 0);
        CalendarCache calendar;
        NotificationCache notes;
        notes.setDays(now.date().addDays(-8), now.date().addDays(15));
        calendar.items(index, from, to);
        notes.upcoming(index, now);
        QList<qint64> cached;
        QList<qint64> stateless;
        qsizetype sink = 0;
        for (int rep = 0; rep < 5; ++rep) {
            index.update(path, rep % 2 ? original : edited);
            QElapsedTimer t;
            t.start();
            sink += calendar.items(index, from, to).size() + notes.upcoming(index, now).size();
            cached.append(t.nsecsElapsed());
            t.restart();
            sink += calendarItems(index, from, to).size() + upcomingNotifications(index, now).size();
            stateless.append(t.nsecsElapsed());
        }
        QVERIFY(sink > 0);
        qInfo("one edit, 3000 notes: cached %.2f ms, stateless %.2f ms", median(cached) / 1e6, median(stateless) / 1e6);
        QVERIFY2(median(cached) <= 0.25 * median(stateless),
                 qPrintable(u"cached %1 ns, stateless %2 ns"_s.arg(median(cached)).arg(median(stateless))));
    }
};

QTEST_GUILESS_MAIN(TestRemindersCache)
#include "tst_reminders_cache.moc"
