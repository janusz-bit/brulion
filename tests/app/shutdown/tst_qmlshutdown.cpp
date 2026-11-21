// Quitting while views are still being created (src/app/ui/qmlshutdown.h). Each case runs this
// program again as a child ("--quit-scene"): a window whose views are built ahead in an
// asynchronous Loader, as the app's kept views are — a list of days whose delegates hold a block
// editor's list of rows, the journal's shape — is incubated by hand, a fixed number of slices
// (QQmlIncubationController::incubateFor(0): one small step each, the same steps on every machine),
// then the app quits the way main() does. Without releaseQmlWindows() Qt crashes for some of those
// steps as the application object is destroyed (QTBUG-87228's delegates handed to it: SIGSEGV in
// QQmlDelegateModelItem::destroyObjectLater, the AppImage job's QML smoke test on 2026-11-20);
// with it — or with only main()'s last guard, deleteQmlLeftovers() — no step crashes and no QML
// item is left for that destructor.
#include "qmlshutdown.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlIncubationController>
#include <QScopeGuard>
#include <QTest>
#include <QTimer>

#include <cstdio>

using namespace Qt::StringLiterals;

namespace {

/// The journal's shape (JournalsView: days in a ListView, a day's BlockEditor with its rows in a
/// ListView, two items between the day and that list) built ahead; "nested" one more level of
/// lists (a list of rows whose rows hold lists): two rounds of hand-overs.
QByteArray sceneQml(const QString &scene)
{
    // (Small, and no cache buffers: few slices to step through.)
    QByteArray qml = R"(import QtQuick
Window {
    width: 320; height: 300; visible: true
    Loader {
        anchors.fill: parent
        asynchronous: true
        sourceComponent: ListView {
            cacheBuffer: 0
            model: 10
            delegate: Item {
                width: 300; height: 140
                Column {
                    Text { text: "Dzień " + index }
                    Item {
                        width: 300; height: 120
                        INNER
                    }
                }
            }
        }
    }
}
)";
    const QByteArray rows = R"(ListView {
                width: 300; height: 60; cacheBuffer: 0
                model: 3
                delegate: Rectangle { width: 300; height: 30; Text { text: "Wiersz " + index + " zażółć" } }
            })";
    const QByteArray nested = R"(ListView {
                width: 300; height: 100; cacheBuffer: 0
                model: 2
                delegate: Item { width: 300; height: 70; ROWS }
            })";
    return qml.replace("INNER", scene == u"nested" ? QByteArray(nested).replace("ROWS", rows) : rows);
}

/// Incubates only when told (the window's own controller would run it between frames).
class HandIncubation : public QQmlIncubationController { };

/// The child: `--quit-scene <scene> <slices> <none|release|leftovers>`. Prints how much was still
/// incubating when it quit and how many QML items were left for the application's destructor.
int quitScene(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QString scene = QString::fromLocal8Bit(argv[2]);
    const int slices = QByteArray(argv[3]).toInt();
    const QByteArray mode = argv[4];
    // As main(): the last guard before the application object goes.
    const auto last = qScopeGuard([&mode] {
        if (mode != "none")
            brulion::ui::deleteQmlLeftovers();
        std::printf("left %d\n", brulion::ui::qmlItemsLeftOnApplication());
        std::fflush(stdout);
    });
    QQmlApplicationEngine engine;
    HandIncubation incubation;
    engine.setIncubationController(&incubation);
    engine.loadData(sceneQml(scene), QUrl(u"file:///quitscene.qml"_s));
    if (engine.rootObjects().isEmpty())
        return 2;
    for (int i = 0; i < slices && incubation.incubatingObjectCount() > 0; ++i)
        incubation.incubateFor(0);
    std::printf("incubating %d\n", incubation.incubatingObjectCount());
    std::fflush(stdout);
    QTimer::singleShot(0, &app, &QCoreApplication::quit);
    const int code = QGuiApplication::exec();
    if (mode == "release")
        brulion::ui::releaseQmlWindows(&engine);
    return code;
}

