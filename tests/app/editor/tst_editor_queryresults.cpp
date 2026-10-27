// QueryResultModel (src/app/editor): live ```query results through the host's QueryHost — the
// FakeEditorHost runs them on the real index module — rows for groups and items, rendering,
// checking tasks here and in other files, refreshing after index changes, limits, navigation.
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "queryresultmodel.h"

#include <QAbstractItemModelTester>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Projekty/Dom.md"_s;
const QString Now = u"2026-10-15 10:00"_s;

/// A page with a query block, other pages with tasks, a controller and a results model.
struct Fixture {
    FakeEditorHost host;
    PageController page;
    QueryResultModel results;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &query, const QString &content = QString())
    {
        host.setNow(Now);
        host.setFile(Page, content.isEmpty()
                                   ? u"- [ ] Pomalować ścianę #remont 📅 2026-10-14\n- Notatka\n\n```query\n"_s + query + u"\n```\n"_s
                                   : content);
        host.setFile(u"Zakupy.md"_s,
                     u"- [ ] Mleko #zakupy 📅 2026-10-16\n- [x] Chleb #zakupy\n- [ ] Śrubki do półki **M6** #zakupy #remont\n"_s);
        host.setFile(u"journals/2026-10-15.md"_s, u"- [ ] Zadzwonić do pani Żanety #remont\n"_s);
        page.setNow(Now);
        page.setSaveDelay(100000);
        page.setHost(&host);
        page.setPath(Page);
        results.setRefreshDelay(0);
        results.setController(&page);
        results.setSource(query);
        tester = std::make_unique<QAbstractItemModelTester>(&results, QAbstractItemModelTester::FailureReportingMode::QtTest);
        results.refresh();
    }

    QVariantMap row(int r) const { return results.get(r); }
    QStringList texts() const
    {
        QStringList out;
        for (int r = 0; r < results.rowCount(); ++r)
            out.append(results.get(r).value(u"kind"_s).toString() == u"group"_s
                               ? u"# "_s + results.get(r).value(u"label"_s).toString()
                               : results.get(r).value(u"text"_s).toString());
        return out;
    }
    int rowOf(const QString &prefix) const
    {
        for (int r = 0; r < results.rowCount(); ++r) {
            if (results.get(r).value(u"text"_s).toString().startsWith(prefix))
                return r;
        }
        return -1;
    }
};

} // namespace

