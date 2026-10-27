// SyncHistory (sync/synchistory.h): run records from reports for every outcome (also a run the
// watchdog stopped), caps, JSON round trips and malformed records, capacity, folding quiet runs,
// run numbers, persistence through a LocalStore file, the needsUser table and redaction.
#include "sync/synchistory.h"

#include "log/redact.h"
#include "vault/localstore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace brulion;
using namespace brulion::sync;
using namespace Qt::StringLiterals;

namespace {

const QDateTime kStart(QDate(2026, 10, 18), QTime(14, 3, 22, 517));

SyncReport report(int number, SyncTrigger trigger = SyncTrigger::Timer)
{
    SyncReport r;
    r.runNumber = number;
    r.trigger = trigger;
    r.started = kStart.addSecs(number * 60);
    r.finished = r.started.addMSecs(1100);
    r.durationMs = 1100;
    r.phases = {SyncPhaseTime{SyncPhase::Connect, 0, 5, 0, 0}, SyncPhaseTime{SyncPhase::ListRemote, 5, 800, 41, 1400},
                SyncPhaseTime{SyncPhase::Transfer, 900, 150, 3, 0}};
    r.stats.requests = 41;
    r.stats.requestsByMethod = {{"PROPFIND", 41}};
    r.stats.listingBytes = 1400;
    r.stats.protocol = "h2";
    r.stats.slowest = {SyncRequestTime{"PROPFIND", u"Dziennik/"_s, 207, 1900, 900}};
    return r;
}

SyncReport changed(int number)
{
    SyncReport r = report(number, SyncTrigger::LocalChange);
    r.uploaded = 3;
    r.downloaded = 1;
    r.changes = {SyncChange{u"a.md"_s, SyncAction::Upload}, SyncChange{u"b.md"_s, SyncAction::Upload},
                 SyncChange{u"c.md"_s, SyncAction::Upload}, SyncChange{u"d.md"_s, SyncAction::Download}};
    return r;
}

} // namespace

class TestSyncHistory : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void cleanup() { logging::clearSecrets(); }

    void outcomes_data();
    void outcomes();
    void needsUserTable_data();
    void needsUserTable();
    void names();
    void capsErrorsAndConflicts();
    void jsonRoundTrip();
    void malformedRecordsAreSkipped();
    void capacity();
    void foldsQuietRuns();
    void numbers();
    void persistsAndReloads();
    void redactsStoredMessages();
    void clearEmpties();
};

void TestSyncHistory::outcomes_data()
{
    QTest::addColumn<SyncReport>("input");
    QTest::addColumn<bool>("stalled");
    QTest::addColumn<QString>("outcome");
    QTest::addColumn<QString>("code");
    QTest::addColumn<int>("status");

    QTest::newRow("ok") << report(1) << false << u"ok"_s << QString() << 0;
    SyncReport conflicts = changed(2);
    conflicts.conflicts = {u"Plan podróży.md"_s};
    QTest::newRow("conflicts") << conflicts << false << u"conflicts"_s << u"conflicts"_s << 0;
    SyncReport files = changed(3);
    files.errors = {SyncFileError{u"Żółw.md"_s, u"PUT failed: HTTP 423"_s, u"locked"_s, 423}};
    QTest::newRow("files") << files << false << u"files"_s << u"files"_s << 423;
    SyncReport confirm = report(4);
    confirm.needsConfirmation = true;
    confirm.pendingDeletions = {SyncChange{u"x.md"_s, SyncAction::DeleteRemote}};
    QTest::newRow("needs confirmation") << confirm << false << u"needs-confirmation"_s << u"needs-confirmation"_s << 0;
    SyncReport refused = report(5);
    refused.syncError = SyncError{SyncErrorCode::LocalEmpty, u"None of the 12 files exist locally"_s};
    QTest::newRow("refused") << refused << false << u"refused"_s << u"local-empty"_s << 0;
    SyncReport failed = report(6);
    failed.remoteError = WebDavError{WebDavErrorCode::Auth, 401, u"PROPFIND failed: HTTP 401"_s, {}};
    QTest::newRow("failed") << failed << false << u"failed"_s << u"auth"_s << 401;
    SyncReport cancelled = report(7);
    cancelled.cancelled = true;
    QTest::newRow("cancelled") << cancelled << false << u"cancelled"_s << QString() << 0;
    QTest::newRow("stalled") << cancelled << true << u"failed"_s << u"stalled"_s << 0;
    SyncReport cancelledAfterError = failed;
    cancelledAfterError.cancelled = true;
    QTest::newRow("cancelled wins") << cancelledAfterError << false << u"cancelled"_s << QString() << 401;
}

