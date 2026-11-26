// Atrament's semantic tree (ui/accessibility.h) on the test platform: what elements say about
// themselves (roles, names from text, states, bounds), ids that stay across frames, actions of an
// assistive technology reaching the elements' listeners, text inputs, list items, scrolling, the
// events between frames, and nothing built while no assistive technology listens.
#include "text/fonts.h"
#include "ui/atrament.h"
#include "ui/testplatform.h"

#include <QtTest>

#include <memory>
#include <set>
#include <string>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        const std::string dir = BRULION_SOURCE_DIR "/assets/fonts/";
        f->addFontFile(dir + "ui/BrulionInter-Regular.ttf");
        f->addFontFile(dir + "BrulionEmoji.ttf");
        f->setFallbackFamilies({u"Brulion Emoji"});
        return f;
    }();
    return fonts;
}

std::u16string u16(const std::string &ascii)
{
    return std::u16string(ascii.begin(), ascii.end());
}

QString q(std::u16string_view s)
{
    return QString::fromUtf16(s.data(), qsizetype(s.size()));
}

struct Harness {
    std::shared_ptr<TestPlatform> platform = std::make_shared<TestPlatform>();
    App app{platform, loadFonts()};
    TestWindow &tw;
    explicit Harness(Size size = {240, 320}) : tw(platform->newWindow(size))
    {
        tw.setRasterize(false); // the tree is what is tested, not pixels
    }
    template<class T>
    Window &open(const Entity<T> &root, bool accessible = true)
    {
        Window &w = app.openWindow(tw, root);
        w.setAccessibilityEnabled(accessible);
        tw.settle();
        return w;
    }
};

const AccessibilityNode *findNode(const AccessibilityTree &tree, Role role, std::u16string_view name = {})
{
    for (const AccessibilityNode *n : tree.nodes()) {
        if (n->role == role && (name.empty() || n->name == name))
            return n;
    }
    return nullptr;
}

int countEvents(const std::vector<TestWindow::RecordedAccessibilityEvent> &events, AccessibilityEvent::Type type,
                AccessNodeId node = 0)
{
    int n = 0;
    for (const auto &e : events)
        n += e.type == type && (node == 0 || e.node == node);
    return n;
}

// --- views

struct Settings {
    std::u16string title = u"Ustawienia";
    int saves = 0;
    bool notifications = false;
    int deletes = 0;
    bool extra = false;
    Entity<TextInput> name;

    explicit Settings(Context<Settings> &cx)
        : name(cx.newEntity<TextInput>(
                  TextInputOptions{u"Twoje imię", false, InputHandler::Purpose::Text, {}, u"Imię"}))
    { }

    AnyElement render(Window &, Context<Settings> &cx)
    {
        return div()
                .sizeFull()
                .flexCol()
                .gap(4)
                .p(8)
                .child(div().role(Role::Heading).ariaLevel(1).child(title))
                .child(extra ? AnyElement(div().id(u"extra")
                                                  .h(20)
                                                  .ariaLabel(u"Więcej opcji")
                                                  .onClick([](const ClickEvent &, Window &, App &) {})
                                                  .child(u"⋯"))
                             : empty())
                .child(div().id(u"save")
                               .h(24)
                               .flexRow()
                               .onClick(cx.listener(
                                       [](Settings &s, const ClickEvent &e, Window &, Context<Settings> &c) {
                                           s.saves += e.fromAccessibility ? 1 : 100;
                                           c.notify();
                                       }))
                               .child(div().ariaHidden().child(u"💾"))
                               .child(u"Zapisz")
                               .child(u"zmiany"))
                .child(div().id(u"notify")
                               .h(24)
                               .checked(notifications)
                               .onClick(
                                       cx.listener([](Settings &s, const ClickEvent &, Window &, Context<Settings> &c) {
                                           s.notifications = !s.notifications;
                                           c.notify();
                                       }))
                               .child(u"Powiadomienia"))
                .child(div().id(u"delete")
                               .h(24)
                               .disabled()
                               .onClick(
                                       cx.listener([](Settings &s, const ClickEvent &, Window &, Context<Settings> &c) {
                                           ++s.deletes;
                                           c.notify();
                                       }))
                               .child(u"Usuń konto"))
                .child(u"Zażółć gęślą jaźń")
                .child(div().h(30).child(name));
    }
};

