// Spell marks in the block editor drawn by Atrament (src/app/editor/atrament/pagespell.cpp;
// docs/ATRAMENT.md "The block editor in Atrament (M2)", step 2; docs/UI.md "Spell checking"): a
// page of a PageController over FakeEditorHost on Atrament's test platform with a SpellCheck over
// word lists (spell::WordListBackend: the same verdicts on every machine) — misspelled words of
// the edited text underlined with a wavy line once their verdicts come, the word being typed left
// alone until the caret leaves it, not in code; the drawn rows' words with SpellCheck.wholePage, each
// only where it is shown as written; the spelling's menu on a right-click (a tap on touch UIs) with
// the word's range, a correction through the controller.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "spellcheck.h"
#include "text/fonts.h"
#include "ui/testplatform.h"

#include "spell/spellchecker.h"

#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Pisownia.md"_s;
const QStringList Polish{u"kot"_s, u"pies"_s, u"zażółć"_s, u"gęślą"_s, u"jaźń"_s, u"ma"_s, u"np."_s, u"ala"_s, u"łódź"_s};
const QStringList English{u"cat"_s, u"dog"_s, u"meeting"_s, u"the"_s};
const at::Color SpellRed = at::rgb(0xff0000);

std::shared_ptr<at::FontCollection> loadFonts()
{
    static std::shared_ptr<at::FontCollection> fonts = [] {
        auto f = std::make_shared<at::FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        for (const char *file : {"ui/BrulionInter-Regular.ttf", "ui/BrulionInter-Bold.ttf", "BrulionEmoji.ttf"})
            f->addFontFile(dir + file);
        f->setFallbackFamilies({u"Brulion Emoji", u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

std::unique_ptr<SpellCheck> makeSpell()
{
    auto factory = [](const QString &language) -> std::unique_ptr<spell::SpellBackend> {
        if (language == u"pl")
            return std::make_unique<spell::WordListBackend>(u"pl"_s, Polish);
        if (language == u"en")
            return std::make_unique<spell::WordListBackend>(u"en"_s, English);
        return nullptr;
    };
    auto spell = std::make_unique<SpellCheck>(QStringList{u"pl_PL"_s, u"en_US"_s}, factory, u"test"_s);
    spell->setSystemLanguage(u"pl_PL"_s);
    spell->setUiLanguage(u"pl"_s);
    spell->setLanguageSetting(u"auto"_s);
    spell->setWholePage(true);
    return spell;
}

QString str(std::u16string_view s)
{
    return QString(reinterpret_cast<const QChar *>(s.data()), qsizetype(s.size()));
}

// The ranges a set of spans underlines with a wavy line.
QList<std::pair<int, int>> wavy(const std::vector<at::TextSpan> &spans)
{
    QList<std::pair<int, int>> out;
    for (const at::TextSpan &s : spans) {
        if (s.style.underline && *s.style.underline && (*s.style.underline)->wavy)
            out.append({s.range.start, s.range.end});
    }
    return out;
}

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::unique_ptr<SpellCheck> spell = makeSpell();
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;
    QList<PageView::Misspelling> menus;
    QList<at::Point> menuPoints;
    int blockMenus = 0;

    explicit Harness(const QString &content, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            v.setEditStyles(std::make_shared<const EditStyler>(QVariantMap{}, std::u16string(u"Brulion Inter")), cx);
            v.setSpellCheck(spell.get(), SpellRed, cx);
            v.onSpellingMenu = [this](const PageView::Misspelling &m, at::Point p) {
                menus.append(m);
                menuPoints.append(p);
            };
            v.onBlockMenu = [this](int, at::Point) { ++blockMenus; };
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    BlockListModel *model() const { return controller.blocks(); }
    const PageView &view() { return page.read(app); }
    QString inputText() { return view().input() ? str(view().input()->read(app).text()) : QString(); }
    // The checker's answers in, the page styled and drawn again.
    void settleSpelling()
    {
        QVERIFY(spell->waitUntilIdle());
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
        tw.settle();
    }
    // The edited text's wavy ranges (the field's spans as it draws them now).
    QList<std::pair<int, int>> editMarks()
    {
        tw.settle();
        const auto *input = view().input();
        return input ? wavy(input->read(app).spans()) : QList<std::pair<int, int>>{};
    }
    at::Point pointOf(int row, int offset)
    {
        const at::TextLayoutHandle *t = view().rowText(row);
        if (!t || !t->layout())
            return {-1, -1};
        const at::Rect caret = t->layout()->caretRect(offset).translated(t->bounds().origin);
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    // The middle of the caret before offset `offset` of the edited text.
    at::Point editPointOf(int offset)
    {
        const at::TextInput &input = view().input()->read(app);
        const at::Rect caret = input.boundsForRange(at::Range{offset, offset}).value_or(at::Rect{});
        return {caret.left() + 0.5f, caret.top() + caret.height() / 2};
    }
    void clickInto(int row, int offset)
    {
        const at::Point p = pointOf(row, offset);
        QVERIFY2(p.x >= 0, "the row is drawn");
        tw.simulateClick(p);
        tw.settle();
    }
};

} // namespace

class TestAtramentSpell : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void editedTextMarked();
    void wordBeingTypedWaits();
    void drawnRowsMarkedWhereShownAsWritten();
    void wholePageOffMarksOnlyTheEditedText();
    void codeIsNotChecked();
    void rightClickOpensTheSpellingMenu();
    void tapOnTouchOpensTheSpellingMenu();
    void correctionAndDictionary();
    void textMenuCarriesTheMisspelledWord();
};

void TestAtramentSpell::editedTextMarked()
{
    Harness h(u"- kot ma zażułć pies\n- jaźń\n"_s);
    h.clickInto(0, 3);
    QCOMPARE(h.controller.editingRow(), 0);
    // The verdicts come from the checker's thread: nothing marked until they are in.
    h.settleSpelling();
    const QString text = h.inputText();
    const int from = int(text.indexOf(u"zażułć"_s));
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{from, from + 6}}));
    // Drawn with a red wavy line under the word.
    const at::TextInput &input = h.view().input()->read(h.app);
    const at::Rect word = input.boundsForRange(at::Range{from, from + 6}).value_or(at::Rect{});
    bool red = false;
    for (int y = int(word.top() + word.height() / 2); y < int(word.bottom()) + 2 && !red; ++y)
        for (int x = int(word.left()); x < int(word.right()) && !red; ++x) {
            const auto p = h.tw.pixel(x, y);
            red = p.r > 150 && p.g < 90 && p.b < 90;
        }
    QVERIFY(red);
    // Typing elsewhere keeps it; mending it takes the mark away.
    h.tw.simulateKeystrokes(u"end");
    h.tw.simulateTyping(u" łódź");
    h.settleSpelling();
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{from, from + 6}}));
}

