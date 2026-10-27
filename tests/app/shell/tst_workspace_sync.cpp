// Workspace.sync against the core's in-memory Nextcloud server (FakeWebDavServer): set up from
// settings, a first sync both ways, server changes reaching the index and the editors, a real
// conflict ending as a conflict copy that is merged back and synced, connection tests, offline
// and authentication errors in the status, the mass-deletion guard waiting for confirmation;
// the history of runs (sync-history.json, folded quiet runs, details, persistence), the live
// progress throttled to a few updates a second, the translated per-file texts, no secrets in
// anything shown; the Syncthing status through a fake REST API, and reading Syncthing's
// config.xml.
#include "workspace.h"
#include "workspacesync.h"

#include "fakewebdavserver.h"
#include "synctest.h"

#include "sync/sync.h"
#include "sync/synchistory.h"
#include "vault/vault.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

using namespace Qt::StringLiterals;
using brulion::shell::NextcloudSettings;
using brulion::shell::SyncthingSettings;
using brulion::shell::Workspace;
using brulion::shell::WorkspaceSync;
using synctest::FakeWebDavServer;

namespace {

/// The local Syncthing daemon's REST API (API key "klucz"): one folder at `folderPath`.
class FakeSyncthing : public QObject, public brulion::sync::HttpTransport {
public:
    QString folderPath;
    bool down = false;
    QString state = u"idle"_s;

    void send(const brulion::sync::HttpRequest &request, brulion::sync::HttpCallback done) override
    {
        const brulion::sync::HttpResponse r = handle(request);
        QTimer::singleShot(0, this, [done, r] { done(r); });
    }

private:
    static brulion::sync::HttpResponse json(const QJsonDocument &doc, int status = 200)
    {
        brulion::sync::HttpResponse r;
        r.status = status;
        r.headers.insert("content-type", "application/json");
        r.body = doc.toJson(QJsonDocument::Compact);
        return r;
    }
    brulion::sync::HttpResponse handle(const brulion::sync::HttpRequest &request) const
    {
        if (down) {
            brulion::sync::HttpResponse r;
            r.networkError = u"Connection refused"_s;
            return r;
        }
        if (request.header("x-api-key") != "klucz")
            return json(QJsonDocument(QJsonObject{ { u"error"_s, u"Forbidden"_s } }), 403);
        const QString path = request.url.path();
        if (path == u"/rest/system/ping")
            return json(QJsonDocument(QJsonObject{ { u"ping"_s, u"pong"_s } }));
        if (path == u"/rest/config/folders")
            return json(QJsonDocument(QJsonArray{ QJsonObject{
                    { u"id"_s, u"notatki"_s }, { u"label"_s, u"Notatki"_s }, { u"path"_s, folderPath } } }));
        if (path == u"/rest/system/connections") {
            const QJsonObject connections{ { u"AAA"_s, QJsonObject{ { u"connected"_s, true } } },
                                           { u"BBB"_s, QJsonObject{ { u"connected"_s, true } } },
                                           { u"CCC"_s, QJsonObject{ { u"connected"_s, false } } } };
            return json(QJsonDocument(QJsonObject{ { u"connections"_s, connections } }));
        }
        if (path == u"/rest/db/status"
            && QUrlQuery(request.url).queryItemValue(u"folder"_s) == u"notatki") {
            return json(QJsonDocument(QJsonObject{
                    { u"state"_s, state }, { u"needBytes"_s, state == u"idle" ? 0 : 500 }, { u"globalBytes"_s, 1000 },
                    { u"errors"_s, 0 } }));
        }
        return json(QJsonDocument(QJsonObject()), 404);
    }
};

} // namespace

