// The index cache on disk (index/indexcache.h, notebook/indexcachekeeper.h): a notebook loaded
// with IndexLoader from a cache written by IndexCacheKeeper equals one built by parsing; a note is
// taken from the cache only while its path, size and text match (a touched file is a hit, a
// same-size same-time edit a miss), another journal configuration or a corrupt file makes the
// whole cache unused; the full-text index comes from the cache (or is rebuilt when the cache had
// none); the keeper saves after a load that parsed, after quiet changes, never for small notebooks
// nor above its size limit, and reports a folder it cannot write.
#include "notebook/indexcachekeeper.h"
#include "notebook/indexloader.h"

#include "filesystemtraits.h"
#include "index/index.h"
#include "indexcompare.h"
#include "util/paths.h"
#include "vault/vault.h"

#include <QDir>
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
                  "- Strona " + n + " o żółwiu [[Strona " + QByteArray::number((i * 3) % notes) + "]] #t" +
                          QByteArray::number(i % 4) + "\n  - ((id1)) spotkanie " + n + "\n");
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

    // Written while the full-text index was incomplete: no search section; a warm load builds it.
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
        QVERIFY(!second.stats.searchFromCache);
        QCOMPARE(second.stats.fromCache, 23);
        QCOMPARE(second.index->pendingSearchDocuments(), 0); // built by the loader's search thread
        const std::unique_ptr<VaultIndex> fresh = serialIndex(root);
        QCOMPARE(describeSearch(second.index->search(u"zolw"_s)), describeSearch(fresh->search(u"zolw"_s)));
    }

    // The keeper: saves soon after a load that parsed, again after quiet changes; nothing for
    // small notebooks, nothing above the size limit; a folder it cannot write is reported.
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
        policy.quietMs = 50;
        policy.minNotes = 10;
        pool.setMaxThreadCount(2);
        IndexCacheKeeper keeper(*l.vault, *l.index, WorkerPool{1, [&pool](std::function<void()> j) { pool.start(std::move(j)); }},
                                cache, policy);
        QSignalSpy saved(&keeper, &IndexCacheKeeper::saved);
        keeper.loaded(l.stats);
        QVERIFY(keeper.isDirty());
        QVERIFY(saved.wait(5000));
        QVERIFY(QFileInfo::exists(cache));
        QVERIFY(saved.first().at(0).toLongLong() > 0);
        QVERIFY(!keeper.isDirty());
        // A change: saved again once quiet.
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
};

QTEST_GUILESS_MAIN(TestNotebookIndexCache)
#include "tst_notebook_indexcache.moc"