struct Rows {
    std::size_t selected = 3;
    AnyElement render(Window &, Context<Rows> &cx)
    {
        Rows *self = this;
        App *app = &cx.app();
        const EntityId id = cx.entityId();
        return div().sizeFull().child(
                uniformList(u"rows", 1000,
                            [self, app, id](std::size_t i, Window &, App &) -> AnyElement {
                                return div()
                                        .id(ElementId(u"row", i))
                                        .h(20)
                                        .flexRow()
                                        .selected(self->selected == i)
                                        .onClick([app, id, i](const ClickEvent &, Window &, App &) {
                                            app->updateEntity<Rows>(id, [i](Rows &r, Context<Rows> &c) {
                                                r.selected = i;
                                                c.notify();
                                            });
                                        })
                                        .child(div().ariaHidden().w(10).child(u"•"))
                                        .child(u"Wiersz " + u16(std::to_string(i + 1)));
                            })
                        .itemHeight(20)
                        .wFull()
                        .h(100)
                        .ariaLabel(u"Notatki"));
    }
};

struct Scroller {
    AnyElement render(Window &, Context<Scroller> &)
    {
        ArenaVector<AnyElement> buttons;
        for (std::uint64_t i = 0; i < 10; ++i)
            buttons.push_back(div().id(ElementId(u"b", i))
                                      .h(40)
                                      .flexShrink0()
                                      .onClick([](const ClickEvent &, Window &, App &) { })
                                      .child(u"Przycisk " + u16(std::to_string(i))));
        return div().sizeFull().child(div().id(u"scroller").h(100).flexCol().overflowYScroll().children(buttons));
    }
};

} // namespace

class TestAtramentAccessibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void nothingBuiltWhenDisabled();
    void treeOfASmallView();
    void idsStayAcrossFrames();
    void actionsReachListeners();
    void textInputThroughTheTree();
    void listItems();
    void scrollIntoViewAndPages();
    void eventsBetweenFrames();
    void hitTest();
};

void TestAtramentAccessibility::nothingBuiltWhenDisabled()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view, false);
    QVERIFY(!w.accessibilityEnabled());
    QVERIFY(w.accessibilityTree().isEmpty());
    const std::size_t arenaWithout = w.lastFrameStats().arenaBytes;
    QVERIFY(h.tw.takeAccessibilityEvents().empty());
    QVERIFY(!w.performAccessibilityAction(1, AccessibilityAction::Click));

    // Turned on: the next frame builds it, and says the tree is new.
    w.setAccessibilityEnabled(true);
    QVERIFY(h.tw.frameRequested());
    h.tw.settle();
    QVERIFY(!w.accessibilityTree().isEmpty());
    QVERIFY(w.lastFrameStats().arenaBytes > arenaWithout);
    const auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::TreeReset), 1);

    // Off again: gone with the next frame, no events.
    w.setAccessibilityEnabled(false);
    h.tw.settle();
    QVERIFY(w.accessibilityTree().isEmpty());
    QCOMPARE(w.lastFrameStats().arenaBytes, arenaWithout);
    QVERIFY(h.tw.takeAccessibilityEvents().empty());
}

