// The index cache on disk (index/indexcache.h, notebook/indexcachekeeper.h): a notebook loaded
// with IndexLoader from a cache written by IndexCacheKeeper equals one built by parsing; a note is
// taken from the cache only while its path, size and text match (a touched file is a hit, a
// same-size same-time edit a miss), another journal configuration or a corrupt file makes the
// whole cache unused; the full-text index comes from the cache (or is rebuilt when the cache had
// none); the keeper saves after a load that parsed, when the app asks (going to the background,
// closing) and at the latest a while after the first unsaved change — not again after every pause
// in typing —, the encoding and the file on a pool thread (inline without one), never for small
// notebooks nor above its size limit (indexCacheBudget() where storage has a quota), and reports a
// folder it cannot write; a file written after edits loads to the index the edits made.
#include "notebook/indexcachekeeper.h"
#include "notebook/indexloader.h"

#include "filesystemtraits.h"
#include "index/index.h"
#include "indexcompare.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;
using namespace brulion::testing;

namespace {

void writeFile(const QString &root, const QString &rel, const QByteArray &bytes, qint64 mtime = 0)
{
    const QString abs = root + u'/' + rel;
    QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
    QFile f(abs);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(bytes), bytes.size());
    if (mtime > 0)
        QVERIFY(f.setFileTime(QDateTime::fromMSecsSinceEpoch(mtime), QFileDevice::FileModificationTime));
}

qint64 fileMtime(const QString &abs)
{
    return QFileInfo(abs).fileTime(QFileDevice::FileModificationTime).toMSecsSinceEpoch();
}

void writeVault(const QString &root, int notes)
{
    writeFile(root, u"Dziennik/2026-10-28 środa.md"_s, "- [ ] zadanie ⏰ 2026-10-29 08:00 #praca\n");
    writeFile(root, u"journals/2026-10-28.md"_s, "- dziennik [[Strona 1]]\n");
    writeFile(root, u"assets/zdjęcie.png"_s, "\x89PNG");
    writeFile(root, u"Żółw.md"_s, "---\naliases: [Zolw]\ntags: [zwierzę]\n---\n- żółw ^id1\n");
    for (int i = 0; i < notes; ++i) {
        const QByteArray n = QByteArray::number(i);
        writeFile(root, u"Strony/Strona %1.md"_s.arg(i),
                  "- Strona " + n + " o żółwiu [[Strona " + QByteArray::number((i * 3) % notes) + "]] #t"
                          + QByteArray::number(i % 4) + "\n  - ((id1)) spotkanie " + n + "\n");
    }
}

/// A fresh index of the vault, file by file.
std::unique_ptr<VaultIndex> serialIndex(const QString &root, const VaultConfig &config = {})
{
    Vault vault(root);
    if (!vault.open())
        return nullptr;
    auto index = std::make_unique<VaultIndex>(config);
    for (const FileStat &f : vault.files()) {
        if (!paths::isIgnored(f.path))
            index->update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
    }
    return index;
}

struct Loaded {
    std::unique_ptr<Vault> vault;
    std::unique_ptr<VaultIndex> index;
    IndexLoader::Stats stats;
};

/// Load `root` with the cache at `cachePath` (threads 0: inline).
Loaded load(const QString &root, const QString &cachePath, int threads, QThreadPool &pool,
            const VaultConfig *config = nullptr)
{
    Loaded out;
    out.vault = std::make_unique<Vault>(root);
    if (!out.vault->open())
        return out;
    if (config)
        out.vault->saveConfig(*config);
    out.index = std::make_unique<VaultIndex>(out.vault->config());
    WorkerPool workers;
    if (threads > 0) {
        pool.setMaxThreadCount(threads + 1);
        workers = {threads, [&pool](std::function<void()> job) { pool.start(std::move(job)); }};
    }
    IndexLoader::Options options;
    options.cachePath = cachePath;
    IndexLoader loader(*out.vault, *out.index, workers, options);
    loader.start(out.vault->files());
    loader.finish();
    out.stats = loader.stats();
    return out;
}

