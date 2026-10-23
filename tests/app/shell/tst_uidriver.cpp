// The UI driver (src/app/shell/uidriver.h) on a small Qt Quick scene, offscreen: key sequences as
// people write them ("Ctrl+K", "Escape", "Shift+Tab", "Down, Down, Return", malformed ones), the
// key strokes of Polish text, typing into a text field through the keyboard's way (shortcuts first)
// and straight to the focused item, a window Shortcut reached only the keyboard's way, mouse
// clicks, double clicks, long presses and drags, finger taps and long presses, touchpad scrolling
// of a Flickable, the same gestures played from timers (play(): no nested event loop) and stopped
// when the window goes away, and finding items (type names, labels, shown, on screen, topmost
// first, scrolled into view).
#include "uidriver.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTest>

using namespace Qt::StringLiterals;
namespace ud = brulion::uidriver;

namespace {

const char *const kScene = R"QML(
import QtQuick
Window {
    id: win
    width: 400; height: 300; visible: true
    property int clicks: 0
    property int rightClicks: 0
    property int doubleClicks: 0
    property int longPresses: 0
    property int taps: 0
    property int fingerLongPresses: 0
    property int shortcuts: 0
    property string lastModifiers: ""
    Shortcut { sequence: "Ctrl+K"; onActivated: win.shortcuts++ }
    TextInput {
        id: field; objectName: "field"
        x: 10; y: 10; width: 200; height: 30
        focus: true
    }
    MouseArea {
        objectName: "button"
        x: 10; y: 50; width: 100; height: 40
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) win.rightClicks++; else win.clicks++
            win.lastModifiers = (mouse.modifiers & Qt.ControlModifier ? "ctrl" : "") + (mouse.modifiers & Qt.ShiftModifier ? "+shift" : "")
        }
        onDoubleClicked: win.doubleClicks++
        Text { objectName: "buttonLabel"; anchors.centerIn: parent; text: "Zapisz zmiany" }
    }
    Rectangle {
        objectName: "tapTarget"
        x: 120; y: 50; width: 100; height: 40
        property string label: "Dotknij"
        TapHandler {
            longPressThreshold: 0.5
            onTapped: win.taps++
            onLongPressed: win.longPresses++
        }
    }
    Rectangle {
        id: draggable; objectName: "draggable"
        x: 10; y: 100; width: 40; height: 40; color: "red"
        DragHandler {}
    }
    Rectangle {
        objectName: "hidden"
        x: 300; y: 10; width: 40; height: 40
        visible: false
        property string text: "Ukryty żółw"
    }
    Flickable {
        id: flick; objectName: "flick"
        x: 220; y: 100; width: 170; height: 150
        clip: true
        contentHeight: 2000
        contentWidth: 400 // wider rows, but it flicks only up and down
        flickableDirection: Flickable.VerticalFlick
        Column {
            Repeater {
                model: 50
                Text { required property int index; height: 40; text: "Wiersz " + index }
            }
            // Beyond the view's width: no flicking sideways reaches it.
            Text { x: 230; height: 40; text: "Poza zasięgiem" }
        }
    }
    // A page that stops at its ends with a switch at its bottom (the settings page), a strip
    // covering its top (a bar over a page), and a list whose content fits it.
    property int toggles: 0
    Flickable {
        id: page; objectName: "page"
        x: 60; y: 100; width: 150; height: 100
        clip: true
        contentHeight: 300
        boundsBehavior: Flickable.StopAtBounds
        MouseArea {
            objectName: "toggle"
            y: 250; width: 150; height: 40
            onClicked: win.toggles++
        }
    }
    MouseArea {
        objectName: "cover"
        x: 60; y: 100; width: 150; height: 20
        z: 1
    }
    Flickable {
        objectName: "fits"
        x: 60; y: 205; width: 150; height: 45
        contentHeight: 30
        flickableDirection: Flickable.AutoFlickIfNeeded
        Text { text: "Krótka lista" }
    }
    // A row of tools wider than the window, scrolled sideways (the phone's editing toolbar).
    Flickable {
        id: tools; objectName: "tools"
        x: 10; y: 255; width: 200; height: 40
        clip: true
        contentWidth: toolRow.width
        flickableDirection: Flickable.HorizontalFlick
        Row {
            id: toolRow
            Repeater {
                model: 30
                Text { required property int index; width: 44; height: 40; text: "Narzędzie " + index }
            }
        }
    }
}
)QML";

} // namespace

