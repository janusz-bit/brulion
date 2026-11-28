// Extensions' input rules and styles of blocks by tag in the block editor (docs/UI.md "Input
// rules", "Styles of blocks by tag"): what a rule replaces as the text is typed (the longest match,
// whole words, Polish text, never in code, math, comments, link targets or URLs, never on a paste or
// a deletion), the replacement as an undo step of its own (one undo brings the typed text back), and
// the tint a block's tag gives its row (its parents' too, not from code, followed as the styles or
// the text change).
#include "blocklistmodel.h"
#include "fakeeditorhost.h"
#include "inputrules.h"
#include "pagecontroller.h"

#include <QSignalSpy>
#include <QTest>

using namespace brulion;
using namespace brulion::editor;
using namespace Qt::StringLiterals;

namespace {

const QString Page = u"Notatki/Strona.md"_s;

const QList<InputRule> Rules{
        {u"->"_s, u"→"_s},  {u"<-"_s, u"←"_s},          {u"-->"_s, u"⟶"_s},
        {u"(c)"_s, u"©"_s}, {u"teh"_s, u"the"_s, true}, {u"zolw"_s, u"żółw"_s, true},
        {u"ż>"_s, u"ż→"_s},
};

struct Fixture {
    FakeEditorHost host;
    PageController page;

    explicit Fixture(const QString &content)
    {
        host.setFile(Page, content);
        host.setInputRules(Rules);
        page.setSaveDelay(0);
        page.setHost(&host);
        page.setPath(Page);
    }
    BlockListModel *model() const { return page.blocks(); }
    QVariant role(int row, const char *name) const
    {
        return model()->data(model()->index(row), model()->roleNames().key(name));
    }
    QString edit(int row) const { return role(row, "editText").toString(); }
    /// Types `typed` at the end of row `row`'s text, one character at a time, as the inline editor
    /// hands each keystroke over (with the text it shows: the controller's after a reset).
    void type(int row, const QString &typed)
    {
        for (const QChar c : typed) {
            const QString text = edit(row) + c;
            page.setEditText(row, text, int(text.size()));
        }
    }
};

} // namespace

