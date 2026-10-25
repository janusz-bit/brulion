// SingleInstance: a second launch hands its message to the running instance; stale sockets of a
// crashed instance do not block a new one; different app data folders do not see each other.
// Only in a folder of this user that nobody else can change: never the shared temp folder (a bare
// key), a folder others can write to or a symbolic link — listen() says why and the app runs
// without it; a lock that cannot be made is no running primary. The messages (appmessage.h):
// "open {json}" from brulion-cli is delivered and parsed, malformed lines are refused; probing for
// a running app sends nothing.
#include "appmessage.h"
#include "controlprotocol.h"
#include "singleinstance.h"

#include <QJsonArray>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalServer>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;
using brulion::ipc::SingleInstance;

class TestSingleInstance : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir; // 0700: a private folder, as the runtime folder is

    static QString uniqueName() { return u"brulion-test-"_s + QUuid::createUuid().toString(QUuid::Id128).left(12); }
    QString uniqueKey() const { return m_dir.filePath(uniqueName()); }
#ifdef Q_OS_UNIX
    static bool chmod(const QString &path, mode_t mode)
    {
        return ::chmod(QFile::encodeName(path).constData(), mode) == 0;
    }
#endif

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_dir.isValid()); }

    void keys()
    {
        QCOMPARE(SingleInstance::keyFor(u"/home/ala/.local/share/Brulion"_s),
                 SingleInstance::keyFor(u"/home/ala/.local/share/Brulion/"_s));
        QVERIFY(SingleInstance::keyFor(u"/a"_s) != SingleInstance::keyFor(u"/b"_s));
        QVERIFY(QFileInfo(SingleInstance::keyFor(u"/home/żółw"_s)).fileName().startsWith(u"brulion-"_s));
        // In the user's private runtime folder when there is one (not world-writable /tmp,
        // where another user could squat the name); on Windows the user's own temp folder (the
        // named pipe's name and the lock file's folder).
#ifdef Q_OS_WIN
        QCOMPARE(QFileInfo(SingleInstance::keyFor(u"/a"_s)).absolutePath(), QDir(QDir::tempPath()).absolutePath());
#else
        const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        if (!runtime.isEmpty())
            QCOMPARE(QFileInfo(SingleInstance::keyFor(u"/a"_s)).absolutePath(), QDir(runtime).absolutePath());
#endif
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

    // Only in a folder no other user can change: in a shared one (the temp folder, where
    // QLocalServer puts a bare name when there is no runtime folder) another user could create the
    // name first — Brulion would not start — or receive what a second launch and brulion-cli send.
    void onlyInAPrivateFolder()
    {
        // A bare name (no runtime folder): never the temp folder, and not "another instance".
        const QString bare = uniqueName();
        SingleInstance app(bare);
        QVERIFY(!app.listen());
        QVERIFY(!app.isListening());
        QVERIFY(app.errorString().contains(u"runtime folder"_s));
        QVERIFY(!QFileInfo::exists(QDir::temp().filePath(bare)));
        QVERIFY(!QFileInfo::exists(QDir::temp().filePath(bare + u".lock"_s)));
        QVERIFY(!SingleInstance(bare).sendToPrimary("activate", 200));

#ifndef Q_OS_UNIX
        QSKIP("Folder modes and symbolic links are Unix checks: on Windows the socket is a named pipe "
              "with an access list of the user (its lock file in the user's temp folder)");
#else
        // A folder the group or others can write to (the shared /tmp is 1777).
        const QString shared = m_dir.filePath(u"wspólny"_s);
        QVERIFY(QDir().mkpath(shared));
        QVERIFY(chmod(shared, 01777));
        SingleInstance inShared(shared + u"/brulion-test"_s);
        QVERIFY(!inShared.listen());
        QVERIFY(inShared.errorString().contains(u"Others can write"_s));
        QCOMPARE(QDir(shared).entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot), QStringList());
        QVERIFY(chmod(shared, 0770));
        QVERIFY(!SingleInstance(shared + u"/brulion-test"_s).listen());

        // A symbolic link to a private folder: whoever owns the link decides where it points.
        const QString target = m_dir.filePath(u"prywatny"_s);
        QVERIFY(QDir().mkpath(target));
        QVERIFY(chmod(target, 0700));
        QVERIFY(QFile::link(target, m_dir.filePath(u"dowiązanie"_s)));
        SingleInstance throughLink(m_dir.filePath(u"dowiązanie/brulion-test"_s));
        QVERIFY(!throughLink.listen());
        QVERIFY(throughLink.errorString().contains(u"symbolic link"_s));

        // The private folder itself (also readable by the group: nobody else can add a file).
        for (const mode_t mode : {mode_t(0700), mode_t(0750)}) {
            QVERIFY(chmod(target, mode));
            SingleInstance primary(target + u"/brulion-test"_s);
            QVERIFY2(primary.listen(), qPrintable(primary.errorString()));
            QVERIFY(primary.errorString().isEmpty());
            QSignalSpy messages(&primary, &SingleInstance::messageReceived);
            QSignalSpy refused(&primary, &SingleInstance::refused);
            SingleInstance second(target + u"/brulion-test"_s);
            QVERIFY(second.isPrimaryRunning());
            QVERIFY(second.sendToPrimary("today"));
            QVERIFY(second.errorString().isEmpty());
            QTRY_COMPARE(messages.size(), 1);
            QCOMPARE(refused.size(), 0); // a connection of this user
        }
#endif
    }

    // A lock file that cannot be made (a folder the user cannot write to) is no running primary:
    // listen() says why instead of sending the caller to wait for an answer that never comes.
    void lockThatCannotBeMade()
    {
#ifndef Q_OS_UNIX
        QSKIP("A folder the user cannot write to is made with Unix modes here");
#else
        if (::geteuid() == 0)
            QSKIP("root writes to any folder");
        const QString folder = m_dir.filePath(u"tylko-do-odczytu"_s);
        QVERIFY(QDir().mkpath(folder));
        QVERIFY(chmod(folder, 0500));
        SingleInstance app(folder + u"/brulion-test"_s);
        QVERIFY(!app.listen());
        QVERIFY(!app.isListening());
        QVERIFY(!app.errorString().isEmpty());
        QVERIFY(chmod(folder, 0700));
        QVERIFY2(app.listen(), qPrintable(app.errorString())); // tried anew
        QVERIFY(app.errorString().isEmpty());
#endif
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