struct Run {
    bool crashed = false;
    int exitCode = -1;
    int incubating = -1; ///< still incubating when it quit
    int left = -1;       ///< QML items left for the application's destructor
    QString output;
};

Run runScene(const QString &scene, int slices, const QString &mode)
{
    QProcess child;
    child.setProcessChannelMode(QProcess::MergedChannels);
    child.start(QCoreApplication::applicationFilePath(), {u"--quit-scene"_s, scene, QString::number(slices), mode});
    Run run;
    if (!child.waitForFinished(60'000 * BRULION_TEST_SLOWDOWN)) {
        child.kill();
        child.waitForFinished();
        run.crashed = true;
        run.output = u"(timed out) "_s;
    }
    run.crashed = run.crashed || child.exitStatus() == QProcess::CrashExit;
    run.exitCode = child.exitCode();
    run.output += QString::fromLocal8Bit(child.readAll());
    for (const QString &line : run.output.split(u'\n')) {
        if (line.startsWith(u"incubating "))
            run.incubating = line.mid(11).toInt();
        else if (line.startsWith(u"left "))
            run.left = line.mid(5).toInt();
    }
    return run;
}

/// More than enough slices for either scene: the runs stop at the first one that quits after the
/// incubation ended.
constexpr int kMaxSlices = 200;

} // namespace

class TestQmlShutdown : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void quitWhileIncubating_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addColumn<QString>("mode");
        for (const QString &scene : {u"journal"_s, u"nested"_s}) {
            QTest::addRow("%s, windows released", qPrintable(scene)) << scene << u"release"_s;
            QTest::addRow("%s, only the last guard", qPrintable(scene)) << scene << u"leftovers"_s;
        }
    }

    /// At every step of the incubation: no crash, nothing left for the application's destructor.
    void quitWhileIncubating()
    {
        QFETCH(QString, scene);
        QFETCH(QString, mode);
        int runs = 0;
        for (int slices = 0; slices <= kMaxSlices; ++slices) {
            const Run run = runScene(scene, slices, mode);
            ++runs;
            QVERIFY2(!run.crashed && run.exitCode == 0,
                     qPrintable(u"quit after %1 slices: %2"_s.arg(slices).arg(run.output)));
            QVERIFY2(run.incubating >= 0, qPrintable(run.output));
            QVERIFY2(run.left == 0, qPrintable(u"quit after %1 slices: %2"_s.arg(slices).arg(run.output)));
            if (run.incubating == 0)
                break;
        }
        QVERIFY2(runs > 5, "the scene was built at once: nothing incubated asynchronously");
    }

    /// The same scenes without either: Qt's crash (QTBUG-87228's delegates deleted by the application
    /// object's destructor) is reached at some step — the scenes show what the release prevents. If
    /// a Qt version stops crashing here, the workaround may be unnecessary there: this fails to say so.
    void unreleasedQuitCrashes_data()
    {
        QTest::addColumn<QString>("scene");
        QTest::addRow("journal") << u"journal"_s;
        QTest::addRow("nested") << u"nested"_s;
    }
    void unreleasedQuitCrashes()
    {
        QFETCH(QString, scene);
        bool crashed = false;
        int itemsLeft = 0;
        for (int slices = 0; slices <= kMaxSlices && !crashed; ++slices) {
            const Run run = runScene(scene, slices, u"none"_s);
            crashed = run.crashed;
            // (Printed before the application object's destructor, where it crashes.)
            itemsLeft = std::max(itemsLeft, run.left);
            if (!run.crashed && run.incubating == 0)
                break;
        }
        QVERIFY2(itemsLeft > 0, "no delegate was handed to the application object");
        QVERIFY2(crashed, "no step crashed without the release (Qt fixed? see qmlshutdown.h)");
    }
};

int main(int argc, char *argv[])
{
    if (argc >= 5 && qstrcmp(argv[1], "--quit-scene") == 0)
        return quitScene(argc, argv);
    QCoreApplication app(argc, argv);
    TestQmlShutdown test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_qmlshutdown.moc"