void TestAtramentAccessibility::treeOfASmallView()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    const AccessibilityTree &tree = w.accessibilityTree();
    QCOMPARE(q(tree.dump()), QStringLiteral("window\n"
                           "  heading 1 \"Ustawienia\"\n"
                           "  button \"Zapisz zmiany\"\n"
                           "  checkBox \"Powiadomienia\" [unchecked]\n"
                           "  button \"Usuń konto\" [disabled]\n"
                           "  text \"Zażółć gęślą jaźń\"\n"
                           "  textInput \"Imię\" (Twoje imię) [focusable, editable]\n"));

    const AccessibilityNode *root = tree.root();
    QVERIFY(root);
    QCOMPARE(root->role, Role::Window);
    QCOMPARE(root->bounds, Rect::fromXYWH(0, 0, 240, 320));
    QCOMPARE(root->childCount, 6);

    const AccessibilityNode *save = findNode(tree, Role::Button, u"Zapisz zmiany");
    QVERIFY(save);
    QCOMPARE(save->bounds.left(), 8.0f);
    QCOMPARE(save->bounds.width(), 240.0f - 16);
    QCOMPARE(save->bounds.height(), 24.0f);
    QCOMPARE(tree.parentOf(*save), root);
    QCOMPARE(save->childCount, 0); // its text is its name, not nodes
    QVERIFY(save->actions & actionBit(AccessibilityAction::Click));
    QVERIFY(!(save->actions & actionBit(AccessibilityAction::Focus)));

    const AccessibilityNode *disabled = findNode(tree, Role::Button, u"Usuń konto");
    QVERIFY(disabled && disabled->states.disabled);
    QVERIFY(!(disabled->actions & actionBit(AccessibilityAction::Click)));

    const AccessibilityNode *heading = findNode(tree, Role::Heading);
    QCOMPARE(heading->level, 1);
    QVERIFY(heading->bounds.top() >= 8 && heading->bounds.bottom() <= save->bounds.top());

    const AccessibilityNode *text = findNode(tree, Role::Text, u"Zażółć gęślą jaźń");
    QVERIFY(text && text->textLayout);
    QVERIFY(text->bounds.width() > 0);

    const AccessibilityNode *input = findNode(tree, Role::TextInput);
    QVERIFY(input);
    QCOMPARE(q(input->name), QStringLiteral("Imię"));
    QCOMPARE(q(input->description), QStringLiteral("Twoje imię"));
    QVERIFY(input->value.empty());
    QVERIFY(input->states.editable && !input->states.multiline && !input->states.focused);
    QVERIFY(input->actions & actionBit(AccessibilityAction::SetValue));
    QVERIFY(input->textLayout);
    QCOMPARE(tree.find(input->id), input);
    QVERIFY(!tree.focused());
}

void TestAtramentAccessibility::idsStayAcrossFrames()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    std::vector<AccessNodeId> before;
    for (const AccessibilityNode *n : w.accessibilityTree().nodes())
        before.push_back(n->id);
    QCOMPARE(before.size(), std::size_t(7));
    for (AccessNodeId id : before)
        QVERIFY(id != 0);
    QCOMPARE(std::set<AccessNodeId>(before.begin(), before.end()).size(), before.size());

    // Another frame, another name: the same nodes.
    view.update(h.app, [](Settings &s, Context<Settings> &cx) {
        s.title = u"Ustawienia konta";
        cx.notify();
    });
    h.tw.settle();
    std::vector<AccessNodeId> after;
    for (const AccessibilityNode *n : w.accessibilityTree().nodes())
        after.push_back(n->id);
    QCOMPARE(after, before);
    QCOMPARE(q(findNode(w.accessibilityTree(), Role::Heading)->name), QStringLiteral("Ustawienia konta"));

    // A node more: the others keep theirs.
    view.update(h.app, [](Settings &s, Context<Settings> &cx) {
        s.extra = true;
        cx.notify();
    });
    h.tw.settle();
    const AccessibilityTree &tree = w.accessibilityTree();
    QCOMPARE(tree.size(), std::size_t(8));
    for (AccessNodeId id : before)
        QVERIFY(tree.find(id));
    const AccessibilityNode *extra = findNode(tree, Role::Button, u"Więcej opcji");
    QVERIFY(extra);
    QCOMPARE(extra->indexInParent, 1);
}

void TestAtramentAccessibility::actionsReachListeners()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    const AccessNodeId save = findNode(w.accessibilityTree(), Role::Button, u"Zapisz zmiany")->id;
    const AccessNodeId notify = findNode(w.accessibilityTree(), Role::CheckBox)->id;
    const AccessibilityNode *del = findNode(w.accessibilityTree(), Role::Button, u"Usuń konto");
    const AccessNodeId deleteId = del->id;
    const Point deleteCenter = del->bounds.center();
    h.tw.takeAccessibilityEvents();

    QVERIFY(w.performAccessibilityAction(save, AccessibilityAction::Click));
    QCOMPARE(view.read(h.app).saves, 1); // a click from the screen reader, not the mouse
    QVERIFY(!w.performAccessibilityAction(save, AccessibilityAction::Increment)); // not a slider
    QVERIFY(!w.performAccessibilityAction(save, AccessibilityAction::Focus));     // not focusable

    QVERIFY(w.performAccessibilityAction(notify, AccessibilityAction::Click));
    QVERIFY(view.read(h.app).notifications);
    h.tw.settle();
    const AccessibilityNode *box = w.accessibilityTree().find(notify);
    QVERIFY(box && box->states.checked && box->states.checkable);
    const auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::StateChanged, notify), 1);
    for (const auto &e : events) {
        if (e.type == AccessibilityEvent::Type::StateChanged)
            QVERIFY(e.changed.checked && !e.changed.disabled);
    }

    // Disabled: neither the tree nor the mouse reaches its listener.
    QVERIFY(!w.performAccessibilityAction(deleteId, AccessibilityAction::Click));
    h.tw.simulateClick(deleteCenter);
    QCOMPARE(view.read(h.app).deletes, 0);

    // A node that is gone does nothing.
    QVERIFY(!w.performAccessibilityAction(0x1234, AccessibilityAction::Click));
}