class TstEditorInputRules : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void hits_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("typed");
        QTest::addColumn<QString>("expected"); // the text after the rule; "" = nothing fires

        QTest::newRow("arrow") << u"a ->"_s << 1 << u"a →"_s;
        QTest::newRow("arrow at the start") << u"->"_s << 1 << u"→"_s;
        QTest::newRow("left arrow, Polish text") << u"zażółć gęślą <-"_s << 1 << u"zażółć gęślą ←"_s;
        QTest::newRow("the longest match wins") << u"x -->"_s << 1 << u"x ⟶"_s;
        QTest::newRow("three characters") << u"(c)"_s << 1 << u"©"_s;
        QTest::newRow("a match with a Polish letter") << u"ż>"_s << 1 << u"ż→"_s;
        QTest::newRow("nothing typed") << u"a ->"_s << 0 << QString();
        QTest::newRow("a paste: more than the match") << u"a ->"_s << 3 << QString();
        QTest::newRow("the match typed at once") << u"a ->"_s << 2 << u"a →"_s;
        QTest::newRow("no match") << u"a -"_s << 1 << QString();
        // Whole words.
        QTest::newRow("word: a space after it") << u"teh "_s << 1 << u"the "_s;
        QTest::newRow("word: punctuation after it") << u"o teh,"_s << 1 << u"o the,"_s;
        QTest::newRow("word: Polish replacement") << u"mój zolw."_s << 1 << u"mój żółw."_s;
        QTest::newRow("word: not yet ended") << u"teh"_s << 1 << QString();
        QTest::newRow("word: a letter after it") << u"tehx"_s << 1 << QString();
        QTest::newRow("word: a Polish letter after it") << u"tehą"_s << 1 << QString();
        QTest::newRow("word: inside a word") << u"xteh "_s << 1 << QString();
        QTest::newRow("word: after a Polish letter") << u"żteh "_s << 1 << QString();
        QTest::newRow("word: after a digit") << u"2teh "_s << 1 << QString();
        QTest::newRow("word: committed with its space") << u"teh "_s << 4 << u"the "_s;
        QTest::newRow("word: more than the word") << u" teh "_s << 5 << QString();
        // Code.
        QTest::newRow("open code span") << u"`a ->"_s << 1 << QString();
        QTest::newRow("open double code span") << u"``a ` ->"_s << 1 << QString();
        QTest::newRow("after a closed code span") << u"`kod` ->"_s << 1 << u"`kod` →"_s;
        QTest::newRow("escaped backtick") << u"\\` ->"_s << 1 << u"\\` →"_s;
        QTest::newRow("fenced code in the text") << u"tekst\n```\nx ->"_s << 1 << QString();
        QTest::newRow("on the fence line") << u"```js ->"_s << 1 << QString();
        QTest::newRow("after a closed fence") << u"```\nx\n```\nżółw ->"_s << 1 << u"```\nx\n```\nżółw →"_s;
        QTest::newRow("a code span ends at a blank line") << u"`a\n\nb ->"_s << 1 << u"`a\n\nb →"_s;
        // Math.
        QTest::newRow("open formula") << u"$x ->"_s << 1 << QString();
        QTest::newRow("closed formula before") << u"$x$ ->"_s << 1 << u"$x$ →"_s;
        QTest::newRow("display formula lines") << u"$$\na ->"_s << 1 << QString();
        QTest::newRow("open display formula") << u"$$a ->"_s << 1 << QString();
        QTest::newRow("money is no formula") << u"5 $ i 10 $ ->"_s << 1 << u"5 $ i 10 $ →"_s;
        // Comments, links, URLs.
        QTest::newRow("comment") << u"%%uwaga ->"_s << 1 << QString();
        QTest::newRow("after a comment") << u"%%uwaga%% ->"_s << 1 << u"%%uwaga%% →"_s;
        QTest::newRow("wiki link target") << u"[[Strona ->"_s << 1 << QString();
        QTest::newRow("embed target") << u"![[Plik ->"_s << 1 << QString();
        QTest::newRow("after a wiki link") << u"[[Strona]] ->"_s << 1 << u"[[Strona]] →"_s;
        QTest::newRow("Markdown link destination") << u"[tekst](plik ->"_s << 1 << QString();
        QTest::newRow("Markdown link text") << u"[tekst ->"_s << 1 << u"[tekst →"_s;
        QTest::newRow("after a Markdown link") << u"[a](b) ->"_s << 1 << u"[a](b) →"_s;
        QTest::newRow("URL") << u"https://example.com/a->"_s << 1 << QString();
        QTest::newRow("www") << u"www.example.com/a->"_s << 1 << QString();
        QTest::newRow("after a URL") << u"https://example.com ->"_s << 1 << u"https://example.com →"_s;
        QTest::newRow("a wiki link in code opens nothing") << u"`[[` ->"_s << 1 << u"`[[` →"_s;
    }
    void hits()
    {
        QFETCH(QString, text);
        QFETCH(int, typed);
        QFETCH(QString, expected);
        const int cursor = int(text.size());
        const std::optional<InputRuleHit> hit = inputRuleHit(text, cursor, typed, Rules);
        if (expected.isEmpty()) {
            QVERIFY2(!hit, hit ? qPrintable(hit->replace) : "");
            return;
        }
        QVERIFY(hit);
        QCOMPARE(text.left(hit->from) + hit->replace + text.mid(hit->to), expected);
    }

    void caretInTheMiddle()
    {
        // Typed before other text: the rule looks at the text before the caret only.
        const QString text = u"a -> b"_s;
        const auto hit = inputRuleHit(text, 4, 1, Rules);
        QVERIFY(hit);
        QCOMPARE(hit->from, 2);
        QCOMPARE(hit->to, 4);
        // Inside a closed code span, typed in its middle.
        QVERIFY(!inputRuleHit(u"`a -> b`"_s, 5, 1, Rules));
        // Inside a closed formula.
        QVERIFY(!inputRuleHit(u"$a -> b$"_s, 5, 1, Rules));
        // Out of range, no rules.
        QVERIFY(!inputRuleHit(u"->"_s, 3, 1, Rules));
        QVERIFY(!inputRuleHit(u"->"_s, 2, 1, {}));
    }

    void laterRuleWinsATie()
    {
        const QList<InputRule> rules{{u"->"_s, u"→"_s}, {u"->"_s, u"⇒"_s}};
        QCOMPARE(inputRuleHit(u"->"_s, 2, 1, rules)->replace, u"⇒"_s);
        const QList<InputRule> other{{u"->"_s, u"⇒"_s}, {u"->"_s, u"→"_s}};
        QCOMPARE(inputRuleHit(u"->"_s, 2, 1, other)->replace, u"→"_s);
    }

    void emptyReplacementRemovesTheMatch()
    {
        const QList<InputRule> rules{{u"!!"_s, QString()}};
        const auto hit = inputRuleHit(u"tak!!"_s, 5, 1, rules);
        QVERIFY(hit);
        QCOMPARE(hit->from, 3);
        QCOMPARE(hit->replace, QString());
    }

    // ---- in the controller -------------------------------------------------------------------------

    void typingReplacesAndUndoBringsTheTextBack()
    {
        Fixture f(u"- zadanie\n- drugi\n"_s);
        QSignalSpy resets(&f.page, &PageController::editTextReset);
        f.page.startEditing(0, 7);
        f.type(0, u" ->"_s);
        QCOMPARE(f.page.text(), u"- zadanie →\n- drugi\n"_s);
        QVERIFY(!resets.isEmpty());
        QCOMPARE(resets.last().at(0).toInt(), 0);
        QCOMPARE(resets.last().at(1).toString(), u"zadanie →"_s);
        QCOMPARE(resets.last().at(2).toInt(), 9);
        QCOMPARE(f.page.cursorPosition(), 9);
        // One undo: the typed text back, the caret after it.
        f.page.undo();
        QCOMPARE(f.page.text(), u"- zadanie ->\n- drugi\n"_s);
        QCOMPARE(f.page.editingRow(), 0);
        QCOMPARE(f.page.cursorPosition(), 10);
        // The next: what was typed before it.
        f.page.undo();
        QCOMPARE(f.page.text(), u"- zadanie\n- drugi\n"_s);
        QVERIFY(!f.page.canUndo());
        f.page.redo();
        QCOMPARE(f.page.text(), u"- zadanie ->\n- drugi\n"_s);
        f.page.redo();
        QCOMPARE(f.page.text(), u"- zadanie →\n- drugi\n"_s);
    }

    void typingAfterTheReplacementIsAStepOfItsOwn()
    {
        Fixture f(u"- a\n"_s);
        f.page.startEditing(0, 1);
        f.type(0, u" -> zażółć"_s);
        QCOMPARE(f.page.text(), u"- a → zażółć\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"- a →\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"- a ->\n"_s);
        // Typed on after the undo: the rule does not take it back.
        f.type(0, u" b"_s);
        QCOMPARE(f.page.text(), u"- a -> b\n"_s);
    }

    void wordRuleKeepsTheCharacterTyped()
    {
        Fixture f(u"Mój\n"_s);
        f.page.startEditing(0, 3);
        f.type(0, u" zolw teh."_s);
        QCOMPARE(f.page.text(), u"Mój żółw the.\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"Mój żółw teh.\n"_s);
    }

    void headingsAndParagraphs()
    {
        Fixture f(u"# Tytuł\n\nAkapit\n"_s);
        f.page.startEditing(0, 5);
        f.type(0, u" ->"_s);
        f.page.startEditing(1, 6);
        f.type(1, u" <-"_s);
        QCOMPARE(f.page.text(), u"# Tytuł →\n\nAkapit ←\n"_s);
    }

    void notInCodeOrWhenDeleting()
    {
        Fixture f(u"```\nkod\n```\n- `x\n- y ->z\n"_s);
        // A code block's text stays code.
        f.page.startEditing(0, 3);
        f.type(0, u" ->"_s);
        QCOMPARE(f.edit(0), u"kod ->"_s);
        // An open code span in a list item.
        f.page.startEditing(1, 2);
        f.type(1, u" ->"_s);
        QCOMPARE(f.edit(1), u"`x ->"_s);
        // A deletion that leaves "->" before the caret replaces nothing.
        f.page.startEditing(2, 5);
        f.page.setEditText(2, u"y ->"_s, 4);
        QCOMPARE(f.edit(2), u"y ->"_s);
    }

    // ---- source mode: the match on the caret's line, the protection over the whole text ---------------

    void matchIgnoresProtection()
    {
        // The match alone: source mode finds it on the caret's line, then asks the whole text.
        const QList<InputRule> rules{{u"->"_s, u"→"_s}, {u"zolw"_s, u"żółw"_s, true}};
        QCOMPARE(inputRuleMatch(u"`a ->"_s, 5, 1, rules), (InputRuleHit{3, 5, u"→"_s}));
        QCOMPARE(inputRuleHit(u"`a ->"_s, 5, 1, rules), std::nullopt);
        QCOMPARE(inputRuleMatch(u"mój zolw "_s, 9, 1, rules), (InputRuleHit{4, 8, u"żółw"_s}));
        QCOMPARE(inputRuleMatch(u"a -"_s, 3, 1, rules), std::nullopt);
        QCOMPARE(inputRuleMatch(u"a ->"_s, 4, 3, rules), std::nullopt); // more typed than the match: a paste
    }

    void aLongerRuleDeclaredFirstStillWins()
    {
        // The rules are tried from the last declared: a shorter one found first does not hide a
        // longer one declared before it.
        const QList<InputRule> rules{{u"-->"_s, u"⟶"_s}, {u"->"_s, u"→"_s}};
        QCOMPARE(inputRuleHit(u"a -->"_s, 5, 1, rules), (InputRuleHit{2, 5, u"⟶"_s}));
    }

    void sourceProtected_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("isProtected");
        // The whole source; "‸" marks where a rule would start replacing, just typed.
        QTest::newRow("a paragraph") << u"Akapit ‸"_s << false;
        QTest::newRow("a list item") << u"# Nagłówek\n\n- [ ] zadanie ‸\n"_s << false;
        QTest::newRow("a heading") << u"## Zażółć ‸"_s << false;
        QTest::newRow("in the frontmatter") << u"---\ntytuł: a ‸\n---\nTreść\n"_s << true;
        QTest::newRow("after the frontmatter") << u"---\ntytuł: a\n---\nTreść ‸"_s << false;
        QTest::newRow("the body's first character") << u"---\ntytuł: a\n---\n‸->"_s << false;
        QTest::newRow("the frontmatter's last character") << u"---\ntytuł: a‸\n---\n"_s << true;
        QTest::newRow("frontmatter never closed: text") << u"---\ntytuł: a ‸\nTreść\n"_s << false;
        QTest::newRow("a fence lines above") << u"Tekst\n\n```luau\nlocal x = 1\nprint(x ‸)\n```\n"_s << true;
        QTest::newRow("after a closed fence") << u"```\nkod\n```\nza nim ‸"_s << false;
        QTest::newRow("a code span over two lines") << u"Akapit `kod\ndalej ‸"_s << true;
        QTest::newRow("a code span cut by a blank line") << u"Akapit `kod\n\nnowy ‸"_s << false;
        QTest::newRow("a display formula") << u"$$\nx = ‸\n$$\n"_s << true;
        // A code span ends where a Markdown block does: the next list item, a heading, a quote.
        QTest::newRow("a code span open in the item above") << u"- Strzałka `a->\n- Zwykły blok ‸\n"_s << false;
        QTest::newRow("a numbered item after it") << u"1. `kod\n2. dalej ‸"_s << false;
        QTest::newRow("a heading after it") << u"Tekst `kod\n## Nagłówek ‸"_s << false;
        QTest::newRow("a quote after it") << u"Tekst `kod\n> cytat ‸"_s << false;
        QTest::newRow("open in the same item") << u"- `kod ‸\n- dalej\n"_s << true;
        QTest::newRow("after a closed display formula") << u"$$\nx\n$$\npo nim ‸"_s << false;
        QTest::newRow("a display formula with a blank line") << u"$$\n\nx = ‸\n$$\n"_s << true;
        QTest::newRow("a tilde fence lines above") << u"~~~\nkod ‸\n~~~\n"_s << true;
        QTest::newRow("a fence after a first empty line") << u"\n~~~\nkod ‸\n~~~\n"_s << true;
        QTest::newRow("a backslash at the caret") << u"Akapit \\‸"_s << false;
        QTest::newRow("a percent sign at the caret") << u"Akapit %‸"_s << false;
        QTest::newRow("a backtick at the caret") << u"Akapit `‸"_s << true;
        QTest::newRow("a backtick in a closed comment") << u"a %% x ` y %% dalej ‸"_s << false;
        QTest::newRow("a link destination from the start") << u"](cel ‸"_s << true;
        QTest::newRow("the very start") << u"‸`kod` dalej"_s << false;
        QTest::newRow("a one-letter code span closed") << u"`a` dalej ‸"_s << false;
        QTest::newRow("right after a formula's opening $") << u"$‸x$ dalej"_s << true;
    }
    void sourceProtected()
    {
        QFETCH(QString, text);
        QFETCH(bool, isProtected);
        const int at = int(text.indexOf(u'‸'));
        text.remove(at, 1);
        QCOMPARE(sourceInputRuleProtected(text, at), isProtected);
    }

    void noRulesNoChange()
    {
        Fixture f(u"- a\n"_s);
        f.host.setInputRules({});
        f.page.startEditing(0, 1);
        f.type(0, u" ->"_s);
        QCOMPARE(f.page.text(), u"- a ->\n"_s);
        f.page.undo();
        QCOMPARE(f.page.text(), u"- a\n"_s); // the typing alone, one step
    }

    // ---- styles of blocks by tag ----------------------------------------------------------------------

    void tagsTintTheirBlocks()
    {
        Fixture f(u"- zrobić #pilne\n- zwykły\n- `#pilne` w kodzie\n- #projekt/dom\n- #PILNE wielkimi\n"_s);
        QCOMPARE(f.role(0, "tagTint").toString(), QString());
        QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
        f.host.setTagStyle(u"pilne"_s, u"danger"_s);
        f.host.setTagStyle(u"projekt"_s, u"#12ab34"_s, false, true);
        QVERIFY(!changed.isEmpty());
        QCOMPARE(f.role(0, "tagTint").toString(), u"danger"_s);
        QCOMPARE(f.role(0, "tagTintMode").toInt(), 3);
        QCOMPARE(f.role(1, "tagTint").toString(), QString());
        QCOMPARE(f.role(1, "tagTintMode").toInt(), 0);
        QCOMPARE(f.role(2, "tagTint").toString(), QString());    // a tag in code is none
        QCOMPARE(f.role(3, "tagTint").toString(), u"#12ab34"_s); // by its parent's style
        QCOMPARE(f.role(3, "tagTintMode").toInt(), 2);
        QCOMPARE(f.role(4, "tagTint").toString(), u"danger"_s); // tags ignore case
        // Typed into a block: its row tells the tint.
        changed.clear();
        f.page.startEditing(1, 6);
        f.page.setEditText(1, u"zwykły #pilne"_s, 13);
        QCOMPARE(f.role(1, "tagTint").toString(), u"danger"_s);
        bool told = false;
        for (const QList<QVariant> &args : changed) {
            const QList<int> roles = args.at(2).value<QList<int>>();
            told = told || roles.contains(BlockListModel::TagTintRole);
        }
        QVERIFY(told);
        // The style gone: so is the tint.
        f.host.setTagStyle(u"pilne"_s, QString());
        QCOMPARE(f.role(0, "tagTint").toString(), QString());
    }
};

QTEST_MAIN(TstEditorInputRules)
#include "tst_editor_inputrules.moc"
