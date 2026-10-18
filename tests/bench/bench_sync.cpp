// Benchmarks of the sync module: three-way merge and line diffs of large notes (scattered edits
// on both sides, journal appends on two devices, conflicts), and SyncEngine runs against the
// in-memory WebDAV server of the sync tests (tests/core/sync/fakewebdavserver.h; responses come
// from the event loop with no latency, so this is the client-side CPU cost: listing parse,
// planning, LocalStore bookkeeping, hashing, transfers) for vaults of 1k and 10k files:
// the first sync (upload everything), a sync without changes, and one with a few edits on
// each side.
#include "benchdata.h"

#include "fakewebdavserver.h"
#include "sync/sync.h"
#include "vault/localstore.h"
#include "vault/vault.h"

#include <QCoreApplication>
#include <QTest>

#include <map>
#include <memory>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

QStringList baseLines(int lines)
{
    bench::Rng r{quint64(lines)};
    QStringList out;
    out.reserve(lines);
    for (int i = 0; i < lines; ++i)
        out << (i % 7 == 0 ? u"## Sekcja %1"_s.arg(i) : u"- "_s + bench::sentence(r, 4, 12));
    return out;
}

struct MergeInput {
    QString base, local, remote;
};

MergeInput mergeInput(int lines, const QString &kind)
{
    const QStringList base = baseLines(lines);
    QStringList local = base;
    QStringList remote = base;
    if (kind == u"scattered") {
        for (int i = 10; i < lines; i += 100)
            local[i] += u" (zmiana lokalna)"_s;
        for (int i = 60; i < lines; i += 100)
            remote[i] += u" (zmiana zdalna)"_s;
    } else if (kind == u"append") {
        local << u"- wpis z telefonu o 9:00"_s << u"- [ ] kupić mleko"_s;
        remote << u"- wpis z laptopa o 9:05"_s << u"- [ ] zadzwonić do mamy"_s;
    } else if (kind == u"conflicts") {
        for (int i = 10; i < lines; i += 500) {
            local[i] += u" (lokalnie)"_s;
            remote[i] += u" (zdalnie)"_s;
        }
    } else if (kind == u"rewrite") {
        // One side reformatted everything (e.g. a Logseq re-export), the other appended.
        for (QString &l : local)
            l = l.replace(u"- "_s, u"* "_s);
        remote << u"- dopisane"_s;
    }
    return {base.join(u'\n') + u'\n', local.join(u'\n') + u'\n', remote.join(u'\n') + u'\n'};
}

/// Run one sync to completion, processing events.
SyncReport runSync(SyncEngine &engine)
{
    std::optional<SyncReport> result;
    engine.sync({}, [&](const SyncReport &r) { result = r; });
    QElapsedTimer t;
    t.start();
    while (!result && t.elapsed() < 600000)
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 20);
    if (!result)
        qFatal("sync did not finish");
    return *result;
}

struct SyncFixture {
    std::unique_ptr<QTemporaryDir> dir;
    std::unique_ptr<synctest::FakeWebDavServer> server;
    std::unique_ptr<Vault> vault;
    std::unique_ptr<LocalStore> store;
    std::unique_ptr<WebDavClient> client;
    std::unique_ptr<SyncEngine> engine;
    QList<bench::VaultFile> files;
    int round = 0;
};

const QString kFolder = u"Notatki"_s;

} // namespace

class BenchSync : public QObject {
    Q_OBJECT

    std::map<int, std::unique_ptr<SyncFixture>> m_fixtures;

    /// A vault of `files` notes on disk, its store file, client and engine. `firstSync` runs
    /// the initial upload (and receives its duration) unless the fixture exists already.
    SyncFixture &fixture(int files, double *firstSyncMs = nullptr)
    {
        auto &f = m_fixtures[files];
        if (f)
            return *f;
        f = std::make_unique<SyncFixture>();
        f->dir = bench::makeTempDir();
        f->files = bench::makeVault(files, 13);
        if (!bench::writeVault(f->dir->path() + u"/vault"_s, f->files))
            qFatal("cannot write the vault");
        f->server = std::make_unique<synctest::FakeWebDavServer>();
        f->server->mkdir(kFolder);
        f->vault = std::make_unique<Vault>(f->dir->path() + u"/vault"_s, QString());
        if (!f->vault->open())
            qFatal("cannot open the vault");
        // Persistent like the app's vault store (AppData/<vault>/store.json).
        f->store = std::make_unique<LocalStore>(f->dir->path() + u"/store.json"_s);
        f->client = std::make_unique<WebDavClient>(f->server->url(kFolder), f->server->options().username,
                                                   f->server->options().password, f->server.get());
        f->engine = std::make_unique<SyncEngine>(f->vault.get(), f->client.get(), f->store.get(), u"bench"_s);
        QElapsedTimer t;
        t.start();
        const SyncReport r = runSync(*f->engine);
        if (firstSyncMs)
            *firstSyncMs = qreal(t.nsecsElapsed()) / 1e6;
        if (r.failed() || r.uploaded != files)
            qFatal("first sync failed: uploaded %d of %d (%s)", r.uploaded, files, qPrintable(r.failureMessage()));
        return *f;
    }