void TestAtramentSpell::wordBeingTypedWaits()
{
    Harness h(u"- kot\n"_s);
    h.clickInto(0, 3);
    h.tw.simulateTyping(u" gęśly");
    h.settleSpelling();
    // The caret right after the wrong word: it is still being typed.
    QVERIFY(h.editMarks().isEmpty());
    h.tw.simulateTyping(u" ");
    h.settleSpelling();
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{4, 9}}));
    // The caret back at its end (an arrow): unmarked again, as MarkdownHighlighter does; inside it
    // marked.
    h.tw.simulateKeystrokes(u"left");
    QVERIFY(h.editMarks().isEmpty());
    h.tw.simulateKeystrokes(u"left");
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{4, 9}}));
}

void TestAtramentSpell::drawnRowsMarkedWhereShownAsWritten()
{
    // A wrong word as written (also in bold) and inside a link's text (shown as a whole: left
    // alone, as spellmarks.h does).
    Harness h(u"- kot zażułć ma\n- **zażułć** i [zażułć](https://example.org)\n"_s);
    QVERIFY(h.view().spellMarks(0).empty()); // asked: pending
    h.settleSpelling();
    const auto marks0 = h.view().spellMarks(0);
    QCOMPARE(marks0.size(), std::size_t(1));
    const at::TextLayoutHandle *t0 = h.view().rowText(0);
    QVERIFY(t0 && t0->layout());
    const QString shown0 = str(t0->layout()->text());
    QCOMPARE(marks0[0].start, int(shown0.indexOf(u"zażułć"_s)));
    QCOMPARE(marks0[0].end, marks0[0].start + 6);
    const auto marks1 = h.view().spellMarks(1);
    QCOMPARE(marks1.size(), std::size_t(1));
    const QString shown1 = str(h.view().rowText(1)->layout()->text());
    QCOMPARE(marks1[0].start, int(shown1.indexOf(u"zażułć"_s))); // the bold one, the first
    // Drawn: red under the word of row 0.
    const at::Rect word = t0->layout()->selectionRects(marks0[0]).front().translated(t0->bounds().origin);
    bool red = false;
    for (int y = int(word.top() + word.height() / 2); y < int(word.bottom()) + 2 && !red; ++y)
        for (int x = int(word.left()); x < int(word.right()) && !red; ++x) {
            const auto p = h.tw.pixel(x, y);
            red = p.r > 150 && p.g < 90 && p.b < 90;
        }
    QVERIFY(red);
}

