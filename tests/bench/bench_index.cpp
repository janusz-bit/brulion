// Benchmarks of the index module on synthetic vaults (bench::makeVault: 1/3 journals, the rest
// document pages; mean ≈ 2 KB per note) of 1k / 10k / 50k notes (small profile: 1k / 10k):
// full build (in memory, and the app's open path from disk: scan + read + index), the first
// search (builds the full-text index), memory, incremental updates after one file change,
// full-text search (exact, prefix, diacritics, fuzzy, many results), page/block suggestions,
// the query language, backlinks / unlinked references and the vault-wide lists; moving and
// renaming (planRelocation, the move dialog's and the drag's notebook::relocationImpact), tags as
// pages on page open and the `#` completion (relocationVault(): 1250 / 5000 / 20000 notes).
#include "benchdata.h"

#include "index/index.h"
#include "markdown/markdown.h"
#include "notebook/pages.h"
#include "vault/vault.h"

#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

// The index loader (L1) and the index cache (L2) — the same bench sources build on trees without
// them (bench.sh compare needs equal sources): their rows are skipped there.
#if __has_include("notebook/indexloader.h")
#include "notebook/indexloader.h"
#define BENCH_HAS_LOADER 1
#endif
#if __has_include("index/indexcache.h")
#include "index/indexcache.h"
#include "notebook/indexcachekeeper.h"
#define BENCH_HAS_CACHE 1
#endif

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <vector>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

struct Fixture {
    QList<bench::VaultFile> files;
    std::unique_ptr<VaultIndex> index; ///< built and search-flushed
};

/// The note the most links point to (pageTarget() favours pages 0–9).
const QString kPopular = u"Strony/Folder 1/Strona 1.md"_s;

/// The moved page "Strona n" of relocationVault(): pages with n % 20 < 3 are in "Projekty".
bool inProjekty(int n)
{
    return n % 20 < 3;
}

/// bench::makeVault(notes) for moving, renaming and tags as pages: a tenth of the notes in the
/// folder "Projekty" (the pages "Strona n" with n % 20 < 3, in their "Folder F" subfolders —
/// 500 of 5000 notes), the pages tags name ("Projekty/Praca.md" for #praca, "Dom.md",
/// "Projekt/Kuchnia.md" for #projekt/kuchnia), in every tenth note a block with a path-qualified
/// link and a bracketed tag naming a page in "Projekty" (rewritten when the folder is renamed),
/// and in every 200th note a ```query block naming the folder, a page in it and tags. The
/// additions draw nothing from an Rng: the same vault on every machine.
QList<bench::VaultFile> relocationVault(int notes)
{
    QList<bench::VaultFile> files = bench::makeVault(notes, 77);
    int pageCount = 0;
    for (bench::VaultFile &f : files) {
        static const QRegularExpression page(u"^Strony/(Folder \\d+)/Strona (\\d+)\\.md$"_s);
        const QRegularExpressionMatch m = page.match(f.path);
        if (!m.hasMatch())
            continue;
        ++pageCount;
        if (inProjekty(m.captured(2).toInt()))
            f.path = u"Projekty/%1/Strona %2.md"_s.arg(m.captured(1), m.captured(2));
    }
    const int steps = std::max(1, pageCount / 20);
    for (qsizetype i = 0; i < files.size(); ++i) {
        QString &content = files[i].content;
        if (i % 10 == 5) {
            const int n = 20 * int((i * 7) % steps) + int(i % 3); // a page in "Projekty"
            const QString target = u"Projekty/Folder %1/Strona %2"_s.arg(n % 50).arg(n);
            content += u"- Zobacz [[%1]] i #[[%1]] oraz [[Strona %2]]\n"_s.arg(target).arg(n + 1);
        }
        if (i % 200 == 7) {
            content += u"\n```query\ntag:praca path:Projekty/ ref:[[Projekty/Folder 1/Strona 1]]\n```\n"_s
                       u"\n```query\ntag:[[Projekty/Folder 1/Strona 1]] -tag:dom\n```\n"_s;
        }
    }
    files.append({u"Projekty/Praca.md"_s, u"# Praca\n\n- strona tagu #ważne\n"_s});
    files.append({u"Dom.md"_s, u"- dom i ogród\n"_s});
    files.append({u"Projekt/Kuchnia.md"_s, u"- przepisy #dom\n"_s});
    return files;
}

/// The moves of `from` (a folder of `files`, or one file) to `to`.
QList<FileMove> movesOf(const QList<bench::VaultFile> &files, const QString &from, const QString &to)
{
    QList<FileMove> moves;
    for (const bench::VaultFile &f : files) {
        if (f.path == from)
            moves.append({f.path, to});
        else if (f.path.startsWith(from + u'/'))
            moves.append({f.path, to + f.path.mid(from.size())});
    }
    return moves;
}

} // namespace

class BenchIndex : public QObject {
    Q_OBJECT

    std::map<int, std::unique_ptr<Fixture>> m_fixtures;
    std::map<int, std::unique_ptr<QTemporaryDir>> m_disk;
    std::map<int, std::unique_ptr<Fixture>> m_relocation; ///< relocationVault(), no search index
    std::unique_ptr<QTemporaryDir> m_moveDir;             ///< relocationVault(5000) on disk
    std::unique_ptr<Vault> m_moveVault;

