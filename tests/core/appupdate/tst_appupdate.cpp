// Tests of the appupdate module: plain versions and their order, GitHub's answer about a release
// (drafts, pre-releases, malformed answers) and the checker against a tiny HTTP server on 127.0.0.1.
#include "appupdate/appupdate.h"

#include "../../app/minihttpserver.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::appupdate;

class TestAppUpdate : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void normalized_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("plain") << u"0.6.3"_s << u"0.6.3"_s;
        QTest::newRow("tag") << u"v0.6.3"_s << u"0.6.3"_s;
        QTest::newRow("leading zeros") << u"v01.02.003"_s << u"1.2.3"_s;
        QTest::newRow("pre-release") << u"v0.7.0-rc.1"_s << QString();
        QTest::newRow("two parts") << u"1.2"_s << QString();
        QTest::newRow("four parts") << u"1.2.3.4"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("words") << u"najnowsza"_s << QString();
        QTest::newRow("capital v") << u"V1.2.3"_s << QString();
        QTest::newRow("spaces") << u" 1.2.3"_s << QString();
        QTest::newRow("new line") << u"1.2.3\n"_s << QString();
        QTest::newRow("path") << u"1.2.3/../../evil"_s << QString();
        QTest::newRow("too long") << u"1.2.1234567890"_s << QString();
        QTest::newRow("arabic digits") << u"١.٢.٣"_s << QString();
    }
    void normalized()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(normalizedVersion(text), expected);
    }

    void newer_data()
    {
        QTest::addColumn<QString>("candidate");
        QTest::addColumn<QString>("current");
        QTest::addColumn<bool>("expected");
        QTest::newRow("patch") << u"0.6.4"_s << u"0.6.3"_s << true;
        QTest::newRow("minor") << u"0.7.0"_s << u"0.6.9"_s << true;
        QTest::newRow("major") << u"1.0.0"_s << u"0.99.99"_s << true;
        QTest::newRow("numbers, not text") << u"0.10.0"_s << u"0.9.0"_s << true;
        QTest::newRow("same") << u"0.6.3"_s << u"0.6.3"_s << false;
        QTest::newRow("older") << u"0.6.2"_s << u"0.6.3"_s << false;
        QTest::newRow("no candidate") << QString() << u"0.6.3"_s << false;
        QTest::newRow("no current") << u"0.6.3"_s << QString() << false;
        QTest::newRow("pre-release") << u"0.7.0-rc.1"_s << u"0.6.3"_s << false;
    }
    void newer()
    {
        QFETCH(QString, candidate);
        QFETCH(QString, current);
        QFETCH(bool, expected);
        QCOMPARE(isNewerVersion(candidate, current), expected);
    }

    void fromRelease_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QString>("expected");
        QTest::newRow("release") << QByteArray(R"({"tag_name":"v0.7.0","draft":false,"prerelease":false})") << u"0.7.0"_s;
        QTest::newRow("draft") << QByteArray(R"({"tag_name":"v0.7.0","draft":true})") << QString();
        QTest::newRow("pre-release") << QByteArray(R"({"tag_name":"v0.7.0","prerelease":true})") << QString();
        QTest::newRow("pre-release tag") << QByteArray(R"({"tag_name":"v0.7.0-rc.1"})") << QString();
        QTest::newRow("no tag") << QByteArray(R"({"name":"Brulion"})") << QString();
        QTest::newRow("tag not text") << QByteArray(R"({"tag_name":7})") << QString();
        QTest::newRow("list") << QByteArray(R"([{"tag_name":"v0.7.0"}])") << QString();
        QTest::newRow("not JSON") << QByteArray("<html>Zażółć</html>") << QString();
        QTest::newRow("cut") << QByteArray(R"({"tag_name":"v0.7)") << QString();
        QTest::newRow("empty") << QByteArray() << QString();
    }
    void fromRelease()
    {
        QFETCH(QByteArray, json);
        QFETCH(QString, expected);
        QCOMPARE(versionFromRelease(json), expected);
    }

    void pageComesFromTheVersion()
    {
        QCOMPARE(releasePageUrl(u"0.7.0"_s), QUrl(u"https://github.com/janusz-bit/brulion/releases/tag/v0.7.0"_s));
    }

    void checks()
    {
        MiniHttpServer server;
        server.bodies.insert("/latest", R"({"tag_name":"v9.8.7"})");
        UpdateChecker checker{QUrl(server.url(u"/latest"_s))};
        QSignalSpy checked(&checker, &UpdateChecker::checked);
        checker.check();
        QVERIFY(checker.isChecking());
        checker.check(); // ignored: one request
        QVERIFY(checked.wait());
        QCOMPARE(checked.first().first().toString(), u"9.8.7"_s);
        QVERIFY(!checker.isChecking());
        QCOMPARE(server.requests.size(), 1);
    }

    void fails_data()
    {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<qint64>("maxBytes");
        QTest::addColumn<QString>("detail");
        QTest::newRow("not JSON") << QByteArray("<html>Zażółć</html>") << qint64(1000) << u"no version in the answer"_s;
        QTest::newRow("too large") << QByteArray(200, ' ') << qint64(100) << u"more than 100 bytes"_s;
    }
    void fails()
    {
        QFETCH(QByteArray, body);
        QFETCH(qint64, maxBytes);
        QFETCH(QString, detail);
        MiniHttpServer server;
        server.bodies.insert("/latest", body);
        UpdateChecker checker{QUrl(server.url(u"/latest"_s))};
        checker.setMaxBytes(maxBytes);
        QSignalSpy checked(&checker, &UpdateChecker::checked);
        QSignalSpy failed(&checker, &UpdateChecker::failed);
        checker.check();
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), detail);
        QVERIFY(checked.isEmpty());
        QVERIFY(!checker.isChecking());
    }

    void failsOnAnHttpError()
    {
        MiniHttpServer server; // 404 for every path
        UpdateChecker checker{QUrl(server.url(u"/latest"_s))};
        QSignalSpy failed(&checker, &UpdateChecker::failed);
        checker.check();
        QVERIFY(failed.wait());
        QCOMPARE(failed.first().first().toString(), u"HTTP 404"_s);
    }
};

QTEST_GUILESS_MAIN(TestAppUpdate)
#include "tst_appupdate.moc"
