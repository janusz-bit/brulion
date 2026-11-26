// Atrament's accessibility bridge (src/atrament/qt/qtaccessibility.h) as Qt's own bridges see it:
// an AtramentItem in a QQuickWindow (offscreen), its QAccessibleInterface from
// QAccessible::queryAccessibleInterface, the nodes under it (roles, names, states, rectangles),
// the text and editable-text interfaces of a field, actions, a list's viewport, and the
// QAccessibleEvents a frame sends.
#include "qtaccessibility.h"
#include "qthost.h"
#include "text/fonts.h"
#include "ui/atrament.h"

#include <QAccessible>
#include <QQuickWindow>
#include <QSGRendererInterface>
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

struct View {
    std::u16string title = u"Dziennik";
    int saves = 0;
    bool reminders = false;
    bool extra = false;
    Entity<TextInput> note;

    explicit View(Context<View> &cx)
        : note(cx.newEntity<TextInput>(
                  TextInputOptions{u"Napisz coś…", false, InputHandler::Purpose::Text, u"Zażółć", u"Notatka"}))
    { }

    AnyElement render(Window &, Context<View> &cx)
    {
        return div()
                .sizeFull()
                .flexCol()
                .p(10)
                .gap(6)
                .child(div().role(Role::Heading).ariaLevel(1).child(title))
                .child(div().id(u"save")
                               .h(30)
                               .onClick(cx.listener([](View &v, const ClickEvent &e, Window &, Context<View> &c) {
                                   v.saves += e.fromAccessibility ? 1 : 100;
                                   c.notify();
                               }))
                               .child(u"Zapisz"))
                .child(div().id(u"reminders")
                               .h(30)
                               .checked(reminders)
                               .onClick(cx.listener([](View &v, const ClickEvent &, Window &, Context<View> &c) {
                                   v.reminders = !v.reminders;
                                   c.notify();
                               }))
                               .child(u"Przypomnienia"))
                .child(div().h(30).child(note))
                .child(uniformList(u"rows", 50,
                                   [](std::size_t i, Window &, App &) -> AnyElement {
                                       return div()
                                               .id(ElementId(u"row", i))
                                               .h(20)
                                               .child(u"Wiersz " + u16(std::to_string(i + 1)));
                                   })
                               .itemHeight(20)
                               .wFull()
                               .h(60)
                               .ariaLabel(u"Lista"))
                .child(extra ? AnyElement(div().id(u"extra")
                                                  .h(20)
                                                  .onClick([](const ClickEvent &, Window &, App &) {})
                                                  .child(u"Nowy"))
                             : empty());
    }
};

struct Recorded {
    QAccessible::Event type;
    QAccessible::Role role = QAccessible::NoRole;
    QString name;
    QString inserted;
    QAccessible::Id id = 0;
    QAccessible::State changed;
};
std::vector<Recorded> g_events;

void recordEvent(QAccessibleEvent *event)
{
    Recorded r{event->type(), QAccessible::NoRole, {}, {}, event->uniqueId(), {}};
    if (QAccessibleInterface *iface = event->accessibleInterface()) {
        r.role = iface->role();
        r.name = iface->text(QAccessible::Name);
    }
    if (event->type() == QAccessible::TextUpdated)
        r.inserted = static_cast<QAccessibleTextUpdateEvent *>(event)->textInserted();
    if (event->type() == QAccessible::StateChanged)
        r.changed = static_cast<QAccessibleStateChangeEvent *>(event)->changedStates();
    g_events.push_back(r);
}

int countEvents(QAccessible::Event type, const QString &name = {})
{
    int n = 0;
    for (const Recorded &r : g_events)
        n += r.type == type && (name.isEmpty() || r.name == name);
    return n;
}

QAccessibleInterface *childOf(QAccessibleInterface *parent, QAccessible::Role role, const QString &name = {})
{
    for (int i = 0; i < parent->childCount(); ++i) {
        QAccessibleInterface *c = parent->child(i);
        if (c && c->role() == role && (name.isEmpty() || c->text(QAccessible::Name) == name))
            return c;
    }
    return nullptr;
}

struct Fixture {
    std::shared_ptr<QtPlatform> platform = std::make_shared<QtPlatform>();
    App app{platform, loadFonts()};
    Entity<View> view = app.newEntity<View>();
    QQuickWindow window;
    AtramentItem *item = nullptr;