class TestUiDriver : public QObject {
    Q_OBJECT
    QQmlEngine *m_engine = nullptr;
    QQuickWindow *m_window = nullptr;

    QQuickItem *named(const QString &name) const { return ud::shownNamed(m_window->contentItem(), name); }
    int prop(const char *name) const { return m_window->property(name).toInt(); }

private Q_SLOTS:
    void init()
    {
        m_engine = new QQmlEngine(this);
        QQmlComponent component(m_engine);
        component.setData(kScene, QUrl(u"qrc:/scene.qml"_s));
        QObject *root = component.create();
        QVERIFY2(root, qPrintable(component.errorString()));
        m_window = qobject_cast<QQuickWindow *>(root);
        QVERIFY(m_window);
        m_window->requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QTRY_VERIFY(m_window->isActive());
    }
    void cleanup()
    {
        delete m_window;
        m_window = nullptr;
        delete m_engine;
        m_engine = nullptr;
    }

    void keySequences_data()
    {
        QTest::addColumn<QString>("sequence");
        QTest::addColumn<QList<int>>("keys");
        QTest::addColumn<QList<int>>("modifiers");
        QTest::addColumn<QStringList>("texts");
        const int ctrl = int(Qt::ControlModifier);
        const int shift = int(Qt::ShiftModifier);
        QTest::newRow("Ctrl+K") << u"Ctrl+K"_s << QList<int>{Qt::Key_K} << QList<int>{ctrl} << QStringList{QString()};
        QTest::newRow("lower case") << u"ctrl+k"_s << QList<int>{Qt::Key_K} << QList<int>{ctrl}
                                    << QStringList{QString()};
        QTest::newRow("Escape") << u"Escape"_s << QList<int>{Qt::Key_Escape} << QList<int>{0} << QStringList{QString()};
        QTest::newRow("Esc") << u"Esc"_s << QList<int>{Qt::Key_Escape} << QList<int>{0} << QStringList{QString()};
        QTest::newRow("Return") << u"Return"_s << QList<int>{Qt::Key_Return} << QList<int>{0} << QStringList{u"\r"_s};
        QTest::newRow("Shift+Tab is Backtab")
                << u"Shift+Tab"_s << QList<int>{Qt::Key_Backtab} << QList<int>{shift} << QStringList{QString()};
        QTest::newRow("Ctrl+Shift+C") << u"Ctrl+Shift+C"_s << QList<int>{Qt::Key_C} << QList<int>{ctrl | shift}
                                      << QStringList{QString()};
        QTest::newRow("several") << u"Down, Down, Return"_s << QList<int>{Qt::Key_Down, Qt::Key_Down, Qt::Key_Return}
                                 << QList<int>{0, 0, 0} << QStringList{QString(), QString(), u"\r"_s};
        QTest::newRow("Ctrl+plus") << u"Ctrl++"_s << QList<int>{Qt::Key_Plus} << QList<int>{ctrl}
                                   << QStringList{QString()};
        QTest::newRow("Ctrl+comma") << u"Ctrl+,"_s << QList<int>{Qt::Key_Comma} << QList<int>{ctrl}
                                    << QStringList{QString()};
        QTest::newRow("a letter") << u"a"_s << QList<int>{Qt::Key_A} << QList<int>{0} << QStringList{u"a"_s};
        QTest::newRow("Shift+a") << u"Shift+a"_s << QList<int>{Qt::Key_A} << QList<int>{shift} << QStringList{u"A"_s};
        QTest::newRow("Polish letter") << u"ż"_s << QList<int>{0x17B} << QList<int>{0} << QStringList{u"ż"_s};
        QTest::newRow("Alt+F4") << u"Alt+F4"_s << QList<int>{Qt::Key_F4} << QList<int>{int(Qt::AltModifier)}
                                << QStringList{QString()};
        QTest::newRow("Delete, PageDown") << u"Delete, PageDown"_s << QList<int>{Qt::Key_Delete, Qt::Key_PageDown}
                                          << QList<int>{0, 0} << QStringList{QString(), QString()};
        QTest::newRow("Space") << u"Space"_s << QList<int>{Qt::Key_Space} << QList<int>{0} << QStringList{u" "_s};
        QTest::newRow("Back") << u"Back"_s << QList<int>{Qt::Key_Back} << QList<int>{0} << QStringList{QString()};
    }
    void keySequences()
    {
        QFETCH(QString, sequence);
        QFETCH(QList<int>, keys);
        QFETCH(QList<int>, modifiers);
        QFETCH(QStringList, texts);
        QString error;
        const auto strokes = ud::parseKeySequence(sequence, &error);
        QVERIFY2(strokes, qPrintable(error));
        QCOMPARE(strokes->size(), keys.size());
        for (qsizetype i = 0; i < keys.size(); ++i) {
            QCOMPARE(strokes->at(i).key, keys.at(i));
            QCOMPARE(int(strokes->at(i).modifiers), modifiers.at(i));
            QCOMPARE(strokes->at(i).text, texts.at(i));
        }
    }