void TestAtramentSpell::wholePageOffMarksOnlyTheEditedText()
{
    Harness h(u"- zażułć\n- zażułć kot\n"_s);
    h.spell->setWholePage(false);
    h.tw.settle();
    h.settleSpelling();
    QVERIFY(h.view().spellMarks(0).empty());
    h.clickInto(1, 9);
    h.settleSpelling();
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{0, 6}}));
    QVERIFY(h.view().spellMarks(0).empty());
    // Spell checking off: no marks at all.
    h.spell->setEnabled(false);
    QCoreApplication::processEvents();
    QVERIFY(h.editMarks().isEmpty());
}

void TestAtramentSpell::codeIsNotChecked()
{
    Harness h(u"```\nzażułć gęśly\n```\n- kot\n"_s);
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) { v.setDrawsCode(true, PageView::CodeLook{}, cx); });
    h.tw.settle();
    h.controller.startEditing(0, 2);
    h.tw.settle();
    QCOMPARE(h.view().editedRow(), 0);
    h.settleSpelling();
    QVERIFY(h.editMarks().isEmpty());
    QVERIFY(h.view().spellMarks(0).empty());
}

void TestAtramentSpell::rightClickOpensTheSpellingMenu()
{
    Harness h(u"- kot zażułć\n- ma zażułć\n"_s);
    h.settleSpelling();
    // A drawn row's word: its range in the block's body.
    const QString shown = str(h.view().rowText(1)->layout()->text());
    const int at = int(shown.indexOf(u"zażułć"_s));
    h.tw.simulateClick(h.pointOf(1, at + 2), at::MouseButton::Right);
    QCOMPARE(h.menus.size(), 1);
    QCOMPARE(h.menus[0].row, 1);
    QVERIFY(!h.menus[0].edited);
    QCOMPARE(h.menus[0].word, u"zażułć"_s);
    QCOMPARE(h.menus[0].text, u"zażułć"_s);
    const QString body = h.model()->document().at(h.model()->document().indexOfUid(h.model()->uidAt(1))).body;
    QCOMPARE(body.mid(h.menus[0].from, h.menus[0].to - h.menus[0].from), u"zażułć"_s);
    QCOMPARE(h.blockMenus, 0);
    // Elsewhere on a row: the block menu.
    h.tw.simulateClick(h.pointOf(1, 1), at::MouseButton::Right);
    QCOMPARE(h.blockMenus, 1);
    QCOMPARE(h.menus.size(), 1);
    // The edited text's word: its range in the editText.
    h.clickInto(0, 1);
    h.settleSpelling();
    h.tw.simulateClick(h.editPointOf(7), at::MouseButton::Right);
    QCOMPARE(h.menus.size(), 2);
    QCOMPARE(h.menus[1].row, 0);
    QVERIFY(h.menus[1].edited);
    QCOMPARE(h.menus[1].from, 4);
    QCOMPARE(h.menus[1].to, 10);
    QCOMPARE(h.menus[1].word, u"zażułć"_s);
    // (The right-click moved no caret.)
    QCOMPARE(h.view().input()->read(h.app).selection().focus, 1);
}

