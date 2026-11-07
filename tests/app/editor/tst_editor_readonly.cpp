// Reading mode in the block editor (docs/UI.md "Reading mode"): PageController.readOnly refuses
// every edit — no block is edited, nothing changes the page's text or its file, every refusal
// says so with editRefused() —, while reading goes on: selecting and copying blocks, folding,
// zooming, finding, links to what already has an id, and changes from outside reloading the page.
// Turning it on keeps what was being written (the edited block's text, source mode's text).
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Czytanie.md"_s;
const QString Other = u"Notatki/Inna.md"_s;
const QString Content = u"---\ntags: [praca]\n---\n"
                        u"- [ ] Zadanie źdźbło 📅 2026-10-20\n"
                        u"  - dziecko ąę\n"
                        u"- Akapit z przypomnieniem ⏰ 2026-10-16 09:00\n"
                        u"- blok z id ^abc123\n"
                        u"- ostatni ł\n"
                        u"\n"
                        u"| Pozycja | Cena |\n|:--|--:|\n| Żółta farba | 40 |\n| Łata | 89 |\n"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content = Content)
    {
        host.setFile(Page, content);
        host.setFile(Other, u"- [ ] cudze zadanie\n"_s);
        page.setNow(u"2026-10-15 10:00"_s);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
    }
    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    QString edit(int row) const { return role(row, "editText").toString(); }
    int count() const { return model()->rowCount(); }
    /// The first row whose edit text contains `text` (-1: none).
    int rowWith(const QString &text) const
    {
        for (int r = 0; r < count(); ++r) {
            if (edit(r).contains(text))
                return r;
        }
        return -1;
    }
    int rowOfView(const QString &view) const
    {
        for (int r = 0; r < count(); ++r) {
            if (role(r, "view").toString() == view)
                return r;
        }
        return -1;
    }
};

} // namespace