    /// The index of relocationVault(notes) (once per process; the full-text index not built).
    Fixture &relocation(int notes)
    {
        auto &f = m_relocation[notes];
        if (!f) {
            f = std::make_unique<Fixture>();
            f->files = relocationVault(notes);
            bench::reportInput(u"relocationVault %1 notes"_s.arg(notes), f->files);
            f->index = std::make_unique<VaultIndex>();
            VaultIndex::Batch batch(*f->index);
            for (const bench::VaultFile &file : std::as_const(f->files))
                f->index->update(file.path, file.content);
        }
        return *f;
    }

    const QList<bench::VaultFile> &files(int notes)
    {
        auto &f = m_fixtures[notes];
        if (!f) {
            f = std::make_unique<Fixture>();
            f->files = bench::makeVault(notes);
        }
        return f->files;
    }

    VaultIndex &index(int notes)
    {
        files(notes);
        auto &f = m_fixtures[notes];
        if (!f->index) {
            f->index = std::make_unique<VaultIndex>();
            VaultIndex::Batch batch(*f->index);
            for (const bench::VaultFile &file : std::as_const(f->files))
                f->index->update(file.path, file.content);
        }
        f->index->flushSearch();
        return *f->index;
    }

    /// The vault of `notes` notes written to a temporary folder (once per process).
    QString diskVault(int notes)
    {
        auto &d = m_disk[notes];
        if (!d) {
            d = bench::makeTempDir();
            if (!bench::writeVault(d->path(), files(notes)))
                qFatal("cannot write the benchmark vault");
        }
        return d->path();
    }

#ifdef BENCH_HAS_CACHE
    /// The index cache of diskVault(notes) (written once per process, not timed).
    QString cacheFile(int notes)
    {
        const QString path = diskVault(notes) + u"-index-cache.bin"_s;
        if (!QFileInfo::exists(path)) {
            Vault vault(diskVault(notes), QString());
            if (!vault.open())
                qFatal("cannot open the benchmark vault");
            VaultIndex idx(vault.config());
            notebook::IndexLoader loader(vault, idx, {}, {});
            loader.start(vault.files());
            loader.finish();
            idx.flushSearch();
            notebook::IndexCacheKeeper::Policy policy;
            policy.minNotes = 0;
            notebook::IndexCacheKeeper keeper(vault, idx, {}, path, policy);
            if (!keeper.saveNow())
                qFatal("cannot write the index cache");
            qInfo("[bench] index cache %d notes: %lld bytes (vault %lld bytes)", notes,
                  qlonglong(QFileInfo(path).size()), qlonglong(bench::totalBytes(files(notes))));
        }
        return path;
    }
#endif

    static void addSizeRows(std::initializer_list<int> full, std::initializer_list<int> small)
    {
        QTest::addColumn<int>("notes");
        for (int n : bench::sizes(full, small))
            QTest::newRow(qPrintable(u"%1 notes"_s.arg(n))) << n;
    }

private Q_SLOTS:
    void initTestCase()
    {
        // In-line sizes of the parsed model.
        qInfo("[bench] sizeof: ParsedNote %zu, Block %zu, Task %zu, Reminder %zu, Link %zu, DateSpec %zu, QString %zu",
              sizeof(ParsedNote), sizeof(Block), sizeof(Task), sizeof(Reminder), sizeof(Link), sizeof(DateSpec),
              sizeof(QString));
    }

    void cleanupTestCase()
    {
        // Report the generated vault sizes (for the README tables).
        for (const auto &[notes, f] : m_fixtures)
            qInfo("[bench] vault %d notes: %lld bytes, sha256 %s", notes, qlonglong(bench::totalBytes(f->files)),
                  bench::digest(f->files).constData());
        m_moveVault.reset();
        m_moveDir.reset();
    }

    // ---- building ---------------------------------------------------------------------------

    void buildInMemory_data() { addSizeRows({1000, 10000, 50000}, {1000, 10000}); }
    void buildInMemory()
    {
        QFETCH(int, notes);
        const QList<bench::VaultFile> &all = files(notes);
        auto idx = std::make_unique<VaultIndex>();
        bench::timeOnce([] {},
                        [&] {
                            VaultIndex::Batch batch(*idx);
                            for (const bench::VaultFile &f : all)
                                idx->update(f.path, f.content);
                        });
        QCOMPARE(idx->paths().size(), qsizetype(notes));
    }

    // The first search after opening a vault: builds every full-text document.
    void firstSearch_data() { addSizeRows({1000, 10000, 50000}, {1000, 10000}); }
    void firstSearch()
    {
        QFETCH(int, notes);
        const QList<bench::VaultFile> &all = files(notes);
        VaultIndex idx;
        bench::timeOnce(
                [&] {
                    VaultIndex::Batch batch(idx);
                    for (const bench::VaultFile &f : all)
                        idx.update(f.path, f.content);
                },
                [&] { idx.flushSearch(); });
        QCOMPARE(idx.pendingSearchDocuments(), 0);
    }

