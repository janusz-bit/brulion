// ViewportObservers (src/app/ui/viewportobservers.h): the empty texts of an offscreen window let go
// of QQuickItem::ItemObservesViewport, so that a scroll step no longer walks down to them; texts
// that draw only their lines in view keep it. `scrollStep` measures a scroll step of a page whose
// rows have empty texts, with and without the look (`-perf -perfcounter instructions:u`,
// docs/PERFORMANCE.md "Scrolling: empty texts no longer walked").
#include "viewportobservers.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <functional>
#include <memory>

using namespace Qt::StringLiterals;
using brulion::ui::ViewportObservers;

namespace {

/// An item that counts how often Qt tells it that something above it moved — what a scroll step
/// walks down to.
class MoveCounter : public QQuickItem {
    Q_OBJECT

public:
    int moves = 0;

protected:
    void itemChange(ItemChange change, const ItemChangeData &data) override
    {
        if (change == ItemTransformHasChanged)
            ++moves;
        QQuickItem::itemChange(change, data);
    }
};

/// A page of `rows` rows under a header, every row with an empty text, a short one and a counter
/// — as a block row with an empty marker and a picture frame with an empty caption.
QByteArray pageQml(int rows)
{
    return QByteArrayLiteral(R"(
import QtQuick
import ViewportTest
Flickable {
    width: 400; height: 600
    contentHeight: column.height
    Column {
        id: column
        Text { objectName: "long"; width: 400; height: 40; text: "x".repeat(20000) }
        TextEdit { objectName: "longEdit"; width: 400; height: 40; text: "y".repeat(20000) }
        Repeater {
            model: )") + QByteArray::number(rows) + QByteArrayLiteral(R"(
            Item {
                width: 400; height: 60
                Text { objectName: "empty" }
                Text { y: 20; text: "Zażółć gęślą jaźń" }
                Text { objectName: "caption"; y: 40; text: "" }
                MoveCounter { objectName: "counter"; width: 10; height: 10 }
            }
        }
    }
}
)");
}

/// A list that makes its rows as it scrolls (no cache), each with an empty text.
const char *const listQml = R"(
import QtQuick
ListView {
    width: 400; height: 600; model: 200; cacheBuffer: 0
    delegate: Item {
        width: 400; height: 60
        Text { objectName: "empty" }
        Text { y: 20; text: "wiersz " + index }
    }
}
)";

/// The items of `root`'s tree (itself too) for which `pred` holds.
QList<QQuickItem *> itemsWhere(QQuickItem *root, const std::function<bool(QQuickItem *)> &pred)
{
    QList<QQuickItem *> found;
    std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
        if (pred(item))
            found.append(item);
        const QList<QQuickItem *> children = item->childItems();
        for (QQuickItem *child : children)
            walk(child);
    };
    walk(root);
    return found;
}

QStringList observers(QQuickItem *root)
{
    QStringList names;
    const QList<QQuickItem *> items = itemsWhere(root, [](QQuickItem *item) {
        return item->flags().testFlag(QQuickItem::ItemObservesViewport);
    });
    for (QQuickItem *item : items)
        names.append(item->objectName().isEmpty() ? QString::fromLatin1(item->metaObject()->className())
                                                  : item->objectName());
    return names;
}

int moves(QQuickItem *root)
{
    int sum = 0;
    const QList<QQuickItem *> counters = itemsWhere(root, [](QQuickItem *item) {
        return qobject_cast<MoveCounter *>(item) != nullptr;
    });
    for (QQuickItem *counter : counters)
        sum += static_cast<MoveCounter *>(counter)->moves;
    return sum;
}

/// A window showing `qml` (offscreen) under its content item.
struct Scene {
    QQmlEngine engine;
    QQuickWindow window;
    std::unique_ptr<QQuickItem> root;

    explicit Scene(const QByteArray &qml)
    {
        QQmlComponent component(&engine);
        component.setData(qml, QUrl(u"qrc:/scene.qml"_s));
        root.reset(qobject_cast<QQuickItem *>(component.create()));
        if (!root)
            qWarning() << component.errors();
        window.resize(400, 600);
        if (root)
            root->setParentItem(window.contentItem());
    }
    ~Scene() { root.reset(); }
};

} // namespace