class TestQueryResults : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void withoutHostShowsNothing()
    {
        PageController page;
        QueryResultModel results;
        results.setController(&page);
        results.setSource(u"type:tasks"_s);
        results.refresh();
        QVERIFY(!results.available());
        QCOMPARE(results.rowCount(), 0);
        QVERIFY(!results.ready());
    }

    void runsTheQuery()
    {
        Fixture f(u"type:tasks status:open tag:zakupy sort:due"_s);
        QVERIFY(f.results.available());
        QVERIFY(f.results.ready());
        QVERIFY(f.results.ok());
        QCOMPARE(f.results.type(), u"tasks"_s);
        QCOMPARE(f.results.rowCount(), 2);
        QVERIFY2(f.texts().at(0).startsWith(u"Mleko"_s) && f.texts().at(1).startsWith(u"Śrubki do półki"_s),
                 qPrintable(f.texts().join(u" | "_s)));
        QCOMPARE(f.results.total(), 2);
        QCOMPARE(f.results.itemCount(), 2);
        const QVariantMap milk = f.row(0);
        QCOMPARE(milk.value(u"kind"_s).toString(), u"item"_s);
        QCOMPARE(milk.value(u"path"_s).toString(), u"Zakupy.md"_s);
        QCOMPARE(milk.value(u"title"_s).toString(), u"Zakupy"_s);
        QCOMPARE(milk.value(u"line"_s).toInt(), 0);
        QCOMPARE(milk.value(u"status"_s).toString(), u"todo"_s);
        QVERIFY(milk.value(u"isTask"_s).toBool());
        QVERIFY(milk.value(u"showPage"_s).toBool());
        QCOMPARE(milk.value(u"due"_s).toString(), u"2026-10-16"_s);
        QVERIFY(!milk.value(u"overdue"_s).toBool());
        // Rendered like the editor: tags as links, the due date as a chip, no "[ ]" (the view
        // draws the checkbox), Markdown applied, Polish text intact.
        const QString html = milk.value(u"html"_s).toString();
        QVERIFY2(html.contains(u"tag:zakupy"_s), qPrintable(html));
        QVERIFY(html.contains(u"📅&nbsp;jutro"_s));
        QVERIFY(!html.contains(u"[ ]"_s));
        const QString screws = f.row(1).value(u"html"_s).toString();
        QVERIFY2(screws.contains(u"<b>M6</b>"_s) && screws.contains(u"Śrubki do półki"_s), qPrintable(screws));
    }

    void overdueAndJournals()
    {
        Fixture f(u"type:tasks status:open tag:remont"_s);
        QCOMPARE(f.results.itemCount(), 3);
        const int wall = f.rowOf(u"Pomalować"_s);
        QVERIFY(wall >= 0);
        QVERIFY(f.row(wall).value(u"overdue"_s).toBool());
        QVERIFY(!f.row(wall).value(u"showPage"_s).toBool()); // this page: no page name
        const int call = f.rowOf(u"Zadzwonić"_s);
        QCOMPARE(f.row(call).value(u"journal"_s).toString(), u"2026-10-15"_s);
        QCOMPARE(f.row(wall).value(u"journal"_s).toString(), QString());
    }

    void groups()
    {
        Fixture f(u"type:tasks tag:zakupy group:page"_s);
        QVERIFY(f.results.grouped());
        QCOMPARE(f.results.rowCount(), 4); // header + 3 tasks of Zakupy
        const QVariantMap header = f.row(0);
        QCOMPARE(header.value(u"kind"_s).toString(), u"group"_s);
        QCOMPARE(header.value(u"count"_s).toInt(), 3);
        QCOMPARE(header.value(u"path"_s).toString(), u"Zakupy.md"_s);
        QVERIFY(!header.value(u"isTask"_s).toBool());
        // Grouped by page: the items need not name it again.
        QVERIFY(!f.row(1).value(u"showPage"_s).toBool());
        QCOMPARE(f.row(2).value(u"status"_s).toString(), u"done"_s);

        Fixture byStatus(u"type:tasks tag:zakupy group:status"_s);
        QVERIFY(byStatus.results.grouped());
        int groups = 0;
        for (int r = 0; r < byStatus.results.rowCount(); ++r) {
            if (byStatus.row(r).value(u"kind"_s).toString() == u"group"_s)
                ++groups;
            else
                QVERIFY(byStatus.row(r).value(u"showPage"_s).toBool());
        }
        QCOMPARE(groups, 2);
    }

    void pagesAndBlocks()
    {
        Fixture pages(u"type:pages tag:zakupy"_s);
        QCOMPARE(pages.results.type(), u"pages"_s);
        QCOMPARE(pages.texts(), QStringList{u"Zakupy"_s});
        QCOMPARE(pages.row(0).value(u"html"_s).toString(), u"Zakupy"_s);
        QCOMPARE(pages.row(0).value(u"isTask"_s).toBool(), false);

        Fixture blocks(u"type:blocks \"Notatka\""_s);
        QCOMPARE(blocks.results.type(), u"blocks"_s);
        QCOMPARE(blocks.texts(), QStringList{u"Notatka"_s});
        QCOMPARE(blocks.row(0).value(u"path"_s).toString(), Page);
    }

    void togglesATaskInAnotherFile()
    {
        Fixture f(u"type:tasks tag:zakupy"_s);
        const int milk = f.rowOf(u"Mleko"_s);
        QVERIFY(f.results.toggleTask(milk));
        // Shown at once, written by the host.
        QCOMPARE(f.row(milk).value(u"status"_s).toString(), u"done"_s);
        QCOMPARE(f.host.taskEdits(), 1);
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- [x] Mleko #zakupy 📅 2026-10-16 ✅ 2026-10-15\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        // And back.
        f.results.refresh();
        QVERIFY(f.results.toggleTask(f.rowOf(u"Mleko"_s)));
        QVERIFY(f.host.file(u"Zakupy.md"_s).startsWith(u"- [ ] Mleko #zakupy 📅 2026-10-16\n"_s));
        // Not a task: nothing happens.
        Fixture pages(u"type:pages tag:zakupy"_s);
        QVERIFY(!pages.results.toggleTask(0));
        QVERIFY(!pages.results.toggleTask(-1));
        QVERIFY(!pages.results.toggleTask(99));
    }

    void togglesATaskOnThisPage()
    {
        Fixture f(u"type:tasks tag:remont status:open"_s);
        const int wall = f.rowOf(u"Pomalować"_s);
        QVERIFY(f.results.toggleTask(wall));
        // An editing command on the open page (undoable), not a write behind the editor's back.
        QCOMPARE(f.host.taskEdits(), 0);
        QVERIFY(f.page.dirty());
        QVERIFY(f.page.text().startsWith(u"- [x] Pomalować ścianę #remont 📅 2026-10-14 ✅ 2026-10-15\n"_s));
        QCOMPARE(f.row(wall).value(u"status"_s).toString(), u"done"_s);
        f.page.undo();
        QVERIFY(f.page.text().startsWith(u"- [ ] Pomalować ścianę"_s));
    }

    void statusMenuOnResults()
    {
        // Another file: through the host, shown at once.
        Fixture f(u"type:tasks tag:zakupy"_s);
        const int milk = f.rowOf(u"Mleko"_s);
        QVERIFY(f.results.setTaskStatus(milk, u"deferred"_s));
        QCOMPARE(f.row(milk).value(u"status"_s).toString(), u"deferred"_s);
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- [>] Mleko #zakupy 📅 2026-10-16\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        QVERIFY(!f.results.setTaskStatus(milk, u"deferred"_s)); // already
        QVERIFY(!f.results.setTaskStatus(milk, u"nonsense"_s));
        QCOMPARE(f.host.taskEdits(), 1);
        // No status: the marker goes, the row is no task any more until the query runs again.
        QVERIFY(f.results.setTaskStatus(milk, QString()));
        QCOMPARE(f.row(milk).value(u"isTask"_s).toBool(), false);
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- Mleko #zakupy 📅 2026-10-16\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        QVERIFY(!f.results.setTaskStatus(milk, u"todo"_s));
        f.results.refresh();
        QCOMPARE(f.rowOf(u"Mleko"_s), -1); // a type:tasks query no longer lists it
        // This page: an undoable editing command.
        Fixture here(u"type:tasks tag:remont status:open"_s);
        const int wall = here.rowOf(u"Pomalować"_s);
        QVERIFY(here.results.setTaskStatus(wall, u"cancelled"_s));
        QCOMPARE(here.host.taskEdits(), 0);
        QVERIFY(here.page.text().startsWith(u"- [-] Pomalować ścianę #remont 📅 2026-10-14 ❌ 2026-10-15\n"_s));
        here.page.undo();
        QVERIFY(here.page.text().startsWith(u"- [ ] Pomalować ścianę #remont 📅 2026-10-14\n"_s));
        here.results.refresh();
        QVERIFY(here.results.setTaskStatus(here.rowOf(u"Pomalować"_s), QString()));
        QVERIFY(here.page.text().startsWith(u"- Pomalować ścianę #remont 📅 2026-10-14\n"_s));
        // Not a task, or a bad row: nothing happens.
        Fixture pages(u"type:pages tag:zakupy"_s);
        QVERIFY(!pages.results.setTaskStatus(0, u"done"_s));
        QVERIFY(!pages.results.setTaskStatus(-1, u"done"_s));
        QVERIFY(!pages.results.setTaskStatus(99, QString()));
    }

    void dateMenuOnResults()
    {
        // The rendered chips are links naming ranges of the item's `raw` block (its menu); the
        // change goes to the item's own file, written like a hand edit, and can be undone there.
        Fixture f(u"type:tasks tag:zakupy"_s);
        const int r = f.rowOf(u"Mleko"_s);
        QVERIFY(r >= 0);
        const QVariantMap item = f.row(r);
        const QString raw = item.value(u"raw"_s).toString();
        QVERIFY2(raw.contains(u"📅 2026-10-16"_s), qPrintable(raw));
        static const QRegularExpression dateLink(u"href=\"(date:\\d+-\\d+)\""_s);
        const QString url = dateLink.match(item.value(u"html"_s).toString()).captured(1);
        QVERIFY2(!url.isEmpty(), qPrintable(item.value(u"html"_s).toString()));
        // A click on the chip opens the item (its menu is the right button's).
        f.results.activateLink(r, url);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Zakupy.md"_s);
        const QVariantMap chip = f.page.tokenChipIn(raw, url);
        QCOMPARE(chip.value(u"kind"_s).toString(), u"due"_s);
        const QVariantMap done = f.page.editTokenIn(item.value(u"path"_s).toString(), item.value(u"line"_s).toInt(),
                                                    chip.value(u"ref"_s).toMap(), u"move"_s, 0, 0);
        QVERIFY(done.value(u"ok"_s).toBool());
        QCOMPARE(f.host.file(u"Zakupy.md"_s),
                 u"- [ ] Mleko #zakupy 📅 2026-10-15\n- [x] Chleb #zakupy\n- [ ] Śrubki do półki **M6** #zakupy #remont\n"_s);
        QVERIFY(f.page.undoTokenEdit(done.value(u"undo"_s).toInt()));
        QCOMPARE(f.host.file(u"Zakupy.md"_s),
                 u"- [ ] Mleko #zakupy 📅 2026-10-16\n- [x] Chleb #zakupy\n- [ ] Śrubki do półki **M6** #zakupy #remont\n"_s);
    }

    void dateMenuOnThisPagesResults()
    {
        Fixture f(u"type:tasks tag:remont"_s);
        const int r = f.rowOf(u"Pomalować"_s);
        QVERIFY(r >= 0);
        const QVariantMap item = f.row(r);
        static const QRegularExpression dateLink(u"href=\"(date:\\d+-\\d+)\""_s);
        const QString url = dateLink.match(item.value(u"html"_s).toString()).captured(1);
        const QVariantMap chip = f.page.tokenChipIn(item.value(u"raw"_s).toString(), url);
        const QVariantMap done = f.page.editTokenIn(Page, item.value(u"line"_s).toInt(), chip.value(u"ref"_s).toMap(),
                                                    u"shift"_s, 7, 0);
        QVERIFY(done.value(u"ok"_s).toBool());
        QCOMPARE(done.value(u"undo"_s).toInt(), 0); // the editor's own undo step
        QVERIFY(f.page.text().startsWith(u"- [ ] Pomalować ścianę #remont 📅 2026-10-21\n"_s));
        f.page.undo();
        QVERIFY(f.page.text().startsWith(u"- [ ] Pomalować ścianę #remont 📅 2026-10-14\n"_s));
    }

    void followsIndexChanges()
    {
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QSignalSpy changed(&f.results, &QueryResultModel::resultChanged);
        QCOMPARE(f.results.itemCount(), 2);
        f.host.externalWrite(u"Ogród.md"_s, u"- [ ] Nasiona marchwi #zakupy\n"_s);
        QTRY_COMPARE(f.results.itemCount(), 3);
        QVERIFY(changed.size() >= 1);
        // The same results again: no reset of the rows.
        QSignalSpy resets(&f.results, &QAbstractItemModel::modelReset);
        f.results.refresh();
        QCOMPARE(resets.size(), 0);
    }

    void heldRowsWaitForTheMenu()
    {
        // A status menu open on a row holds the rows: an index change (sync, an edit in another
        // note) would rebuild them and take the menu away. The run comes when it lets go.
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QSignalSpy held(&f.results, &QueryResultModel::heldChanged);
        QObject menu;
        QVERIFY(!f.results.held());
        f.results.hold(&menu);
        QVERIFY(f.results.held());
        QCOMPARE(held.size(), 1);
        f.results.hold(&menu); // the same holder twice: still one
        QCOMPARE(held.size(), 1);
        QSignalSpy resets(&f.results, &QAbstractItemModel::modelReset);
        f.host.externalWrite(u"Ogród.md"_s, u"- [ ] Nasiona marchwi #zakupy\n"_s);
        QTest::qWait(100);
        QCOMPARE(f.results.itemCount(), 2);
        QCOMPARE(resets.size(), 0);
        // The query edited meanwhile waits as well.
        f.results.setSource(u"type:tasks tag:zakupy status:open sort:due"_s);
        QTest::qWait(50);
        QCOMPARE(resets.size(), 0);
        // Let go: one run with everything that came meanwhile.
        f.results.release(&menu);
        QVERIFY(!f.results.held());
        QCOMPARE(held.size(), 2);
        QTRY_COMPARE(f.results.itemCount(), 3);
        QVERIFY(f.rowOf(u"Nasiona marchwi"_s) >= 0);
        // Releasing again, or something that holds nothing, changes nothing.
        f.results.release(&menu);
        f.results.release(nullptr);
        f.results.hold(nullptr);
        QVERIFY(!f.results.held());
        QCOMPARE(held.size(), 2);
    }

    void heldByTwoHolders()
    {
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QObject first;
        QObject second;
        f.results.hold(&first);
        f.results.hold(&second);
        f.host.externalWrite(u"Ogród.md"_s, u"- [ ] Nasiona marchwi #zakupy\n"_s);
        f.results.release(&first);
        QVERIFY(f.results.held());
        QTest::qWait(100);
        QCOMPARE(f.results.itemCount(), 2);
        f.results.release(&second);
        QTRY_COMPARE(f.results.itemCount(), 3);
    }

    void aDestroyedHolderLetsGo()
    {
        // The view is destroyed with its menu open: nothing stays held, no run is lost.
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        auto menu = std::make_unique<QObject>();
        f.results.hold(menu.get());
        f.host.externalWrite(u"Ogród.md"_s, u"- [ ] Nasiona marchwi #zakupy\n"_s);
        QTest::qWait(100);
        QCOMPARE(f.results.itemCount(), 2);
        menu.reset();
        QVERIFY(!f.results.held());
        QTRY_COMPARE(f.results.itemCount(), 3);
        // Without anything waiting, letting go runs nothing.
        auto other = std::make_unique<QObject>();
        f.results.hold(other.get());
        QSignalSpy changed(&f.results, &QueryResultModel::resultChanged);
        other.reset();
        QTest::qWait(100);
        QCOMPARE(changed.size(), 0);
        // The model goes first: the holder's end later reaches nothing.
        auto holder = std::make_unique<QObject>();
        {
            Fixture gone(u"type:tasks tag:zakupy"_s);
            gone.results.hold(holder.get());
        }
        holder.reset();
    }

    void statusChosenWhileHeld()
    {
        // The menu's own choice: the row shows it at once and stays (the menu closes on its own);
        // the run that drops the waiting task comes once the menu let go.
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QObject menu;
        f.results.hold(&menu);
        const int milk = f.rowOf(u"Mleko"_s);
        QSignalSpy resets(&f.results, &QAbstractItemModel::modelReset);
        QVERIFY(f.results.setTaskStatus(milk, u"deferred"_s));
        QCOMPARE(f.row(milk).value(u"status"_s).toString(), u"deferred"_s);
        QVERIFY(f.host.file(u"Zakupy.md"_s).startsWith(u"- [>] Mleko #zakupy"_s));
        QTest::qWait(100);
        QCOMPARE(resets.size(), 0);
        QCOMPARE(f.results.itemCount(), 2);
        f.results.release(&menu);
        QTRY_COMPARE(f.results.itemCount(), 1);
        QCOMPARE(f.rowOf(u"Mleko"_s), -1);
    }

    void statusChosenOnHeldRowsFollowsTheFile()
    {
        // While the rows wait, the file changed: a line above the task moved it down. The status
        // goes to the same task, not to whatever is on its old line now.
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QObject menu;
        f.results.hold(&menu);
        const int milk = f.rowOf(u"Mleko"_s);
        QCOMPARE(f.row(milk).value(u"line"_s).toInt(), 0);
        f.host.externalWrite(u"Zakupy.md"_s, u"- [ ] Jajka #zakupy\n- [ ] Mleko #zakupy 📅 2026-10-16\n- [x] Chleb #zakupy\n"
                                             u"- [ ] Śrubki do półki **M6** #zakupy #remont\n"_s);
        QTest::qWait(100);
        QCOMPARE(f.row(milk).value(u"line"_s).toInt(), 0); // the rows waited
        QVERIFY(f.results.setTaskStatus(milk, u"done"_s));
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- [ ] Jajka #zakupy\n- [x] Mleko #zakupy 📅 2026-10-16 ✅ 2026-10-15\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        // The task is gone from the file meanwhile: nothing is written anywhere.
        const int screws = f.rowOf(u"Śrubki"_s);
        f.host.externalWrite(u"Zakupy.md"_s, u"- [ ] Jajka #zakupy\n- [x] Mleko #zakupy\n- [ ] Ćwiczyć #zakupy\n"_s);
        QTest::qWait(100);
        const int edits = f.host.taskEdits();
        QVERIFY(!f.results.setTaskStatus(screws, u"cancelled"_s));
        QVERIFY(!f.results.toggleTask(screws));
        QCOMPARE(f.host.taskEdits(), edits);
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [ ] Jajka #zakupy\n- [x] Mleko #zakupy\n- [ ] Ćwiczyć #zakupy\n"_s);
        f.results.release(&menu);
        QTRY_VERIFY(f.rowOf(u"Ćwiczyć"_s) >= 0);
        QCOMPARE(f.rowOf(u"Śrubki"_s), -1);
    }

    void heldRowFollowsItsTaskWhoseStatusChanged()
    {
        // The rows wait while the index changes: the row's task is found again by its text, not
        // by its source line, which holds the status marker and the date tokens. The user's own
        // choice from the menu, then a click on the same checkbox before the rows catch up.
        Fixture f(u"type:tasks tag:zakupy"_s);
        QObject menu;
        f.results.hold(&menu);
        const int milk = f.rowOf(u"Mleko"_s);
        QVERIFY(f.results.setTaskStatus(milk, u"doing"_s));
        QVERIFY(f.host.file(u"Zakupy.md"_s).startsWith(u"- [/] Mleko #zakupy 📅 2026-10-16\n"_s));
        f.host.externalWrite(u"Zakupy.md"_s, u"- [ ] Jajka #zakupy\n"_s + f.host.file(u"Zakupy.md"_s));
        QTest::qWait(100);
        QCOMPARE(f.row(milk).value(u"line"_s).toInt(), 0); // the rows waited
        QVERIFY(f.results.toggleTask(milk));
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- [ ] Jajka #zakupy\n- [x] Mleko #zakupy 📅 2026-10-16 ✅ 2026-10-15\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        QCOMPARE(f.row(milk).value(u"status"_s).toString(), u"done"_s);
        // Once more, after another change: the row knows where its task was written last.
        f.host.externalWrite(u"Zakupy.md"_s, u"- [ ] Ser\n"_s + f.host.file(u"Zakupy.md"_s));
        QTest::qWait(100);
        QVERIFY(f.results.toggleTask(milk));
        QVERIFY2(f.host.file(u"Zakupy.md"_s).startsWith(u"- [ ] Ser\n- [ ] Jajka #zakupy\n- [ ] Mleko #zakupy 📅 2026-10-16\n"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));

        // Another device changed only the task's status and its due date while the menu was open:
        // the choice still goes to that task.
        const int screws = f.rowOf(u"Śrubki"_s);
        QString content = f.host.file(u"Zakupy.md"_s);
        content.replace(u"- [ ] Śrubki do półki **M6** #zakupy #remont"_s, u"- [>] Śrubki do półki **M6** #zakupy #remont 📅 2026-11-02"_s);
        f.host.externalWrite(u"Zakupy.md"_s, content);
        QTest::qWait(100);
        QVERIFY(f.results.setTaskStatus(screws, u"cancelled"_s));
        QVERIFY2(f.host.file(u"Zakupy.md"_s).contains(u"- [-] Śrubki do półki **M6** #zakupy #remont 📅 2026-11-02"_s),
                 qPrintable(f.host.file(u"Zakupy.md"_s)));
        f.results.release(&menu);
        QTRY_VERIFY(f.rowOf(u"Ser"_s) < 0 && f.rowOf(u"Jajka"_s) >= 0);
    }

    void heldRowPrefersTheNearestOfTwoSameTasks()
    {
        // Two tasks with the same text: the row's own (its status changed from the menu) is the
        // nearest one to where it was, not the other one that still has the old source line.
        Fixture f(u"type:tasks tag:zakupy"_s, u"- Notatka\n"_s);
        f.host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko #zakupy\n- [x] Chleb #zakupy\n- [ ] Mleko #zakupy\n"_s);
        f.results.refresh();
        QObject menu;
        f.results.hold(&menu);
        const int first = f.rowOf(u"Mleko"_s);
        QCOMPARE(f.row(first).value(u"line"_s).toInt(), 0);
        QVERIFY(f.results.setTaskStatus(first, u"doing"_s));
        f.host.externalWrite(u"Zakupy.md"_s, u"- [ ] Ser\n"_s + f.host.file(u"Zakupy.md"_s));
        QTest::qWait(100);
        QVERIFY(f.results.setTaskStatus(first, u"deferred"_s));
        QCOMPARE(f.host.file(u"Zakupy.md"_s), u"- [ ] Ser\n- [>] Mleko #zakupy\n- [x] Chleb #zakupy\n- [ ] Mleko #zakupy\n"_s);
        f.results.release(&menu);
    }

    void followsTheQueryText()
    {
        Fixture f(u"type:tasks tag:zakupy"_s);
        QCOMPARE(f.results.itemCount(), 3);
        f.results.setSource(u"type:tasks tag:zakupy status:done"_s);
        QTRY_COMPARE(f.results.itemCount(), 1);
        QVERIFY(f.texts().at(0).startsWith(u"Chleb"_s));
    }

    void errorsAndEmptyQueries()
    {
        Fixture bad(u"kolor:zielony"_s);
        QVERIFY(bad.results.ready());
        QVERIFY(!bad.results.ok());
        QVERIFY(!bad.results.error().isEmpty());
        QCOMPARE(bad.results.rowCount(), 0);

        // An empty block lists nothing (and does not ask the host for every block).
        Fixture empty(u"  "_s);
        QCOMPARE(empty.host.queryRuns(), 0);
        QVERIFY(empty.results.ok());
        QCOMPARE(empty.results.rowCount(), 0);

        Fixture failing(u"type:tasks"_s);
        failing.host.setQueriesFail(true);
        failing.results.refresh();
        QVERIFY(!failing.results.ok());
        QCOMPARE(failing.results.rowCount(), 0);
    }

    void limitsRowsAndShowsMore()
    {
        QString content = u"```query\ntype:tasks tag:dużo\n```\n"_s;
        for (int i = 0; i < 120; ++i)
            content += u"- [ ] Zadanie %1 #dużo\n"_s.arg(i);
        Fixture f(u"type:tasks tag:dużo"_s, content);
        QCOMPARE(f.results.itemCount(), 120);
        QCOMPARE(f.results.rowCount(), 50);
        QCOMPARE(f.results.hiddenCount(), 70);
        f.results.showMore();
        QCOMPARE(f.results.rowCount(), 100);
        f.results.showMore();
        QCOMPARE(f.results.rowCount(), 120);
        QCOMPARE(f.results.hiddenCount(), 0);
        // The query's own limit: total counts every match.
        f.results.setSource(u"type:tasks tag:dużo limit:5"_s);
        f.results.refresh();
        QCOMPARE(f.results.itemCount(), 5);
        QCOMPARE(f.results.total(), 120);
    }

    void opensItems()
    {
        Fixture f(u"type:tasks tag:remont"_s);
        const QVariantMap here = f.results.open(f.rowOf(u"Pomalować"_s));
        QCOMPARE(here.value(u"action"_s).toString(), u"scrollTo"_s);
        QCOMPARE(here.value(u"row"_s).toInt(), 0);
        QSignalSpy nav(&f.host, &FakeEditorHost::navigated);
        QVERIFY(f.results.open(f.rowOf(u"Zadzwonić"_s)).isEmpty());
        QCOMPARE(nav.size(), 1);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"journals/2026-10-15.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"line"_s).toInt(), 0);
        // Links in an item's text go through the editor's link handling; task links open it.
        f.results.activateLink(0, u"tag:remont"_s);
        QCOMPARE(f.host.lastRoute().value(u"view"_s).toString(), u"tag"_s);
        f.results.activateLink(f.rowOf(u"Zadzwonić"_s), u"task:toggle"_s);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"journals/2026-10-15.md"_s);
    }

    void sameItemPageLinks()
    {
        // [[#^id]] / [[#Heading]] in an item of another page name that page: its block is shown
        // and opened there, not looked for on the query's page.
        Fixture f(u"type:tasks tag:remont"_s);
        f.host.setFile(u"Inna.md"_s,
                       u"## Cele\n\n- [ ] Zadanie z [[#^cel1]] i [[#Cele]] #remont\n- Cel główny ^cel1\n"_s);
        f.results.refresh();
        const int row = f.rowOf(u"Zadanie z"_s);
        QVERIFY(row >= 0);
        QVERIFY2(f.row(row).value(u"html"_s).toString().contains(u">Cel główny<"_s),
                 qPrintable(f.row(row).value(u"html"_s).toString()));
        QSignalSpy nav(&f.host, &FakeEditorHost::navigated);
        QVERIFY(f.results.activateLink(row, u"page:#%5Ecel1"_s).isEmpty());
        QCOMPARE(nav.size(), 1);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"blockId"_s).toString(), u"cel1"_s);
        QVERIFY(f.results.activateLink(row, u"page:#Cele"_s).isEmpty());
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"Inna.md"_s);
        QCOMPARE(f.host.lastRoute().value(u"heading"_s).toString(), u"Cele"_s);
        // An item of the query's own page: its headings and blocks are here.
        const QVariantMap here = f.results.activateLink(f.rowOf(u"Pomalować"_s), u"page:#%5Ebrak"_s);
        QCOMPARE(here.value(u"action"_s).toString(), u"notFound"_s);
        QCOMPARE(nav.size(), 2);
    }

    void rendersWithTheThemeAndLanguage()
    {
        Fixture f(u"type:tasks tag:zakupy status:open"_s);
        QVERIFY(f.row(0).value(u"html"_s).toString().contains(u"jutro"_s));
        f.page.setLocale(u"en"_s);
        QVERIFY(f.row(0).value(u"html"_s).toString().contains(u"tomorrow"_s));
        f.page.setPalette({{u"tagFg"_s, u"#123456"_s}});
        QVERIFY(f.row(0).value(u"html"_s).toString().contains(u"#123456"_s, Qt::CaseInsensitive));
    }
};

QTEST_MAIN(TestQueryResults)
#include "tst_editor_queryresults.moc"