void TestAtramentAccessibility::textInputThroughTheTree()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    const Entity<TextInput> field = view.read(h.app).name;
    const AccessNodeId input = findNode(w.accessibilityTree(), Role::TextInput)->id;
    h.tw.takeAccessibilityEvents();

    QVERIFY(w.performAccessibilityAction(input, AccessibilityAction::Focus));
    h.tw.settle();
    QVERIFY(field.read(h.app).focusHandle().isFocused(w));
    QCOMPARE(w.accessibilityTree().focused(), w.accessibilityTree().find(input));
    QVERIFY(w.accessibilityTree().find(input)->states.focused);
    auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::Focus, input), 1);

    const AccessibilityActionRequest setValue{AccessibilityAction::SetValue, u"Łucja Żółć", {}, 0};
    QVERIFY(w.performAccessibilityAction(input, setValue));
    QCOMPARE(q(field.read(h.app).text()), QStringLiteral("Łucja Żółć"));
    h.tw.settle();
    const AccessibilityNode *node = w.accessibilityTree().find(input);
    QCOMPARE(q(node->value), QStringLiteral("Łucja Żółć"));
    QCOMPARE(node->selection.focus, 10);
    events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ValueChanged, input), 1);
    for (const auto &e : events) {
        if (e.type == AccessibilityEvent::Type::ValueChanged)
            QVERIFY(e.oldText.empty());
    }

    const AccessibilityActionRequest selectRange{AccessibilityAction::SetSelection, {}, {6, 10}, 0};
    QVERIFY(w.performAccessibilityAction(input, selectRange));
    h.tw.settle();
    node = w.accessibilityTree().find(input);
    QCOMPARE(node->selection.anchor, 6);
    QCOMPARE(node->selection.focus, 10);
    events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::SelectionChanged, input), 1);
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ValueChanged), 0);

    const AccessibilityActionRequest replaceRange{AccessibilityAction::ReplaceText, u"Lu", {0, 1}, 0};
    QVERIFY(w.performAccessibilityAction(input, replaceRange));
    QCOMPARE(q(field.read(h.app).text()), QStringLiteral("Luucja Żółć"));
    h.tw.settle(); // keys move the caret over the layout on screen
    // Typing goes into the tree as well; a one-line field turns a line break into a space.
    h.tw.simulateKeystrokes(u"end");
    h.tw.simulateTyping(u"!");
    QVERIFY(w.performAccessibilityAction(
            input, AccessibilityActionRequest{AccessibilityAction::ReplaceText, u"\n", {0, 0}, 0}));
    h.tw.settle();
    QCOMPARE(q(w.accessibilityTree().find(input)->value), QStringLiteral(" Luucja Żółć!"));
    // The text's characters are where the layout put them.
    node = w.accessibilityTree().find(input);
    QVERIFY(node->textLayout);
    QVERIFY(node->bounds.contains(node->textOrigin));
    QVERIFY(!w.performAccessibilityAction(input, AccessibilityAction::Increment));
}