/// Write the cache of a loaded notebook (what the keeper does).
bool save(Loaded &l, const QString &cachePath, qsizetype minNotes = 0)
{
    IndexCacheKeeper::Policy policy;
    policy.minNotes = minNotes;
    IndexCacheKeeper keeper(*l.vault, *l.index, {}, cachePath, policy);
    return keeper.saveNow();
}

} // namespace

class TestNotebookIndexCache : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void validation_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<int>("threads");
        QTest::addColumn<int>("misses"); // notes parsed (not from the cache); -1: the cache unused
        for (const int threads : {0, 3}) {
            const QString t = threads ? u" (threads)"_s : u" (inline)"_s;
            QTest::newRow(qPrintable(u"unchanged"_s + t)) << u"unchanged"_s << threads << 0;
            QTest::newRow(qPrintable(u"touched"_s + t)) << u"touched"_s << threads << 0;
            QTest::newRow(qPrintable(u"same size and time"_s + t)) << u"same size and time"_s << threads << 1;
            QTest::newRow(qPrintable(u"appended"_s + t)) << u"appended"_s << threads << 1;
            QTest::newRow(qPrintable(u"renamed"_s + t)) << u"renamed"_s << threads << 1;
            QTest::newRow(qPrintable(u"new file"_s + t)) << u"new file"_s << threads << 1;
            QTest::newRow(qPrintable(u"deleted"_s + t)) << u"deleted"_s << threads << 0;
            QTest::newRow(qPrintable(u"BOM added"_s + t)) << u"BOM added"_s << threads << 1;
            QTest::newRow(qPrintable(u"journal config"_s + t)) << u"journal config"_s << threads << -1;
            QTest::newRow(qPrintable(u"corrupt file"_s + t)) << u"corrupt file"_s << threads << -1;
            QTest::newRow(qPrintable(u"no cache file"_s + t)) << u"no cache file"_s << threads << -1;
        }
    }
    void validation()
    {
        QFETCH(QString, change);
        QFETCH(int, threads);
        QFETCH(int, misses);
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/Notatki"_s;
        const QString cache = tmp.path() + u"/state/index-cache.bin"_s;
        writeVault(root, 40);
        QThreadPool pool;
        {
            Loaded first = load(root, cache, threads, pool);
            QCOMPARE(first.stats.cache, u"absent"_s);
            QCOMPARE(first.stats.parsed, 43); // 40 pages, 2 journals, Żółw
            first.index->flushSearch();
            QVERIFY(save(first, cache));
        }
        const QString target = u"Strony/Strona 7.md"_s;
        const QString abs = root + u'/' + target;
        const QByteArray before = [&] {
            QFile f(abs);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        VaultConfig config;
        bool configChanged = false;
        if (change == u"touched") {
            writeFile(root, target, before, fileMtime(abs) + 3600 * 1000);
        } else if (change == u"same size and time") {
            QByteArray other = before;
            other.replace("Strona 7 o", "Strona 7 0"); // same size
            QCOMPARE(other.size(), before.size());
            writeFile(root, target, other, fileMtime(abs));
        } else if (change == u"appended") {
            writeFile(root, target, before + "- dopisane [[Nowa]]\n");
        } else if (change == u"renamed") {
            QVERIFY(QDir().rename(abs, root + u"/Strony/Przeniesiona.md"_s));
        } else if (change == u"new file") {
            writeFile(root, u"Nowa.md"_s, "- nowa strona [[Żółw]]\n");
        } else if (change == u"deleted") {
            QVERIFY(QFile::remove(abs));
        } else if (change == u"BOM added") {
            writeFile(root, target, "\xEF\xBB\xBF" + before);
        } else if (change == u"journal config") {
            config.journalsFolder = u"Dziennik"_s;
            configChanged = true;
        } else if (change == u"corrupt file") {
            QFile f(cache);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.seek(f.size() - 64)); // in the directory: the whole cache is unusable
            QVERIFY(f.write("XXXXXXXX") == 8);
        } else if (change == u"no cache file") {
            QVERIFY(QFile::remove(cache));
        }
        Loaded second = load(root, cache, threads, pool, configChanged ? &config : nullptr);
        QVERIFY(second.index);
        const qsizetype notes = second.index->paths().size();
        if (misses < 0) {
            QCOMPARE(second.stats.fromCache, 0);
            QCOMPARE(second.stats.parsed, notes);
            QVERIFY2(second.stats.cache != u"used", qPrintable(second.stats.cache));
            if (change == u"journal config")
                QVERIFY(second.stats.cache.startsWith(u"invalid: "_s));
        } else {
            QCOMPARE(second.stats.cache, u"used"_s);
            QCOMPARE(second.stats.parsed, misses);
            QCOMPARE(second.stats.fromCache, notes - misses);
            QCOMPARE(second.stats.recordsRejected, 0);
        }
        const std::unique_ptr<VaultIndex> fresh = serialIndex(root, second.vault->config());
        QVERIFY(fresh);
        compareIndexes(*second.index, *fresh, change);
        if (threads > 0 && misses >= 0) {
            QVERIFY(second.stats.searchFromCache);
            QCOMPARE(second.index->pendingSearchDocuments(), 0); // parsed notes: built on the search thread
        }
        for (const QString &q : {u"zolw"_s, u"spotkanie 7"_s, u"dopisane"_s, u"strona"_s})
            QCOMPARE(describeSearch(second.index->search(q)), describeSearch(fresh->search(q)));
    }

    // Written while the full-text index was incomplete: the documents it had; a warm load builds
    // the rest (in the background, or at once for a search).
    void cacheWithoutSearch()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        const QString cache = tmp.path() + u"/index-cache.bin"_s;
        writeVault(root, 20);
        QThreadPool pool;
        {
            Loaded first = load(root, cache, 0, pool); // inline: the search is not built yet
            QVERIFY(first.index->pendingSearchDocuments() > 0);
            QVERIFY(save(first, cache));
        }
        Loaded second = load(root, cache, 2, pool);
        QCOMPARE(second.stats.cache, u"used"_s);
        QVERIFY(second.stats.searchFromCache);
        QCOMPARE(second.stats.fromCache, 23);
        // The documents the file lacked are built again (on the search worker, or by search()).
        QVERIFY(second.index->pendingSearchDocuments() > 0);
        const std::unique_ptr<VaultIndex> fresh = serialIndex(root);
        QCOMPARE(describeSearch(second.index->search(u"zolw"_s)), describeSearch(fresh->search(u"zolw"_s)));
    }

    // The keeper: saves soon after a load that parsed, again at the latest maxDirtyMs after a
    // change; nothing for small notebooks, nothing above the size limit; a folder it cannot write
    // is reported.
    void keeperSaves()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        const QString cache = tmp.path() + u"/state/index-cache.bin"_s;
        writeVault(root, 30);
        QThreadPool pool;
        Loaded l = load(root, cache, 0, pool);
        l.index->flushSearch();
        IndexCacheKeeper::Policy policy;
        policy.firstSaveQuietMs = 10;
        policy.maxDirtyMs = 50;
        policy.minNotes = 10;
        pool.setMaxThreadCount(2);
        IndexCacheKeeper keeper(*l.vault, *l.index,
                                WorkerPool{1, [&pool](std::function<void()> j) { pool.start(std::move(j)); }}, cache,
                                policy);
        QSignalSpy saved(&keeper, &IndexCacheKeeper::saved);
        keeper.loaded(l.stats);
        QVERIFY(keeper.isDirty());
        QVERIFY(saved.wait(5000));
        QVERIFY(QFileInfo::exists(cache));
        QVERIFY(saved.first().at(0).toLongLong() > 0);
        QVERIFY(!keeper.isDirty());
        QTRY_VERIFY(!keeper.isSaving());
        // A change: saved again soon (maxDirtyMs).
        l.index->update(u"Nowa.md"_s, u"- nowa"_s);
        QTRY_VERIFY(keeper.isDirty() || saved.count() == 2);
        QVERIFY(saved.wait(5000) || saved.count() == 2);
        QCOMPARE(saved.count(), 2);
        // A load from it takes everything from the cache (Nowa.md is not on disk: absent).
        Loaded warm = load(root, cache, 0, pool);
        QCOMPARE(warm.stats.fromCache, 33);
        // Small notebooks get none.
        IndexCacheKeeper::Policy big = policy;
        big.minNotes = 1000;
        IndexCacheKeeper small(*l.vault, *l.index, {}, tmp.path() + u"/small.bin"_s, big);
        QVERIFY(!small.saveNow());
        QVERIFY(!QFileInfo::exists(tmp.path() + u"/small.bin"_s));
        // Above the size limit: nothing written (the old file stays).
        IndexCacheKeeper::Policy tiny = policy;
        tiny.maxBytes = 100;
        IndexCacheKeeper limited(*l.vault, *l.index, {}, cache, tiny);
        const qint64 oldSize = QFileInfo(cache).size();
        QSignalSpy failed(&limited, &IndexCacheKeeper::saveFailed);
        QVERIFY(!limited.saveNow());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(QFileInfo(cache).size(), oldSize);
    }

    // Changes are saved when the app asks (saveIfDirty(): the app goes to the background, the
    // notebook closes) and after maxDirtyMs of unsaved changes — however often they come: not
    // 60 s after every pause in typing. The file written loads to the index the edits made.
    void keeperSavesWhenAskedNotAfterEachChange_data()
    {
        QTest::addColumn<int>("threads");
        QTest::newRow("pool") << 2;
        QTest::newRow("inline") << 0;
    }
    void keeperSavesWhenAskedNotAfterEachChange()
    {
        QFETCH(int, threads);
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/Zeszyt żółwia"_s;
        const QString cache = tmp.path() + u"/stan/index-cache.bin"_s;
        writeVault(root, 30);
        QThreadPool pool;
        pool.setMaxThreadCount(threads + 1);
        const WorkerPool workers =
                threads > 0 ? WorkerPool{threads, [&pool](std::function<void()> j) { pool.start(std::move(j)); }}
                            : WorkerPool{};
        {
            Loaded l = load(root, QString(), threads, pool);
            l.index->flushSearch();
            IndexCacheKeeper::Policy policy;
            policy.firstSaveQuietMs = 600000;
            policy.maxDirtyMs = 600000;
            policy.minNotes = 10;
            IndexCacheKeeper keeper(*l.vault, *l.index, workers, cache, policy);
            QSignalSpy saved(&keeper, &IndexCacheKeeper::saved);
            keeper.loaded(l.stats);
            QVERIFY(keeper.isDirty());
            // Edits on disk and in the index, with pauses: nothing is written meanwhile.
            for (int i = 0; i < 5; ++i) {
                const QString path = u"Strony/Strona %1.md"_s.arg(i);
                const QByteArray text = "- zmieniona strona " + QByteArray::number(i) + " o źdźble [[Żółw]]\n";
                writeFile(root, path, text);
                l.index->update(path, QString::fromUtf8(text));
                QTest::qWait(30);
            }
            writeFile(root, u"Nowa ćma.md"_s, "- nowa ćma #owady\n");
            l.index->update(u"Nowa ćma.md"_s, u"- nowa ćma #owady\n"_s);
            QVERIFY(QFile::remove(root + u"/Strony/Strona 9.md"_s));
            l.index->remove(u"Strony/Strona 9.md"_s);
            l.vault->rescan(); // the vault's sizes of the edited files
            QTest::qWait(200);
            QCOMPARE(saved.count(), 0);
            QVERIFY(!QFileInfo::exists(cache));
            // The app goes to the background: written (on the pool, or right here inline).
            keeper.saveIfDirty();
            QVERIFY(!keeper.isDirty());
            if (threads == 0) {
                QCOMPARE(saved.count(), 1);
                QVERIFY(!keeper.isSaving());
            } else {
                QTRY_COMPARE(saved.count(), 1);
            }
            QVERIFY(QFileInfo::exists(cache));
            // Nothing new: asking again writes nothing.
            keeper.saveIfDirty();
            QTest::qWait(50);
            QCOMPARE(saved.count(), 1);
            QVERIFY(!keeper.isSaving());
        }
        pool.waitForDone();
        // Loaded from that file: the index the edits made, every note from the cache.
        Loaded warm = load(root, cache, threads, pool);
        QCOMPARE(warm.stats.cache, u"used"_s);
        QCOMPARE(warm.stats.parsed, 0);
        QVERIFY(warm.stats.searchFromCache);
        const std::unique_ptr<VaultIndex> fresh = serialIndex(root, warm.vault->config());
        compareIndexes(*warm.index, *fresh, u"saved after edits"_s);
        for (const QString &q : {u"zdzblo"_s, u"ćma"_s, u"spotkanie 9"_s, u"zmieniona strona"_s})
            QCOMPARE(describeSearch(warm.index->search(q)), describeSearch(fresh->search(q)));
    }

    // While the app stays in front, changes that never pause are written maxDirtyMs after the
    // first of them (a pause does not postpone it); the file (encoded on the pool, or in slices
    // on this thread) loads.
    void keeperSavesAfterTheLongestWait_data()
    {
        QTest::addColumn<int>("threads");
        QTest::newRow("pool") << 1;
        QTest::newRow("inline, in slices") << 0;
    }
    void keeperSavesAfterTheLongestWait()
    {
        QFETCH(int, threads);
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        const QString cache = tmp.path() + u"/index-cache.bin"_s;
        writeVault(root, 20);
        QThreadPool pool;
        Loaded l = load(root, QString(), 0, pool);
        l.index->flushSearch();
        IndexCacheKeeper::Policy policy;
        policy.firstSaveQuietMs = 600000;
        policy.maxDirtyMs = 300;
        policy.minNotes = 10;
        pool.setMaxThreadCount(2);
        policy.sliceMs = 1;
        const WorkerPool workers =
                threads > 0 ? WorkerPool{threads, [&pool](std::function<void()> j) { pool.start(std::move(j)); }}
                            : WorkerPool{};
        IndexCacheKeeper keeper(*l.vault, *l.index, workers, cache, policy);
        QSignalSpy saved(&keeper, &IndexCacheKeeper::saved);
        IndexLoader::Stats used = l.stats;
        used.cache = u"used"_s;
        used.parsed = 0;
        used.searchFromCache = true;
        keeper.loaded(used); // nothing to save after the load
        QVERIFY(!keeper.isDirty());
        QElapsedTimer clock;
        clock.start();
        int edit = 0;
        while (saved.count() == 0 && clock.elapsed() < 10000) {
            l.index->update(u"Pisanie.md"_s, u"- tekst pisany bez przerwy %1"_s.arg(++edit));
            QTest::qWait(40);
        }
        QCOMPARE(saved.count(), 1);
        QVERIFY2(clock.elapsed() < 3000, qPrintable(QString::number(clock.elapsed())));
        pool.waitForDone();
        Loaded warm = load(root, cache, 0, pool);
        QCOMPARE(warm.stats.cache, u"used"_s);
        QCOMPARE(warm.stats.fromCache, 23);
    }

    // The keeper goes (the notebook closes) while its file is written on the pool: the write
    // finishes; a save of an older snapshot never replaces a newer file.
    void keeperClosesWhileWriting()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        const QString cache = tmp.path() + u"/index-cache.bin"_s;
        writeVault(root, 20);
        QThreadPool pool;
        pool.setMaxThreadCount(2);
        Loaded l = load(root, QString(), 0, pool);
        l.index->flushSearch();
        IndexCacheKeeper::Policy policy;
        policy.minNotes = 10;
        {
            IndexCacheKeeper keeper(*l.vault, *l.index,
                                    WorkerPool{1, [&pool](std::function<void()> j) { pool.start(std::move(j)); }},
                                    cache, policy);
            keeper.loaded(l.stats);
            keeper.saveIfDirty();
            QVERIFY(keeper.isSaving());
        }
        pool.waitForDone();
        Loaded warm = load(root, cache, 0, pool);
        QCOMPARE(warm.stats.cache, u"used"_s);
        QCOMPARE(warm.stats.parsed, 0);
    }

    void keeperReportsAFolderItCannotWrite()
    {
        if (!brulion::test::hasUnixPermissions())
            QSKIP("no Unix permissions here");
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        writeVault(root, 12);
        const QString dir = tmp.path() + u"/zablokowany"_s;
        QVERIFY(QDir().mkpath(dir));
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        if (QFileInfo(dir).isWritable())
            QSKIP("permissions are not enforced (root)");
        QThreadPool pool;
        Loaded l = load(root, QString(), 0, pool);
        IndexCacheKeeper::Policy policy;
        policy.minNotes = 1;
        IndexCacheKeeper keeper(*l.vault, *l.index, {}, dir + u"/index-cache.bin"_s, policy);
        QSignalSpy failed(&keeper, &IndexCacheKeeper::saveFailed);
        QVERIFY(!keeper.saveNow());
        QCOMPARE(failed.count(), 1);
        QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

    // Where storage has a quota (the browser): the old cache plus half of what is free, capped.
    void budget_data()
    {
        QTest::addColumn<qint64>("usage");
        QTest::addColumn<qint64>("quota");
        QTest::addColumn<qint64>("replaced");
        QTest::addColumn<qint64>("expected");
        const qint64 cap = 256LL << 20;
        const qint64 mb = 1LL << 20;
        QTest::newRow("unknown usage") << qint64(-1) << 100 * mb << qint64(0) << cap;
        QTest::newRow("unknown quota") << 10 * mb << qint64(-1) << qint64(0) << cap;
        QTest::newRow("quota 0") << 10 * mb << qint64(0) << 5 * mb << cap;
        QTest::newRow("plenty of room: capped") << 50 * mb << 300'000 * mb << qint64(0) << cap;
        QTest::newRow("half of what is free") << 60 * mb << 100 * mb << qint64(0) << 20 * mb;
        QTest::newRow("plus the old cache") << 60 * mb << 100 * mb << 8 * mb << 28 * mb;
        QTest::newRow("full: only the old one's size") << 100 * mb << 100 * mb << 8 * mb << 8 * mb;
        QTest::newRow("over the quota") << 120 * mb << 100 * mb << qint64(0) << qint64(0);
        QTest::newRow("a missing old file") << 60 * mb << 100 * mb << qint64(-1) << 20 * mb;
    }
    void budget()
    {
        QFETCH(qint64, usage);
        QFETCH(qint64, quota);
        QFETCH(qint64, replaced);
        QFETCH(qint64, expected);
        QCOMPARE(indexCacheBudget(usage, quota, replaced, 256LL << 20), expected);
    }

    // A budget below the encoded size: nothing is written, the old file stays.
    void keeperKeepsWithinItsBudget()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/Notatnik żółwia"_s;
        writeVault(root, 30);
        QThreadPool pool;
        Loaded l = load(root, QString(), 0, pool);
        IndexCacheKeeper::Policy policy;
        policy.minNotes = 1;
        policy.maxBytes = indexCacheBudget(1000, 2000, 0, 256LL << 20); // 500 bytes
        const QString cache = tmp.path() + u"/stan/index-cache.bin"_s;
        IndexCacheKeeper keeper(*l.vault, *l.index, {}, cache, policy);
        QSignalSpy failed(&keeper, &IndexCacheKeeper::saveFailed);
        QVERIFY(!keeper.saveNow());
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed.at(0).at(0).toString().startsWith(u"larger than"_s));
        QVERIFY(!QFileInfo::exists(cache));
    }
};

QTEST_GUILESS_MAIN(TestNotebookIndexCache)
#include "tst_notebook_indexcache.moc"