void TestSyncHistory::outcomes()
{
    QFETCH(SyncReport, input);
    QFETCH(bool, stalled);
    QFETCH(QString, outcome);
    QFETCH(QString, code);
    QFETCH(int, status);
    const SyncRunRecord r = SyncRunRecord::fromReport(input, stalled);
    QCOMPARE(syncOutcomeName(r.outcome), outcome);
    QCOMPARE(syncOutcomeName(outcomeOf(input, stalled)), outcome);
    QCOMPARE(r.errorCode, code);
    if (r.outcome != SyncOutcome::Cancelled)
        QCOMPARE(r.errorStatus, status);
    QCOMPARE(r.number, input.runNumber);
    QCOMPARE(r.trigger, input.trigger);
    QCOMPARE(r.phases, input.phases);
    QCOMPARE(r.stats, input.stats);
    QCOMPARE(r.uploaded, input.uploaded);
    QCOMPARE(r.fileErrors, int(input.errors.size()));
    QCOMPARE(r.conflicts, int(input.conflicts.size()));
    QCOMPARE(r.isQuiet(), outcome == u"ok" && input.changeCount() == 0);
    if (!code.isEmpty() && code != u"conflicts")
        QVERIFY(!r.errorMessage.isEmpty());
}

void TestSyncHistory::needsUserTable_data()
{
    QTest::addColumn<int>("outcome");
    QTest::addColumn<QString>("code");
    QTest::addColumn<bool>("expected");
    const int failed = int(SyncOutcome::Failed);
    const int refused = int(SyncOutcome::Refused);
    for (const QString &c : {u"auth"_s, u"forbidden"_s, u"not-found"_s, u"parse"_s, u"insufficient-storage"_s})
        QTest::newRow(qPrintable(u"failed "_s + c)) << failed << c << true;
    for (const QString &c : {u"network"_s, u"timeout"_s, u"server"_s, u"stalled"_s, u"http"_s})
        QTest::newRow(qPrintable(u"failed "_s + c)) << failed << c << false;
    for (const QString &c : {u"local-empty"_s, u"remote-empty"_s, u"vault-unavailable"_s})
        QTest::newRow(qPrintable(u"refused "_s + c)) << refused << c << true;
    QTest::newRow("refused not-ready") << refused << u"not-ready"_s << false;
    QTest::newRow("confirmation") << int(SyncOutcome::NeedsConfirmation) << u"needs-confirmation"_s << true;
    QTest::newRow("conflicts") << int(SyncOutcome::Conflicts) << u"conflicts"_s << true;
    QTest::newRow("files") << int(SyncOutcome::FileErrors) << u"files"_s << false;
    QTest::newRow("cancelled") << int(SyncOutcome::Cancelled) << QString() << false;
    QTest::newRow("ok") << int(SyncOutcome::Ok) << QString() << false;
}

void TestSyncHistory::needsUserTable()
{
    QFETCH(int, outcome);
    QFETCH(QString, code);
    QFETCH(bool, expected);
    QCOMPARE(needsUser(SyncOutcome(outcome), code), expected);
}

void TestSyncHistory::names()
{
    for (int i = int(SyncOutcome::Ok); i <= int(SyncOutcome::Cancelled); ++i)
        QCOMPARE(syncOutcomeFromName(syncOutcomeName(SyncOutcome(i))), SyncOutcome(i));
    QCOMPARE(syncOutcomeFromName(u"zły"), std::nullopt);
    for (int i = int(SyncPhase::Connect); i <= int(SyncPhase::SaveState); ++i)
        QCOMPARE(syncPhaseFromName(syncPhaseName(SyncPhase(i))), SyncPhase(i));
    QCOMPARE(syncPhaseName(SyncPhase::ListRemote), u"list-remote"_s);
}