void TestAtramentAccessibility::listItems()
{
    Harness h({200, 120});
    Entity<Rows> view = h.app.newEntity<Rows>();
    Window &w = h.open(view);
    const AccessibilityTree *tree = &w.accessibilityTree();
    const AccessibilityNode *list = findNode(*tree, Role::List);
    QVERIFY(list);
    QCOMPARE(q(list->name), QStringLiteral("Notatki"));
    QCOMPARE(list->rowCount, std::int64_t(1000));
    QVERIFY(list->scrolls && list->scrollY);
    QVERIFY(list->actions & actionBit(AccessibilityAction::ScrollForward));
    // Only the rows in view are there (and the one the view cuts), each a list item named by its
    // text, the bullet left out.
    QCOMPARE(list->childCount, 6);
    for (int i = 0; i < list->childCount; ++i) {
        const AccessibilityNode *item = tree->child(*list, i);
        QCOMPARE(item->role, Role::ListItem);
        QCOMPARE(item->rowIndex, std::int64_t(i));
        QCOMPARE(q(item->name), QStringLiteral("Wiersz %1").arg(i + 1));
        QCOMPARE(item->childCount, 0);
        QCOMPARE(item->states.selected, i == 3);
        QVERIFY(item->states.selectable);
        QVERIFY(item->actions & actionBit(AccessibilityAction::Click));
        QCOMPARE(item->bounds, Rect::fromXYWH(0, float(i) * 20, 200, 20));
        QCOMPARE(item->scrollAncestor, list->treeIndex);
    }
    QVERIFY(tree->child(*list, 5)->states.offscreen);
    QVERIFY(!tree->child(*list, 4)->states.offscreen);

    // A click through the tree selects the row.
    const AccessNodeId row1 = tree->child(*list, 1)->id;
    QVERIFY(w.performAccessibilityAction(row1, AccessibilityAction::Click));
    QCOMPARE(view.read(h.app).selected, std::size_t(1));
    h.tw.settle();
    tree = &w.accessibilityTree();
    QVERIFY(tree->find(row1)->states.selected);

    // A page forward: other rows, each still known by its index.
    const AccessNodeId row4 = tree->child(*findNode(*tree, Role::List), 4)->id;
    h.tw.takeAccessibilityEvents();
    QVERIFY(w.performAccessibilityAction(findNode(*tree, Role::List)->id, AccessibilityAction::ScrollForward));
    h.tw.settle();
    tree = &w.accessibilityTree();
    list = findNode(*tree, Role::List);
    QCOMPARE(list->scrollOffset.y, 87.5f);
    QCOMPARE(tree->child(*list, 0)->rowIndex, std::int64_t(4));
    QCOMPARE(tree->child(*list, 0)->id, row4);
    QVERIFY(!tree->find(row1));
    const auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ScrollChanged, list->id), 1);
    QVERIFY(countEvents(events, AccessibilityEvent::Type::Removed) >= 1);
    QVERIFY(countEvents(events, AccessibilityEvent::Type::Added) >= 1);
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ChildrenChanged, list->id), 1);

    // Row 4 is cut at the top: brought into view.
    QVERIFY(w.performAccessibilityAction(row4, AccessibilityAction::ScrollIntoView));
    h.tw.settle();
    QCOMPARE(findNode(w.accessibilityTree(), Role::List)->scrollOffset.y, 80.0f);
    QVERIFY(w.performAccessibilityAction(findNode(w.accessibilityTree(), Role::List)->id,
                                         AccessibilityAction::ScrollBackward));
    h.tw.settle();
    QCOMPARE(findNode(w.accessibilityTree(), Role::List)->scrollOffset.y, 0.0f);
    // At the start: nothing more to scroll back.
    QVERIFY(!w.performAccessibilityAction(findNode(w.accessibilityTree(), Role::List)->id,
                                          AccessibilityAction::ScrollBackward));
}

