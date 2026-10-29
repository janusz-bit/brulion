// IndexLoader (notebook/indexloader.h): the index loaded on worker threads — inline (no
// threads, WebAssembly), on 1 and 4 QThreadPool threads, on plain std::threads — equals an index
// built file by file on one thread, for a vault of tricky files (case variants, NFD names,
// `.markdown`, attachments, conflict copies, ignored folders, empty files, BOM, CRLF, invalid
// UTF-8, a 512 KB note, Polish names); files changed while loading (before the first step, mid-load,
// after a worker read them but before the merge) end up as on disk; finish() in every state,
// destruction mid-load, a config change mid-load, back-pressure, the vault's bookkeeping of the
// workers' reads, journals merged first, search during and after loading. (Sizes are kept small:
// CI runs this under AddressSanitizer within ctest's 300 s.)
#include "notebook/indexloader.h"

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

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;
using namespace brulion::testing;

namespace {

void writeFile(const QString &root, const QString &rel, const QByteArray &bytes)
{
    const QString abs = root + u'/' + rel;
    QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
    QFile f(abs);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(f.write(bytes), bytes.size());
}

/// Notes that link to each other in every way the index resolves, plus the hard cases.
void writeTrickyVault(const QString &root, int generated, bool bigNote = false)
{
    writeFile(root, u"A.md"_s, "- [[a]] i [[B]] #tag\n- ((id1)) ^id0\n");
    writeFile(root, u"x/sub/a.md"_s, "- mała litera [[A]] [[sub/a]] ^id1\n");
    writeFile(root, u"x/A.md"_s, "---\naliases: [Alias, \"Łódź\"]\n---\n- [[./A]] [[../A]] [[/A]]\n");
    writeFile(root, u"Z\u0307o\u0301\u0142w.md"_s, "- NFD [[Żółw]] #żółw\n"); // an NFD name (macOS)
    writeFile(root, u"y/c.markdown"_s, "- [[C]] [[c.markdown]]\n");
    writeFile(root, u"C.md"_s, "\xEF\xBB\xBF- BOM i [[Alias]]\r\n- CRLF ⏰ 2026-10-29 08:00\r\n");
    writeFile(root, u"assets/zdjęcie.png"_s, "\x89PNG");
    writeFile(root, u"assets/plik.pdf"_s, "%PDF");
    writeFile(root, u"A.sync-conflict-20261028-120000-ABCDEFG.md"_s, "- konflikt\n");
    writeFile(root, u".trash/stara.md"_s, "- w koszu\n");
    writeFile(root, u"Pusta.md"_s, "");
    writeFile(root, u"Zły UTF-8.md"_s, "- \xff\xfe [[A]]\n");
    writeFile(root, u"journals/2026-10-27.md"_s, "- [ ] zadanie 📅 2026-10-28 #praca\n");
    writeFile(root, u"journals/2026-10-28.md"_s, "- [[Spotkanie]] i [[Brak]] ⏰ 21:00\n");
    writeFile(root, u"Dziennik/2026-10-28 środa.md"_s, "- [[journals/2026-10-28]]\n");
    if (bigNote) { // 512 KB (no links: backlinks() and unlinked references stay cheap)
        QByteArray big;
        for (int i = 0; big.size() < 512 * 1024; ++i)
            big += "- wiersz " + QByteArray::number(i) + " o żółwiu\n";
        writeFile(root, u"Duża.md"_s, big);
    }
    for (int i = 0; i < generated; ++i) {
        const QByteArray n = QByteArray::number(i);
        writeFile(root, u"Strony/Strona %1.md"_s.arg(i),
                  "- Strona " + n + " [[Strona " + QByteArray::number((i * 7) % generated) + "]] #t"
                          + QByteArray::number(i % 5) + "\n  - dziecko ((id1))\n- [ ] zadanie " + n + " 📅 2026-11-0"
                          + QByteArray::number(1 + i % 9) + "\n");
    }
}

/// The index built file by file on this thread, in path order (what Workspace did before L1).
std::unique_ptr<VaultIndex> serialIndex(Vault &vault)
{
    auto index = std::make_unique<VaultIndex>(vault.config());
    VaultIndex::Batch batch(*index);
    for (const FileStat &f : vault.files()) {
        if (paths::isIgnored(f.path))
            continue;
        if (paths::isMarkdown(f.path)) {
            bool ok = false;
            const QString text = vault.readText(f.path, &ok);
            if (ok)
                index->update(f.path, text);
        } else {
            index->update(f.path, QString());
        }
    }
    return index;
}

/// A pool of plain std::threads (joined at the end): the TSan configuration.
class StdThreadPool {
public:
    ~StdThreadPool() { join(); }
    WorkerPool pool(int threads)
    {
        return WorkerPool{threads, [this](std::function<void()> job) {
                              std::lock_guard lock(m_mutex);
                              m_threads.emplace_back(std::move(job));
                          }};
    }
    void join()
    {
        std::deque<std::thread> threads;
        {
            std::lock_guard lock(m_mutex);
            threads.swap(m_threads);
        }
        for (std::thread &t : threads)
            t.join();
    }

private:
    std::mutex m_mutex;
    std::deque<std::thread> m_threads;
};

/// A pool whose jobs run only when the test says so (on std::threads).
class ManualPool {
public:
    ~ManualPool() { join(); }
    WorkerPool pool(int threads)
    {
        return WorkerPool{threads, [this](std::function<void()> job) { m_jobs.append(std::move(job)); }};
    }
    void runAll()
    {
        for (std::function<void()> &job : m_jobs)
            m_threads.emplace_back(std::move(job));
        m_jobs.clear();
    }
    void join()
    {
        for (std::thread &t : m_threads)
            t.join();
        m_threads.clear();
    }
    qsizetype queued() const { return m_jobs.size(); }

private:
    QList<std::function<void()>> m_jobs;
    std::deque<std::thread> m_threads;
};

enum class PoolKind { Inline, QtOne, QtFour, StdThreads };

/// The notes the loader reads (Markdown, indexable).
qsizetype noteCount(const Vault &vault)
{
    qsizetype n = 0;
    for (const FileStat &f : vault.files())
        n += paths::isMarkdown(f.path) && VaultIndex::isIndexable(f.path) ? 1 : 0;
    return n;
}

/// Wait until the workers prepared `count` notes (none merged yet).
void waitPending(const IndexLoader &loader, qsizetype count)
{
    QElapsedTimer t;
    t.start();
    while (loader.pendingResults() < count && t.elapsed() < 60000 * BRULION_TEST_SLOWDOWN)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    QCOMPARE(loader.pendingResults(), count);
}

/// Load with step() as the app does (the event loop delivers resultsReady()).
void stepToTheEnd(IndexLoader &loader)
{
    QElapsedTimer guard;
    guard.start();
    for (;;) {
        const IndexLoader::Step s = loader.step(5);
        if (s == IndexLoader::Step::Done)
            break;
        if (s == IndexLoader::Step::Waiting)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QVERIFY2(guard.elapsed() < 120000 * BRULION_TEST_SLOWDOWN, "the loader never finished");
    }
}

/// Wait until the full-text index built elsewhere is adopted (posted to the owner).
void waitForSearch(VaultIndex &index, IndexLoader &loader)
{
    Q_UNUSED(loader);
    QElapsedTimer guard;
    guard.start();
    while (guard.elapsed() < 30000 * BRULION_TEST_SLOWDOWN) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        if (index.pendingSearchDocuments() == 0)
            break;
    }
}

} // namespace

