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
//    tried again for about a second, the compare-and-swap before every attempt; a final failure
//    leaves no temporary file. A failing sync says why.
// The coarse times are simulated: a file sync that cuts the temporary file's time to an even second
// (as FAT keeps it), files of other programs written with whole-second times, and rewrites that
// put the file's exact time back (std::filesystem: nanoseconds).
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
            plantFine(abs, before, 1500); // (not older than the save's read, whatever the load)
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

    // A note read (by an editor, the index) right after another program wrote it, which writes
    // again in the same tick with the same size: found by the next listing.
    void readThenSameTick()
    {
        Fixture f;
        f.clock.now = QDateTime::currentDateTime();
        const QString abs = f.root() + u"/Notatka.md"_s;
        writeCoarse(abs, "- wersja A\n");
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

    // A rename refused for a moment is tried again (about a second), the compare-and-swap before
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
            QVERIFY(timer.elapsed() >= 1000); // (all the waits)
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
        std::filesystem::last_write_time(
                fsPath(abs), now + std::chrono::milliseconds(offsetMs) + std::chrono::nanoseconds(123'457), code);
        QVERIFY(!code);
        QVERIFY(brulion::vaultutil::fineMtime(abs));
    }
};

QTEST_GUILESS_MAIN(TestVaultCoarse)
#include "tst_vault_coarse.moc"
