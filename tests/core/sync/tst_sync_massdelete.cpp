// Data safety of the sync engine: the mass-deletion guard (> 25 % and at least 10 files of a
// side, or every file of a side — like the Nextcloud client), confirming held-back deletions,
// and refusing to sync a vault whose folder is missing or on another storage.
#include "synctest.h"

#include <QSignalSpy>

using namespace synctest;

namespace {

QString noteName(int i)
{
    return u"Notatka %1 ąę.md"_s.arg(i, 3, 10, QLatin1Char('0'));
}

QMap<QString, QByteArray> notes(int count)
{
    QMap<QString, QByteArray> out;
    for (int i = 0; i < count; ++i)
        out.insert(noteName(i), u8(u"Treść notatki %1 — żółć\n"_s.arg(i)));
    return out;
}

QStringList pendingPaths(const SyncReport &report, std::optional<SyncAction> action = std::nullopt)
{
    QStringList out;
    for (const SyncChange &change : report.pendingDeletions) {
        if (!action || change.action == *action)
            out.append(change.path);
    }
    return out;
}

QStringList firstNames(int count)
{
    QStringList out;
    for (int i = 0; i < count; ++i)
        out.append(noteName(i));
    return out;
}

} // namespace

class TestSyncMassDelete : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void threshold_data()
    {
        QTest::addColumn<int>("files");
        QTest::addColumn<int>("deleted");
        QTest::addColumn<bool>("onServer"); // deleted on the server (else in the vault)
        QTest::addColumn<bool>("held");
        const struct {
            const char *name;
            int files;
            int deleted;
            bool held;
        } rows[] = {
                {"10 of 40 (exactly 25 %)", 40, 10, false},
                {"10 of 39 (over 25 %)", 39, 10, true},
                {"9 of 12 (fewer than 10)", 12, 9, false},
                {"10 of 12", 12, 10, true},
                {"25 of 100", 100, 25, false},
                {"26 of 100", 100, 26, true},
        };
        for (const auto &row : rows) {
            QTest::addRow("local: %s", row.name) << row.files << row.deleted << false << row.held;
            QTest::addRow("server: %s", row.name) << row.files << row.deleted << true << row.held;
        }
    }

    void threshold()
    {
        QFETCH(int, files);
        QFETCH(int, deleted);
        QFETCH(bool, onServer);
        QFETCH(bool, held);
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(files));
        QCOMPARE(counts(a.sync()), (Counts{.up = files}));
        for (int i = 0; i < deleted; ++i) {
            if (onServer)
                server.remove(kFolder + u'/' + noteName(i));
            else
                QVERIFY(a.remove(noteName(i)));
        }
        const SyncReport r = a.sync();
        QVERIFY(!r.failed());
        QCOMPARE(r.needsConfirmation, held);
        if (!held) {
            QVERIFY(r.pendingDeletions.isEmpty());
            QCOMPARE(counts(r), onServer ? Counts{.delL = deleted} : Counts{.delR = deleted});
            QCOMPARE(server.files(kFolder).size(), files - deleted);
            QCOMPARE(a.files().size(), files - deleted);
            return;
        }
        const SyncAction action = onServer ? SyncAction::DeleteLocal : SyncAction::DeleteRemote;
        QCOMPARE(pendingPaths(r, action), firstNames(deleted));
        QCOMPARE(pendingPaths(r), firstNames(deleted));
        QCOMPARE(counts(r), Counts{});
        // Nothing was deleted on the side that still has the files.
        if (onServer)
            QCOMPARE(a.files().size(), files);
        else
            QCOMPARE(server.files(kFolder).size(), files);
        // Confirmed: the deletions go through.
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        const SyncReport confirmed = a.sync(confirm);
        QVERIFY(!confirmed.needsConfirmation);
        QCOMPARE(counts(confirmed), onServer ? Counts{.delL = deleted} : Counts{.delR = deleted});
        QCOMPARE(server.files(kFolder).size(), files - deleted);
        QCOMPARE(a.files().size(), files - deleted);
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void otherChangesSyncWhileDeletionsWait()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(12));
        a.sync();
        for (int i = 0; i < 10; ++i)
            server.remove(kFolder + u'/' + noteName(i));
        QVERIFY(a.write(noteName(10), u"edytowana lokalnie\n"_s));
        QVERIFY(a.write(u"Nowa.md"_s, u"nowa\n"_s));
        server.putText(kFolder + u'/' + noteName(11), u"edytowana na serwerze\n"_s);
        const SyncReport r = a.sync();
        QVERIFY(r.needsConfirmation);
        QCOMPARE(pendingPaths(r, SyncAction::DeleteLocal), firstNames(10));
        QCOMPARE(counts(r), (Counts{.up = 2, .down = 1}));
        QCOMPARE(a.read(noteName(11)), u"edytowana na serwerze\n"_s);
        QCOMPARE(server.text(kFolder + u"/Nowa.md"_s), std::optional<QString>(u"nowa\n"_s));
        QCOMPARE(a.files().size(), 13);
        QVERIFY(a.engine->stateOf(noteName(0))); // still known: planned again next time

        // Not confirmed: the same question again, nothing else to do.
        const SyncReport again = a.sync();
        QCOMPARE(again.pendingDeletions, r.pendingDeletions);
        QCOMPARE(counts(again), Counts{});

        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        QCOMPARE(counts(a.sync(confirm)), (Counts{.delL = 10}));
        QCOMPARE(a.trash().size(), 10);
        QCOMPARE(counts(a.sync()), Counts{});
        QCOMPARE(a.files().keys(), server.files(kFolder).keys());
    }

    void restoredFilesAreNotDeletions()
    {
        // Ten local deletions, but two of those files were edited on the server meanwhile: they
        // are restored, so only eight are deleted — below the limit.
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(12));
        a.sync();
        for (int i = 0; i < 10; ++i)
            QVERIFY(a.remove(noteName(i)));
        server.putText(kFolder + u'/' + noteName(0), u"poprawiona\n"_s);
        server.putText(kFolder + u'/' + noteName(1), u"poprawiona\n"_s);
        const SyncReport r = a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), (Counts{.down = 2, .delR = 8}));
        QCOMPARE(a.read(noteName(0)), u"poprawiona\n"_s);
    }

    void locallyEditedFilesAreNotDeletions()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(12));
        a.sync();
        for (int i = 0; i < 10; ++i)
            server.remove(kFolder + u'/' + noteName(i));
        QVERIFY(a.write(noteName(0), u"poprawiona lokalnie\n"_s));
        const SyncReport r = a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), (Counts{.up = 1, .delL = 9}));
    }

    void deletingEveryFileOfASideNeedsConfirmation()
    {
        // Not caught by the empty-side guard (each side still has a file synced before), but
        // the vault would end up empty.
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}, {u"c.md"_s, "c\n"}});
        a.sync();
        QVERIFY(a.remove(u"c.md"_s));
        server.remove(u"Notatki/a.md"_s);
        server.remove(u"Notatki/b.md"_s);
        const SyncReport r = a.sync();
        QVERIFY(!r.failed());
        QVERIFY(r.needsConfirmation);
        QCOMPARE(r.pendingDeletions, (QList<SyncChange>{{u"a.md"_s, SyncAction::DeleteLocal},
                                                        {u"b.md"_s, SyncAction::DeleteLocal},
                                                        {u"c.md"_s, SyncAction::DeleteRemote}}));
        QCOMPARE(a.files().keys(), (QStringList{u"a.md"_s, u"b.md"_s}));
        QCOMPARE(server.files(kFolder).keys(), QStringList{u"c.md"_s});
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        QCOMPARE(counts(a.sync(confirm)), (Counts{.delL = 2, .delR = 1}));
        QVERIFY(a.files().isEmpty());
        QVERIFY(server.files(kFolder).isEmpty());
    }

    void theVaultConfigDoesNotCount()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, {{u"a.md"_s, "a\n"}, {u"b.md"_s, "b\n"}, {u".brulion/config.json"_s, "{}\n"}});
        a.sync();
        // One of two notes (plus the config) deleted locally: neither "every file" nor > 25 % of 10.
        QVERIFY(a.remove(u"a.md"_s));
        QVERIFY(QFile::remove(a.root + u"/.brulion/config.json"_s));
        a.vault->rescan();
        const SyncReport r = a.sync();
        QVERIFY(!r.needsConfirmation);
        QCOMPARE(counts(r), (Counts{.delR = 2}));
    }

    void bothDevicesConvergeAfterConfirmation()
    {
        TestClock clock;
        FakeWebDavServer server(serverOptions(clock));
        DeviceOptions options;
        options.clock = &clock;
        Device a(server, u"laptop"_s, notes(15), options);
        Device b(server, u"telefon"_s, {}, options);
        a.sync();
        QCOMPARE(counts(b.sync()), (Counts{.down = 15}));
        // The phone deletes a whole batch: its own run uploads the deletions only after asking.
        for (int i = 0; i < 12; ++i)
            QVERIFY(b.remove(noteName(i)));
        QVERIFY(b.sync().needsConfirmation);
        QCOMPARE(server.files(kFolder).size(), 15);
        SyncRunOptions confirm;
        confirm.confirmDeletions = true;
        QCOMPARE(counts(b.sync(confirm)), (Counts{.delR = 12}));
        // The laptop sees 12 of 15 files gone on the server: it asks too before trashing them.
        const SyncReport onA = a.sync();
        QVERIFY(onA.needsConfirmation);
        QCOMPARE(pendingPaths(onA, SyncAction::DeleteLocal), firstNames(12));
        QCOMPARE(a.files().size(), 15);
        QCOMPARE(counts(a.sync(confirm)), (Counts{.delL = 12}));
        QCOMPARE(a.files(), b.files());
        QCOMPARE(a.trash().size(), 12);
    }

    void refusesAMissingVaultFolder()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(3));
        a.sync();
        QVERIFY(QDir().rename(a.root, a.root + u"-karta"_s));
        const SyncReport r = a.sync();
        QVERIFY(r.failed());
        QVERIFY(r.syncError.is(SyncErrorCode::VaultUnavailable));
        QCOMPARE(r.failureCode(), u"vault-unavailable"_s);
        QCOMPARE(server.files(kFolder).size(), 3);
        QVERIFY(QDir().rename(a.root + u"-karta"_s, a.root));
        QCOMPARE(counts(a.sync()), Counts{});
    }

    void refusesAVaultOnAnotherStorage()
    {
        FakeWebDavServer server;
        Device a(server, u"laptop"_s, notes(3));
        QString storage = u"karta"_s;
        a.vault->setStorageProbe([&storage](const QString &) { return storage; });
        QVERIFY(a.vault->open());
        a.sync();
        server.putText(kFolder + u"/z serwera.md"_s, u"nowa\n"_s);
        storage = u"pamięć wewnętrzna"_s; // an empty mount point
        QVERIFY(a.sync().syncError.is(SyncErrorCode::VaultUnavailable));
        QVERIFY(!QFileInfo::exists(a.root + u"/z serwera.md"_s));
        storage = u"karta"_s;
        QCOMPARE(counts(a.sync()), (Counts{.down = 1}));
    }
};

QTEST_GUILESS_MAIN(TestSyncMassDelete)
#include "tst_sync_massdelete.moc"
