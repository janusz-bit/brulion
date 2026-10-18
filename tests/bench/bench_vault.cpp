// Benchmarks of the vault module on disk (below $BRULION_BENCH_TMP, see benchdata.h): atomic
// saves (with and without History), History snapshots / listing / pruning, a rescan without
// changes (resume, periodic rescans), a rescan after 1000 external changes, the file-system
// watcher under an event storm (1000 files changed by another program, e.g. Syncthing), and
// LocalStore saves with the sync bookkeeping of a whole vault in it.
#include "benchdata.h"

#include "vault/vault.h"

#include <QCoreApplication>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <cstdio>
#include <map>
#include <memory>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// Write `bytes` to root/rel as another program would.
bool writeBehind(const QString &root, const QString &rel, const QByteArray &bytes)
{
    const QString abs = root + u'/' + rel;
    QDir().mkpath(QFileInfo(abs).absolutePath());
    QFile f(abs);
    return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
}

/// Write `bytes` to root/rel the way Syncthing does: a temporary file in the same folder, renamed
/// over the note (the vault sees that through its folder watches).
bool replaceBehind(const QString &root, const QString &rel, const QByteArray &bytes)
{
    const QString abs = root + u'/' + rel;
    const QFileInfo info(abs);
    const QString temp = info.absolutePath() + u"/.syncthing."_s + info.fileName() + u".tmp"_s;
    return writeBehind(root, QDir(root).relativeFilePath(temp), bytes)
           && std::rename(QFile::encodeName(temp).constData(), QFile::encodeName(abs).constData()) == 0;
}

/// Process events until `count` changes arrived on `spy` (or 120 s passed).
qsizetype waitForChanges(QSignalSpy &spy, qsizetype count)
{
    QElapsedTimer t;
    t.start();
    qsizetype seen = 0;
    while (t.elapsed() < 120000) {
        seen = 0;
        for (const QList<QVariant> &args : std::as_const(spy))
            seen += args.at(0).value<QList<VaultChange>>().size();
        if (seen >= count)
            break;
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents, 5);
    }
    return seen;
}

} // namespace

class BenchVault : public QObject {
    Q_OBJECT

    std::map<int, std::unique_ptr<QTemporaryDir>> m_vaults;

    QString vaultOnDisk(int notes)
    {
        auto &d = m_vaults[notes];
        if (!d) {
            d = bench::makeTempDir();
            if (!bench::writeVault(d->path(), bench::makeVault(notes, 77)))
                qFatal("cannot write the benchmark vault");
        }
        return d->path();
    }

private Q_SLOTS:
    // ---- saves ----------------------------------------------------------------------------------

    void writeText_data()
    {
        QTest::addColumn<int>("kb");
        QTest::addColumn<bool>("history");
        for (int kb : {5, 1024}) {
            QTest::newRow(qPrintable(u"%1 KB without History"_s.arg(kb))) << kb << false;
            QTest::newRow(qPrintable(u"%1 KB with History (throttled)"_s.arg(kb))) << kb << true;
        }
    }
    void writeText()
    {
        QFETCH(int, kb);
        QFETCH(bool, history);
        const auto dir = bench::makeTempDir();
        Vault vault(dir->path() + u"/vault"_s, history ? dir->path() + u"/state"_s : QString());
        QVERIFY(vault.open());
        bench::Rng r(1);
        const QString base = bench::documentNote(r, 1, 100, qsizetype(kb) * 1024);
        const QString path = u"Strony/Notatka.md"_s;
        QVERIFY(vault.writeText(path, base));
        int n = 0;
        QBENCHMARK {
            // Each save differs (a typed character), like the editor's debounced saves.
            QVERIFY(vault.writeText(path, base + QString::number(++n)));
        }
    }

    // A snapshot that is stored (throttling off) into a history of 50 entries.
    void historySnapshot_data()
    {
        QTest::addColumn<int>("kb");
        QTest::newRow("5 KB") << 5;
        QTest::newRow("100 KB") << 100;
    }
    void historySnapshot()
    {
        QFETCH(int, kb);
        const auto dir = bench::makeTempDir();
        History history(dir->path());
        HistoryPolicy policy;
        policy.throttleSeconds = 0;
        history.setPolicy(policy);
        bench::Rng r(2);
        const QString base = bench::documentNote(r, 1, 100, qsizetype(kb) * 1024);
        for (int i = 0; i < 50; ++i)
            QVERIFY(history.snapshot(u"Notatka.md"_s, base + QString::number(i), u"user"_s));
        int n = 100;
        QBENCHMARK {
            QVERIFY(history.snapshot(u"Notatka.md"_s, base + QString::number(++n), u"user"_s));
        }
    }

    void historyListAndPrune_data()
    {
        QTest::addColumn<int>("paths");
        QTest::addColumn<bool>("prune");
        for (int n : bench::sizes({200, 1000}, {200})) {
            QTest::newRow(qPrintable(u"list, %1 paths x 20"_s.arg(n))) << n << false;
            QTest::newRow(qPrintable(u"prune, %1 paths x 20"_s.arg(n))) << n << true;
        }
    }
    void historyListAndPrune()
    {
        QFETCH(int, paths);
        QFETCH(bool, prune);
        const auto dir = bench::makeTempDir();
        History history(dir->path());
        HistoryPolicy policy;
        policy.throttleSeconds = 0;
        history.setPolicy(policy);
        for (int p = 0; p < paths; ++p)
            for (int i = 0; i < 20; ++i)
                history.snapshot(u"Strony/Strona %1.md"_s.arg(p), u"Treść %1 wersja %2 żółw"_s.arg(p).arg(i),
                                 u"user"_s);
        QBENCHMARK {
            if (prune) {
                history.prune(50, 90, 5);
            } else {
                qsizetype total = 0;
                for (const QString &p : history.paths())
                    total += history.list(p).size();
                QCOMPARE(total, qsizetype(paths) * 20);
            }
        }
    }

