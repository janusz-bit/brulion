// What the hooks hear (src/core/extensions/events.h): a task's status found again after an edit —
// ticked in the editor or by an extension, a Logseq keyword, a recurring task's completion, tasks of
// one text, the dates ticking adds —, and a burst of changes as each extension's hooks get it: never
// its own writes, every other extension's, a sync's and the editor's, a daily note made, a chain of
// hooks answering hooks cut off.

#include "extensions/events.h"
#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::extensions;

namespace {

QList<TaskState> tasksOf(const QString &content, const VaultPath &path = u"Dom.md"_s)
{
    return taskStates(markdown::parseNote(path, content), content);
}

QString describe(const QList<TaskChange> &changes)
{
    QStringList out;
    for (const TaskChange &c : changes)
        out << u"%1:[%2]→[%3]@%4"_s.arg(c.after.plain, c.before.status, c.after.status).arg(c.after.line + 1);
    return out.join(u"; "_s);
}

NoteChange change(const QString &path, Origin origin, const QString &by = QString(), int depth = 0)
{
    NoteChange c;
    c.path = path;
    c.page = path.section(u'/', -1).chopped(3);
    c.origin = origin;
    c.by = by;
    c.depth = depth;
    return c;
}

} // namespace

class TestExtensionsEvents : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tasksFoundAgain_data()
    {
        QTest::addColumn<QString>("before");
        QTest::addColumn<QString>("after");
        QTest::addColumn<QString>("changes");
        QTest::newRow("ticked") << u"- [ ] Zapłacić czynsz 📅 2026-11-13\n"_s
                                << u"- [x] Zapłacić czynsz 📅 2026-11-13 ✅ 2026-11-14\n"_s
                                << u"Zapłacić czynsz:[ ]→[x]@1"_s; // (plain: without the task's fields)
        QTest::newRow("reopened") << u"- [x] Kupić mleko ✅ 2026-11-14 10:05\n"_s << u"- [ ] Kupić mleko\n"_s
                                  << u"Kupić mleko:[x]→[ ]@1"_s;
        QTest::newRow("in progress, a line above added")
                << u"- [ ] Pomalować pokój\n"_s << u"- nowy punkt\n- [/] Pomalować pokój\n"_s
                << u"Pomalować pokój:[ ]→[/]@2"_s;
        QTest::newRow("cancelled") << u"- [ ] Żółw\n"_s << u"- [-] Żółw ❌ 2026-11-14\n"_s << u"Żółw:[ ]→[-]@1"_s;
        QTest::newRow("Logseq keyword") << u"- TODO Zadzwonić do mamy\n"_s << u"- DONE Zadzwonić do mamy\n"_s
                                        << u"Zadzwonić do mamy:[ ]→[x]@1"_s;
        // The next occurrence goes above the done one, with the next date: the done one changed.
        QTest::newRow("recurring") << u"- [ ] Podlać kwiaty 🔁 co tydzień 📅 2026-11-14\n"_s
                                   << u"- [ ] Podlać kwiaty 🔁 co tydzień 📅 2026-11-21\n"
                                      "- [x] Podlać kwiaty 🔁 co tydzień 📅 2026-11-14 ✅ 2026-11-14\n"_s
                                   << u"Podlać kwiaty:[ ]→[x]@2"_s;
        QTest::newRow("one text twice, the second ticked")
                << u"- [ ] pompki\n- [ ] pompki\n"_s << u"- [ ] pompki\n- [x] pompki\n"_s << u"pompki:[ ]→[x]@2"_s;
        QTest::newRow("rewritten and ticked: no task's change")
                << u"- [ ] Kupić chleb\n"_s << u"- [x] Kupić chleb żytni\n"_s << QString();
        QTest::newRow("added done") << u"- a\n"_s << u"- a\n- [x] Nowe zadanie\n"_s << QString();
        QTest::newRow("removed") << u"- [ ] Stare\n- [ ] Inne\n"_s << u"- [ ] Inne\n"_s << QString();
        QTest::newRow("nothing changed") << u"- [ ] A\n- [x] B\n"_s << u"- [ ] A\n- [x] B\n"_s << QString();
        QTest::newRow("CRLF, nested") << u"- Projekt\r\n  - [ ] krok pierwszy\r\n"_s
                                      << u"- Projekt\r\n  - [x] krok pierwszy\r\n"_s << u"krok pierwszy:[ ]→[x]@2"_s;
        QTest::newRow("malformed lines") << u"- [ \n- []\n- [ ] ok\n"_s << u"- [x \n- [x]\n- [x] ok\n"_s
                                         << u"ok:[ ]→[x]@3"_s;
    }
    void tasksFoundAgain()
    {
        QFETCH(QString, before);
        QFETCH(QString, after);
        QFETCH(QString, changes);
        QCOMPARE(describe(changedTasks(tasksOf(before), tasksOf(after))), changes);
    }

    void taskStatesOfANote()
    {
        const QList<TaskState> tasks = tasksOf(u"# Dom\n\n- [ ] Zapłacić czynsz #dom\n- tekst\n  - [x] [[Książki/Lalka|Lalka]] ✅ 2026-11-01\n"_s);
        QCOMPARE(tasks.size(), 2);
        QCOMPARE(tasks[0].line, 2);
        QCOMPARE(tasks[0].status, u" "_s);
        QCOMPARE(tasks[0].text, u"Zapłacić czynsz #dom"_s);
        QCOMPARE(tasks[0].plain, u"Zapłacić czynsz"_s);
        QCOMPARE(tasks[1].status, u"x"_s);
        QCOMPARE(tasks[1].key, u"[[Książki/Lalka|Lalka]]"_s); // (the done date is no part of it)
        QCOMPARE(tasks[1].plain, u"Lalka"_s);
        QVERIFY(tasksOf(QString()).isEmpty());
    }

    void noEchoes()
    {
        // Two extensions and the editor: each hears everyone's changes but its own.
        ChangeBatch batch;
        QVERIFY(batch.isEmpty());
        QVERIFY(batch.add(change(u"Projekty.md"_s, Origin::Extension, u"indeks-projektow"_s, 1)));
        QVERIFY(batch.add(change(u"Dom.md"_s, Origin::Extension, u"przeglad-zadan"_s, 1)));
        QVERIFY(batch.add(change(u"Zakupy.md"_s, Origin::App)));
        const QVariantMap index = batch.changeEvent(u"indeks-projektow"_s);
        QCOMPARE(index.value(u"pages"_s).toStringList(), (QStringList{u"Dom"_s, u"Zakupy"_s}));
        QCOMPARE(index.value(u"paths"_s).toStringList(), (QStringList{u"Dom.md"_s, u"Zakupy.md"_s}));
        QCOMPARE(index.value(u"by"_s).toStringList(), QStringList{u"przeglad-zadan"_s});
        QCOMPARE(index.value(u"sources"_s).toStringList(), (QStringList{u"extension"_s, u"app"_s}));
        const QVariantMap review = batch.changeEvent(u"przeglad-zadan"_s);
        QCOMPARE(review.value(u"pages"_s).toStringList(), (QStringList{u"Projekty"_s, u"Zakupy"_s}));
        QCOMPARE(batch.changeEvent(u"trzecie"_s).value(u"pages"_s).toStringList().size(), 3);
        QCOMPARE(batch.depthFor(u"indeks-projektow"_s), 1);
        QCOMPARE(batch.depthFor(u"trzecie"_s), 1);

        // What only one extension wrote is nothing for it.
        ChangeBatch own;
        own.add(change(u"Projekty.md"_s, Origin::Extension, u"indeks-projektow"_s, 1));
        QVERIFY(own.changeEvent(u"indeks-projektow"_s).isEmpty());
        QCOMPARE(own.changeEvent(u"inne"_s).value(u"by"_s).toStringList(), QStringList{u"indeks-projektow"_s});
        QCOMPARE(own.depthFor(u"indeks-projektow"_s), 0);
    }

    void pagesOfOneNameAreTwoPaths()
    {
        ChangeBatch batch;
        batch.add(change(u"Projekty/Dom.md"_s, Origin::Sync));
        batch.add(change(u"Archiwum/Dom.md"_s, Origin::Sync));
        batch.add(change(u"Archiwum/Dom.md"_s, Origin::App)); // (the same page again: once)
        const QVariantMap e = batch.changeEvent(u"x"_s);
        QCOMPARE(e.value(u"pages"_s).toStringList(), QStringList{u"Dom"_s});
        QCOMPARE(e.value(u"paths"_s).toStringList(), (QStringList{u"Projekty/Dom.md"_s, u"Archiwum/Dom.md"_s}));
        QCOMPARE(e.value(u"sources"_s).toStringList(), (QStringList{u"sync"_s, u"app"_s}));
        QVERIFY(e.value(u"by"_s).toStringList().isEmpty());
    }

    void chainsEnd()
    {
        ChangeBatch batch;
        QVERIFY(batch.add(change(u"A.md"_s, Origin::Extension, u"a"_s, ChangeBatch::kMaxDepth)));
        QVERIFY(!batch.add(change(u"B.md"_s, Origin::Extension, u"b"_s, ChangeBatch::kMaxDepth + 1)));
        QCOMPARE(batch.dropped(), 1);
        QCOMPARE(batch.changeEvent(u"c"_s).value(u"pages"_s).toStringList(), QStringList{u"A"_s});
        QCOMPARE(batch.depthFor(u"c"_s), ChangeBatch::kMaxDepth);
        batch.clear();
        QVERIFY(batch.isEmpty());
        QCOMPARE(batch.dropped(), 0);
    }

    void taskEvents()
    {
        ChangeBatch batch;
        NoteChange note = change(u"journals/2026-11-14.md"_s, Origin::Extension, u"przeglad-zadan"_s, 1);
        note.page = u"2026-11-14"_s;
        note.date = u"2026-11-14"_s;
        batch.add(note);
        const QString before = u"- [ ] Zapłacić czynsz\n"_s, after = u"- [x] Zapłacić czynsz ✅ 2026-11-14\n"_s;
        batch.addTasks(note, changedTasks(tasksOf(before, note.path), tasksOf(after, note.path)));
        QVERIFY(batch.taskEvents(u"przeglad-zadan"_s).isEmpty()); // its own tick
        const QList<QVariantMap> events = batch.taskEvents(u"indeks-projektow"_s);
        QCOMPARE(events.size(), 1);
        const QVariantMap e = events.first();
        QCOMPARE(e.value(u"task"_s).toString(), u"x"_s);
        QCOMPARE(e.value(u"previous"_s).toString(), u" "_s);
        QCOMPARE(e.value(u"plain"_s).toString(), u"Zapłacić czynsz"_s);
        QCOMPARE(e.value(u"line"_s).toInt(), 1);
        QCOMPARE(e.value(u"date"_s).toString(), u"2026-11-14"_s);
        QCOMPARE(e.value(u"source"_s).toString(), u"extension"_s);
        QCOMPARE(e.value(u"by"_s).toString(), u"przeglad-zadan"_s);
        QVERIFY(e.value(u"ref"_s).toString().startsWith(u"journals/2026-11-14.md#L1@"_s));
    }

    void taskFlood()
    {
        // A sync of a year's journal does not run a hook ten thousand times.
        ChangeBatch batch;
        QString before, after;
        for (int i = 0; i < ChangeBatch::kMaxTasks + 50; ++i) {
            before += u"- [ ] zadanie %1\n"_s.arg(i);
            after += u"- [x] zadanie %1\n"_s.arg(i);
        }
        const NoteChange note = change(u"Dużo.md"_s, Origin::Sync);
        batch.add(note);
        batch.addTasks(note, changedTasks(tasksOf(before), tasksOf(after)));
        QCOMPARE(batch.taskEvents(u"x"_s).size(), ChangeBatch::kMaxTasks);
        QCOMPARE(batch.dropped(), 50);
    }

    void journalEvents()
    {
        ChangeBatch batch;
        NoteChange made = change(u"journals/2026-11-15.md"_s, Origin::App);
        made.created = true;
        made.date = u"2026-11-15"_s;
        batch.add(made);
        NoteChange edited = change(u"journals/2026-11-14.md"_s, Origin::App);
        edited.date = u"2026-11-14"_s; // (not made: changed)
        batch.add(edited);
        NoteChange page = change(u"Nowa.md"_s, Origin::App);
        page.created = true; // (no daily note)
        batch.add(page);
        NoteChange synced = change(u"journals/2026-11-16.md"_s, Origin::Sync);
        synced.created = true;
        synced.date = u"2026-11-16"_s;
        batch.add(synced);
        const QList<QVariantMap> events = batch.journalEvents(u"x"_s);
        QCOMPARE(events.size(), 2);
        QCOMPARE(events[0].value(u"date"_s).toString(), u"2026-11-15"_s);
        QCOMPARE(events[0].value(u"source"_s).toString(), u"app"_s);
        QCOMPARE(events[1].value(u"source"_s).toString(), u"sync"_s);
        QCOMPARE(originName(Origin::Other), u"other"_s);
    }
};

QTEST_GUILESS_MAIN(TestExtensionsEvents)
#include "tst_extensions_events.moc"
