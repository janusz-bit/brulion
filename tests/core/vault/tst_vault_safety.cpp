// Data safety of scans: what a scan cannot see (a missing vault folder, an empty mount point, a
// folder without permissions, a linked folder whose target is unmounted) must never be reported
// as deleted — it is reported through Vault::scanStatus() instead.
#include "vaulttest.h"

#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace vaulttest;

namespace {

/// Restores permissions on destruction so QTemporaryDir can clean up after a failed check.
class PermissionGuard {
public:
    explicit PermissionGuard(QString path) : m_path(std::move(path)), m_saved(QFile::permissions(m_path)) { }
    ~PermissionGuard() { restore(); }
    PermissionGuard(const PermissionGuard &) = delete;
    PermissionGuard &operator=(const PermissionGuard &) = delete;

    bool set(QFileDevice::Permissions permissions) { return QFile::setPermissions(m_path, permissions); }
    void restore() { QFile::setPermissions(m_path, m_saved); }

private:
    QString m_path;
    QFileDevice::Permissions m_saved;
};

/// Permission checks do not apply to root (e.g. some CI containers).
bool permissionsEnforced(const QString &dir)
{
    return !QFileInfo(dir).isReadable() || !QFileInfo(dir).isExecutable();
}

using State = VaultScanStatus::State;

} // namespace

