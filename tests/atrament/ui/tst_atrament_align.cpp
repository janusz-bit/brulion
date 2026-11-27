// Text aligned right or centred (TextStyle::align; docs/ATRAMENT.md "Text", alignment): everything
// the paragraph gives is where its glyphs are drawn — a link's hit region and its node for screen
// readers, the offsets of a TextLayoutHandle, an inline object's box, a text field's caret — in a
// wrapping text, a text that does not wrap in a wider box, and a TextInput.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <memory>
#include <string>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->setFallbackFamilies({u"Brulion Inter"});
        return f;
    }();
    return fonts;
}

// "Idź do Głównej" and two no-break spaces an object stands on; the link is "Głównej".
constexpr std::u16string_view kText = u"Idź do Głównej  ";
constexpr Range kLink{7, 14};
constexpr Range kObject{14, 16};
constexpr float kObjectWidth = 20;
constexpr float kRowLeft = 10;
constexpr float kRowWidth = 400;

struct Para {
    TextAlign align = TextAlign::Right;
    bool nowrap = false;
    float width = kRowWidth;
    int link = 0;
    int row = 0;
    Rect object; // what the object's painter was told
    TextLayoutHandle handle;
    AnyElement render(Window &, Context<Para> &cx)
    {
        App *app = &cx.app();
        const EntityId id = cx.entityId();
        auto bump = [app, id](int Para::*field) {
            return [app, id, field](const ClickEvent &, Window &, App &) {
                app->updateEntity<Para>(id, [field](Para &p, Context<Para> &c) {
                    ++(p.*field);
                    c.notify();
                });
            };
        };
        Para *self = this; // (the entity outlives the frame)
        // (A column: the text as wide as the row, as a block's text is.)
        Div box = div().id(u"row")
                          .flexCol()
                          .w(width)
                          .textSize(16)
                          .textColor(black())
                          .fontFamily(u"Brulion Inter")
                          .onClick(bump(&Para::row));
        if (align == TextAlign::Right)
            std::move(box).textRight();
        else if (align == TextAlign::Center)
            std::move(box).textCenter();
        if (nowrap)
            std::move(box).whitespaceNowrap();
        return div().sizeFull().bg(white()).p(kRowLeft).child(
                std::move(box).child(text(kText)
                                             .link(kLink, bump(&Para::link))
                                             .inlineObject(kObject, InlineObjectSize{kObjectWidth, 12, 2},
                                                           [self](const InlineObjectPaint &p, Window &) { self->object = p.bounds; })
                                             .trackLayout(handle)));
    }
};

struct Field {
    Entity<TextInput> input;
    explicit Field(Context<Field> &cx)
        : input(cx.newEntity<TextInput>(TextInputOptions{u"", false, InputHandler::Purpose::Text, u"Zażółć", u""}))
    { }
    AnyElement render(Window &, Context<Field> &)
    {
        return div().sizeFull().bg(white()).p(kRowLeft).child(
                div().w(kRowWidth).textSize(16).textColor(black()).fontFamily(u"Brulion Inter").textRight().child(input));
    }
};

const AccessibilityNode *linkNode(const AccessibilityTree &tree)
{
    for (const AccessibilityNode *n : tree.nodes())
        if (n->role == Role::Link)
            return n;
    return nullptr;
}

// The columns with ink (dark pixels) between y0 and y1: [first, last], or {-1, -1}.
std::pair<int, int> inkColumns(const TestWindow &tw, int y0, int y1)
{
    int first = -1, last = -1;
    for (int x = 0; x < 500; ++x)
        for (int y = y0; y < y1; ++y)
            if (tw.pixel(x, y).r < 140) {
                if (first < 0)
                    first = x;
                last = x;
                break;
            }
    return {first, last};
}

} // namespace

class TestAtramentAlign : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void aligned_data();
    void aligned();
    void wrappedLinesEachAligned();
    void textInputAligned();
};

void TestAtramentAlign::aligned_data()
{
    QTest::addColumn<int>("align");
    QTest::addColumn<bool>("nowrap");
    QTest::newRow("right") << int(TextAlign::Right) << false;
    QTest::newRow("centre") << int(TextAlign::Center) << false;
    QTest::newRow("right, no wrapping") << int(TextAlign::Right) << true;
    QTest::newRow("centre, no wrapping") << int(TextAlign::Center) << true;
}