class TestWorkspaceSync : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;
    synctest::TestClock m_clock;

    /// A vault with `files`, opened, with sync wired to `transport` (manual syncs only, online).
    std::unique_ptr<Workspace> open(brulion::sync::HttpTransport *transport, const QMap<QString, QString> &files = {},
                                    QString *rootOut = nullptr)
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
        sync->setDeviceName(u"Telefon Janusza"_s);
        if (!ws->open(root, root + u"-state"_s))
            qWarning() << "cannot open" << root;
        ws->finishIndexing();
        if (rootOut)
            *rootOut = root;
        return ws;
    }

    static NextcloudSettings account(const FakeWebDavServer &server, const QString &password = {})
    {
        NextcloudSettings n;
        n.enabled = true;
        n.url = server.options().origin;
        n.user = server.options().username;
        n.password = password.isEmpty() ? server.options().password : password;
        n.folder = u"Brulion"_s;
        return n;
    }

    /// Sync now and wait for the run to end (also when it fails).
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

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }
    void init() { m_clock.now = QDateTime(QDate(2026, 10, 14), QTime(10, 0)); }

    void nextcloudRoundTrip()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        server.putText(u"Brulion/Z laptopa.md"_s, u"- przyszło z laptopa #chmura\n"_s);
        auto ws = open(&server, { { u"Notatka.md"_s, u"- lokalna notatka\n"_s } });
        WorkspaceSync *sync = ws->sync();
        QVERIFY(!sync->configured());
        QVERIFY(!sync->active());
        QCOMPARE(sync->state(), u"disabled"_s);

        QSignalSpy configuration(sync, &WorkspaceSync::configurationChanged);
        sync->setNextcloud(account(server));
        QVERIFY(sync->configured());
        QVERIFY(sync->active());
        QVERIFY(!configuration.isEmpty());
        QCOMPARE(sync->currentDavUrl(), server.url(u"Brulion"_s));
        QCOMPARE(sync->deviceName(), u"Telefon-Janusza"_s);
        QCOMPARE(sync->statusText(), u"Not synced yet"_s);

        QVERIFY(syncAndWait(sync));
        QCOMPARE(sync->state(), u"idle"_s);
        QCOMPARE(sync->lastSync(), u"2026-10-14T10:00:00"_s);
        QCOMPARE(sync->statusText(), u"Synced %1"_s.arg(sync->lastSyncText()));
        QVERIFY(sync->lastSyncText().contains(u"10:00"_s));
        QCOMPARE(server.text(u"Brulion/Notatka.md"_s).value_or(QString()), u"- lokalna notatka\n"_s);
        QCOMPARE(ws->readFile(u"Z laptopa.md"_s), u"- przyszło z laptopa #chmura\n"_s);
        QCOMPARE(ws->tagList().size(), 1); // the index follows the sync's writes
        QVariantMap report = sync->lastReport();
        QCOMPARE(report.value(u"uploaded"_s).toInt(), 1);
        QCOMPARE(report.value(u"downloaded"_s).toInt(), 1);
        QCOMPARE(report.value(u"failed"_s).toBool(), false);
        QCOMPARE(sync->appStatus().value(u"state"_s).toString(), u"idle"_s);
        QCOMPARE(sync->progress(), -1.0);

        // A change on the server reaches the open editors (fileChanged) and the search.
        QSignalSpy fileChanged(ws.get(), &brulion::EditorHost::fileChanged);
        server.putText(u"Brulion/Notatka.md"_s, u"- lokalna notatka\n- dopisane na laptopie: żółć\n"_s);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QCOMPARE(ws->readFile(u"Notatka.md"_s), u"- lokalna notatka\n- dopisane na laptopie: żółć\n"_s);
        QVERIFY(!fileChanged.isEmpty());
        QCOMPARE(ws->search(u"zolc"_s).size(), 1);

        // Both sides change the same line: the local text stays, the server's becomes a copy.
        QSignalSpy found(sync, &WorkspaceSync::conflictsFound);
        QVERIFY(ws->write(u"Notatka.md"_s, u"- notatka zmieniona na telefonie\n- dopisane na laptopie: żółć\n"_s));
        server.putText(u"Brulion/Notatka.md"_s, u"- notatka zmieniona na laptopie\n- dopisane na laptopie: żółć\n"_s);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QCOMPARE(found.size(), 1);
        QCOMPARE(sync->conflictCount(), 1);
        const QVariantMap conflict = sync->conflicts().first().toMap();
        QCOMPARE(conflict.value(u"original"_s).toString(), u"Notatka.md"_s);
        QCOMPARE(conflict.value(u"source"_s).toString(), u"brulion"_s);
        QCOMPARE(conflict.value(u"device"_s).toString(), u"Telefon-Janusza"_s);
        const QString copy = conflict.value(u"copy"_s).toString();
        const QVariantMap details = sync->conflictDetails(copy);
        QCOMPARE(details.value(u"ours"_s).toString(), u"- notatka zmieniona na telefonie\n- dopisane na laptopie: żółć\n"_s);
        QCOMPARE(details.value(u"theirs"_s).toString(), u"- notatka zmieniona na laptopie\n- dopisane na laptopie: żółć\n"_s);

        // Merged by the user; the next sync uploads it and removes the copy everywhere.
        const QString merged = u"- notatka zmieniona na telefonie i laptopie\n- dopisane na laptopie: żółć\n"_s;
        QVERIFY(sync->resolveConflict(u"Notatka.md"_s, copy, merged));
        QCOMPARE(sync->conflictCount(), 0);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QCOMPARE(server.text(u"Brulion/Notatka.md"_s).value_or(QString()), merged);
        QVERIFY(!server.text(u"Brulion/"_s + copy).has_value());

        // Switching it off stops the engine; the vault stays as it is.
        sync->setNextcloud({});
        QVERIFY(!sync->active());
        QCOMPARE(sync->state(), u"disabled"_s);
        QCOMPARE(ws->readFile(u"Notatka.md"_s), merged);
    }

    void automaticSync()
    {
        // Automatic mode (the app): the first sync starts shortly after the settings settle,
        // edits are synced after the debounce, all without "Sync now".
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server, { { u"Automat.md"_s, u"- zsynchronizuj mnie\n"_s } });
        WorkspaceSync *sync = ws->sync();
        sync->setAutomatic(true);
        sync->setNextcloud(account(server));
        QVERIFY(sync->active());
        QCOMPARE(server.logSize(), 0); // not while the settings may still be typed
        QTRY_VERIFY_WITH_TIMEOUT(server.text(u"Brulion/Automat.md"_s).has_value(), 10000);
        QTRY_COMPARE(sync->state(), u"idle"_s);
        QVERIFY(!sync->nextSync().isEmpty()); // the next periodic run is planned
    }

    void folderChangesAreSyncedLikeEdits()
    {
        // An empty folder made, removed or renamed in the app makes no Vault change of its own:
        // the sync still follows after the debounce, like after an edit.
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server, {{u"Automat.md"_s, u"- zsynchronizuj mnie\n"_s}});
        WorkspaceSync *sync = ws->sync();
        sync->setAutomatic(true);
        sync->setNextcloud(account(server));
        QTRY_VERIFY_WITH_TIMEOUT(server.text(u"Brulion/Automat.md"_s).has_value(), 10000);
        QTRY_COMPARE(sync->state(), u"idle"_s);
        sync->scheduler()->setDebounce(200);
        QCOMPARE(ws->createFolder(u"Pusty żółw"_s), u"Pusty żółw"_s);
        QTRY_VERIFY_WITH_TIMEOUT(server.isDir(u"Brulion/Pusty żółw"_s), 10000);
        QTRY_COMPARE(sync->state(), u"idle"_s);
        QVERIFY(!ws->deletePage(u"Pusty żółw"_s).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(!server.isDir(u"Brulion/Pusty żółw"_s), 10000);
    }

    void connectionTest()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        server.mkdir(u"Brulion"_s);
        auto ws = open(&server);
        WorkspaceSync *sync = ws->sync();
        QSignalSpy tested(sync, &WorkspaceSync::connectionTested);
        QSignalSpy testing(sync, &WorkspaceSync::testingChanged);

        sync->testConnection(server.options().origin, u"jan"_s, server.options().password, u"Brulion"_s);
        QVERIFY(sync->testing());
        QTRY_COMPARE(tested.size(), 1);
        QVERIFY(!sync->testing());
        QCOMPARE(tested.first().at(0).toBool(), true);
        QCOMPARE(testing.size(), 2);

        // A folder that does not exist yet is created by the first sync: fine.
        sync->testConnection(server.options().origin, u"jan"_s, server.options().password, u"Nowy folder"_s);
        QTRY_COMPARE(tested.size(), 2);
        QCOMPARE(tested.at(1).at(0).toBool(), true);

        sync->testConnection(server.options().origin, u"jan"_s, u"złe hasło"_s, u"Brulion"_s);
        QTRY_COMPARE(tested.size(), 3);
        QCOMPARE(tested.at(2).at(0).toBool(), false);
        QCOMPARE(tested.at(2).at(1).toString(), u"Wrong user name or app password."_s);

        // Incomplete input is answered at once, without a request; the stored password is used
        // when none is typed.
        sync->testConnection(QString(), u"jan"_s, u"x"_s, QString());
        QCOMPARE(tested.size(), 4);
        QCOMPARE(tested.at(3).at(0).toBool(), false);
        sync->testConnection(server.options().origin, u"jan"_s, QString(), u"Brulion"_s);
        QCOMPARE(tested.size(), 5);
        QCOMPARE(tested.at(4).at(0).toBool(), false); // no password stored yet
        NextcloudSettings stored = account(server);
        stored.enabled = false;
        sync->setNextcloud(stored);
        sync->testConnection(server.options().origin, u"jan"_s, QString(), u"Brulion"_s);
        QTRY_COMPARE(tested.size(), 6);
        QCOMPARE(tested.at(5).at(0).toBool(), true);
        QCOMPARE(sync->davUrlFor(u"chmura.example.pl/"_s, u"jan"_s, QString()),
                 u"https://chmura.example.pl/remote.php/dav/files/jan/Brulion"_s);
    }

    void errorsInTheStatus()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server, { { u"a.md"_s, u"- a\n"_s } });
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server, u"złe hasło"_s));
        QVERIFY(syncAndWait(sync));
        QCOMPARE(sync->state(), u"error"_s);
        QCOMPARE(sync->lastErrorCode(), u"auth"_s);
        QCOMPARE(sync->lastError(), u"Wrong user name or app password."_s);
        QCOMPARE(sync->statusText(), u"Wrong user name or app password."_s);
        QCOMPARE(sync->appStatus().value(u"state"_s).toString(), u"error"_s);
        QVERIFY(sync->lastSync().isEmpty());

        // Offline: nothing is attempted.
        sync->setOnlineCheck([] { return false; });
        server.clearLog();
        sync->syncNow();
        QTRY_COMPARE(sync->state(), u"offline"_s);
        QCOMPARE(sync->statusText(), u"Offline — will sync later"_s);
        QCOMPARE(server.logSize(), 0);

        // Incomplete settings: no engine, a hint instead of a sync.
        QSignalSpy errors(sync, &WorkspaceSync::errorOccurred);
        NextcloudSettings incomplete = account(server);
        incomplete.password.clear();
        sync->setNextcloud(incomplete);
        QVERIFY(!sync->active());
        QCOMPARE(sync->statusText(), u"Nextcloud is not set up completely"_s);
        sync->syncNow();
        QCOMPARE(errors.size(), 1);
    }

    void historyAndDetails()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        QString root;
        auto ws = open(&server, {{u"Zażółć.md"_s, u"- gęślą jaźń\n"_s}}, &root);
        WorkspaceSync *sync = ws->sync();
        QVERIFY(sync->history().isEmpty());
        QVERIFY(sync->lastRun().isEmpty());
        sync->setNextcloud(account(server));
        QSignalSpy historyChanged(sync, &WorkspaceSync::historyChanged);
        QVERIFY(syncAndWait(sync));
        // Two runs without changes fold into one row.
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QVERIFY(historyChanged.size() >= 3);
        QVariantList rows = sync->history();
        QCOMPARE(rows.size(), 2);
        const QVariantMap quiet = rows.first().toMap();
        QCOMPARE(quiet.value(u"outcome"_s).toString(), u"ok"_s);
        QCOMPARE(quiet.value(u"quiet"_s).toBool(), true);
        QCOMPARE(quiet.value(u"repeats"_s).toInt(), 2);
        QCOMPARE(quiet.value(u"outcomeText"_s).toString(), u"No changes ×2"_s);
        QCOMPARE(quiet.value(u"tone"_s).toString(), u"success"_s);
        QCOMPARE(quiet.value(u"trigger"_s).toString(), u"manual"_s);
        QCOMPARE(quiet.value(u"triggerText"_s).toString(), u"by hand"_s);
        QCOMPARE(quiet.value(u"number"_s).toInt(), 3);
        const QVariantMap first = rows.last().toMap();
        QCOMPARE(first.value(u"number"_s).toInt(), 1);
        QCOMPARE(first.value(u"outcomeText"_s).toString(), u"Synced"_s);
        QVERIFY2(first.value(u"summary"_s).toString().startsWith(u"↑1"_s),
                 qPrintable(first.value(u"summary"_s).toString()));
        QCOMPARE(sync->lastRun().value(u"number"_s).toInt(), 3);

        const QVariantMap details = sync->runDetails(1);
        QCOMPARE(details.value(u"number"_s).toInt(), 1);
        const QVariantList phases = details.value(u"phases"_s).toList();
        QVERIFY(phases.size() >= 5);
        QCOMPARE(phases.first().toMap().value(u"id"_s).toString(), u"connect"_s);
        QCOMPARE(phases.first().toMap().value(u"text"_s).toString(), u"Connecting to the server"_s);
        const QVariantMap stats = details.value(u"stats"_s).toMap();
        QVERIFY(stats.value(u"requests"_s).toInt() > 0);
        QVERIFY(stats.value(u"requestsText"_s).toString().contains(u"PROPFIND"_s));
        QVERIFY(stats.value(u"requestsText"_s).toString().contains(u"PUT"_s));
        QCOMPARE(details.value(u"counts"_s).toMap().value(u"uploaded"_s).toInt(), 1);
        QVERIFY(details.value(u"fromMs"_s).toLongLong() > 0);
        QVERIFY(details.value(u"toMs"_s).toLongLong() >= details.value(u"fromMs"_s).toLongLong());
        QVERIFY(sync->runDetails(99).isEmpty());
        const QString text = sync->runDetailsText(1);
        QVERIFY2(text.startsWith(u"Sync #1 · "_s), qPrintable(text));
        QVERIFY(text.contains(u"Result: Synced"_s));
        QVERIFY(text.contains(u"Requests: "_s));
        QCOMPARE(sync->runDetailsText(99), QString());
        const QStringList lines = sync->historyLines(5);
        QCOMPARE(lines.size(), 2);
        QVERIFY2(lines.first().startsWith(u"#3 2026-10-14 10:02 · by hand · No changes ×2"_s),
                 qPrintable(lines.first()));

        // A failed run is in the history too, with its code and explanation.
        sync->setNextcloud(account(server, u"złe hasło"_s));
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        const QVariantMap failed = sync->history().first().toMap();
        QCOMPARE(failed.value(u"outcome"_s).toString(), u"failed"_s);
        QCOMPARE(failed.value(u"errorCode"_s).toString(), u"auth"_s);
        QCOMPARE(failed.value(u"errorText"_s).toString(), u"Wrong user name or app password."_s);
        QCOMPARE(failed.value(u"outcomeText"_s).toString(), u"Failed: wrong user name or app password"_s);
        QCOMPARE(failed.value(u"needsUser"_s).toBool(), true);
        QCOMPARE(failed.value(u"tone"_s).toString(), u"danger"_s);
        QCOMPARE(failed.value(u"number"_s).toInt(), 4); // numbering survives the new engine
        QVERIFY(!sync->runDetailsText(4).contains(u"złe hasło"_s));
        QVERIFY(!sync->historyLines().join(u'\n').contains(u"złe hasło"_s));

        // Persisted next to the vault's device state: a new session sees the same runs and
        // goes on numbering.
        ws.reset();
        QVERIFY(QFile::exists(root + u"-state/sync-history.json"_s));
        auto again = std::make_unique<Workspace>();
        again->setClock(m_clock.fn());
        WorkspaceSync *sync2 = again->sync();
        sync2->setSyncthingDiscovery(false);
        sync2->setTransport(&server);
        sync2->setAutomatic(false);
        sync2->setOnlineCheck([] { return true; });
        QVERIFY(again->open(root, root + u"-state"_s));
        QCOMPARE(sync2->history().size(), 3);
        sync2->setNextcloud(account(server));
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync2));
        QCOMPARE(sync2->history().first().toMap().value(u"number"_s).toInt(), 5);
        sync2->clearHistory();
        QVERIFY(sync2->history().isEmpty());
    }

    void liveIsThrottled()
    {
        // Progress arrives once per file; the UI gets a few updates a second, and the status line
        // (sidebar, settings) is not re-evaluated per file either.
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        QMap<QString, QString> files;
        for (int i = 0; i < 80; ++i)
            files.insert(u"Dziennik/Dzień %1.md"_s.arg(i), u"- wpis %1\n"_s.arg(i));
        auto ws = open(&server, files);
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server));
        server.setLatency("PUT", 15);
        QSignalSpy live(sync, &WorkspaceSync::liveChanged);
        QSignalSpy status(sync, &WorkspaceSync::statusChanged);
        QVariantMap seen;
        connect(sync, &WorkspaceSync::liveChanged, this, [&] {
            if (sync->live().value(u"phase"_s).toString() == u"syncing"_s)
                seen = sync->live();
        });
        QElapsedTimer timer;
        timer.start();
        QVERIFY(syncAndWait(sync));
        QTRY_VERIFY(sync->live().isEmpty()); // idle again
        const qint64 ms = timer.elapsed();
        QCOMPARE(server.text(u"Brulion/Dziennik/Dzień 79.md"_s).value_or(QString()), u"- wpis 79\n"_s);
        const int bound = int(ms / 250) + 6;
        QVERIFY2(live.size() <= bound, qPrintable(u"%1 live updates in %2 ms"_s.arg(live.size()).arg(ms)));
        QVERIFY2(status.size() <= bound + 6, qPrintable(u"%1 status updates in %2 ms"_s.arg(status.size()).arg(ms)));
        QVERIFY(status.size() < 80);
        if (!seen.isEmpty()) { // a fast machine may finish within one update
            QCOMPARE(seen.value(u"running"_s).toBool(), true);
            QCOMPARE(seen.value(u"total"_s).toInt(), 80);
            QVERIFY(seen.value(u"phaseText"_s).toString().endsWith(u"…"_s));
            QVERIFY(seen.value(u"elapsedText"_s).toString().startsWith(u"Running for "_s));
            QCOMPARE(seen.value(u"trigger"_s).toString(), u"manual"_s);
            QVERIFY(seen.value(u"number"_s).toInt() > 0);
        }
    }

    void texts_data()
    {
        QTest::addColumn<QString>("code");
        QTest::addColumn<QString>("text");
        QTest::newRow("locked") << u"locked"_s << u"The file is locked on the server (open in another app?)."_s;
        QTest::newRow("case-clash")
                << u"case-clash"_s
                << u"Another file has the same name in different letter case — rename one of them."_s;
        QTest::newRow("read-failed") << u"read-failed"_s << u"The file cannot be read on this device."_s;
        QTest::newRow("unknown with status") << u"http"_s << u"The file could not be synced (HTTP 418)."_s;
    }

    void texts()
    {
        QFETCH(QString, code);
        QFETCH(QString, text);
        WorkspaceSync sync;
        QCOMPARE(sync.fileErrorText(code, code == u"http" ? 418 : 0), text);
        QCOMPARE(sync.triggerText(u"edit"_s), u"after an edit"_s);
        QCOMPARE(sync.triggerText(u"network"_s), u"network is back"_s);
        QCOMPARE(sync.triggerText(u"nonsense"_s), QString());
    }

    void noSecretsShown()
    {
        // A password typed into the server address and a refused one never show up anywhere.
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        auto ws = open(&server, {{u"a.md"_s, u"- a\n"_s}});
        WorkspaceSync *sync = ws->sync();
        NextcloudSettings n = account(server, u"Tajne-Hasło-123"_s);
        n.url = u"https://jan:Inne-Tajne-456@chmura.example.pl"_s;
        sync->setNextcloud(n);
        QVERIFY(!sync->currentDavUrl().contains(u"Inne-Tajne"_s));
        QVERIFY(syncAndWait(sync));
        QCOMPARE(sync->lastErrorCode(), u"auth"_s);
        const QString shown = sync->statusText() + sync->lastError()
                              + QString::fromUtf8(QJsonDocument::fromVariant(sync->history()).toJson())
                              + QString::fromUtf8(QJsonDocument::fromVariant(sync->runDetails(1)).toJson())
                              + sync->runDetailsText(1) + sync->historyLines().join(u'\n');
        QVERIFY(!shown.contains(u"Tajne-Hasło-123"_s));
        QVERIFY(!shown.contains(u"Inne-Tajne-456"_s));
    }

    void massDeletionWaitsForConfirmation()
    {
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        QMap<QString, QString> files;
        for (int i = 0; i < 12; ++i)
            files.insert(u"Notatki/Strona %1.md"_s.arg(i), u"- treść %1\n"_s.arg(i));
        auto ws = open(&server, files);
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server));
        QVERIFY(syncAndWait(sync));
        QCOMPARE(server.files(u"Brulion"_s).size(), 12);

        for (int i = 0; i < 11; ++i)
            QVERIFY(!ws->deletePage(u"Notatki/Strona %1.md"_s.arg(i)).isEmpty());
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QVERIFY(sync->needsConfirmation());
        QCOMPARE(sync->pendingDeletions().size(), 11);
        QCOMPARE(sync->pendingDeletions().first().toMap().value(u"action"_s).toString(), u"delete-remote"_s);
        QCOMPARE(sync->lastErrorCode(), u"needs-confirmation"_s);
        QCOMPARE(sync->statusText(), u"11 file(s) would be deleted. Confirm in the sync center."_s);
        QCOMPARE(server.files(u"Brulion"_s).size(), 12); // nothing deleted yet

        // The confirmation names the list the user saw: another list confirms nothing.
        QStringList shown;
        for (const QVariant &d : sync->pendingDeletions())
            shown.append(d.toMap().value(u"path"_s).toString());
        QSignalSpy errors(sync, &WorkspaceSync::errorOccurred);
        sync->confirmDeletions(shown.mid(1));
        QCOMPARE(errors.size(), 1);
        QVERIFY(!sync->scheduler()->isRunning());
        QVERIFY(sync->needsConfirmation());
        QCOMPARE(server.files(u"Brulion"_s).size(), 12);

        bool done = false;
        connect(sync->scheduler(), &brulion::sync::SyncScheduler::synced, this, [&done] { done = true; });
        std::reverse(shown.begin(), shown.end()); // order does not matter
        sync->confirmDeletions(shown);
        QVERIFY(synctest::waitFor([&] { return done; }));
        QCOMPARE(server.files(u"Brulion"_s).size(), 1);
        QVERIFY(!sync->needsConfirmation());
    }

    void foldersTravelAndTheTreeFollows()
    {
        // Empty folders come down (the tree hears of them: folders make no Vault change of their
        // own), a folder renamed in the app is moved on the server, and both show in the history.
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        QMap<QString, QString> files;
        for (int i = 0; i < 12; ++i)
            files.insert(u"Projekty/Strona %1 ąę.md"_s.arg(i), u"- treść %1\n"_s.arg(i));
        auto ws = open(&server, files);
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server));
        QVERIFY(syncAndWait(sync));
        server.mkdir(u"Brulion/Z chmury/Puste żółw"_s);
        QSignalSpy filesChanged(ws.get(), &Workspace::filesChanged);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QVERIFY(ws->folderPaths().contains(u"Z chmury/Puste żółw"_s));
        QStringList announced;
        for (const QList<QVariant> &args : std::as_const(filesChanged))
            announced += args.at(0).toStringList();
        QVERIFY2(announced.contains(u"Z chmury/Puste żółw"_s), qPrintable(announced.join(u", "_s)));
        QCOMPARE(sync->lastReport().value(u"foldersCreated"_s).toInt(), 2);
        QVERIFY2(sync->history().first().toMap().value(u"summary"_s).toString().contains(u"2 new folder(s)"_s),
                 qPrintable(sync->history().first().toMap().value(u"summary"_s).toString()));
        QVERIFY(sync->runDetailsText(2).contains(u"Folders: 2 new"_s));

        QCOMPARE(ws->renameFolder(u"Projekty"_s, u"Projekty 2026"_s), u"Projekty 2026"_s);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QVERIFY(!sync->needsConfirmation());
        QCOMPARE(server.requests("MOVE").size(), 1);
        QCOMPARE(server.files(u"Brulion/Projekty 2026"_s).size(), 12);
        QVERIFY(!server.isDir(u"Brulion/Projekty"_s));
        QCOMPARE(sync->lastReport().value(u"foldersMoved"_s).toInt(), 1);
        const QVariantMap row = sync->history().first().toMap();
        QCOMPARE(row.value(u"outcome"_s).toString(), u"ok"_s);
        QVERIFY2(row.value(u"summary"_s).toString().contains(u"1 folder(s) moved"_s),
                 qPrintable(row.value(u"summary"_s).toString()));
        QCOMPARE(sync->folderCountsText(0, 0, 0), QString());
        QCOMPARE(sync->folderCountsText(2, 1, 1), u"Folders: 2 new, 1 deleted, 1 moved on the server"_s);
    }

    void serverRenamesKeepHistoryFavouritesAndFolds()
    {
        // A folder and a page renamed on the server (another device, the Nextcloud web page): the
        // sync renames them here too, so the version history, the favourites and the folded
        // blocks of the pages follow, and the folder tree hears of the folder's new name.
        synctest::FakeWebDavServer::Options options = synctest::serverOptions(m_clock);
        options.fileIds = true;
        FakeWebDavServer server(options);
        auto ws = open(&server, {{u"Projekty/Plan ąę.md"_s, u"- plan\n  - szczegół\n"_s},
                                 {u"Projekty/Inne.md"_s, u"- inne\n"_s},
                                 {u"Luźna strona.md"_s, u"- luźna\n"_s},
                                 {u"Dziennik.md"_s, u"- dziś\n"_s}});
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server));
        QVERIFY(syncAndWait(sync));
        const QString plan = u"Projekty/Plan ąę.md"_s;
        QVERIFY(ws->write(plan, u"- plan poprawiony\n  - szczegół\n"_s));
        QCOMPARE(ws->vault()->history()->list(plan).size(), 1);
        ws->setFavorite(plan, true);
        ws->setFavorite(u"Luźna strona.md"_s, true);
        ws->setFoldState(plan, {0});
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        QVERIFY(server.moveNode(u"Brulion/Projekty"_s, u"Brulion/Archiwum/Projekty 2026"_s));
        QVERIFY(server.moveNode(u"Brulion/Luźna strona.md"_s, u"Brulion/Archiwum/Strona ż.md"_s));
        QSignalSpy folderRenamed(ws.get(), &Workspace::folderRenamed);
        m_clock.advance(60'000);
        QVERIFY(syncAndWait(sync));
        const QString moved = u"Archiwum/Projekty 2026/Plan ąę.md"_s;
        QVERIFY(ws->exists(moved));
        QVERIFY(!ws->exists(plan));
        QCOMPARE(ws->vault()->history()->list(moved).size(), 1);
        QVERIFY(ws->isFavorite(moved));
        QVERIFY(ws->isFavorite(u"Archiwum/Strona ż.md"_s));
        QVERIFY(!ws->isFavorite(plan));
        QCOMPARE(ws->foldState(moved), (QVariantList{0}));
        QVERIFY(ws->vault()->trash().isEmpty());
        QCOMPARE(folderRenamed.size(), 1);
        QCOMPARE(folderRenamed.first().at(0).toString(), u"Projekty"_s);
        QCOMPARE(folderRenamed.first().at(1).toString(), u"Archiwum/Projekty 2026"_s);
        QCOMPARE(sync->lastReport().value(u"foldersMovedHere"_s).toInt(), 1);
        QCOMPARE(sync->lastReport().value(u"filesMovedHere"_s).toInt(), 1);
        QCOMPARE(sync->lastReport().value(u"downloaded"_s).toInt(), 0);
        const QString summary = sync->history().first().toMap().value(u"summary"_s).toString();
        QVERIFY2(summary.contains(u"1 folder(s) moved"_s) && summary.contains(u"1 file(s) moved"_s), qPrintable(summary));
        const QString details = sync->runDetailsText(sync->history().first().toMap().value(u"number"_s).toInt());
        QVERIFY2(details.contains(u"Folders: 1 moved here"_s), qPrintable(details));
        QVERIFY2(details.contains(u"Files: 1 moved here"_s), qPrintable(details));
        QCOMPARE(sync->folderCountsText(0, 0, 0, 1), u"Folders: 1 moved here"_s);
        QCOMPARE(sync->fileMovesText(0, 0), QString());
        QCOMPARE(sync->fileMovesText(1, 2), u"Files: 1 moved on the server, 2 moved here"_s);
    }

    void cancelMidSync()
    {
        // A long first sync (slow uploads) stopped with cancelSync(): what is done stays synced,
        // the state is idle (not an error), and the next run finishes the rest.
        FakeWebDavServer server(synctest::serverOptions(m_clock));
        server.setLatency("PUT", 40);
        QMap<QString, QString> files;
        for (int i = 0; i < 30; ++i)
            files.insert(u"Notatki/Notatka %1 — zażółć.md"_s.arg(i), u"- treść %1\n"_s.arg(i));
        auto ws = open(&server, files);
        WorkspaceSync *sync = ws->sync();
        sync->setNextcloud(account(server));
        sync->cancelSync(); // idle: nothing to cancel
        sync->syncNow();
        QVERIFY(synctest::waitFor([&] { return !server.requests("PUT").isEmpty(); }));
        QCOMPARE(sync->state(), u"syncing"_s);
        sync->cancelSync();
        QVERIFY(synctest::waitFor([&] { return !sync->scheduler()->isRunning(); }));
        QCOMPARE(sync->state(), u"idle"_s);
        QVERIFY(sync->lastError().isEmpty());
        const qsizetype uploaded = server.files(u"Brulion"_s).size();
        QVERIFY2(uploaded > 0 && uploaded < 30, qPrintable(QString::number(uploaded)));
        QVERIFY(syncAndWait(sync));
        QCOMPARE(server.files(u"Brulion"_s).size(), 30);
        QCOMPARE(sync->state(), u"idle"_s);
    }

    void syncthingStatus()
    {
        FakeSyncthing syncthing;
        syncthing.folderPath = m_tmp.path();
        QString root;
        auto ws = open(&syncthing, {}, &root);
        WorkspaceSync *sync = ws->sync();
        QCOMPARE(sync->syncthingStatus().value(u"enabled"_s).toBool(), false); // no API key, no discovery
        QSignalSpy changed(sync, &WorkspaceSync::syncthingChanged);

        sync->setSyncthing({ u"klucz"_s, u"http://127.0.0.1:8384"_s });
        QTRY_COMPARE(sync->syncthingStatus().value(u"state"_s).toString(), u"idle"_s);
        QVariantMap s = sync->syncthingStatus();
        QCOMPARE(s.value(u"enabled"_s).toBool(), true);
        QCOMPARE(s.value(u"folderLabel"_s).toString(), u"Notatki"_s);
        QCOMPARE(s.value(u"folderId"_s).toString(), u"notatki"_s);
        QCOMPARE(s.value(u"connected"_s).toInt(), 2);
        QCOMPARE(s.value(u"text"_s).toString(), u"Up to date with 2 device(s)."_s);

        syncthing.state = u"syncing"_s;
        sync->refreshSyncthing();
        QTRY_COMPARE(sync->syncthingStatus().value(u"state"_s).toString(), u"syncing"_s);
        QCOMPARE(sync->syncthingStatus().value(u"completion"_s).toDouble(), 50.0);
        QCOMPARE(sync->syncthingStatus().value(u"text"_s).toString(), u"Syncthing is syncing (50 %)."_s);

        sync->setSyncthing({ u"zły klucz"_s, QString() });
        QTRY_COMPARE(sync->syncthingStatus().value(u"text"_s).toString(), u"Syncthing refused the API key."_s);

        syncthing.down = true;
        sync->setSyncthing({ u"klucz"_s, QString() });
        QTRY_COMPARE(sync->syncthingStatus().value(u"state"_s).toString(), u"offline"_s);
        QCOMPARE(sync->syncthingStatus().value(u"text"_s).toString(), u"Syncthing is not running on this device."_s);

        // A vault outside every Syncthing folder.
        syncthing.down = false;
        syncthing.folderPath = u"/gdzie/indziej"_s;
        sync->setSyncthing({ u"klucz"_s, u"http://localhost:8384"_s });
        QTRY_COMPARE(sync->syncthingStatus().value(u"state"_s).toString(), u"not-shared"_s);
        QVERIFY(!changed.isEmpty());
        Q_UNUSED(root);
    }

    void syncthingConfig()
    {
        const QByteArray xml = R"(<configuration version="37">
  <folder id="abc" label="Notatki" path="/home/jan/Sync"></folder>
  <gui enabled="true" tls="false" debugging="false">
    <address>0.0.0.0:8384</address>
    <apikey>Kl1uczApi</apikey>
    <theme>default</theme>
  </gui>
  <apikey>not-this-one</apikey>
</configuration>)";
        const auto parsed = brulion::shell::parseSyncthingConfig(xml);
        QVERIFY(parsed);
        QCOMPARE(parsed->apiKey, u"Kl1uczApi"_s);
        QCOMPARE(parsed->url, u"http://127.0.0.1:8384"_s);

        const auto tls = brulion::shell::parseSyncthingConfig(
                R"(<configuration><gui tls="true"><address>[::]:8385</address><apikey>k</apikey></gui></configuration>)");
        QVERIFY(tls);
        QCOMPARE(tls->url, u"https://[::1]:8385"_s);
        const auto socket = brulion::shell::parseSyncthingConfig(
                "<configuration><gui><address>/run/syncthing.sock</address><apikey>k</apikey></gui></configuration>");
        QVERIFY(socket);
        QVERIFY(socket->url.isEmpty()); // a unix socket: the default address is tried
        QVERIFY(!brulion::shell::parseSyncthingConfig("<configuration><gui><address>127.0.0.1:8384</address></gui>"
                                                      "</configuration>"));
        QVERIFY(!brulion::shell::parseSyncthingConfig("nie jest xml <<<"));
        QVERIFY(!brulion::shell::parseSyncthingConfig(QByteArray()));
    }
};

QTEST_GUILESS_MAIN(TestWorkspaceSync)
#include "tst_workspace_sync.moc"