class TestVaultSafety : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void aFreshVaultIsOk()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(f.vault->scanStatus().isOk());
        QVERIFY(f.vault->scanStatus().unreadableFolders.isEmpty());
        QVERIFY(f.vault->isStorageAvailable());
        Vault closed(f.root());
        QVERIFY(!closed.isStorageAvailable());
    }

    void aMissingVaultFolderIsAnErrorState()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"Projekty/b.md"_s, "b");
        QVERIFY(f.open());
        QSignalSpy statusSpy(f.vault.get(), &Vault::scanStatusChanged);
        QVERIFY(QDir().rename(f.root(), f.root() + u"-odmontowany"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"vault folder is missing"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.vault->scanStatus().state, State::RootMissing);
        QVERIFY(f.vault->scanStatus().message.contains(f.root()));
        QVERIFY(!f.vault->isStorageAvailable());
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(f.files(), (QStringList{u"Projekty/b.md"_s, u"a.md"_s}));
        // Writes fail instead of creating a new, empty vault folder.
        QVERIFY(!f.vault->writeText(u"Projekty/c.md"_s, u"c"_s));
        QVERIFY(f.vault->errorString().contains(u"missing"_s));
        QVERIFY(!QFileInfo::exists(f.root()));

        QVERIFY(QDir().rename(f.root() + u"-odmontowany"_s, f.root()));
        QVERIFY(f.vault->isStorageAvailable());
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(statusSpy.count(), 2);
        QCOMPARE(f.spy->count(), 0);
    }

    void anEmptyMountPointIsNotAMassDeletion()
    {
        Fixture f;
        QString storage = u"/dev/mmcblk1p1|/media/karta"_s;
        f.storageProbe = [&storage](const QString &) { return storage; };
        f.write(u"a.md"_s, "a");
        f.write(u"Dziennik/2026-10-14.md"_s, "- wpis");
        QVERIFY(f.open());
        QVERIFY(f.vault->scanStatus().isOk());

        // The card is unmounted: the vault folder is now an empty folder on the phone's storage.
        const QString hidden = f.tmp.path() + u"/zawartość-karty"_s;
        QVERIFY(QDir().rename(f.root(), hidden));
        QVERIFY(QDir().mkpath(f.root()));
        storage = u"/dev/root|/"_s;
        QVERIFY(!f.vault->isStorageAvailable());
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"another storage"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.vault->scanStatus().state, State::StorageChanged);
        QVERIFY(f.vault->scanStatus().message.contains(u"/dev/mmcblk1p1"_s));
        QCOMPARE(f.files(), (QStringList{u"Dziennik/2026-10-14.md"_s, u"a.md"_s}));
        QCOMPARE(f.folders(), QStringList{u"Dziennik"_s});
        // Nothing is written onto the empty mount point (it would be hidden by the card later).
        QVERIFY(!f.vault->writeText(u"Dziennik/2026-10-14.md"_s, u"- wpis po odmontowaniu"_s));
        QVERIFY(f.vault->errorString().contains(u"another storage"_s));
        QVERIFY(f.vault->create(u"Nowa.md"_s, u"x"_s).isEmpty());
        QVERIFY(!f.vault->createFolder(u"Nowy folder"_s));
        QVERIFY(diskFiles(f.root()).isEmpty());
        QVERIFY(QDir(f.root()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());

        // Mounted again: nothing happened.
        QVERIFY(QDir(f.root()).removeRecursively());
        QVERIFY(QDir().rename(hidden, f.root()));
        storage = u"/dev/mmcblk1p1|/media/karta"_s;
        QVERIFY(f.vault->isStorageAvailable());
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(f.spy->count(), 0);
        QCOMPARE(f.text(u"a.md"_s), u"a"_s);
        QVERIFY(f.vault->writeText(u"Dziennik/2026-10-14.md"_s, u"- wpis po zamontowaniu"_s));
    }

    void watcherBatchesOnAnEmptyMountPointChangeNothing()
    {
        Fixture f;
        QString storage = u"karta"_s;
        f.storageProbe = [&storage](const QString &) { return storage; };
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        QVERIFY(f.open());
        f.vault->setWatchDelay(0);
        f.vault->startWatching();
        storage = u"wewnętrzna"_s;
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"another storage"_s));
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QVERIFY(removeBehind(f.root(), u"b.md"_s));
        QTRY_COMPARE(f.vault->scanStatus().state, State::StorageChanged);
        QCOMPARE(f.spy->count(), 0);
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"b.md"_s}));
        // Back on the right storage the deletions were real after all: now they are reported.
        storage = u"karta"_s;
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"delete:a.md@external"_s, u"delete:b.md@external"_s}));
        QVERIFY(f.vault->scanStatus().isOk());
    }

    void anotherStorageWithoutVanishingFilesIsHarmless()
    {
        Fixture f;
        QString storage = u"pierwszy"_s;
        f.storageProbe = [&storage](const QString &) { return storage; };
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        storage = u"drugi"_s;
        f.write(u"nowa.md"_s, "nowa");
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"create:nowa.md@external"_s});
        QVERIFY(f.vault->scanStatus().isOk());
        QVERIFY(!f.vault->isStorageAvailable());
        // Opening again adopts the new storage.
        QVERIFY(f.open());
        QVERIFY(f.vault->isStorageAvailable());
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"delete:a.md@external"_s});
    }

    void anUnknownStorageIdentityIsNeverCompared()
    {
        Fixture f;
        QString storage;
        f.storageProbe = [&storage](const QString &) { return storage; };
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        storage = u"teraz-znany"_s;
        QVERIFY(f.vault->isStorageAvailable());
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"delete:a.md@external"_s});
    }

    void anUnreadableFolderKeepsItsFiles_data()
    {
        QTest::addColumn<int>("permissions");
        QTest::newRow("no permissions") << 0;
        QTest::newRow("read without search") << int(QFileDevice::ReadOwner | QFileDevice::ReadUser);
        QTest::newRow("search without read") << int(QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }

    void anUnreadableFolderKeepsItsFiles()
    {
        BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS();
        QFETCH(int, permissions);
        Fixture f;
        f.write(u"c.md"_s, "c");
        f.write(u"Projekty/Dom.md"_s, "dom");
        f.write(u"Projekty/Ogród/Grządki.md"_s, "grządki");
        QVERIFY(f.open());
        QSignalSpy statusSpy(f.vault.get(), &Vault::scanStatusChanged);
        PermissionGuard guard(f.root() + u"/Projekty"_s);
        QVERIFY(guard.set(QFileDevice::Permissions(permissions)));
        if (!permissionsEnforced(f.root() + u"/Projekty"_s)) {
            guard.restore();
            QSKIP("file permissions are not enforced for this user");
        }
        QVERIFY(removeBehind(f.root(), u"c.md"_s));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"could not be read"_s));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"delete:c.md@external"_s});
        const VaultScanStatus status = f.vault->scanStatus();
        QCOMPARE(status.state, State::Unreadable);
        QCOMPARE(status.unreadableFolders, QStringList{u"Projekty"_s});
        QVERIFY(status.message.contains(u"Projekty"_s));
        QCOMPARE(statusSpy.count(), 1);
        QCOMPARE(f.files(), (QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród/Grządki.md"_s}));
        QCOMPARE(f.folders(), (QStringList{u"Projekty"_s, u"Projekty/Ogród"_s}));
        // Still unreadable: the same status, no signal, no changes.
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(statusSpy.count(), 1);

        guard.restore();
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(statusSpy.count(), 2);
        QCOMPARE(f.text(u"Projekty/Ogród/Grządki.md"_s), u"grządki"_s);
    }

    void anUnreadableVaultFolderChangesNothing()
    {
        BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS();
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b/c.md"_s, "c");
        QVERIFY(f.open());
        PermissionGuard guard(f.root());
        QVERIFY(guard.set({}));
        if (!permissionsEnforced(f.root())) {
            guard.restore();
            QSKIP("file permissions are not enforced for this user");
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"could not be read"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.vault->scanStatus().state, State::Unreadable);
        QCOMPARE(f.vault->scanStatus().unreadableFolders, QStringList{QString()});
        QVERIFY(f.vault->scanStatus().message.contains(u"(vault folder)"_s));
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"b/c.md"_s}));
        guard.restore();
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->scanStatus().isOk());
    }

    void openReportsUnreadableFolders()
    {
        BRULION_SKIP_WITHOUT_UNIX_PERMISSIONS();
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"Tajne/b.md"_s, "b");
        PermissionGuard guard(f.root() + u"/Tajne"_s);
        QVERIFY(guard.set({}));
        if (!permissionsEnforced(f.root() + u"/Tajne"_s)) {
            guard.restore();
            QSKIP("file permissions are not enforced for this user");
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"could not be read"_s));
        QVERIFY(f.open());
        QCOMPARE(f.vault->scanStatus().state, State::Unreadable);
        QCOMPARE(f.vault->scanStatus().unreadableFolders, QStringList{u"Tajne"_s});
        QCOMPARE(f.files(), QStringList{u"a.md"_s});
        guard.restore();
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"create:Tajne/b.md@external"_s});
        QVERIFY(f.vault->scanStatus().isOk());
    }

    void aLinkedFolderWithAnUnmountedTargetKeepsItsFiles()
    {
        BRULION_SKIP_WITHOUT_SYMLINKS();
        Fixture f;
        const QString card = f.tmp.path() + u"/karta/Zdjęcia"_s;
        writeBehind(card, u"zdjęcie.png"_s, "PNG");
        writeBehind(card, u"opis.md"_s, "opis");
        f.write(u"a.md"_s, "a");
        QVERIFY(QFile::link(card, f.root() + u"/assets"_s));
        QVERIFY(f.open());
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"assets/opis.md"_s, u"assets/zdjęcie.png"_s}));

        const QString away = f.tmp.path() + u"/odmontowana"_s;
        QVERIFY(QDir().rename(f.tmp.path() + u"/karta"_s, away));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"could not be read"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.vault->scanStatus().state, State::Unreadable);
        QCOMPARE(f.vault->scanStatus().unreadableFolders, QStringList{u"assets"_s});
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"assets/opis.md"_s, u"assets/zdjęcie.png"_s}));

        QVERIFY(QDir().rename(away, f.tmp.path() + u"/karta"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(f.spy->count(), 0);
    }

    void realDeletionsAreStillReported()
    {
        Fixture f;
        for (int i = 0; i < 20; ++i)
            f.write(u"Notatka %1.md"_s.arg(i), "treść");
        f.write(u"Folder/x.md"_s, "x");
        QVERIFY(f.open());
        for (int i = 0; i < 20; ++i)
            QVERIFY(removeBehind(f.root(), u"Notatka %1.md"_s.arg(i)));
        QVERIFY(removeBehind(f.root(), u"Folder"_s));
        QCOMPARE(f.vault->rescan().size(), 21);
        QVERIFY(f.vault->scanStatus().isOk());
        QVERIFY(f.files().isEmpty());
        QVERIFY(f.folders().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestVaultSafety)
#include "tst_vault_safety.moc"
