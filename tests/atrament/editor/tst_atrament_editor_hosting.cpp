// Foreign rows hosted in an AtramentItem (src/app/editor/atrament/foreignhost.h; docs/ATRAMENT.md
// "Rows Atrament does not draw yet"; qthost.h "Hosted children"), in an offscreen Qt Quick window
// with the software renderer: a code block and a table get their QML delegate (the row's roles as
// required properties, `index`), placed over their rows and measured (their heights become the
// rows'), the header above the blocks and the footer below them; the delegates follow a scroll in
// the same frame and go when far away; a finger's drag that starts on a delegate scrolls the page
// (the delegate's press cancelled), a tap stays the delegate's, the wheel over one scrolls; the
// hosted items are the item's accessible children; a click on the page's own rows takes the
// keyboard back from a delegate.
#include "blocklistmodel.h"
#include "editorhost.h"
#include "foreignhost.h"
#include "pageview.h"
#include "qthost.h"
#include "text/fonts.h"
#include "ui/atrament.h"

#include <QAccessible>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>

#include <memory>

using namespace brulion::editor;
using namespace brulion::editor::atr;
using namespace Qt::StringLiterals;
namespace at = brulion::atrament;

namespace {

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

// What AtramentView does for "page": the host places its items after every frame.
class PageHostItem : public at::AtramentItem {
public:
    using AtramentItem::AtramentItem;
    std::unique_ptr<ForeignRowHost> host;
    // One polish now: a frame and the items placed over it (as Qt Quick's polish does).
    void frame()
    {
        requestFrame();
        updatePolish();
    }

protected:
    void placeHostedChildren() override
    {
        if (host)
            host->place();
    }
};

// A code block takes presses as a MouseArea (the mouse Qt Quick makes of a finger: an exclusive
// grab), any other row as a TapHandler (a passive one).
constexpr const char *kDelegate = R"(
import QtQuick
Item {
    id: d
    required property int index
    required property bool shown
    required property string view
    required property bool isEditing
    property var model
    property int taps: 0
    property int cancels: 0
    width: 600
    height: view === "code" ? 120 : 90
    Accessible.role: Accessible.Grouping
    Accessible.name: "wiersz " + view
    MouseArea {
        anchors.fill: parent
        enabled: d.view === "code"
        onClicked: d.taps++
        onCanceled: d.cancels++
    }
    TapHandler {
        enabled: d.view !== "code"
        onTapped: d.taps++
    }
}
)";

QString pageWithBlocks()
{
    return u"Akapit pierwszy o gęsi.\n\n```cpp\nint źdźbło = 1;\n```\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\n"
           "Akapit po tabeli.\n"_s;
}

// Paragraphs with a code block or a table every fourth block (code at rows 3, 11, 19…, tables at
// 7, 15, 23…).
QString longPage(int blocks)
{
    QStringList parts;
    for (int k = 0; k < blocks; ++k) {
        if (k % 8 == 3)
            parts << u"```cpp\nint gęś = %1;\n```"_s.arg(k);
        else if (k % 8 == 7)
            parts << u"| a | b |\n|---|---|\n| %1 | ż |"_s.arg(k);
        else
            parts << u"Akapit %1 o źdźble trawy."_s.arg(k);
    }
    return parts.join(u"\n\n"_s) + u'\n';
}

struct Fixture {
    std::shared_ptr<at::QtPlatform> platform = std::make_shared<at::QtPlatform>();
    at::App app{platform, loadFonts()};
    QQmlEngine engine;
    QQuickWindow window;
    BlockListModel model;
    at::Entity<PageView> page;
    PageHostItem *item = nullptr;
    QQmlComponent *delegate = nullptr;
    QQuickItem *header = nullptr;
    QQuickItem *footer = nullptr;
    QPointingDevice *finger = nullptr;