    Fixture()
    {
        window.resize(320, 400);
        item = new AtramentItem(window.contentItem());
        item->setSize(QSizeF(320, 400));
        item->attach(app, AnyView{view.id(), &renderView<View>});
    }
    ~Fixture() { delete item; }
    Window &atrament() { return *item->atramentWindow(); }
    QAccessibleInterface *root() { return QAccessible::queryAccessibleInterface(item); }
};

} // namespace

class TestAtramentQtAccessibility : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void itemIsTheRoot();
    void actionsAndStates();
    void textInput();
    void structureAndNames();
    void listViewport();
};

void TestAtramentQtAccessibility::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    QAccessible::installUpdateHandler(recordEvent);
}

void TestAtramentQtAccessibility::init()
{
    g_events.clear();
}

void TestAtramentQtAccessibility::cleanupTestCase()
{
    QAccessible::installUpdateHandler(nullptr);
}

void TestAtramentQtAccessibility::itemIsTheRoot()
{
    Fixture f;
    QVERIFY(!f.atrament().accessibilityEnabled()); // nobody asked yet
    QAccessibleInterface *root = f.root();
    QVERIFY(root && root->isValid());
    QCOMPARE(root->object(), f.item);
    QCOMPARE(root->role(), QAccessible::Client);
    QCOMPARE(root->window(), &f.window);

    // The first question turns the tree on and draws it.
    QCOMPARE(root->childCount(), 5);
    QVERIFY(f.atrament().accessibilityEnabled());
    const QAccessible::Role roles[] = {QAccessible::Heading, QAccessible::Button, QAccessible::CheckBox,
                                       QAccessible::EditableText, QAccessible::List};
    const QString names[] = {QStringLiteral("Dziennik"), QStringLiteral("Zapisz"), QStringLiteral("Przypomnienia"),
                                                                  QStringLiteral("Notatka"), QStringLiteral("Lista")};
    const QRect rootRect = root->rect();
    QCOMPARE(rootRect.size(), QSize(320, 400));
    std::vector<QAccessible::Id> ids;
    for (int i = 0; i < 5; ++i) {
        QAccessibleInterface *c = root->child(i);
        QVERIFY(c && c->isValid());
        QCOMPARE(c->role(), roles[i]);
        QCOMPARE(c->text(QAccessible::Name), names[i]);
        QCOMPARE(c->parent(), root);
        QCOMPARE(root->indexOfChild(c), i);
        QVERIFY(c->object() == nullptr);
        QCOMPARE(c->window(), &f.window);
        QVERIFY(rootRect.contains(c->rect()));
        QCOMPARE(c->rect().left(), rootRect.left() + 10);
        ids.push_back(QAccessible::uniqueId(c));
    }
    QCOMPARE(root->childAt(root->child(1)->rect().center().x(), root->child(1)->rect().center().y()), root->child(1));

    // Qt Quick lists the item among its window's children (it is accessible).
    QAccessibleInterface *windowIface = QAccessible::queryAccessibleInterface(&f.window);
    QVERIFY(windowIface);
    QVERIFY(windowIface->indexOfChild(root) >= 0);
    QCOMPARE(root->parent(), windowIface);

    // Another frame: the same interfaces, known by the same ids.
    f.atrament().refresh();
    f.atrament().draw();
    for (int i = 0; i < 5; ++i)
        QCOMPARE(QAccessible::uniqueId(root->child(i)), ids[std::size_t(i)]);
    QCOMPARE(countEvents(QAccessible::ObjectReorder), 1); // the tree when it was first built
}

