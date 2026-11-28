// Links in text (Text::link, docs/ATRAMENT.md M2 "Hit regions in text"): a range of a paragraph
// clicked with the mouse or tapped runs its listener and not the click of the element around it,
// the pointing hand over it, the underline while hovered, a link across a line break, link nodes
// for screen readers (named by their text, activated through the tree), a link told when the
// pointer comes onto it and leaves it (a preview's cue).
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <cmath>
#include <memory>
#include <string>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;

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

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    explicit Harness(Size size) : tw(platform->newWindow(size, 1)) { }
};

// "Idź do strony Główna albo zobacz Inną stronę" — links on "Główna" and "Inną stronę".
constexpr std::u16string_view kText = u"Idź do strony Główna albo zobacz Inną stronę";

struct Para {
    int main = 0;
    int other = 0;
    int row = 0;
    float width = 400;
    AnyElement render(Window &, Context<Para> &cx)
    {
        Para *self = this;
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
        (void)self;
        return div().sizeFull().bg(white()).p(10).child(div().id(u"row")
                                                                .w(width)
                                                                .textSize(16)
                                                                .textColor(black())
                                                                .fontFamily(u"Brulion Inter")
                                                                .onClick(bump(&Para::row))
                                                                .child(text(kText)
                                                                               .id(u"para")
                                                                               .link({14, 20}, bump(&Para::main))
                                                                               .link({33, 44}, bump(&Para::other))));
    }
};

// A link with a menu of its own (a chip's, a tag's) in a row with one too: a right-click or a
// finger held on the link opens the link's, beside it the row's.
struct Menus {
    int linkMenu = 0;
    int rowMenu = 0;
    int rowHeld = 0;
    Point at;
    AnyElement render(Window &, Context<Menus> &cx)
    {
        App *app = &cx.app();
        const EntityId id = cx.entityId();
        auto bump = [app, id](int Menus::*field) {
            app->updateEntity<Menus>(id, [field](Menus &m, Context<Menus> &c) {
                ++(m.*field);
                c.notify();
            });
        };
        return div().sizeFull().bg(white()).p(10).child(
                div().id(u"row")
                        .w(400)
                        .textSize(16)
                        .textColor(black())
                        .fontFamily(u"Brulion Inter")
                        .onMouseDown(MouseButton::Right, [bump](const MouseDownEvent &, Window &, App &) { bump(&Menus::rowMenu); })
                        .onLongPress([bump](Point, Window &, App &) { bump(&Menus::rowHeld); })
                        .child(text(kText).link({14, 20}, [](const ClickEvent &, Window &, App &) {},
                                                [app, id](Point p, Window &, App &) {
                                                    app->updateEntity<Menus>(id, [p](Menus &m, Context<Menus> &c) {
                                                        ++m.linkMenu;
                                                        m.at = p;
                                                        c.notify();
                                                    });
                                                })));
    }
};

// Two links that log the pointer coming onto them ("Główna on 12,30") and leaving them.
struct Hovers {
    QStringList log;
    AnyElement render(Window &, Context<Hovers> &cx)
    {
        App *app = &cx.app();
        const EntityId id = cx.entityId();
        auto hover = [app, id](const char *name) {
            return [app, id, name](bool on, Point p, Window &, App &) {
                app->updateEntity<Hovers>(id, [&](Hovers &h, Context<Hovers> &) {
                    h.log.append(on ? QStringLiteral("%1 on %2,%3").arg(QString::fromUtf8(name)).arg(p.x).arg(p.y)
                                    : QStringLiteral("%1 off").arg(QString::fromUtf8(name)));
                });
            };
        };
        auto none = [](const ClickEvent &, Window &, App &) {};
        return div().sizeFull().bg(white()).p(10).child(div().w(400)
                                                                .textSize(16)
                                                                .textColor(black())
                                                                .fontFamily(u"Brulion Inter")
                                                                .child(text(kText)
                                                                               .link({14, 20}, none, {}, hover("Główna"))
                                                                               .link({33, 44}, none, {}, hover("Inną"))));
    }
};

const AccessibilityNode *linkNode(const AccessibilityTree &tree, std::u16string_view name)
{
    for (const AccessibilityNode *n : tree.nodes())
        if (n->role == Role::Link && n->name == name)
            return n;
    return nullptr;
}

Point centre(Rect r)
{
    return {r.left() + r.width() / 2, r.top() + r.height() / 2};
}

} // namespace

class TestAtramentLinks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void clickHoverAndAccessibility();
    void linkAcrossALineBreak();
    void linkMenuBeforeTheRows();
    void hoverComesAndGoes();
    void keysAndOtherActionsLeaveLinksAlone();
};

