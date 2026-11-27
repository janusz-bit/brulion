// Code blocks' line numbers in the block editor drawn by Atrament (src/app/editor/atrament/pagecode.cpp;
// docs/ATRAMENT.md "The block editor in Atrament (M2)", step 5; CodeBlockView.qml's gutter): with
// CodeLook::lineNumbers a code block is still Atrament's (no foreign row), each line of its code is
// numbered left of it on its first visual line (a wrapped line once), the code moved right by the
// gutter — drawn and edited alike, a new line numbered as it is typed.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "editstyles.h"
#include "fakeeditorhost.h"
#include "pagecontroller.h"
#include "pageview.h"
#include "text/fonts.h"
#include "text/textlayout.h"
#include "ui/testplatform.h"

#include <QTest>

#include <memory>

using namespace brulion;
using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

const QString PagePath = u"Notatki/Kod.md"_s;

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

struct Harness {
    FakeEditorHost host;
    PageController controller;
    std::shared_ptr<at::TestPlatform> platform = std::make_shared<at::TestPlatform>();
    at::App app{platform, loadFonts()};
    at::TestWindow &tw;
    at::Entity<PageView> page;

    Harness(const QString &content, bool numbers, at::Size size = {600, 400}) : tw(platform->newWindow(size, 1))
    {
        host.setFile(PagePath, content);
        controller.setNow(u"2026-11-27 10:00"_s);
        controller.setSaveDelay(1000000); // saved by the tests only
        controller.setHost(&host);
        controller.setPath(PagePath);
        page = app.newEntity<PageView>(controller.blocks());
        page.update(app, [this, numbers](PageView &v, at::Context<PageView> &cx) {
            v.setController(&controller, cx);
            PageView::CodeLook look;
            look.label = at::rgb(0xff0000); // the numbers' (and the label's) colour: found on the frame
            look.lineNumbers = numbers;
            v.setDrawsCode(true, look, cx);
            // (The code face is the body's here: the test loads no monospace font.)
            PagePalette palette = v.palette();
            palette.codeFamily = u"Brulion Inter";
            v.setStyle(v.metrics(), palette);
        });
        app.openWindow(tw, page);
        tw.settle();
    }
    const PageView &view() { return page.read(app); }
    // The code as laid out now (drawn or edited) and where.
    const at::TextLayout *codeLayout(at::Point *origin)
    {
        if (view().editedRow() == 0 && view().input()) {
            const at::TextInput &input = view().input()->read(app);
            *origin = input.bounds().value_or(at::Rect{}).origin;
            return input.layout();
        }
        const at::TextLayoutHandle *t = view().rowText(0);
        if (!t)
            return nullptr;
        *origin = t->bounds().origin;
        return t->layout();
    }
    // Whether red ink is in the gutter (left of the code) across visual line `line` of the code.
    bool numberOn(int line)
    {
        at::Point origin;
        const at::TextLayout *layout = codeLayout(&origin);
        if (!layout || line >= int(layout->lines().size()))
            return false;
        const at::TextLine &l = layout->lines()[std::size_t(line)];
        for (int y = int(origin.y + l.top) + 1; y < int(origin.y + l.top + l.height) - 1; ++y)
            for (int x = int(origin.x) - 40; x < int(origin.x) - 2; ++x) {
                const auto p = tw.pixel(x, y);
                if (p.r > 150 && p.g < 110 && p.b < 110)
                    return true;
            }
        return false;
    }
};

} // namespace

class TestAtramentCodeLines : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void numbersLeftOfTheCode();
    void wrappedLineNumberedOnce();
    void editedCodeNumbered();
};

void TestAtramentCodeLines::numbersLeftOfTheCode()
{
    const QString content = u"```\nlet a = 1\nlet b = 2\n```\n- tekst\n"_s;
    Harness plain(content, false);
    Harness numbered(content, true);
    // Atrament's row either way (no QML delegate over it).
    QVERIFY(numbered.view().foreignRows().empty());
    QVERIFY(numbered.view().drawsRow(0));
    at::Point a;
    at::Point b;
    QVERIFY(plain.codeLayout(&a));
    QVERIFY(numbered.codeLayout(&b));
    // The code moved right by the gutter: Theme.space3, the numbers, Theme.space3 (16 without).
    QVERIFY2(b.x > a.x + 12, qPrintable(u"%1 → %2"_s.arg(a.x).arg(b.x)));
    QVERIFY(numbered.numberOn(0));
    QVERIFY(numbered.numberOn(1));
    QVERIFY(!plain.numberOn(0));
}

void TestAtramentCodeLines::wrappedLineNumberedOnce()
{
    // The first line wraps in a narrow window; the second starts below its last visual line.
    const QString content =
            u"```\nzażółć gęślą jaźń zażółć gęślą jaźń zażółć gęślą jaźń zażółć gęślą jaźń\nkoniec\n```\n"_s;
    Harness h(content, true, {280, 400});
    at::Point origin;
    const at::TextLayout *layout = h.codeLayout(&origin);
    QVERIFY(layout);
    const int lines = int(layout->lines().size());
    QVERIFY2(lines >= 3, qPrintable(QString::number(lines)));
    QVERIFY(h.numberOn(0));
    for (int l = 1; l < lines - 1; ++l)
        QVERIFY2(!h.numberOn(l), qPrintable(u"visual line %1"_s.arg(l)));
    QVERIFY(h.numberOn(lines - 1)); // "koniec": 2
}

void TestAtramentCodeLines::editedCodeNumbered()
{
    Harness h(u"```\nlet a = 1\n```\n"_s, true);
    h.controller.startEditing(0, 9);
    h.tw.settle();
    QCOMPARE(h.view().editedRow(), 0);
    QVERIFY(h.numberOn(0));
    QVERIFY(!h.numberOn(1));
    // Enter in code is a line break of its text: a second line, numbered at once.
    h.tw.simulateKeystrokes(u"enter");
    h.tw.simulateTyping(u"let b = 2");
    QCOMPARE(h.controller.blocks()->editTextAt(0), u"let a = 1\nlet b = 2"_s);
    QVERIFY(h.numberOn(1));
}

QTEST_MAIN(TestAtramentCodeLines)
#include "tst_atrament_codelines.moc"