void TestAtramentQtAccessibility::actionsAndStates()
{
    Fixture f;
    QAccessibleInterface *root = f.root();
    QAccessibleInterface *save = childOf(root, QAccessible::Button, QStringLiteral("Zapisz"));
    QVERIFY(save);
    QAccessibleActionInterface *actions = save->actionInterface();
    QVERIFY(actions);
    QVERIFY(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    QVERIFY(!actions->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    actions->doAction(QAccessibleActionInterface::pressAction());
    QCOMPARE(f.view.read(f.app).saves, 1);

    QAccessibleInterface *box = childOf(root, QAccessible::CheckBox);
    QVERIFY(box);
    QVERIFY(box->state().checkable);
    QVERIFY(!box->state().checked);
    QVERIFY(box->actionInterface()->actionNames().contains(QAccessibleActionInterface::toggleAction()));
    g_events.clear();
    box->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
    QVERIFY(f.view.read(f.app).reminders);
    // The frame is drawn at once: the state is there for the next question, and Qt heard of it.
    QVERIFY(box->state().checked);
    bool checkedEvent = false;
    for (const Recorded &r : g_events)
        checkedEvent = checkedEvent
                       || (r.type == QAccessible::StateChanged && r.changed.checked && r.role == QAccessible::CheckBox);
    QVERIFY(checkedEvent);
    QVERIFY(!root->child(0)->actionInterface()); // a heading does nothing
}

void TestAtramentQtAccessibility::textInput()
{
    Fixture f;
    f.window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&f.window));
    QAccessibleInterface *root = f.root();
    QAccessibleInterface *field = childOf(root, QAccessible::EditableText);
    QVERIFY(field);
    QCOMPARE(field->text(QAccessible::Name), QStringLiteral("Notatka"));
    QCOMPARE(field->text(QAccessible::Description), QStringLiteral("Napisz coś…"));
    QVERIFY(field->state().editable && field->state().focusable && !field->state().multiLine);
    QAccessibleTextInterface *text = field->textInterface();
    QAccessibleEditableTextInterface *editable = field->editableTextInterface();
    QVERIFY(text && editable);
    QCOMPARE(text->characterCount(), 6);
    QCOMPARE(text->text(0, text->characterCount()), QStringLiteral("Zażółć"));
    QCOMPARE(text->cursorPosition(), 6);
    QCOMPARE(text->selectionCount(), 0);

    // Focus from the screen reader: Qt hears where it went.
    f.item->forceActiveFocus();
    QVERIFY(f.item->hasActiveFocus());
    g_events.clear();
    field->actionInterface()->doAction(QAccessibleActionInterface::setFocusAction());
    QVERIFY(field->state().focused);
    QCOMPARE(root->focusChild(), field);
    QVERIFY(countEvents(QAccessible::Focus, QStringLiteral("Notatka")) >= 1);

    g_events.clear();
    editable->insertText(6, QStringLiteral(" gęślą jaźń"));
    QCOMPARE(text->text(0, -1), QStringLiteral("Zażółć gęślą jaźń"));
    QCOMPARE(q(f.view.read(f.app).note.read(f.app).text()), QStringLiteral("Zażółć gęślą jaźń"));
    bool inserted = false;
    for (const Recorded &r : g_events)
        inserted = inserted || (r.type == QAccessible::TextUpdated && r.inserted == QStringLiteral(" gęślą jaźń"));
    QVERIFY(inserted);

    g_events.clear();
    text->setSelection(0, 7, 12);
    int start = -1, end = -1;
    text->selection(0, &start, &end);
    QCOMPARE(start, 7);
    QCOMPARE(end, 12);
    QCOMPARE(text->selectionCount(), 1);
    QCOMPARE(countEvents(QAccessible::TextSelectionChanged), 1);
    QCOMPARE(countEvents(QAccessible::TextCaretMoved), 1);

    int s = 0, e = 0;
    QCOMPARE(text->textAtOffset(9, QAccessible::WordBoundary, &s, &e), QStringLiteral("gęślą"));
    QCOMPARE(s, 7);
    QCOMPARE(e, 12);
    QCOMPARE(text->textAtOffset(2, QAccessible::CharBoundary, &s, &e), QStringLiteral("ż"));
    QCOMPARE(text->textAtOffset(0, QAccessible::NoBoundary, &s, &e), QStringLiteral("Zażółć gęślą jaźń"));
    const QRect c2 = text->characterRect(2);
    QVERIFY(!c2.isEmpty());
    QVERIFY(field->rect().contains(c2.center()));
    QCOMPARE(text->offsetAtPoint(QPoint(c2.left() + 1, c2.center().y())), 2);

    editable->replaceText(0, 6, QStringLiteral("Łódź"));
    QCOMPARE(text->text(0, -1), QStringLiteral("Łódź gęślą jaźń"));
    editable->deleteText(4, 10);
    QCOMPARE(text->text(0, -1), QStringLiteral("Łódź jaźń"));
}

