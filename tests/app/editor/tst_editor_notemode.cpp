// Note modes in the block editor (docs/UI.md "Note modes"): PageController.mode resolved at load
// (frontmatter key, journal / page defaults, content), kept while the page is open, setMode()
// writing only the `brulion:` key (one undo step, saved), the document mode's commands (Enter
// and Backspace leave lists, Tab only in lists, paragraphs as new blocks, no zoom / folding) and
// the model's roles for it (listMarker, prevKind, noteModeOnly, visibleCount).
#include "blocklistmodel.h"
#include "blocktext.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"

#include "markdown/notemode.h"

#include <QAbstractItemModelTester>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;
const QString Now = u"2026-10-15 10:00"_s;
const QString DocKey = u"---\nbrulion: document\n---\n"_s;

/// A controller on `content` in a vault whose new pages are `pageMode` (model tester attached).
struct Fixture {
    FakeEditorHost host;
    PageController page;
    std::unique_ptr<QAbstractItemModelTester> tester;

    explicit Fixture(const QString &content, const QString &pageMode = u"outline"_s, const QString &path = Page)
    {
        host.setFile(path, content);
        host.setPageMode(pageMode);
        page.setNow(Now);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(path);
        tester = std::make_unique<QAbstractItemModelTester>(page.blocks(),
                                                            QAbstractItemModelTester::FailureReportingMode::QtTest);
    }

    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        const QHash<int, QByteArray> roles = model()->roleNames();
        return model()->data(model()->index(row), roles.key(name));
    }
    QString edit(int row) const { return role(row, "editText").toString(); }
    QString kind(int row) const { return role(row, "kind").toString(); }
    QString text() const { return page.text(); }
    int count() const { return model()->rowCount(); }
    QList<bool> visible() const
    {
        QList<bool> out;
        for (int r = 0; r < count(); ++r)
            out.append(role(r, "visible").toBool());
        return out;
    }
};

} // namespace

