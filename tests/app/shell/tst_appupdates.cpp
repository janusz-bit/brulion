// AppUpdates (appupdates.h): when GitHub is asked (a local HTTP server stands in for it), when the
// message about a newer version shows and what is kept between runs. A build with the CMake option
// BRULION_UPDATE_CHECK=OFF runs only the tests that need no check (TestsWithoutTheCheck) and the
// one of such a build: never supported, never asking.
#include "appcontroller.h"
#include "appsettings.h"
#include "appupdates.h"

#include "../minihttpserver.h"

#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::AppController;
using brulion::shell::AppSettings;
using brulion::shell::AppUpdates;

namespace {

const QDateTime Noon(QDate(2026, 11, 2), QTime(12, 0));

const QSet<QByteArray> TestsWithoutTheCheck = {"checkFollowsTheCMakeOption", "unsupportedNeverAsks",
                                               "neverInABuildWithoutTheCheck"};

} // namespace

class TestAppUpdates : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<AppSettings> m_settings;
    std::unique_ptr<MiniHttpServer> m_server;
    QDateTime m_now;

    std::unique_ptr<AppUpdates> make(const QString &current = u"0.6.3"_s, bool supported = true)
    {
        AppUpdates::Options options;
        options.settings = m_settings.get();
        options.currentVersion = current;
        options.supported = supported;
        options.apiUrl = QUrl(m_server->url(u"/latest"_s));
        options.firstCheckDelayMs = 0;
        options.clock = [this] { return m_now; };
        return std::make_unique<AppUpdates>(options);
    }
    void answer(const QByteArray &tag) { m_server->bodies.insert("/latest", R"({"tag_name":")" + tag + R"("})"); }
    /// A check whose answer makes availableChanged.
    static bool checkAndWait(AppUpdates &updates)
    {
        QSignalSpy changed(&updates, &AppUpdates::availableChanged);
        updates.checkIfDue();
        return changed.wait();
    }

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.isValid());
        QFile::remove(m_dir.filePath(u"settings.ini"_s));
        m_settings = std::make_unique<AppSettings>(m_dir.filePath(u"settings.ini"_s), m_dir.path());
        m_server = std::make_unique<MiniHttpServer>();
        m_now = Noon;
        if (!AppUpdates::checkBuiltIn() && !TestsWithoutTheCheck.contains(QTest::currentTestFunction()))
            QSKIP("built with BRULION_UPDATE_CHECK=OFF: no check to test");
    }
    void cleanup() { m_settings.reset(); }

    void showsANewerRelease()
    {
        answer("v0.7.0");
        auto updates = make();
        QCOMPARE(updates->availableVersion(), QString());
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(updates->availableVersion(), u"0.7.0"_s);
        QCOMPARE(updates->releaseUrl(), QUrl(u"https://github.com/janusz-bit/brulion/releases/tag/v0.7.0"_s));
    }

    void nothingForTheSameOrAnOlderRelease_data()
    {
        QTest::addColumn<QByteArray>("tag");
        QTest::newRow("same") << QByteArray("v0.6.3");
        QTest::newRow("older") << QByteArray("v0.5.0");
    }
    void nothingForTheSameOrAnOlderRelease()
    {
        QFETCH(QByteArray, tag);
        answer(tag);
        auto updates = make();
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(updates->availableVersion(), QString());
        QVERIFY(updates->releaseUrl().isEmpty());
    }

    void asksOnceADay()
    {
        answer("v0.7.0");
        auto updates = make();
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(m_server->requests.size(), 1);

        m_now = Noon.addSecs(23 * 3600);
        updates->checkIfDue();
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1);

        m_now = Noon.addSecs(24 * 3600);
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(m_server->requests.size(), 2);
    }

    void asksAgainAfterAFailure()
    {
        auto updates = make(); // 404
        updates->checkIfDue();
        QTRY_COMPARE(m_server->requests.size(), 1);
        QTest::qWait(50);
        QCOMPARE(updates->availableVersion(), QString());
        answer("v0.7.0");
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(updates->availableVersion(), u"0.7.0"_s);
    }

    void asksWhenTheLastCheckIsInTheFuture()
    {
        answer("v0.7.0");
        m_settings->setValue(u"updates/lastCheck"_s, Noon.addYears(1).toMSecsSinceEpoch());
        auto updates = make();
        QVERIFY(checkAndWait(*updates));
    }

    void keptBetweenRunsAndGoneAfterTheUpdate()
    {
        answer("v0.7.0");
        {
            auto updates = make();
            QVERIFY(checkAndWait(*updates));
        }
        // Offline, no new check: the last answer.
        QCOMPARE(make()->availableVersion(), u"0.7.0"_s);
        // Updated to it.
        QCOMPARE(make(u"0.7.0"_s)->availableVersion(), QString());
    }

    void dismissedUntilALaterRelease()
    {
        answer("v0.7.0");
        auto updates = make();
        QVERIFY(checkAndWait(*updates));
        QSignalSpy changed(updates.get(), &AppUpdates::availableChanged);
        updates->dismiss();
        QCOMPARE(changed.size(), 1);
        QCOMPARE(updates->availableVersion(), QString());
        QCOMPARE(make()->availableVersion(), QString()); // also in the next run

        answer("v0.7.1");
        m_now = Noon.addDays(1);
        QVERIFY(checkAndWait(*updates));
        QCOMPARE(updates->availableVersion(), u"0.7.1"_s);
    }

    void turnedOff()
    {
        answer("v0.7.0");
        auto updates = make();
        QVERIFY(updates->enabled()); // on by default
        QVERIFY(checkAndWait(*updates));
        updates->setEnabled(false);
        QCOMPARE(updates->availableVersion(), QString());

        m_now = Noon.addDays(2);
        updates->checkIfDue();
        QTest::qWait(50);
        QCOMPARE(m_server->requests.size(), 1); // never asks

        QVERIFY(!make()->enabled()); // remembered

        QSignalSpy changed(updates.get(), &AppUpdates::availableChanged);
        updates->setEnabled(true); // asks at once: it is due
        QTRY_COMPARE(m_server->requests.size(), 2);
        QVERIFY(!changed.isEmpty());
        QTRY_COMPARE(updates->availableVersion(), u"0.7.0"_s);
    }

    void unsupportedNeverAsks()
    {
        answer("v0.7.0");
        m_settings->setValue(u"updates/latest"_s, u"0.7.0"_s);
        auto updates = make(u"0.6.3"_s, false);
        updates->start();
        updates->checkIfDue();
        QTest::qWait(50);
        QVERIFY(m_server->requests.isEmpty());
        QCOMPARE(updates->availableVersion(), QString());
    }

    // The test is built with the CMake option's value too (tests/app/shell/CMakeLists.txt).
    void checkFollowsTheCMakeOption() { QCOMPARE(AppUpdates::checkBuiltIn(), bool(BRULION_UPDATE_CHECK_EXPECTED)); }

    void neverInABuildWithoutTheCheck()
    {
        if (AppUpdates::checkBuiltIn())
            QSKIP("built with BRULION_UPDATE_CHECK=ON");
        answer("v0.7.0");
        m_settings->setValue(u"updates/latest"_s, u"0.7.0"_s);
        auto updates = make(); // asked to be supported
        QVERIFY(!updates->supported());
        updates->start();
        updates->checkIfDue();
        QTest::qWait(50);
        QVERIFY(m_server->requests.isEmpty());
        QCOMPARE(updates->availableVersion(), QString());

        // Not even --update-available shows the message.
        AppController::Options options;
        options.settingsFile = m_dir.filePath(u"controller.ini"_s);
        options.dataDir = m_dir.path();
        options.checkForUpdates = true;
        options.arguments = {u"brulion"_s, u"--update-available"_s, u"99.0.0"_s};
        AppController dev(options);
        QVERIFY(!dev.updates()->supported());
        QCOMPARE(dev.updates()->availableVersion(), QString());
    }

    void startAsksAfterTheDelay()
    {
        answer("v0.7.0");
        auto updates = make();
        updates->start();
        QVERIFY(m_server->requests.isEmpty()); // not during the start itself
        QTRY_COMPARE(updates->availableVersion(), u"0.7.0"_s);
    }

    void controllerOffersItOnlyWhereAsked()
    {
        const QString settings = m_dir.filePath(u"controller.ini"_s);
        AppController::Options options;
        options.settingsFile = settings;
        options.dataDir = m_dir.path();
        AppController plain(options);
        QVERIFY(!plain.updates()->supported());

        options.arguments = {u"brulion"_s, u"--update-available"_s, u"99.0.0"_s};
        AppController dev(options);
        QVERIFY(dev.updates()->supported());
        QCOMPARE(dev.updates()->availableVersion(), u"99.0.0"_s);
        dev.updates()->checkIfDue(); // never asks GitHub
        QCOMPARE(dev.settings()->value(u"updates/latest"_s), QVariant());
    }
};

QTEST_GUILESS_MAIN(TestAppUpdates)
#include "tst_appupdates.moc"
