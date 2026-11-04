// Task dependencies (🆔 / ⛔ across notes: blocked, blocking, missing ids, cycles), the `is:` query
// filter kept incrementally (QueryCache, the change log) and time summaries of :LOGBOOK: clocks.
#include "centraleuropeantime.h"
#include "index/querycache.h"
#include "index/tasktime.h"
#include "index/vaultindex.h"
#include "markdown/edit.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;

namespace {

const QDate Today(2026, 11, 3);

Query parsed(const QString &source)
{
    const auto r = parseQuery(source);
    if (const auto *error = std::get_if<QString>(&r))
        qWarning("unexpected query error: %s", qPrintable(*error));
    return std::get<Query>(r);
}

QStringList texts(const QueryResult &r)
{
    QStringList out;
    for (const QueryItem &i : r.items)
        out.append(i.task ? i.task->text : QString());
    return out;
}

QStringList texts(const QList<TaskRef> &refs)
{
    QStringList out;
    for (const TaskRef &t : refs)
        out.append(t.task.text);
    return out;
}

/// The block of the task with `text` in `path`, -1 when none.
int blockOf(const VaultIndex &index, const VaultPath &path, const QString &text)
{
    for (const TaskRef &t : index.tasksOf(path)) {
        if (t.task.text == text)
            return t.blockIndex;
    }
    return -1;
}

void fill(VaultIndex &index)
{
    index.update(u"Budowa.md"_s, u"- [ ] Wylać fundament 🆔 fund\n"
                                 "- [ ] Postawić ściany 🆔 sciany ⛔ fund\n"
                                 "- [ ] Dach ⛔ fund,sciany 📅 2026-11-20\n"
                                 "- [x] Projekt gotowy 🆔 proj\n"
                                 "- [ ] Pozwolenie ⛔ proj\n"_s);
    index.update(u"journals/2026-11-03.md"_s, u"- [ ] Zamówić żwir ⛔ fund\n"
                                              "- [ ] Nieznane ⛔ brak-takiego\n"
                                              "- [-] Anulowane 🆔 anul\n"
                                              "- [ ] Po anulowanym ⛔ anul\n"
                                              "- [x] Zrobione choć czeka ⛔ fund\n"
                                              "- [ ] Kura 🆔 kura ⛔ jajko\n"
                                              "- [ ] Jajko 🆔 jajko ⛔ kura\n"
                                              "- [ ] Sam na siebie 🆔 sam ⛔ sam\n"
                                              "- [ ] Dwa razy 🆔 dwa\n"
                                              "- [x] Dwa razy, zrobione 🆔 dwa\n"
                                              "- [ ] Czeka na dwa ⛔ dwa\n"_s);
}

} // namespace