    void badKeySequences_data()
    {
        QTest::addColumn<QString>("sequence");
        QTest::newRow("empty") << QString();
        QTest::newRow("blank") << u"   "_s;
        QTest::newRow("modifier alone") << u"Ctrl+"_s;
        QTest::newRow("unknown modifier") << u"Hyper+K"_s;
        QTest::newRow("unknown key") << u"Escapee"_s;
        QTest::newRow("one bad of two") << u"Down, Bogus"_s;
    }
    void badKeySequences()
    {
        QFETCH(QString, sequence);
        QString error;
        QVERIFY(!ud::parseKeySequence(sequence, &error));
        QVERIFY(!error.isEmpty());
    }

    void strokesOfText()
    {
        const QList<ud::KeyStroke> strokes = ud::strokesFor(u"Aż ę\n\t😀\r\n"_s);
        QCOMPARE(strokes.size(), 8);
        QCOMPARE(strokes.at(0).key, int(Qt::Key_A));
        QCOMPARE(strokes.at(0).modifiers, Qt::ShiftModifier);
        QCOMPARE(strokes.at(0).text, u"A"_s);
        QCOMPARE(strokes.at(1).key, 0x17B); // Ż
        QCOMPARE(strokes.at(1).modifiers, Qt::NoModifier);
        QCOMPARE(strokes.at(1).text, u"ż"_s);
        QCOMPARE(strokes.at(2).key, int(Qt::Key_Space));
        QCOMPARE(strokes.at(3).key, 0x118); // Ę
        QCOMPARE(strokes.at(4).key, int(Qt::Key_Return));
        QCOMPARE(strokes.at(5).key, int(Qt::Key_Tab));
        QCOMPARE(strokes.at(6).key, 0);
        QCOMPARE(strokes.at(6).text, u"😀"_s);
        QCOMPARE(strokes.at(7).key, int(Qt::Key_Return)); // "\r\n" is one Return
        // A backslash and an "n" written out (docs/CLI.md `app type`) are those two characters.
        const QList<ud::KeyStroke> written = ud::strokesFor(uR"(a\nb)"_s);
        QCOMPARE(written.size(), 4);
        QCOMPARE(written.at(1).text, u"\\"_s);
        QCOMPARE(written.at(2).text, u"n"_s);
        QVERIFY(written.at(1).key != int(Qt::Key_Return) && written.at(2).key != int(Qt::Key_Return));
    }

    void typing_data()
    {
        QTest::addColumn<int>("delivery");
        QTest::addColumn<bool>("played");
        QTest::newRow("keyboard, at once") << int(ud::KeyDelivery::System) << false;
        QTest::newRow("focus item, at once") << int(ud::KeyDelivery::FocusItem) << false;
        QTest::newRow("keyboard, from timers") << int(ud::KeyDelivery::System) << true;
    }
    void typing()
    {
        QFETCH(int, delivery);
        QFETCH(bool, played);
        const QString text = u"Zażółć gęślą jaźń ĄĆĘŁŃÓŚŹŻ 123 [[Żółw]]"_s;
        const ud::Gesture gesture = ud::typeText(m_window, text, ud::KeyDelivery(delivery), 0, 0);
        if (played) {
            bool done = false;
            bool completed = false;
            ud::play(this, m_window, gesture, [&](bool c) {
                done = true;
                completed = c;
            });
            QVERIFY(!done); // nothing happens until the event loop runs
            QTRY_VERIFY(done);
            QVERIFY(completed);
        } else {
            ud::run(gesture);
        }
        QCOMPARE(named(u"field"_s)->property("text").toString(), text);
    }