    static void addSizeRows()
    {
        QTest::addColumn<int>("files");
        for (int n : bench::sizes({1000, 10000}, {1000, 5000}))
            QTest::newRow(qPrintable(u"%1 files"_s.arg(n))) << n;
    }

private Q_SLOTS:
    // ---- merge3 / diff ----------------------------------------------------------------------------

    void merge3_data()
    {
        QTest::addColumn<int>("lines");
        QTest::addColumn<QString>("kind");
        for (int n : bench::sizes({1000, 10000, 50000}, {1000, 10000})) {
            for (const QString &k : {u"scattered"_s, u"append"_s, u"conflicts"_s, u"rewrite"_s, u"identical"_s})
                QTest::newRow(qPrintable(u"%1 lines %2"_s.arg(n).arg(k))) << n << k;
        }
    }
    void merge3()
    {
        QFETCH(int, lines);
        QFETCH(QString, kind);
        const MergeInput in = mergeInput(lines, kind);
        QBENCHMARK {
            const MergeResult r = sync::merge3(in.base, in.local, in.remote);
            Q_UNUSED(r);
        }
    }

    void diffLines_data()
    {
        QTest::addColumn<int>("lines");
        for (int n : bench::sizes({1000, 10000, 50000}, {1000, 10000}))
            QTest::newRow(qPrintable(u"%1 lines, 1% changed"_s.arg(n))) << n;
    }
    void diffLines()
    {
        QFETCH(int, lines);
        const MergeInput in = mergeInput(lines, u"scattered"_s);
        const QStringList a = splitText(in.base).lines;
        const QStringList b = splitText(in.local).lines;
        QBENCHMARK {
            const auto hunks = sync::diffLines(a, b);
            Q_UNUSED(hunks);
        }
    }

    // ---- SyncEngine -------------------------------------------------------------------------------

    void firstSync_data() { addSizeRows(); }
    void firstSync()
    {
        QFETCH(int, files);
        m_fixtures.erase(files);
        double ms = 0;
        fixture(files, &ms);
        QTest::setBenchmarkResult(ms, QTest::WalltimeMilliseconds);
    }

    void syncNoChanges_data() { addSizeRows(); }
    void syncNoChanges()
    {
        QFETCH(int, files);
        SyncFixture &f = fixture(files);
        QElapsedTimer t;
        t.start();
        const SyncReport r = runSync(*f.engine);
        bench::reportMs(t);
        QVERIFY(!r.failed());
        QCOMPARE(r.uploaded + r.downloaded, 0);
    }

    // 5 notes edited locally and 5 others remotely since the last sync.
    void syncFewChanges_data() { addSizeRows(); }
    void syncFewChanges()
    {
        QFETCH(int, files);
        SyncFixture &f = fixture(files);
        ++f.round;
        bench::timeOnce(
                [&] {
                    for (int i = 0; i < 5; ++i) {
                        const bench::VaultFile &local = f.files[(i * 97 + f.round) % f.files.size()];
                        f.vault->writeText(local.path, local.content + u"\n- edycja lokalna %1\n"_s.arg(f.round));
                        const bench::VaultFile &remote = f.files[(i * 89 + 500 + f.round) % f.files.size()];
                        f.server->putText(kFolder + u'/' + remote.path,
                                          remote.content + u"\n- edycja zdalna %1\n"_s.arg(f.round));
                    }
                },
                [&] {
                    const SyncReport r = runSync(*f.engine);
                    QVERIFY(!r.failed());
                    QCOMPARE(r.uploaded, 5);
                    QCOMPARE(r.downloaded, 5);
                });
    }

    void cleanupTestCase()
    {
        for (const auto &[files, f] : m_fixtures) {
            f->store->flush();
            qInfo("[bench] sync store for %d files: %lld bytes", files,
                  QFileInfo(f->dir->path() + u"/store.json"_s).size());
        }
        m_fixtures.clear();
    }
};

QTEST_GUILESS_MAIN(BenchSync)
#include "bench_sync.moc"
