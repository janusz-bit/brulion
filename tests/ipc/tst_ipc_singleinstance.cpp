// SingleInstance: a second launch hands its message to the running instance; stale sockets of a
// crashed instance do not block a new one; different app data folders do not see each other.
// The messages (appmessage.h): "open {json}" from brulion-cli is delivered and parsed, malformed
// lines are refused; probing for a running app sends nothing.
#include "appmessage.h"
#include "singleinstance.h"

#include <QJsonArray>

#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QUuid>

using namespace Qt::StringLiterals;
using brulion::ipc::SingleInstance;

class TestSingleInstance : public QObject {
    Q_OBJECT

    static QString uniqueKey() { return u"brulion-test-"_s + QUuid::createUuid().toString(QUuid::Id128).left(12); }

private Q_SLOTS:
    void keys()
    {
        QCOMPARE(SingleInstance::keyFor(u"/home/ala/.local/share/Brulion"_s),
                 SingleInstance::keyFor(u"/home/ala/.local/share/Brulion/"_s));
        QVERIFY(SingleInstance::keyFor(u"/a"_s) != SingleInstance::keyFor(u"/b"_s));
        QVERIFY(QFileInfo(SingleInstance::keyFor(u"/home/żółw"_s)).fileName().startsWith(u"brulion-"_s));
        // In the user's private runtime folder when there is one (not world-writable /tmp,
        // where another user could squat the name).
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (!runtime.isEmpty())
            QCOMPARE(QFileInfo(SingleInstance::keyFor(u"/a"_s)).absolutePath(), QDir(runtime).absolutePath());
    }

    void secondLaunchActivatesFirst()
    {
        const QString key = uniqueKey();
        SingleInstance primary(key);
        QVERIFY(!primary.sendToPrimary("activate", 200)); // nobody there yet
        QVERIFY(primary.listen());
        QVERIFY(primary.isListening());
        QSignalSpy messages(&primary, &SingleInstance::messageReceived);

        SingleInstance second(key);
        QVERIFY(second.sendToPrimary("activate"));
        QTRY_COMPARE(messages.size(), 1);
        QCOMPARE(messages.first().first().toByteArray(), QByteArray("activate"));
        QVERIFY(second.sendToPrimary("capture"));
        QTRY_COMPARE(messages.size(), 2);
        QCOMPARE(messages.last().first().toByteArray(), QByteArray("capture"));
    }

    void simultaneousLaunchDoesNotStealTheSocket()
    {
        // Two launches at the same moment: both find no primary, both try to listen. The one
        // that loses must not delete the winner's socket (that would leave two primaries and
        // the first one unreachable).
        const QString key = uniqueKey();
        SingleInstance first(key);
        QVERIFY(first.listen());
        QSignalSpy messages(&first, &SingleInstance::messageReceived);
        SingleInstance second(key);
        QVERIFY(!second.listen());
        QVERIFY(!second.isListening());
        QVERIFY(second.sendToPrimary("activate")); // what main() does then
        QTRY_COMPARE(messages.size(), 1);
        SingleInstance third(key);
        QVERIFY(third.sendToPrimary("today"));
        QTRY_COMPARE(messages.size(), 2);
        QCOMPARE(messages.last().first().toByteArray(), QByteArray("today"));
    }

    void openMessageFromTheCommandLine()
    {
        const QString key = uniqueKey();
        SingleInstance app(key);
        SingleInstance cli(key);
        QVERIFY(!cli.isPrimaryRunning(100));
        QVERIFY(app.listen());
        QSignalSpy messages(&app, &SingleInstance::messageReceived);
        QVERIFY(cli.isPrimaryRunning()); // probing sends nothing
        const QJsonObject route{{u"view"_s, u"page"_s}, {u"path"_s, u"Projekty/Żółw.md"_s}, {u"line"_s, 4}};
        QVERIFY(cli.sendToPrimary(brulion::ipc::openMessage(route, u"/home/ala/Notatki"_s)));
        QTRY_COMPARE(messages.size(), 1);
        const auto message = brulion::ipc::parseMessage(messages.first().first().toByteArray());
        QVERIFY(message);
        QCOMPARE(message->verb, QByteArray("open"));
        QCOMPARE(message->payload.value(u"route"_s).toObject(), route);
        QCOMPARE(message->payload.value(u"vault"_s).toString(), u"/home/ala/Notatki"_s);
    }

    void parseMessages_data()
    {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<bool>("valid");
        QTest::addColumn<QByteArray>("verb");
        QTest::newRow("verb") << QByteArray("activate\n") << true << QByteArray("activate");
        QTest::newRow("verb with json") << QByteArray(R"(open {"route":{"view":"journal"}})") << true
                                        << QByteArray("open");
        QTest::newRow("future verb") << QByteArray("state-get {}") << true << QByteArray("state-get");
        QTest::newRow("empty") << QByteArray("  ") << false << QByteArray();
        QTest::newRow("upper case") << QByteArray("OPEN") << false << QByteArray();
        QTest::newRow("not an object") << QByteArray("open [1,2]") << false << QByteArray();
        QTest::newRow("broken json") << QByteArray("open {\"route\":") << false << QByteArray();
        QTest::newRow("binary") << QByteArray("op\xff"
                                              "en")
                                << false << QByteArray();
    }
    void parseMessages()
    {
        QFETCH(QByteArray, line);
        QFETCH(bool, valid);
        QFETCH(QByteArray, verb);
        const auto message = brulion::ipc::parseMessage(line);
        QCOMPARE(message.has_value(), valid);
        if (message)
            QCOMPARE(message->verb, verb);
        QCOMPARE(brulion::ipc::encodeMessage("today"), QByteArray("today"));
        QCOMPARE(brulion::ipc::parseMessage(brulion::ipc::encodeMessage("open", {{u"a"_s, u"ż"_s}}))->payload,
                 (QJsonObject{{u"a"_s, u"ż"_s}}));
    }

    void staleSocketIsReplaced()
    {
        const QString key = uniqueKey();
        {
            // Simulate a crashed instance: a server that goes away without cleaning up is what
            // removeServer() handles; a closed server must not block a new primary either.
            QLocalServer old;
            QVERIFY(old.listen(key));
        }
        SingleInstance next(key);
        QVERIFY(next.listen());
        QSignalSpy messages(&next, &SingleInstance::messageReceived);
        SingleInstance other(key);
        QVERIFY(other.sendToPrimary("activate"));
        QTRY_COMPARE(messages.size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSingleInstance)
#include "tst_ipc_singleinstance.moc"
