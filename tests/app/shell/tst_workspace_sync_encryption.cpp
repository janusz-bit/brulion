// Workspace.sync with an end-to-end encrypted Nextcloud folder (docs/SYNC-ENCRYPTION.md) against
// the core's in-memory server: a new encrypted notebook made in an empty folder (its key file, the
// README for older versions, the recovery key given once), its notes synced with no plain name or
// byte on the server, another device opening it with the password or the recovery key, a wrong
// password, a short one and a folder with files refused, a device without the key locked (nothing
// syncs), and the connection test telling an encrypted folder.
#include "workspace.h"
#include "workspacesync.h"

#include "fakewebdavserver.h"
#include "synctest.h"

#include "sync/encryption.h"
#include "sync/encryptionkeys.h"
#include "sync/scheduler.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::shell::NextcloudSettings;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceSync;
using synctest::FakeWebDavServer;
namespace encryption = brulion::sync::encryption;

namespace {

const QString kFolder = u"Notatki (zaszyfrowane)"_s;
const QString kPassword = u"długie hasło notatnika 2026"_s;

/// What setUpEncryption() answered.
struct Answer {
    bool ok = false;
    QString folder;
    QByteArray key;
    QString recovery;
    QString failure;
};

} // namespace

class TestWorkspaceSyncEncryption : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    synctest::TestClock m_clock;

    std::unique_ptr<Workspace> open(brulion::sync::HttpTransport *transport, const QMap<QString, QString> &files = {})
    {
        const QString root = m_tmp.filePath(u"vault-%1"_s.arg(++m_n));
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            synctest::writeFileBehind(root, it.key(), it.value().toUtf8());
        QDir().mkpath(root);
        auto ws = std::make_unique<Workspace>();
        ws->setClock(m_clock.fn());
        WorkspaceSync *sync = ws->sync();
        sync->setSyncthingDiscovery(false);
        sync->setTransport(transport);
        sync->setAutomatic(false);
        sync->setOnlineCheck([] { return true; });
        if (!ws->open(root, root + u"-state"_s))
            qWarning() << "cannot open" << root;
        ws->finishIndexing();
        return ws;
    }

    static NextcloudSettings account(const FakeWebDavServer &server, const QString &folder = kFolder)
    {
        NextcloudSettings n;
        n.enabled = true;
        n.url = server.options().origin;
        n.user = server.options().username;
        n.password = server.options().password;
        n.folder = folder;
        return n;
    }

    static Answer setUp(WorkspaceSync *sync, const FakeWebDavServer &server, const QString &secret,
                        const QString &folder = kFolder)
    {
        Answer answer;
        bool done = false;
        const auto made = QObject::connect(sync, &WorkspaceSync::encryptionKeyMade, sync,
                                           [&](const QString &f, const QByteArray &key, const QString &recovery) {
                                               answer = {true, f, key, recovery, {}};
                                               done = true;
                                           });
        const auto failed = QObject::connect(sync, &WorkspaceSync::encryptionFailed, sync, [&](const QString &why) {
            answer.failure = why;
            done = true;
        });
        sync->setUpEncryption(server.options().origin, server.options().username, server.options().password, folder,
                              secret);
        if (!synctest::waitFor([&] { return done; }, 60000))
            answer.failure = u"(no answer)"_s;
        QObject::disconnect(made);
        QObject::disconnect(failed);
        return answer;
    }

    static bool syncAndWait(WorkspaceSync *sync)
    {
        if (!sync->scheduler())
            return false;
        bool done = false;
        auto *scheduler = sync->scheduler();
        const QMetaObject::Connection c = QObject::connect(scheduler, &brulion::sync::SyncScheduler::statusChanged,
                                                           scheduler, [&] {
                                                               if (!scheduler->isRunning())
                                                                   done = true;
                                                           });
        sync->syncNow();
        const bool ok = synctest::waitFor([&] { return done && !scheduler->isRunning(); });
        QObject::disconnect(c);
        return ok;
    }

    /// The account's settings with the encryption setUp() answered.
    static NextcloudSettings encrypted(const FakeWebDavServer &server, const Answer &answer)
    {
        NextcloudSettings n = account(server, answer.folder);
        n.encrypted = true;
        n.notebookKey = answer.key;
        return n;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init() { m_clock.now = QDateTime(QDate(2026, 11, 28), QTime(10, 0)); }

    void aNewEncryptedNotebookAndASecondDevice()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto laptop = open(&server, {{u"Dziennik/2026-11-28.md"_s, u"- Zażółć gęślą jaźń\n"_s}});
        WorkspaceSync *sync = laptop->sync();
        QSignalSpy busy(sync, &WorkspaceSync::encryptionBusyChanged);
        const Answer made = setUp(sync, server, kPassword);
        QVERIFY2(made.ok, qPrintable(made.failure));
        QCOMPARE(busy.size(), 2);
        QVERIFY(!sync->encryptionBusy());
        QCOMPARE(made.folder, kFolder);
        QCOMPARE(made.key.size(), encryption::NotebookKeySize);
        QVERIFY(encryption::parseRecoveryKey(made.recovery));
        QVERIFY(server.bytes(kFolder + u"/.brulion/encryption.json"_s));
        QVERIFY(server.text(kFolder + u"/README.txt"_s)->contains(u"encrypted"_s));

        sync->setNextcloud(encrypted(server, made));
        QVERIFY(!sync->locked());
        QVERIFY(syncAndWait(sync));
        QCOMPARE(sync->state(), u"idle"_s);
        const encryption::Keys keys = encryption::Keys::fromNotebookKey(made.key);
        const QMap<QString, QString> onServer = server.files(kFolder);
        QCOMPARE(onServer.size(), 3); // the note (encrypted), the key file, the README
        for (auto it = onServer.cbegin(); it != onServer.cend(); ++it) {
            QVERIFY2(!it.key().contains(u"Dziennik"_s), qPrintable(it.key()));
            QVERIFY(!it.value().contains(u"Zażółć"_s));
        }
        QVERIFY(encryption::decryptPath(keys, *encryption::encryptPath(keys, u"Dziennik/2026-11-28.md"_s)));

        // The phone: opened by the password, then by the recovery key — the same notebook key.
        auto phone = open(&server);
        const Answer byPassword = setUp(phone->sync(), server, kPassword);
        QVERIFY2(byPassword.ok, qPrintable(byPassword.failure));
        QCOMPARE(byPassword.key, made.key);
        QVERIFY(byPassword.recovery.isEmpty()); // (shown once, where it was made)
        const Answer byRecovery = setUp(phone->sync(), server, made.recovery.toUpper());
        QVERIFY2(byRecovery.ok, qPrintable(byRecovery.failure));
        QCOMPARE(byRecovery.key, made.key);
        phone->sync()->setNextcloud(encrypted(server, byPassword));
        QVERIFY(syncAndWait(phone->sync()));
        QCOMPARE(phone->readFile(u"Dziennik/2026-11-28.md"_s), u"- Zażółć gęślą jaźń\n"_s);
        QVERIFY(phone->readFile(u"README.txt"_s).isEmpty());
    }

    void refusals_data()
    {
        QTest::addColumn<QString>("secret");
        QTest::addColumn<bool>("filesThere");
        QTest::addColumn<bool>("keyThere");
        QTest::addColumn<QString>("message");
        QTest::newRow("wrong password") << u"zupełnie inne hasło"_s << false << true << u"does not open"_s;
        QTest::newRow("short password for a new one") << u"krótkie"_s << false << false << u"at least 12"_s;
        QTest::newRow("a folder with files") << kPassword << true << false << u"has files already"_s;
        QTest::newRow("no password") << u"  "_s << false << false << u"Enter the notebook"_s;
    }

    void refusals()
    {
        QFETCH(QString, secret);
        QFETCH(bool, filesThere);
        QFETCH(bool, keyThere);
        QFETCH(QString, message);
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        if (filesThere)
            server.putText(kFolder + u"/Jawna notatka.md"_s, u"jawna"_s);
        auto ws = open(&server);
        if (keyThere)
            QVERIFY(setUp(ws->sync(), server, kPassword).ok);
        const Answer answer = setUp(ws->sync(), server, secret);
        QVERIFY(!answer.ok);
        QVERIFY2(answer.failure.contains(message), qPrintable(answer.failure));
        QCOMPARE(server.text(kFolder + u"/Jawna notatka.md"_s).has_value(), filesThere); // left alone
    }

    void aDeviceWithoutTheKeyIsLocked()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server, {{u"a.md"_s, u"a\n"_s}});
        NextcloudSettings n = account(server);
        n.encrypted = true;
        ws->sync()->setNextcloud(n);
        QVERIFY(ws->sync()->locked());
        QCOMPARE(ws->sync()->state(), u"locked"_s);
        QVERIFY(ws->sync()->statusText().contains(u"encrypted"_s));
        QVERIFY(!ws->sync()->scheduler());
        ws->sync()->syncNow();
        QTest::qWait(50);
        QCOMPARE(server.requests().size(), 0);
    }

    void theConnectionTestTellsAnEncryptedFolder()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server);
        QVERIFY(setUp(ws->sync(), server, kPassword).ok);
        QSignalSpy tested(ws->sync(), &WorkspaceSync::connectionTested);
        ws->sync()->testConnection(server.options().origin, server.options().username, server.options().password,
                                   kFolder);
        QVERIFY(tested.wait());
        QVERIFY(tested.first().at(0).toBool());
        QVERIFY2(tested.first().at(1).toString().contains(u"encrypted"_s), qPrintable(tested.first().at(1).toString()));
        tested.clear();
        ws->sync()->testConnection(server.options().origin, server.options().username, server.options().password,
                                   u"Inny"_s);
        QVERIFY(tested.wait());
        QVERIFY(!tested.first().at(1).toString().contains(u"encrypted"_s));
    }

    void thePasswordChanged()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto laptop = open(&server);
        const Answer made = setUp(laptop->sync(), server, kPassword);
        QVERIFY(made.ok);
        // Without the folder set up on this device: nothing to change.
        QSignalSpy failed(laptop->sync(), &WorkspaceSync::encryptionFailed);
        laptop->sync()->changeEncryptionPassword(u"nowe długie hasło łąka"_s);
        QCOMPARE(failed.size(), 1);
        laptop->sync()->setNextcloud(encrypted(server, made));
        laptop->sync()->changeEncryptionPassword(u"krótkie"_s);
        QCOMPARE(failed.size(), 2);
        QSignalSpy changed(laptop->sync(), &WorkspaceSync::encryptionPasswordChanged);
        laptop->sync()->changeEncryptionPassword(u"nowe długie hasło łąka"_s);
        QVERIFY(changed.wait(60000));
        QVERIFY(!laptop->sync()->encryptionBusy());
        // Another device: the old password no longer opens it, the new one and the recovery key do.
        auto phone = open(&server);
        QVERIFY(setUp(phone->sync(), server, kPassword).failure.contains(u"does not open"_s));
        const Answer byNew = setUp(phone->sync(), server, u"nowe długie hasło łąka"_s);
        QVERIFY2(byNew.ok, qPrintable(byNew.failure));
        QCOMPARE(byNew.key, made.key);
        QCOMPARE(setUp(phone->sync(), server, made.recovery).key, made.key);
    }
};

QTEST_MAIN(TestWorkspaceSyncEncryption)
#include "tst_workspace_sync_encryption.moc"