void TestAtramentLinks::clickHoverAndAccessibility()
{
    Harness h({500, 120});
    Entity<Para> para = h.app.newEntity<Para>();
    Window &w = h.app.openWindow(h.tw, para);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityNode *main = linkNode(w.accessibilityTree(), u"Główna");
    const AccessibilityNode *other = linkNode(w.accessibilityTree(), u"Inną stronę");
    QVERIFY(main);
    QVERIFY(other);
    QVERIFY(main->actions & actionBit(AccessibilityAction::Click));
    QVERIFY(main->bounds.left() < other->bounds.left());
    const Point onMain = centre(main->bounds);
    const Point onOther = centre(other->bounds);

    // The pointing hand over a link, the arrow beside it.
    h.tw.simulateMouseMove(onMain);
    QCOMPARE(h.tw.cursor(), CursorStyle::PointingHand);
    h.tw.simulateMouseMove({15, onMain.y});
    QVERIFY(h.tw.cursor() != CursorStyle::PointingHand);

    // A click on a link: its listener, not the row's.
    h.tw.simulateClick(onMain);
    QCOMPARE(para.read(h.app).main, 1);
    QCOMPARE(para.read(h.app).row, 0);
    h.tw.simulateClick(onOther);
    QCOMPARE(para.read(h.app).other, 1);
    // Beside the links: the row's click.
    h.tw.simulateClick({15, onMain.y});
    QCOMPARE(para.read(h.app).row, 1);
    QCOMPARE(para.read(h.app).main, 1);
    // Pressed on one link, released on the other: nothing.
    h.tw.simulateMouseDown(onMain);
    h.tw.simulateMouseUp(onOther);
    QCOMPARE(para.read(h.app).main, 1);
    QCOMPARE(para.read(h.app).other, 1);
    // A finger's tap.
    h.tw.simulateTap(onOther);
    h.tw.settle();
    QCOMPARE(para.read(h.app).other, 2);
    // A screen reader activates it.
    QVERIFY(w.performAccessibilityAction(linkNode(w.accessibilityTree(), u"Główna")->id, AccessibilityAction::Click));
    QCOMPARE(para.read(h.app).main, 2);

    // Hovered: underlined (ink under the word that is not there otherwise).
    auto inkBelow = [&](Rect r) {
        int dark = 0;
        for (int y = int(r.bottom()) - 6; y < int(r.bottom()); ++y)
            for (int x = int(r.left()) + 2; x < int(r.right()) - 2; ++x)
                dark += h.tw.pixel(x, y).r < 140 ? 1 : 0;
        return dark;
    };
    h.tw.simulateMouseMove({15, 100});
    h.tw.settle();
    const Rect box = linkNode(w.accessibilityTree(), u"Główna")->bounds;
    const int plain = inkBelow(box);
    h.tw.simulateMouseMove(onMain);
    h.tw.settle();
    const int hovered = inkBelow(box);
    QVERIFY2(hovered > plain + int(box.width() / 2), qPrintable(QStringLiteral("%1 → %2").arg(plain).arg(hovered)));
}

void TestAtramentLinks::linkAcrossALineBreak()
{
    // So narrow that "Inną stronę" breaks over two lines: both parts click.
    Harness h({200, 200});
    Entity<Para> para = h.app.newEntity<Para>();
    para.update(h.app, [](Para &p, Context<Para> &) { p.width = 70; });
    Window &w = h.app.openWindow(h.tw, para);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityNode *other = linkNode(w.accessibilityTree(), u"Inną stronę");
    QVERIFY(other);
    QVERIFY2(other->bounds.height() > 30, "on two lines");
    // The bottom line's start and the top line's start (left edge + a little).
    h.tw.simulateClick({other->bounds.left() + 4, other->bounds.bottom() - 4});
    QCOMPARE(para.read(h.app).other, 1);
}

void TestAtramentLinks::linkMenuBeforeTheRows()
{
    using namespace std::chrono_literals;
    Harness h({500, 120});
    Entity<Menus> menus = h.app.newEntity<Menus>();
    Window &w = h.app.openWindow(h.tw, menus);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityNode *main = linkNode(w.accessibilityTree(), u"Główna");
    QVERIFY(main);
    const Point onMain = centre(main->bounds);
    const Point beside{15, onMain.y};
    h.tw.simulateClick(onMain, MouseButton::Right);
    QCOMPARE(menus.read(h.app).linkMenu, 1);
    QCOMPARE(menus.read(h.app).rowMenu, 0);
    QCOMPARE(menus.read(h.app).at.x, onMain.x);
    h.tw.simulateClick(beside, MouseButton::Right);
    QCOMPARE(menus.read(h.app).rowMenu, 1);
    QCOMPARE(menus.read(h.app).linkMenu, 1);
    // A finger held: the link's menu, not the row's long press.
    h.tw.simulateTouchDown(onMain);
    h.tw.advance(600ms);
    h.tw.simulateTouchUp(onMain);
    QCOMPARE(menus.read(h.app).linkMenu, 2);
    QCOMPARE(menus.read(h.app).rowHeld, 0);
    h.tw.simulateTouchDown(beside);
    h.tw.advance(600ms);
    h.tw.simulateTouchUp(beside);
    QCOMPARE(menus.read(h.app).rowHeld, 1);
    QCOMPARE(menus.read(h.app).linkMenu, 2);
}