class TstIndexTasks : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { brulion::test::useCentralEuropeanTime("CET-1CEST,M3.5.0,M10.5.0/3"); }

    void blocked_data()
    {
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("blocked");
        QTest::addColumn<bool>("blocking");
        QTest::addColumn<bool>("missing");
        QTest::addColumn<bool>("cycle");
        const QString b = u"Budowa.md"_s;
        const QString j = u"journals/2026-11-03.md"_s;
        QTest::newRow("an open id blocks") << b << u"Postawić ściany"_s << true << true << false << false;
        QTest::newRow("the first one blocks the others")
                << b << u"Wylać fundament"_s << false << true << false << false;
        QTest::newRow("two ids") << b << u"Dach"_s << true << false << false << false;
        QTest::newRow("done dependency") << b << u"Pozwolenie"_s << false << false << false << false;
        QTest::newRow("done id blocks nothing") << b << u"Projekt gotowy"_s << false << false << false << false;
        QTest::newRow("in another note") << j << u"Zamówić żwir"_s << true << false << false << false;
        QTest::newRow("missing id: not blocked, flagged") << j << u"Nieznane"_s << false << false << true << false;
        QTest::newRow("cancelled dependency") << j << u"Po anulowanym"_s << false << false << false << false;
        QTest::newRow("a done task is never blocked")
                << j << u"Zrobione choć czeka"_s << false << false << false << false;
        QTest::newRow("cycle") << j << u"Kura"_s << true << true << false << true;
        QTest::newRow("cycle, other end") << j << u"Jajko"_s << true << true << false << true;
        QTest::newRow("itself") << j << u"Sam na siebie"_s << false << false << false << true;
        QTest::newRow("duplicated id, one open") << j << u"Czeka na dwa"_s << true << false << false << false;
    }
    void blocked()
    {
        QFETCH(QString, path);
        QFETCH(QString, text);
        QFETCH(bool, blocked);
        QFETCH(bool, blocking);
        QFETCH(bool, missing);
        QFETCH(bool, cycle);
        VaultIndex index;
        fill(index);
        const int block = blockOf(index, path, text);
        QVERIFY(block >= 0);
        QCOMPARE(index.isTaskBlocked(path, block), blocked);
        QCOMPARE(index.isTaskBlocking(path, block), blocking);
        const TaskDependencies deps = index.taskDependencies(path, block);
        QCOMPARE(deps.blocked, blocked);
        QCOMPARE(deps.missing, missing);
        QCOMPARE(deps.cycle, cycle);
    }

    void dependencyDetails()
    {
        VaultIndex index;
        fill(index);
        const int roof = blockOf(index, u"Budowa.md"_s, u"Dach"_s);
        const TaskDependencies deps = index.taskDependencies(u"Budowa.md"_s, roof);
        QCOMPARE(deps.dependsOn.size(), 2);
        QCOMPARE(deps.dependsOn[0].id, u"fund"_s);
        QCOMPARE(texts(deps.dependsOn[0].tasks), QStringList{u"Wylać fundament"_s});
        QVERIFY(deps.dependsOn[0].open);
        QCOMPARE(texts(deps.dependsOn[1].tasks), QStringList{u"Postawić ściany"_s});
        const int base = blockOf(index, u"Budowa.md"_s, u"Wylać fundament"_s);
        const TaskDependencies waiting = index.taskDependencies(u"Budowa.md"_s, base);
        // Open tasks waiting for it, notes by path (the done one left out).
        QCOMPARE(texts(waiting.dependents), (QStringList{u"Postawić ściany"_s, u"Dach"_s, u"Zamówić żwir"_s}));
        QCOMPARE(texts(index.tasksWithId(u"dwa"_s)), (QStringList{u"Dwa razy"_s, u"Dwa razy, zrobione"_s}));
        QVERIFY(index.hasTaskId(u"fund"_s));
        QVERIFY(!index.hasTaskId(u"FUND"_s)); // case-sensitive
        QVERIFY(!index.hasTaskId(u"brak-takiego"_s));
        // Not a task: nothing.
        QCOMPARE(index.taskDependencies(u"Budowa.md"_s, 99).dependsOn.size(), 0);
        QVERIFY(!index.isTaskBlocked(u"Nie ma.md"_s, 0));
    }

    void queries_data()
    {
        QTest::addColumn<QString>("query");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("is:blocked") << u"is:blocked path:Budowa"_s << QStringList{u"Postawić ściany"_s, u"Dach"_s};
        QTest::newRow("-is:blocked") << u"-is:blocked status:open path:Budowa"_s
                                     << QStringList{u"Wylać fundament"_s, u"Pozwolenie"_s};
        QTest::newRow("is:blocking") << u"is:blocking"_s
                                     << QStringList{u"Wylać fundament"_s, u"Postawić ściany"_s, u"Kura"_s, u"Jajko"_s,
                                                    u"Dwa razy"_s};
        QTest::newRow("polish") << u"jest:zablokowane path:Budowa"_s << QStringList{u"Postawić ściany"_s, u"Dach"_s};
        QTest::newRow("or") << u"is:blocked,blocking path:Budowa"_s
                            << QStringList{u"Wylać fundament"_s, u"Postawić ściany"_s, u"Dach"_s};
        QTest::newRow("sorted") << u"is:blocked sort:due path:Budowa"_s << QStringList{u"Dach"_s, u"Postawić ściany"_s};
    }
    void queries()
    {
        QFETCH(QString, query);
        QFETCH(QStringList, expected);
        VaultIndex index;
        fill(index);
        const Query q = parsed(query);
        QCOMPARE(q.type, QueryType::Tasks); // is: is a task filter
        QCOMPARE(texts(runQuery(q, index, Today)), expected);
    }

    void parseErrors_data()
    {
        QTest::addColumn<QString>("query");
        QTest::newRow("unknown value") << u"is:zielone"_s;
        QTest::newRow("empty") << u"is:"_s;
        QTest::newRow("pages") << u"type:pages is:blocked"_s;
    }
    void parseErrors()
    {
        QFETCH(QString, query);
        const auto r = parseQuery(query);
        QVERIFY(std::holds_alternative<QString>(r));
        QVERIFY(!std::get<QString>(r).isEmpty());
    }

    // Completing the task an id names unblocks the tasks waiting for it in other notes: the
    // change log names them and a QueryCache follows.
    void incremental()
    {
        VaultIndex index;
        fill(index);
        const Query q = parsed(u"is:blocked"_s);
        QueryCache cache;
        cache.update(q, index, Today);
        QCOMPARE(cache.result(index).total, runQuery(q, index, Today).total);
        const int version = index.version();
        const QString budowa = index.content(u"Budowa.md"_s);
        index.update(u"Budowa.md"_s, markdown::setTaskStatus(budowa, 0, TaskStatus::Done, Today));
        const IndexChanges changes = index.changesSince(version);
        QVERIFY(changes.notes.contains(u"journals/2026-11-03.md"_s));
        cache.update(q, index, Today);
        QCOMPARE(texts(cache.result(index)), texts(runQuery(q, index, Today)));
        QVERIFY(!texts(cache.result(index)).contains(u"Zamówić żwir"_s));
        QVERIFY(texts(cache.result(index)).contains(u"Dach"_s)); // still waits for the walls

        // The id goes: waiting tasks are no longer blocked by it (missing instead).
        const int v2 = index.version();
        index.remove(u"Budowa.md"_s);
        QVERIFY(index.changesSince(v2).notes.contains(u"journals/2026-11-03.md"_s));
        const int j = blockOf(index, u"journals/2026-11-03.md"_s, u"Zamówić żwir"_s);
        QVERIFY(index.taskDependencies(u"journals/2026-11-03.md"_s, j).missing);
        // It comes back with a new note: blocked again, logged.
        const int v3 = index.version();
        index.update(u"Nowa.md"_s, u"- [/] Wylać fundament 🆔 fund\n"_s);
        QVERIFY(index.changesSince(v3).notes.contains(u"journals/2026-11-03.md"_s));
        QVERIFY(index.isTaskBlocked(u"journals/2026-11-03.md"_s, j));
        cache.update(q, index, Today);
        QCOMPARE(texts(cache.result(index)), texts(runQuery(q, index, Today)));
        // A task that starts waiting: the note with the id is logged (it becomes blocking).
        const int v4 = index.version();
        index.update(u"Inna.md"_s, u"- [ ] nowe ⛔ fund\n"_s);
        QVERIFY(index.changesSince(v4).notes.contains(u"Nowa.md"_s));
    }

    void clocks()
    {
        VaultIndex index;
        index.update(u"Praca.md"_s, u"tags:: praca\n\n"
                                    "- DOING Raport o żółwiach #pilne\n"
                                    "  :LOGBOOK:\n"
                                    "  CLOCK: [2026-11-02 Mon 23:30:00]--[2026-11-03 Tue 00:30:00] =>  01:00:00\n"
                                    "  CLOCK: [2026-11-03 Tue 10:00:00]\n"
                                    "  :END:\n"
                                    "- [x] Spotkanie\n"
                                    "  :LOGBOOK:\n"
                                    "  CLOCK: [2026-11-03 Tue 08:00:00]--[2026-11-03 Tue 08:45:00] =>  00:45:00\n"
                                    "  :END:\n"
                                    "- [ ] Bez czasu\n"_s);
        index.update(u"journals/2026-11-01.md"_s,
                     u"- [x] Niedziela\n"
                     "  :LOGBOOK:\n"
                     "  CLOCK: [2026-11-01 Sun 12:00:00]--[2026-11-01 Sun 12:20:00] =>  00:20:00\n"
                     "  :END:\n"_s);
        QCOMPARE(texts(index.runningClocks()), QStringList{u"Raport o żółwiach #pilne"_s});
        QCOMPARE(texts(runQuery(parsed(u"is:running"_s), index, Today)), QStringList{u"Raport o żółwiach #pilne"_s});
        QCOMPARE(texts(runQuery(parsed(u"is:tracked sort:page"_s), index, Today)),
                 (QStringList{u"Niedziela"_s, u"Raport o żółwiach #pilne"_s, u"Spotkanie"_s}));
        const QDateTime now(Today, QTime(10, 30));
        QCOMPARE(taskSpentSeconds(index.runningClocks().first().task, now), qint64(3600 + 1800));

        const QList<TaskRef> all = index.tasks();
        const TimeSummary days = summarizeTime(index, all, TimeGroup::Day, {}, {}, now);
        QCOMPARE(days.rows.size(), 3);
        QCOMPARE(days.rows[0].key, u"2026-11-01"_s);
        QCOMPARE(days.rows[1].seconds, qint64(30 * 60));                     // 2 Nov 23:30–24:00
        QCOMPARE(days.rows[2].seconds, qint64(30 * 60 + 30 * 60 + 45 * 60)); // 3 Nov
        QCOMPARE(days.rows[2].tasks, 2);
        QCOMPARE(days.total, qint64((20 + 90 + 45) * 60));
        QCOMPARE(days.tasks, 3);

        const TimeSummary today = summarizeTime(index, all, TimeGroup::Page, Today, Today, now);
        QCOMPARE(today.rows.size(), 1);
        QCOMPARE(today.rows[0].label, u"Praca"_s);
        QCOMPARE(today.rows[0].seconds, qint64(105 * 60));
        QCOMPARE(today.rows[0].tasks, 2);

        const TimeSummary tags = summarizeTime(index, all, TimeGroup::Tag, {}, {}, QDateTime());
        // Without `now` the running clock does not count; the page tag and the block tag.
        QCOMPARE(tags.rows.size(), 3);
        QCOMPARE(tags.rows[0].key, u"praca"_s);
        QCOMPARE(tags.rows[0].seconds, qint64(105 * 60));
        QCOMPARE(tags.rows[1].key, u"pilne"_s);
        QCOMPARE(tags.rows[1].seconds, qint64(60 * 60));
        QCOMPARE(tags.rows[2].key, QString());  // no tag
        QCOMPARE(tags.total, qint64(125 * 60)); // each task once

        const TimeSummary tasks = summarizeTime(index, all, TimeGroup::Task, {}, {}, now);
        QCOMPARE(tasks.rows.first().label, u"Raport o żółwiach #pilne"_s);
        QCOMPARE(tasks.rows.first().path, u"Praca.md"_s);
        QCOMPARE(tasks.rows.first().line, 2);
        QCOMPARE(tasks.rows.first().seconds, qint64(90 * 60));
    }
};

QTEST_GUILESS_MAIN(TstIndexTasks)
#include "tst_index_tasks.moc"
