// The block selection of PageController (docs/UI.md "Selecting and copying"): ranges with their
// subtrees (folded children too), Shift+↑/↓ steps, select all (also in a zoom), the selection
// following edits, undo / redo and external changes, copying (Markdown, the host's HTML, plain
// text, the size limit, exact characters of an edited block), cut and delete as one undo step,
// indenting, moving and task statuses of a range in both note modes, a paste over a selection,
// block ids a paste must not duplicate (kept after a cut: a move), HTML pasted from other apps,
// and the read-only page of the "Zaznacz tekst" view. Offscreen QGuiApplication: a real
// clipboard in the process.
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include <QAbstractItemModelTester>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;
const QString Other = u"Inna.md"_s;

struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content, int saveDelay = 0)
    {
        host.setFile(Page, content);
        host.setFile(Other, u"- Blok z innej strony ^inny1\n- Drugi blok\n"_s);
        page.setNow(u"2026-10-15 10:00"_s);
        page.setSaveDelay(saveDelay);
        page.setHost(&host);
        page.setPath(Page);
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(),
                                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = page.blocks()->roleNames();
        return page.blocks()->data(page.blocks()->index(row), roles.key(name));
    }
    QString text() const { return page.text(); }
    QList<int> selected() const
    {
        QList<int> out;
        for (const QVariant &v : page.selectedRows())
            out.append(v.toInt());
        return out;
    }
    QStringList edges() const
    {
        QStringList out;
        for (int r = 0; r < page.blocks()->rowCount(); ++r)
            out.append(role(r, "selectionEdge").toString());
        return out;
    }
};

const QString Outline = u"- a\n\t- a1\n\t- a2\n- b\n\t- b1\n- c\n- d\n"_s;

QString clipboardText()
{
    return QGuiApplication::clipboard()->text();
}

} // namespace

