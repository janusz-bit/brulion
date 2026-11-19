// Storage whose file times cannot tell two writes apart (FAT's 2 s, exFAT's 10 ms, 1 s of network
// and FUSE mounts, Android's document providers), and writes racing the vault's own:
//  * the compare-and-swap of a save (vaultutil::ReadMark, stillAsRead()): another program's write
//    while the save writes and syncs its data — keeping the size and the time, to the nanosecond —
//    is never written over (async on a writer thread, inline, and synchronous), nor a file created
//    meanwhile; when the stamps cannot vouch (coarse, or the file written just before the read:
//    stampsVouch()) the bytes are read again;
//  * the stat of a file written within the last tick cannot vouch for its content (noteUnsettled()):
//    another program's write of the same size in the same tick is found by the next listing — after
//    an own write, after another program's write, after a read — until the tick has passed;
//  * a rename refused for a moment (FileReplace's Busy: Windows' virus scanners and indexers) is
//    tried again for 0.3 s, the compare-and-swap before every attempt; a final failure
//    leaves no temporary file. A failing sync says why;
//  * folders whose clock stamps changes with its tick (NTFS, Linux before 6.13, Android): the probe
//    of a folder's clock (vaultutil::probeFolderClock()) finds them coarse, and another program's
//    change in the tick of an own save's temporary file is reported — the echo is not trusted.
// The coarse times are simulated: a file sync that cuts the temporary file's time to an even second
// (as FAT keeps it), files of other programs written with whole-second times, rewrites that
// put the file's exact time back (std::filesystem: nanoseconds), and a folder clock that ticks only
// when told (vaultutil::setStampFilter()).
#include "vaulttest.h"

#include "vault/filesync.h"
#include "vault/vaultutil_p.h"

#include <QElapsedTimer>
#include <QScopeGuard>
#include <QThread>
#include <QThreadPool>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <mutex>

using namespace vaulttest;

namespace {

std::filesystem::path fsPath(const QString &abs)
{
    return std::filesystem::path(abs.toStdU16String());
}

/// FAT's file times: the temporary file's time cut to an even second once its data is written.
bool coarseSync(QFileDevice &file, QString *)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    return file.setFileTime(QDateTime::fromMSecsSinceEpoch(now / 2000 * 2000), QFileDevice::FileModificationTime);
}

/// The vault's own writes on coarse storage while it lives.
struct CoarseStorage {
    brulion::vaultutil::FileSync previous = brulion::vaultutil::fileSync();
    CoarseStorage() { brulion::vaultutil::setFileSync(&coarseSync); }
    ~CoarseStorage() { brulion::vaultutil::setFileSync(previous); }
};

/// Another program writes `bytes` over the file and puts its exact time back: on storage whose
/// time keeps ticks (or with a write within the tick) the stat stays as it was. (No QVERIFY: also
/// on the writer thread.)
[[nodiscard]] bool rewriteKeepingStamp(const QString &abs, const QByteArray &bytes)
{
    std::error_code code;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(fsPath(abs), code);
    if (code)
        return false;
    QFile file(abs);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size())
        return false;
    file.close();
    std::filesystem::last_write_time(fsPath(abs), time, code);
    return !code;
}

/// A file of another program with a whole-second time (coarse) `secondsAgo` before now.
void writeCoarse(const QString &abs, const QByteArray &bytes, qint64 secondsAgo = 0)
{
    QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
    QFile file(abs);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(file.write(bytes), bytes.size());
    QVERIFY(file.flush());
    const qint64 second = QDateTime::currentMSecsSinceEpoch() / 1000 * 1000 - secondsAgo * 1000;
    QVERIFY(file.setFileTime(QDateTime::fromMSecsSinceEpoch(second), QFileDevice::FileModificationTime));
}

bool hasTemp(const QString &root)
{
    return diskFiles(root).join(u'\n').contains(u".brulion-tmp-"_s);
}

/// The writer of a test: one thread, as the app's, or none (inline, WebAssembly).
struct Writer {
    QThreadPool pool;
    Writer() { pool.setMaxThreadCount(1); }
    ~Writer() { pool.waitForDone(); }
    void attach(Vault &vault, bool threaded)
    {
        if (threaded)
            vault.setWriteExecutor([this](std::function<void()> job) { pool.start(std::move(job)); });
        else
            vault.setWriteExecutor({});
    }
};

