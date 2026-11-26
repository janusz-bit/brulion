// The Qt host's touch (src/atrament/qt/qthost.h): AtramentItem takes Qt Quick's QTouchEvents
// itself — the first finger is Atrament's pointer of kind Touch, with the events' times — and a
// TouchCancel or a grab taken away ends its press, while the ungrab Qt Quick sends after every
// release changes nothing. Driven with QTest's touch sequences on an offscreen window.
#include "qthost.h"
#include "text/fonts.h"
#include "ui/atrament.h"

#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QtTest>
#include <qpa/qwindowsysteminterface.h>

#include <memory>

using namespace brulion::atrament;

namespace {

std::shared_ptr<FontCollection> loadFonts()
{
    static std::shared_ptr<FontCollection> fonts = [] {
        auto f = std::make_shared<FontCollection>();
        f->addFontFile(BRULION_SOURCE_DIR "/assets/fonts/ui/BrulionInter-Regular.ttf");
        return f;
    }();
    return fonts;
}

// 100 rows of 50 px in a scroll view; rows count their clicks.
struct Rows {
    int clicks = 0;
    ScrollHandle scroll;
    AnyElement render(Window &, Context<Rows> &cx)
    {
        ArenaVector<AnyElement> rows;
        for (int i = 0; i < 100; ++i)
            rows.push_back(div().id(ElementId(u"row", std::uint64_t(i)))
                                   .h(50)
                                   .flexShrink0()
                                   .bg(i % 2 ? rgb(0xff0000) : rgb(0x0000ff))
                                   .onClick(cx.listener([](Rows &r, const ClickEvent &e, Window &, Context<Rows> &) {
                                       if (e.down.pointer == PointerKind::Touch)
                                           ++r.clicks;
                                   })));
        return div().sizeFull().flexCol().overflowYScroll().trackScroll(scroll).children(rows);
    }
};

struct Fixture {
    std::shared_ptr<QtPlatform> platform = std::make_shared<QtPlatform>();
    App app{platform, loadFonts()};
    Entity<Rows> rows = app.newEntity<Rows>();
    QQuickWindow window;
    AtramentItem *item = nullptr;
    QPointingDevice *finger = nullptr;

    Fixture()
    {
        window.resize(200, 300);
        item = new AtramentItem(window.contentItem());
        item->setSize(QSizeF(200, 300));
        item->attach(app, AnyView{rows.id(), &renderView<Rows>});
        window.show();
        draw();
        static QPointingDevice *device = QTest::createTouchDevice();
        finger = device;
    }
    ~Fixture() { delete item; }
    Window &atrament() { return *item->atramentWindow(); }
    void draw()
    {
        atrament().refresh();
        atrament().draw();
    }
    float offset() { return rows.read(app).scroll.offset().y; }
    int clicks() { return rows.read(app).clicks; }
};

} // namespace

class TestAtramentQtTouch : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase();
    void fingerDragScrolls();
    void tapClicks();
    void secondFingerIgnored();
    void cancelEndsThePress();
    void grabTakenEndsThePress();
    void flingRunsOnFrames();
};

void TestAtramentQtTouch::initTestCase()
{
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    // Every move delivered as it comes (Qt Quick otherwise merges a frame's moves into one).
    qputenv("QML_NO_TOUCH_COMPRESSION", "1");
}

void TestAtramentQtTouch::fingerDragScrolls()
{
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 250));
    QVERIFY(f.atrament().touchActive());
    QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 245)); // within the slop
    QCOMPARE(f.offset(), 0.0f);
    QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 200));
    QVERIFY(f.atrament().touchScrolling());
    const float slop = f.platform->touchSlop();
    QCOMPARE(f.offset(), 50 - slop);
    QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 150));
    QCOMPARE(f.offset(), 100 - slop);
    QTest::qWait(60); // rest: no fling
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 150));
    QVERIFY(!f.atrament().touchActive());
    QVERIFY(f.atrament().touchScrolled());
    QVERIFY(!f.atrament().flinging());
    QCOMPARE(f.clicks(), 0);
}

void TestAtramentQtTouch::tapClicks()
{
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 125));
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 125));
    QCOMPARE(f.clicks(), 1);
    QVERIFY(!f.atrament().touchScrolled());
    // Held past the tap timeout (a Qt timer), then lifted.
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 125));
    QTest::qWait(150);
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 125));
    QCOMPARE(f.clicks(), 2);
    // The mouse Qt Quick could make of a touch never came (the clicks counted only touches).
    QCOMPARE(f.offset(), 0.0f);
}

void TestAtramentQtTouch::secondFingerIgnored()
{
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 250));
    QTest::touchEvent(&f.window, f.finger).stationary(0).press(1, QPoint(50, 50));
    QTest::touchEvent(&f.window, f.finger).stationary(0).move(1, QPoint(50, 10));
    QCOMPARE(f.offset(), 0.0f);
    QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 200)).stationary(1);
    QCOMPARE(f.offset(), 50 - f.platform->touchSlop());
    QTest::touchEvent(&f.window, f.finger).stationary(0).release(1, QPoint(50, 10));
    QVERIFY(f.atrament().touchActive());
    QTest::qWait(60);
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 200));
    QVERIFY(!f.atrament().touchActive());
}

void TestAtramentQtTouch::cancelEndsThePress()
{
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 125));
    QTest::qWait(150); // the row has the press
    QVERIFY(f.atrament().touchActive());
    QWindowSystemInterface::handleTouchCancelEvent(&f.window, f.finger);
    QCoreApplication::processEvents();
    QVERIFY(!f.atrament().touchActive());
    QCOMPARE(f.clicks(), 0);
    // The next touch starts anew.
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 125));
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 125));
    QCOMPARE(f.clicks(), 1);
}

void TestAtramentQtTouch::grabTakenEndsThePress()
{
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 250));
    QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 200));
    QVERIFY(f.atrament().touchScrolling());
    // Something else takes the finger (a Drawer's edge, a Flickable around the item).
    f.item->ungrabTouchPoints();
    QVERIFY(!f.atrament().touchActive());
    QVERIFY(!f.atrament().flinging());
    // A mouse click: its release's ungrab ends nothing more (one click, no cancel after it).
    QTest::mouseClick(&f.window, Qt::LeftButton, {}, QPoint(100, 125));
    QVERIFY(!f.atrament().touchActive());
}

void TestAtramentQtTouch::flingRunsOnFrames()
{
    // A fling asks for its frames from inside the frame: never a polish loop (Qt Quick would warn).
    QTest::failOnWarning(QRegularExpression(QStringLiteral("polish")));
    Fixture f;
    QTest::touchEvent(&f.window, f.finger).press(0, QPoint(100, 280));
    for (int i = 1; i <= 8; ++i) {
        QTest::qWait(10);
        QTest::touchEvent(&f.window, f.finger).move(0, QPoint(100, 280 - 25 * i));
    }
    QTest::touchEvent(&f.window, f.finger).release(0, QPoint(100, 80));
    const float released = f.offset();
    QVERIFY(f.atrament().flinging());
    // It goes on over Qt Quick's frames, then comes to rest.
    QTRY_VERIFY_WITH_TIMEOUT(f.offset() > released + 100, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!f.atrament().flinging(), 6000);
    const float rest = f.offset();
    QTest::qWait(100);
    QCOMPARE(f.offset(), rest);
}

QTEST_MAIN(TestAtramentQtTouch)
#include "tst_atrament_qt_touch.moc"