    // Heap memory held by an index (contents are shared with the generated list, so this is the
    // parsed model + lookup maps + full-text index; the app also keeps every note's content —
    // about 2 bytes per character of the vault). glibc malloc statistics, not RSS.
    // "ParsedNote only": the same notes parsed with markdown::parseNote() and kept in a list
    // (no index maps, no search) — how much of the index is the parsed model itself.
    void memory_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<int>("what");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            QTest::newRow(qPrintable(u"%1 notes ParsedNote only"_s.arg(n))) << n << 0;
            QTest::newRow(qPrintable(u"%1 notes parsed"_s.arg(n))) << n << 1;
            QTest::newRow(qPrintable(u"%1 notes + search index"_s.arg(n))) << n << 2;
        }
        // From the disk with the loader (vault, texts, index and full-text index): parsed, and
        // taken from the index cache — the same heap within a few percent.
        QTest::newRow("10000 notes loaded from disk") << 10000 << 3;
        QTest::newRow("10000 notes loaded from the index cache") << 10000 << 4;
    }
    void memory()
    {
        QFETCH(int, notes);
        QFETCH(int, what);
        const QList<bench::VaultFile> &all = files(notes);
        if (bench::heapBytes() < 0)
            QSKIP("no malloc statistics on this platform");
        if (what >= 3) {
#ifdef BENCH_HAS_CACHE
            const QString root = diskVault(notes);
            const QString cache = what == 4 ? cacheFile(notes) : QString();
            const qint64 start = bench::heapBytes();
            {
                Vault vault(root, QString());
                QVERIFY(vault.open());
                VaultIndex idx(vault.config());
                notebook::IndexLoader::Options options;
                options.cachePath = cache;
                notebook::IndexLoader loader(vault, idx, {}, options);
                loader.start(vault.files());
                loader.finish();
                idx.flushSearch();
                QCOMPARE(loader.stats().fromCache, what == 4 ? qsizetype(notes) : 0);
                QTest::setBenchmarkResult(qreal(bench::heapBytes() - start), QTest::BytesAllocated);
            }
            return;
#else
            QSKIP("no index cache in this tree");
#endif
        }
        const qint64 before = bench::heapBytes();
        if (what == 0) {
            const VaultConfig config;
            QList<ParsedNote> parsed;
            parsed.reserve(all.size());
            qsizetype blocks = 0;
            for (const bench::VaultFile &f : all) {
                parsed.append(markdown::parseNote(f.path, f.content, config));
                blocks += parsed.last().blocks.size();
            }
            const qint64 bytes = bench::heapBytes() - before;
            qInfo("[bench] %d notes: %lld blocks, %lld bytes of ParsedNote heap (%lld per note, %lld per block)", notes,
                  qlonglong(blocks), qlonglong(bytes), qlonglong(bytes / notes),
                  qlonglong(bytes / qMax<qsizetype>(1, blocks)));
            // What the model holds (docs/PERFORMANCE.md "Wave 5", L8): the same on every tree.
            qsizetype tasks = 0, withProperties = 0, withId = 0, links = 0, refs = 0, rawChars = 0, textChars = 0;
            QSet<QString> tags;
            for (const ParsedNote &n : std::as_const(parsed)) {
                for (const Block &b : n.blocks) {
                    tasks += b.task ? 1 : 0;
                    withProperties += b.properties.isEmpty() ? 0 : 1;
                    withId += b.id.isEmpty() ? 0 : 1;
                    rawChars += QStringView(b.raw).size();
                    textChars += b.text.size();
                }
                for (const Link &l : n.links) {
                    ++links;
                    refs += l.kind == LinkKind::BlockRef || l.kind == LinkKind::BlockEmbed ? 1 : 0;
                }
                for (const QString &t : n.tags)
                    tags.insert(t);
            }
            qInfo("[bench] %d notes model: %lld blocks, %lld tasks, %lld with properties, %lld with an id, %lld links "
                  "(%lld block refs), %lld distinct tags, %lld raw + %lld text characters",
                  notes, qlonglong(blocks), qlonglong(tasks), qlonglong(withProperties), qlonglong(withId),
                  qlonglong(links), qlonglong(refs), qlonglong(tags.size()), qlonglong(rawChars), qlonglong(textChars));
            QTest::setBenchmarkResult(qreal(bytes), QTest::BytesAllocated);
            return;
        }
        {
            VaultIndex idx;
            {
                VaultIndex::Batch batch(idx);
                for (const bench::VaultFile &f : all)
                    idx.update(f.path, f.content);
            }
            if (what == 2)
                idx.flushSearch();
            QTest::setBenchmarkResult(qreal(bench::heapBytes() - before), QTest::BytesAllocated);
        }
    }

    // The app's open path: Vault::open() (scan), readText() of every note, VaultIndex::update();
    // and with the index loader (notebook::IndexLoader, L1): reading, parsing and the full-text
    // index on worker threads (QThreadPool, 1 or 4 parse workers + 1 for the search), merged on
    // this thread — "loader inline" is the single-threaded loader (WebAssembly). Timed until
    // finish() returns: every note merged and the full-text index adopted.
    void openFromDisk_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<int>("phase");
        const int workers = bench::smallProfile() ? 3 : 4;
        for (int n : bench::sizes({1000, 10000, 50000}, {1000, 10000})) {
            QTest::newRow(qPrintable(u"%1 notes scan"_s.arg(n))) << n << 0;
            QTest::newRow(qPrintable(u"%1 notes scan+read"_s.arg(n))) << n << 1;
            QTest::newRow(qPrintable(u"%1 notes scan+read+index"_s.arg(n))) << n << 2;
            QTest::newRow(qPrintable(u"%1 notes loader inline"_s.arg(n))) << n << 3;
            QTest::newRow(qPrintable(u"%1 notes loader 1 worker"_s.arg(n))) << n << 4;
            QTest::newRow(qPrintable(u"%1 notes loader %2 workers"_s.arg(n).arg(workers))) << n << 4 + workers;
            QTest::newRow(qPrintable(u"%1 notes loader warm cache inline"_s.arg(n))) << n << 20;
            QTest::newRow(qPrintable(u"%1 notes loader warm cache %2 workers"_s.arg(n).arg(workers))) << n << 20 + workers;
        }
    }
    void openFromDisk()
    {
        QFETCH(int, notes);
        QFETCH(int, phase);
        const QString root = diskVault(notes);
        if (phase >= 3) {
#ifdef BENCH_HAS_LOADER
            const bool warm = phase >= 20;
#ifdef BENCH_HAS_CACHE
            const QString cache = warm ? cacheFile(notes) : QString();
#else
            const QString cache;
            if (warm)
                QSKIP("no index cache in this tree");
#endif
            const int threads = warm ? phase - 20 : (phase == 3 ? 0 : (phase == 4 ? 1 : phase - 4)); // N workers
            QThreadPool pool;
            pool.setMaxThreadCount(threads + 1);
            notebook::WorkerPool workers;
            if (threads > 0)
                workers = {threads, [&pool](std::function<void()> job) { pool.start(std::move(job)); }};
            QElapsedTimer t;
            t.start();
            Vault vault(root, QString());
            QVERIFY(vault.open());
            VaultIndex idx;
            notebook::IndexLoader::Options options;
            options.firstFolder = vault.config().journalsFolder;
            options.cachePath = cache;
            notebook::IndexLoader loader(vault, idx, workers, options);
            loader.start(vault.files());
            loader.finish();
            bench::reportMs(t);
            QCOMPARE(idx.paths().size(), qsizetype(notes));
            QCOMPARE(loader.stats().fromCache, warm ? qsizetype(notes) : 0);
            QCOMPARE(idx.pendingSearchDocuments(), threads > 0 || warm ? 0 : notes);
            return;
#else
            QSKIP("no index loader in this tree");
#endif
        }
        QElapsedTimer t;
        t.start();
        Vault vault(root, QString());
        QVERIFY(vault.open());
        qsizetype count = 0;
        if (phase >= 1) {
            VaultIndex idx;
            VaultIndex::Batch batch(idx);
            for (const FileStat &f : vault.markdownFiles()) {
                bool ok = false;
                const QString content = vault.readText(f.path, &ok);
                if (phase >= 2 && ok)
                    idx.update(f.path, content);
                count += ok;
            }
            bench::reportMs(t); // before the index is destroyed
        } else {
            count = vault.markdownFiles().size();
            bench::reportMs(t);
        }
        QCOMPARE(count, qsizetype(notes));
    }

    // VaultIndex::prepare() of every note on one thread (what the loader's workers share out):
    // the loader's parallel efficiency is prepare / (loader N workers x N).
    void prepare_data() { addSizeRows({1000, 10000}, {1000, 10000}); }
    void prepare()
    {
        QFETCH(int, notes);
#ifdef BENCH_HAS_LOADER
        const QList<bench::VaultFile> &all = files(notes);
        const VaultConfig config;
        std::vector<PreparedNote> prepared; // move-only
        prepared.reserve(size_t(all.size()));
        bench::timeOnce([] {},
                        [&] {
                            for (const bench::VaultFile &f : all)
                                prepared.push_back(VaultIndex::prepare(f.path, f.content, config));
                        });
        QCOMPARE(qsizetype(prepared.size()), qsizetype(notes));
#else
        Q_UNUSED(notes);
        QSKIP("no VaultIndex::prepare() in this tree");
#endif
    }

    // The index cache: encoding a whole index with its full-text index (the keeper's work on
    // the GUI thread, in slices there), and decoding every note and the full-text index on one
    // thread (what a warm load shares out to the workers).
    void cacheEncode_data() { addSizeRows({1000, 10000, 50000}, {1000, 10000}); }
    void cacheEncode()
    {
        QFETCH(int, notes);
#ifdef BENCH_HAS_CACHE
        VaultIndex &idx = index(notes);
        const auto identity = indexcache::Identity::current(idx.config(), u"bench"_s);
        const auto stat = [](const VaultPath &p) -> std::optional<FileStat> { return FileStat{p, false, 1, 1}; };
        QByteArray out;
        bench::timeOnce([] {}, [&] { out = indexcache::encode(idx, identity, stat); });
        QVERIFY(out.size() > notes * 100);
        qInfo("[bench] index cache %d notes: %lld bytes", notes, qlonglong(out.size()));
#else
        Q_UNUSED(notes);
        QSKIP("no index cache in this tree");
#endif
    }

    void cacheDecode_data() { addSizeRows({1000, 10000, 50000}, {1000, 10000}); }
    void cacheDecode()
    {
        QFETCH(int, notes);
#ifdef BENCH_HAS_CACHE
        VaultIndex &idx = index(notes);
        const auto identity = indexcache::Identity::current(idx.config(), u"bench"_s);
        QHash<VaultPath, qint64> sizes;
        for (const bench::VaultFile &f : files(notes))
            sizes.insert(f.path, f.content.toUtf8().size());
        const QByteArray bytes = indexcache::encode(idx, identity, [&](const VaultPath &p) -> std::optional<FileStat> {
            return FileStat{p, false, sizes.value(p), 1};
        });
        qsizetype decoded = 0;
        bench::timeOnce([] {},
                        [&] {
                            const auto reader = indexcache::Reader::open(bytes, identity);
                            for (const bench::VaultFile &f : files(notes)) {
                                if (const indexcache::Record *r = reader->find(f.path))
                                    decoded += reader->decodeNote(*r, f.path, f.content, idx.config()) ? 1 : 0;
                            }
                            indexcache::Reader::SearchDecoder search = reader->searchDecoder();
                            search.step(std::numeric_limits<int>::max());
                            QVERIFY(!search.failed());
                        });
        QCOMPARE(decoded, qsizetype(notes));
#else
        Q_UNUSED(notes);
        QSKIP("no index cache in this tree");
#endif
    }

    // ---- incremental updates ------------------------------------------------------------------

    void incrementalUpdate_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("change");
        QTest::addColumn<bool>("flush");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            for (const QString &c :
                 {u"append to journal"_s, u"edit popular page"_s, u"edit 20 KB page"_s, u"rename popular page"_s}) {
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(c))) << n << c << false;
                QTest::newRow(qPrintable(u"%1 notes %2 + search"_s.arg(n).arg(c))) << n << c << true;
            }
        }
    }
    void incrementalUpdate()
    {
        QFETCH(int, notes);
        QFETCH(QString, change);
        QFETCH(bool, flush);
        VaultIndex &idx = index(notes);
        const QList<bench::VaultFile> &all = files(notes);
        // Targets: the first journal, the popular page, the largest page.
        QString path = all.first().path;
        if (change.contains(u"popular"_s))
            path = kPopular;
        if (change.contains(u"20 KB"_s)) {
            qsizetype best = 0;
            for (const bench::VaultFile &f : all) {
                if (!f.path.startsWith(u"journals/"_s) && f.content.size() > best) {
                    best = f.content.size();
                    path = f.path;
                }
            }
        }
        const QString original = idx.content(path);
        QVERIFY(!original.isNull());
        const QString edited = original + u"\n- nowy wpis o [[Strona 3]] #praca ⏰ 2026-10-20 09:00\n"_s;
        const QString renamed = u"Strony/Folder 1/Strona pierwsza.md"_s;
        int flip = 0;
        QBENCHMARK {
            if (change.startsWith(u"rename"_s)) {
                if (flip++ & 1)
                    idx.rename(renamed, path, original);
                else
                    idx.rename(path, renamed, original);
            } else {
                idx.update(path, (flip++ & 1) ? original : edited);
            }
            if (flush)
                idx.flushSearch();
        }
        // Leave the fixture as it was.
        if (change.startsWith(u"rename"_s)) {
            if (idx.contains(renamed))
                idx.rename(renamed, path, original);
        } else {
            idx.update(path, original);
        }
        QVERIFY(idx.contains(path));
    }

    // One large note (re)indexed for search in a vault of 1000 notes: what saving it costs once
    // the background search update runs (search documents are folded with text::foldKey()).
    void searchLargeNote_data()
    {
        QTest::addColumn<int>("kb");
        for (int kb : bench::sizes({50, 200, 1000}, {50, 200}))
            QTest::newRow(qPrintable(u"%1 KB note"_s.arg(kb))) << kb;
    }
    void searchLargeNote()
    {
        QFETCH(int, kb);
        VaultIndex &idx = index(1000);
        bench::Rng r{quint64(kb)};
        const QString big = bench::documentNote(r, 99999, 500, qsizetype(kb) * 1024);
        bench::reportInput(u"searchLargeNote %1 KB"_s.arg(kb), big);
        const QString path = u"Strony/Duża notatka.md"_s;
        int flip = 0;
        QBENCHMARK {
            idx.update(path, (flip++ & 1) ? big : big + u"\nkoniec\n"_s);
            idx.flushSearch();
        }
        idx.remove(path);
    }

    // ---- search and suggestions -------------------------------------------------------------

    void search_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("query");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            const QList<std::pair<QString, QString>> queries = {
                    {u"exact"_s, u"spotkanie"_s},
                    {u"prefix"_s, u"spot"_s},
                    {u"folded"_s, u"zolw"_s},
                    {u"diacritics"_s, u"żółw"_s},
                    {u"fuzzy"_s, u"spotkanei"_s},
                    {u"two words"_s, u"budżet faktura"_s},
                    {u"common word"_s, u"się"_s},
                    {u"one letter"_s, u"s"_s},
                    {u"no hits"_s, u"xyzzyqwerty"_s},
                    {u"title"_s, u"Strona 1234"_s},
                    {u"five words"_s, u"kupić farbę do ogrodu jutro"_s},
            };
            for (const auto &[name, q] : queries)
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(name))) << n << q;
        }
    }
    void search()
    {
        QFETCH(int, notes);
        QFETCH(QString, query);
        const VaultIndex &idx = index(notes);
        QBENCHMARK {
            const QList<SearchHit> hits = idx.search(query);
            Q_UNUSED(hits);
        }
    }

    void suggestPages_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("prefix");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            QTest::newRow(qPrintable(u"%1 notes 'stro'"_s.arg(n))) << n << u"stro"_s;
            QTest::newRow(qPrintable(u"%1 notes empty"_s.arg(n))) << n << QString();
            QTest::newRow(qPrintable(u"%1 notes 'Strona 12'"_s.arg(n))) << n << u"Strona 12"_s;
            QTest::newRow(qPrintable(u"%1 notes fuzzy 'sotrna'"_s.arg(n))) << n << u"sotrna"_s;
        }
    }
    void suggestPages()
    {
        QFETCH(int, notes);
        QFETCH(QString, prefix);
        const VaultIndex &idx = index(notes);
        QBENCHMARK {
            const auto s = idx.suggestPages(prefix, 20);
            Q_UNUSED(s);
        }
    }

    void suggestBlocks_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("text");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            QTest::newRow(qPrintable(u"%1 notes 'kawa ogród'"_s.arg(n))) << n << u"kawa ogród"_s;
            QTest::newRow(qPrintable(u"%1 notes empty"_s.arg(n))) << n << QString();
        }
    }
    void suggestBlocks()
    {
        QFETCH(int, notes);
        QFETCH(QString, text);
        const VaultIndex &idx = index(notes);
        QBENCHMARK {
            const auto s = idx.suggestBlocks(text, 20);
            Q_UNUSED(s);
        }
    }

    // ---- query language -------------------------------------------------------------------------

    void query_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("source");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            const QList<std::pair<QString, QString>> queries = {
                    {u"open due this week"_s, u"status:open due:<=+7d sort:due"_s},
                    {u"overdue"_s, u"status:open due:overdue sort:due limit:20"_s},
                    {u"tag praca"_s, u"tag:praca"_s},
                    {u"tag + -tag"_s, u"type:tasks tag:zakupy -tag:dom"_s},
                    {u"all tasks sorted"_s, u"type:tasks sort:due,-priority,page"_s},
                    {u"text group page"_s, u"type:blocks \"łódź\" group:page"_s},
                    {u"reminders week"_s, u"remind:this-week"_s},
                    {u"ref popular"_s, u"type:blocks ref:[[Strona 1]]"_s},
            };
            for (const auto &[name, q] : queries)
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(name))) << n << q;
        }
    }
    void query()
    {
        QFETCH(int, notes);
        QFETCH(QString, source);
        const VaultIndex &idx = index(notes);
        const QDate today = bench::fixedToday();
        QVERIFY2(std::holds_alternative<Query>(parseQuery(source)), qPrintable(source));
        QBENCHMARK {
            const auto parsed = parseQuery(source);
            const QueryResult r = runQuery(std::get<Query>(parsed), idx, today);
            Q_UNUSED(r);
        }
    }

    // ---- links ----------------------------------------------------------------------------------

    void links_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("what");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            for (const QString &w : {u"backlinks popular"_s, u"backlinks journal"_s, u"linksTo popular"_s,
                                     u"unlinked popular"_s, u"outgoing 20 KB page"_s, u"resolveLink"_s})
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void links()
    {
        QFETCH(int, notes);
        QFETCH(QString, what);
        const VaultIndex &idx = index(notes);
        const QString journal = files(notes).first().path;
        QString big = kPopular;
        qsizetype best = 0;
        for (const bench::VaultFile &f : files(notes)) {
            if (f.content.size() > best) {
                best = f.content.size();
                big = f.path;
            }
        }
        qsizetype sink = 0;
        QBENCHMARK {
            if (what == u"backlinks popular")
                sink += idx.backlinks(kPopular).size();
            else if (what == u"backlinks journal")
                sink += idx.backlinks(journal).size();
            else if (what == u"linksTo popular")
                sink += idx.linksTo(kPopular).size();
            else if (what == u"unlinked popular")
                sink += idx.unlinkedReferences(kPopular).size();
            else if (what == u"outgoing 20 KB page")
                sink += idx.outgoingLinks(big).size();
            else
                sink += idx.resolveLink(u"strona 4321"_s, journal).has_value();
        }
        QVERIFY(sink >= 0);
    }

    // unlinkedReferences() of "Strona 1" when another note has one long line with n mentions of
    // "Strona 1x" — prefixes of the title that are not whole words, so the matcher retries at the
    // next offset each time (QRegularExpression::match(line, offset) re-validates the line).
    void unlinkedLongLine_data()
    {
        QTest::addColumn<int>("mentions");
        for (int n : bench::sizes({1000, 5000, 20000}, {1000, 5000}))
            QTest::newRow(qPrintable(u"%1 partial mentions on one line"_s.arg(n))) << n;
    }
    void unlinkedLongLine()
    {
        QFETCH(int, mentions);
        VaultIndex idx;
        idx.update(u"Strona 1.md"_s, u"# Strona 1\n"_s);
        QString line;
        for (int i = 0; i < mentions; ++i)
            line += u"Strona 1%1 "_s.arg(i % 10);
        idx.update(u"Długa notatka.md"_s, line + u"i na końcu Strona 1.\n"_s);
        QBENCHMARK {
            const auto refs = idx.unlinkedReferences(u"Strona 1.md"_s);
            QCOMPARE(refs.size(), qsizetype(1));
        }
    }

    // ---- moving and renaming ---------------------------------------------------------------------

    // VaultIndex::planRelocation() of the folder "Projekty" of relocationVault() — a tenth of the
    // notes — renamed in place ("Archiwum": its path-qualified links, bracketed tags and query
    // filters are rewritten) or moved into another folder ("Archiwum/Projekty": every link
    // still resolves; the plan checks them all), and of the most linked page renamed.
    void planRelocation_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("what");
        for (int n : bench::sizes({1250, 5000, 20000}, {1250, 5000})) {
            for (const QString &w : {u"folder rename"_s, u"folder move"_s, u"page rename"_s})
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void planRelocation()
    {
        QFETCH(int, notes);
        QFETCH(QString, what);
        Fixture &f = relocation(notes);
        QList<FileMove> moves;
        if (what == u"folder rename")
            moves = movesOf(f.files, u"Projekty"_s, u"Archiwum"_s);
        else if (what == u"folder move")
            moves = movesOf(f.files, u"Projekty"_s, u"Archiwum/Projekty"_s);
        else
            moves = {{u"Projekty/Folder 1/Strona 1.md"_s, u"Projekty/Folder 1/Strona pierwsza.md"_s}};
        RelocationPlan plan;
        QBENCHMARK {
            plan = f.index->planRelocation(moves);
        }
        qInfo("[bench] planRelocation %d notes %s: %lld moves, %lld notes rewritten, %d links, %d tags, %d queries",
              notes, qPrintable(what), qlonglong(plan.moves().size()), qlonglong(plan.files().size()), plan.links(),
              plan.tags(), plan.queries());
        QVERIFY(!plan.moves().isEmpty());
        QVERIFY(!plan.files().isEmpty());
    }

    // notebook::relocationImpact() — what the move dialog and the dragged row's ghost ask for each
    // target (Workspace::moveImpact): the checks against the vault on disk, the file moves of the
    // folder and the plan. relocationVault(5000) opened from disk.
    void moveImpact_data()
    {
        QTest::addColumn<QString>("from");
        QTest::addColumn<QString>("to");
        QTest::newRow("5000 notes folder rename") << u"Projekty"_s << u"Archiwum"_s;
        QTest::newRow("5000 notes folder move") << u"Projekty"_s << u"Archiwum/Projekty"_s;
        QTest::newRow("5000 notes page move") << u"Projekty/Folder 1/Strona 1.md"_s << u"Archiwum/Strona 1.md"_s;
    }
    void moveImpact()
    {
        QFETCH(QString, from);
        QFETCH(QString, to);
        Fixture &f = relocation(5000);
        if (!m_moveVault) {
            m_moveDir = bench::makeTempDir();
            if (!bench::writeVault(m_moveDir->path(), f.files))
                qFatal("cannot write the benchmark vault");
            m_moveVault = std::make_unique<Vault>(m_moveDir->path(), QString());
            if (!m_moveVault->open())
                qFatal("cannot open the benchmark vault");
        }
        notebook::RelocationImpact impact;
        QBENCHMARK {
            impact = notebook::relocationImpact(*m_moveVault, f.index.get(), from, to);
        }
        QVERIFY(impact.ok);
        QVERIFY(impact.notes > 0);
    }

    // The same plans a few notes per event-loop turn (RelocationPlanner, Workspace's
    // requestMoveImpact() for the dragged row's ghost: 8 ms per turn): what the GUI thread is
    // blocked for at most — the preparation (lookup tables, the notes to look at), then the
    // longest step; and the number of turns.
    void relocationSteps_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("what");
        for (int n : bench::sizes({1250, 5000, 20000}, {1250, 5000})) {
            for (const QString &w : {u"folder move prepare"_s, u"folder move longest step"_s, u"folder move turns"_s,
                                     u"folder rename prepare"_s, u"folder rename longest step"_s})
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void relocationSteps()
    {
        QFETCH(int, notes);
        QFETCH(QString, what);
        Fixture &f = relocation(notes);
        const QList<FileMove> moves = what.contains(u"rename"_s)
                                              ? movesOf(f.files, u"Projekty"_s, u"Archiwum"_s)
                                              : movesOf(f.files, u"Projekty"_s, u"Archiwum/Projekty"_s);
        if (what.endsWith(u"prepare"_s)) {
            QBENCHMARK {
                RelocationPlanner planner(*f.index, moves);
                QVERIFY(!planner.done());
            }
            return;
        }
        // One planner per repetition: the longest step (or the number of turns) of the median run.
        QList<qreal> results;
        for (int run = 0; run < 7; ++run) {
            RelocationPlanner planner(*f.index, moves);
            qint64 longest = 0;
            int turns = 0;
            bool done = false;
            while (!done) {
                QElapsedTimer t;
                t.start();
                done = planner.step(8);
                longest = std::max(longest, t.nsecsElapsed());
                ++turns;
            }
            QVERIFY(!planner.plan().files().isEmpty());
            results.append(what.endsWith(u"turns"_s) ? qreal(turns) : qreal(longest) / 1e6);
        }
        std::sort(results.begin(), results.end());
        QTest::setBenchmarkResult(results.at(results.size() / 2),
                                  what.endsWith(u"turns"_s) ? QTest::Events : QTest::WalltimeMilliseconds);
    }

    // ---- tags as pages ----------------------------------------------------------------------------

    // Opening the page a tag names (PageTools::pageInfo, the references list): the tag list, the
    // tags naming the page, the blocks tagged with it, its backlinks — with the caches built
    // ("warm"), and right after a save elsewhere, which drops them ("after a save"; "save" alone
    // for comparison). relocationVault(): #praca names "Projekty/Praca.md".
    void tagPageOpen_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("what");
        for (int n : bench::sizes({1250, 5000, 20000}, {1250, 5000})) {
            for (const QString &w : {u"warm"_s, u"after a save"_s, u"save"_s})
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void tagPageOpen()
    {
        QFETCH(int, notes);
        QFETCH(QString, what);
        Fixture &f = relocation(notes);
        VaultIndex &idx = *f.index;
        const QString page = u"Projekty/Praca.md"_s;
        const QString journal = f.files.first().path;
        const QString original = idx.content(journal);
        const QString edited = original + u"- nowy wpis #praca\n"_s;
        int flip = 0;
        qsizetype references = 0;
        QBENCHMARK {
            if (what != u"warm")
                idx.update(journal, (flip++ & 1) ? original : edited);
            if (what != u"save") {
                qsizetype n = idx.tags().size() + idx.tagsNaming(page).size();
                references = idx.tagReferences(page).size();
                n += idx.backlinks(page).size();
                QVERIFY(n > 0);
            }
        }
        idx.update(journal, original);
        if (what != u"save")
            QVERIFY(references > 0);
    }

    // `#` completion (VaultIndex::suggestTags: tags in use, then the pages a tag would name):
    // a tag's prefix, nothing typed yet, many pages and no tag ('stro'), a tag that only contains
    // the text ('kuch').
    void suggestTags_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("prefix");
        for (int n : bench::sizes({1250, 5000, 20000}, {1250, 5000})) {
            QTest::newRow(qPrintable(u"%1 notes 'pra'"_s.arg(n))) << n << u"pra"_s;
            QTest::newRow(qPrintable(u"%1 notes empty"_s.arg(n))) << n << QString();
            QTest::newRow(qPrintable(u"%1 notes 'stro'"_s.arg(n))) << n << u"stro"_s;
            QTest::newRow(qPrintable(u"%1 notes 'kuch'"_s.arg(n))) << n << u"kuch"_s;
        }
    }
    void suggestTags()
    {
        QFETCH(int, notes);
        QFETCH(QString, prefix);
        const VaultIndex &idx = *relocation(notes).index;
        qsizetype count = 0;
        QBENCHMARK {
            count = idx.suggestTags(prefix, 20).size();
        }
        QVERIFY(count > 0);
    }

    // ---- vault-wide lists -----------------------------------------------------------------------

    void lists_data()
    {
        QTest::addColumn<int>("notes");
        QTest::addColumn<QString>("what");
        for (int n : bench::sizes({10000, 50000}, {10000})) {
            for (const QString &w :
                 {u"tasks"_s, u"tags"_s, u"reminders"_s, u"journals"_s, u"missingPages"_s, u"paths"_s, u"graph"_s})
                QTest::newRow(qPrintable(u"%1 notes %2"_s.arg(n).arg(w))) << n << w;
        }
    }
    void lists()
    {
        QFETCH(int, notes);
        QFETCH(QString, what);
        const VaultIndex &idx = index(notes);
        qsizetype sink = 0;
        QBENCHMARK {
            if (what == u"tasks")
                sink += idx.tasks().size();
            else if (what == u"tags")
                sink += idx.tags().size();
            else if (what == u"reminders")
                sink += idx.reminders().size();
            else if (what == u"journals")
                sink += idx.journals().size();
            else if (what == u"missingPages")
                sink += idx.missingPages().size();
            else if (what == u"paths")
                sink += idx.paths().size();
            else
                sink += idx.graph(true, true).edges.size();
        }
        QVERIFY(sink >= 0);
    }
};

QTEST_GUILESS_MAIN(BenchIndex)
#include "bench_index.moc"
