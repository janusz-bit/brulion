// NearView (src/app/ui/nearview.h): items of an offscreen window, far from its view and in it,
// moved the way a scrolled page moves them. Most checks look at once (NearView::checkAll) so that
// they depend on no frame; "a frame looks" and the QML scene leave it to the window's frames, as
// the app does.
#include "memorykeeper.h"
#include "nearview.h"
#include "processmemory.h"

#include <QImage>
#include <QList>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::MemoryKeeper;
using brulion::ui::NearView;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

/// The reach the checks count with: a window 400 × 600 reaches 600 px up and down to enter and
/// 1200 px to leave, sideways (narrower than minScreen) 480 px and 960 px.
NearView::Reach testReach(int hiddenGraceMs = 600000, int placedWaitMs = 600000)
{
    NearView::Reach reach;
    reach.enter = 1.0;
    reach.leave = 2.0;
    reach.minScreen = 480;
    reach.hiddenGraceMs = hiddenGraceMs;
    reach.placedWaitMs = placedWaitMs;
    return reach;
}

/// A list of forty rows of 300 px under a header of 50 px, every row with a frame that asks
/// whether it is near; `wrong` counts the frames of rows far below the view (from the ninth) that
/// were near at any time, `made` the rows whose creation ended.
const char *const rowsQml = R"(
import QtQuick
import NearViewTest
ListView {
    id: list
    property int wrong: 0
    property int made: 0
    width: 400; height: 600; model: 40; cacheBuffer: 100000
    header: Rectangle {
        objectName: "header"
        width: 400; height: 50
        readonly property bool near: NearView.near
    }
    delegate: Item {
        id: row
        required property int index
        objectName: "row" + index
        width: 400; height: 300
        readonly property alias near: frame.near
        Rectangle {
            id: frame
            width: 300; height: 200
            readonly property bool near: NearView.near
            onNearChanged: if (near && row.index >= 8) list.wrong += 1
        }
        // (Many objects: a row takes many steps to create.)
        Repeater { model: 12; Item { Rectangle { width: 4; height: 4 } } }
        Component.onCompleted: list.made += 1
    }
}
)";