void TestSyncHistory::capsErrorsAndConflicts()
{
    SyncReport r = changed(1);
    for (int i = 0; i < 50; ++i) {
        r.errors.append(SyncFileError{u"błąd %1.md"_s.arg(i), u"HTTP 500"_s, u"server"_s, 500});
        r.conflicts.append(u"konflikt %1.md"_s.arg(i));
    }
    const SyncRunRecord rec = SyncRunRecord::fromReport(r);
    QCOMPARE(rec.errors.size(), SyncRunRecord::MaxErrors);
    QCOMPARE(rec.conflictPaths.size(), SyncRunRecord::MaxConflicts);
    QCOMPARE(rec.fileErrors, 50);
    QCOMPARE(rec.conflicts, 50);
    QCOMPARE(rec.errors.first().path, u"błąd 0.md"_s);
    // The JSON keeps the caps too.
    QJsonObject json = rec.toJson();
    QJsonArray many;
    for (int i = 0; i < 40; ++i)
        many.append(QJsonObject{{u"path"_s, u"p%1"_s.arg(i)}});
    json.insert(u"errors"_s, many);
    QCOMPARE(SyncRunRecord::fromJson(json)->errors.size(), SyncRunRecord::MaxErrors);
}

void TestSyncHistory::jsonRoundTrip()
{
    SyncReport r = changed(17);
    r.errors = {SyncFileError{u"Zażółć/gęślą jaźń.md"_s, u"PUT failed: HTTP 423"_s, u"locked"_s, 423},
                SyncFileError{u"b.md"_s, u"Cannot read b.md"_s, u"read-failed"_s, 0}};
    r.conflicts = {u"Plan.md"_s};
    r.skipped = {u"film.mp4"_s};
    r.stats.timeouts = 1;
    r.stats.preconditionRetries = 2;
    r.stats.longestStallMs = 48'000;
    r.foldersCreated = 2;
    r.foldersMoved = 1;
    r.foldersMovedHere = 2;
    r.filesMoved = 3;
    r.filesMovedHere = 4;
    SyncRunRecord rec = SyncRunRecord::fromReport(r);
    QCOMPARE(rec.foldersCreated, 2);
    QCOMPARE(rec.foldersDeleted, 0);
    QCOMPARE(rec.foldersMoved, 1);
    QCOMPARE(rec.foldersMovedHere, 2);
    QCOMPARE(rec.filesMoved, 3);
    QCOMPARE(rec.filesMovedHere, 4);
    // up, down, the conflict, the folders, the moves
    QCOMPARE(rec.changeCount(), 3 + 1 + 1 + 2 + 1 + 2 + 3 + 4);
    rec.repeats = 3;
    rec.firstStarted = kStart;
    const QJsonObject json = rec.toJson();
    // Through text, as on disk.
    const QJsonObject reread = QJsonDocument::fromJson(QJsonDocument(json).toJson()).object();
    const std::optional<SyncRunRecord> back = SyncRunRecord::fromJson(reread);
    QVERIFY(back);
    QCOMPARE(*back, rec);
    QCOMPARE(back->started, r.started);
    QCOMPARE(back->stats.requestsByMethod.value("PROPFIND"), 41);
    QCOMPARE(back->errors.at(0).code, u"locked"_s);
    QCOMPARE(back->skipped, 1);
    QCOMPARE(back->foldersCreated, 2);
    QCOMPARE(back->foldersMoved, 1);
    QCOMPARE(back->foldersMovedHere, 2);
    QCOMPARE(back->filesMoved, 3);
    QCOMPARE(back->filesMovedHere, 4);
    QVERIFY(!json.contains(u"foldersDel"_s)); // only counts that happened are written
    // Records of older versions have no move counts: 0.
    QJsonObject old = json;
    for (const QString &key : {u"foldersMovedHere"_s, u"filesMoved"_s, u"filesMovedHere"_s})
        old.remove(key);
    const std::optional<SyncRunRecord> older = SyncRunRecord::fromJson(old);
    QVERIFY(older);
    QCOMPARE(older->foldersMovedHere, 0);
    QCOMPARE(older->filesMoved, 0);
    QCOMPARE(older->filesMovedHere, 0);
    QVERIFY(!SyncRunRecord::fromReport(changed(18)).toJson().contains(u"filesMovedHere"_s));
}

