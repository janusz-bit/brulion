// Nextcloud sync per notebook through AppController and the core's in-memory Nextcloud: the
// switch and folder of 0.3.0 and older (one for all notebooks) go to the notebook that was synced
// — the one current then, or the one whose sync bookkeeping names that account and folder, also
// the sample notebook — never to another notebook opened first (its notes would mix into that
// folder), never to the documentation; one folder reached through a symlink or a relative path is
// one notebook; nothing syncs on the start screen.
#include "appcontroller.h"
#include "appsettings.h"
#include "workspace.h"
#include "workspacesync.h"

#include "fakewebdavserver.h"
#include "synctest.h"

#include "sync/scheduler.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::shell;
using synctest::FakeWebDavServer;

class TestNextcloud : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_counter = 0;

    struct Paths {
        QString settings;
        QString data;
    };

    Paths freshPaths()
    {
        ++m_counter;
        return {m_tmp.filePath(u"settings-%1.ini"_s.arg(m_counter)), m_tmp.filePath(u"data-%1"_s.arg(m_counter))};
    }

    /// Settings as 0.3.0 wrote them (one switch and one folder for every notebook), with
    /// `recent` notebooks (most recent last) and `current` as the notebook open at the end.
    static void writeOld(const Paths &p, const FakeWebDavServer &server, const QString &current,
                         const QStringList &recent = {})
    {
        {
            QSettings raw(p.settings, QSettings::IniFormat);
            QVariantList list;
            for (const QString &r : recent)
                list.prepend(QVariantMap{{u"path"_s, r}, {u"name"_s, QFileInfo(r).fileName()}});
            if (!list.isEmpty())
                raw.setValue(u"vaults/recent"_s, list);
            raw.setValue(u"sync/nextcloud/enabled"_s, true);
            raw.setValue(u"sync/nextcloud/folder"_s, u"Notatki"_s);
            raw.setValue(u"sync/nextcloud/url"_s, server.options().origin);
            raw.setValue(u"sync/nextcloud/user"_s, server.options().username);
            if (!current.isEmpty())
                raw.setValue(u"vaults/current"_s, current);
        }
        QDir().mkpath(p.data + u"/secrets"_s);
        QFile pw(p.data + u"/secrets/nextcloud-password"_s);
        if (pw.open(QIODevice::WriteOnly))
            pw.write(server.options().password.toUtf8());
    }

    std::unique_ptr<AppController> makeApp(const Paths &p, FakeWebDavServer *server, const QStringList &args = {})
    {
        AppController::Options o;
        o.settingsFile = p.settings;
        o.dataDir = p.data;
        o.arguments = args;
        auto app = std::make_unique<AppController>(o);
        app->setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 0)); });
        WorkspaceSync *sync = app->workspace()->sync();
        sync->setSyncthingDiscovery(false);
        sync->setAutomatic(false);
        sync->setOnlineCheck([] { return true; });
        sync->setTransport(server);
        return app;
    }

    static bool syncAndWait(WorkspaceSync *sync)
    {
        if (!sync->scheduler())
            return false;
        bool done = false;
        auto *scheduler = sync->scheduler();
        const QMetaObject::Connection c =
                QObject::connect(scheduler, &brulion::sync::SyncScheduler::statusChanged, scheduler, [&] {
                    if (!scheduler->isRunning())
                        done = true;
                });
        sync->syncNow();
        const bool ok = synctest::waitFor([&] { return done && !scheduler->isRunning(); });
        QObject::disconnect(c);
        return ok && sync->state() != u"error";
    }

    static bool openAndWait(AppController *app, const QString &path)
    {
        app->openVault(path);
        return synctest::waitFor(
                [&] { return app->status() == u"ready" && app->workspace()->rootPath() == QDir(path).absolutePath(); });
    }

    /// Sync `root` with the server folder "Notatki" the way 0.3.0 did, into the device state the
    /// app gives that notebook: it leaves the sync bookkeeping (sync/meta) of that account.
    static bool syncedBefore(const QString &root, const Paths &p, FakeWebDavServer *server)
    {
        Workspace ws;
        WorkspaceSync *sync = ws.sync();
        sync->setSyncthingDiscovery(false);
        sync->setAutomatic(false);
        sync->setOnlineCheck([] { return true; });
        sync->setTransport(server);
        if (!ws.open(root, QDir::cleanPath(p.data + u"/vaults/"_s + brulion::Vault::idForPath(root))))
            return false;
        ws.finishIndexing();
        NextcloudSettings n;
        n.enabled = true;
        n.url = server->options().origin;
        n.user = server->options().username;
        n.password = server->options().password;
        n.folder = u"Notatki"_s;
        sync->setNextcloud(n);
        const bool ok = syncAndWait(sync);
        ws.close();
        return ok;
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        // No desktop notification service (whatever the machine has): deterministic reminders.
        qputenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/nonexistent/brulion-test-bus");
    }

    void syncedNotebookMissingAtFirstStart()
    {
        // The synced notebook A is on an unmounted card at the first start after the update; the
        // user creates C. C must not take A's server folder: its private note would go up there
        // and A's notes would come down into it. A syncs again when it is back.
        FakeWebDavServer server;
        server.putText(u"Notatki/Notatka z A.md"_s, u"- notatka notatnika A\n"_s);
        const Paths p = freshPaths();
        const QString a = QDir::cleanPath(m_tmp.filePath(u"A-odmontowany"_s));
        writeOld(p, server, a, {a});
        auto app = makeApp(p, &server);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);

        const QString c = m_tmp.filePath(u"C-nowy"_s);
        QVERIFY(synctest::writeFileBehind(c, u"Prywatne C.md"_s, "- prywatna notatka notatnika C\n"));
        QVERIFY(openAndWait(app.get(), c));
        WorkspaceSync *sync = app->workspace()->sync();
        QVERIFY(!app->settings()->nextcloudEnabled());
        QVERIFY(!sync->active());
        QVERIFY(app->settings()->hasSharedNextcloud());

        QVERIFY(synctest::writeFileBehind(a, u"Nowa z A.md"_s, "- napisana na karcie\n"));
        QVERIFY(openAndWait(app.get(), a));
        QVERIFY(app->settings()->nextcloudEnabled());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(sync->active());
        QVERIFY(syncAndWait(sync));
        QCOMPARE(server.files(u"Notatki"_s).keys(), (QStringList{u"Notatka z A.md"_s, u"Nowa z A.md"_s}));
        QVERIFY(QFile::exists(a + u"/Notatka z A.md"_s));
        QVERIFY(!QFile::exists(c + u"/Notatka z A.md"_s));

        // C stays without sync, and its form offers a folder of its own.
        QVERIFY(openAndWait(app.get(), c));
        QVERIFY(!sync->active());
        QCOMPARE(app->settings()->proposedNextcloudFolder(), u"C-nowy"_s);
    }

    void startedInTheDocumentationThenANewNotebook()
    {
        // 0.3.0 was left in the documentation and lists no other notebook: the start screen. The
        // user creates a notebook: it is not the synced one, so it does not sync.
        FakeWebDavServer server;
        server.putText(u"Notatki/Notatka z A.md"_s, u"- notatka notatnika A\n"_s);
        const Paths p = freshPaths();
        const QString docs = QDir::cleanPath(p.data + u"/Dokumentacja Brulionu"_s);
        QVERIFY(synctest::writeFileBehind(docs, u"Spis treści.md"_s, "# Spis\n"));
        writeOld(p, server, docs, {docs});
        auto app = makeApp(p, &server);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);
        const QString c = m_tmp.filePath(u"C-z-dokumentacji"_s);
        QVERIFY(synctest::writeFileBehind(c, u"Lista zakupów.md"_s, "- mleko\n"));
        QVERIFY(openAndWait(app.get(), c));
        QVERIFY(!app->settings()->nextcloudEnabled());
        QVERIFY(!app->workspace()->sync()->active());
        QCOMPARE(server.files(u"Notatki"_s).keys(), QStringList{u"Notatka z A.md"_s});
        // The documentation never takes them over either.
        app->openDocsVault();
        QVERIFY(synctest::waitFor([&] { return app->status() == u"ready" && app->inDocsVault(); }));
        QVERIFY(!app->workspace()->sync()->active());
        QVERIFY(app->settings()->hasSharedNextcloud());
    }

    void startedInTheDocumentationGoesBackToTheSyncedNotebook()
    {
        // The common way: 0.3.0 was left in the documentation after notebook A; the app starts
        // in A, which syncs on.
        FakeWebDavServer server;
        const Paths p = freshPaths();
        const QString a = QDir::cleanPath(m_tmp.filePath(u"A-przed-dokumentacją"_s));
        QVERIFY(synctest::writeFileBehind(a, u"a.md"_s, "- a\n"));
        const QString docs = QDir::cleanPath(p.data + u"/Dokumentacja Brulionu"_s);
        writeOld(p, server, docs, {a, docs});
        auto app = makeApp(p, &server);
        app->start();
        QVERIFY(synctest::waitFor([&] { return app->status() == u"ready"; }));
        QCOMPARE(app->workspace()->rootPath(), a);
        QVERIFY(app->settings()->nextcloudEnabled());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(app->workspace()->sync()->active());
    }

    void sampleNotebookKeepsItsSync()
    {
        // Whoever wrote in the sample notebook and synced it keeps syncing it; the next notebook
        // opened does not get its folder.
        FakeWebDavServer server;
        server.putText(u"Notatki/Moje notatki.md"_s, u"- piszę tu od miesiąca\n"_s);
        const Paths p = freshPaths();
        const QString demo = QDir::cleanPath(p.data + u"/Przykładowy brulion"_s);
        QVERIFY(synctest::writeFileBehind(demo, u"Moje notatki.md"_s, "- piszę tu od miesiąca\n"));
        writeOld(p, server, demo, {demo});
        auto app = makeApp(p, &server);
        app->start();
        QVERIFY(synctest::waitFor([&] { return app->status() == u"ready"; }));
        QCOMPARE(app->workspace()->rootPath(), demo);
        QVERIFY(app->workspace()->sync()->active());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(synctest::writeFileBehind(demo, u"Jutro.md"_s, "- dalej tutaj\n"));
        QVERIFY(syncAndWait(app->workspace()->sync()));
        QVERIFY(server.text(u"Notatki/Jutro.md"_s).has_value());

        const QString x = m_tmp.filePath(u"X-później"_s);
        QVERIFY(synctest::writeFileBehind(x, u"Praca.md"_s, "- tajne sprawy służbowe\n"));
        QVERIFY(openAndWait(app.get(), x));
        QVERIFY(!app->workspace()->sync()->active());
        QVERIFY(!server.text(u"Notatki/Praca.md"_s).has_value());
    }

    void syncedNotebookFoundByItsBookkeeping()
    {
        // No notebook to record (left in the documentation, nothing else listed). Notebook M was
        // synced with "Notatki" (its sync bookkeeping says so): M takes the settings over, N
        // opened before it does not.
        FakeWebDavServer server;
        const Paths p = freshPaths();
        const QString m = QDir::cleanPath(m_tmp.filePath(u"M-zsynchronizowany"_s));
        QVERIFY(synctest::writeFileBehind(m, u"Z M.md"_s, "- z notatnika M\n"));
        QVERIFY(syncedBefore(m, p, &server));
        QVERIFY(server.text(u"Notatki/Z M.md"_s).has_value());
        const QString docs = QDir::cleanPath(p.data + u"/Dokumentacja Brulionu"_s);
        writeOld(p, server, docs, {docs});
        auto app = makeApp(p, &server);
        app->start();
        QCOMPARE(app->status(), u"onboarding"_s);

        const QString n = m_tmp.filePath(u"N-inny"_s);
        QVERIFY(synctest::writeFileBehind(n, u"Z N.md"_s, "- z notatnika N\n"));
        QVERIFY(openAndWait(app.get(), n));
        QVERIFY(!app->workspace()->sync()->active());

        QVERIFY(openAndWait(app.get(), m));
        QVERIFY(app->settings()->nextcloudEnabled());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(app->workspace()->sync()->active());
        QVERIFY(syncAndWait(app->workspace()->sync()));
        QCOMPARE(server.files(u"Notatki"_s).keys(), QStringList{u"Z M.md"_s});
    }

    void relativePathTakesTheSettings()
    {
        // The synced notebook opened with a relative path (typed, --vault): the same notebook.
        FakeWebDavServer server;
        const Paths p = freshPaths();
        const QString parent = QDir::cleanPath(m_tmp.filePath(u"względny-%1"_s.arg(m_counter)));
        const QString a = parent + u"/Względny"_s;
        QVERIFY(synctest::writeFileBehind(a, u"a.md"_s, "- a\n"));
        writeOld(p, server, a, {a});
        const QString cwd = QDir::currentPath();
        QVERIFY(QDir::setCurrent(parent));
        auto app = makeApp(p, &server, {u"brulion"_s, u"--vault"_s, u"Względny"_s});
        app->start();
        const bool ready = synctest::waitFor([&] { return app->status() == u"ready"; });
        QDir::setCurrent(cwd);
        QVERIFY(ready);
        QCOMPARE(app->workspace()->rootPath(), a);
        QVERIFY(app->settings()->nextcloudEnabled());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(app->workspace()->sync()->active());
        QVERIFY(!app->settings()->hasSharedNextcloud());
    }

    void symlinkedNotebookIsOneNotebook()
    {
        // One folder reached through a symlink and through its real path has one sync
        // bookkeeping (device state) and so one switch and server folder.
        FakeWebDavServer server;
        server.putText(u"Notatki/Z serwera.md"_s, u"- z serwera\n"_s);
        const Paths p = freshPaths();
        const QString real = QDir::cleanPath(m_tmp.filePath(u"Dropbox/Notatnik"_s));
        QVERIFY(synctest::writeFileBehind(real, u"a.md"_s, "- a\n"));
        const QString link = QDir::cleanPath(m_tmp.filePath(u"Notatnik-skrót"_s));
        QVERIFY(QFile::link(real, link));
        writeOld(p, server, link, {link});
        auto app = makeApp(p, &server);
        app->start();
        QVERIFY(synctest::waitFor([&] { return app->status() == u"ready"; }));
        WorkspaceSync *sync = app->workspace()->sync();
        QVERIFY(sync->active());
        QVERIFY(syncAndWait(sync));
        const QString stateViaLink = app->workspace()->vault()->deviceStatePath();

        QVERIFY(openAndWait(app.get(), real));
        QCOMPARE(app->workspace()->vault()->deviceStatePath(), stateViaLink);
        QVERIFY(app->settings()->nextcloudEnabled());
        QCOMPARE(app->settings()->nextcloudFolder(), u"Notatki"_s);
        QVERIFY(sync->active());
        QVERIFY(syncAndWait(sync));
        QVERIFY(QFile::exists(real + u"/Z serwera.md"_s));
        QCOMPARE(server.files(u"Notatki"_s).keys(), (QStringList{u"Z serwera.md"_s, u"a.md"_s}));
    }

    void startScreenSyncsNothing()
    {
        // The settings of 0.3.0 still wait for their notebook; the documentation is closed to the
        // start screen. While it is still attached (one turn), nothing may read them as its own.
        FakeWebDavServer server;
        const Paths p = freshPaths();
        const QString docs = QDir::cleanPath(p.data + u"/Dokumentacja Brulionu"_s);
        writeOld(p, server, docs, {docs});
        auto app = makeApp(p, &server);
        app->workspace()->sync()->setAutomatic(true);
        app->start();
        app->openDocsVault();
        QVERIFY(synctest::waitFor([&] { return app->status() == u"ready" && app->inDocsVault(); }));
        QVERIFY(!app->workspace()->sync()->active());
        app->closeVault();
        QVERIFY(!app->settings()->nextcloudEnabled());
        QVERIFY(!app->workspace()->sync()->active());
        QTest::qWait(2000); // past the automatic sync's start delay
        QCOMPARE(server.logSize(), 0);
    }
};

QTEST_GUILESS_MAIN(TestNextcloud)
#include "tst_nextcloud.moc"