    explicit Fixture(const QString &content)
    {
        window.resize(600, 400);
        model.resetContent(content);
        page = app.newEntity<PageView>(&model);
        item = new PageHostItem(window.contentItem());
        item->setSize(QSizeF(600, 400));
        delegate = new QQmlComponent(&engine, &window);
        delegate->setData(kDelegate, QUrl());
        if (!delegate->isReady())
            qWarning() << delegate->errorString();
        QQmlComponent edge(&engine);
        edge.setData("import QtQuick\nItem { width: 600; property int taps: 0; "
                     "TapHandler { onTapped: parent.taps++ } }",
                     QUrl());
        header = qobject_cast<QQuickItem *>(edge.create());
        header->setHeight(80);
        footer = qobject_cast<QQuickItem *>(edge.create());
        footer->setHeight(60);
        item->host = std::make_unique<ForeignRowHost>(item, app, page, &model);
        item->host->setDelegate(delegate);
        item->host->setHeader(header);
        item->host->setFooter(footer);
        item->attach(app, at::AnyView{page.id(), &at::renderView<PageView>});
        window.show();
        static QPointingDevice *device = QTest::createTouchDevice();
        finger = device;
        settle();
    }
    ~Fixture()
    {
        delete item;
        delete header;
        delete footer;
    }
    const PageView &view() const { return page.read(const_cast<at::App &>(app)); }
    // Frames until nothing more is asked for (what Qt Quick's next frames would do).
    void settle()
    {
        for (int i = 0; i < 8; ++i)
            item->frame();
        QCoreApplication::processEvents();
    }
    float scrollY() const { return view().handle().scroll().offset.y; }
    const ForeignFrame *frameOf(int row) const
    {
        for (const ForeignFrame &f : view().foreignFrames())
            if (f.kind == ForeignFrame::Kind::Row && f.row == row)
                return &f;
        return nullptr;
    }
    QPoint centre(QQuickItem *it) const { return it->mapToScene(QPointF(it->width() / 2, it->height() / 2)).toPoint(); }
};

} // namespace

class TestAtramentEditorHosting : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void hostsCodeAndTable();
    void followsTheScroll();
    void touchDragOnHostedRowScrolls();
    void tapStaysTheDelegates();
    void wheelOverHostedRowScrolls();
    void hostedItemsAreAccessibleChildren();
    void keyboardComesBackFromHostedRow();
    void headerComesAndGoes();
};

void TestAtramentEditorHosting::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    // Every move delivered as it comes (Qt Quick otherwise merges a frame's moves into one).
    qputenv("QML_NO_TOUCH_COMPRESSION", "1");
}

void TestAtramentEditorHosting::hostsCodeAndTable()
{
    Fixture f(pageWithBlocks());
    QCOMPARE(f.model.rowCount(), 4);
    const PageView &v = f.view();
    QVERIFY(v.drawsRow(0) && !v.drawsRow(1) && !v.drawsRow(2) && v.drawsRow(3));
    QQuickItem *code = f.item->host->rowItem(1);
    QQuickItem *table = f.item->host->rowItem(2);
    QVERIFY(code && table);
    QVERIFY(!f.item->host->rowItem(0)); // drawn by Atrament
    QVERIFY(!f.item->host->rowItem(3));
    QCOMPARE(code->parentItem(), f.item);
    QCOMPARE(code->property("index").toInt(), 1);
    QCOMPARE(code->property("view").toString(), u"code"_s);
    QCOMPARE(table->property("view").toString(), u"table"_s);
    QVERIFY(code->property("shown").toBool());
    // The header above the blocks, the rows below it; each delegate over its row, which is as tall
    // as the delegate.
    QCOMPARE(f.header->parentItem(), f.item);
    QCOMPARE(f.header->y(), 0.0);
    const RowHeightTree &tree = v.rows().heights();
    QCOMPARE(v.rowBounds(0)->top(), 80.0f);
    QCOMPARE(tree.height(PageRows::treeRow(1)), 120.0);
    QCOMPARE(tree.height(PageRows::treeRow(2)), 90.0);
    QVERIFY(tree.isMeasured(PageRows::treeRow(1)));
    QCOMPARE(code->y(), qreal(v.rowBounds(1)->top()));
    QCOMPARE(code->y(), qreal(f.frameOf(1)->bounds.top()));
    QCOMPARE(table->y(), code->y() + 120);
    QCOMPARE(qreal(v.rowBounds(3)->top()), table->y() + 90);
    QCOMPARE(code->x(), 0.0);
    // The footer under the last block; the item cuts its children to itself.
    QCOMPARE(f.footer->y(), qreal(v.rowBounds(3)->bottom()));
    QVERIFY(f.item->clip());
    // The delegate follows its row's roles.
    QCOMPARE(f.item->host->hostedCount(), 2);
    f.model.resetContent(u"```py\nprint('żółw')\n```\n\nAkapit.\n"_s);
    f.settle();
    QQuickItem *py = f.item->host->rowItem(0);
    QVERIFY(py);
    QCOMPARE(py->property("index").toInt(), 0);
    QCOMPARE(f.item->host->hostedCount(), 1);
    QCOMPARE(py->y(), 80.0);
}