void TestAtramentAccessibility::scrollIntoViewAndPages()
{
    Harness h({200, 200});
    Entity<Scroller> view = h.app.newEntity<Scroller>();
    Window &w = h.open(view);
    const AccessibilityTree *tree = &w.accessibilityTree();
    const AccessibilityNode *scroller = findNode(*tree, Role::ScrollView);
    QVERIFY(scroller);
    QCOMPARE(scroller->childCount, 10);
    QVERIFY(scroller->actions & actionBit(AccessibilityAction::ScrollForward));
    const AccessibilityNode *b5 = findNode(*tree, Role::Button, u"Przycisk 5");
    QVERIFY(b5 && b5->states.offscreen);
    QCOMPARE(b5->bounds.top(), 200.0f);
    QVERIFY(!findNode(*tree, Role::Button, u"Przycisk 0")->states.offscreen);
    const AccessNodeId b5id = b5->id;
    h.tw.takeAccessibilityEvents();

    QVERIFY(w.performAccessibilityAction(b5id, AccessibilityAction::ScrollIntoView));
    h.tw.settle();
    tree = &w.accessibilityTree();
    QCOMPARE(findNode(*tree, Role::ScrollView)->scrollOffset.y, 140.0f);
    b5 = tree->find(b5id);
    QVERIFY(!b5->states.offscreen);
    QCOMPARE(b5->bounds.bottom(), 100.0f);
    const auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ScrollChanged), 1);
    QVERIFY(countEvents(events, AccessibilityEvent::Type::StateChanged, b5id) == 1); // no longer offscreen
    // Already visible: nothing to do.
    QVERIFY(!w.performAccessibilityAction(b5id, AccessibilityAction::ScrollIntoView));
    // Pages to the end and no further.
    QVERIFY(w.performAccessibilityAction(findNode(*tree, Role::ScrollView)->id, AccessibilityAction::ScrollForward));
    h.tw.settle();
    QCOMPARE(findNode(w.accessibilityTree(), Role::ScrollView)->scrollOffset.y, 227.5f);
    QVERIFY(w.performAccessibilityAction(findNode(w.accessibilityTree(), Role::ScrollView)->id,
                                         AccessibilityAction::ScrollForward));
    h.tw.settle();
    QCOMPARE(findNode(w.accessibilityTree(), Role::ScrollView)->scrollOffset.y, 300.0f);
    QVERIFY(!w.performAccessibilityAction(findNode(w.accessibilityTree(), Role::ScrollView)->id,
                                          AccessibilityAction::ScrollForward));
}

void TestAtramentAccessibility::eventsBetweenFrames()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    auto events = h.tw.takeAccessibilityEvents();
    QCOMPARE(events.size(), std::size_t(1));
    QCOMPARE(events.front().type, AccessibilityEvent::Type::TreeReset);

    // A frame that changes nothing says nothing.
    w.refresh();
    QCOMPARE(h.tw.settle(), 1);
    QVERIFY(h.tw.takeAccessibilityEvents().empty());

    const AccessNodeId heading = findNode(w.accessibilityTree(), Role::Heading)->id;
    view.update(h.app, [](Settings &s, Context<Settings> &cx) {
        s.title = u"Konto";
        s.extra = true;
        cx.notify();
    });
    h.tw.settle();
    events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::NameChanged, heading), 1);
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::Added), 1);
    const AccessNodeId root = w.accessibilityTree().root()->id;
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::ChildrenChanged, root), 1);
    for (const auto &e : events) {
        if (e.type == AccessibilityEvent::Type::Added) {
            QCOMPARE(e.parent, root);
            QCOMPARE(q(w.accessibilityTree().find(e.node)->name), QStringLiteral("Więcej opcji"));
        }
    }

    view.update(h.app, [](Settings &s, Context<Settings> &cx) {
        s.extra = false;
        cx.notify();
    });
    h.tw.settle();
    events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::Removed), 1);
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::Added), 0);

    // The focus leaving the window is an event too.
    const AccessNodeId input = findNode(w.accessibilityTree(), Role::TextInput)->id;
    QVERIFY(w.performAccessibilityAction(input, AccessibilityAction::Click)); // clicking a field focuses it
    h.tw.settle();
    QCOMPARE(countEvents(h.tw.takeAccessibilityEvents(), AccessibilityEvent::Type::Focus, input), 1);
    w.blur();
    h.tw.settle();
    events = h.tw.takeAccessibilityEvents();
    QCOMPARE(countEvents(events, AccessibilityEvent::Type::Focus), 1);
    for (const auto &e : events) {
        if (e.type == AccessibilityEvent::Type::Focus)
            QCOMPARE(e.node, AccessNodeId(0));
    }
}

void TestAtramentAccessibility::hitTest()
{
    Harness h;
    Entity<Settings> view = h.app.newEntity<Settings>();
    Window &w = h.open(view);
    const AccessibilityTree &tree = w.accessibilityTree();
    const AccessibilityNode *save = findNode(tree, Role::Button, u"Zapisz zmiany");
    QCOMPARE(tree.hitTest(save->bounds.center()), save);
    QCOMPARE(tree.hitTest({1, 1}), tree.root()); // the padding: the window itself
    QCOMPARE(tree.hitTest({-5, 10}), nullptr);
}

QTEST_GUILESS_MAIN(TestAtramentAccessibility)
#include "tst_atrament_accessibility.moc"
