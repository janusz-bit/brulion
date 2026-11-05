// Two processes of one device syncing one notebook through the same store file (Android: the app
// and its background check, docs/ANDROID.md "Sprawdzanie notatek w tle"): a run holds the store's
// sync lock, a run while another process holds it is refused (OtherProcess) and leaves everything
// as it was, a dead owner's lock is taken over, and every run starts from the state the other
// process's runs left in the store.
#include "synctest.h"

#include <QLockFile>
#include <QTest>

using namespace synctest;

namespace {

/// One process's view of the notebook: its own Vault, LocalStore (on the shared file) and engine.
struct Process {
    /// `flushNow`: every change is written at once (re-reading the file) — the app's store waits
    /// LocalStore::kDefaultFlushDelay instead, so between runs it keeps what it read.
    Process(FakeWebDavServer &server, const QString &root, const QString &storeFile, TestClock &clock,
            const QString &device = u"Telefon"_s, bool flushNow = true)
        : vault(std::make_unique<Vault>(root, QString()))
        , store(std::make_unique<LocalStore>(storeFile))
        , client(std::make_unique<WebDavClient>(server.url(kFolder), server.options().username,
                                                server.options().password, &server))
    {
        vault->setClock(clock.fn());
        vault->open();
        if (flushNow)
            store->setFlushDelay(0);
        engine = std::make_unique<SyncEngine>(vault.get(), client.get(), store.get(), device);
        engine->setClock(clock.fn());
    }

    SyncReport sync() { return Device::runSync(*engine); }

    std::unique_ptr<Vault> vault;
    std::unique_ptr<LocalStore> store;
    std::unique_ptr<WebDavClient> client;
    std::unique_ptr<SyncEngine> engine;
};

} // namespace

class TestSyncProcesses : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void aRunWhileAnotherProcessSyncsIsRefused()
    {
        FakeWebDavServer server;
        TestClock clock;
        QTemporaryDir tmp;
        const QString root = tmp.filePath(u"Notatnik"_s);
        QVERIFY(writeFileBehind(root, u"Zakupy.md"_s, u8(u"- [ ] Kupić żółty ser ⏰ 2026-10-14 18:00"_s)));
        const QString storeFile = tmp.filePath(u"stan/store.json"_s);
        QVERIFY(QDir().mkpath(tmp.filePath(u"stan"_s)));
        Process app(server, root, storeFile, clock);

        // The other process (the background check) is in the middle of its run.
        QLockFile other(storeFile + u".sync-lock"_s);
        QVERIFY(other.tryLock(0));
        const SyncReport refused = app.sync();
        QVERIFY(refused.syncError.is(SyncErrorCode::OtherProcess));
        QCOMPARE(refused.failureCode(), u"other-process"_s);
        QCOMPARE(counts(refused), Counts{});
        QVERIFY(server.requests().isEmpty());
        QVERIFY(!app.store->contains(SyncEngine::stateKey(u"Zakupy.md"_s)));

        other.unlock();
        const SyncReport synced = app.sync();
        QVERIFY2(!synced.failed(), qPrintable(synced.failureCode()));
        QCOMPARE(synced.uploaded, 1);
        // The lock is gone with the run: the next process may take it.
        QLockFile next(storeFile + u".sync-lock"_s);
        QVERIFY(next.tryLock(0));
    }

    void aRunStartsFromWhatTheOtherProcessLeftInTheStore()
    {
        FakeWebDavServer server;
        TestClock clock;
        QTemporaryDir tmp;
        const QString root = tmp.filePath(u"Notatnik"_s);
        QVERIFY(writeFileBehind(root, u"Łódź.md"_s, u8(u"# Łódź\n- Spotkanie ⏰ 2026-10-15 09:00"_s)));
        const QString storeFile = tmp.filePath(u"store.json"_s);
        // The app opened the notebook (and read the store) before the check ran.
        Process app(server, root, storeFile, clock, u"Telefon"_s, /*flushNow*/ false);
        {
            Process check(server, root, storeFile, clock, u"Telefon"_s);
            const SyncReport report = check.sync();
            QVERIFY2(!report.failed(), qPrintable(report.failureCode()));
            QCOMPARE(report.uploaded, 1);
            // Something else the other process kept in the store meanwhile.
            check.store->set(u"test/znacznik"_s, u"zażółć"_s);
            QVERIFY(check.store->flush());
        }
        QVERIFY(!app.store->contains(SyncEngine::stateKey(u"Łódź.md"_s))); // read before the check

        // Another device edits the note; the app's run sees the check's state, not its stale copy:
        // a plain download, no merge of two "new" files.
        server.putText(kFolder + u"/Łódź.md"_s, u"# Łódź\n- Spotkanie ⏰ 2026-10-15 10:30"_s);
        clock.advance(60'000);
        const SyncReport report = app.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureCode()));
        QCOMPARE(counts(report), (Counts{0, 1, 0, 0, 0, 0, 0}));
        QCOMPARE(app.vault->readText(u"Łódź.md"_s), u"# Łódź\n- Spotkanie ⏰ 2026-10-15 10:30"_s);
        QCOMPARE(app.store->get(u"test/znacznik"_s).toString(), u"zażółć"_s);
    }

    void theLockOfADeadProcessIsTakenOver()
    {
        FakeWebDavServer server;
        TestClock clock;
        QTemporaryDir tmp;
        const QString root = tmp.filePath(u"Notatnik"_s);
        QVERIFY(writeFileBehind(root, u"Notatka.md"_s, u8(u"Treść"_s)));
        const QString storeFile = tmp.filePath(u"store.json"_s);
        // A lock file left by a process that is gone (killed by Android mid-run): a pid that runs
        // no program, on this host.
        QFile stale(storeFile + u".sync-lock"_s);
        QVERIFY(stale.open(QIODevice::WriteOnly));
        stale.write("999999999\nbrulion\n" + QSysInfo::machineHostName().toUtf8() + "\n");
        stale.close();
        Process app(server, root, storeFile, clock);
        const SyncReport report = app.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureCode()));
        QCOMPARE(report.uploaded, 1);
    }

    void aStoreInMemoryNeedsNoLock()
    {
        FakeWebDavServer server;
        Device device(server, u"Telefon"_s, {{u"Notatka.md"_s, u8(u"Treść"_s)}});
        const SyncReport report = device.sync();
        QVERIFY2(!report.failed(), qPrintable(report.failureCode()));
        QCOMPARE(report.uploaded, 1);
    }
};

QTEST_GUILESS_MAIN(TestSyncProcesses)
#include "tst_sync_processes.moc"