    void shortcutsOnlyTheKeyboardsWay()
    {
        // Not in the text field: it claims Ctrl+K itself (delete to the end of the line), as a
        // text editor of the app does.
        m_window->contentItem()->forceActiveFocus();
        const auto ctrlK = ud::parseKeySequence(u"Ctrl+K"_s);
        QVERIFY(ctrlK);
        QVERIFY(ud::deliverKey(m_window, ctrlK->first(), ud::KeyDelivery::System));
        QCOMPARE(prop("shortcuts"), 1);
        // Escape is not handled by anything here.
        QVERIFY(!ud::sendKey(m_window, Qt::Key_Escape, Qt::NoModifier, QString(), 0));
    }

    void mouse()
    {
        QQuickItem *button = named(u"button"_s);
        QVERIFY(button);
        ud::run(ud::mouseClick(m_window, ud::pointIn(button)));
        QCOMPARE(prop("clicks"), 1);
        ud::run(ud::mouseClick(m_window, ud::pointIn(button), Qt::RightButton,
                               Qt::ControlModifier | Qt::ShiftModifier));
        QCOMPARE(prop("rightClicks"), 1);
        QCOMPARE(m_window->property("lastModifiers").toString(), u"ctrl+shift"_s);
        // Clicks a second apart are no double click; this one is.
        ud::run(ud::mouseDoubleClick(m_window, ud::pointIn(button)));
        QCOMPARE(prop("doubleClicks"), 1);
        QCOMPARE(prop("clicks"), 2); // the first click of the pair
        // A long press with the mouse.
        QQuickItem *tap = named(u"tapTarget"_s);
        ud::run(ud::mouseLongPress(m_window, ud::pointIn(tap), 800));
        QCOMPARE(prop("longPresses"), 1);
        QCOMPARE(prop("taps"), 0);
        ud::run(ud::mouseClick(m_window, ud::pointIn(tap)));
        QCOMPARE(prop("taps"), 1);
        // A drag moves the item by the distance.
        QQuickItem *box = named(u"draggable"_s);
        const QPointF before = box->position();
        ud::run(ud::mouseDrag(m_window, ud::pointIn(box), ud::pointIn(box) + QPointF(100, 60)));
        QVERIFY2(std::abs(box->x() - before.x() - 100) < 2 && std::abs(box->y() - before.y() - 60) < 2,
                 qPrintable(u"moved to %1,%2"_s.arg(box->x()).arg(box->y())));
    }

    void touch()
    {
        QQuickItem *tap = named(u"tapTarget"_s);
        ud::run(ud::touchTap(m_window, ud::pointIn(tap)));
        QCOMPARE(prop("taps"), 1);
        ud::run(ud::touchTap(m_window, ud::pointIn(tap), 900, 100));
        QCOMPARE(prop("longPresses"), 1);
        QCOMPARE(prop("taps"), 1);
        QQuickItem *box = named(u"draggable"_s);
        const QPointF before = box->position();
        ud::run(ud::touchDrag(m_window, ud::pointIn(box), QPointF(80, 0), 12, 16, 100, 100));
        QVERIFY2(box->x() - before.x() > 60, qPrintable(u"moved to %1"_s.arg(box->x())));
    }