class TestNoteMode : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // --- resolution ----------------------------------------------------------------------------------

    void resolution_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("pageMode");
        QTest::addColumn<QString>("path");
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("source");
        QTest::newRow("key wins over bullets") << u"---\nbrulion: document\n---\n- a\n- b\n- c\n"_s << u"outline"_s
                                               << Page << u"document"_s << u"frontmatter"_s;
        QTest::newRow("quoted, upper case key value")
                << u"---\ntags: [x]\nbrulion: \"OUTLINE\"\n---\nAkapit\n\nDrugi\n"_s << u"document"_s << Page
                << u"outline"_s << u"frontmatter"_s;
        QTest::newRow("logseq page") << u"- Zażółć\n- gęślą\n\t- jaźń\n"_s << u"document"_s << Page << u"outline"_s
                                     << u"heuristic"_s;
        QTest::newRow("obsidian note") << u"# Notatka\n\nAkapit pierwszy.\n\nAkapit drugi.\n"_s << u"outline"_s << Page
                                       << u"document"_s << u"heuristic"_s;
        QTest::newRow("empty page, document default")
                << QString() << u"document"_s << Page << u"document"_s << u"default"_s;
        QTest::newRow("one block, outline default")
                << u"Tylko jedno zdanie.\n"_s << u"outline"_s << Page << u"outline"_s << u"default"_s;
        QTest::newRow("invalid key value is ignored") << u"---\nbrulion: kanban\n---\n# A\n\nB\n"_s << u"outline"_s
                                                      << Page << u"document"_s << u"heuristic"_s;
        QTest::newRow("journal") << u"Akapit dziennika.\n\nI drugi.\n"_s << u"document"_s << u"journals/2026-10-15.md"_s
                                 << u"outline"_s << u"journal"_s;
        QTest::newRow("journal with a key") << u"---\nbrulion: document\n---\n- a\n"_s << u"outline"_s
                                            << u"journals/2026-10-15.md"_s << u"document"_s << u"frontmatter"_s;
    }

    void resolution()
    {
        QFETCH(QString, content);
        QFETCH(QString, pageMode);
        QFETCH(QString, path);
        QFETCH(QString, mode);
        QFETCH(QString, source);
        Fixture f(content, pageMode, path);
        QCOMPARE(f.page.mode(), mode);
        QCOMPARE(f.page.modeSource(), source);
        QCOMPARE(f.page.documentMode(), mode == u"document");
        QCOMPARE(f.text(), content); // resolving never touches the text
        QVERIFY(!f.page.dirty());
    }

    void modeStaysWhileTheContentChanges()
    {
        // A new page in the document default: writing a list does not turn it into an outline
        // while it is open (the layout never flips under the caret); reopened, the content decides.
        Fixture f(QString(), u"document"_s);
        QSignalSpy modes(&f.page, &PageController::modeChanged);
        QCOMPARE(f.page.mode(), u"document"_s);
        const int row = f.page.appendEmptyBlock();
        f.page.setEditText(row, u"- pierwszy"_s, 10);
        f.page.split(row, 9);
        f.page.setEditText(row + 1, u"drugi"_s, 5);
        f.page.split(row + 1, 5);
        f.page.setEditText(row + 2, u"trzeci"_s, 6);
        QCOMPARE(f.text(), u"- pierwszy\n- drugi\n- trzeci\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(modes.size(), 0);
        f.page.load();
        QCOMPARE(f.page.mode(), u"outline"_s);
        QCOMPARE(f.page.modeSource(), u"heuristic"_s);
    }

    void configChangeIsFollowedOnRefresh()
    {
        Fixture f(u"Jedno zdanie.\n"_s, u"outline"_s);
        QCOMPARE(f.page.mode(), u"outline"_s);
        f.host.setPageMode(u"document"_s);
        QCOMPARE(f.page.mode(), u"outline"_s); // until refreshed (the shell does it on configChanged)
        f.page.refreshMode();
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(f.page.modeSource(), u"default"_s);
        // An explicit key is not affected by the vault's defaults.
        Fixture explicitMode(DocKey + u"Jedno zdanie.\n"_s, u"document"_s);
        explicitMode.host.setPageMode(u"outline"_s);
        explicitMode.page.refreshMode();
        QCOMPARE(explicitMode.page.mode(), u"document"_s);
    }

    void configChangesNeverLookAtTheContentAgain()
    {
        // Workspace.configChanged comes for any setting (week start, journal folder…, also from
        // another device): only a mode that came from the notebook's defaults follows them, the
        // content written since opening never flips the page.
        Fixture f(u"Akapit pierwszy.\n\nAkapit drugi.\n"_s, u"outline"_s);
        QCOMPARE(f.page.modeSource(), u"heuristic"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        f.page.insertMarkdown(1, u"- a\n- b\n- c\n- d\n- e\n- f\n"_s);
        QCOMPARE(markdown::heuristicNoteMode(f.text()), std::optional<NoteMode>(NoteMode::Outline));
        f.page.refreshMode();
        QCOMPARE(f.page.mode(), u"document"_s);
        f.host.setPageMode(u"document"_s);
        f.page.refreshMode();
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(f.page.modeSource(), u"heuristic"_s);

        // A page the content did not decide: the default, also after a list was written into it.
        Fixture g(QString(), u"document"_s);
        const int row = g.page.appendEmptyBlock();
        g.page.insertMarkdown(row, u"- jeden\n- dwa\n- trzy\n"_s);
        QCOMPARE(markdown::heuristicNoteMode(g.text()), std::optional<NoteMode>(NoteMode::Outline));
        g.page.refreshMode(); // e.g. weekStartsOn changed: nothing to follow
        QCOMPARE(g.page.mode(), u"document"_s);
        QCOMPARE(g.page.modeSource(), u"default"_s);
        g.host.setPageMode(u"outline"_s);
        g.page.refreshMode();
        QCOMPARE(g.page.mode(), u"outline"_s);
        g.host.setPageMode(u"document"_s);
        g.page.refreshMode();
        QCOMPARE(g.page.mode(), u"document"_s);
        QCOMPARE(g.page.modeSource(), u"default"_s);
    }

    void undoOfASwitchRestoresTheModeOfTheSession()
    {
        // Opened as a document (the default), a list written into it, switched to an outline:
        // undo gives back the document the page was — not what the content would say now.
        Fixture f(u"Wstęp.\n"_s, u"document"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        f.page.insertMarkdown(0, u"- jabłka\n- gruszki\n- śliwki\n- wiśnie\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        const QString written = f.text();
        QVERIFY(f.page.setMode(u"outline"_s));
        QCOMPARE(f.page.mode(), u"outline"_s);
        f.page.undo();
        QCOMPARE(f.text(), written);
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(f.page.modeSource(), u"default"_s);
        f.page.redo();
        QCOMPARE(f.page.mode(), u"outline"_s);
        // Another device removing the key again: the same.
        f.host.externalWrite(Page, written);
        QCOMPARE(f.text(), written);
        QCOMPARE(f.page.mode(), u"document"_s);
        // Reopened, the content decides.
        f.page.load();
        QCOMPARE(f.page.mode(), u"outline"_s);
        QCOMPARE(f.page.modeSource(), u"heuristic"_s);
    }

    // --- switching ---------------------------------------------------------------------------------------

    void setModeWritesOnlyTheKey_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("mode");
        QTest::newRow("no frontmatter") << u"- a\n- b\n"_s << u"document"_s;
        QTest::newRow("frontmatter, crlf")
                << u"---\r\ntytuł: Zażółć gęślą\r\ntags: [a, b]\r\n---\r\n# Jaźń\r\n\r\nTreść\r\n"_s << u"outline"_s;
        QTest::newRow("existing key") << u"---\nbrulion: outline\nautor: Łucja\n---\n- x\n"_s << u"document"_s;
        QTest::newRow("bom, no final newline") << u"\uFEFFAkapit bez końca"_s << u"outline"_s;
        QTest::newRow("empty page") << QString() << u"outline"_s;
    }

    void setModeWritesOnlyTheKey()
    {
        QFETCH(QString, content);
        QFETCH(QString, mode);
        Fixture f(content);
        const std::optional<NoteMode> wanted = markdown::parseNoteMode(mode);
        QVERIFY(f.page.setMode(mode));
        QCOMPARE(f.page.mode(), mode);
        QCOMPARE(f.page.modeSource(), u"frontmatter"_s);
        const QString expected = markdown::setNoteModeKey(content, wanted);
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.host.file(Page), expected); // saved at once
        QVERIFY(!f.page.dirty());
        // One undo step back to the original bytes (and the mode the page had).
        f.page.undo();
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.canUndo());
        QCOMPARE(markdown::frontmatterNoteMode(f.text()), markdown::frontmatterNoteMode(content));
        f.page.redo();
        QCOMPARE(f.text(), expected);
        QCOMPARE(f.page.mode(), mode);
        // Switching back keeps the key (an explicit choice) and nothing else changes.
        const QString other = mode == u"document" ? u"outline"_s : u"document"_s;
        QVERIFY(f.page.toggleMode());
        QCOMPARE(f.page.mode(), other);
        QCOMPARE(f.text(), markdown::setNoteModeKey(content, markdown::parseNoteMode(other)));
    }

    void setModeKeepsTheCaret()
    {
        Fixture f(u"- pierwszy\n- drugi ąę\n"_s);
        f.page.startEditing(1, 3);
        QVERIFY(f.page.setMode(u"document"_s));
        QCOMPARE(f.text(), u"---\nbrulion: document\n---\n- pierwszy\n- drugi ąę\n"_s);
        // The new (hidden) frontmatter row is above: the edited block moved down a row.
        QCOMPARE(f.page.editingRow(), 2);
        QVERIFY(f.role(2, "isEditing").toBool());
        QCOMPARE(f.page.cursorPosition(), 3);
        QCOMPARE(f.edit(2), u"drugi ąę"_s);
        f.page.undo();
        QCOMPARE(f.page.editingRow(), 1);
        QCOMPARE(f.page.mode(), u"outline"_s);
    }

    void setModeRefusesUnsafeFrontmatter()
    {
        // YAML the core cannot edit without touching other keys (it leaves such a note alone).
        QString content;
        for (const QString &candidate :
             {u"---\ntitle: \"Łódź\nbrulion: outline\n---\nTreść\n"_s, u"---\ntags: [a,\nbrulion: b]\n---\nTreść\n"_s,
              u"---\nto nie jest yaml\n---\nTreść\n"_s, u"---\n- a\n- b\n---\nTreść\n"_s,
              u"---\n\tbrulion: outline\nx:\t1\n---\n"_s, u"---\n  brulion: outline\n  title: Łódź\n---\nTreść\n"_s}) {
            if (content.isEmpty() && markdown::setNoteModeKey(candidate, NoteMode::Document) == candidate)
                content = candidate;
        }
        QVERIFY2(!content.isEmpty(), "the core edits every sample of unsafe frontmatter");
        Fixture f(content);
        QSignalSpy modes(&f.page, &PageController::modeChanged);
        QVERIFY(!f.page.setMode(u"document"_s));
        QCOMPARE(f.text(), content);
        QVERIFY(!f.page.dirty());
        QVERIFY(!f.page.canUndo());
        QCOMPARE(modes.size(), 0);
        QVERIFY(!f.page.setMode(u"kanban"_s));
    }

    void setModeWhileEditingTheFrontmatter()
    {
        // The properties block is being edited (raw YAML in the inline editor) when the header's
        // toggle is used: the editor must get the new text, or its next keystroke writes the old
        // key back (a silent switch back, and the user's choice lost).
        // (The edited block changed, so editing it ends: the inactive editor shows the model's
        // text again — InlineEditor.onModelTextChanged.)
        const QString content = u"---\nbrulion: outline\nautor: Łucja\n---\n- a\n- b\n"_s;
        Fixture f(content);
        f.page.startEditing(0, 3);
        QCOMPARE(f.page.editingRow(), 0);
        QVERIFY(f.page.setMode(u"document"_s));
        QCOMPARE(f.text(), u"---\nbrulion: document\nautor: Łucja\n---\n- a\n- b\n"_s);
        QCOMPARE(f.page.editingRow(), -1);
        f.page.startEditing(0, -1);
        QVERIFY(f.edit(0).contains(u"brulion: document"_s));
        f.page.setEditText(0, f.edit(0) + u"\nźródło: x"_s, -1);
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(markdown::frontmatterNoteMode(f.text()), NoteMode::Document);
        f.page.undo();
        f.page.undo();
        QCOMPARE(f.text(), content);
        QCOMPARE(f.page.mode(), u"outline"_s);
    }

    void setModeInSourceMode()
    {
        Fixture f(u"- a\n- b\n"_s);
        f.page.setSourceMode(true);
        QVERIFY(f.page.setMode(u"document"_s));
        QCOMPARE(f.page.sourceText(), DocKey + u"- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(f.host.file(Page), DocKey + u"- a\n- b\n"_s);
        f.page.setSourceMode(false);
        QCOMPARE(f.text(), DocKey + u"- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
    }

    void keyEditsAndExternalChangesSwitch()
    {
        Fixture f(u"- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"outline"_s);
        // Another device switched the page.
        f.host.externalWrite(Page, DocKey + u"- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        QCOMPARE(f.page.modeSource(), u"frontmatter"_s);
        // The key typed in source mode.
        f.page.setSourceMode(true);
        f.page.setSourceText(u"---\nbrulion: outline\n---\n- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"outline"_s);
        f.page.setSourceMode(false);
        // The key removed: the content decides again.
        f.page.setSourceText(u"- a\n- b\n"_s);
        QCOMPARE(f.page.mode(), u"outline"_s);
        QCOMPARE(f.page.modeSource(), u"heuristic"_s);
    }

    // --- the model's roles ------------------------------------------------------------------------------

    void modeOnlyFrontmatterIsHidden()
    {
        Fixture f(DocKey + u"# Tytuł\n\nTreść\n"_s);
        QVERIFY(f.role(0, "noteModeOnly").toBool());
        QCOMPARE(f.visible(), QList<bool>({false, true, true}));
        QCOMPARE(f.model()->visibleCount(), 2);
        QCOMPARE(f.role(0, "properties").toList().size(), 0);
        QCOMPARE(f.page.previousVisibleRow(1), -1);
        // Nothing to merge into above the first block.
        f.page.startEditing(1, 0);
        QCOMPARE(f.page.mergeWithPrevious(1).value(u"row"_s).toInt(), -1);
        QCOMPARE(f.text(), DocKey + u"# Tytuł\n\nTreść\n"_s);
        // Edited (Esc, then Up from the first block of the outline is not how one gets there, but
        // a route to line 0 is): shown while edited.
        f.page.startEditing(0, -1);
        QVERIFY(f.role(0, "visible").toBool());
        f.page.stopEditing();
        QVERIFY(!f.role(0, "visible").toBool());
        // Other properties: an ordinary panel without the mode key.
        Fixture props(u"---\nautor: Łucja\nbrulion: document\n---\nTreść\n"_s);
        QVERIFY(!props.role(0, "noteModeOnly").toBool());
        QVERIFY(props.role(0, "visible").toBool());
        const QVariantList list = props.role(0, "properties").toList();
        QCOMPARE(list.size(), 1);
        QCOMPARE(list[0].toMap().value(u"key"_s).toString(), u"autor"_s);
        // A page with nothing but its mode is empty for the views.
        Fixture empty(DocKey);
        QCOMPARE(empty.count(), 1);
        QCOMPARE(empty.model()->visibleCount(), 0);
    }

    void listMarkersAndPreviousKinds()
    {
        Fixture f(u"# Plan\n\nAkapit.\n\n- jeden\n\t- pod\n\n2. dwa\n\n3) trzy\n\n* gwiazdka\n\nKoniec\n"_s);
        QCOMPARE(f.role(0, "listMarker").toString(), QString());
        QCOMPARE(f.role(2, "listMarker").toString(), u"-"_s);
        QCOMPARE(f.role(3, "listMarker").toString(), u"-"_s);
        QCOMPARE(f.role(4, "listMarker").toString(), u"2."_s);
        QCOMPARE(f.role(5, "listMarker").toString(), u"3)"_s);
        QCOMPARE(f.role(6, "listMarker").toString(), u"*"_s);
        QCOMPARE(f.model()->get(4).value(u"listMarker"_s).toString(), u"2."_s);
        const QStringList prev{QString(),     u"heading"_s,  u"paragraph"_s, u"listItem"_s,
                               u"listItem"_s, u"listItem"_s, u"listItem"_s,  u"listItem"_s};
        QCOMPARE(f.count(), prev.size());
        for (int r = 0; r < f.count(); ++r)
            QCOMPARE(f.role(r, "prevKind").toString(), prev.value(r));
        // prevKind follows edits of the row before (a paragraph typed into a list item).
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.page.setEditText(1, u"- akapit jako punkt"_s, 19);
        QCOMPARE(f.kind(1), u"listItem"_s);
        QCOMPARE(f.role(2, "prevKind").toString(), u"listItem"_s);
        QVERIFY(!changed.isEmpty());
        // Hidden rows do not count: the first content block after a mode-only frontmatter.
        Fixture hidden(DocKey + u"Akapit\n"_s);
        QCOMPARE(hidden.role(1, "prevKind").toString(), QString());
    }

    void documentModeIgnoresFolds()
    {
        Fixture f(u"- a\n\t- a1\n\t\t- a11\n- b\n"_s);
        f.page.setFolded(0, true);
        f.page.zoomInto(3);
        QCOMPARE(f.page.zoomBlock(), 3);
        QVERIFY(f.page.setMode(u"document"_s));
        // No zoom; every block shown, nothing reported as folded; the folds are kept.
        QCOMPARE(f.page.zoomBlock(), -1);
        QCOMPARE(f.visible(), QList<bool>({false, true, true, true, true})); // row 0: the mode key
        QVERIFY(!f.role(1, "collapsed").toBool());
        QCOMPARE(f.page.foldState(), QVariantList{3}); // line of "- a" below the frontmatter
        // Zoom and folding commands do nothing here.
        f.page.zoomInto(1);
        QCOMPARE(f.page.zoomBlock(), -1);
        f.page.toggleFold(4);
        f.page.foldAll(true);
        QCOMPARE(f.page.foldState(), QVariantList{3});
        // Back in the outline the fold is there again — but the edited block stays in view.
        f.page.startEditing(3, 0);
        QVERIFY(f.page.setMode(u"outline"_s));
        QVERIFY(f.role(3, "visible").toBool());
        QCOMPARE(f.page.foldState(), QVariantList());
    }

    // --- document mode commands ----------------------------------------------------------------------

    void enterInDocuments()
    {
        Fixture f(DocKey + u"# Lista zakupów\n\nNa sobotę:\n\n- [ ] mleko\n- chleb\n1. pierwszy\n"_s);
        QCOMPARE(f.page.mode(), u"document"_s);
        // Enter at the end of a heading: a paragraph after it.
        f.page.startEditing(1, 15);
        QVariantMap r = f.page.split(1, 15);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(f.kind(2), u"paragraph"_s);
        f.page.setEditText(2, u"Żółty ser też."_s, 14);
        // Enter in a paragraph: a new paragraph.
        r = f.page.split(2, 14);
        QCOMPARE(f.kind(3), u"paragraph"_s);
        QCOMPARE(f.page.editingRow(), 3);
        f.page.undo();
        // Enter in a task continues the checkbox; on the empty new task it leaves the list.
        f.page.startEditing(4, 5);
        r = f.page.split(4, 5);
        QCOMPARE(r.value(u"row"_s).toInt(), 5);
        QCOMPARE(f.role(5, "taskStatus").toString(), u"todo"_s);
        r = f.page.split(5, 0);
        QCOMPARE(r.value(u"row"_s).toInt(), 5);
        QCOMPARE(f.kind(5), u"paragraph"_s);
        QCOMPARE(f.edit(5), QString());
        QCOMPARE(f.page.editingRow(), 5);
        QCOMPARE(
                f.text(),
                DocKey + u"# Lista zakupów\nŻółty ser też.\n\nNa sobotę:\n\n- [ ] mleko\n\n\n\n- chleb\n1. pierwszy\n"_s);
        f.page.undo();
        // Numbering continues.
        f.page.startEditing(7, 8);
        f.page.split(7, 8);
        QCOMPARE(f.role(8, "listMarker").toString(), u"2."_s);
    }

    void enterOnEmptyNestedItemOutdents()
    {
        Fixture f(DocKey + u"Akapit\n\n- a\n\t- a1\n\t- \n"_s);
        f.page.startEditing(4, 0);
        const QVariantMap r = f.page.split(4, 0);
        QCOMPARE(r.value(u"row"_s).toInt(), 4);
        QCOMPARE(f.text(), DocKey + u"Akapit\n\n- a\n\t- a1\n- \n"_s);
        // Once more: top level, it becomes a paragraph after the list.
        f.page.split(4, 0);
        QCOMPARE(f.kind(4), u"paragraph"_s);
        QCOMPARE(f.text(), DocKey + u"Akapit\n\n- a\n\t- a1\n\n\n"_s);
    }

    void backspaceLeavesTheList()
    {
        Fixture f(DocKey + u"Wstęp\n\n- jabłka\n\t- antonówki\n- gruszki\n"_s);
        // Top level: the item becomes a paragraph in place, its children move out.
        f.page.startEditing(2, 0);
        QVariantMap r = f.page.leaveList(2);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(r.value(u"cursor"_s).toInt(), 0);
        QCOMPARE(f.text(), DocKey + u"Wstęp\n\njabłka\n\n- antonówki\n- gruszki\n"_s);
        QCOMPARE(f.kind(2), u"paragraph"_s);
        QCOMPARE(f.page.editingRow(), 2);
        QCOMPARE(f.page.cursorPosition(), 0);
        f.page.undo();
        QCOMPARE(f.text(), DocKey + u"Wstęp\n\n- jabłka\n\t- antonówki\n- gruszki\n"_s);
        // Nested: one level out, the caret stays.
        f.page.startEditing(3, 0);
        r = f.page.leaveList(3);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(f.text(), DocKey + u"Wstęp\n\n- jabłka\n- antonówki\n- gruszki\n"_s);
        QCOMPARE(f.page.editingRow(), 3);
        // Not a list item: nothing.
        QCOMPARE(f.page.leaveList(1).value(u"row"_s).toInt(), -1);
        QCOMPARE(f.page.leaveList(99).value(u"row"_s).toInt(), -1);
    }

    void backspaceLeavesNumberedListsAndKeepsEveryLine()
    {
        // A numbered item with nested children and a continuation line loses its marker: no
        // line of text is lost, the children stay a (renumbered) list after it.
        const QString content =
                DocKey + u"Wstęp\n\n1. pierwszy\n2. drugi ąę\n   ciąg dalszy\n   1. a\n   2. b\n3. trzeci\n"_s;
        Fixture f(content);
        QCOMPARE(f.edit(3), u"drugi ąę\nciąg dalszy"_s);
        f.page.startEditing(3, 0);
        const QVariantMap r = f.page.leaveList(3);
        QCOMPARE(r.value(u"row"_s).toInt(), 3);
        QCOMPARE(f.kind(3), u"paragraph"_s);
        QCOMPARE(f.edit(3), u"drugi ąę\nciąg dalszy"_s);
        const QString out = f.text();
        for (const QString &word :
             {u"Wstęp"_s, u"pierwszy"_s, u"drugi ąę"_s, u"ciąg dalszy"_s, u"a"_s, u"b"_s, u"trzeci"_s})
            QVERIFY2(out.contains(word), qPrintable(out));
        // The text reads back as the same blocks (the model and the file agree).
        markdown::BlockDocument reread(out);
        QCOMPARE(reread.count(), f.count());
        for (int i = 0; i < reread.count(); ++i) {
            QCOMPARE(reread.at(i).kind, f.page.blocks()->document().at(i).kind);
            QCOMPARE(reread.at(i).body, f.page.blocks()->document().at(i).body);
        }
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void enterOnEmptyItemWithChildren()
    {
        // An empty top-level item with children: Enter leaves the list, the children stay.
        const QString content = DocKey + u"Akapit\n\n- \n\t- dziecko żółte\n- dalej\n"_s;
        Fixture f(content);
        f.page.startEditing(2, 0);
        const QVariantMap r = f.page.split(2, 0);
        QCOMPARE(r.value(u"row"_s).toInt(), 2);
        QCOMPARE(f.kind(2), u"paragraph"_s);
        QVERIFY2(f.text().contains(u"- dziecko żółte\n- dalej\n"_s), qPrintable(f.text()));
        f.page.undo();
        QCOMPARE(f.text(), content);
    }

    void backspaceOnTheFirstItemAfterHiddenFrontmatter()
    {
        // The first block is a list item under a hidden mode-only frontmatter: Backspace makes it
        // a paragraph and the frontmatter stays as it is (hidden, unchanged).
        Fixture f(DocKey + u"- jedyny żółw\n"_s);
        f.page.startEditing(1, 0);
        QCOMPARE(f.page.leaveList(1).value(u"row"_s).toInt(), 1);
        QCOMPARE(f.text(), DocKey + u"jedyny żółw\n"_s);
        QVERIFY(!f.role(0, "visible").toBool());
        QCOMPARE(f.page.mergeWithPrevious(1).value(u"row"_s).toInt(), -1);
        QCOMPARE(f.text(), DocKey + u"jedyny żółw\n"_s);
    }

    void frontmatterWithMoreThanTheModeIsShown()
    {
        // A comment or text next to the key is the user's: the block stays visible.
        for (const QString &content : {u"---\n# wybrane ręcznie\nbrulion: document\n---\nTreść\n"_s,
                                       u"---\nWstęp — zażółć gęślą jaźń.\nbrulion: document\n---\nTreść\n"_s}) {
            Fixture f(content);
            QCOMPARE(f.page.mode(), u"document"_s);
            QVERIFY(!f.role(0, "noteModeOnly").toBool());
            QVERIFY(f.role(0, "visible").toBool());
            QCOMPARE(f.model()->visibleCount(), 2);
        }
    }

    void tabIndentsNoListItemAfterAParagraph()
    {
        // The first item of a list right after a paragraph has no item to go under: Tab must not
        // turn the paragraph into a list item (or the item into an indented code block).
        const QString content = DocKey + u"Akapit\n\n- a\n- b\n"_s;
        Fixture f(content);
        f.page.indent(2);
        QCOMPARE(f.text(), content);
        QCOMPARE(f.kind(1), u"paragraph"_s);
        // Shift+Tab on a paragraph does nothing either.
        QVERIFY(!f.page.outdent(1));
        QCOMPARE(f.text(), content);
    }

    void tabOnlyIndentsListItems()
    {
        Fixture f(DocKey + u"Akapit ąę\n\n- a\n- b\n"_s);
        QVERIFY(!f.page.indent(1));
        QCOMPARE(f.text(), DocKey + u"Akapit ąę\n\n- a\n- b\n"_s);
        QVERIFY(!f.page.canUndo());
        QVERIFY(f.page.indent(3));
        QCOMPARE(f.text(), DocKey + u"Akapit ąę\n\n- a\n\t- b\n"_s);
        QVERIFY(f.page.outdent(3));
        QVERIFY(!f.page.outdent(3)); // top level: nowhere to go
        // The outline mode still turns a paragraph into a block on Tab.
        QVERIFY(f.page.setMode(u"outline"_s));
        QVERIFY(f.page.indent(1));
        QCOMPARE(f.kind(1), u"listItem"_s);
    }

    void listButtonTogglesParagraphs()
    {
        Fixture f(DocKey + u"Zażółć gęślą\ndruga linia\n\n# Nagłówek\n"_s);
        f.page.startEditing(1, 4);
        QVariantMap r = f.page.toggleList(1);
        QCOMPARE(r.value(u"row"_s).toInt(), 1);
        QCOMPARE(f.text(), DocKey + u"- Zażółć gęślą\n  druga linia\n\n# Nagłówek\n"_s);
        QCOMPARE(f.edit(1), u"Zażółć gęślą\ndruga linia"_s);
        QCOMPARE(f.page.cursorPosition(), 4); // the same place in the text
        r = f.page.toggleList(1);
        QCOMPARE(f.text(), DocKey + u"Zażółć gęślą\ndruga linia\n\n# Nagłówek\n"_s);
        QCOMPARE(f.page.toggleList(2).value(u"row"_s).toInt(), -1); // headings stay headings
        QCOMPARE(f.page.toggleList(0).value(u"row"_s).toInt(), -1); // frontmatter
        f.page.undo();
        QCOMPARE(f.kind(1), u"listItem"_s);
    }

    void taskOnAParagraphMakesATaskItem()
    {
        // Ctrl+Enter / the toolbar's task button on a paragraph: a task list item, never a
        // paragraph with a literal "[ ]" (only list items are tasks, SYNTAX §5).
        Fixture f(DocKey + u"Kupić żółty ser\n\nInny akapit\n"_s);
        f.page.startEditing(1, 4);
        f.page.cycleTask(1);
        QCOMPARE(f.kind(1), u"listItem"_s);
        QCOMPARE(f.role(1, "taskStatus").toString(), u"todo"_s);
        QCOMPARE(f.edit(1), u"Kupić żółty ser"_s);
        QVERIFY(f.text().startsWith(DocKey + u"- [ ] Kupić żółty ser\n"_s));
        f.page.undo();
        QCOMPARE(f.text(), DocKey + u"Kupić żółty ser\n\nInny akapit\n"_s);
    }

    void newBlocksAreParagraphs()
    {
        Fixture f(DocKey + u"Akapit\n\n- punkt\n"_s);
        // After a paragraph: a paragraph; after a list item: a list item.
        int row = f.page.insertBlockAfter(1, u"Nowy akapit"_s);
        QCOMPARE(row, 2);
        QCOMPARE(f.kind(2), u"paragraph"_s);
        row = f.page.insertBlockAfter(3, u"drugi punkt"_s);
        QCOMPARE(f.kind(row), u"listItem"_s);
        QCOMPARE(f.text(), DocKey + u"Akapit\n\nNowy akapit\n\n- punkt\n- drugi punkt\n"_s);
        // A click below the last block: an empty paragraph (a last empty block is used as it is).
        row = f.page.appendEmptyBlock();
        QCOMPARE(f.kind(row), u"paragraph"_s);
        QCOMPARE(f.page.appendEmptyBlock(), row);
        QCOMPARE(f.page.editingRow(), row);
        // A new page in a document vault starts with a paragraph, and an empty one writes nothing.
        FakeEditorHost host;
        host.setPageMode(u"document"_s);
        PageController page;
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(u"Nowa strona.md"_s);
        QCOMPARE(page.mode(), u"document"_s);
        QCOMPARE(page.startNewPage(QString()), 0);
        QCOMPARE(page.blocks()->get(0).value(u"kind"_s).toString(), u"paragraph"_s);
        QVERIFY(page.save());
        QCOMPARE(host.writeCount(), 0);
        page.setEditText(0, u"Pierwsza myśl"_s, 13);
        QCOMPARE(host.file(u"Nowa strona.md"_s), u"Pierwsza myśl\n"_s);
        // A template without an empty bullet gets a paragraph in a document.
        host.setFile(u"templates/notatka.md"_s, u"---\nbrulion: document\n---\n# {{title}}"_s);
        page.setPath(u"Inna.md"_s);
        const int start = page.startNewPage(u"templates/notatka.md"_s);
        QCOMPARE(page.mode(), u"document"_s);
        QCOMPARE(page.blocks()->get(start).value(u"kind"_s).toString(), u"paragraph"_s);
    }

    // The empty paragraph at the end of a document (a click below the last block) stays a row
    // when an edit elsewhere is undone and redone: it is only a blank line in the text.
    void undoKeepsTheLastEmptyParagraph_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("edited"); // the row typed into after the empty paragraph is added
        QTest::newRow("final-newline") << DocKey + u"Akapit źdźbła\n\nDrugi\n"_s << 1;
        QTest::newRow("no-final-newline") << DocKey + u"Akapit źdźbła\n\nDrugi"_s << 2;
        QTest::newRow("after-a-list") << DocKey + u"Akapit\n\n- punkt ą\n"_s << 2;
    }

    void undoKeepsTheLastEmptyParagraph()
    {
        QFETCH(QString, content);
        QFETCH(int, edited);
        Fixture f(content);
        const int empty = f.page.appendEmptyBlock();
        QVERIFY(empty > 0);
        QCOMPARE(f.kind(empty), u"paragraph"_s);
        QCOMPARE(f.page.blocks()->editTextAt(empty), QString());
        const int rows = f.page.blocks()->rowCount();
        const QString withEmpty = f.text();
        f.page.startEditing(edited, 0);
        f.page.setEditText(edited, u"żółw "_s + f.page.blocks()->editTextAt(edited), 5);
        const QString typed = f.text();
        for (int round = 0; round < 2; ++round) {
            f.page.undo();
            QCOMPARE(f.text(), withEmpty);
            QCOMPARE(f.page.blocks()->rowCount(), rows);
            QCOMPARE(f.kind(empty), u"paragraph"_s);
            QCOMPARE(f.page.blocks()->editTextAt(empty), QString());
            QCOMPARE(f.page.editingRow(), edited);
            f.page.redo();
            QCOMPARE(f.text(), typed);
            QCOMPARE(f.page.blocks()->rowCount(), rows);
            QCOMPARE(f.page.blocks()->editTextAt(empty), QString());
        }
        // Typing into the empty paragraph after the redo.
        f.page.startEditing(empty, 0);
        f.page.setEditText(empty, u"koniec ę"_s, 8);
        QVERIFY(f.text().startsWith(typed.trimmed()));
        QVERIFY(f.text().contains(u"koniec ę"_s));
        QCOMPARE(f.page.blocks()->rowCount(), rows);
    }

    void playgroundDocumentSample()
    {
        // The playground's document sample opens as a document, whatever the vault default.
        FakeEditorHost host;
        host.populateSample();
        PageController page;
        page.setHost(&host);
        page.setPath(host.documentPath());
        QCOMPARE(page.mode(), u"document"_s);
        QCOMPARE(page.modeSource(), u"frontmatter"_s);
        page.setPath(host.samplePath());
        QCOMPARE(page.mode(), u"outline"_s);
    }
};

QTEST_MAIN(TestNoteMode)
#include "tst_editor_notemode.moc"