class TestSelection : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void rangesTakeWholeSubtrees()
    {
        Fixture f(Outline);
        f.page.setFolded(3, true); // b with its child b1
        QSignalSpy changed(&f.page, &PageController::selectionChanged);
        f.page.selectRange(1, 3);
        QCOMPARE(f.selected(), QList<int>({1, 2, 3, 4})); // the folded b1 too
        QCOMPARE(f.page.selectionCount(), 3);             // what is shown
        QCOMPARE(f.page.selectionAnchorRow(), 1);
        QCOMPARE(f.page.selectedRow(), 3);
        QCOMPARE(f.page.selectionFirstRow(), 1);
        QCOMPARE(f.page.selectionLastRow(), 3);
        QVERIFY(f.page.multiSelection());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(f.edges(), QStringList({u""_s, u"first"_s, u"middle"_s, u"last"_s, u""_s, u""_s, u""_s}));
        QVERIFY(f.role(4, "isSelected").toBool());
        // A parent alone brings its children; the band's ends follow.
        f.page.selectRange(0, 0);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2}));
        QCOMPARE(f.edges(), QStringList({u"first"_s, u"middle"_s, u"last"_s, u""_s, u""_s, u""_s, u""_s}));
        // Backwards is the same range.
        f.page.selectRange(5, 3);
        QCOMPARE(f.selected(), QList<int>({3, 4, 5}));
        QCOMPARE(f.page.selectedRow(), 3);
        // One block: "single"; editing ends a selection.
        f.page.setSelectedRow(6);
        QCOMPARE(f.edges().at(6), u"single"_s);
        QVERIFY(!f.page.multiSelection());
        f.page.startEditing(5, 0);
        QCOMPARE(f.page.selectedRow(), -1);
        QCOMPARE(f.page.selectionCount(), 0);
        QCOMPARE(f.edges().join(QString()), QString());
        // Starting a range stops editing.
        f.page.selectRange(5, 6);
        QCOMPARE(f.page.editingRow(), -1);
        f.page.clearSelection();
        QCOMPARE(f.selected(), QList<int>());
        f.page.selectRange(-1, 2);
        QCOMPARE(f.selected(), QList<int>());
    }

    void frontmatterIsNeverInARange()
    {
        Fixture f(u"---\ntytuł: Zakupy\n---\ntyp:: lista\n\n- mleko\n- chleb\n"_s);
        f.page.selectRange(0, 3);
        QCOMPARE(f.selected(), QList<int>({2, 3}));
        f.page.selectAll();
        QCOMPARE(f.selected(), QList<int>({2, 3}));
        f.page.selectRange(2, 2);
        QVERIFY(!f.page.extendSelection(-1)); // nothing above the first block
        // A lone frontmatter can still be selected (Esc from its editor).
        f.page.setSelectedRow(0);
        QCOMPARE(f.selected(), QList<int>({0}));
        QVERIFY(f.page.copySelection().value(u"reason"_s).toString() == u"empty"_s);
    }

    void shiftArrowsStepOverSelectedSubtrees()
    {
        Fixture f(Outline);
        f.page.startEditing(1, 0); // a1
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.selected(), QList<int>({1, 2}));
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.selected(), QList<int>({1, 2, 3, 4}));
        QVERIFY(f.page.extendSelection(1)); // over b1, inside b's subtree
        QCOMPARE(f.page.selectedRow(), 5);
        QCOMPARE(f.selected(), QList<int>({1, 2, 3, 4, 5}));
        QVERIFY(f.page.extendSelection(-1));
        QCOMPARE(f.page.selectedRow(), 4); // b1: b's subtree still whole? no — up to b1
        QCOMPARE(f.selected(), QList<int>({1, 2, 3, 4}));
        QVERIFY(f.page.extendSelection(-1)); // b1 -> b changes nothing: on to a2
        QCOMPARE(f.selected(), QList<int>({1, 2}));
        QVERIFY(f.page.extendSelection(-1));
        QCOMPARE(f.selected(), QList<int>({1}));
        QVERIFY(f.page.extendSelection(-1)); // past the anchor: a, with its subtree
        QCOMPARE(f.page.selectedRow(), 0);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2}));
        QVERIFY(!f.page.extendSelection(-1));
        // Down from a parent jumps past its children.
        f.page.selectRange(0, 0);
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.page.selectedRow(), 3);
        // Shrinking from the top.
        f.page.selectRange(5, 3);
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.selected(), QList<int>({4, 5}));
        // Hidden rows are stepped over.
        f.page.setFolded(3, true);
        f.page.selectRange(2, 2);
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.page.selectedRow(), 3);
        QVERIFY(f.page.extendSelection(1));
        QCOMPARE(f.page.selectedRow(), 5);
        QVERIFY(f.page.extendSelection(1));
        QVERIFY(!f.page.extendSelection(1));
        QVERIFY(!PageController().extendSelection(1));
    }

    void selectAllAndZoom()
    {
        Fixture f(Outline);
        f.page.selectAll();
        QCOMPARE(f.selected().size(), 7);
        QCOMPARE(f.page.selectionCount(), 7);
        f.page.zoomInto(3);                 // b
        QCOMPARE(f.page.selectedRow(), -1); // a zoom ends a selection
        f.page.selectAll();
        QCOMPARE(f.selected(), QList<int>({3, 4}));
        // The zoomed block cannot be indented, moved, outdented or replaced.
        QVERIFY(!f.page.indentSelection());
        QVERIFY(!f.page.moveSelectionUp());
        QVERIFY(!f.page.outdentSelection());
        f.page.selectRange(4, 4);
        QVERIFY(!f.page.outdentSelection()); // out of the zoom
        f.page.zoomTo(-1);
        QCOMPARE(f.page.selectedRow(), -1);
    }

    void selectionFollowsEditsUndoAndOtherDevices()
    {
        Fixture f(Outline);
        f.page.selectRange(3, 5);
        // An edit of another block, then undo / redo.
        f.page.setBody(0, u"a zmienione"_s);
        QCOMPARE(f.selected(), QList<int>({3, 4, 5}));
        f.page.undo();
        QCOMPARE(f.selected(), QList<int>({3, 4, 5}));
        // A change from another device inserts a block above: the rows move with their blocks.
        f.host.externalWrite(Page, u"- nowy z telefonu\n"_s + f.text());
        QCOMPARE(f.text().left(18), u"- nowy z telefonu\n"_s);
        QCOMPARE(f.selected(), QList<int>({4, 5, 6}));
        QCOMPARE(f.page.selectionAnchorRow(), 4);
        // The selected blocks deleted elsewhere: nothing selected.
        f.host.externalWrite(Page, u"- a\n"_s);
        QCOMPARE(f.page.selectedRow(), -1);
        QCOMPARE(f.page.selectionCount(), 0);
    }

    void copyPutsMarkdownAndHtml()
    {
        Fixture f(u"# Plan\n\n- [ ] Kupić żółć i\u00a0źdźbło\n\t- [x] mleko ✅ 2026-10-15\n- chleb\n\nKoniec\n"_s);
        f.page.selectRange(1, 3);
        const QVariantMap r = f.page.copySelection();
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"count"_s).toInt(), 3);
        const QString md = u"- [ ] Kupić żółć i\u00a0źdźbło\n\t- [x] mleko ✅ 2026-10-15\n- chleb"_s;
        QCOMPARE(clipboardText(), md);
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasHtml());
        QCOMPARE(mime->html(), u"<pre>"_s + md.toHtmlEscaped() + u"</pre>"_s);
        QCOMPARE(f.page.selectionMarkdown(), md);
        // As plain text: no markup, no HTML.
        QVERIFY(f.page.copySelection(true).value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"☐ Kupić żółć i\u00a0źdźbło\n  ☑ mleko ✅ 2026-10-15\n• chleb"_s);
        QVERIFY(!QGuiApplication::clipboard()->mimeData()->hasHtml());
        // Nothing selected: nothing copied, the clipboard keeps what it had.
        f.page.clearSelection();
        QCOMPARE(f.page.copySelection().value(u"reason"_s).toString(), u"empty"_s);
        QCOMPARE(clipboardText(), u"☐ Kupić żółć i\u00a0źdźbło\n  ☑ mleko ✅ 2026-10-15\n• chleb"_s);
        // The page is untouched by copying.
        QCOMPARE(f.page.canUndo(), false);
    }

    void tooLargeIsRefusedNotCut()
    {
        QString content;
        const QString line = u"- "_s + QString(200, u'ż') + u'\n'; // 403 bytes in UTF-8
        for (qsizetype n = 0; n < PageController::ClipboardLimit / 403 + 10; ++n)
            content += line;
        Fixture f(content);
        QGuiApplication::clipboard()->setText(u"poprzednie"_s);
        f.page.selectAll();
        const QVariantMap r = f.page.copySelection();
        QVERIFY(!r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"reason"_s).toString(), u"tooLarge"_s);
        QCOMPARE(clipboardText(), u"poprzednie"_s);
        // A cut that cannot copy deletes nothing.
        QCOMPARE(f.page.cutSelection().value(u"reason"_s).toString(), u"tooLarge"_s);
        QCOMPARE(f.text(), content);
    }

    void cutIsOneUndoStep()
    {
        const QString content = u"Wstęp\r\n\r\n- a\r\n\t- a1\r\n- b\r\n- c\r\n"_s;
        Fixture f(content);
        f.page.selectRange(1, 3);
        const QVariantMap r = f.page.cutSelection();
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"count"_s).toInt(), 3);
        QCOMPARE(clipboardText(), u"- a\n\t- a1\n- b"_s);
        QCOMPARE(f.text(), u"Wstęp\r\n\r\n- c\r\n"_s);
        QCOMPARE(f.page.selectedRow(), 0); // the block before
        f.page.undo();
        QCOMPARE(f.text(), content); // byte for byte, CRLF too
        QCOMPARE(f.selected(), QList<int>({1, 2, 3}));
        f.page.redo();
        QCOMPARE(f.text(), u"Wstęp\r\n\r\n- c\r\n"_s);
        QCOMPARE(f.page.selectedRow(), 0);
    }

    void deleteSelectsTheBlockBefore()
    {
        Fixture f(Outline);
        f.page.selectRange(3, 5);
        QCOMPARE(f.page.deleteSelection(), 2);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- a2\n- d\n"_s);
        QCOMPARE(f.page.selectedRow(), 2);
        f.page.undo();
        QCOMPARE(f.text(), Outline);
        QCOMPARE(f.selected(), QList<int>({3, 4, 5}));
        // From the top: the new first block.
        f.page.selectRange(0, 0);
        QCOMPARE(f.page.deleteSelection(), 0);
        QCOMPARE(f.text(), u"- b\n\t- b1\n- c\n- d\n"_s);
        // Everything.
        f.page.selectAll();
        QCOMPARE(f.page.deleteSelection(), -1);
        QCOMPARE(f.text(), QString());
        QCOMPARE(f.page.selectedRow(), -1);
        f.page.undo();
        QCOMPARE(f.text(), u"- b\n\t- b1\n- c\n- d\n"_s);
    }

    void indentOutdentAndMove()
    {
        Fixture f(Outline);
        f.page.selectRange(5, 6); // c, d
        QVERIFY(f.page.indentSelection());
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- a2\n- b\n\t- b1\n\t- c\n\t- d\n"_s);
        QCOMPARE(f.selected(), QList<int>({5, 6}));
        QVERIFY(f.page.outdentSelection());
        QCOMPARE(f.text(), Outline);
        // All or nothing.
        f.page.selectRange(0, 3);
        QVERIFY(!f.page.indentSelection());
        QCOMPARE(f.text(), Outline);
        // Moving a run of siblings with their children; one undo step each.
        f.page.selectRange(3, 5);
        QVERIFY(f.page.moveSelectionUp());
        QCOMPARE(f.text(), u"- b\n\t- b1\n- c\n- a\n\t- a1\n\t- a2\n- d\n"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2}));
        QVERIFY(!f.page.moveSelectionUp());
        QVERIFY(f.page.moveSelectionDown());
        QCOMPARE(f.text(), Outline);
        f.page.undo();
        QCOMPARE(f.text(), u"- b\n\t- b1\n- c\n- a\n\t- a1\n\t- a2\n- d\n"_s);
        // A folded parent that gets new children shows them.
        Fixture folded(u"- a\n\t- a1\n- b\n- c\n"_s);
        folded.page.setFolded(0, true);
        folded.page.selectRange(2, 3);
        QVERIFY(folded.page.indentSelection());
        QVERIFY(!folded.role(0, "collapsed").toBool());
    }

    void documentModeIndentsListItemsOnly()
    {
        Fixture f(u"---\nbrulion: document\n---\n# Plan\n\nAkapit\n\n- a\n- b\n- c\n"_s);
        QVERIFY(f.page.documentMode());
        f.page.selectRange(2, 4); // a paragraph and list items
        QVERIFY(!f.page.indentSelection());
        f.page.selectRange(4, 5);
        QVERIFY(f.page.indentSelection());
        QCOMPARE(f.text(), u"---\nbrulion: document\n---\n# Plan\n\nAkapit\n\n- a\n\t- b\n\t- c\n"_s);
        QVERIFY(f.page.outdentSelection());
        QCOMPARE(f.text(), u"---\nbrulion: document\n---\n# Plan\n\nAkapit\n\n- a\n- b\n- c\n"_s);
        f.page.selectRange(1, 2);
        QVERIFY(f.page.copySelection().value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"# Plan\n\nAkapit"_s);
    }

    void taskStatusesOfASelection()
    {
        Fixture f(u"- mleko\n- [x] chleb ✅ 2026-10-14\n\nAkapit\n\n# Nagłówek\n"_s);
        f.page.selectRange(0, 3);
        f.page.cycleSelectionTasks(); // the first has none: todo for all that can have one
        QCOMPARE(f.text(), u"- [ ] mleko\n- [ ] chleb\n\n- [ ] Akapit\n\n# Nagłówek\n"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2, 3}));
        f.page.cycleSelectionTasks();
        QCOMPARE(f.text(), u"- [/] mleko\n- [/] chleb\n\n- [/] Akapit\n\n# Nagłówek\n"_s);
        f.page.setSelectionTaskStatus(QString());
        QCOMPARE(f.text(), u"- mleko\n- chleb\n\n- Akapit\n\n# Nagłówek\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), u"- [/] mleko\n- [/] chleb\n\n- [/] Akapit\n\n# Nagłówek\n"_s);
    }

    void pasteReplacesTheSelection()
    {
        Fixture f(Outline);
        f.page.selectRange(3, 5); // b, b1, c
        QVariantMap r = f.page.replaceSelection(u"- nowy\r\n\t- pod nowym\r\n"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- a2\n- nowy\n\t- pod nowym\n- d\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 4);
        QCOMPARE(f.selected(), QList<int>({3, 4}));
        f.page.undo();
        QCOMPARE(f.text(), Outline);
        // Into a nested position: the pasted blocks take the depth of the first selected one.
        f.page.selectRange(2, 2); // a2
        f.page.replaceSelection(u"- x\n- y"_s);
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- x\n\t- y\n- b\n\t- b1\n- c\n- d\n"_s);
        // Nothing to paste: nothing happens.
        f.page.selectRange(0, 0);
        QCOMPARE(f.page.replaceSelection(u"\n\n"_s).value(u"row"_s).toInt(), -1);
        // From the clipboard.
        QGuiApplication::clipboard()->setText(u"Akapit z zewnątrz"_s);
        f.page.selectRange(7, 7);
        f.page.pasteOverSelection();
        QVERIFY(f.text().endsWith(u"- c\n- Akapit z zewnątrz\n"_s));
    }

    void copyThenPasteRoundTrips()
    {
        const QString content =
                u"- [ ] Kupić\n\t- [x] mleko\n\t\t- 2% ✅ 2026-10-15\n\t- chleb\n- **Pogrubione** [[Strona|link]]\n"_s;
        Fixture from(content);
        from.page.selectAll();
        QVERIFY(from.page.copySelection().value(u"ok"_s).toBool());
        Fixture to(u"- \n"_s);
        to.page.startEditing(0, 0);
        to.page.pasteClipboard(0, 0, 0);
        QCOMPARE(to.text(), content);
        // Into a nested empty item: the structure keeps its relative depths.
        Fixture nested(u"- x\n\t- \n"_s);
        nested.page.pasteText(1, 0, 0, from.page.selectionMarkdown());
        QCOMPARE(
                nested.text(),
                u"- x\n\t- [ ] Kupić\n\t\t- [x] mleko\n\t\t\t- 2% ✅ 2026-10-15\n\t\t- chleb\n\t- **Pogrubione** [[Strona|link]]\n"_s);
    }

    void pastedIdsAreNeverDuplicated()
    {
        // A copy: an id another page has (or this one) is dropped.
        Fixture f(u"- tu ^tu1\n- \n"_s, 100000);
        f.page.pasteText(1, 0, 0, u"- Blok z innej strony ^inny1\n- nowy ^wolny1\n- kopia ^tu1\n"_s);
        QCOMPARE(f.text(), u"- tu ^tu1\n- Blok z innej strony\n- nowy ^wolny1\n- kopia\n"_s);
        // A cut and paste is a move: the ids go with the blocks (the vault still lists them).
        Fixture move(u"- jeden ^id1\n- dwa ^id2\n- trzy\n- \n"_s, 100000);
        move.page.selectRange(0, 1);
        QVERIFY(move.page.cutSelection().value(u"ok"_s).toBool());
        QCOMPARE(move.text(), u"- trzy\n- \n"_s);
        QVERIFY(move.host.blockIdTaken(u"id1"_s)); // not saved yet
        move.page.startEditing(1, 0);
        move.page.pasteClipboard(1, 0, 0);
        QCOMPARE(move.text(), u"- trzy\n- jeden ^id1\n- dwa ^id2\n"_s);
        // A second paste of the same cut is a copy.
        const int row = move.page.insertBlockAfter(2);
        move.page.pasteText(row, 0, 0, u"- jeden ^id1\n- dwa ^id2"_s);
        QCOMPARE(move.text(), u"- trzy\n- jeden ^id1\n- dwa ^id2\n- jeden\n- dwa\n"_s);
        // Pasting a selection over itself keeps its ids.
        Fixture self(u"- a ^aa1\n- b\n"_s, 100000);
        self.page.save();
        self.page.selectRange(0, 0);
        QVERIFY(self.page.copySelection().value(u"ok"_s).toBool());
        self.page.pasteOverSelection();
        QCOMPARE(self.text(), u"- a ^aa1\n- b\n"_s);
    }

    void htmlFromOtherAppsBecomesMarkdown()
    {
        Fixture f(u"- \n"_s);
        auto *mime = new QMimeData;
        mime->setText(u"Zakupy\nmleko\nchleb"_s);
        mime->setHtml(u"<h2>Zakupy</h2><ul><li>mleko</li><li><b>chleb</b></li></ul>"_s);
        QGuiApplication::clipboard()->setMimeData(mime);
        const QString converted = f.page.clipboardText();
        QVERIFY2(converted.contains(u"## Zakupy"_s) && converted.contains(u"- mleko"_s)
                         && converted.contains(u"**chleb**"_s),
                 qPrintable(converted));
        // Markdown-looking text is pasted as it is (Obsidian, an editor).
        mime = new QMimeData;
        mime->setText(u"- [ ] zadanie\n- **ważne**"_s);
        mime->setHtml(u"<p>- [ ] zadanie</p>"_s);
        QGuiApplication::clipboard()->setMimeData(mime);
        QCOMPARE(f.page.clipboardText(), u"- [ ] zadanie\n- **ważne**"_s);
        // This app's own copy: its text is the Markdown.
        Fixture source(u"Zwykły akapit\n"_s);
        source.page.selectRange(0, 0);
        source.page.copySelection();
        QCOMPARE(f.page.clipboardText(), u"Zwykły akapit"_s);
        QGuiApplication::clipboard()->clear();
        QCOMPARE(f.page.clipboardText(), QString());
        QCOMPARE(f.page.pasteClipboard(0, 0, 0).value(u"row"_s).toInt(), -1);
    }

    void copyEditTextKeepsEveryCharacter()
    {
        Fixture f(u"- [ ] Jadę w\u00a0góry z\u00a0Anią 📅 2026-10-20\n"_s);
        const QString edit = f.role(0, "editText").toString();
        QCOMPARE(edit, u"Jadę w\u00a0góry z\u00a0Anią 📅 2026-10-20"_s);
        QVERIFY(f.page.copyEditText(0, 5, 18).value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"w\u00a0góry z\u00a0Anią"_s);
        QVERIFY(f.page.copyEditText(0, 18, 5).value(u"ok"_s).toBool()); // either order
        QCOMPARE(f.page.copyEditText(0, 3, 3).value(u"reason"_s).toString(), u"empty"_s);
        QCOMPARE(f.page.copyEditText(9, 0, 3).value(u"reason"_s).toString(), u"empty"_s);
    }

    void readingHtmlIsEscapedAndComplete()
    {
        Fixture f(
                u"---\ntytuł: x\n---\n# Plan <b>wyjazdu</b>\n\n- [ ] Kupić bilety\n\t- [x] mapa\n1. pierwszy\n\n"
                "```js\nif (a < b) {}\n```\n\n| a | b |\n|---|---|\n| 1 | **2** |\n\n> cytat\n\n---\n\nKoniec w\u00a0domu\n"_s);
        const QString html = f.page.readingHtml();
        QVERIFY(!html.contains(u"tytuł"_s));                                        // no frontmatter
        QVERIFY2(html.contains(u"&lt;b&gt;wyjazdu&lt;/b&gt;"_s), qPrintable(html)); // never raw HTML
        QVERIFY(html.contains(u"<h1"_s));
        QVERIFY(html.contains(u"☐ Kupić bilety"_s));
        QVERIFY(html.contains(u"☑ mapa"_s));
        QVERIFY(html.contains(u"1. pierwszy"_s));
        QVERIFY(html.contains(u"if (a &lt; b) {}"_s));
        QVERIFY(html.contains(u"<table"_s) && html.contains(u"<td>1</td>"_s));
        QVERIFY(html.contains(u"cytat"_s) && html.contains(u"<hr/>"_s));
        QVERIFY(html.contains(u"Koniec w\u00a0domu"_s) || html.contains(u"Koniec w&nbsp;domu"_s));
        QCOMPARE(PageController().readingHtml(), u"<html><body></body></html>"_s);
    }
};

QTEST_MAIN(TestSelection)
#include "tst_editor_selection.moc"