void TestAtramentQtAccessibility::structureAndNames()
{
    Fixture f;
    QAccessibleInterface *root = f.root();
    QCOMPARE(root->childCount(), 5);
    for (int i = 0; i < root->childCount(); ++i)
        QVERIFY(root->child(i)); // an assistive technology has seen them all
    QAccessibleInterface *heading = root->child(0);
    g_events.clear();

    f.view.update(f.app, [](View &v, Context<View> &cx) {
        v.title = u"Kalendarz";
        v.extra = true;
        cx.notify();
    });
    f.atrament().draw();
    QCOMPARE(heading->text(QAccessible::Name), QStringLiteral("Kalendarz"));
    QCOMPARE(countEvents(QAccessible::NameChanged, QStringLiteral("Kalendarz")), 1);
    QCOMPARE(countEvents(QAccessible::ObjectCreated, QStringLiteral("Nowy")), 1);
    QCOMPARE(root->childCount(), 6);
    QAccessibleInterface *extra = childOf(root, QAccessible::Button, QStringLiteral("Nowy"));
    QVERIFY(extra);
    const QAccessible::Id extraId = QAccessible::uniqueId(extra);

    g_events.clear();
    f.view.update(f.app, [](View &v, Context<View> &cx) {
        v.extra = false;
        cx.notify();
    });
    f.atrament().draw();
    QCOMPARE(root->childCount(), 5);
    QCOMPARE(countEvents(QAccessible::ObjectDestroyed, QStringLiteral("Nowy")), 1);
    QCOMPARE(countEvents(QAccessible::ObjectHide), 1);
    // The interface answers for its last place until the event loop runs, then it is gone.
    QVERIFY(QAccessible::accessibleInterface(extraId));
    QCOMPARE(QAccessible::accessibleInterface(extraId)->parent(), root);
    QCoreApplication::processEvents();
    QVERIFY(!QAccessible::accessibleInterface(extraId));
}

void TestAtramentQtAccessibility::listViewport()
{
    Fixture f;
    QAccessibleInterface *root = f.root();
    QAccessibleInterface *list = childOf(root, QAccessible::List);
    QVERIFY(list);
    QCOMPARE(list->childCount(), 4); // three rows in 60 px, and the one cut off
    QAccessibleInterface *first = list->child(0);
    QCOMPARE(first->role(), QAccessible::ListItem);
    QCOMPARE(first->text(QAccessible::Name), QStringLiteral("Wiersz 1"));
    QVERIFY(list->child(3)->state().offscreen);
    QAccessibleViewportInterface *viewport = list->viewportInterface();
    QVERIFY(viewport);
    QVERIFY(viewport->isIndexed());
    QCOMPARE(viewport->contentSize(), QSizeF(1, 50));
    QCOMPARE(viewport->position(), QPointF(0, 0));
    QCOMPARE(viewport->viewportSize(), QSizeF(1, 3.0 / 50));
    // TalkBack scrolls a list forward with Increase, then looks at its first child.
    const QStringList names = list->actionInterface()->actionNames();
    QVERIFY(names.contains(QAccessibleActionInterface::increaseAction()));
    QVERIFY(names.contains(QAccessibleActionInterface::scrollDownAction()));
    const QRect before = first->rect();
    g_events.clear();
    list->actionInterface()->doAction(QAccessibleActionInterface::increaseAction());
    QAccessibleInterface *nowFirst = list->child(0);
    QVERIFY(nowFirst);
    QVERIFY(nowFirst->rect() != before);
    QCOMPARE(nowFirst->text(QAccessible::Name), QStringLiteral("Wiersz 3"));
    QVERIFY(viewport->position().y() > 0);
    QCOMPARE(countEvents(QAccessible::ScrollingPositionChanged), 1);
    // A row half out of view comes into it.
    QVERIFY(nowFirst->actionInterface()->actionNames().contains(QAccessibleActionInterface::showOnScreenAction()));
    nowFirst->actionInterface()->doAction(QAccessibleActionInterface::showOnScreenAction());
    QCOMPARE(list->child(0)->rect().top(), list->rect().top());
    viewport->setPosition(QPointF(0, 0));
    QCOMPARE(list->child(0)->text(QAccessible::Name), QStringLiteral("Wiersz 1"));
}

QTEST_MAIN(TestAtramentQtAccessibility)
#include "tst_atrament_qt_accessibility.moc"