    // ---- scans ----------------------------------------------------------------------------------

    void rescanUnchanged_data()
    {
        QTest::addColumn<int>("notes");
        for (int n : bench::sizes({1000, 10000}, {1000, 10000}))
            QTest::newRow(qPrintable(u"%1 notes"_s.arg(n))) << n;
    }
    void rescanUnchanged()
    {
        QFETCH(int, notes);
        Vault vault(vaultOnDisk(notes), QString());
        QVERIFY(vault.open());
        QBENCHMARK {
            const QList<VaultChange> changes = vault.rescan();
            QVERIFY(changes.isEmpty());
        }
    }

    // 1000 files changed by another program while the app was in the background, then rescan().
    void rescanAfterExternalChanges()
    {
        const int notes = 5000;
        Vault vault(vaultOnDisk(notes), QString());
        QVERIFY(vault.open());
        const QList<FileStat> all = vault.markdownFiles();
        static int round = 0;
        ++round;
        bench::timeOnce(
                [&] {
                    for (int i = 0; i < 1000; ++i)
                        writeBehind(vault.rootPath(), all[i * 5].path,
                                    u"Zmienione z zewnątrz %1 %2\n"_s.arg(round).arg(i).toUtf8());
                },
                [&] { QCOMPARE(vault.rescan().size(), qsizetype(1000)); });
    }

    // The watcher: 1000 files written behind the vault's back by a sync client (each a temporary
    // file renamed over the note, as Syncthing does), until every change is reported.
    // "end-to-end" includes the writes; "after last write" starts when the storm ends.
    void watcherStorm_data()
    {
        QTest::addColumn<int>("changes");
        QTest::addColumn<bool>("fromLastWrite");
        QTest::addColumn<bool>("newFolder");
        for (int n : {100, 1000}) {
            QTest::newRow(qPrintable(u"%1 modified, end-to-end"_s.arg(n))) << n << false << false;
            QTest::newRow(qPrintable(u"%1 modified, after last write"_s.arg(n))) << n << true << false;
            QTest::newRow(qPrintable(u"%1 created in one folder, after last write"_s.arg(n))) << n << true << true;
        }
    }
    void watcherStorm()
    {
        QFETCH(int, changes);
        QFETCH(bool, fromLastWrite);
        QFETCH(bool, newFolder);
        const int notes = 5000;
        Vault vault(vaultOnDisk(notes), QString());
        vault.setWatchDelay(50);
        vault.startWatching();
        QVERIFY(vault.open());
        QCoreApplication::processEvents();
        const QList<FileStat> all = vault.markdownFiles();
        static int round = 0;
        ++round;
        QSignalSpy spy(&vault, &Vault::changed);
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < changes; ++i) {
            const QString rel = newFolder ? u"Burza %1/Plik %2.md"_s.arg(round).arg(i) : all[(i * 5) % all.size()].path;
            replaceBehind(vault.rootPath(), rel, u"Burza %1 %2 — źdźbło\n"_s.arg(round).arg(i).toUtf8());
        }
        if (fromLastWrite)
            t.restart();
        const qsizetype seen = waitForChanges(spy, changes);
        bench::reportMs(t);
        QCOMPARE(seen, qsizetype(changes));
        vault.stopWatching();
    }

    // ---- LocalStore -------------------------------------------------------------------------------

    // The vault's store holds UI state and the sync bookkeeping (state + merge base per file):
    // one small change (e.g. a fold toggle) and flush() rewrites it all.
    void localStoreFlush_data()
    {
        QTest::addColumn<int>("files");
        for (int n : bench::sizes({1000, 10000}, {1000, 10000}))
            QTest::newRow(qPrintable(u"%1 synced files"_s.arg(n))) << n;
    }
    void localStoreFlush()
    {
        QFETCH(int, files);
        const auto dir = bench::makeTempDir();
        const QString file = dir->path() + u"/store.json"_s;
        const QList<bench::VaultFile> vault = bench::makeVault(files, 5);
        {
            LocalStore store(file);
            for (const bench::VaultFile &f : vault) {
                store.set(u"sync/state/"_s + f.path, QJsonObject{{u"etag"_s, u"\"5f3a9c1e2b\""_s},
                                                                 {u"size"_s, f.content.size()},
                                                                 {u"mtime"_s, 1760000000000.0},
                                                                 {u"hash"_s, u"9f86d081884c7d65"_s}});
                store.set(u"sync/base/"_s + f.path, f.content);
            }
            QVERIFY(store.flush());
        }
        qInfo("[bench] store.json for %d files: %lld bytes", files, QFileInfo(file).size());
        LocalStore store(file);
        int n = 0;
        QBENCHMARK {
            store.set(u"ui/fold/Strony/Notatka.md"_s, QJsonValue(++n));
            QVERIFY(store.flush());
        }
    }

    void localStoreLoad_data() { localStoreFlush_data(); }
    void localStoreLoad()
    {
        QFETCH(int, files);
        const auto dir = bench::makeTempDir();
        const QString file = dir->path() + u"/store.json"_s;
        {
            LocalStore store(file);
            for (const bench::VaultFile &f : bench::makeVault(files, 5))
                store.set(u"sync/base/"_s + f.path, f.content);
            QVERIFY(store.flush());
        }
        QBENCHMARK {
            LocalStore store(file);
            QVERIFY(store.size() >= files);
        }
    }
};

QTEST_GUILESS_MAIN(BenchVault)
#include "bench_vault.moc"