void TestSyncHistory::malformedRecordsAreSkipped()
{
    QCOMPARE(SyncRunRecord::fromJson({}), std::nullopt);
    QJsonObject good = SyncRunRecord::fromReport(report(3)).toJson();
    QVERIFY(SyncRunRecord::fromJson(good));
    QJsonObject noNumber = good;
    noNumber.remove(u"n"_s);
    QCOMPARE(SyncRunRecord::fromJson(noNumber), std::nullopt);
    QJsonObject badOutcome = good;
    badOutcome.insert(u"outcome"_s, u"świetnie"_s);
    QCOMPARE(SyncRunRecord::fromJson(badOutcome), std::nullopt);
    QJsonObject badTime = good;
    badTime.insert(u"started"_s, 42);
    QCOMPARE(SyncRunRecord::fromJson(badTime), std::nullopt);
    // Unknown trigger, broken phases and stats: kept with defaults.
    QJsonObject odd = good;
    odd.insert(u"trigger"_s, u"księżyc"_s);
    odd.insert(u"phases"_s, QJsonArray{QJsonObject{{u"p"_s, u"nieznana"_s}}, 7});
    odd.insert(u"stats"_s, u"zepsute"_s);
    const std::optional<SyncRunRecord> r = SyncRunRecord::fromJson(odd);
    QVERIFY(r);
    QCOMPARE(r->trigger, SyncTrigger::Unknown);
    QVERIFY(r->phases.isEmpty());
    QCOMPARE(r->stats, SyncStats());

    // A store with garbage among the runs.
    LocalStore store{QString()};
    store.set(u"runs"_s, QJsonArray{good, u"tekst"_s, 5, QJsonObject{}, noNumber});
    SyncHistory history(&store);
    QCOMPARE(history.runs().size(), 1);
    store.set(u"runs"_s, u"nie tablica"_s);
    QVERIFY(SyncHistory(&store).runs().isEmpty());
}

void TestSyncHistory::capacity()
{
    LocalStore store{QString()};
    SyncHistory history(&store);
    for (int i = 1; i <= 30; ++i)
        history.add(changed(i));
    QCOMPARE(history.runs().size(), SyncHistory::DefaultCapacity);
    QCOMPARE(history.runs().first().number, 30);
    QCOMPARE(history.runs().last().number, 11);
    QCOMPARE(store.get(u"runs"_s).toArray().size(), SyncHistory::DefaultCapacity);
    SyncHistory small(nullptr, 3);
    for (int i = 1; i <= 5; ++i)
        small.add(changed(i));
    QCOMPARE(small.runs().size(), 3);
    QCOMPARE(small.run(5)->number, 5);
    QCOMPARE(small.run(1), std::nullopt);
}

void TestSyncHistory::foldsQuietRuns()
{
    SyncHistory history(nullptr);
    QSignalSpy changedSpy(&history, &SyncHistory::changed);
    history.add(changed(1));
    SyncReport quiet2 = report(2);
    quiet2.durationMs = 900;
    history.add(quiet2);
    SyncReport quiet3 = report(3, SyncTrigger::Foreground);
    quiet3.durationMs = 2500;
    history.add(quiet3);
    history.add(report(4));
    QCOMPARE(changedSpy.size(), 4);
    QCOMPARE(history.runs().size(), 2);
    const SyncRunRecord folded = history.runs().first();
    QCOMPARE(folded.repeats, 3);
    QCOMPARE(folded.number, 4);
    QCOMPARE(folded.firstStarted, quiet2.started);
    QCOMPARE(folded.started, report(4).started);
    QCOMPARE(folded.durationMs, 2500);
    QCOMPARE(history.runs().at(1).number, 1);
    // A run with changes or a failure is never folded.
    SyncReport failed = report(5);
    failed.remoteError = WebDavError{WebDavErrorCode::Timeout, 0, u"PROPFIND timed out"_s, {}};
    history.add(failed);
    history.add(report(6));
    history.add(report(7));
    QCOMPARE(history.runs().size(), 4);
    QCOMPARE(history.runs().first().repeats, 2);
    QCOMPARE(history.runs().first().firstStarted, report(6).started);
    QCOMPARE(history.runs().at(1).number, 5);
    // Skipped files are not "quiet".
    SyncReport skipped = report(8);
    skipped.skipped = {u"duży.bin"_s};
    history.add(skipped);
    QCOMPARE(history.runs().first().repeats, 1);
    // Nor is a run that only moved or created folders.
    history.add(report(9));
    SyncReport folders = report(10);
    folders.foldersMoved = 1;
    folders.changes = {SyncChange{u"Archiwum/Projekty"_s, SyncAction::MoveRemote}};
    history.add(folders);
    QCOMPARE(history.runs().first().number, 10);
    QCOMPARE(history.runs().first().repeats, 1);
    QVERIFY(!history.runs().first().isQuiet());
    // Nor one that only renamed a folder or a page as the server had.
    for (int i = 0; i < 2; ++i) {
        history.add(report(11 + 2 * i));
        SyncReport here = report(12 + 2 * i);
        if (i == 0) {
            here.foldersMovedHere = 1;
            here.changes = {SyncChange{u"Archiwum/Projekty ąę"_s, SyncAction::MoveLocal}};
        } else {
            here.filesMovedHere = 1;
            here.changes = {SyncChange{u"Archiwum/Notatka ż.md"_s, SyncAction::MoveFileLocal}};
        }
        history.add(here);
        QCOMPARE(history.runs().first().number, 12 + 2 * i);
        QVERIFY(!history.runs().first().isQuiet());
    }
}