void TestAtramentEditorHosting::followsTheScroll()
{
    Fixture f(longPage(200));
    QQuickItem *first = f.item->host->rowItem(3);
    QVERIFY(first);
    // Scrolled by 100 px (the wheel, through Atrament): the delegate moved with its row, in the
    // frame that scrolled.
    const qreal before = first->y();
    QWheelEvent wheel(QPointF(300, 200), f.window.mapToGlobal(QPointF(300, 200)), QPoint(0, -100), QPoint(),
                      Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&f.window, &wheel);
    f.item->frame(); // one polish: the scroll and the items' places
    QCOMPARE(f.scrollY(), 100.0f);
    QCOMPARE(first->y(), before - 100);
    QCOMPARE(first->y(), qreal(f.frameOf(3)->bounds.top()));
    // Every painted foreign row has its delegate, at its row's place.
    for (const ForeignFrame &fr : f.view().foreignFrames()) {
        if (fr.kind != ForeignFrame::Kind::Row)
            continue;
        QQuickItem *it = f.item->host->rowItem(fr.row);
        QVERIFY(it);
        QCOMPARE(it->y(), qreal(fr.bounds.top()));
        QCOMPARE(it->height(), qreal(fr.bounds.height()));
    }
    // Far down: the delegates up there went, those around the view are there — a few, not 50.
    f.page.update(f.app, [](PageView &v, at::Context<PageView> &cx) {
        v.handle().scrollToRow(150);
        cx.notify();
    });
    f.settle();
    QVERIFY(!f.item->host->rowItem(3));
    QVERIFY(f.item->host->rowItem(151));
    QVERIFY2(f.item->host->hostedCount() < 16, qPrintable(QString::number(f.item->host->hostedCount())));
    // Rows near the view below it are made (and measured) before they scroll in.
    const std::vector<int> near = f.view().foreignRowsNear(0, 200);
    QVERIFY(!near.empty());
    for (int row : near)
        QVERIFY2(f.item->host->rowItem(row), qPrintable(QString::number(row)));
    QVERIFY(f.view().rows().heights().isMeasured(PageRows::treeRow(near.back())));
}

void TestAtramentEditorHosting::touchDragOnHostedRowScrolls()
{
    Fixture f(longPage(200));
    QQuickItem *code = f.item->host->rowItem(3);
    QVERIFY(code);
    const QPoint start = f.centre(code);
    QVERIFY(start.y() > 100 && start.y() < 380);
    // A finger on the code block's MouseArea, dragged up by 120 px: the page scrolls, the
    // MouseArea's press is cancelled, nothing is clicked.
    QTest::touchEvent(&f.window, f.finger).press(0, start);
    QVERIFY(f.item->atramentWindow()->touchActive());
    QCOMPARE(f.scrollY(), 0.0f);
    for (int dy = 10; dy <= 120; dy += 10)
        QTest::touchEvent(&f.window, f.finger).move(0, start - QPoint(0, dy));
    QVERIFY(f.item->atramentWindow()->touchScrolling());
    QTest::qWait(60); // rest: no fling
    QTest::touchEvent(&f.window, f.finger).release(0, start - QPoint(0, 120));
    f.item->frame();
    const float slop = f.platform->touchSlop();
    QVERIFY2(std::abs(f.scrollY() - (120 - slop)) < 11, qPrintable(QString::number(f.scrollY())));
    QCOMPARE(code->property("taps").toInt(), 0);
    QVERIFY(code->property("cancels").toInt() >= 1);
    QCOMPARE(code->y(), qreal(f.frameOf(3)->bounds.top()));
    // The same on a table (a TapHandler, a passive grab): it scrolls, no tap.
    QQuickItem *row = nullptr;
    for (const ForeignFrame &fr : f.view().foreignFrames()) {
        QQuickItem *it = fr.kind == ForeignFrame::Kind::Row ? f.item->host->rowItem(fr.row) : nullptr;
        if (it && it->property("view").toString() == u"table"_s && fr.bounds.top() > 0 && fr.bounds.bottom() < 400)
            row = it;
    }
    QVERIFY(row);
    const float y0 = f.scrollY();
    const QPoint on = f.centre(row);
    QTest::touchEvent(&f.window, f.finger).press(0, on);
    for (int dy = 10; dy <= 80; dy += 10)
        QTest::touchEvent(&f.window, f.finger).move(0, on - QPoint(0, dy));
    QTest::qWait(60);
    QTest::touchEvent(&f.window, f.finger).release(0, on - QPoint(0, 80));
    f.item->frame();
    QVERIFY2(f.scrollY() > y0 + 50, qPrintable(QString::number(f.scrollY())));
    QCOMPARE(row->property("taps").toInt(), 0);
}