// The pointer onto a link: its hover listener with the pointer's place; moving along it, nothing
// more; onto the text beside it, onto the other link (the link it came onto told first, then the
// one it left: who keeps the hovered link ignores a leave of another), out of the window: its
// leave. A finger hovers nothing.
void TestAtramentLinks::hoverComesAndGoes()
{
    Harness h({500, 120});
    Entity<Hovers> hovers = h.app.newEntity<Hovers>();
    Window &w = h.app.openWindow(h.tw, hovers);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const Rect main = linkNode(w.accessibilityTree(), u"Główna")->bounds;
    const Rect other = linkNode(w.accessibilityTree(), u"Inną stronę")->bounds;
    const Point onMain{std::floor(main.left()) + 3, std::floor(centre(main).y)};
    const Point onOther{std::floor(other.left()) + 3, onMain.y};
    auto log = [&] { return hovers.read(h.app).log; };
    auto at = [](Point p) { return QStringLiteral("%1,%2").arg(p.x).arg(p.y); };

    h.tw.simulateMouseMove({15, onMain.y});
    QCOMPARE(log(), QStringList());
    h.tw.simulateMouseMove(onMain);
    QCOMPARE(log(), QStringList{u"Główna on "_s + at(onMain)});
    h.tw.simulateMouseMove({onMain.x + 2, onMain.y});
    QCOMPARE(log().size(), 1);
    h.tw.simulateMouseMove({15, onMain.y});
    QCOMPARE(log().last(), u"Główna off"_s);
    h.tw.simulateMouseMove(onOther);
    QCOMPARE(log().last(), u"Inną on "_s + at(onOther));
    h.tw.simulateMouseMove(onMain);
    QCOMPARE(log().mid(log().size() - 2), (QStringList{u"Główna on "_s + at(onMain), u"Inną off"_s}));
    h.tw.simulateMouseExit();
    QCOMPARE(log().last(), u"Główna off"_s);
    const qsizetype before = log().size();
    h.tw.simulateTap(onOther);
    h.tw.settle();
    QCOMPARE(log().size(), before);
}

// A key pressed while the pointer rests on a link tells its hover nothing more; a screen reader's
// action other than a click on a link does nothing; a link in a part hidden from screen readers is
// no node.
void TestAtramentLinks::keysAndOtherActionsLeaveLinksAlone()
{
    Harness h({500, 120});
    Entity<Hovers> hovers = h.app.newEntity<Hovers>();
    Window &w = h.app.openWindow(h.tw, hovers);
    w.setAccessibilityEnabled(true);
    h.tw.settle();
    const AccessibilityNode *main = linkNode(w.accessibilityTree(), u"Główna");
    QVERIFY(main);
    h.tw.simulateMouseMove(centre(main->bounds));
    QCOMPARE(hovers.read(h.app).log.size(), 1);
    h.tw.simulateKeystrokes(u"a");
    h.tw.settle();
    QCOMPARE(hovers.read(h.app).log.size(), 1);
    QVERIFY(!w.performAccessibilityAction(linkNode(w.accessibilityTree(), u"Główna")->id, AccessibilityAction::Focus));

    Harness hidden({500, 120});
    struct Hidden {
        AnyElement render(Window &, Context<Hidden> &)
        {
            return div().sizeFull().p(10).child(div().ariaHidden().w(400).textSize(16).fontFamily(u"Brulion Inter").child(
                    text(kText).link({14, 20}, [](const ClickEvent &, Window &, App &) {})));
        }
    };
    Entity<Hidden> part = hidden.app.newEntity<Hidden>();
    Window &hw = hidden.app.openWindow(hidden.tw, part);
    hw.setAccessibilityEnabled(true);
    hidden.tw.settle();
    QVERIFY(!linkNode(hw.accessibilityTree(), u"Główna"));
}

QTEST_GUILESS_MAIN(TestAtramentLinks)
#include "tst_atrament_links.moc"