void TestAtramentSpell::tapOnTouchOpensTheSpellingMenu()
{
    Harness h(u"- kot zażułć\n"_s);
    h.page.update(h.app, [](PageView &v, at::Context<PageView> &cx) {
        PageMetrics m = v.metrics();
        m.touch = true;
        v.setStyle(m, v.palette());
        cx.notify();
    });
    h.tw.settle();
    h.clickInto(0, 1);
    h.settleSpelling();
    QVERIFY(h.menus.isEmpty());
    // A finger's tap on the marked word: the caret goes there and its corrections open.
    h.tw.simulateTap(h.editPointOf(6));
    h.tw.advance(std::chrono::milliseconds(200));
    QCOMPARE(h.menus.size(), 1);
    QVERIFY(h.menus[0].edited);
    QCOMPARE(h.menus[0].from, 4);
    QCOMPARE(h.view().input()->read(h.app).selection().focus, 6);
    // A tap on a word spelled right: nothing.
    h.tw.simulateTap(h.editPointOf(1));
    h.tw.advance(std::chrono::milliseconds(200));
    QCOMPARE(h.menus.size(), 1);
}

void TestAtramentSpell::correctionAndDictionary()
{
    Harness h(u"- kot zażułć gęśly\n"_s);
    h.clickInto(0, 1);
    h.settleSpelling();
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{4, 10}, {11, 16}}));
    // A correction (BlockEditor.replaceMisspelledRange: PageController.replaceRange): the field
    // shows it, one mark left.
    h.controller.replaceRange(0, 4, 10, u"zażółć"_s);
    h.settleSpelling();
    QCOMPARE(h.inputText(), u"kot zażółć gęśly"_s);
    QCOMPARE(h.editMarks(), (QList<std::pair<int, int>>{{11, 16}}));
    // Added to the dictionary: asked again, unmarked once the answer comes.
    h.spell->addToDictionary(u"gęśly"_s);
    h.settleSpelling();
    QVERIFY(h.editMarks().isEmpty());
}

void TestAtramentSpell::textMenuCarriesTheMisspelledWord()
{
    // With the edited text's own menu (InlineEditor's right-click): on a misspelled word it is that
    // menu, the word's corrections first — not the spelling's alone; elsewhere in the text too.
    Harness h(u"- kot zażułć\n"_s);
    QList<std::optional<PageView::Misspelling>> textMenus;
    h.page.update(h.app, [&](PageView &v, at::Context<PageView> &) {
        v.onTextMenu = [&](int, const std::optional<PageView::Misspelling> &spelling, at::Point) { textMenus.append(spelling); };
    });
    h.clickInto(0, 1);
    h.settleSpelling();
    h.tw.simulateClick(h.editPointOf(7), at::MouseButton::Right);
    QCOMPARE(textMenus.size(), 1);
    QVERIFY(textMenus[0]);
    QCOMPARE(textMenus[0]->from, 4);
    QCOMPARE(textMenus[0]->to, 10);
    QCOMPARE(textMenus[0]->word, u"zażułć"_s);
    QVERIFY(textMenus[0]->edited);
    QVERIFY(h.menus.isEmpty());
    QCOMPARE(h.blockMenus, 0);
    h.tw.simulateClick(h.editPointOf(1), at::MouseButton::Right);
    QCOMPARE(textMenus.size(), 2);
    QVERIFY(!textMenus[1]);
    QVERIFY(h.menus.isEmpty());
}

QTEST_MAIN(TestAtramentSpell)
#include "tst_atrament_spell.moc"