    void scrolling()
    {
        QQuickItem *flick = named(u"flick"_s);
        QVERIFY(flick);
        const auto settled = [flick] {
            qreal last = -1;
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < 3000) {
                QTest::qWait(100);
                const qreal y = flick->property("contentY").toReal();
                if (y == last)
                    return y;
                last = y;
            }
            return last;
        };
        ud::run(ud::wheel(m_window, ud::pointIn(flick), QPointF(0, 300)));
        const qreal down = settled();
        QVERIFY2(std::abs(down - 300) < 2, qPrintable(QString::number(down)));
        ud::run(ud::wheel(m_window, ud::pointIn(flick), QPointF(0, -100)));
        const qreal up = settled();
        QVERIFY2(std::abs(up - 200) < 2, qPrintable(QString::number(up)));
    }

    // A finger moves content only where a Flickable under it can move that way: at the end of a
    // page that stops there (or where the content fits) a finger dragged over a switch toggles it
    // (Qt hands the press on) — what canScroll() tells the remote control beforehand.
    void fingerScrollsOnlyWhereContentMoves()
    {
        QQuickItem *page = named(u"page"_s);
        QQuickItem *toggle = named(u"toggle"_s);
        QVERIFY(page && toggle);
        QVERIFY(!ud::onScreen(m_window, toggle));
        QVERIFY(ud::scrollIntoView(m_window, toggle));
        QCOMPARE(page->property("contentY").toReal(), 200.0); // at its end
        const QPointF at = ud::pointIn(toggle);
        QCOMPARE(ud::pressTargetAt(m_window, at), toggle);
        QCOMPARE(ud::flickablesAt(m_window, at), QList<QQuickItem *>{page});
        QVERIFY(!ud::canScroll(page, QPointF(0, 10))); // further down: no
        QVERIFY(ud::canScroll(page, QPointF(0, -10)));
        QVERIFY(!ud::canScroll(page, QPointF(10, 0))); // its content is as wide as it
        // What the check prevents: a finger moving up (the content would go down) taps it.
        const int drag = QGuiApplication::styleHints()->startDragDistance() + 3;
        ud::run(ud::touchDrag(m_window, at, QPointF(0, -drag), 6, 16, 120, 300));
        QCOMPARE(prop("toggles"), 1);
        QCOMPARE(page->property("contentY").toReal(), 200.0);
        // Down (the content goes up): the page takes the drag, no tap.
        ud::run(ud::touchDrag(m_window, at, QPointF(0, drag), 6, 16, 120, 300));
        QCOMPARE(prop("toggles"), 1);
        // A finger scroll moves the content by exactly what was asked, past the threshold (or
        // from its first move while the page is still `moving`: a drag that did not move it
        // can leave it so).
        const auto scroll = [&](qreal dy) {
            ud::run(ud::touchScroll(m_window, at, QPointF(0, dy), !page->property("moving").toBool()));
        };
        scroll(-30);
        QCOMPARE(prop("toggles"), 1);
        QCOMPARE(page->property("contentY").toReal(), 170.0);
        scroll(1);
        QCOMPARE(page->property("contentY").toReal(), 171.0);
        scroll(-21);
        QCOMPARE(page->property("contentY").toReal(), 150.0);
        QCOMPARE(prop("toggles"), 1);
        QVERIFY(ud::canScroll(page, QPointF(0, 10)));
        // At the top: not up any more.
        page->setProperty("contentY", 0);
        QVERIFY(!ud::canScroll(page, QPointF(0, -10)));
        QVERIFY(ud::canScroll(page, QPointF(0, 10)));
        // Not interactive: never.
        page->setProperty("interactive", false);
        QVERIFY(!ud::canScroll(page, QPointF(0, 10)));
        page->setProperty("interactive", true);
        // A bar over the page takes the press: no flickable under the finger there.
        QQuickItem *cover = named(u"cover"_s);
        QCOMPARE(ud::pressTargetAt(m_window, ud::pointIn(cover)), cover);
        QVERIFY(ud::flickablesAt(m_window, ud::pointIn(cover)).isEmpty());
        // Content that fits its list (flicking only if needed): nothing moves either way.
        QQuickItem *fits = named(u"fits"_s);
        QCOMPARE(ud::flickablesAt(m_window, ud::pointIn(fits)), QList<QQuickItem *>{fits});
        QVERIFY(!ud::canScroll(fits, QPointF(0, 10)));
        QVERIFY(!ud::canScroll(fits, QPointF(0, -10)));
        // A list that may be dragged beyond its ends moves at its end too (and springs back).
        QQuickItem *flick = named(u"flick"_s);
        QCOMPARE(flick->property("contentY").toReal(), 0.0);
        QVERIFY(ud::canScroll(flick, QPointF(0, -10)));
        QVERIFY(!ud::canScroll(flick, QPointF(10, 0))); // it flicks only up and down
        // Nothing takes a press at an empty spot of the window.
        QCOMPARE(ud::pressTargetAt(m_window, QPointF(395, 295)), nullptr);
        QVERIFY(ud::flickablesAt(m_window, QPointF(395, 295)).isEmpty());
    }

    void playedFromTimersAndStoppedWhenTheWindowGoes()
    {
        QQuickItem *button = named(u"button"_s);
        bool completed = false;
        int calls = 0;
        ud::play(this, m_window, ud::mouseClick(m_window, ud::pointIn(button)), [&](bool c) {
            ++calls;
            completed = c;
        });
        QTRY_COMPARE(calls, 1);
        QVERIFY(completed);
        QCOMPARE(prop("clicks"), 1);
        // A long gesture whose window is closed meanwhile.
        ud::play(this, m_window, ud::touchTap(m_window, ud::pointIn(button), 400, 100), [&](bool c) {
            ++calls;
            completed = c;
        });
        QTest::qWait(50);
        delete m_window;
        m_window = nullptr;
        QTRY_COMPARE(calls, 2);
        QVERIFY(!completed);
    }

    void findingItems()
    {
        QQuickItem *label = named(u"buttonLabel"_s);
        QVERIFY(label);
        QCOMPARE(ud::typeName(label), u"QQuickText"_s);
        QCOMPARE(ud::labelOf(label), u"Zapisz zmiany"_s);
        QCOMPARE(ud::labelOf(named(u"tapTarget"_s)), u"Dotknij"_s); // a "label" property
        QVERIFY(ud::isA(label, "QQuickText"));
        QVERIFY(ud::hasAncestor(label, "QQuickMouseArea"));
        QVERIFY(ud::shown(label));
        QVERIFY(ud::onScreen(m_window, label));
        // Hidden items: not shown, found only when asked for.
        const auto hiddenTurtle = [](QQuickItem *item) { return ud::labelOf(item).contains(u"żółw"_s); };
        QVERIFY(!ud::topmost(m_window, hiddenTurtle));
        QCOMPARE(ud::matching(m_window, hiddenTurtle, false).size(), 1);
        // Topmost first: the label lies on its MouseArea, both match.
        const QList<QQuickItem *> both = ud::matching(m_window, [](QQuickItem *item) {
            return item->objectName() == u"button" || item->objectName() == u"buttonLabel";
        });
        QCOMPARE(both.size(), 2);
        QCOMPARE(both.first()->objectName(), u"buttonLabel"_s);
        QVERIFY(ud::itemsAt(m_window, ud::pointIn(label)).contains(u"QQuickText/buttonLabel"_s));
        QCOMPARE(ud::openPopups(m_window).size(), 0);
        QCOMPARE(ud::openMenus(m_window), 0);
        // A row far down the list, scrolled into view.
        QQuickItem *flick = named(u"flick"_s);
        QQuickItem *row = ud::findShown(flick, [](QQuickItem *item) { return ud::labelOf(item) == u"Wiersz 1"; });
        QVERIFY(row);
        QQuickItem *far = nullptr;
        for (QQuickItem *item : ud::allItems(flick)) {
            if (ud::labelOf(item) == u"Wiersz 40")
                far = item;
        }
        QVERIFY(far);
        QVERIFY(!ud::onScreen(m_window, far));
        QCOMPARE(ud::enclosingFlickable(far), flick);
        QVERIFY(ud::scrollIntoView(m_window, far));
        QVERIFY(ud::onScreen(m_window, far));
        // A flickable that only flicks up and down is never moved sideways, even for an item
        // beyond its width (a user could not scroll to it either).
        QQuickItem *beyond = nullptr;
        for (QQuickItem *item : ud::allItems(flick)) {
            if (ud::labelOf(item) == u"Poza zasięgiem")
                beyond = item;
        }
        QVERIFY(beyond);
        QVERIFY(!ud::scrollIntoView(m_window, beyond));
        QCOMPARE(flick->property("contentX").toReal(), 0.0);
        // A tool far to the right of a row that scrolls sideways (remote control: `app click` on
        // the phone toolbar's "Więcej" clicked beside the window and said it had clicked).
        QQuickItem *tools = named(u"tools"_s);
        QQuickItem *tool = nullptr;
        for (QQuickItem *item : ud::allItems(tools)) {
            if (ud::labelOf(item) == u"Narzędzie 25")
                tool = item;
        }
        QVERIFY(tool);
        QVERIFY(!ud::onScreen(m_window, tool));
        QVERIFY(ud::scrollIntoView(m_window, tool));
        QVERIFY(ud::onScreen(m_window, tool));
        QCOMPARE(tools->property("contentY").toReal(), 0.0);
    }
};

QTEST_MAIN(TestUiDriver)
#include "tst_uidriver.moc"