/// "async thread", "async inline" or "sync": a save of `next` over `expected`.
WriteOutcome save(Vault &vault, const QString &mode, const QString &path, const QString &next, const QString &expected)
{
    if (mode == "sync"_L1)
        return vault.writeTextIfUnchanged(path, next, expected);
    auto outcome = std::make_shared<std::optional<WriteOutcome>>();
    vault.writeTextIfUnchangedAsync(AsyncTextWrite{.path = path,
                                                   .content = next,
                                                   .expected = expected,
                                                   .source = ChangeSource::User,
                                                   .done = [outcome](WriteOutcome o) { *outcome = o; }});
    vault.waitForWrites();
    return outcome->value_or(WriteOutcome::Failed);
}

// --- a rename of the test (vaultutil::FileReplace)
int g_replaceCalls = 0;
int g_replaceRefusals = 0;
brulion::vaultutil::Replaced g_refusal = brulion::vaultutil::Replaced::Busy;
std::function<void()> g_whileRefused;

brulion::vaultutil::Replaced testReplace(const QString &from, const QString &to, QString *error)
{
    // (Only the note's renames count and are refused: History's go through.)
    if (to.endsWith(u"/Plan.md"_s) && ++g_replaceCalls <= g_replaceRefusals) {
        if (g_whileRefused)
            g_whileRefused();
        *error = u"zajęty przez skaner (test)"_s;
        return g_refusal;
    }
    std::error_code code;
    std::filesystem::rename(fsPath(from), fsPath(to), code);
    return code ? brulion::vaultutil::Replaced::Failed : brulion::vaultutil::Replaced::Done;
}

bool failingSync(QFileDevice &, QString *error)
{
    *error = u"brak miejsca na nośniku (test)"_s;
    return false;
}

// --- a folder clock that ticks only when told (vaultutil::setStampFilter())
// A file system that stamps changes with its clock tick (NTFS, Linux before 6.13, Android) gives
// every change of a folder within one tick that tick's stamp. Simulated over the real stamps (a
// change moves them here: multigrain times): a folder's stamp is the tick in which it was first
// seen — the tick of its change, as long as the clock moves on only between a change and the next
// look, which the test makes sure of: it ticks by hand, and right before each of the vault's renames
// into the notebook (tickingReplace).
std::mutex g_clockMutex;
std::map<qint64, qint64> g_folderTicks; ///< a folder's real stamp -> its tick's stamp
qint64 g_tick = 0;
QString g_tickingRoot;             ///< renames into it tick first
std::atomic<int> g_slowLooksMs{0}; ///< every look at a folder's stamp takes this long
brulion::vaultutil::FileReplace g_realReplace = nullptr;

std::optional<qint64> tickedFolderStamp(const QString &abs, std::optional<qint64> stamp)
{
    if (!stamp || !QFileInfo(abs).isDir())
        return stamp; // (files keep their real stamps)
    const std::lock_guard lock(g_clockMutex);
    // Windows' 15.6 ms ticks from a time in 2023, plus 7 ns: never a whole millisecond (coarse).
    constexpr qint64 kBase = 1'700'000'000'000'000'000;
    constexpr qint64 kTickNs = 15'625'000;
    return g_folderTicks.try_emplace(*stamp, kBase + g_tick * kTickNs + 7).first->second;
}

std::optional<qint64> slowFolderStamp(const QString &abs, std::optional<qint64> stamp)
{
    if (QFileInfo(abs).isDir())
        QThread::msleep(g_slowLooksMs.load());
    return stamp;
}

void nextTick()
{
    const std::lock_guard lock(g_clockMutex);
    ++g_tick;
}

brulion::vaultutil::Replaced tickingReplace(const QString &from, const QString &to, QString *error)
{
    if (to.startsWith(g_tickingRoot + u'/'))
        nextTick();
    if (g_realReplace)
        return g_realReplace(from, to, error);
    std::error_code code;
    std::filesystem::rename(fsPath(from), fsPath(to), code);
    if (code && error)
        *error = QString::fromStdString(code.message());
    return code ? brulion::vaultutil::Replaced::Failed : brulion::vaultutil::Replaced::Done;
}

/// The coarse folder clock of the notebook `root` while it lives.
struct TickingFolderClock {
    explicit TickingFolderClock(const QString &root)
    {
        g_folderTicks.clear();
        g_tick = 0;
        g_tickingRoot = root;
        g_realReplace = brulion::vaultutil::fileReplace();
        brulion::vaultutil::setFileReplace(&tickingReplace);
        brulion::vaultutil::setStampFilter(&tickedFolderStamp);
    }
    ~TickingFolderClock()
    {
        brulion::vaultutil::setStampFilter(nullptr);
        brulion::vaultutil::setFileReplace(g_realReplace);
    }
    Q_DISABLE_COPY_MOVE(TickingFolderClock)
};

/// Files of the folder `dir` itself (hidden ones too).
QStringList filesIn(const QString &dir)
{
    return QDir(dir).entryList(QDir::Files | QDir::Hidden | QDir::System);
}

} // namespace

