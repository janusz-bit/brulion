// SingleInstance: a second launch hands its message to the running instance; stale sockets of a
// crashed instance do not block a new one; different app data folders do not see each other.
#include "singleinstance.h"

#include <QDir>
#include <QFileInfo>
#include <QLocalServer>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

using namespace Qt::StringLiterals;
using brulion::shell::SingleInstance;

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
#include "tst_singleinstance.moc"
