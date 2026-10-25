// SyncEngine with one device: decision table, path classification, transfers, deletions,
// preconditions, cancellation, guards, servers without ETags, binary files, the vault config.
#include "synctest.h"

#include "vault/vaultconfig.h"

#include <QSignalSpy>
#include <QTest>

using namespace synctest;

namespace {

const QDateTime kStart(QDate(2026, 10, 14), QTime(10, 0, 0));

QStringList shortChanges(const QList<VaultChange> &changes)
{
    QStringList out;
    for (const VaultChange &c : changes) {
        const QString type = c.type == ChangeType::Created    ? u"create"_s
                           : c.type == ChangeType::Modified   ? u"modify"_s
                           : c.type == ChangeType::Deleted    ? u"delete"_s
                                                              : u"rename"_s;
        out.append(type + u':' + c.path + u':' + changeSourceName(c.source));
    }
    out.sort();
    return out;
}

QStringList keys(const QMap<QString, QString> &map)
{
    return map.keys();
}

std::optional<HttpResponse> fail(int status)
{
    return FakeWebDavServer::status(status);
}

} // namespace

class TestSyncEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- pure helpers --------------------------------------------------------------------------
    void planSync_data()
    {
        QTest::addColumn<bool>("local");
        QTest::addColumn<bool>("remote");
        QTest::addColumn<bool>("known");
        QTest::addColumn<bool>("localChanged");
        QTest::addColumn<bool>("remoteChanged");
        QTest::addColumn<int>("plan");
        // Without history: upload, download or reconcile.
        QTest::newRow("new local") << true << false << false << true << false << int(SyncPlan::Upload);
        QTest::newRow("new remote") << false << true << false << false << true << int(SyncPlan::Download);
        QTest::newRow("new on both") << true << true << false << true << true << int(SyncPlan::Reconcile);
        QTest::newRow("nothing") << false << false << false << false << false << int(SyncPlan::Forget);
        // Both present with history.
        QTest::newRow("unchanged") << true << true << true << false << false << int(SyncPlan::Noop);
        QTest::newRow("changed locally") << true << true << true << true << false << int(SyncPlan::Upload);
        QTest::newRow("changed remotely") << true << true << true << false << true << int(SyncPlan::Download);
        QTest::newRow("changed on both") << true << true << true << true << true << int(SyncPlan::Reconcile);
        // Deletions propagate unless the other side was modified.
        QTest::newRow("deleted remotely") << true << false << true << false << false << int(SyncPlan::DeleteLocal);
        QTest::newRow("deleted remotely, edited locally") << true << false << true << true << false << int(SyncPlan::Upload);
        QTest::newRow("deleted locally") << false << true << true << false << false << int(SyncPlan::DeleteRemote);
        QTest::newRow("deleted locally, edited remotely") << false << true << true << false << true << int(SyncPlan::Download);
        QTest::newRow("deleted on both") << false << false << true << false << false << int(SyncPlan::Forget);
    }
    void planSync()
    {
        QFETCH(bool, local);
        QFETCH(bool, remote);
        QFETCH(bool, known);
        QFETCH(bool, localChanged);
        QFETCH(bool, remoteChanged);
        QFETCH(int, plan);
        QCOMPARE(int(sync::planSync(SyncFacts{local, remote, known, localChanged, remoteChanged})), plan);
    }

    void isSyncTextPath()
    {
        for (const QString &p : {u"a.md"_s, u"b/C.MD"_s, u"x.txt"_s, u"d.json"_s, u"e.csv"_s, u"f.canvas"_s, u"g.css"_s,
                                 u"h.ics"_s, u"i.opml"_s, u"j.html"_s, u"k.svg"_s, u"l.markdown"_s, u"Zażółć.md"_s})
            QVERIFY2(sync::isSyncTextPath(p), qPrintable(p));
        for (const QString &p : {u"a.png"_s, u"b.pdf"_s, u"README"_s, u".md"_s, u"x.md.png"_s, u"dir.md/plik"_s})
            QVERIFY2(!sync::isSyncTextPath(p), qPrintable(p));
    }

    void isSyncablePath()
    {
        for (const QString &p : {u"Strona.md"_s, u"Strona.sync-conflict-20261014-101010-ABCDEFG.md"_s,
                                 u".brulion/config.json"_s, u"Notatki/Łódź.md"_s, u"Import/Logseq/pages/Strona.md"_s,
                                 u"Projekty/Node_Modules.md"_s, u"assets/zdjęcie.png"_s})
            QVERIFY2(sync::isSyncablePath(p), qPrintable(p));
        for (const QString &p : {u".trash/a.md"_s, u".obsidian/app.json"_s, u".git/HEAD"_s, u"logseq/config.edn"_s,
                                 u".brulion/cache.json"_s, u"~syncthing~a.md.tmp"_s, u"a/.brulion-tmp-1"_s, QString(),
                                 u"x.md.part"_s,
                                 // Hidden folders in another letter case (case-insensitive storage).
                                 u".GIT/hooks/pre-commit"_s, u".Obsidian/plugins/x/main.js"_s, u".Brulion/config.json"_s,
                                 u".Stignore"_s, u".TRASH/a.md"_s})
            QVERIFY2(!sync::isSyncablePath(p), qPrintable(p));
    }

    void decodeSyncText()
    {
        QCOMPARE(sync::decodeSyncText(u"Zażółć gęślą jaźń 😀"_s.toUtf8()), std::optional<QString>(u"Zażółć gęślą jaźń 😀"_s));
        QCOMPARE(sync::decodeSyncText(QByteArray("\xef\xbb\xbf" "a")), std::optional<QString>(u"a"_s));
        QCOMPARE(sync::decodeSyncText(QByteArray()), std::optional<QString>(QString()));
        QVERIFY(!sync::decodeSyncText(QByteArray("\x5a\x61\xbf\xf3")));
        QVERIFY(!sync::decodeSyncText(QByteArray("\xed\xa0\x80"))); // an encoded surrogate
        // A real U+FFFD in valid UTF-8 is text.
        QCOMPARE(sync::decodeSyncText(u"�"_s.toUtf8()), std::optional<QString>(u"�"_s));
    }

    void groupByCase()
    {
        const auto in = [](const QStringList &set) { return [set](const VaultPath &p) { return set.contains(p); }; };
        QCOMPARE(sync::groupByCase({u"B.md"_s, u"Notatka.md"_s, u"a.md"_s, u"notatka.md"_s},
                                   in({u"Notatka.md"_s, u"a.md"_s}), in({u"notatka.md"_s, u"a.md"_s, u"B.md"_s})),
                 (QList<QStringList>{{u"B.md"_s}, {u"Notatka.md"_s, u"notatka.md"_s}, {u"a.md"_s}}));
        QCOMPARE(sync::groupByCase({u"Notatka.md"_s, u"notatka.md"_s}, in({u"notatka.md"_s}), in({u"Notatka.md"_s})),
                 (QList<QStringList>{{u"notatka.md"_s, u"Notatka.md"_s}}));
        QCOMPARE(sync::groupByCase({u"Łódź.md"_s, u"łódź.md"_s}, in({}), in({})),
                 (QList<QStringList>{{u"Łódź.md"_s, u"łódź.md"_s}}));
        QVERIFY(sync::groupByCase({}, in({}), in({})).isEmpty());
    }

    void remoteVersionPrefersTheEtag()
    {
        QCOMPARE(remoteVersion(RemoteEntry{u"a"_s, false, u"abc"_s, 1, 2}), u"abc"_s);
        QCOMPARE(remoteVersion(RemoteEntry{u"a"_s, false, QString(), 1, 2}), u"m1-s2"_s);
    }

    void fileStateJsonRoundTrip()
    {
        SyncFileState state{u"h"_s, u"e1"_s, 12, 1'760'000'000'123, 1'760'000'001'000};
        QCOMPARE(SyncFileState::fromJson(state.toJson()), std::optional<SyncFileState>(state));
        state.mtime.reset();
        QCOMPARE(SyncFileState::fromJson(state.toJson()), std::optional<SyncFileState>(state));
        QVERIFY(!SyncFileState::fromJson(QJsonObject{{u"localHash"_s, 1}}));
        QVERIFY(!SyncFileState::fromJson(QJsonObject()));
    }

    // --- one device ----------------------------------------------------------------------------
    void firstSyncCreatesTheBaseAndUploadsSyncableFiles()
    {
        TestClock clock;
        FakeWebDavServer server(serverOptions(clock));
        const QByteArray png("\x89PNG\x00\x01\x02\xff", 8);
        DeviceOptions options;
        options.clock = &clock;
        options.folder = u"Dokumenty/Notatki"_s;
        Device a(server, u"laptop"_s,
                 {{u"Strona.md"_s, "# Strona\n"},
                  {u"journals/2026-10-14.md"_s, "- kawa\n"},
                  {u"Zażółć gęślą jaźń.md"_s, u8(u"ąćęłńóśźż\n"_s)},
                  {u"assets/zdjęcie.png"_s, png},
                  {u".brulion/config.json"_s, "{\n  \"journalsFolder\": \"journals\"\n}\n"},
                  {u".brulion/cache.json"_s, "{}"},
                  {u".trash/stare.md"_s, u8(u"usunięte"_s)},
                  {u".obsidian/app.json"_s, "{}"},
                  {u"~syncthing~Strona.md.tmp"_s, "tmp"}},
                 options);
        const SyncReport r = a.sync();
        QCOMPARE(counts(r), (Counts{.up = 5}));
        QVERIFY(!r.failed());
        QCOMPARE(keys(server.files(u"Dokumenty/Notatki"_s)),
                 (QStringList{u".brulion/config.json"_s, u"Strona.md"_s, u"Zażółć gęślą jaźń.md"_s, u"assets/zdjęcie.png"_s,
                              u"journals/2026-10-14.md"_s}));
        QCOMPARE(server.text(u"Dokumenty/Notatki/Zażółć gęślą jaźń.md"_s), std::optional<QString>(u"ąćęłńóśźż\n"_s));
        QCOMPARE(server.bytes(u"Dokumenty/Notatki/assets/zdjęcie.png"_s), std::optional<QByteArray>(png));
        QCOMPARE(r.started, kStart);
        QCOMPARE(r.finished, kStart);
        for (const SyncChange &c : r.changes)
            QCOMPARE(c.action, SyncAction::Upload);
        // New files are uploaded with If-None-Match: *.
        for (const auto &put : server.requests("PUT"))
            QCOMPARE(put.header("if-none-match"), QByteArray("*"));
    }

    void secondSyncWithoutChangesTransfersNothing()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "x\n"}, {u"b.png"_s, "\x01\x02\x03"}});
        a.sync();
        server.clearLog();
        QCOMPARE(counts(a.sync()), Counts{});
        for (const auto &request : server.requests())
            QCOMPARE(request.method, QByteArray("PROPFIND"));
    }

    void uploadsLocalEditsWithIfMatch()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "v1\n"}});
        a.sync();
        const QString etag = server.etag(u"Notatki/a.md"_s);
        QVERIFY(a.write(u"a.md"_s, u"v2\n"_s));
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(server.text(u"Notatki/a.md"_s), std::optional<QString>(u"v2\n"_s));
        QCOMPARE(server.requests("PUT").last().header("if-match"), QByteArray("\"" + etag.toUtf8() + "\""));
    }

    void downloadsRemoteEditsAndNewFiles()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "v1\n"}});
        a.sync();
        server.putText(u"Notatki/a.md"_s, u"v2 z serwera\n"_s);
        server.putText(u"Notatki/nowy/plik ż.md"_s, u"nowy\n"_s);
        a.changes.clear();
        QCOMPARE(counts(a.sync()), (Counts{.down = 2}));
        QCOMPARE(a.read(u"a.md"_s), u"v2 z serwera\n"_s);
        QCOMPARE(a.read(u"nowy/plik ż.md"_s), u"nowy\n"_s);
        QCOMPARE(shortChanges(a.changes), (QStringList{u"create:nowy/plik ż.md:sync"_s, u"modify:a.md:sync"_s}));
    }

    void etagChangeWithoutContentChangeDoesNotRewrite()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "v1\n"}});
        a.sync();
        server.touchEtag(u"Notatki/a.md"_s);
        a.changes.clear();
        QCOMPARE(counts(a.sync()), Counts{});
        QVERIFY(a.changes.isEmpty());
        // ...and the new ETag is remembered.
        QCOMPARE(a.engine->stateOf(u"a.md"_s)->remoteEtag, server.etag(u"Notatki/a.md"_s));
        const qsizetype gets = server.requests("GET").size();
        a.sync();
        QCOMPARE(server.requests("GET").size(), gets);
    }

    void propagatesDeletionsBothWays()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}, {u"c.md"_s, "c\n"}});
        a.sync();
        QVERIFY(a.remove(u"a.md"_s));
        server.remove(u"Notatki/b.md"_s);
        const SyncReport r = a.sync();
        QCOMPARE(counts(r), (Counts{.delL = 1, .delR = 1}));
        QCOMPARE(server.files(kFolder), (QMap<QString, QString>{{u"c.md"_s, u"c\n"_s}}));
        QCOMPARE(a.files(), (QMap<QString, QString>{{u"c.md"_s, u"c\n"_s}}));
        QCOMPARE(a.trash(), (QStringList{u".trash/a.md"_s, u".trash/b.md"_s}));
        QVERIFY(!a.engine->stateOf(u"a.md"_s));
        QVERIFY(!a.engine->stateOf(u"b.md"_s));
        QVERIFY(!a.engine->mergeBaseOf(u"b.md"_s));
    }

    void dropsTheStateOfFilesDeletedOnBothSides()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}});
        a.sync();
        QVERIFY(a.remove(u"a.md"_s));
        server.remove(u"Notatki/a.md"_s);
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(a.store->keys(SyncEngine::stateKey(QString())), QStringList{SyncEngine::stateKey(u"b.md"_s)});
    }

    void storesMergeBasesForTextFilesOnly()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "tekst\n"}, {u"b.png"_s, "\x01"}});
        a.sync();
        QCOMPARE(a.engine->mergeBaseOf(u"a.md"_s), std::optional<QString>(u"tekst\n"_s));
        QVERIFY(!a.engine->mergeBaseOf(u"b.png"_s));
        const std::optional<SyncFileState> state = a.engine->stateOf(u"a.md"_s);
        QVERIFY(state);
        QCOMPARE(state->remoteEtag, server.etag(u"Notatki/a.md"_s));
        QCOMPARE(state->size, 6);
        QCOMPARE(state->syncedAt, kStart.toMSecsSinceEpoch());
        QVERIFY(state->mtime.has_value());
    }

    void largeTextFilesKeepNoMergeBase()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"duży.md"_s, QByteArray(64, 'x')}, {u"mały.md"_s, "ok\n"}});
        a.engine->setMaxBaseSize(32);
        a.sync();
        QVERIFY(!a.engine->mergeBaseOf(u"duży.md"_s));
        QVERIFY(a.engine->stateOf(u"duży.md"_s));
        QCOMPARE(a.engine->mergeBaseOf(u"mały.md"_s), std::optional<QString>(u"ok\n"_s));
    }

    void runsOnlyOneSyncAtATime()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "x"}});
        QSignalSpy started(a.engine.get(), &SyncEngine::started);
        QSignalSpy finished(a.engine.get(), &SyncEngine::finished);
        std::optional<SyncReport> first, second;
        QVERIFY(a.engine->sync({}, [&](const SyncReport &r) { first = r; }));
        QVERIFY(!a.engine->sync({}, [&](const SyncReport &r) { second = r; }));
        QVERIFY(a.engine->isSyncing());
        QTRY_VERIFY(first && second);
        QVERIFY(!a.engine->isSyncing());
        QCOMPARE(started.size(), 1);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(first->uploaded, 1);
        QCOMPARE(second->uploaded, 1);
        QCOMPARE(finished.first().first().value<SyncReport>().uploaded, 1);
        QVERIFY(a.engine->sync());
        QTRY_VERIFY(!a.engine->isSyncing());
    }

    void aFailingFileDoesNotAbortTheOthers()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"zły.md"_s, "z"}, {u"c.md"_s, "c"}});
        bool broken = true;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (broken && request.method == "PUT" && path.endsWith(u"/zły.md"_s))
                return fail(500);
            return std::nullopt;
        });
        const SyncReport r = a.sync();
        QCOMPARE(counts(r), (Counts{.up = 2, .errors = 1}));
        QCOMPARE(r.errors.first().path, u"zły.md"_s);
        QVERIFY(r.errors.first().message.contains(u"500"_s));
        QVERIFY(!r.failed());
        broken = false;
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(server.text(u"Notatki/zły.md"_s), std::optional<QString>(u"z"_s));
    }

    void stopsOnAuthenticationFailures()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}, {u"c.md"_s, "c"}, {u"d.md"_s, "d"},
                                       {u"e.md"_s, "e"}, {u"f.md"_s, "f"}});
        a.engine->setConcurrency(1);
        server.hooks.append([](const HttpRequest &request, const QString &) -> std::optional<HttpResponse> {
            return request.method == "PUT" ? fail(401) : std::nullopt;
        });
        const SyncReport r = a.sync();
        QVERIFY(r.failed());
        QVERIFY(r.remoteError.is(WebDavErrorCode::Auth));
        QCOMPARE(r.failureCode(), u"auth"_s);
        QVERIFY(r.errors.isEmpty());
        QCOMPARE(server.requests("PUT").size(), 1); // not every file
    }

    void listingFailuresStopTheRun()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        server.hooks.append([](const HttpRequest &, const QString &) { return FakeWebDavServer::status(503); });
        const SyncReport r = a.sync();
        QVERIFY(r.remoteError.is(WebDavErrorCode::Server));
        QCOMPARE(r.uploaded, 0);
        QVERIFY(!r.failureMessage().isEmpty());
    }

    void cancelStopsBetweenFilesAndTheNextSyncResumes()
    {
        FakeWebDavServer server;
        QMap<QString, QByteArray> files;
        for (int i = 0; i < 6; ++i)
            files.insert(u"n%1.md"_s.arg(i), u8(u"notatka %1\n"_s.arg(i)));
        DeviceOptions options;
        options.concurrency = 1;
        Device a(server, u"laptop"_s, files, options);
        SyncRunOptions run;
        run.onProgress = [&](const SyncProgress &p) {
            if (p.phase == SyncProgress::Phase::Syncing && p.done == 2)
                a.engine->cancel();
        };
        const SyncReport r = a.sync(run);
        QVERIFY(r.cancelled);
        QVERIFY(!r.failed());
        QCOMPARE(keys(server.files(kFolder)), (QStringList{u"n0.md"_s, u"n1.md"_s}));
        QVERIFY(!a.engine->isSyncing());
        QCOMPARE(counts(a.sync()), (Counts{.up = 4}));
    }

    void cancelBeforeTheListingArrives()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        std::optional<SyncReport> report;
        a.engine->sync({}, [&](const SyncReport &r) { report = r; });
        a.engine->cancel();
        QTRY_VERIFY(report);
        QVERIFY(report->cancelled);
        QVERIFY(server.requests("PUT").isEmpty());
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
    }

    void reportsProgress()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        QList<SyncProgress> events;
        QSignalSpy spy(a.engine.get(), &SyncEngine::progress);
        SyncRunOptions options;
        options.onProgress = [&](const SyncProgress &p) { events.append(p); };
        a.sync(options);
        QList<SyncProgress::Phase> phases;
        for (const SyncProgress &p : events)
            phases.append(p.phase);
        using P = SyncProgress::Phase;
        // The server folder does not exist yet (created by this first sync): no folder answered.
        QCOMPARE(phases, (QList<P>{P::Connecting, P::Listing, P::ListingLocal, P::Comparing, P::Syncing, P::Syncing,
                                   P::Syncing, P::Saving, P::Done}));
        QCOMPARE(events.last().phase, P::Done);
        QCOMPARE(events.last().done, 2);
        QCOMPARE(events.last().total, 2);
        QCOMPARE(events.last().changes, 2);
        QCOMPARE(events.at(5).path, u"a.md"_s);
        QCOMPARE(events.at(5).done, 1);
        // Elapsed time and bytes only grow.
        for (qsizetype i = 1; i < events.size(); ++i) {
            QVERIFY(events.at(i).elapsedMs >= events.at(i - 1).elapsedMs);
            QVERIFY(events.at(i).bytesDone >= events.at(i - 1).bytesDone);
        }
        QVERIFY(events.last().bytesDone > 0);
        QCOMPARE(spy.size(), events.size());
    }

    void skipsFilesThatAreTooLarge()
    {
        FakeWebDavServer server;
        DeviceOptions options;
        options.maxFileSize = 50;
        Device a(server, u"laptop"_s, {{u"mały.md"_s, "x"}, {u"duży.bin"_s, QByteArray(100, '\0')}}, options);
        server.putFile(u"Notatki/zdalny-duży.bin"_s, QByteArray(80, 'z'));
        const SyncReport r = a.sync();
        QCOMPARE(r.skipped, (QList<VaultPath>{u"duży.bin"_s, u"zdalny-duży.bin"_s}));
        QCOMPARE(keys(server.files(kFolder)), (QStringList{u"mały.md"_s, u"zdalny-duży.bin"_s}));
        QVERIFY(!a.files().contains(u"zdalny-duży.bin"_s));
    }

    void mergesWhenTheRemoteChangedBeforeTheUpload()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "jeden\ndwa\ntrzy\n"}});
        a.sync();
        QVERIFY(a.write(u"a.md"_s, u"JEDEN\ndwa\ntrzy\n"_s));
        bool raced = false;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (!raced && request.method == "PUT" && path.endsWith(u"/a.md"_s)) {
                raced = true;
                server.putText(u"Notatki/a.md"_s, u"jeden\ndwa\ntrzy\ncztery\n"_s);
            }
            return std::nullopt;
        });
        QCOMPARE(counts(a.sync()), (Counts{.merged = 1}));
        QCOMPARE(server.text(u"Notatki/a.md"_s), std::optional<QString>(u"JEDEN\ndwa\ntrzy\ncztery\n"_s));
        QCOMPARE(a.read(u"a.md"_s), u"JEDEN\ndwa\ntrzy\ncztery\n"_s);
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void givesUpWhenTheRemoteKeepsChanging()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\nb\nc\n"}});
        a.sync();
        QVERIFY(a.write(u"a.md"_s, u"A\nb\nc\n"_s));
        int version = 0;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PUT" && path.endsWith(u"/a.md"_s))
                server.putText(u"Notatki/a.md"_s, u"a\nb\nc\n%1\n"_s.arg(++version));
            return std::nullopt;
        });
        const SyncReport r = a.sync();
        QCOMPARE(r.errors.size(), 1);
        QVERIFY(r.errors.first().message.contains(u"keeps changing"_s));
        QVERIFY(!r.failed());
    }

    void restoresARemoteFileChangedWhileDeletingIt()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "v1\n"}});
        a.sync();
        QVERIFY(a.remove(u"a.md"_s));
        bool raced = false;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (!raced && request.method == "DELETE" && path.endsWith(u"/a.md"_s)) {
                raced = true;
                server.putText(u"Notatki/a.md"_s, u"v2 z innego urządzenia\n"_s);
            }
            return std::nullopt;
        });
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(a.read(u"a.md"_s), u"v2 z innego urządzenia\n"_s);
    }

    void skipsAFileEditedLocallyWhileDownloading()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "baza\n"}});
        a.sync();
        server.putText(u"Notatki/a.md"_s, u"baza\nzdalnie\n"_s);
        bool edited = false;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (!edited && request.method == "GET" && path.endsWith(u"/a.md"_s)) {
                edited = true;
                a.write(u"a.md"_s, u"lokalnie\nbaza\n"_s);
            }
            return std::nullopt;
        });
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(a.read(u"a.md"_s), u"lokalnie\nbaza\n"_s);
        // Next sync: changed on both sides -> clean merge.
        QCOMPARE(counts(a.sync()), (Counts{.merged = 1}));
        QCOMPARE(a.read(u"a.md"_s), u"lokalnie\nbaza\nzdalnie\n"_s);
        QCOMPARE(server.text(u"Notatki/a.md"_s), std::optional<QString>(u"lokalnie\nbaza\nzdalnie\n"_s));
    }

    void quietlySkipsALocalFileThatVanished()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        // Removed by another program; the vault has not noticed yet.
        QVERIFY(QFile::remove(a.root + u"/a.md"_s));
        const SyncReport r = a.sync();
        QCOMPARE(counts(r), (Counts{.up = 1}));
        QCOMPARE(keys(server.files(kFolder)), QStringList{u"b.md"_s});
    }

    void neverOverwritesALocalFileTheVaultHasNotSeen()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        a.sync();
        server.putText(u"Notatki/nowy.md"_s, u"z serwera\n"_s);
        // Created by another program right now; no rescan yet.
        QVERIFY(writeFileBehind(a.root, u"nowy.md"_s, "lokalny\n"));
        const SyncReport r = a.sync();
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().path, u"nowy.md"_s);
        QVERIFY(r.errors.first().message.contains(u"not seen yet"_s));
        QFile file(a.root + u"/nowy.md"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("lokalny\n"));
        // Once the vault knows it, both versions are kept.
        a.vault->rescan();
        const SyncReport next = a.sync();
        QCOMPARE(counts(next), (Counts{.merged = 1}));
        QCOMPARE(a.read(u"nowy.md"_s), u"lokalny\nz serwera\n"_s);
    }

    void startsOverWhenTheServerFolderChanges()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}});
        a.sync();
        server.putText(u"Inne/b.md"_s, u"b\n"_s);
        WebDavClient other(server.url(u"Inne"_s), u"jan"_s, server.options().password, &server);
        SyncEngine engine2(a.vault.get(), &other, a.store.get(), u"laptop"_s);
        const SyncReport r = Device::runSync(engine2);
        QCOMPARE(counts(r), (Counts{.up = 1, .down = 1}));
        QCOMPARE(server.files(u"Inne"_s), (QMap<QString, QString>{{u"a.md"_s, u"a\n"_s}, {u"b.md"_s, u"b\n"_s}}));
        QCOMPARE(a.read(u"b.md"_s), u"b\n"_s);
        QCOMPARE(a.store->get(SyncEngine::metaKey()).toObject().value(u"remote"_s).toString(), other.identity());
    }

    void refusesToTrashEverythingWhenTheServerFolderIsEmpty()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}});
        a.sync();
        server.remove(kFolder);
        const SyncReport r = a.sync();
        QVERIFY(r.syncError.is(SyncErrorCode::RemoteEmpty));
        QCOMPARE(r.failureCode(), u"remote-empty"_s);
        QCOMPARE(a.files(), (QMap<QString, QString>{{u"a.md"_s, u"a\n"_s}, {u"b.md"_s, u"b\n"_s}}));
        // After a reset the vault is uploaded again.
        a.engine->resetState();
        QCOMPARE(counts(a.sync()), (Counts{.up = 2}));
        QCOMPARE(server.files(kFolder), (QMap<QString, QString>{{u"a.md"_s, u"a\n"_s}, {u"b.md"_s, u"b\n"_s}}));
    }

    void refusesToTrashEveryNoteWhenTheServerOnlyHasNewFiles()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}});
        a.sync();
        server.remove(u"Notatki/a.md"_s);
        server.remove(u"Notatki/b.md"_s);
        server.putText(u"Notatki/inny.md"_s, u"z innego urządzenia\n"_s);
        QVERIFY(a.sync().syncError.is(SyncErrorCode::RemoteEmpty));
        QCOMPARE(a.files(), (QMap<QString, QString>{{u"a.md"_s, u"a\n"_s}, {u"b.md"_s, u"b\n"_s}}));
    }

    void allowEmptySideLetsAnIntendedMassDeletionThrough()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}});
        a.sync();
        server.remove(u"Notatki/a.md"_s);
        server.remove(u"Notatki/b.md"_s);
        SyncRunOptions options;
        options.allowEmptySide = true;
        QCOMPARE(counts(a.sync(options)), (Counts{.delL = 2}));
        QVERIFY(a.files().isEmpty());
    }

    void refusesToEmptyTheServerWhenTheLocalVaultLooksEmpty()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}, {u"c.md"_s, "c\n"}});
        a.sync();
        // The vault storage was lost; the app has created today's journal and a default config.
        QTemporaryDir other;
        writeFileBehind(other.path(), u"journals/2026-10-14.md"_s, "- \n");
        writeFileBehind(other.path(), u".brulion/config.json"_s, "{}\n");
        Vault vault2(other.path(), QString());
        QVERIFY(vault2.open());
        SyncEngine engine2(&vault2, a.client.get(), a.store.get(), u"laptop"_s);
        const SyncReport r = Device::runSync(engine2);
        QVERIFY(r.syncError.is(SyncErrorCode::LocalEmpty));
        QCOMPARE(keys(server.files(kFolder)), (QStringList{u"a.md"_s, u"b.md"_s, u"c.md"_s}));
    }

    void needsAnOpenVault()
    {
        FakeWebDavServer server;
        DeviceOptions options;
        options.open = false;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}}, options);
        const SyncReport r = a.sync();
        QVERIFY(r.syncError.is(SyncErrorCode::NotReady));
        QVERIFY(server.requests().isEmpty());
        SyncEngine nothing(nullptr, nullptr, nullptr, u"x"_s);
        QVERIFY(Device::runSync(nothing).syncError.is(SyncErrorCode::NotReady));
    }

    void worksWithServersWithoutEtags()
    {
        TestClock clock;
        FakeWebDavServer::Options serverOpts = serverOptions(clock);
        serverOpts.noEtags = true;
        FakeWebDavServer server(serverOpts);
        DeviceOptions options;
        options.clock = &clock;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "v1\n"}, {u"b.md"_s, "b\n"}}, options);
        QCOMPARE(counts(a.sync()), (Counts{.up = 2}));
        QVERIFY(a.engine->stateOf(u"a.md"_s)->remoteEtag.startsWith(u"m"_s));
        QCOMPARE(counts(a.sync()), Counts{});
        clock.advance(5000);
        server.putText(u"Notatki/a.md"_s, u"v2 dłuższe\n"_s);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(a.read(u"a.md"_s), u"v2 dłuższe\n"_s);
        QVERIFY(a.write(u"b.md"_s, u"b2\n"_s));
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void detectsChangedBinaryFilesBySizeMtimeAndHash()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"img.png"_s, "\x01\x02\x03"}});
        a.sync();
        const qsizetype gets = server.requests("GET").size();
        // Untouched binary: not transferred.
        QCOMPARE(counts(a.sync()), Counts{});
        // Same size, new content and modification time (written by another program).
        QVERIFY(writeFileBehind(a.root, u"img.png"_s, "\x09\x09\x09", 1'700'000'123'000));
        a.vault->rescan();
        QCOMPARE(counts(a.sync()), (Counts{.up = 1}));
        QCOMPARE(server.bytes(u"Notatki/img.png"_s), std::optional<QByteArray>("\x09\x09\x09"));
        QCOMPARE(server.requests("GET").size(), gets);
        // A touch without a content change uploads nothing (the hash decides).
        QVERIFY(writeFileBehind(a.root, u"img.png"_s, "\x09\x09\x09", 1'700'000'456'000));
        a.vault->rescan();
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(a.engine->stateOf(u"img.png"_s)->mtime, std::optional<qint64>(1'700'000'456'000));
    }

    void syncsTheVaultConfigAndAppliesIt()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        a.sync();
        server.putText(u"Notatki/.brulion/config.json"_s, u"{\n  \"journalsFolder\": \"Dziennik\",\n  \"weekStartsOn\": 0\n}\n"_s);
        QSignalSpy configChanged(a.vault.get(), &Vault::configChanged);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(a.vault->config().journalsFolder, u"Dziennik"_s);
        QCOMPARE(a.vault->config().weekStartsOn, Qt::Sunday);
        QCOMPARE(configChanged.size(), 1);
    }

    void leavesTheTrashAndIgnoredFoldersOnTheServerAlone()
    {
        FakeWebDavServer server;
        server.putText(u"Notatki/.trash/x.md"_s, u"x"_s);
        server.putText(u"Notatki/.git/HEAD"_s, u"ref"_s);
        server.putText(u"Notatki/.GIT/hooks/pre-commit"_s, u"#!/bin/sh\nrm -rf ~\n"_s);
        server.putText(u"Notatki/a.md"_s, u"a"_s);
        Device a(server, u"laptop"_s);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QCOMPARE(a.files(), (QMap<QString, QString>{{u"a.md"_s, u"a"_s}}));
        for (const auto &request : server.requests("PROPFIND")) {
            QVERIFY2(!request.path.contains(u".git"_s, Qt::CaseInsensitive), qPrintable(request.path));
            QVERIFY(!request.path.contains(u".trash"_s));
        }
        // ...and never deletes them either.
        QVERIFY(a.remove(u"a.md"_s));
        a.sync();
        QVERIFY(server.text(u"Notatki/.trash/x.md"_s));
        QVERIFY(server.text(u"Notatki/.git/HEAD"_s));
    }

    void keepsByteOrderMarksAndLineEndingsOfDownloads()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        a.sync();
        const QByteArray bytes = "\xef\xbb\xbf" "Zażółć\r\ngęślą\r\n";
        server.putFile(u"Notatki/bom.md"_s, bytes);
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
        QFile file(a.root + u"/bom.md"_s);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), bytes);
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void destroyingTheEngineMidRunIsSafe()
    {
        FakeWebDavServer server;
        server.setLatency("PUT", 300);
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        bool called = false;
        a.engine->sync({}, [&](const SyncReport &) { called = true; });
        QVERIFY(waitFor([&] { return !server.requests("PUT").isEmpty(); })); // transfers in flight
        QVERIFY(!called);
        a.engine.reset();
        QTest::qWait(400);
        QVERIFY(!called);
        // A new engine on the same store carries on.
        a.engine = std::make_unique<SyncEngine>(a.vault.get(), a.client.get(), a.store.get(), u"laptop"_s);
        const SyncReport r = a.sync();
        QVERIFY(!r.failed());
        QCOMPARE(server.files(kFolder).size(), 2);
    }

    void destroyingTheClientMidRunEndsTheRun()
    {
        // The app replaces the WebDAV client (account settings changed) while a sync runs: its
        // pending requests die with it, so the run must not wait for them forever.
        FakeWebDavServer server;
        server.setLatency("PUT", 300);
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        std::optional<SyncReport> report;
        a.engine->sync({}, [&](const SyncReport &r) { report = r; });
        QVERIFY(waitFor([&] { return !server.requests("PUT").isEmpty(); }));
        a.client.reset();
        QVERIFY(waitFor([&] { return report.has_value(); }, 3000));
        QVERIFY(report->syncError.is(SyncErrorCode::NotReady));
        QVERIFY(report->failed());
        QVERIFY(!a.engine->isSyncing());
        // Later syncs answer (NotReady) instead of joining a dead run.
        QVERIFY(Device::runSync(*a.engine).syncError.is(SyncErrorCode::NotReady));
    }

    void destroyingTheVaultMidRunEndsTheRun()
    {
        FakeWebDavServer server;
        server.setLatency("PUT", 300);
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}});
        std::optional<SyncReport> report;
        a.engine->sync({}, [&](const SyncReport &r) { report = r; });
        QVERIFY(waitFor([&] { return !server.requests("PUT").isEmpty(); }));
        a.vault.reset();
        QVERIFY(waitFor([&] { return report.has_value(); }, 3000));
        QVERIFY(report->syncError.is(SyncErrorCode::NotReady));
        QVERIFY(!a.engine->isSyncing());
    }

    void persistsTheStoreAtTheEnd()
    {
        QTemporaryDir dir;
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        LocalStore persistent(dir.path() + u"/sync.json"_s);
        persistent.setFlushDelay(60'000);
        SyncEngine engine(a.vault.get(), a.client.get(), &persistent, u"laptop"_s);
        Device::runSync(engine);
        QVERIFY(!persistent.hasPendingChanges());
        LocalStore reloaded(dir.path() + u"/sync.json"_s);
        QVERIFY(reloaded.contains(SyncEngine::stateKey(u"a.md"_s)));
    }

    // ---- Instrumentation: phases, statistics, codes, progress, run numbers, log lines ----------

    void reportsPhasesAndStatistics()
    {
        FakeWebDavServer server;
        server.putText(u"Notatki/Dziennik/2026-10-14.md"_s, u"- zdalny wpis\n"_s);
        server.mkdir(u"Notatki/Projekty"_s);
        Device a(server, u"laptop"_s, {{u"Zażółć.md"_s, "gęślą jaźń\n"}, {u"b.md"_s, "b"}});
        SyncRunOptions options;
        options.trigger = SyncTrigger::Manual;
        const SyncReport r = a.sync(options);
        QCOMPARE(counts(r), (Counts{.up = 2, .down = 1}));
        QCOMPARE(r.runNumber, 1);
        QCOMPARE(r.trigger, SyncTrigger::Manual);
        QList<SyncPhase> phases;
        for (const SyncPhaseTime &p : r.phases)
            phases.append(p.phase);
        QCOMPARE(phases, (QList<SyncPhase>{SyncPhase::Connect, SyncPhase::ListRemote, SyncPhase::ListLocal,
                                           SyncPhase::Compare, SyncPhase::Transfer, SyncPhase::SaveState}));
        qint64 previousStart = 0;
        for (const SyncPhaseTime &p : r.phases) {
            QVERIFY(p.startMs >= previousStart);
            QVERIFY(p.durationMs >= 0);
            QVERIFY(p.startMs + p.durationMs <= r.durationMs);
            previousStart = p.startMs;
        }
        QCOMPARE(r.phases.at(1).items, 3); // the base folder, Dziennik, Projekty
        QVERIFY(r.phases.at(1).bytes > 0);
        QCOMPARE(r.phases.at(2).items, 2);
        QCOMPARE(r.phases.at(4).items, 3);
        QVERIFY(r.phases.at(4).bytes > 0);
        const SyncStats &st = r.stats;
        QCOMPARE(st.requests, int(server.logSize()));
        QCOMPARE(st.requestsByMethod.value("PROPFIND"), int(server.requests("PROPFIND").size()));
        QCOMPARE(st.requestsByMethod.value("GET"), 1);
        QCOMPARE(st.requestsByMethod.value("PUT"), 2);
        QVERIFY(st.listingBytes > 0);
        QVERIFY(st.bytesSent > 0);
        QVERIFY(st.bytesReceived >= st.listingBytes);
        QCOMPARE(st.remoteFolders, 3);
        QCOMPARE(st.remoteFiles, 1);
        QCOMPARE(st.localFiles, 2);
        QCOMPARE(st.pathsChecked, 3);
        QCOMPARE(st.failedRequests, 0);
        QCOMPARE(st.timeouts, 0);
        QVERIFY(!st.slowest.isEmpty() && st.slowest.size() <= 5);
        for (qsizetype i = 1; i < st.slowest.size(); ++i)
            QVERIFY(st.slowest.at(i - 1).ms >= st.slowest.at(i).ms);
        // Paths in the statistics are relative to the WebDAV base, never full URLs.
        for (const SyncRequestTime &t : st.slowest)
            QVERIFY2(!t.path.contains(u"://"_s), qPrintable(t.path));
        // The next run gets the next number.
        QCOMPARE(a.sync().runNumber, 2);
        QCOMPARE(a.engine->nextRunNumber(), 3);
    }

    void failedRunKeepsThePhasesItReached()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        server.hooks.append([](const HttpRequest &, const QString &) { return FakeWebDavServer::status(503); });
        const SyncReport r = a.sync();
        QVERIFY(r.failed());
        QList<SyncPhase> phases;
        for (const SyncPhaseTime &p : r.phases)
            phases.append(p.phase);
        QCOMPARE(phases, (QList<SyncPhase>{SyncPhase::Connect, SyncPhase::ListRemote, SyncPhase::SaveState}));
        QCOMPARE(r.stats.failedRequests, 1);
        QCOMPARE(r.stats.requests, 1);
    }

    void runNumbersAndTheLogTag()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        a.engine->setNextRunNumber(42);
        QCOMPARE(a.sync().runNumber, 42);
        QCOMPARE(a.client->logTag(), u"#42"_s);
        QCOMPARE(a.sync().runNumber, 43);
        a.engine->setNextRunNumber(-5);
        QCOMPARE(a.engine->nextRunNumber(), 1);
    }

    void fileErrorCodes_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("code");
        QTest::newRow("locked") << 423 << u"locked"_s;
        QTest::newRow("forbidden") << 403 << u"forbidden"_s;
        QTest::newRow("too large") << 413 << u"too-large"_s;
        QTest::newRow("quota") << 507 << u"insufficient-storage"_s;
        QTest::newRow("server") << 500 << u"server"_s;
        QTest::newRow("other") << 418 << u"http"_s;
    }

    void fileErrorCodes()
    {
        QFETCH(int, status);
        QFETCH(QString, code);
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"Żółw.md"_s, "z"}});
        server.hooks.append([status](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PUT" && path.endsWith(u"/Żółw.md"_s))
                return fail(status);
            return std::nullopt;
        });
        const SyncReport r = a.sync();
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().path, u"Żółw.md"_s);
        QCOMPARE(r.errors.first().code, code);
        QCOMPARE(r.errors.first().status, status);
        QVERIFY(r.errors.first().message.contains(QString::number(status)));
        QVERIFY(!r.failed());
        QCOMPARE(r.stats.failedRequests, 1);
    }

    void localErrorCodes()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}});
        a.sync();
        server.putText(u"Notatki/nowy.md"_s, u"z serwera\n"_s);
        QVERIFY(writeFileBehind(a.root, u"nowy.md"_s, "lokalny\n"));
        const SyncReport r = a.sync();
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().code, u"unseen-local-file"_s);
        QCOMPARE(r.errors.first().status, 0);
    }

    void readFailureCode()
    {
        BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS(); // no mode that keeps the owner from reading
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"tajne.md"_s, "x"}});
        const QString file = a.root + u"/tajne.md"_s;
        QVERIFY(QFile::setPermissions(file, QFileDevice::Permissions()));
        QFile probe(file);
        if (probe.open(QIODevice::ReadOnly)) {
            QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            QSKIP("running with permissions that ignore file modes (root)");
        }
        const SyncReport r = a.sync();
        QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(r.errors.size(), 1);
        QCOMPARE(r.errors.first().path, u"tajne.md"_s);
        QCOMPARE(r.errors.first().code, u"read-failed"_s);
    }

    void countsPreconditionRetriesAndRemoteBusy()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "jeden\ndwa\n"}});
        a.sync();
        QVERIFY(a.write(u"a.md"_s, u"JEDEN\ndwa\n"_s));
        bool raced = false;
        server.hooks.append([&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (!raced && request.method == "PUT" && path.endsWith(u"/a.md"_s)) {
                raced = true;
                server.putText(u"Notatki/a.md"_s, u"jeden\ndwa\ntrzy\n"_s);
            }
            return std::nullopt;
        });
        const SyncReport merged = a.sync();
        QCOMPARE(counts(merged), (Counts{.merged = 1}));
        QCOMPARE(merged.stats.preconditionRetries, 1);

        QVERIFY(a.write(u"a.md"_s, u"JEDEN\ndwa\ntrzy\ncztery\n"_s));
        int version = 0;
        server.hooks = {[&](const HttpRequest &request, const QString &path) -> std::optional<HttpResponse> {
            if (request.method == "PUT" && path.endsWith(u"/a.md"_s))
                server.putText(u"Notatki/a.md"_s, u"JEDEN\ndwa\ntrzy\n%1\n"_s.arg(++version));
            return std::nullopt;
        }};
        const SyncReport busy = a.sync();
        QCOMPARE(busy.errors.size(), 1);
        QCOMPARE(busy.errors.first().code, u"remote-busy"_s);
        QVERIFY(busy.stats.preconditionRetries >= 2);
    }

    void listingProgressGrows()
    {
        FakeWebDavServer server;
        for (const QString &folder : {u"A"_s, u"B"_s, u"C"_s})
            server.putText(u"Notatki/%1/x.md"_s.arg(folder), u"x"_s);
        Device a(server, u"laptop"_s);
        QList<SyncProgress> listing;
        SyncRunOptions options;
        options.onProgress = [&](const SyncProgress &p) {
            if (p.phase == SyncProgress::Phase::Listing)
                listing.append(p);
        };
        a.sync(options);
        QVERIFY(listing.size() >= 2);
        QCOMPARE(listing.first().done, 0);
        for (qsizetype i = 1; i < listing.size(); ++i) {
            QVERIFY(listing.at(i).done >= listing.at(i - 1).done);
            QVERIFY(listing.at(i).total >= listing.at(i - 1).total);
            QVERIFY(listing.at(i).done <= listing.at(i).total);
        }
        QCOMPARE(listing.last().done, 4);
        QCOMPARE(listing.last().total, 4);
        QCOMPARE(listing.last().remoteEntries, 6); // 3 folders + 3 files
    }

    void activePathsAndActivity()
    {
        FakeWebDavServer server;
        server.setLatency("PUT", 200);
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"b.md"_s, "b"}, {u"c.md"_s, "c"}});
        QCOMPARE(a.engine->activity(), SyncActivity());
        QList<SyncProgress> syncing;
        SyncRunOptions options;
        options.onProgress = [&](const SyncProgress &p) {
            if (p.phase == SyncProgress::Phase::Syncing)
                syncing.append(p);
        };
        std::optional<SyncReport> report;
        a.engine->sync(options, [&](const SyncReport &r) { report = r; });
        QVERIFY(waitFor([&] { return a.client->requestsInFlight() > 0 && !server.requests("PUT").isEmpty(); }));
        const SyncActivity activity = a.engine->activity();
        QVERIFY(activity.running);
        QCOMPARE(activity.runNumber, 1);
        QVERIFY(activity.requestsInFlight >= 1);
        QVERIFY(!activity.waitingFor.isEmpty());
        QVERIFY2(activity.waitingFor.first().startsWith(u"PUT "_s), qPrintable(activity.waitingFor.join(u", ")));
        QVERIFY(activity.idleMs >= 0 && activity.idleMs < 200 * BRULION_TEST_SLOWDOWN + 5000);
        QVERIFY(waitFor([&] { return report.has_value(); }));
        bool sawActive = false;
        for (const SyncProgress &p : syncing) {
            QVERIFY(p.active.size() <= a.engine->concurrency());
            QVERIFY(!p.active.contains(p.path));
            sawActive = sawActive || !p.active.isEmpty();
        }
        QVERIFY(sawActive);
        QVERIFY(!a.engine->activity().running);
    }

    void logsAFewLinesPerRun()
    {
        static QStringList lines;
        lines.clear();
        const QtMessageHandler previous = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context,
                                                                    const QString &message) {
            if (type != QtDebugMsg && context.category && QByteArrayView(context.category).startsWith("brulion."))
                lines.append(QString::fromUtf8(context.category) + u' ' + message);
        });
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a"}, {u"Zażółć.md"_s, "b"}});
        SyncRunOptions options;
        options.trigger = SyncTrigger::LocalChange;
        a.sync(options);
        a.sync();
        qInstallMessageHandler(previous);
        const QString all = lines.join(u'\n');
        QVERIFY2(lines.size() >= 4 && lines.size() <= 10, qPrintable(all));
        QVERIFY2(all.contains(u"brulion.sync #1 started (edit)"_s), qPrintable(all));
        QVERIFY2(all.contains(u"brulion.sync #1 ok in "_s), qPrintable(all));
        QVERIFY2(all.contains(u"up 2, down 0"_s), qPrintable(all));
        QVERIFY2(all.contains(u"requests (MKCOL"_s) || all.contains(u"requests ("_s), qPrintable(all));
        QVERIFY2(all.contains(u"#2 ok in "_s) && all.contains(u"nothing to change"_s), qPrintable(all));
        QVERIFY2(!all.contains(server.options().password), qPrintable(all));
    }
};

QTEST_GUILESS_MAIN(TestSyncEngine)
#include "tst_sync_engine.moc"