class TestVaultCoarse : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // When equal stamps prove a file unchanged since a read: fine-grained, and old already then.
    void stampsVouch_data()
    {
        QTest::addColumn<bool>("fine");
        QTest::addColumn<bool>("hasNow");
        QTest::addColumn<qint64>("ageMs");    // the file system's now minus the file's stamp
        QTest::addColumn<int>("sinceReadMs"); // from the mark to the check
        QTest::addColumn<bool>("vouches");
        QTest::newRow("old fine stamp") << true << true << qint64(10'000) << 0 << true;
        QTest::newRow("a second old") << true << true << qint64(1000) << 0 << true;
        QTest::newRow("written just before the read") << true << true << qint64(20) << 0 << false;
        QTest::newRow("two ticks old") << true << true << qint64(99) << 0 << false;
        QTest::newRow("old, but the save took long") << true << true << qint64(300) << 250 << false;
        QTest::newRow("from the future") << true << true << qint64(-5000) << 0 << false;
        QTest::newRow("coarse") << false << true << qint64(10'000) << 0 << false;
        QTest::newRow("no clock of the file system") << true << false << qint64(10'000) << 0 << false;
    }
    void stampsVouch()
    {
        QFETCH(bool, fine);
        QFETCH(bool, hasNow);
        QFETCH(qint64, ageMs);
        QFETCH(int, sinceReadMs);
        QFETCH(bool, vouches);
        vaultutil::ReadMark mark;
        const qint64 stamp = 1'763'456'789'123'456'789;
        mark.fineMtime = fine ? std::optional<qint64>(stamp) : std::nullopt;
        mark.since.start();
        if (sinceReadMs > 0)
            QThread::msleep(sinceReadMs);
        const std::optional<qint64> now = hasNow ? std::optional<qint64>(stamp + ageMs * 1'000'000) : std::nullopt;
        QCOMPARE(vaultutil::stampsVouch(mark, mark.fineMtime, now), vouches);
    }

    // stillAsRead(): the kind, size and times — and the bytes, where the stamps cannot tell.
    void stillAsRead()
    {
        QTemporaryDir dir;
        const QString abs = dir.path() + u"/Zakupy.md"_s;
        const qint64 old = QDateTime::currentMSecsSinceEpoch() * 1'000'000 + 777; // (fine "now")

        // Absent, and still or no longer.
        vaultutil::ReadMark absent = vaultutil::markBeforeRead(abs);
        QVERIFY(absent.absent);
        QVERIFY(vaultutil::stillAsRead(abs, absent, std::nullopt));
        writeCoarse(abs, "- chleb\n");
        QVERIFY(!vaultutil::stillAsRead(abs, absent, std::nullopt));

        // Coarse: the same stat with other bytes is a change, with the same bytes none.
        vaultutil::ReadMark coarse = vaultutil::markBeforeRead(abs);
        QVERIFY(!coarse.absent);
        QVERIFY(!coarse.fineMtime);
        coarse.bytes = "- chleb\n";
        QVERIFY(vaultutil::stillAsRead(abs, coarse, old));
        QVERIFY(rewriteKeepingStamp(abs, "- mleko\n"));
        QVERIFY(!vaultutil::stillAsRead(abs, coarse, old));
        QVERIFY(rewriteKeepingStamp(abs, "- chleb\n"));
        QVERIFY(vaultutil::stillAsRead(abs, coarse, old));
        // Another size or time: a change, whatever the bytes.
        writeCoarse(abs, "- chleb\n- masło\n");
        QVERIFY(!vaultutil::stillAsRead(abs, coarse, old));

        // Fine and old: the stat vouches (the bytes are not read).
        QFile::remove(abs);
        plantFine(abs, "- żółty ser\n", -60'000);
        vaultutil::ReadMark fine = vaultutil::markBeforeRead(abs);
        QVERIFY(fine.fineMtime);
        fine.bytes = "- żółty ser\n";
        const qint64 fsNow = *fine.fineMtime + 60'000'000'000LL;
        QVERIFY(vaultutil::stillAsRead(abs, fine, fsNow));
        QVERIFY(rewriteKeepingStamp(abs, "- źółty ser\n")); // (a forged nanosecond stamp: not seen)
        QVERIFY(vaultutil::stillAsRead(abs, fine, fsNow));
        // Fine but written just before the read: the bytes decide.
        QVERIFY(!vaultutil::stillAsRead(abs, fine, *fine.fineMtime + 5'000'000));
#ifdef Q_OS_UNIX
        // A link where the file was: not the file read.
        QVERIFY(QFile::remove(abs));
        QVERIFY(QFile::link(dir.path() + u"/inny.md"_s, abs));
        QVERIFY(!vaultutil::stillAsRead(abs, fine, fsNow));
#endif
    }

    // Another program writes the note while a save writes and syncs its own (a sync client
    // renaming its version over it, an editor saving in place) and puts the size and time back
    // exactly: the save does not go over it. Coarse storage: the stamps never vouch; fine
    // storage, the note written just before the save: they do not either (racy).
    void rewriteWhileSaving_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("stamps");
        for (const QString &mode : {u"async thread"_s, u"async inline"_s, u"sync"_s}) {
            for (const QString &stamps : {u"coarse"_s, u"fine, just written"_s, u"fine, another size"_s})
                QTest::newRow(qPrintable(mode + u", "_s + stamps)) << mode << stamps;
        }
    }
    void rewriteWhileSaving()
    {
        QFETCH(QString, mode);
        QFETCH(QString, stamps);
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString path = u"Dziennik/2026-11-18.md"_s;
        const QString abs = f.root() + u'/' + path;
        const QByteArray before = "- rano: kawa\n";
        if (stamps == "coarse"_L1)
            writeCoarse(abs, before, 60);
        else
            plantFine(abs, before, 3'600'000); // (from the future: never older than the read, whatever the load)
        QVERIFY(f.open());
        Writer writer;
        writer.attach(*f.vault, mode == "async thread"_L1);
        const QString text = f.text(path);
        QCOMPARE(text, QString::fromUtf8(before));
        const QByteArray theirs = stamps == "fine, another size"_L1 ? QByteArray("- rano: herbata z cytryną\n")
                                                                    : QByteArray("- rano: żur\n"); // (13 bytes too)
        QCOMPARE(stamps == "fine, another size"_L1, theirs.size() != before.size());
        auto rewritten = std::make_shared<std::atomic<int>>(0);
        f.vault->setBeforeRenameHook([abs, theirs, rewritten](const VaultPath &) {
            *rewritten = rewriteKeepingStamp(abs, theirs) ? 1 : -1;
        });
        const WriteOutcome outcome = save(*f.vault, mode, path, text + u"- południe: obiad\n"_s, text);
        f.vault->setBeforeRenameHook({});
        QCOMPARE(rewritten->load(), 1);
        QCOMPARE(outcome, WriteOutcome::Changed);
        QCOMPARE(f.read(path), theirs);
        QVERIFY(!hasTemp(f.root()));
        QCOMPARE(f.changes(), QStringList{u"modify:Dziennik/2026-11-18.md@external"_s});
        QCOMPARE(f.text(path), QString::fromUtf8(theirs));
        QVERIFY(f.historyContents(path).contains(QString::fromUtf8(before)));
        // The next save over their text goes through.
        QCOMPARE(save(*f.vault, mode, path, f.text(path) + u"- wieczór\n"_s, f.text(path)), WriteOutcome::Written);
        QCOMPARE(f.read(path), theirs + "- wieczór\n");
    }

    // A note created by another program while a save creates it: kept, reported.
    void createdWhileSaving_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("async") << u"async inline"_s; // (a new file: written at once, on the vault's thread)
        QTest::newRow("sync") << u"sync"_s;
    }
    void createdWhileSaving()
    {
        QFETCH(QString, mode);
        Fixture f;
        QVERIFY(f.open());
        const QString abs = f.root() + u"/Nowa.md"_s;
        f.vault->setBeforeRenameHook([&](const VaultPath &) { writeCoarse(abs, "- z innego urządzenia\n"); });
        QCOMPARE(save(*f.vault, mode, u"Nowa.md"_s, u"- moja\n"_s, QString()), WriteOutcome::Changed);
        f.vault->setBeforeRenameHook({});
        QCOMPARE(f.read(u"Nowa.md"_s), QByteArray("- z innego urządzenia\n"));
        QCOMPARE(f.changes(), QStringList{u"create:Nowa.md@external"_s});
        QVERIFY(!hasTemp(f.root()));
    }

    // An own write on coarse storage, then another program's write of the same size in the same
    // tick (the stat stays): the next listing compares the content and finds it — until the tick
    // has passed; then the stat is trusted again (no note read on every listing).
    void ownWriteThenSameTick_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("sync") << u"sync"_s;
        QTest::newRow("async thread") << u"async thread"_s;
        QTest::newRow("writeText") << u"writeText"_s;
    }
    void ownWriteThenSameTick()
    {
        QFETCH(QString, mode);
        CoarseStorage coarse;
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString path = u"Zakupy.md"_s;
        const QString abs = f.root() + u'/' + path;
        writeCoarse(abs, "- chleb\n", 3600);
        QVERIFY(f.open());
        Writer writer;
        writer.attach(*f.vault, mode == "async thread"_L1);
        const QString mine = u"- chleb\n- mleko\n"_s;
        if (mode == "writeText"_L1)
            QVERIFY(f.vault->writeText(path, mine));
        else
            QCOMPARE(save(*f.vault, mode, path, mine, f.text(path)), WriteOutcome::Written);
        QVERIFY(!vaultutil::fineMtime(abs)); // (the simulation works)
        f.spy->clear();

        QVERIFY(rewriteKeepingStamp(abs, "- chleb\n- jajka\n")); // the same size, the same tick
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:Zakupy.md@external"_s});
        QCOMPARE(f.text(path), u"- chleb\n- jajka\n"_s);
        QVERIFY(f.vault->rescan().isEmpty());

        // Once the tick has passed (and a listing compared the note after it) the stat vouches again.
        f.clock.advanceMs(vaultutil::kSettleMs);
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(rewriteKeepingStamp(abs, "- chleb\n- ryż!\n")); // (impossible now on real storage: unseen)
        QVERIFY(f.vault->rescan().isEmpty());
    }

    // Another program writes twice within a tick, the second time with the same size: the first
    // write is found by its stat, the second by its content.
    void foreignWritesTwiceInATick()
    {
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString abs = f.root() + u"/Notatka.md"_s;
        writeCoarse(abs, "- pierwsza\n", 3600);
        QVERIFY(f.open());
        writeCoarse(abs, "- druga wersja\n");
        f.clock.now = QDateTime::currentDateTime(); // (the write is recent by the vault's clock, whatever the load)
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:Notatka.md@external"_s});
        QVERIFY(rewriteKeepingStamp(abs, "- trzecia ąę\n")); // (15 bytes, as the second)
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:Notatka.md@external"_s});
        QCOMPARE(f.text(u"Notatka.md"_s), u"- trzecia ąę\n"_s);
        // An old write's stat vouches: nothing to compare.
        writeCoarse(f.root() + u"/Stara.md"_s, "- stara\n", 3600);
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"create:Stara.md@external"_s});
        QVERIFY(rewriteKeepingStamp(f.root() + u"/Stara.md"_s, "- stary\n"));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    // Another program touches a note (the same bytes, a new coarse stamp), then writes other bytes
    // of the same size within that tick: the touch is no change, the write is found by content.
    void touchThenSameTick()
    {
        Fixture f;
        const QString abs = f.root() + u"/Notatka.md"_s;
        writeCoarse(abs, "- wersja A\n", 3600);
        f.clock.now = QDateTime::currentDateTime();
        QVERIFY(f.open());
        QCOMPARE(f.text(u"Notatka.md"_s), u"- wersja A\n"_s);
        writeCoarse(abs, "- wersja A\n");
        f.clock.now = QDateTime::currentDateTime();
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(rewriteKeepingStamp(abs, "- wersja B\n")); // (the same size, the same tick)
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:Notatka.md@external"_s});
        QCOMPARE(f.text(u"Notatka.md"_s), u"- wersja B\n"_s);
    }

    // Whole milliseconds are coarse in every clock — also MSVC's 100 ns ticks from 1601, whose
    // count in nanoseconds does not fit in 64 bits (2026: ~1.3e19) — and differences of stamps are
    // right where the nanoseconds wrap.
    void fineStampOfAnyClock()
    {
        using Ticks = std::chrono::duration<qint64, std::ratio<1, 10'000'000>>; // MSVC's file_clock
        // 2026-11-18 12:00:00 UTC in 100 ns ticks since 1601-01-01 (11 644 473 600 s before 1970).
        const Ticks noon{(qint64(1'763'467'200) + qint64(11'644'473'600)) * 10'000'000};
        QVERIFY(!vaultutil::fineStampOf(noon));                                     // FAT's whole seconds
        QVERIFY(!vaultutil::fineStampOf(noon + std::chrono::milliseconds(10)));     // exFAT's 10 ms
        const std::optional<qint64> a = vaultutil::fineStampOf(noon + Ticks(1234)); // NTFS: 123.4 µs
        const std::optional<qint64> b = vaultutil::fineStampOf(noon + std::chrono::seconds(2) + Ticks(5678));
        QVERIFY(a && b);
        QCOMPARE(vaultutil::fineStampAge(*b, *a), qint64(2'000'444'400));
        QCOMPARE(vaultutil::fineStampAge(*a, *b), qint64(-2'000'444'400));
        // libstdc++'s nanoseconds from 2174 (negative now) and libc++'s from 1970: exact.
        const std::chrono::nanoseconds before2174{-qint64(4'674'196'800) * 1'000'000'000 + 123'456'789};
        QCOMPARE(vaultutil::fineStampOf(before2174), std::optional<qint64>(before2174.count()));
        const std::chrono::nanoseconds since1970{qint64(1'763'467'200) * 1'000'000'000 + 7};
        QCOMPARE(vaultutil::fineStampOf(since1970), std::optional<qint64>(since1970.count()));
        QVERIFY(!vaultutil::fineStampOf(std::chrono::nanoseconds(qint64(1'763'467'200) * 1'000'000'000)));
    }

    // A note read (by an editor, the index) right after another program wrote it, which writes
    // again in the same tick with the same size: found by the next listing.
    void readThenSameTick()
    {
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString abs = f.root() + u"/Notatka.md"_s;
        writeCoarse(abs, "- wersja A\n");
        f.clock.now = QDateTime::currentDateTime(); // (the write is recent by the vault's clock, whatever the load)
        QVERIFY(f.open());
        QCOMPARE(f.text(u"Notatka.md"_s), u"- wersja A\n"_s);
        QVERIFY(rewriteKeepingStamp(abs, "- wersja B\n"));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:Notatka.md@external"_s});
        QCOMPARE(f.text(u"Notatka.md"_s), u"- wersja B\n"_s);
    }

    // A larger text written over without a condition (writeText()) on coarse storage, right after
    // another program's write of the same size in the same tick: its text goes to History first.
    void bigTextKeptInHistory()
    {
        CoarseStorage coarse;
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString path = u"Duża.md"_s;
        const QString abs = f.root() + u'/' + path;
        const QByteArray line = "- wiersz z polskimi znakami: zażółć gęślą jaźń\n";
        QByteArray big;
        while (big.size() < 2 * 1024 * 1024)
            big += line;
        writeCoarse(abs, big, 3600);
        QVERIFY(f.open());
        QCOMPARE(f.text(path).size(), QString::fromUtf8(big).size());
        QVERIFY(f.vault->writeText(path, QString::fromUtf8(big) + u"- moja\n"_s));
        const QByteArray theirs = big + "- obca\n";
        QVERIFY(rewriteKeepingStamp(abs, theirs));
        QVERIFY(f.vault->writeText(path, u"- krótko\n"_s));
        QVERIFY(f.historyContents(path).contains(QString::fromUtf8(theirs)));
    }

    // A rename refused for a moment is tried again (0.3 s), the compare-and-swap before
    // every attempt; a refusal that lasts, or another failure, fails the write without a trace.
    void busyRename_data()
    {
        QTest::addColumn<int>("refusals");
        QTest::addColumn<bool>("busy"); // the refusal is Busy (else Failed)
        QTest::addColumn<bool>("theirsMeanwhile");
        QTest::addColumn<int>("calls");
        QTest::addColumn<int>("outcome"); // WriteOutcome
        const int attempts = int(std::size(vaultutil::kReplaceWaitsMs)) + 1;
        QTest::newRow("busy three times") << 3 << true << false << 4 << int(WriteOutcome::Written);
        QTest::newRow("busy for good") << 1000 << true << false << attempts << int(WriteOutcome::Failed);
        QTest::newRow("failed") << 1000 << false << false << 1 << int(WriteOutcome::Failed);
        QTest::newRow("busy, another program writes") << 1 << true << true << 1 << int(WriteOutcome::Changed);
    }
    void busyRename()
    {
        QFETCH(int, refusals);
        QFETCH(bool, busy);
        QFETCH(bool, theirsMeanwhile);
        QFETCH(int, calls);
        QFETCH(int, outcome);
        Fixture f;
        f.write(u"Plan.md"_s, "- plan\n");
        QVERIFY(f.open());
        const vaultutil::FileReplace previous = vaultutil::fileReplace();
        const auto restore = qScopeGuard([&] {
            vaultutil::setFileReplace(previous);
            g_whileRefused = {};
        });
        vaultutil::setFileReplace(&testReplace);
        g_replaceCalls = 0;
        g_replaceRefusals = refusals;
        g_refusal = busy ? vaultutil::Replaced::Busy : vaultutil::Replaced::Failed;
        const QString root = f.root();
        if (theirsMeanwhile)
            g_whileRefused = [root] { writeBehind(root, u"Plan.md"_s, "- plan innego programu\n"); };
        QElapsedTimer timer;
        timer.start();
        const WriteOutcome result = f.vault->writeTextIfUnchanged(u"Plan.md"_s, u"- mój plan\n"_s, u"- plan\n"_s);
        QCOMPARE(int(result), outcome);
        QCOMPARE(g_replaceCalls, calls);
        QVERIFY(!hasTemp(f.root()));
        if (outcome == int(WriteOutcome::Written))
            QCOMPARE(f.read(u"Plan.md"_s), QByteArray("- mój plan\n"));
        else if (theirsMeanwhile)
            QCOMPARE(f.read(u"Plan.md"_s), QByteArray("- plan innego programu\n"));
        else
            QCOMPARE(f.read(u"Plan.md"_s), QByteArray("- plan\n"));
        if (outcome == int(WriteOutcome::Failed))
            QVERIFY2(f.vault->errorString().contains(u"zajęty przez skaner (test)"_s),
                     qPrintable(f.vault->errorString()));
        if (calls == int(std::size(vaultutil::kReplaceWaitsMs)) + 1)
            QVERIFY(timer.elapsed() >= 300); // (all the waits: 0.31 s)
    }

    // A failing sync says why, and leaves no temporary file.
    void failingSyncSaysWhy()
    {
        Fixture f;
        f.write(u"Plan.md"_s, "- plan\n");
        QVERIFY(f.open());
        const vaultutil::FileSync previous = vaultutil::fileSync();
        const auto restore = qScopeGuard([&] { vaultutil::setFileSync(previous); });
        vaultutil::setFileSync(&failingSync);
        QVERIFY(!f.vault->writeText(u"Plan.md"_s, u"- mój plan\n"_s));
        QVERIFY2(f.vault->errorString().contains(u"brak miejsca na nośniku (test)"_s),
                 qPrintable(f.vault->errorString()));
        QCOMPARE(f.read(u"Plan.md"_s), QByteArray("- plan\n"));
        QVERIFY(!hasTemp(f.root()));
    }

    // probeFolderClock(): four own changes of the folder, each right after a look at its stamp —
    // Coarse when one kept it, Fine only when all moved it within less than any clock's tick,
    // Unknown when it cannot tell. No file is left behind.
    void probeFolderClock()
    {
        using vaultutil::FolderClock;
        QTemporaryDir tmp;
        const QString dir = tmp.path() + u"/Dziennik źdźbło"_s;
        QVERIFY(QDir().mkpath(dir));
        // This file system's own clock: Fine with multigrain times (Linux 6.13 and later on ext4,
        // XFS, Btrfs, tmpfs), Coarse on NTFS, older kernels and Android; Unknown where it could not
        // tell (no fine stamps, a stalled run).
        const FolderClock real = vaultutil::probeFolderClock(dir);
        const char *const names[] = {"fine", "coarse", "unknown"};
        qInfo("this file system's folder clock: %s", names[int(real)]);
        QCOMPARE(filesIn(dir), QStringList());
        // A clock that stamps with its tick (ticking only when told here): Coarse, every time.
        {
            TickingFolderClock clock(dir);
            for (int i = 0; i < 5; ++i)
                QCOMPARE(vaultutil::probeFolderClock(dir), FolderClock::Coarse);
            nextTick();
            QCOMPARE(vaultutil::probeFolderClock(dir, vaultutil::fineMtime(dir)), FolderClock::Coarse);
        }
        QCOMPARE(filesIn(dir), QStringList());
        // Every look takes longer than a tick: moved stamps prove nothing then.
        g_slowLooksMs = 1;
        vaultutil::setStampFilter(&slowFolderStamp);
        const FolderClock slow = vaultutil::probeFolderClock(dir);
        vaultutil::setStampFilter(nullptr);
        QVERIFY(slow != FolderClock::Fine);
        QCOMPARE(filesIn(dir), QStringList());
        // No folder, or one that cannot be changed: Unknown.
        QCOMPARE(vaultutil::probeFolderClock(dir + u"/nie ma"_s), FolderClock::Unknown);
        if (test::hasUnixPermissions()) {
            const QString locked = tmp.path() + u"/Zamknięty"_s;
            QVERIFY(QDir().mkpath(locked));
            QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::ExeOwner));
            QCOMPARE(vaultutil::probeFolderClock(locked), FolderClock::Unknown);
            QVERIFY(QFile::setPermissions(locked, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
            QCOMPARE(filesIn(locked), QStringList());
        }
    }

    // Another program changes a folder within the clock tick in which an own save made its
    // temporary file there (Windows CI's foreignChangeWhileSaving): where changes are stamped with
    // the clock's tick, the folder keeps the stamp the temporary file gave it, and its stamps cannot
    // tell the two apart — the echo of that save must not be trusted. Simulated by a folder clock
    // that ticks by hand and right before the vault's renames: never between the temporary file and
    // the other program's change, always between that and the rename (so no own step meets another
    // in a tick, as the sync between them mostly makes sure of).
    void foreignChangeInTheTickOfTheTemporaryFile_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("expected");
        for (const QString &mode : {u"async thread"_s, u"async inline"_s, u"sync"_s}) {
            const auto row = [&](const QString &kind, const QString &expected) {
                QTest::newRow(qPrintable(mode + u", "_s + kind)) << mode << kind << expected;
            };
            row(u"created"_s, u"create:Dziennik/Z telefonu ż.md@external"_s);
            row(u"replaced"_s, u"modify:Dziennik/Zakupy.md@external"_s);
            row(u"deleted"_s, u"delete:Dziennik/Zakupy.md@external"_s);
        }
    }
    void foreignChangeInTheTickOfTheTemporaryFile()
    {
        QFETCH(QString, mode);
        QFETCH(QString, kind);
        QFETCH(QString, expected);
        Fixture f;
        const QString path = u"Dziennik/2026-11-19.md"_s;
        f.write(path, "- moja\n");
        f.write(u"Dziennik/Zakupy.md"_s, "- chleb\n");
        TickingFolderClock clock(f.root()); // (the listing's stamps are the clock's too)
        QVERIFY(f.open());
        Writer writer;
        writer.attach(*f.vault, mode == "async thread"_L1);
        f.vault->setWatchDelay(30);
        f.vault->startWatching();
        // An own save first: the folder's stamp is known as the save left it.
        nextTick();
        QCOMPARE(save(*f.vault, mode, path, u"- moja\n- 1\n"_s, u"- moja\n"_s), WriteOutcome::Written);
        QTest::qWait(100); // (its echo)
        f.vault->processPendingEvents();
        QCOMPARE(f.changes(), QStringList{u"modify:Dziennik/2026-11-19.md@user"_s});
        f.spy->clear();

        nextTick();
        const QString dir = f.root() + u"/Dziennik"_s;
        auto done = std::make_shared<std::atomic<int>>(0);
        f.vault->setBeforeRenameHook([dir, kind, done](const VaultPath &) {
            // (On the writer thread: no QVERIFY. No tick: the temporary file's.)
            bool ok = false;
            if (kind == "deleted"_L1) {
                ok = QFile::remove(dir + u"/Zakupy.md"_s);
            } else {
                const QString name = kind == "created"_L1 ? u"/Z telefonu ż.md"_s : u"/.syncthing.Zakupy.md.tmp"_s;
                QFile file(dir + name);
                ok = file.open(QIODevice::WriteOnly) && file.write("- chleb z innego urządzenia\n") > 0;
                file.close();
                if (ok && kind == "replaced"_L1) {
                    // (Replaces on Windows too: MoveFileExW. Again while a virus scanner looks.)
                    std::error_code code;
                    for (int attempt = 0; attempt < 50; ++attempt) {
                        std::filesystem::rename(fsPath(dir + name), fsPath(dir + u"/Zakupy.md"_s), code);
                        if (code != std::errc::permission_denied)
                            break;
                        QThread::msleep(20);
                    }
                    ok = !code;
                }
            }
            *done = ok ? 1 : -1;
        });
        QCOMPARE(save(*f.vault, mode, path, u"- moja\n- 1\n- 2\n"_s, u"- moja\n- 1\n"_s), WriteOutcome::Written);
        f.vault->setBeforeRenameHook({});
        QCOMPARE(done->load(), 1);
        QTRY_VERIFY2(f.changes().contains(expected), qPrintable(f.changes().join(u", "_s)));
        QCOMPARE(f.changes().constFirst(), u"modify:Dziennik/2026-11-19.md@user"_s);
        QCOMPARE(f.read(path), QByteArray("- moja\n- 1\n- 2\n"));
    }

private:
    /// A file with a fine-grained time `offsetMs` from now (nanoseconds that are not whole ms).
    static void plantFine(const QString &abs, const QByteArray &bytes, qint64 offsetMs)
    {
        QVERIFY(QDir().mkpath(QFileInfo(abs).absolutePath()));
        QFile file(abs);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        std::error_code code;
        const auto now = std::filesystem::last_write_time(fsPath(abs), code);
        QVERIFY(!code);
        // (In the clock's own ticks: MSVC's are 100 ns, a time point of nanoseconds is another type.)
        const auto offset = std::chrono::duration_cast<std::filesystem::file_time_type::duration>(
                std::chrono::milliseconds(offsetMs) + std::chrono::microseconds(123));
        std::filesystem::last_write_time(fsPath(abs), now + offset, code);
        QVERIFY(!code);
        QVERIFY(brulion::vaultutil::fineMtime(abs));
    }
};

QTEST_GUILESS_MAIN(TestVaultCoarse)
#include "tst_vault_coarse.moc"