class TestReadOnly : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(Content.contains(u"| Łata |"_s)); // (the table the edits below aim at)
    }

    // Every edit refused: the text, the file and the history stay as they were, and each refusal
    // says so (editRefused).
    void editsRefused_data()
    {
        QTest::addColumn<QString>("edit");
        const QStringList edits{
                u"startEditing"_s, u"setEditingRow"_s, u"setBody"_s, u"setEditText"_s, u"split"_s,
                u"mergeWithPrevious"_s, u"mergeNext"_s, u"toggleList"_s, u"leaveList"_s, u"indent"_s,
                u"outdent"_s, u"moveUp"_s, u"moveDown"_s, u"toggleTask"_s, u"cycleTask"_s,
                u"setTaskStatus"_s, u"removeTaskMarker"_s, u"toggleTaskClock"_s, u"deleteBlock"_s,
                u"insertBlockAfter"_s, u"insertMarkdown"_s, u"appendEmptyBlock"_s, u"startNewPage"_s,
                u"insertToken"_s, u"replaceRange"_s, u"setHighlight"_s, u"pasteText"_s,
                u"ensureBlockRef"_s, u"setReminder"_s, u"setDateToken"_s, u"setRecurrence"_s,
                u"shiftDateToken"_s, u"applyTokenAction"_s, u"taskLink"_s, u"setTableCell"_s,
                u"addTableRow"_s, u"addTableColumn"_s, u"removeTableRow"_s, u"moveTableRow"_s,
                u"setTableColumnAlign"_s, u"sortTable"_s, u"setPageProperty"_s, u"addPageListItem"_s,
                u"removePageListItem"_s, u"setPageIcon"_s, u"setPageCover"_s, u"setMode"_s,
                u"toggleMode"_s, u"sourceMode"_s, u"setSourceText"_s, u"deleteSelection"_s,
                u"cutSelection"_s, u"pasteOverSelection"_s, u"indentSelection"_s,
                u"moveSelectionDown"_s, u"setSelectionTaskStatus"_s, u"cycleSelectionTasks"_s,
                u"undo"_s, u"redo"_s, u"replaceTextRanges"_s, u"setTaskStatusAt"_s, u"toggleTaskAt"_s,
                u"downloadWebPictures"_s};
        for (const QString &edit : edits)
            QTest::newRow(qPrintable(edit)) << edit;
    }
    void editsRefused()
    {
        QFETCH(QString, edit);
        Fixture f;
        const int task = f.rowWith(u"Zadanie"_s);
        const int child = f.rowWith(u"dziecko"_s);
        const int reminder = f.rowWith(u"przypomnieniem"_s);
        const int last = f.rowWith(u"ostatni"_s);
        const int table = f.rowOfView(u"table"_s);
        QVERIFY(task >= 0 && child > task && reminder > child && last > reminder && table > last);
        // An undo step and a redo step to refuse (made before reading mode).
        f.page.setBody(last, u"ostatni łabędź"_s);
        f.page.setBody(last, u"ostatni łoś"_s);
        f.page.undo();
        QVERIFY(f.page.canUndo() && f.page.canRedo());
        f.page.selectRange(child, reminder);

        QSignalSpy readOnlyChanged(&f.page, &PageController::readOnlyChanged);
        f.page.setReadOnly(true);
        QCOMPARE(readOnlyChanged.size(), 1);
        QVERIFY(f.page.readOnly());
        const QString text = f.page.text();
        const QString file = f.host.file(Page);
        const QString other = f.host.file(Other);
        const int writes = f.host.writeCount();
        QGuiApplication::clipboard()->setText(u"- wklejony ż\n"_s);
        QSignalSpy refused(&f.page, &PageController::editRefused);

        const int otherLine = 0;
        if (edit == u"startEditing")
            f.page.startEditing(last, 0);
        else if (edit == u"setEditingRow")
            f.page.setEditingRow(last);
        else if (edit == u"setBody")
            f.page.setBody(last, u"zmieniony"_s);
        else if (edit == u"setEditText")
            f.page.setEditText(last, u"zmieniony"_s, 3);
        else if (edit == u"split")
            f.page.split(last, 3);
        else if (edit == u"mergeWithPrevious")
            f.page.mergeWithPrevious(last);
        else if (edit == u"mergeNext")
            f.page.mergeNext(reminder);
        else if (edit == u"toggleList")
            f.page.toggleList(last);
        else if (edit == u"leaveList")
            f.page.leaveList(last);
        else if (edit == u"indent")
            f.page.indent(last);
        else if (edit == u"outdent")
            f.page.outdent(child);
        else if (edit == u"moveUp")
            f.page.moveUp(last);
        else if (edit == u"moveDown")
            f.page.moveDown(task);
        else if (edit == u"toggleTask")
            f.page.toggleTask(task);
        else if (edit == u"cycleTask")
            f.page.cycleTask(task);
        else if (edit == u"setTaskStatus")
            f.page.setTaskStatus(task, u"done"_s);
        else if (edit == u"removeTaskMarker")
            f.page.removeTaskMarker(task);
        else if (edit == u"toggleTaskClock")
            QVERIFY(!f.page.toggleTaskClock(task));
        else if (edit == u"deleteBlock")
            QCOMPARE(f.page.deleteBlock(last), -1);
        else if (edit == u"insertBlockAfter")
            QCOMPARE(f.page.insertBlockAfter(last, u"nowy"_s), -1);
        else if (edit == u"insertMarkdown")
            QCOMPARE(f.page.insertMarkdown(last, u"- wstawiony\n"_s), -1);
        else if (edit == u"appendEmptyBlock")
            QCOMPARE(f.page.appendEmptyBlock(), -1);
        else if (edit == u"startNewPage")
            QCOMPARE(f.page.startNewPage(QString()), -1);
        else if (edit == u"insertToken")
            f.page.insertToken(last, 0, u"📅 2026-10-30"_s);
        else if (edit == u"replaceRange")
            f.page.replaceRange(last, 0, 3, u"x"_s);
        else if (edit == u"setHighlight")
            QVERIFY(!f.page.setHighlight(last, 0, 3, u"red"_s).value(u"ok"_s).toBool());
        else if (edit == u"pasteText")
            f.page.pasteText(last, 0, 0, u"a\nb"_s);
        else if (edit == u"ensureBlockRef")
            QCOMPARE(f.page.ensureBlockRef(last), QString());
        else if (edit == u"setReminder")
            f.page.setReminder(last, u"2026-10-16 18:00"_s, false, QString());
        else if (edit == u"setDateToken")
            f.page.setDateToken(task, u"due"_s, u"2026-10-21"_s);
        else if (edit == u"setRecurrence")
            f.page.setRecurrence(task, u"every day"_s);
        else if (edit == u"shiftDateToken") {
            const int from = int(f.edit(task).indexOf(u"📅"_s));
            f.page.shiftDateTokenAt(task, from, int(f.edit(task).size()), 1);
        } else if (edit == u"applyTokenAction") {
            const int from = int(f.edit(reminder).indexOf(u"⏰"_s));
            QVERIFY(!f.page.applyTokenAction(reminder, from, int(f.edit(reminder).size()), u"remove"_s));
        } else if (edit == u"taskLink")
            f.page.activateLink(task, u"task:"_s + QString::number(task));
        else if (edit == u"setTableCell")
            f.page.setTableCell(table, 0, 1, u"41"_s);
        else if (edit == u"addTableRow")
            f.page.addTableRow(table);
        else if (edit == u"addTableColumn")
            f.page.addTableColumn(table);
        else if (edit == u"removeTableRow")
            f.page.removeTableRow(table, 0);
        else if (edit == u"moveTableRow")
            f.page.moveTableRow(table, 0, 1);
        else if (edit == u"setTableColumnAlign")
            f.page.setTableColumnAlign(table, 0, u"center"_s);
        else if (edit == u"sortTable")
            QVERIFY(!f.page.sortTable(table, 1, true));
        else if (edit == u"setPageProperty")
            QVERIFY(!f.page.setPageProperty(QString(), u"status"_s, u"w toku"_s, u"text"_s).value(u"ok"_s).toBool());
        else if (edit == u"addPageListItem")
            QVERIFY(!f.page.addPageListItem(u"tags"_s, u"dom"_s).value(u"ok"_s).toBool());
        else if (edit == u"removePageListItem")
            QVERIFY(!f.page.removePageListItem(u"tags"_s, u"praca"_s).value(u"ok"_s).toBool());
        else if (edit == u"setPageIcon")
            QVERIFY(!f.page.setPageIcon(u"emoji"_s, u"📚"_s).value(u"ok"_s).toBool());
        else if (edit == u"setPageCover")
            QVERIFY(!f.page.setPageCover(u"assets/okładka.png"_s).value(u"ok"_s).toBool());
        else if (edit == u"setMode")
            QVERIFY(!f.page.setMode(u"document"_s));
        else if (edit == u"toggleMode")
            QVERIFY(!f.page.toggleMode());
        else if (edit == u"sourceMode") {
            f.page.setSourceMode(true);
            QVERIFY(!f.page.sourceMode());
        } else if (edit == u"setSourceText")
            f.page.setSourceText(u"- całkiem inny tekst\n"_s);
        else if (edit == u"deleteSelection")
            QCOMPARE(f.page.deleteSelection(), -1);
        else if (edit == u"cutSelection")
            QVERIFY(!f.page.cutSelection().value(u"ok"_s).toBool());
        else if (edit == u"pasteOverSelection")
            QCOMPARE(f.page.pasteOverSelection().value(u"row"_s).toInt(), -1);
        else if (edit == u"indentSelection")
            QVERIFY(!f.page.indentSelection());
        else if (edit == u"moveSelectionDown")
            QVERIFY(!f.page.moveSelectionDown());
        else if (edit == u"setSelectionTaskStatus")
            f.page.setSelectionTaskStatus(u"done"_s);
        else if (edit == u"cycleSelectionTasks")
            f.page.cycleSelectionTasks();
        else if (edit == u"undo")
            f.page.undo();
        else if (edit == u"redo")
            f.page.redo();
        else if (edit == u"replaceTextRanges")
            QVERIFY(!f.page.replaceTextRanges({text::TextRange{0, 3}}, u"x"_s));
        else if (edit == u"setTaskStatusAt")
            QVERIFY(!f.page.setTaskStatusAt(Other, otherLine, u"done"_s));
        else if (edit == u"toggleTaskAt")
            QVERIFY(!f.page.toggleTaskAt(Other, otherLine, u"todo"_s));
        else if (edit == u"downloadWebPictures")
            QCOMPARE(f.page.downloadWebPictures(), -1);
        else
            QFAIL("unknown edit");

        QCOMPARE(f.page.text(), text);
        QCOMPARE(f.host.file(Page), file);
        QCOMPARE(f.host.file(Other), other);
        QCOMPARE(f.host.writeCount(), writes);
        QCOMPARE(f.page.editingRow(), -1);
        QVERIFY(f.page.canUndo() && f.page.canRedo());
        QVERIFY(!f.page.dirty());
        QVERIFY2(refused.size() >= 1, qPrintable(edit));
    }

    // Reading goes on: the selection, copying it, folds, zoom, links to what has an id already;
    // a link that would add an id is refused with its own reason, and nothing is written.
    void readingWorks()
    {
        Fixture f;
        f.page.setReadOnly(true);
        const int task = f.rowWith(u"Zadanie"_s);
        const int withId = f.rowWith(u"blok z id"_s);
        const int last = f.rowWith(u"ostatni"_s);
        const QString text = f.page.text();
        QSignalSpy refused(&f.page, &PageController::editRefused);

        f.page.selectRange(task, task);
        QVERIFY(f.page.selectionCount() >= 1);
        const QVariantMap copied = f.page.copySelection(true);
        QVERIFY(copied.value(u"ok"_s).toBool());
        QVERIFY(QGuiApplication::clipboard()->text().contains(u"dziecko ąę"_s));
        f.page.clearSelection();

        f.page.toggleFold(task);
        QVERIFY(f.role(task, "collapsed").toBool());
        f.page.toggleFold(task);
        QVERIFY(!f.role(task, "collapsed").toBool());
        f.page.zoomInto(task);
        QCOMPARE(f.page.zoomBlock(), task);
        f.page.zoomOut();
        QCOMPARE(f.page.zoomBlock(), -1);

        const QVariantMap link = f.page.linkText(withId, u"blockRef"_s);
        QVERIFY(link.value(u"ok"_s).toBool());
        QCOMPARE(link.value(u"text"_s).toString(), u"((abc123))"_s);
        const QVariantMap needsId = f.page.linkText(last, u"blockRef"_s);
        QVERIFY(!needsId.value(u"ok"_s).toBool());
        QCOMPARE(needsId.value(u"reason"_s).toString(), u"readingMode"_s);
        QCOMPARE(f.page.copyLink(-1, u"page"_s).value(u"ok"_s).toBool(), true);

        QCOMPARE(f.page.text(), text);
        QCOMPARE(refused.size(), 0);
        QVERIFY(!f.page.canUndo());
    }

    // A change from outside (sync, another device) still reloads a page being read.
    void externalChangesReload()
    {
        Fixture f(u"- a\n- b\n"_s);
        f.page.setReadOnly(true);
        QSignalSpy changed(&f.page, &PageController::externallyChanged);
        f.host.externalWrite(Page, u"- a\n- b zmienione przez Syncthing ż\n"_s);
        QCOMPARE(f.page.text(), u"- a\n- b zmienione przez Syncthing ż\n"_s);
        QCOMPARE(changed.size(), 1);
        QVERIFY(!f.page.dirty());
    }

    // Turning it on ends the editing and keeps what was typed; source mode's text is applied
    // first. Off again, editing works as before.
    void switchingKeepsWhatWasWritten()
    {
        Fixture f(u"- a\n- b\n"_s);
        f.page.startEditing(1, 1);
        f.page.setEditText(1, u"bąk"_s, 3);
        QCOMPARE(f.page.editingRow(), 1);
        f.page.setReadOnly(true);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.page.text(), u"- a\n- bąk\n"_s);
        QCOMPARE(f.host.file(Page), u"- a\n- bąk\n"_s);

        f.page.setReadOnly(false);
        f.page.setSourceMode(true);
        f.page.setSourceText(u"- a\n- bąk\n- źródło\n"_s);
        f.page.setReadOnly(true);
        QVERIFY(!f.page.sourceMode());
        QCOMPARE(f.page.text(), u"- a\n- bąk\n- źródło\n"_s);
        QCOMPARE(f.host.file(Page), u"- a\n- bąk\n- źródło\n"_s);

        f.page.setReadOnly(false);
        f.page.startEditing(0, 0);
        QCOMPARE(f.page.editingRow(), 0);
        f.page.setEditText(0, u"aż"_s, 2);
        QCOMPARE(f.host.file(Page), u"- aż\n- bąk\n- źródło\n"_s);
    }

    // A drawing opens as a picture (its view offers "Edit drawing"), not in the drawing editor.
    void drawingOpensAsPicture()
    {
        Fixture f(u"- ![[assets/szkic.svg]]\n"_s);
        f.page.openDrawing(u"assets/szkic.svg"_s);
        QCOMPARE(f.host.lastRoute().value(u"view"_s).toString(), u"drawing"_s);
        f.page.setReadOnly(true);
        f.page.openDrawing(u"assets/szkic.svg"_s);
        QCOMPARE(f.host.lastRoute().value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(f.host.lastRoute().value(u"path"_s).toString(), u"assets/szkic.svg"_s);
    }
};

QTEST_MAIN(TestReadOnly)
#include "tst_editor_readonly.moc"