/// The item named `name` among `root` and what it shows (a Repeater's items are no QObject
/// children of anything findChild() walks).
QQuickItem *itemNamed(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
        return root;
    const QList<QQuickItem *> children = root->childItems();
    for (QQuickItem *child : children) {
        if (QQuickItem *found = itemNamed(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

class TestNearView : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<QQuickWindow> m_window;
    QQuickItem *m_page = nullptr; // what scrolls: moved by its position

    bool makeWindow()
    {
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(400, 600);
        m_page = new QQuickItem(m_window->contentItem());
        m_page->setSize(QSizeF(400, 100000));
        m_window->show();
        return QTest::qWaitForWindowExposed(m_window.get());
    }
    /// An item of the page and its NearView.
    NearView *box(qreal x, qreal y, qreal width = 200, qreal height = 100)
    {
        auto *item = new QQuickItem(m_page);
        item->setPosition(QPointF(x, y));
        item->setSize(QSizeF(width, height));
        return new NearView(item);
    }
    static bool looked(NearView *view)
    {
        NearView::checkAll();
        return view->isNear();
    }
    static bool becomes(NearView *view, bool near)
    {
        return QTest::qWaitFor([view, near] { return view->isNear() == near; }, 10000 * Slowdown);
    }

private Q_SLOTS:
    void init()
    {
        NearView::setReach(testReach());
        NearView::setReleased({});
        QVERIFY(!NearView::switchedOff());
    }
    void cleanup()
    {
        // (First: a test's hook must not outlive what it captured.)
        NearView::setReleased({});
        m_page = nullptr;
        m_window.reset();
    }

    void inAndAroundTheView_data()
    {
        QTest::addColumn<QRectF>("place");
        QTest::addColumn<bool>("near");
        QTest::newRow("in the view") << QRectF(0, 0, 200, 100) << true;
        QTest::newRow("at the view's lower edge") << QRectF(0, 599, 200, 100) << true;
        QTest::newRow("a window height below") << QRectF(0, 1150, 200, 100) << true;
        QTest::newRow("just within reach below") << QRectF(0, 1200, 200, 100) << true;
        QTest::newRow("beyond reach below") << QRectF(0, 1201, 200, 100) << false;
        QTest::newRow("far below") << QRectF(0, 50000, 200, 100) << false;
        QTest::newRow("above, its end within reach") << QRectF(0, -700, 200, 100) << true;
        QTest::newRow("above, beyond reach") << QRectF(0, -701, 200, 100) << false;
        // (A window 400 px wide reaches as far as one of minScreen: 480 px.)
        QTest::newRow("to the right, within reach") << QRectF(880, 0, 200, 100) << true;
        QTest::newRow("to the right, beyond reach") << QRectF(881, 0, 200, 100) << false;
        QTest::newRow("to the left, within reach") << QRectF(-680, 0, 200, 100) << true;
        QTest::newRow("to the left, beyond reach") << QRectF(-681, 0, 200, 100) << false;
        QTest::newRow("larger than the view") << QRectF(-5000, -5000, 10000, 10000) << true;
        QTest::newRow("no size yet, in the view") << QRectF(10, 10, 0, 0) << true;
        QTest::newRow("no size yet, far") << QRectF(10, 5000, 0, 0) << false;
    }

    void inAndAroundTheView()
    {
        QFETCH(QRectF, place);
        QFETCH(bool, near);
        QVERIFY(makeWindow());
        NearView *view = box(place.x(), place.y(), place.width(), place.height());
        QCOMPARE(looked(view), near);
    }

    /// Near from `enter`, far only beyond `leave`: reading back and forth around one place loads
    /// nothing again.
    void leavesFartherThanItEnters()
    {
        QVERIFY(makeWindow());
        NearView *view = box(0, 1300);
        QSignalSpy changed(view, &NearView::nearChanged);
        const qint64 releases = NearView::releases();
        QVERIFY(!looked(view));
        m_page->setY(-200); // the item 1100 px down the window: within a window height
        QVERIFY(looked(view));
        QCOMPARE(changed.size(), 1);
        m_page->setY(0); // back where it was not near before: still held
        QVERIFY(looked(view));
        m_page->setY(400); // 1700 px down: within `leave` (1800)
        QVERIFY(looked(view));
        QCOMPARE(changed.size(), 1);
        QCOMPARE(NearView::releases(), releases);
        m_page->setY(600); // 1900 px down
        QVERIFY(!looked(view));
        QCOMPARE(changed.size(), 2);
        QCOMPARE(NearView::releases(), releases + 1);
        m_page->setY(0); // 1300 px down again: beyond `enter`
        QVERIFY(!looked(view));
        QCOMPARE(changed.size(), 2);
    }

    /// The app never asks for a look: the window's frames do it.
    void aFrameLooks()
    {
        QVERIFY(makeWindow());
        NearView *far = box(0, 5000);
        NearView *shown = box(0, 100);
        QVERIFY(becomes(shown, true));
        QVERIFY(!far->isNear());
        m_page->setY(-4900);
        QVERIFY(becomes(far, true));
        QVERIFY(becomes(shown, false));
    }

    /// The place counts, whatever puts the item there: a parent scaled and moved.
    void throughItsParents()
    {
        QVERIFY(makeWindow());
        auto *half = new QQuickItem(m_page);
        half->setTransformOrigin(QQuickItem::TopLeft);
        half->setScale(0.5);
        auto *item = new QQuickItem(half);
        item->setPosition(QPointF(0, 2000)); // 1000 px down the window
        item->setSize(QSizeF(100, 100));
        auto *view = new NearView(item);
        QVERIFY(looked(view));
        half->setScale(1); // 2000 px down: beyond `leave`
        QVERIFY(!looked(view));
        half->setY(-1000);
        QVERIFY(looked(view));
    }

    /// A hidden item keeps what it holds for a while: a look at another view and back.
    void hiddenForAMoment()
    {
        QVERIFY(makeWindow());
        NearView *view = box(0, 100);
        NearView *other = box(0, 200);
        QVERIFY(looked(view));
        QSignalSpy changed(view, &NearView::nearChanged);
        view->parent()->setProperty("visible", false);
        QVERIFY(looked(view));
        m_page->setVisible(false); // its view hidden: every item of it
        QVERIFY(looked(view));
        QVERIFY(other->isNear());
        m_page->setVisible(true);
        view->parent()->setProperty("visible", true);
        QVERIFY(looked(view));
        QCOMPARE(changed.size(), 0);
    }

    void hiddenForLong()
    {
        NearView::setReach(testReach(40));
        QVERIFY(makeWindow());
        NearView *view = box(0, 100);
        NearView *stays = box(0, 200);
        NearView *far = box(0, 9000);
        QVERIFY(looked(view));
        const qint64 releases = NearView::releases();
        auto *item = qobject_cast<QQuickItem *>(view->parent());
        item->setVisible(false);
        // No frame is drawn for it: the grace's own timer looks.
        QVERIFY(becomes(view, false));
        QCOMPARE(NearView::releases(), releases + 1);
        QVERIFY(stays->isNear());
        QVERIFY(!far->isNear());
        // Shown again: near at once (its next look), not after a frame that may never come.
        item->setVisible(true);
        QVERIFY(becomes(view, true));
        // A far item hidden and shown stays far.
        qobject_cast<QQuickItem *>(far->parent())->setVisible(false);
        QVERIFY(!looked(far));
        qobject_cast<QQuickItem *>(far->parent())->setVisible(true);
        QVERIFY(!looked(far));
    }

    /// The window hidden (minimised, the app in the background): all of it, after the grace.
    void windowHidden()
    {
        NearView::setReach(testReach(40));
        QVERIFY(makeWindow());
        NearView *view = box(0, 100);
        QVERIFY(becomes(view, true));
        m_window->hide();
        QVERIFY(becomes(view, false));
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        QVERIFY(becomes(view, true));
    }

    void withoutAWindow()
    {
        QVERIFY(makeWindow());
        auto item = std::make_unique<QQuickItem>();
        item->setSize(QSizeF(100, 100));
        auto *view = new NearView(item.get());
        QVERIFY(!view->isNear());
        NearView::checkAll();
        QVERIFY(!view->isNear());
        item->setParentItem(m_page);
        QVERIFY(becomes(view, true));
        // Taken out of the window: nothing shows it. (Told a turn of the event loop later: an
        // item also leaves its window while it is destroyed.)
        const qint64 releases = NearView::releases();
        item->setParentItem(nullptr);
        QVERIFY(becomes(view, false));
        QCOMPARE(NearView::releases(), releases + 1);
        // Into another window.
        QQuickWindow second;
        second.resize(300, 300);
        second.show();
        QVERIFY(QTest::qWaitForWindowExposed(&second));
        item->setParentItem(second.contentItem());
        QVERIFY(becomes(view, true));
        item->setY(5000);
        QVERIFY(becomes(view, false));
        item.reset(); // before its window
    }

    void notWatched()
    {
        QVERIFY(makeWindow());
        NearView *view = box(0, 9000);
        QVERIFY(!looked(view));
        QSignalSpy changed(view, &NearView::nearChanged);
        QSignalSpy enabled(view, &NearView::enabledChanged);
        view->setEnabled(false);
        QVERIFY(view->isNear());
        QCOMPARE(changed.size(), 1);
        QCOMPARE(enabled.size(), 1);
        QVERIFY(looked(view)); // wherever it is
        view->setEnabled(false);
        QCOMPARE(enabled.size(), 1);
        view->setEnabled(true);
        QVERIFY(!looked(view));
        QCOMPARE(changed.size(), 2);
        // Watched again where it is shown.
        view->setEnabled(false);
        qobject_cast<QQuickItem *>(view->parent())->setY(100);
        view->setEnabled(true);
        QVERIFY(looked(view));
    }

    void notAnItem()
    {
        QObject plain;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"NearView attached to .* which is no Item"_s));
        NearView view(&plain);
        QVERIFY(view.isNear());
        NearView::checkAll();
        QVERIFY(view.isNear());
        // (Once: a second one says nothing.)
        QObject another;
        NearView second(&another);
        QVERIFY(second.isNear());
    }

    void itemsComeAndGo()
    {
        QVERIFY(makeWindow());
        QList<QPointer<NearView>> views;
        for (int i = 0; i < 200; ++i)
            views.append(box(0, i * 150));
        NearView::checkAll();
        int near = 0;
        for (const QPointer<NearView> &view : std::as_const(views))
            near += view->isNear() ? 1 : 0;
        QCOMPARE(near, 9); // tops 0 … 1200
        // Every other item goes, the page scrolls on.
        for (int i = 0; i < views.size(); i += 2)
            delete views.at(i)->parent();
        m_page->setY(-15000);
        NearView::checkAll();
        near = 0;
        for (const QPointer<NearView> &view : std::as_const(views))
            near += view && view->isNear() ? 1 : 0;
        QCOMPARE(near, 6); // tops 14400 … 16200 of those left: 14550, 14850 … 16050
        // The last ones go with their window (cleanup()).
    }

    /// What a change starts may add and remove items in the middle of a look.
    void changedInAHandler()
    {
        QVERIFY(makeWindow());
        NearView *first = box(0, 1300);
        QPointer<NearView> second = box(0, 1310);
        QPointer<NearView> third = box(0, 1320);
        QPointer<NearView> made;
        connect(
                first, &NearView::nearChanged, this,
                [&] {
                    delete second->parent();
                    delete third.data(); // the attached object alone
                    made = box(0, 1300);
                },
                Qt::SingleShotConnection);
        m_page->setY(-300);
        QVERIFY(looked(first));
        QVERIFY(!second);
        QVERIFY(!third);
        QVERIFY(made);
        QVERIFY(becomes(made, true));
    }

    /// An item destroyed while near tells nobody: what follows `near` must not run on an item
    /// half gone.
    void destroyedWhileNear()
    {
        QVERIFY(makeWindow());
        NearView *view = box(0, 100);
        QVERIFY(looked(view));
        int changes = 0;
        connect(view, &NearView::nearChanged, this, [&changes] { ++changes; });
        const qint64 releases = NearView::releases();
        delete view->parent();
        QCoreApplication::processEvents();
        QCOMPARE(changes, 0);
        QCOMPARE(NearView::releases(), releases);
        // … nor do the items of a window that goes.
        NearView *other = box(0, 200);
        QVERIFY(looked(other));
        connect(other, &NearView::nearChanged, this, [&changes] { ++changes; });
        m_page = nullptr;
        m_window.reset();
        QCoreApplication::processEvents();
        QCOMPARE(changes, 0);
    }

    void releasedIsTold()
    {
        QVERIFY(makeWindow());
        int told = 0;
        NearView::setReleased([&told] { ++told; });
        NearView *view = box(0, 100);
        NearView *far = box(0, 9000);
        QVERIFY(looked(view));
        QVERIFY(!far->isNear());
        QCOMPARE(told, 0); // becoming near, staying far: nothing let go
        m_page->setY(-9000);
        NearView::checkAll();
        QVERIFY(!view->isNear());
        QVERIFY(far->isNear());
        QCOMPARE(told, 1);
        NearView::setReleased({});
    }

    /// The application's keeper follows the releases by itself. (The only test that makes the
    /// keeper: it sets NearView's hook when it is made, init() takes it away again.)
    void keeperFollows()
    {
        MemoryKeeper *keeper = MemoryKeeper::instance();
        QVERIFY(keeper);
        QCOMPARE(MemoryKeeper::instance(), keeper);
        QVERIFY(makeWindow());
        NearView *view = box(0, 100);
        QVERIFY(looked(view));
        // (The window shown may have changed the application's state: a run of its own.)
        QTRY_VERIFY_WITH_TIMEOUT(!keeper->pending(), 10000 * Slowdown);
        const int runs = keeper->runs();
        keeper->setQuietMs(20);
        m_page->setY(-9000);
        QVERIFY(!looked(view));
        if (!brulion::platform::keepsFreedMemory()) {
            QVERIFY(!keeper->pending());
            QSKIP("This platform's allocator keeps nothing that was freed");
        }
        QVERIFY(keeper->pending());
        QTRY_VERIFY_WITH_TIMEOUT(keeper->runs() > runs, 10000 * Slowdown);
    }

    /// A list creates the rows beyond its view in the background, a piece per frame; until it
    /// places a row, the row sits at the list's top left — where a look takes it for one in view.
    /// (The pictures of a whole page were each decoded once for nothing that way.)
    void rowsStillBeingCreated()
    {
        // The pieces by hand: one per call, a look after each, as a frame between two of them.
        class Pieces : public QQmlIncubationController {
        } pieces;
        qmlRegisterUncreatableType<NearView>("NearViewTest", 1, 0, "NearView", u"attached"_s);
        QQmlEngine engine;
        engine.setIncubationController(&pieces);
        QQmlComponent component(&engine);
        component.setData(rowsQml, QUrl(u"file:///rows.qml"_s));
        std::unique_ptr<QQuickItem> list(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(list, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(400, 600);
        list->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        NearView::checkAll();
        const int atOnce = list->property("made").toInt();
        QVERIFY2(atOnce >= 2 && atOnce < 40, qPrintable(QString::number(atOnce))); // the rows in view
        int looks = 0;
        while (list->property("made").toInt() < 40 && looks < 200000) {
            pieces.incubateFor(0);
            NearView::checkAll();
            ++looks;
        }
        QCOMPARE(list->property("made").toInt(), 40);
        // (More looks than rows: rows were looked at while they were being created.)
        QVERIFY2(looks > 3 * (40 - atOnce), qPrintable(QString::number(looks)));
        QCOMPARE(list->property("wrong").toInt(), 0);
        // Placed, the rows are near by their places: tops 50 + 300 · row, within 1200.
        const auto near = [&list](int row) {
            QQuickItem *item = itemNamed(list.get(), u"row%1"_s.arg(row));
            return item && item->property("near").toBool();
        };
        NearView::checkAll();
        for (int row = 0; row < 40; ++row)
            QCOMPARE(near(row), row <= 3);
        // … and follow the list as any item.
        list->setProperty("contentY", 9000 - 50);
        NearView::checkAll();
        QVERIFY(near(30) && near(33) && !near(27) && !near(35) && !near(0));
        list.reset(); // before its window and the engine
    }

    /// What a list shows besides its rows is not waited for: its header at once, anything else
    /// in its content after a while.
    void listPartsThatAreNoRows()
    {
        NearView::setReach(testReach(600000, 50));
        qmlRegisterUncreatableType<NearView>("NearViewTest", 1, 0, "NearView", u"attached"_s);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(rowsQml, QUrl(u"file:///rows.qml"_s));
        std::unique_ptr<QQuickItem> list(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(list, qPrintable(component.errorString()));
        QQuickWindow window;
        window.resize(400, 600);
        list->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        NearView::checkAll();
        QQuickItem *header = itemNamed(list.get(), u"header"_s);
        QVERIFY(header);
        QVERIFY(header->property("near").toBool());
        // Something a view put among its rows itself: the list does not know it by its place.
        auto *stray = new QQuickItem(list->property("contentItem").value<QQuickItem *>());
        stray->setPosition(QPointF(0, 100));
        stray->setSize(QSizeF(50, 50));
        auto *view = new NearView(stray);
        QVERIFY(!looked(view));
        QVERIFY(becomes(view, true));
        list.reset();
    }

    /// As the app's pictures use it: `near` decides the source, the frames look.
    void picturesOfAPage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QImage image(64, 48, QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        const QString file = dir.filePath(u"zdjęcie.png"_s);
        QVERIFY(image.save(file));

        qmlRegisterUncreatableType<NearView>("NearViewTest", 1, 0, "NearView", u"attached"_s);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(QByteArrayLiteral(R"(
import QtQuick
import NearViewTest
Flickable {
    id: page
    property url picture
    width: 400; height: 600; contentHeight: 20000
    Repeater {
        model: 20
        Rectangle {
            required property int index
            objectName: "frame" + index
            y: index * 1000; width: 320; height: 240
            readonly property bool near: NearView.near
            readonly property int status: picture.status
            Image { id: picture; anchors.fill: parent; source: parent.near ? page.picture : "" }
        }
    }
}
)"),
                                            QUrl(u"file:///nearview.qml"_s));
        std::unique_ptr<QQuickItem> page(qobject_cast<QQuickItem *>(component.create()));
        QVERIFY2(page, qPrintable(component.errorString()));
        page->setProperty("picture", QUrl::fromLocalFile(file));
        QQuickWindow window;
        window.resize(400, 600);
        page->setParentItem(window.contentItem());
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));

        constexpr int Null = 0, Ready = 1; // Image.Null, Image.Ready
        const auto frame = [&page](int index) { return itemNamed(page.get(), u"frame%1"_s.arg(index)); };
        const auto ready = [&frame](int index) { return frame(index)->property("status").toInt() == Ready; };
        QVERIFY(frame(0) && frame(19));
        // Rows 0 and 1 (tops 0 and 1000) are within a window height, the rest hold nothing.
        QTRY_VERIFY_WITH_TIMEOUT(ready(0) && ready(1), 10000 * Slowdown);
        for (int i = 2; i < 20; ++i) {
            QVERIFY2(!frame(i)->property("near").toBool(), qPrintable(QString::number(i)));
            QCOMPARE(frame(i)->property("status").toInt(), Null);
        }
        // Scrolled to row 10: it and row 11 (its top within a window height) load, the first
        // rows let go.
        page->setProperty("contentY", 10000);
        QTRY_VERIFY_WITH_TIMEOUT(ready(10) && ready(11), 10000 * Slowdown);
        QTRY_COMPARE_WITH_TIMEOUT(frame(0)->property("status").toInt(), Null, 10000 * Slowdown);
        QCOMPARE(frame(1)->property("status").toInt(), Null);
        QVERIFY(!frame(9)->property("near").toBool());
        QVERIFY(!frame(12)->property("near").toBool());
        // Back a page and a half: rows 8 and 9 load; row 10, 1500 px down the window by now, is
        // still held (within `leave`), row 11 (2500 px) lets go.
        page->setProperty("contentY", 8500);
        QTRY_VERIFY_WITH_TIMEOUT(ready(8) && ready(9), 10000 * Slowdown);
        QVERIFY(ready(10));
        QTRY_COMPARE_WITH_TIMEOUT(frame(11)->property("status").toInt(), Null, 10000 * Slowdown);
        page.reset(); // before its window and the engine
    }
};

QTEST_MAIN(TestNearView)
#include "tst_near_view.moc"