void TestAtramentEditorHosting::tapStaysTheDelegates()
{
    Fixture f(pageWithBlocks());
    QQuickItem *code = f.item->host->rowItem(1);
    QQuickItem *table = f.item->host->rowItem(2);
    QVERIFY(code && table);
    // A tap on each: the delegate's own (a click of the MouseArea, the TapHandler's tap).
    QTest::touchEvent(&f.window, f.finger).press(0, f.centre(code));
    QTest::touchEvent(&f.window, f.finger).release(0, f.centre(code));
    QCOMPARE(code->property("taps").toInt(), 1);
    QTest::touchEvent(&f.window, f.finger).press(0, f.centre(table));
    QTest::touchEvent(&f.window, f.finger).release(0, f.centre(table));
    QCOMPARE(table->property("taps").toInt(), 1);
    // The footer's room under the blocks takes its tap too.
    QTest::touchEvent(&f.window, f.finger).press(0, f.centre(f.footer));
    QTest::touchEvent(&f.window, f.finger).release(0, f.centre(f.footer));
    QCOMPARE(f.footer->property("taps").toInt(), 1);
    QCOMPARE(f.scrollY(), 0.0f);
    // A mouse's click too.
    QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, f.centre(code));
    QCOMPARE(code->property("taps").toInt(), 2);
}

void TestAtramentEditorHosting::wheelOverHostedRowScrolls()
{
    Fixture f(longPage(100));
    QQuickItem *code = f.item->host->rowItem(3);
    QVERIFY(code);
    const QPoint over = f.centre(code);
    QWheelEvent wheel(QPointF(over), f.window.mapToGlobal(QPointF(over)), QPoint(0, -60), QPoint(), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(&f.window, &wheel);
    f.item->frame();
    QCOMPARE(f.scrollY(), 60.0f);
    QCOMPARE(code->y(), qreal(f.frameOf(3)->bounds.top()));
}

void TestAtramentEditorHosting::hostedItemsAreAccessibleChildren()
{
    Fixture f(pageWithBlocks());
    QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(f.item);
    QVERIFY(iface);
    f.settle();
    // The window's nodes, then the delegates (Qt Quick's own interfaces), whose parent is the item.
    QAccessibleInterface *code = QAccessible::queryAccessibleInterface(f.item->host->rowItem(1));
    QVERIFY(code);
    const int index = iface->indexOfChild(code);
    QVERIFY2(index >= 0, qPrintable(QString::number(index)));
    QCOMPARE(iface->child(index)->object(), f.item->host->rowItem(1));
    QCOMPARE(iface->child(index)->text(QAccessible::Name), u"wiersz code"_s);
    QVERIFY(index < iface->childCount());
    QCOMPARE(code->parent()->object(), f.item);
}

void TestAtramentEditorHosting::keyboardComesBackFromHostedRow()
{
    Fixture f(pageWithBlocks());
    f.window.requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(&f.window));
    QQuickItem *code = f.item->host->rowItem(1);
    QVERIFY(code);
    // The delegate takes the keyboard (its editor): Qt Quick's, the window inactive meanwhile.
    code->forceActiveFocus();
    QCOMPARE(f.window.activeFocusItem(), code);
    QVERIFY(!f.item->isActive());
    // A click on the delegate leaves it there.
    QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, f.centre(code));
    QCOMPARE(f.window.activeFocusItem(), code);
    // A click on a paragraph (the window's own row) takes it back.
    const at::Rect row = *f.view().rowBounds(0);
    QTest::mouseClick(&f.window, Qt::LeftButton, Qt::NoModifier, QPoint(int(row.center().x), int(row.center().y)));
    QCOMPARE(f.window.activeFocusItem(), f.item);
    QVERIFY(f.item->isActive());
}

void TestAtramentEditorHosting::headerComesAndGoes()
{
    // A day of the journal stream: no footer item (the room under its blocks is the stream's), a
    // header — the zoom's breadcrumb — only while it is zoomed into a block.
    Fixture f(pageWithBlocks());
    f.item->host->setHeader(nullptr);
    f.item->host->setFooter(nullptr);
    f.page.update(f.app, [](PageView &v, at::Context<PageView> &cx) {
        v.setFooterHeight(12);
        cx.notify();
    });
    f.settle();
    QCOMPARE(f.view().headerHeight(), 0.0f);
    QCOMPARE(f.view().footerHeight(), 12.0f);
    QCOMPARE(f.view().rowBounds(0)->top(), 0.0f);
    // The header comes: the blocks below it, the page's own footer kept.
    f.item->host->setHeader(f.header);
    f.settle();
    QCOMPARE(f.view().headerHeight(), 80.0f);
    QCOMPARE(f.view().footerHeight(), 12.0f);
    QCOMPARE(f.view().rowBounds(0)->top(), 80.0f);
    QCOMPARE(f.header->y(), 0.0);
    QVERIFY(f.header->isVisible());
    // It goes: no room above the blocks any more.
    f.item->host->setHeader(nullptr);
    f.settle();
    QCOMPARE(f.view().headerHeight(), 0.0f);
    QCOMPARE(f.view().footerHeight(), 12.0f);
    QCOMPARE(f.view().rowBounds(0)->top(), 0.0f);
}

QTEST_MAIN(TestAtramentEditorHosting)
#include "tst_atrament_editor_hosting.moc"