void TestSyncHistory::numbers()
{
    SyncHistory history(nullptr);
    QCOMPARE(history.nextNumber(), 1);
    history.add(changed(41));
    QCOMPARE(history.nextNumber(), 42);
    history.add(changed(7)); // out of order (an older engine): the highest number counts
    QCOMPARE(history.nextNumber(), 42);
    QVERIFY(history.run(41));
    QCOMPARE(history.run(100), std::nullopt);
}

void TestSyncHistory::persistsAndReloads()
{
    QTemporaryDir dir;
    const QString file = dir.filePath(u"sync-history.json"_s);
    {
        LocalStore store(file);
        SyncHistory history(&store);
        history.add(changed(1));
        SyncReport files = changed(2);
        files.errors = {SyncFileError{u"Żółw.md"_s, u"HTTP 507"_s, u"insufficient-storage"_s, 507}};
        history.add(files);
        history.add(report(3));
        QVERIFY(store.flush());
    }
    LocalStore store(file);
    SyncHistory history(&store);
    QCOMPARE(history.runs().size(), 3);
    QCOMPARE(history.runs().at(0).number, 3);
    QCOMPARE(history.runs().at(1).errors.first().code, u"insufficient-storage"_s);
    QCOMPARE(history.runs().at(1).outcome, SyncOutcome::FileErrors);
    QCOMPARE(history.nextNumber(), 4);
    QFile raw(file);
    QVERIFY(raw.open(QIODevice::ReadOnly));
    QVERIFY(raw.readAll().contains("\"runs\""));
}

void TestSyncHistory::redactsStoredMessages()
{
    const QString secret = u"Zażółć-Gęślą-Jaźń-42!"_s;
    SyncReport r = changed(9);
    r.remoteError = WebDavError{
            WebDavErrorCode::Network,
            0,
            u"PUT https://jan:%1@chmura.example.pl/x.md failed: Authorization: Basic amFuOnRham5l"_s.arg(secret),
            {}};
    r.errors = {SyncFileError{u"a.md"_s, u"GET https://jan:tajne@h/a.md?token=abc failed"_s, u"server"_s, 500}};
    const SyncRunRecord rec = SyncRunRecord::fromReport(r);
    const QString json = QString::fromUtf8(QJsonDocument(rec.toJson()).toJson());
    QVERIFY2(!json.contains(secret), qPrintable(json));
    QVERIFY2(!json.contains(u"tajne"_s), qPrintable(json));
    QVERIFY2(!json.contains(u"amFuOnRham5l"_s), qPrintable(json));
    QVERIFY2(!json.contains(u"token=abc"_s), qPrintable(json));
    QVERIFY(json.contains(u"https://***@chmura.example.pl/x.md"_s));
    // A secret registered later is masked when the record is written again.
    SyncRunRecord plain;
    plain.number = 1;
    plain.started = kStart;
    plain.errorMessage = u"hasło: kot-pies-123"_s;
    logging::addSecret(u"kot-pies-123"_s);
    QVERIFY(!QJsonDocument(plain.toJson()).toJson().contains("kot-pies-123"));
}

void TestSyncHistory::clearEmpties()
{
    LocalStore store{QString()};
    SyncHistory history(&store);
    history.add(changed(1));
    QSignalSpy spy(&history, &SyncHistory::changed);
    history.clear();
    QCOMPARE(spy.size(), 1);
    QVERIFY(history.runs().isEmpty());
    QVERIFY(store.get(u"runs"_s).toArray().isEmpty());
    QCOMPARE(history.nextNumber(), 1);
}

QTEST_GUILESS_MAIN(TestSyncHistory)
#include "tst_sync_history.moc"