void TestAtramentAlign::aligned()
{
    QFETCH(int, align);
    QFETCH(bool, nowrap);
    auto platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw = platform->newWindow({500, 80}, 1);
    Entity<Para> para = app.newEntity<Para>();
    para.update(app, [&](Para &p, Context<Para> &) {
        p.align = TextAlign(align);
        p.nowrap = nowrap;
    });
    Window &w = app.openWindow(tw, para);
    w.setAccessibilityEnabled(true);
    tw.settle();

    // Where the glyphs are: the object's box right after them, at the row's right edge (or the
    // whole line in its middle).
    const Rect object = para.read(app).object;
    QVERIFY(!object.isEmpty());
    const auto [inkLeft, inkRight] = inkColumns(tw, 10, 40);
    QVERIFY(inkLeft >= 0);
    QVERIFY2(std::abs(object.left() - float(inkRight)) < 4,
             qPrintable(QStringLiteral("object %1, ink to %2").arg(object.left()).arg(inkRight)));
    if (TextAlign(align) == TextAlign::Right) {
        QVERIFY2(std::abs(object.right() - (kRowLeft + kRowWidth)) < 1, qPrintable(QString::number(object.right())));
    } else {
        const float middle = (float(inkLeft) + object.right()) / 2;
        QVERIFY2(std::abs(middle - (kRowLeft + kRowWidth / 2)) < 2, qPrintable(QString::number(middle)));
    }

    // The layout handle's offsets and carets are the drawn ones.
    const TextLayoutHandle &handle = para.read(app).handle;
    QVERIFY(handle.layout());
    const float y = handle.bounds().center().y;
    QCOMPARE(handle.offsetAt({float(inkLeft), y}).value_or(-1), 0);
    QCOMPARE(handle.offsetAt({float(inkRight) + 1, y}).value_or(-1), kLink.end);
    const float caret0 = handle.bounds().left() + handle.layout()->caretRect(0).left();
    QVERIFY2(std::abs(caret0 - float(inkLeft)) < 3, qPrintable(QStringLiteral("%1 vs %2").arg(caret0).arg(inkLeft)));
    const float caretObject = handle.bounds().left() + handle.layout()->caretX(kObject.start);
    QVERIFY2(std::abs(caretObject - object.left()) < 0.5f, qPrintable(QString::number(caretObject)));

    // The link: its node over its glyphs, a click on its last letter runs it, not the row's click.
    const AccessibilityNode *link = linkNode(w.accessibilityTree());
    QVERIFY(link);
    QVERIFY2(std::abs(link->bounds.right() - object.left()) < 0.5f,
             qPrintable(QStringLiteral("%1 vs %2").arg(link->bounds.right()).arg(object.left())));
    tw.simulateClick({float(inkRight) - 3, y});
    QCOMPARE(para.read(app).link, 1);
    QCOMPARE(para.read(app).row, 0);
    // Before the line's glyphs: the row's.
    tw.simulateClick({float(inkLeft) - 4, y});
    QCOMPARE(para.read(app).row, 1);
    QCOMPARE(para.read(app).link, 1);
}

void TestAtramentAlign::wrappedLinesEachAligned()
{
    // Two lines ("Idź do" / "Głównej  "), each at the right edge of a narrow row.
    auto platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw = platform->newWindow({500, 100}, 1);
    Entity<Para> para = app.newEntity<Para>();
    para.update(app, [](Para &p, Context<Para> &) { p.width = 110; });
    app.openWindow(tw, para);
    tw.settle();
    const TextLayoutHandle &handle = para.read(app).handle;
    QVERIFY(handle.layout());
    QCOMPARE(int(handle.layout()->lines().size()), 2);
    const TextLine &first = handle.layout()->lines()[0];
    const TextLine &second = handle.layout()->lines()[1];
    const float right = kRowLeft + 110;
    const auto [firstLeft, firstRight] = inkColumns(tw, int(handle.bounds().top() + first.top),
                                                    int(handle.bounds().top() + first.top + first.height));
    QVERIFY2(std::abs(float(firstRight) - right) < 3, qPrintable(QString::number(firstRight)));
    QVERIFY(firstLeft > int(kRowLeft) + 20);
    // The second line ends with the object at the edge.
    QVERIFY2(std::abs(para.read(app).object.right() - right) < 1, qPrintable(QString::number(para.read(app).object.right())));
    QVERIFY(para.read(app).object.top() >= handle.bounds().top() + second.top - 0.5f);
    // ↑ from before the object lands on the first line under the same x, where it is drawn.
    const float x = handle.layout()->caretX(kObject.start);
    const int above = handle.layout()->offsetAbove(kObject.start, x);
    QVERIFY(above >= 0 && above < first.end);
    const float aboveX = handle.bounds().left() + handle.layout()->caretX(above);
    QVERIFY2(aboveX >= float(firstLeft) - 2 && aboveX <= float(firstRight) + 2,
             qPrintable(QStringLiteral("%1 in %2…%3").arg(aboveX).arg(firstLeft).arg(firstRight)));
    QVERIFY2(std::abs(aboveX - (handle.bounds().left() + x)) < 8, qPrintable(QString::number(aboveX)));
}

void TestAtramentAlign::textInputAligned()
{
    auto platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw = platform->newWindow({500, 80}, 1);
    Entity<Field> field = app.newEntity<Field>();
    Window &w = app.openWindow(tw, field);
    tw.settle();
    const Entity<TextInput> input = field.read(app).input;
    input.read(app).focusHandle().focus(w);
    tw.settle();
    const TextInput &t = input.read(app);
    QVERIFY(t.layout() && t.bounds());
    const int length = int(t.text().size());
    const float caretEnd = t.bounds()->left() + t.layout()->caretX(length);
    QVERIFY2(std::abs(caretEnd - (kRowLeft + kRowWidth)) < 1, qPrintable(QString::number(caretEnd)));
    const auto [inkLeft, inkRight] = inkColumns(tw, 10, 40);
    QVERIFY2(std::abs(float(inkRight) - (kRowLeft + kRowWidth)) < 4, qPrintable(QString::number(inkRight)));
    // A click on the first letter puts the caret before it.
    tw.simulateClick({float(inkLeft) + 1, t.bounds()->center().y});
    tw.settle();
    QCOMPARE(input.read(app).selection().focus, 0);
}

QTEST_GUILESS_MAIN(TestAtramentAlign)
#include "tst_atrament_align.moc"