class TestViewportObservers : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        qmlRegisterType<MoveCounter>("ViewportTest", 1, 0, "MoveCounter");
        QVERIFY(!ViewportObservers::switchedOff());
    }

    void emptyTextsLetGoAfterAFrame()
    {
        Scene scene(pageQml(40));
        QVERIFY(scene.root);
        // Qt's start: every empty text observes the viewport.
        QCOMPARE(observers(scene.root.get()).count(u"empty"_s), 40);
        ViewportObservers::watch(&scene.window);
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_COMPARE(observers(scene.root.get()), (QStringList{u"long"_s, u"longEdit"_s}));
    }

    void aSweepTellsHowManyLetGo()
    {
        Scene scene(pageQml(10));
        QVERIFY(scene.root);
        QCOMPARE(ViewportObservers::sweep(scene.window.contentItem()), 20); // "empty" and "caption"
        QCOMPARE(ViewportObservers::sweep(scene.window.contentItem()), 0);
        QCOMPARE(observers(scene.root.get()), (QStringList{u"long"_s, u"longEdit"_s}));
    }

    void aScrollStepWalksOnlyToTheTextsThatUseTheViewport()
    {
        Scene scene(pageQml(40));
        QVERIFY(scene.root);
        ViewportObservers::sweep(scene.window.contentItem());
        const int made = moves(scene.root.get()); // (told while the rows were added)
        for (int step = 1; step <= 10; ++step)
            scene.root->setProperty("contentY", step * 30);
        QCOMPARE(moves(scene.root.get()) - made, 0);
    }

    void anItemThatUsesTheViewportIsStillTold()
    {
        // A row's item that draws by the viewport (as a long text does) is told of every step.
        Scene scene(pageQml(40));
        QVERIFY(scene.root);
        const QList<QQuickItem *> counters = itemsWhere(scene.root.get(), [](QQuickItem *item) {
            return item->objectName() == u"counter"_s;
        });
        QCOMPARE(counters.size(), 40);
        auto *watcher = static_cast<MoveCounter *>(counters.at(20));
        watcher->setFlag(QQuickItem::ItemObservesViewport);
        QCOMPARE(ViewportObservers::sweep(scene.window.contentItem()), 80);
        const int made = watcher->moves;
        const int allMade = moves(scene.root.get());
        for (int step = 1; step <= 10; ++step)
            scene.root->setProperty("contentY", step * 30);
        QCOMPARE(watcher->moves - made, 10);
        QCOMPARE(moves(scene.root.get()) - allMade, 10); // … and no other row
    }

    void withoutTheLookAScrollStepWalksEveryRow()
    {
        // The premise of the other cases: Qt alone walks down to the rows of empty texts.
        Scene scene(pageQml(40));
        QVERIFY(scene.root);
        const int made = moves(scene.root.get());
        for (int step = 1; step <= 10; ++step)
            scene.root->setProperty("contentY", step * 30);
        QCOMPARE(moves(scene.root.get()) - made, 40 * 10);
    }

    void aTextGivenALongTextObservesAgain()
    {
        Scene scene(pageQml(3));
        QVERIFY(scene.root);
        ViewportObservers::sweep(scene.window.contentItem());
        const QList<QQuickItem *> empties = itemsWhere(scene.root.get(), [](QQuickItem *item) {
            return item->objectName() == u"empty"_s;
        });
        QCOMPARE(empties.size(), 3);
        QQuickItem *empty = empties.first();
        empty->setProperty("text", QString(20000, u'ż'));
        QVERIFY(empty->flags().testFlag(QQuickItem::ItemObservesViewport));
        // … and a scroll step reaches it again, past the switched-off paths.
        QCOMPARE(ViewportObservers::sweep(scene.window.contentItem()), 0);
        QVERIFY(empty->flags().testFlag(QQuickItem::ItemObservesViewport));
        QCOMPARE(observers(scene.root.get()).size(), 3);
    }

    void rowsMadeWhileScrollingLetGo()
    {
        Scene scene(listQml);
        QVERIFY(scene.root);
        ViewportObservers::watch(&scene.window);
        ViewportObservers::watch(&scene.window); // once, however often asked
        scene.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&scene.window));
        QTRY_VERIFY(observers(scene.root.get()).isEmpty());
        for (int step = 1; step <= 20; ++step) {
            scene.root->setProperty("contentY", step * 300);
            QTRY_VERIFY2(observers(scene.root.get()).isEmpty(),
                         qPrintable(observers(scene.root.get()).join(u' ')));
        }
    }

    void scrollStep_data()
    {
        QTest::addColumn<bool>("swept");
        QTest::newRow("Qt alone") << false;
        QTest::newRow("empty texts let go") << true;
    }

    void scrollStep()
    {
        QFETCH(bool, swept);
        Scene scene(pageQml(300));
        QVERIFY(scene.root);
        if (swept)
            ViewportObservers::sweep(scene.window.contentItem());
        int y = 0;
        QBENCHMARK {
            y = (y + 30) % 6000;
            scene.root->setProperty("contentY", y);
        }
    }

    // Last: from here on every window shown is watched.
    void theInstanceWatchesEveryWindowShown()
    {
        Scene before(pageQml(5)); // shown before the instance was made
        QVERIFY(before.root);
        before.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&before.window));
        QVERIFY(ViewportObservers::instance());
        QCOMPARE(ViewportObservers::instance(), ViewportObservers::instance());
        Scene after(pageQml(5));
        QVERIFY(after.root);
        after.window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&after.window));
        before.window.requestUpdate();
        QTRY_COMPARE(observers(before.root.get()).size(), 2);
        QTRY_COMPARE(observers(after.root.get()).size(), 2);
    }
};

QTEST_MAIN(TestViewportObservers)
#include "tst_viewport_observers.moc"
