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
/// Rows: 0 Zażółć, 1 gęślą (its child), 2 jaźń, 3 łódź, 4 żółw (its child), 5 źdźbło.
const QString Picks = u"- Zażółć\n\t- gęślą\n- jaźń\n- łódź\n\t- żółw\n- źdźbło\n"_s;

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

    // The toasts' "Cofnij" after deleting or cutting blocks (BlockEditor.undoAction): undoStep()
    // undoes exactly the edit its token names, only while that is the last step of the history.
    void toastUndoNamesItsEdit()
    {
        const QString content = u"- Zażółć\r\n\t- gęślą\r\n- jaźń\r\n- łódź\r\n"_s;
        Fixture f(content);
        QVERIFY(!f.page.undoStep(f.page.undoToken())); // nothing to undo yet
        QVERIFY(!f.page.undoStep(QString()));
        f.page.selectRange(0, 2);
        const QString before = f.page.undoToken();
        f.page.deleteSelection();
        const QString token = f.page.undoToken();
        QVERIFY(token != before);
        QCOMPARE(f.text(), u"- łódź\r\n"_s);
        QVERIFY(f.page.undoStep(token));
        QCOMPARE(f.text(), content); // byte for byte, CRLF too
        QCOMPARE(f.selected(), QList<int>({0, 1, 2}));
        QVERIFY(!f.page.undoStep(token)); // undone already
        QCOMPARE(f.text(), content);
        // Redone: the same edit is the last step again.
        f.page.redo();
        QVERIFY(f.page.undoStep(token));
        QCOMPARE(f.text(), content);

        // Undone with Ctrl+Z, then another edit in its place: the toast's Cofnij undoes nothing.
        f.page.selectRange(0, 2);
        f.page.deleteSelection();
        const QString replaced = f.page.undoToken();
        f.page.undo();
        QCOMPARE(f.text(), content);
        f.page.setEditText(3, u"łódka"_s);
        const QString retyped = f.text();
        QVERIFY(retyped != content);
        QVERIFY(f.page.undoToken() != replaced);
        QVERIFY(!f.page.undoStep(replaced));
        QCOMPARE(f.text(), retyped);
        f.page.undo();
        QCOMPARE(f.text(), content);

        // Another edit after it: the toast's Cofnij undoes nothing (not even that edit).
        f.page.selectRange(0, 2);
        f.page.deleteSelection();
        const QString stale = f.page.undoToken();
        f.page.setEditText(0, u"łódka"_s);
        const QString edited = f.text();
        QVERIFY(edited != u"- łódź\r\n"_s);
        QVERIFY(f.page.undoToken() != stale);
        QVERIFY(!f.page.undoStep(stale));
        QCOMPARE(f.text(), edited);

        // Cut: the same.
        Fixture g(content);
        g.page.selectRange(2, 3);
        QVERIFY(g.page.cutSelection().value(u"ok"_s).toBool());
        const QString cut = g.page.undoToken();
        QCOMPARE(g.text(), u"- Zażółć\r\n\t- gęślą\r\n"_s);
        // Source mode: no block edits.
        g.page.setSourceMode(true);
        QVERIFY(!g.page.undoStep(cut));
        g.page.setSourceMode(false);
        QVERIFY(g.page.undoStep(cut));
        QCOMPARE(g.text(), content);

        // A reload resets the history: an old token never undoes a new edit at the same position.
        Fixture h(content);
        h.page.selectRange(0, 0);
        h.page.deleteSelection();
        const QString old = h.page.undoToken();
        QVERIFY(h.page.save());
        h.host.externalWrite(Page, u"- inna treść\n- druga\n"_s);
        QTRY_COMPARE(h.text(), u"- inna treść\n- druga\n"_s);
        h.page.selectRange(0, 0);
        h.page.deleteSelection();
        QVERIFY(!h.page.undoStep(old));
        QCOMPARE(h.text(), u"- druga\n"_s);
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
        // The selection tools offer indenting only where it can happen.
        QVERIFY(!f.page.canIndentSelection());
        f.page.selectRange(4, 5);
        QVERIFY(f.page.canIndentSelection());
        f.page.selectRange(2, 3); // a paragraph and a list item
        QVERIFY(!f.page.canIndentSelection());
        f.page.clearSelection();
        QVERIFY(!f.page.canIndentSelection());
        Fixture outline(Outline);
        outline.page.selectRange(0, 3);
        QVERIFY(outline.page.canIndentSelection());
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

    void taskStatusesTakeTheRowsBetweenTheEnds()
    {
        // A parent selected alone (Esc) brings its children along, but Ctrl+Enter changes only it:
        // their statuses and completion dates stay.
        const QString content =
                u"- [ ] Rodzic\n\t- [x] dziecko ✅ 2026-10-01\n\t- [ ] drugie\n- [ ] inny\n\t- [ ] jego\n"_s;
        Fixture f(content);
        f.page.setSelectedRow(0);
        QCOMPARE(f.page.selectionCount(), 3);
        f.page.cycleSelectionTasks();
        QCOMPARE(f.text(),
                 u"- [/] Rodzic\n\t- [x] dziecko ✅ 2026-10-01\n\t- [ ] drugie\n- [ ] inny\n\t- [ ] jego\n"_s);
        f.page.setSelectionTaskStatus(u"todo"_s);
        QCOMPARE(f.text(), content);
        // A range: the rows from one end to the other (children between them too), not the
        // children the last block brings along; in either direction.
        f.page.selectRange(3, 2);
        QCOMPARE(f.selected(), QList<int>({2, 3, 4}));
        f.page.cycleSelectionTasks();
        QCOMPARE(f.text(),
                 u"- [ ] Rodzic\n\t- [x] dziecko ✅ 2026-10-01\n\t- [/] drugie\n- [/] inny\n\t- [ ] jego\n"_s);
        f.page.undo();
        f.page.selectAll();
        f.page.setSelectionTaskStatus(u"doing"_s);
        QCOMPARE(f.text().count(u"[/]"_s), 5);
    }

    // Ctrl+click / the touch tools' "Pojedynczo": single blocks picked into or out of the
    // selection, gaps allowed (bullets 1, 3 and 5).
    void blocksPickedOneByOne()
    {
        Fixture f(Picks);
        QCOMPARE(f.page.toggleSelected(0), u"added"_s); // Zażółć with gęślą
        QCOMPARE(f.selected(), QList<int>({0, 1}));
        QVERIFY(!f.page.selectionPicked()); // one block: a range of one
        QCOMPARE(f.page.toggleSelected(2), u"added"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2}));
        QVERIFY(f.page.selectionPicked());
        QVERIFY(f.page.selectionContiguous()); // no gap between them
        QCOMPARE(f.page.toggleSelected(5), u"added"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2, 5}));
        QVERIFY(!f.page.selectionContiguous());
        QCOMPARE(f.page.selectionCount(), 4);
        QCOMPARE(f.page.selectionFirstRow(), 0);
        QCOMPARE(f.page.selectionLastRow(), 5);
        QCOMPARE(f.page.selectedRow(), 5); // both ends on the block picked last
        QCOMPARE(f.page.selectionAnchorRow(), 5);
        QVERIFY(f.page.multiSelection());
        // A band for each run.
        QCOMPARE(f.edges(), QStringList({u"first"_s, u"middle"_s, u"last"_s, u""_s, u""_s, u"single"_s}));
        // A child of a picked block goes with it.
        QCOMPARE(f.page.toggleSelected(1), u"inParent"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 2, 5}));
        // Copied in document order, each with its subtree.
        QVERIFY(f.page.copySelection().value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"- Zażółć\n\t- gęślą\n- jaźń\n- źdźbło"_s);
        QCOMPARE(f.page.copySelection().value(u"count"_s).toInt(), 4);
        QVERIFY(f.page.copySelection(true).value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"• Zażółć\n  • gęślą\n• jaźń\n• źdźbło"_s);
        // Out again: the ends go to the block picked last before it.
        QCOMPARE(f.page.toggleSelected(2), u"removed"_s);
        QCOMPARE(f.selected(), QList<int>({0, 1, 5}));
        QCOMPARE(f.page.selectedRow(), 5);
        // Gaps: neither indented nor moved, nothing changes.
        QVERIFY(!f.page.indentSelection());
        QVERIFY(!f.page.outdentSelection());
        QVERIFY(!f.page.moveSelectionUp());
        QVERIFY(!f.page.moveSelectionDown());
        QCOMPARE(f.text(), Picks);
        QVERIFY(!f.page.canUndo());
        // A range (Shift+click) replaces the picked blocks, from the one picked last.
        f.page.extendSelectionTo(3);
        QCOMPARE(f.selected(), QList<int>({3, 4, 5}));
        QVERIFY(!f.page.selectionPicked());
        QCOMPARE(f.page.selectionAnchorRow(), 5);
        // A block of a range picked out: the range's other blocks stay.
        QCOMPARE(f.page.toggleSelected(5), u"removed"_s);
        QCOMPARE(f.selected(), QList<int>({3, 4}));
        QCOMPARE(f.page.toggleSelected(3), u"removed"_s);
        QCOMPARE(f.page.selectionCount(), 0);
        QCOMPARE(f.page.selectedRow(), -1);
        // Esc / one block selected again, Shift+↓: ranges as before.
        f.page.toggleSelected(0);
        f.page.toggleSelected(5);
        f.page.setSelectedRow(2);
        QCOMPARE(f.selected(), QList<int>({2}));
        QVERIFY(!f.page.selectionPicked());
        QCOMPARE(f.page.toggleSelected(-1), QString());
    }

    void pickedBlocksAreCopiedCutDeletedAndPastedOver()
    {
        Fixture f(Picks);
        f.page.setSelectedRow(0);
        f.page.toggleSelected(3); // Zażółć and łódź with their children
        QCOMPARE(f.selected(), QList<int>({0, 1, 3, 4}));
        // Delete: one undo step, which brings them back picked.
        QCOMPARE(f.page.deleteSelection(), 0);
        QCOMPARE(f.text(), u"- jaźń\n- źdźbło\n"_s);
        QCOMPARE(f.page.selectedRow(), 0);
        QVERIFY(!f.page.selectionPicked());
        f.page.undo();
        QCOMPARE(f.text(), Picks);
        QCOMPARE(f.selected(), QList<int>({0, 1, 3, 4}));
        QVERIFY(f.page.selectionPicked());
        QVERIFY(!f.page.selectionContiguous());
        f.page.redo();
        QCOMPARE(f.text(), u"- jaźń\n- źdźbło\n"_s);
        f.page.undo();
        // Cut.
        QVariantMap r = f.page.cutSelection();
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"count"_s).toInt(), 4);
        QCOMPARE(clipboardText(), u"- Zażółć\n\t- gęślą\n- łódź\n\t- żółw"_s);
        QCOMPARE(f.text(), u"- jaźń\n- źdźbło\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), Picks);
        // A paste over them goes where the first one was.
        r = f.page.replaceSelection(u"- nowy"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 0);
        QCOMPARE(f.text(), u"- nowy\n- jaźń\n- źdźbło\n"_s);
        QCOMPARE(f.selected(), QList<int>({0}));
        f.page.undo();
        QCOMPARE(f.text(), Picks);
        // Task statuses: the picked blocks, not the children they bring along.
        f.page.cycleSelectionTasks();
        QCOMPARE(f.text(), u"- [ ] Zażółć\n\t- gęślą\n- jaźń\n- [ ] łódź\n\t- żółw\n- źdźbło\n"_s);
        f.page.undo();
        QCOMPARE(f.text(), Picks);
    }

    void pickedBlocksWithoutGapsMove()
    {
        Fixture f(Outline);
        f.page.toggleSelected(5); // c
        f.page.toggleSelected(6); // d
        QVERIFY(f.page.selectionPicked());
        QVERIFY(f.page.selectionContiguous());
        QVERIFY(f.page.indentSelection());
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- a2\n- b\n\t- b1\n\t- c\n\t- d\n"_s);
        QCOMPARE(f.selected(), QList<int>({5, 6}));
        f.page.undo();
        QCOMPARE(f.text(), Outline);
        QVERIFY(f.page.moveSelectionUp());
        QCOMPARE(f.text(), u"- a\n\t- a1\n\t- a2\n- c\n- d\n- b\n\t- b1\n"_s);
        QCOMPARE(f.selected(), QList<int>({3, 4}));
        // Picking a parent takes in its picked children.
        Fixture g(Outline);
        g.page.toggleSelected(1); // a1
        g.page.toggleSelected(4); // b1
        QVERIFY(!g.page.selectionContiguous());
        QCOMPARE(g.page.toggleSelected(0), u"added"_s); // a, with a1
        QCOMPARE(g.selected(), QList<int>({0, 1, 2, 4}));
        QCOMPARE(g.page.toggleSelected(0), u"removed"_s); // a1 is not picked on its own any more
        QCOMPARE(g.selected(), QList<int>({4}));
    }

    void theEditedBlockJoinsPickedBlocks()
    {
        Fixture f(Picks);
        f.page.startEditing(2, 1);
        QCOMPARE(f.page.toggleSelected(5), u"added"_s);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.selected(), QList<int>({2, 5}));
        QVERIFY(f.page.selectionPicked());
        // A picked block deleted on another device: the other one stays, alone.
        f.host.externalWrite(Page, u"- Zażółć\n\t- gęślą\n- jaźń\n- łódź\n\t- żółw\n"_s);
        QCOMPARE(f.selected(), QList<int>({2}));
        QVERIFY(!f.page.selectionPicked());
        // A child of the block being edited: that block is selected (with the child).
        f.page.startEditing(0, 0);
        QCOMPARE(f.page.toggleSelected(1), u"inParent"_s);
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.selected(), QList<int>({0, 1}));
        // Editing ends the picked selection.
        f.page.toggleSelected(3);
        QVERIFY(f.page.selectionPicked());
        f.page.startEditing(2, 0);
        QCOMPARE(f.page.selectionCount(), 0);
        QVERIFY(!f.page.selectionPicked());
        // The page's properties are never picked.
        Fixture props(u"---\ntytuł: Lista\n---\n- mleko\n- chleb\n"_s);
        QCOMPARE(props.page.toggleSelected(0), QString());
        QCOMPARE(props.page.selectionCount(), 0);
    }

    void pasteOverASelectionNeedsSomethingToPaste()
    {
        // A pasted note's YAML frontmatter alone: nothing to paste, nothing deleted.
        Fixture f(u"- a\n- b\n- c\n"_s);
        f.page.selectRange(1, 1);
        QVariantMap r = f.page.replaceSelection(u"---\ntytuł: x\n---"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), -1);
        QCOMPARE(f.text(), u"- a\n- b\n- c\n"_s);
        QVERIFY(!f.page.canUndo());
        QCOMPARE(f.selected(), QList<int>({1}));
        // With blocks after it: only the frontmatter is left out.
        r = f.page.replaceSelection(u"---\ntytuł: x\n---\n- nowy\n"_s);
        QCOMPARE(f.text(), u"- a\n- nowy\n- c\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        // Logseq page properties are text: a block of their own (as pasted into a block).
        f.page.selectRange(1, 1);
        r = f.page.replaceSelection(u"status:: done"_s);
        QVERIFY2(r.value(u"row"_s).toInt() == 1 && f.text().contains(u"status:: done"_s)
                         && !f.text().contains(u"nowy"_s),
                 qPrintable(f.text()));
    }

    void thePagePropertiesAreNeverReplaced()
    {
        // Selected alone (↑ from the first block), they stay: the paste goes in below them.
        Fixture f(u"---\ntytuł: Zakupy\n---\n- mleko\n"_s);
        f.page.setSelectedRow(0);
        QCOMPARE(f.selected(), QList<int>({0}));
        const QVariantMap r = f.page.replaceSelection(u"- chleb\n- masło"_s);
        QCOMPARE(f.text(), u"---\ntytuł: Zakupy\n---\n- chleb\n- masło\n- mleko\n"_s);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(f.selected(), QList<int>({1, 2}));
        f.page.undo();
        QCOMPARE(f.text(), u"---\ntytuł: Zakupy\n---\n- mleko\n"_s);
        // Logseq page properties too.
        Fixture logseq(u"typ:: lista\n\n- mleko\n"_s);
        logseq.page.setSelectedRow(0);
        logseq.page.replaceSelection(u"- chleb"_s);
        QVERIFY2(logseq.text().startsWith(u"typ:: lista\n"_s) && logseq.text().indexOf(u"- chleb\n"_s) > 0
                         && logseq.text().indexOf(u"- chleb\n"_s) < logseq.text().indexOf(u"- mleko\n"_s),
                 qPrintable(logseq.text()));
    }

    void noEmptySelectionInBetween()
    {
        // Deleting, cutting or pasting over a selection goes from it to the next one at once: an
        // empty one in between would end the touch selection mode.
        Fixture f(Outline);
        QList<int> counts;
        connect(&f.page, &PageController::selectionChanged, &f.page, [&] { counts.append(f.page.selectionCount()); });
        f.page.selectRange(3, 5);
        counts.clear();
        f.page.deleteSelection();
        QCOMPARE(counts, QList<int>({1}));
        f.page.selectRange(1, 2);
        counts.clear();
        QVERIFY(f.page.cutSelection().value(u"ok"_s).toBool());
        QCOMPARE(counts, QList<int>({1})); // a: the block before the cut
        f.page.selectRange(1, 1);
        counts.clear();
        f.page.replaceSelection(u"- x\n- y"_s);
        QCOMPARE(counts, QList<int>({2}));
    }

    void undoBringingASelectionBackStopsEditing()
    {
        Fixture f(Outline);
        f.page.selectRange(5, 6);
        QVERIFY(f.page.indentSelection());
        f.page.startEditing(0, 1); // another block edited meanwhile
        QCOMPARE(f.page.selectionCount(), 0);
        QSignalSpy editing(&f.page, &PageController::editingRowChanged);
        f.page.undo();
        QCOMPARE(f.text(), Outline);
        QCOMPARE(f.page.editingRow(), -1);
        QVERIFY(editing.count() >= 1);
        QCOMPARE(f.selected(), QList<int>({5, 6}));
        f.page.startEditing(0, 1);
        f.page.redo();
        QCOMPARE(f.page.editingRow(), -1);
        QCOMPARE(f.selected(), QList<int>({5, 6}));
        // Typing is undone into its block, as before.
        f.page.startEditing(0, -1);
        f.page.setEditText(0, u"a!"_s, 2);
        f.page.undo();
        QCOMPARE(f.page.editingRow(), 0);
        QCOMPARE(f.page.selectionCount(), 0);
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

    void anInBlockCutMovesItsId()
    {
        // Ctrl+X of a block's text with its id, Ctrl+V right away: the id goes with it (the saved
        // page and the index still have it).
        Fixture f(u"- Zadanie ^abc\n- \n"_s, 100000);
        QVERIFY(f.host.blockIdTaken(u"abc"_s));
        f.page.startEditing(0, 0);
        const int length = int(f.role(0, "editText").toString().size());
        QVERIFY(f.page.copyEditText(0, 0, length, true).value(u"ok"_s).toBool());
        f.page.setEditText(0, QString(), 0); // the editor removes the cut text
        f.page.startEditing(1, 0);
        f.page.pasteClipboard(1, 0, 0);
        QCOMPARE(f.text(), u"- \n- Zadanie ^abc\n"_s);
        // A copy (no cut) of it is a copy: the id is not duplicated.
        QVERIFY(f.page.copyEditText(1, 0, 12).value(u"ok"_s).toBool());
        f.page.startEditing(0, 0);
        f.page.pasteClipboard(0, 0, 0);
        QCOMPARE(f.text(), u"- Zadanie\n- Zadanie ^abc\n"_s);
    }

    void anUndoneCutIsACopy()
    {
        FakeEditorHost host;
        host.setFile(Page, u"- jeden ^id1\n- dwa\n"_s);
        host.setFile(u"Nowa.md"_s, u"- \n- \n- \n"_s);
        auto a = std::make_unique<PageController>();
        a->setSaveDelay(100000);
        a->setHost(&host);
        a->setPath(Page);
        PageController b;
        b.setSaveDelay(100000);
        b.setHost(&host);
        b.setPath(u"Nowa.md"_s);
        a->selectRange(0, 0);
        QVERIFY(a->cutSelection().value(u"ok"_s).toBool());
        a->undo(); // ^id1 is on page A again
        b.startEditing(0, 0);
        b.pasteClipboard(0, 0, 0);
        QCOMPARE(b.text(), u"- jeden\n- \n- \n"_s);
        // Redone, the cut is a move again.
        a->redo();
        b.startEditing(1, 0);
        b.pasteClipboard(1, 0, 0);
        QCOMPARE(b.text(), u"- jeden\n- jeden ^id1\n- \n"_s);
        // The cut page closed (saved) after the cut was undone: its file has the id again.
        host.setFile(u"Trzeci.md"_s, u"- trzy ^id3\n"_s);
        auto c = std::make_unique<PageController>();
        c->setSaveDelay(100000);
        c->setHost(&host);
        c->setPath(u"Trzeci.md"_s);
        c->selectRange(0, 0);
        QVERIFY(c->cutSelection().value(u"ok"_s).toBool());
        c->undo();
        c->save();
        c.reset();
        QVERIFY(host.file(u"Trzeci.md"_s).contains(u"^id3"_s));
        b.startEditing(2, 0);
        b.pasteClipboard(2, 0, 0);
        QCOMPARE(b.text(), u"- jeden\n- jeden ^id1\n- trzy\n"_s);
    }

    void pastingOneListItem_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("pasted");
        QTest::addColumn<QString>("expected");
        // A copied block is its line of Markdown: its marker is not pasted again ("- - jeden").
        QTest::newRow("a bullet into an empty one") << u"- \n"_s << 0 << 0 << u"- jeden"_s << u"- jeden\n"_s;
        QTest::newRow("a task into an empty bullet")
                << u"- \n"_s << 0 << 0 << u"- [ ] Skuć stare płytki 📅 2026-10-13"_s
                << u"- [ ] Skuć stare płytki 📅 2026-10-13\n"_s;
        QTest::newRow("a done task replaces an empty task")
                << u"- [ ] \n"_s << 0 << 0 << u"- [x] mleko ✅ 2026-10-15"_s << u"- [x] mleko ✅ 2026-10-15\n"_s;
        QTest::newRow("a bullet into an empty task keeps its checkbox")
                << u"- [ ] \n"_s << 0 << 0 << u"- mleko"_s << u"- [ ] mleko\n"_s;
        // A numbered line from elsewhere may be text ("2025. rok"): as it is (a copy of this app's: below).
        QTest::newRow("a numbered line") << u"- \n"_s << 0 << 0 << u"2025. rok"_s << u"- 2025. rok\n"_s;
        QTest::newRow("CRLF and a line break at the end") << u"- \n"_s << 0 << 0 << u"- żółć\r\n"_s << u"- żółć\n"_s;
        QTest::newRow("into the middle of text") << u"- ab\n"_s << 1 << 1 << u"- [ ] Skuć"_s << u"- aSkućb\n"_s;
        QTest::newRow("over selected text") << u"- abc\n"_s << 1 << 2 << u"* dwa"_s << u"- adwac\n"_s;
        QTest::newRow("into a nested item") << u"- x\n\t- \n"_s << 0 << 0 << u"- a1"_s << u"- x\n\t- a1\n"_s;
        QTest::newRow("a continuation line") << u"- \n"_s << 0 << 0 << u"- jeden\n  dalej"_s << u"- jeden\n  dalej\n"_s;
        QTest::newRow("a paragraph becomes the item")
                << u"Akapit\n"_s << 0 << 6 << u"- [ ] zadanie"_s << u"- [ ] zadanie\n"_s;
    }
    void pastingOneListItem()
    {
        QFETCH(QString, content);
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, pasted);
        QFETCH(QString, expected);
        Fixture f(content);
        const int row = f.page.blocks()->rowCount() - 1;
        f.page.startEditing(row, from);
        f.page.pasteText(row, from, to, pasted);
        QCOMPARE(f.text(), expected);
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void pastingOneCopiedNumberedItem()
    {
        Fixture f(u"1. pierwszy\n2. drugi\n\n- \n"_s);
        f.page.selectRange(1, 1);
        QVERIFY(f.page.copySelection().value(u"ok"_s).toBool());
        QCOMPARE(clipboardText(), u"2. drugi"_s);
        f.page.startEditing(2, 0);
        f.page.pasteClipboard(2, 0, 0);
        QCOMPARE(f.text(), u"1. pierwszy\n2. drugi\n\n- drugi\n"_s);
    }

    void pastingOneCodeItemIntoAnEmptyBullet()
    {
        Fixture f(u"- \n"_s);
        f.page.startEditing(0, 0);
        f.page.pasteText(0, 0, 0, u"- ```js\n  let a = 1;\n  ```"_s);
        QCOMPARE(f.role(0, "view").toString(), u"code"_s);
        QVERIFY2(!f.text().contains(u"- -"_s) && f.text().contains(u"let a = 1;"_s), qPrintable(f.text()));
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

    void ownCopiesAreTheirMarkdown_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("html");
        // This app's HTML (io::clipboardHtml marks it): also after a restart.
        QTest::newRow("marked as this app's")
                << u"Zwykły akapit po restarcie"_s
                << u"<meta charset=\"utf-8\"><meta name=\"generator\" content=\"Brulion\"><p>Inny tekst</p>"_s;
        // What only Markdown says (an HTML rendering loses it): the text is pasted.
        QTest::newRow("a block id") << u"Akapit ^abc123"_s << u"<p>Akapit</p>"_s;
        QTest::newRow("a property") << u"Akapit\ntyp:: plan"_s << u"<p>Akapit typ plan</p>"_s;
        QTest::newRow("a due date") << u"Zadanie 📅 2026-10-20"_s << u"<p>Zadanie</p>"_s;
        QTest::newRow("a reminder") << u"Lekarz ⏰ 2026-10-20 09:00"_s << u"<p>Lekarz</p>"_s;
        QTest::newRow("a formula and a footnote")
                << u"Wzór $$x^2$$ i przypis[^1]"_s << u"<p>Wzór <i>x</i>2 i przypis1</p>"_s;
    }
    void ownCopiesAreTheirMarkdown()
    {
        QFETCH(QString, text);
        QFETCH(QString, html);
        Fixture f(u"- \n"_s);
        auto *mime = new QMimeData;
        mime->setText(text);
        mime->setHtml(html);
        QGuiApplication::clipboard()->setMimeData(mime);
        QCOMPARE(f.page.clipboardText(), text);
    }

    void theLastCopyIsRecognisedWithOtherLineBreaks()
    {
        Fixture f(u"Pierwszy akapit\n\nDrugi akapit\n"_s);
        f.page.selectAll();
        QVERIFY(f.page.copySelection().value(u"ok"_s).toBool());
        auto *mime = new QMimeData;
        mime->setText(u"Pierwszy akapit\r\n\r\nDrugi akapit\r\n"_s); // through a Windows clipboard
        mime->setHtml(u"<p>Pierwszy</p>"_s);
        QGuiApplication::clipboard()->setMimeData(mime);
        QCOMPARE(f.page.clipboardText(), u"Pierwszy akapit\r\n\r\nDrugi akapit\r\n"_s);
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

    void readingHtmlHasTheNotesOwnSpaces()
    {
        // The editor joins one-letter words to the next one with no-break spaces (Polish
        // typography); text selected and copied in "Zaznacz tekst" has the note's own spaces.
        Fixture f(u"- Jadę w góry z Anią i\u00a0psem\n\n| a | b |\n|---|---|\n| w domu | 1 |\n"_s);
        const QString html = f.page.readingHtml();
        QVERIFY2(html.contains(u"Jadę w góry z Anią i\u00a0psem"_s)
                         || html.contains(u"Jadę w góry z Anią i&nbsp;psem"_s),
                 qPrintable(html));
        QVERIFY2(html.contains(u">w domu<"_s), qPrintable(html));
        // The editor itself still joins them.
        QVERIFY(f.role(0, "html").toString().contains(u"w\u00a0góry"_s));
    }

    void readingHtmlHasResults()
    {
        Fixture f(u"```calc\nbilety = 3 * 120\nsum\n```\n\n```query\ntype:tasks tag:zakupy\n```\n"_s);
        f.host.setFile(u"Zakupy.md"_s, u"- [ ] Mleko #zakupy\n- [x] Chleb #zakupy\n"_s);
        const QString html = f.page.readingHtml();
        QVERIFY2(html.contains(u"bilety = 3 * 120 = 360"_s), qPrintable(html));
        QVERIFY2(html.contains(u"☐ Mleko"_s) && !html.contains(u"type:tasks"_s), qPrintable(html));
        // A query without results (or a host without queries) shows the query.
        f.host.setQueriesFail(true);
        QVERIFY(f.page.readingHtml().contains(u"type:tasks tag:zakupy"_s));
    }
};

QTEST_MAIN(TestSelection)
#include "tst_editor_selection.moc"