class TestNotebookIndexLoader : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    QString m_root;

    WorkerPool makePool(PoolKind kind, QThreadPool &qt, StdThreadPool &std)
    {
        switch (kind) {
        case PoolKind::Inline: return {};
        case PoolKind::QtOne:
        case PoolKind::QtFour: {
            const int n = kind == PoolKind::QtOne ? 1 : 4;
            qt.setMaxThreadCount(n + 1);
            return WorkerPool{n, [&qt](std::function<void()> job) { qt.start(std::move(job)); }};
        }
        case PoolKind::StdThreads: return std.pool(3);
        }
        return {};
    }

    static void addPoolRows()
    {
        QTest::addColumn<int>("kind");
        QTest::newRow("inline") << int(PoolKind::Inline);
        QTest::newRow("1 Qt thread") << int(PoolKind::QtOne);
        QTest::newRow("4 Qt threads") << int(PoolKind::QtFour);
        QTest::newRow("3 std::threads") << int(PoolKind::StdThreads);
    }

private Q_SLOTS:
    void initTestCase()
    {
        QVERIFY(m_tmp.isValid());
        m_root = m_tmp.path() + u"/Notatki"_s;
        writeTrickyVault(m_root, 150, true);
    }

    void loadEqualsSerial_data() { addPoolRows(); }
    void loadEqualsSerial()
    {
        QFETCH(int, kind);
        QThreadPool qt;
        StdThreadPool std;
        Vault vault(m_root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        IndexLoader::Options options;
        options.firstFolder = vault.config().journalsFolder;
        {
            IndexLoader loader(vault, index, makePool(PoolKind(kind), qt, std), options);
            QSignalSpy finished(&loader, &IndexLoader::finished);
            loader.start(vault.files());
            QVERIFY(!loader.isDone());
            stepToTheEnd(loader);
            QVERIFY(loader.isDone());
            QCOMPARE(finished.count(), 1);
            QCOMPARE(loader.progress(), 1.0);
            const IndexLoader::Stats stats = loader.stats();
            QCOMPARE(stats.attachments, 2);
            QCOMPARE(stats.total, 2 + 162); // the conflict copy and the trash are not read
            QCOMPARE(noteCount(vault), 162);
            QCOMPARE(stats.merged, stats.total);
            QCOMPARE(stats.parsed, 162);
            QCOMPARE(stats.dropped, 0);
            QCOMPARE(stats.readFailed, 0);
            const int workers[] = {0, 1, 4, 3};
            QCOMPARE(stats.workers, workers[kind]);
            waitForSearch(index, loader);
        }
        std.join();
        Vault serialVault(m_root);
        QVERIFY(serialVault.open());
        const std::unique_ptr<VaultIndex> serial = serialIndex(serialVault);
        compareIndexes(index, *serial, u"loaded"_s);
        QCOMPARE(describeRecency(index), describeRecency(*serial));
        QCOMPARE(index.pendingSearchDocuments(), 0);
        for (const QString &q : {u"zolw"_s, u"spotkanie"_s, u"strona 12"_s, u"duzy"_s})
            QCOMPARE(describeSearch(index.search(q)), describeSearch(serial->search(q)));
    }

    // finish() blocks until everything is merged (taking unstarted files itself) and is a no-op
    // when done; destruction mid-load never waits for the workers nor crashes.
    void finishInEveryState_data() { addPoolRows(); }
    void finishInEveryState()
    {
        QFETCH(int, kind);
        QThreadPool qt;
        StdThreadPool std;
        Vault vault(m_root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        {
            IndexLoader loader(vault, index, makePool(PoolKind(kind), qt, std), {});
            loader.start(vault.files());
            loader.step(1);
            loader.finish();
            QVERIFY(loader.isDone());
            QCOMPARE(loader.stats().merged, loader.stats().total);
            loader.finish(); // twice: nothing
            QCOMPARE(loader.step(10), IndexLoader::Step::Done);
        }
        std.join();
        Vault serialVault(m_root);
        QVERIFY(serialVault.open());
        compareIndexes(index, *serialIndex(serialVault), u"finished"_s);
        // Destroyed mid-load: no crash, the workers stop after their file.
        VaultIndex other(vault.config());
        {
            IndexLoader loader(vault, other, makePool(PoolKind(kind), qt, std), {});
            loader.start(vault.files());
            loader.step(1);
        }
        std.join();
        qt.waitForDone();
    }

    // A file changed after the scan: before the first step, mid-load, and after a worker read it
    // but before its merge — the index ends up as the disk (the vault reports the change, the
    // caller forgets the path and indexes it itself, as Workspace does).
    void changesDuringLoad_data()
    {
        QTest::addColumn<QString>("change");
        QTest::addColumn<int>("when"); // 0 before the first step, 1 mid-load, 2 read but not merged
        for (const QString &c : {u"modify"_s, u"delete"_s, u"rename"_s, u"create"_s}) {
            QTest::newRow(qPrintable(c + u" before"_s)) << c << 0;
            QTest::newRow(qPrintable(c + u" mid-load"_s)) << c << 1;
            QTest::newRow(qPrintable(c + u" after the read"_s)) << c << 2;
        }
    }
    void changesDuringLoad()
    {
        QFETCH(QString, change);
        QFETCH(int, when);
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        writeTrickyVault(root, 60);
        Vault vault(root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        ManualPool manual;
        IndexLoader::Options options;
        options.maxPending = 100000;
        IndexLoader loader(vault, index, manual.pool(2), options);
        QSignalSpy vaultChanges(&vault, &Vault::changed);
        // What Workspace::onVaultChanged() does.
        QObject::connect(&vault, &Vault::changed, &loader, [&](const QList<VaultChange> &changes) {
            for (const VaultChange &c : changes) {
                loader.forget(c.path);
                loader.forget(c.oldPath);
                if (c.type == ChangeType::Deleted) {
                    index.remove(c.path);
                } else if (c.type == ChangeType::Renamed) {
                    index.rename(c.oldPath, c.path, vault.readText(c.path));
                } else {
                    index.update(c.path, vault.readText(c.path));
                }
            }
        });
        loader.start(vault.files());
        const QString target = u"Strony/Strona 7.md"_s;
        auto applyChange = [&] {
            if (change == u"modify")
                writeFile(root, target, "- zmieniona po skanowaniu [[Nowa]] #zmiana\n");
            else if (change == u"delete")
                QVERIFY(QFile::remove(root + u'/' + target));
            else if (change == u"rename")
                QVERIFY(QDir().rename(root + u'/' + target, root + u"/Strony/Przeniesiona.md"_s));
            else
                writeFile(root, u"Nowa.md"_s, "- nowa strona [[A]]\n");
        };
        if (when == 0)
            applyChange();
        manual.runAll();
        if (when == 2) {
            waitPending(loader, noteCount(vault)); // every note read and prepared, none merged
            applyChange();
        }
        if (when == 1) {
            QElapsedTimer t;
            t.start();
            while (loader.pendingResults() < 10 && t.elapsed() < 10000)
                std::this_thread::yield();
            loader.step(0);
            applyChange();
        }
        vault.rescan(); // the watcher's report
        vault.processPendingEvents();
        stepToTheEnd(loader);
        manual.join();
        QVERIFY(vaultChanges.count() > 0);
        Vault fresh(root);
        QVERIFY(fresh.open());
        compareIndexes(index, *serialIndex(fresh), change);
        if (when == 2 && change != u"create")
            QVERIFY(loader.stats().dropped >= 1);
    }

    void configChangeMidLoad()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        writeTrickyVault(root, 40);
        writeFile(root, u"Dziennik/2026-10-29.md"_s, "- ⏰ 2026-10-30\n");
        Vault vault(root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        ManualPool manual;
        IndexLoader loader(vault, index, manual.pool(2), {});
        loader.start(vault.files());
        manual.runAll();
        waitPending(loader, noteCount(vault)); // prepared with the old config
        VaultConfig config = vault.config();
        config.journalsFolder = u"Dziennik"_s;
        config.defaultReminderTime = QTime(7, 15);
        index.setConfig(config);
        loader.configChanged();
        loader.finish();
        VaultIndex fresh(config);
        {
            VaultIndex::Batch batch(fresh);
            for (const FileStat &f : vault.files()) {
                if (!paths::isIgnored(f.path))
                    fresh.update(f.path, paths::isMarkdown(f.path) ? vault.readText(f.path) : QString());
            }
        }
        compareIndexes(index, fresh, u"config"_s);
        const ParsedNote *journal = index.note(u"Dziennik/2026-10-29.md"_s);
        QVERIFY(journal && journal->journalDate);
        QCOMPARE(journal->reminders.first().at.time, std::optional<QTime>(QTime(7, 15)));
    }

    // Workers pause at maxPending prepared notes until the owner merges some.
    void backPressure()
    {
        Vault vault(m_root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        ManualPool manual;
        IndexLoader::Options options;
        options.maxPending = 4;
        IndexLoader loader(vault, index, manual.pool(3), options);
        loader.start(vault.files());
        manual.runAll();
        QElapsedTimer t;
        t.start();
        while (loader.pendingResults() < 4 && t.elapsed() < 10000)
            std::this_thread::yield();
        QTest::qWait(100);
        QCOMPARE(loader.pendingResults(), 4); // never more
        QCOMPARE(loader.stats().merged, 0);
        loader.step(1000); // merges: the workers go on
        loader.finish();
        manual.join();
        QCOMPARE(loader.stats().merged, loader.stats().total);
    }

    // The workers' reads are recorded by the vault as readText() records them: an external
    // overwrite afterwards gets a History snapshot of the text the index saw.
    void vaultBookkeepingAfterDetachedReads()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        writeFile(root, u"Notatka.md"_s, "- pierwotna treść\n");
        writeTrickyVault(root, 10);
        Vault vault(root, tmp.path() + u"/state"_s);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        QThreadPool qt;
        qt.setMaxThreadCount(3);
        {
            IndexLoader loader(vault, index, WorkerPool{2, [&qt](std::function<void()> j) { qt.start(std::move(j)); }},
                               {});
            loader.start(vault.files());
            loader.finish();
        }
        writeFile(root, u"Notatka.md"_s, "- nadpisana przez inny program\n");
        QSignalSpy spy(&vault, &Vault::changed);
        vault.rescan();
        QCOMPARE(spy.count(), 1);
        const QList<HistoryEntry> entries = vault.history()->list(u"Notatka.md"_s);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(vault.history()->read(entries.first().id), u"- pierwotna treść\n"_s);
    }

    // Journals first, newest first: the merge order (seen by step(0), one note per step).
    void journalsFirst()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/N"_s;
        writeFile(root, u"A.md"_s, "- a\n");
        writeFile(root, u"journals/2026-10-27.md"_s, "- 27\n");
        writeFile(root, u"journals/2026-10-29.md"_s, "- 29\n");
        writeFile(root, u"journals/2026-10-28.md"_s, "- 28\n");
        writeFile(root, u"B.md"_s, "- b\n");
        Vault vault(root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        IndexLoader::Options options;
        options.firstFolder = u"journals"_s;
        IndexLoader loader(vault, index, {}, options);
        loader.start(vault.files());
        QStringList order;
        for (int i = 0; i < 20 && !loader.isDone(); ++i) {
            loader.step(0); // one note per step
            for (const VaultPath &p : index.paths()) {
                if (!order.contains(p))
                    order.append(p);
            }
        }
        QCOMPARE(order, (QStringList{u"journals/2026-10-29.md"_s, u"journals/2026-10-28.md"_s,
                                     u"journals/2026-10-27.md"_s, u"A.md"_s, u"B.md"_s}));
        // Recency is the path order, whatever the merge order.
        Vault serialVault(root);
        QVERIFY(serialVault.open());
        QCOMPARE(describeRecency(index), describeRecency(*serialIndex(serialVault)));
    }

    // A search while loading finds the notes merged so far; after the full-text index built on
    // a worker is adopted, search equals a fresh index's.
    void searchDuringLoad()
    {
        Vault vault(m_root);
        QVERIFY(vault.open());
        VaultIndex index(vault.config());
        ManualPool manual;
        IndexLoader::Options options;
        options.maxPending = 100000;
        IndexLoader loader(vault, index, manual.pool(2), options);
        loader.start(vault.files());
        manual.runAll();
        waitPending(loader, noteCount(vault));
        while (index.paths().isEmpty())
            loader.step(0); // the attachments first, then one note
        const QList<VaultPath> merged = index.paths();
        QVERIFY(!merged.isEmpty());
        const QList<SearchHit> early = index.search(u"wiersz"_s);
        for (const SearchHit &h : early)
            QVERIFY(merged.contains(h.path));
        loader.finish();
        QCOMPARE(index.pendingSearchDocuments(), 0);
        Vault serialVault(m_root);
        QVERIFY(serialVault.open());
        const std::unique_ptr<VaultIndex> serial = serialIndex(serialVault);
        for (const QString &q : {u"zolw"_s, u"strona"_s, u"zadanie 12"_s, u"wiersz 1000"_s})
            QCOMPARE(describeSearch(index.search(q)), describeSearch(serial->search(q)));
    }

    void emptyVault()
    {
        QTemporaryDir tmp;
        Vault vault(tmp.path());
        QVERIFY(vault.open());
        VaultIndex index;
        IndexLoader loader(vault, index, {}, {});
        QSignalSpy finished(&loader, &IndexLoader::finished);
        loader.start(vault.files());
        QVERIFY(loader.isDone()); // at once: Workspace::ready() right after open()
        QCOMPARE(finished.count(), 1);
        QCOMPARE(loader.step(10), IndexLoader::Step::Done);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(loader.progress(), 1.0);
    }
};

QTEST_GUILESS_MAIN(TestNotebookIndexLoader)
#include "tst_notebook_indexloader.moc"
