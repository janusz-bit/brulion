// MemoryPolicy (src/app/ui/memorypolicy.h): when the app counts as away — another program in
// front for a while, no window shown for a shorter while, never while it is in front or was never
// in front with its window shown —, what it tells then (letGo(), released()), that it is in use
// again at once, and that a hidden window gives up its scene graph and draws again when shown.
#include "memorypolicy.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

using brulion::ui::MemoryPolicy;

namespace {

constexpr int Slowdown = BRULION_TEST_SLOWDOWN;

/// A policy driven by the test (not the application's state), with short delays.
struct Policy {
    Policy()
    {
        policy.setFollowsApplication(false);
        policy.setUnfocusedMs(120 * Slowdown);
        policy.setHiddenMs(40 * Slowdown);
        policy.setStartMs(40 * Slowdown);
    }
    MemoryPolicy policy;
    QSignalSpy letGo{&policy, &MemoryPolicy::letGo};
    QSignalSpy released{&policy, &MemoryPolicy::released};
    QSignalSpy inUse{&policy, &MemoryPolicy::inUseChanged};
};

} // namespace

class TestMemoryPolicy : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void defaults()
    {
        MemoryPolicy policy;
        QVERIFY(!MemoryPolicy::switchedOff());
        QVERIFY(policy.inUse());
        QVERIFY(policy.buildAhead());
        QCOMPARE(policy.keptViews(), 2);
        QCOMPARE(policy.hiddenViewMs(), 5 * 60 * 1000);
        QCOMPARE(policy.unfocusedMs(), 60 * 1000);
        QCOMPARE(policy.hiddenMs(), 8 * 1000);
        QCOMPARE(policy.startMs(), 20 * 1000);
        QSignalSpy limits(&policy, &MemoryPolicy::limitsChanged);
        policy.setKeptViews(1);
        policy.setKeptViews(1);
        policy.setHiddenViewMs(1000);
        QCOMPARE(limits.size(), 2);
        QCOMPARE(policy.keptViews(), 1);
        QCOMPARE(policy.hiddenViewMs(), 1000);
    }

    void inFrontIsInUse()
    {
        Policy p;
        p.policy.setState(true, true);
        QTest::qWait(200 * Slowdown);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.letGo.size(), 0);
        QCOMPARE(p.policy.runs(), 0);
    }

    void neverInFrontStaysInUse()
    {
        // A window shown that was never activated (a test's X server without a window manager).
        Policy p;
        p.policy.setState(false, true);
        QTest::qWait(200 * Slowdown);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.letGo.size(), 0);
    }

    void anotherProgramInFront()
    {
        Policy p;
        p.policy.setState(true, true);
        p.policy.setState(false, true);
        QVERIFY(p.policy.inUse());
        // Not after the shorter delay of a hidden app.
        QTest::qWait(60 * Slowdown);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.letGo.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.letGo.at(0).at(0).toBool(), false);
        QVERIFY(!p.policy.inUse());
        QVERIFY(!p.policy.buildAhead());
        QCOMPARE(p.inUse.size(), 1);
        // The engine, the windows and the allocator are asked a moment later.
        QTRY_COMPARE_WITH_TIMEOUT(p.released.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.released.at(0).at(0).toBool(), false);
        // Away stays away: nothing more while nothing changes.
        QTest::qWait(200 * Slowdown);
        QCOMPARE(p.letGo.size(), 1);
        // In front again: in use at once.
        p.policy.setState(true, true);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.inUse.size(), 2);
    }

    void backInTimeIsNotAway()
    {
        Policy p;
        p.policy.setState(true, true);
        p.policy.setState(false, true);
        QTest::qWait(40 * Slowdown);
        p.policy.setState(true, true);
        QTest::qWait(250 * Slowdown);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.letGo.size(), 0);
        QCOMPARE(p.inUse.size(), 0);
    }

    void noWindowShown()
    {
        Policy p;
        p.policy.setState(true, true);
        p.policy.setState(false, false); // minimised, the tray, a phone's home screen
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.letGo.at(0).at(0).toBool(), true);
        QVERIFY(!p.policy.inUse());
        QTRY_COMPARE_WITH_TIMEOUT(p.released.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.released.at(0).at(0).toBool(), true);
    }

    void hiddenAfterUnfocused()
    {
        // Another program in front first (the hidden views go), the window hidden later (its
        // graphics too): two rounds.
        Policy p;
        p.policy.setState(true, true);
        p.policy.setState(false, true);
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.letGo.at(0).at(0).toBool(), false);
        p.policy.setState(false, false);
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 2, 2000 * Slowdown);
        QCOMPARE(p.letGo.at(1).at(0).toBool(), true);
        // Hidden and let go: not a third time, also not when it is shown without being in front.
        p.policy.setState(false, true);
        p.policy.setState(false, false);
        QTest::qWait(150 * Slowdown);
        QCOMPARE(p.letGo.size(), 2);
        QCOMPARE(p.inUse.size(), 1);
    }

    void shownAgainBeforeTheHiddenRound()
    {
        Policy p;
        p.policy.setState(true, true);
        p.policy.setState(false, true);
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 1, 2000 * Slowdown);
        p.policy.setState(false, false);
        p.policy.setState(false, true); // shown again at once (a workspace switched through)
        QTest::qWait(150 * Slowdown);
        QCOMPARE(p.letGo.size(), 1);
    }

    void startedWithoutAWindow()
    {
        // A start into the tray: never shown, never in front — away after the start's own delay.
        Policy p;
        p.policy.setStartMs(160 * Slowdown);
        p.policy.setState(false, false);
        QTest::qWait(80 * Slowdown);
        QCOMPARE(p.letGo.size(), 0); // (not after hiddenMs)
        QTRY_COMPARE_WITH_TIMEOUT(p.letGo.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.letGo.at(0).at(0).toBool(), true);
    }

    void aStartThatGetsItsWindow()
    {
        Policy p;
        p.policy.setState(false, false);
        p.policy.setState(false, true); // the window, not yet in front
        QTest::qWait(150 * Slowdown);
        QVERIFY(p.policy.inUse());
        QCOMPARE(p.letGo.size(), 0);
    }

    void letGoNow()
    {
        Policy p;
        p.policy.setState(true, true);
        p.policy.letGoNow();
        QCOMPARE(p.letGo.size(), 1);
        QCOMPARE(p.letGo.at(0).at(0).toBool(), false);
        QVERIFY(p.policy.inUse()); // asked for: the app stays in use
        QCOMPARE(p.policy.runs(), 1);
        p.policy.letGoNow(true);
        QCOMPARE(p.letGo.size(), 2);
        // One release for both (the deletions of both ran).
        QTRY_COMPARE_WITH_TIMEOUT(p.released.size(), 1, 2000 * Slowdown);
        QCOMPARE(p.released.at(0).at(0).toBool(), true);
        QTest::qWait(350 * Slowdown);
        QCOMPARE(p.released.size(), 1);
    }

    void engineStaysUsable()
    {
        // letGo() → released(): the engine collects its garbage and drops the types nothing uses
        // (a component made from data among them), and goes on working.
        QQmlEngine engine;
        Policy p;
        p.policy.setEngine(&engine);
        QQmlComponent component(&engine);
        component.setData("import QtQml\nQtObject { property var held: null\n"
                          "function make() { held = Qt.createQmlObject('import QtQml; QtObject {}', this); "
                          "held.destroy(); held = null } }",
                          QUrl(QStringLiteral("qrc:/policy/Holder.qml")));
        std::unique_ptr<QObject> holder(component.create());
        QVERIFY2(holder, qPrintable(component.errorString()));
        QVERIFY(QMetaObject::invokeMethod(holder.get(), "make"));
        p.policy.letGoNow();
        QTRY_COMPARE_WITH_TIMEOUT(p.released.size(), 1, 2000 * Slowdown);
        // (The engine is still usable afterwards.)
        QVERIFY(QMetaObject::invokeMethod(holder.get(), "make"));
    }

    void hiddenWindowDrawsAgain()
    {
        // A window nobody sees gives up its scene graph (and its graphics, where it has any) and
        // builds it again when shown: the same picture as before.
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nWindow { width: 120; height: 80; color: 'white'\n"
                          "Rectangle { x: 10; y: 10; width: 60; height: 40; color: '#c04020' }\n"
                          "Text { x: 12; y: 54; text: 'zażółć'; color: 'black' } }",
                          QUrl(QStringLiteral("qrc:/policy/Window.qml")));
        std::unique_ptr<QQuickWindow> window(qobject_cast<QQuickWindow *>(component.create()));
        QVERIFY2(window, qPrintable(component.errorString()));
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        const QImage before = window->grabWindow();
        QVERIFY(!before.isNull());
        QCOMPARE(before.pixelColor(before.width() / 3, before.height() / 3), QColor(0xc0, 0x40, 0x20));

        Policy p;
        p.policy.setEngine(&engine);
        QVERIFY(!p.policy.releasesGraphics()); // off unless main() turns it on (Linux)
        p.policy.setReleasesGraphics(true);
        QVERIFY(p.policy.releasesGraphics());
        window->hide();
        QTRY_VERIFY_WITH_TIMEOUT(!window->isExposed(), 2000 * Slowdown);
        p.policy.letGoNow(true);
        QTRY_COMPARE_WITH_TIMEOUT(p.released.size(), 1, 2000 * Slowdown);
        // What the window was is kept: persistent again for the next short hide.
        QVERIFY(window->isPersistentSceneGraph());
        QVERIFY(window->isPersistentGraphics());

        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        const QImage after = window->grabWindow();
        QCOMPARE(after.size(), before.size());
        QCOMPARE(after, before);
    }
};

QTEST_MAIN(TestMemoryPolicy)
#include "tst_memory_policy.moc"
