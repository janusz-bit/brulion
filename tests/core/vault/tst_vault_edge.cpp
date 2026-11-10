// Edge cases found in review: ids of folders that do not exist yet, huge text files, changes
// hidden behind an unchanged stat (coarse timestamps), History of non-UTF-8 files, links to
// files (never followed) and through the trash, byte order marks of unseen files, trash entries
// with NFD names written by other devices, watching requested before open().
#include "vaulttest.h"

#include <QRegularExpression>

using namespace vaulttest;

namespace {

/// "Zażółć gęślą jaźń\r\n" in Windows-1250 (Polish text of old Windows editors): not UTF-8.
QByteArray cp1250Sample()
{
    return QByteArray("Za\xBF\xF3\xB3\xE6 g\xEA\x9Cl\xB9 ja\x9F\xF1\r\n");
}

/// About 17 MB of CSV text: above the size up to which the vault reads files while scanning.
QByteArray hugeCsv()
{
    const QByteArray row("data;kwota;opis zażółć\n");
    QByteArray out;
    out.reserve(17 * 1024 * 1024 + row.size());
    while (out.size() < 17 * 1024 * 1024)
        out += row;
    return out;
}

} // namespace

class TestVaultEdge : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- ids -----------------------------------------------------------------------------
    void idOfAFolderThatDoesNotExistYet()
    {
        // The app names the device state folder by Vault::idForPath() before the first open()
        // creates the vault folder; a symlinked parent (/sdcard -> /storage/emulated/0) must
        // not change the id once the folder exists, or History and LocalStore are orphaned.
        BRULION_SKIP_WITHOUT_SYMLINKS();
        QTemporaryDir tmp;
        QVERIFY(QDir().mkpath(tmp.path() + u"/prawdziwy"_s));
        QVERIFY(QFile::link(tmp.path() + u"/prawdziwy"_s, tmp.path() + u"/skrót"_s));
        const QString root = tmp.path() + u"/skrót/Notatki/Główne"_s;
        const QString before = Vault::idForPath(root);
        Vault vault(root);
        QCOMPARE(vault.id(), before);
        QVERIFY(vault.open());
        QVERIFY(QFileInfo(tmp.path() + u"/prawdziwy/Notatki/Główne"_s).isDir());
        QCOMPARE(vault.id(), before);
        QCOMPARE(Vault::idForPath(root), before);
        QCOMPARE(Vault::idForPath(tmp.path() + u"/prawdziwy/Notatki/Główne/"_s), before);
        QVERIFY(Vault::idForPath(tmp.path() + u"/prawdziwy/Notatki"_s) != before);
    }

    // ---- huge files ------------------------------------------------------------------------
    void hugeTextFilesAreComparedByStatOnly()
    {
        // Scans never read text files above 16 MB (a CSV export, a log): they are compared by
        // size and modification time like binary files, so a touch counts as a modification.
        Fixture f;
        QVERIFY(f.open());
        const QByteArray csv = hugeCsv();
        QVERIFY(f.vault->writeBinary(u"eksport.csv"_s, csv));
        touchBehind(f.root(), u"eksport.csv"_s);
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:eksport.csv@external"_s});
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->writeBinary(u"eksport.csv"_s, csv + "koniec\n"));
        QVERIFY(f.vault->history()->list(u"eksport.csv"_s).isEmpty()); // never kept in History
        QCOMPARE(f.read(u"eksport.csv"_s).size(), csv.size() + 7);
    }

    void aSmallTextFileGrowingHugeKeepsItsLastContent()
    {
        Fixture f;
        f.write(u"rośnie.csv"_s, "a;b\n");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"rośnie.csv"_s), u"a;b\n"_s);
        f.write(u"rośnie.csv"_s, hugeCsv());
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:rośnie.csv@external"_s});
        QCOMPARE(f.historyContents(u"rośnie.csv"_s), QStringList{u"a;b\n"_s});
        touchBehind(f.root(), u"rośnie.csv"_s);
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"modify:rośnie.csv@external"_s});
        QCOMPARE(f.historyContents(u"rośnie.csv"_s), QStringList{u"a;b\n"_s});
    }

    void filesOverAGigabyteAreNotRead()
    {
        // readText(), readBinary() and readDetached() hold the whole file in memory: one over
        // 1 GiB fails without being read (sparse here: nothing is written); scans compare it by
        // its stat, like every file over 16 MB.
        Fixture f;
        QVERIFY(f.open());
        {
            QFile file(f.root() + u"/nagranie z próby.txt"_s);
            QVERIFY(file.open(QIODevice::WriteOnly));
            if (!file.resize((qint64(1) << 30) + 1))
                QSKIP("the file system cannot make a 1 GiB file here");
        }
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"create:nagranie z próby.txt@external"_s});
        bool ok = true;
        QVERIFY(f.vault->readBinary(u"nagranie z próby.txt"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(f.vault->errorString().contains(u"1024 MB"_s));
        QVERIFY(f.vault->readText(u"nagranie z próby.txt"_s, &ok).isEmpty());
        QVERIFY(!ok);
        const DetachedRead read =
                Vault::readDetached(u"nagranie z próby.txt"_s, f.vault->absolutePath(u"nagranie z próby.txt"_s));
        QVERIFY(!read.ok);
        QVERIFY(read.bytes.isEmpty());
        QVERIFY(f.vault->rescan().isEmpty());
    }

    // ---- changes hidden behind an unchanged stat ---------------------------------------------
    void readingAChangeWithAnUnchangedStat()
    {
        // Coarse timestamps (FAT: 2 s, SMB mounts) let another program change a note without
        // changing its size or modification time. A read that sees other content than the
        // vault knows must report it (and keep the old content), not swallow it.
        Fixture f;
        f.write(u"a.md"_s, "wersja A");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"wersja A"_s);
        f.write(u"a.md"_s, "wersja B", f.vault->stat(u"a.md"_s)->mtime);
        QCOMPARE(f.text(u"a.md"_s), u"wersja B"_s);
        QCOMPARE(f.spy->count(), 0); // never synchronously from a read
        f.vault->processPendingEvents();
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@external"_s});
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"wersja A"_s});
        QCOMPARE(f.historyReasons(u"a.md"_s), QStringList{u"external"_s});
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void writingOverAChangeWithAnUnchangedStat()
    {
        Fixture f;
        f.write(u"a.md"_s, "wersja A");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"wersja A"_s);
        f.write(u"a.md"_s, "wersja B", f.vault->stat(u"a.md"_s)->mtime);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"moja wersja C"_s));
        QCOMPARE(f.read(u"a.md"_s), "moja wersja C"_ba);
        QCOMPARE(f.changes(), (QStringList{u"modify:a.md@external"_s, u"modify:a.md@user"_s}));
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"wersja B"_s, u"wersja A"_s}));
        QCOMPARE(f.historyReasons(u"a.md"_s), (QStringList{u"user"_s, u"external"_s}));
    }

    void writingWhatAHiddenChangeAlreadyWrote()
    {
        Fixture f;
        f.write(u"a.md"_s, "wersja A");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"wersja A"_s);
        const qint64 mtime = f.vault->stat(u"a.md"_s)->mtime;
        f.write(u"a.md"_s, "wersja B", mtime);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"wersja B"_s)); // already on disk: no write
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@external"_s});
        QCOMPARE(f.vault->stat(u"a.md"_s)->mtime, mtime);
    }

    // ---- History keeps bytes -----------------------------------------------------------------
    void historyKeepsTheBytesOfNonUtf8Files()
    {
        // A note in Windows-1250 is shown with replacement characters; whatever replaces it,
        // History must keep the original bytes so the Polish letters can be recovered.
        Fixture f;
        f.write(u"stare/Zakupy.txt"_s, cp1250Sample());
        QVERIFY(f.open());
        QVERIFY(f.text(u"stare/Zakupy.txt"_s).contains(QChar(QChar::ReplacementCharacter)));
        QVERIFY(f.vault->writeText(u"stare/Zakupy.txt"_s, u"Zażółć gęślą jaźń\r\n"_s));
        const QList<HistoryEntry> entries = f.vault->history()->list(u"stare/Zakupy.txt"_s);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(entries.first().size, cp1250Sample().size());
        bool ok = false;
        QCOMPARE(f.vault->history()->readBytes(entries.first().id, &ok), cp1250Sample());
        QVERIFY(ok);
    }

    void historyKeepsTheByteOrderMark()
    {
        Fixture f;
        f.write(u"bom.md"_s, "\xEF\xBB\xBFstara\r\n");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeBinary(u"bom.md"_s, "nowa\r\n"_ba, ChangeSource::Sync));
        const QList<HistoryEntry> entries = f.vault->history()->list(u"bom.md"_s);
        QCOMPARE(entries.size(), 1);
        QCOMPARE(f.vault->history()->readBytes(entries.first().id), "\xEF\xBB\xBFstara\r\n"_ba);
        QCOMPARE(f.vault->history()->read(entries.first().id), u"stara\r\n"_s); // like readText()
    }

    // ---- symlinks --------------------------------------------------------------------------
    void writesNeverFollowDanglingLinksOutOfTheVault()
    {
        // A dangling link (planted by a sync peer, left by a removed drive) is not listed, so a
        // write to its name must not create a file wherever the link points.
        BRULION_SKIP_WITHOUT_SYMLINKS();
        Fixture f;
        QVERIFY(f.open());
        const QString outside = f.tmp.path() + u"/poza/sekret.md"_s;
        QVERIFY(QDir().mkpath(f.tmp.path() + u"/poza"_s));
        QVERIFY(QFile::link(outside, f.root() + u"/notatka.md"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(!f.vault->writeText(u"notatka.md"_s, u"tajne"_s));
        QVERIFY(f.vault->errorString().contains(u"notatka.md"_s));
        QVERIFY(!f.vault->writeBinary(u"notatka.md"_s, "tajne"_ba));
        QVERIFY(!QFileInfo::exists(outside));
        QCOMPARE(f.vault->create(u"notatka.md"_s, u"nowa"_s), u"notatka 2.md"_s);
        QVERIFY(!QFileInfo::exists(outside));
        QCOMPARE(f.changes(), QStringList{u"create:notatka 2.md@user"_s});
    }

    void fileLinksAreNeverFollowed_data()
    {
        QTest::addColumn<bool>("intoTheVault");
        QTest::newRow("outside the vault") << false; // "klucz.md -> ~/.ssh/id_rsa"
        QTest::newRow("into the vault") << true;     // a duplicate of a note
    }

    void fileLinksAreNeverFollowed()
    {
        // A link to a file (a sync peer's — Syncthing syncs symlinks — or a checkout's) is not
        // followed, wherever it points: it would be indexed, uploaded by sync and written through.
        // Not even inside a linked folder that is followed (the device allows it here).
        BRULION_SKIP_WITHOUT_SYMLINKS();
        QFETCH(bool, intoTheVault);
        Fixture f;
        f.followOutsideLinks = true;
        f.write(u"Notatka.md"_s, "moja notatka");
        f.write(u"zastąpiona.md"_s, "stara treść");
        f.write(u".trash/usunięta.md"_s, "usunięta");
        const QString outside = f.tmp.path() + u"/poza"_s;
        writeBehind(outside, u"id_rsa"_s, "tajny klucz zażółć");
        writeBehind(outside, u"Zdjęcia/lato.png"_s, "\x01");
        const QFileInfo target(intoTheVault ? QString(f.root() + u"/Notatka.md"_s) : QString(outside + u"/id_rsa"_s));
        const QByteArray secret = readBehind(target.absolutePath(), target.fileName());
        const QStringList links{f.root() + u"/klucz.md"_s, f.root() + u"/.trash/klucz.md"_s,
                                outside + u"/Zdjęcia/klucz.md"_s};
        for (const QString &link : links)
            QVERIFY(QFile::link(target.filePath(), link));
        QVERIFY(QFile::link(outside + u"/Zdjęcia"_s, f.root() + u"/assets"_s));
        QVERIFY(f.open());
        QCOMPARE(f.files(), (QStringList{u"Notatka.md"_s, u"assets/lato.png"_s, u"zastąpiona.md"_s}));
        QCOMPARE(statPaths(f.vault->trash()), QStringList{u".trash/usunięta.md"_s});
        for (const QString &link : {u"klucz.md"_s, u"assets/klucz.md"_s, u".trash/klucz.md"_s}) {
            QVERIFY2(!f.vault->exists(link) && !f.vault->stat(link), qPrintable(link));
            bool ok = true;
            QVERIFY(f.vault->readText(link, &ok).isEmpty());
            QVERIFY(!ok);
            QVERIFY(f.vault->errorString().contains(link));
            QVERIFY(f.vault->readBinary(link, &ok).isEmpty());
            QVERIFY(!ok);
            QVERIFY(!Vault::readDetached(link, f.vault->absolutePath(link)).ok);
            QVERIFY(!f.vault->writeText(link, u"nadpisane"_s));
            QVERIFY(!f.vault->writeBinary(link, "nadpisane"_ba));
            QCOMPARE(f.vault->writeTextIfUnchanged(link, u"nadpisane"_s, QString::fromUtf8(secret)),
                     WriteOutcome::Failed);
        }
        QCOMPARE(readBehind(target.absolutePath(), target.fileName()), secret);
        QVERIFY(f.vault->rescan().isEmpty());
        // A note replaced by such a link behind the vault's back is gone (kept in History), and
        // its folder is no gap in the listing.
        QCOMPARE(f.text(u"zastąpiona.md"_s), u"stara treść"_s);
        QVERIFY(removeBehind(f.root(), u"zastąpiona.md"_s));
        QVERIFY(QFile::link(target.filePath(), f.root() + u"/zastąpiona.md"_s));
        QCOMPARE(shortChanges(f.vault->rescan()), QStringList{u"delete:zastąpiona.md@external"_s});
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(f.historyContents(u"zastąpiona.md"_s), QStringList{u"stara treść"_s});
        QVERIFY(f.vault->rescan().isEmpty());
        QCOMPARE(f.changes(), QStringList{u"delete:zastąpiona.md@external"_s});
        QCOMPARE(readBehind(target.absolutePath(), target.fileName()), secret);
    }

    void outsideFolderLinksOnlyWhereTheDeviceAllowsThem()
    {
        // A synced "Dokumenty -> ~/Documents" (Syncthing syncs symlinks) must not make another
        // folder's files notes — listed, indexed, uploaded by sync, written or deleted through the
        // link: a folder link pointing outside is followed only when the device allows it.
        BRULION_SKIP_WITHOUT_SYMLINKS();
        Fixture f;
        f.write(u"a.md"_s, "notatka");
        const QString outside = f.tmp.path() + u"/Dokumenty"_s;
        writeBehind(outside, u"plan.md"_s, "tajny plan zażółć");
        writeBehind(outside, u"Podatki/zeznanie.pdf"_s, "%PDF");
        const QStringList theirs{u"Podatki/zeznanie.pdf"_s, u"plan.md"_s};
        QVERIFY(QFile::link(outside, f.root() + u"/Dokumenty"_s));
        QVERIFY(f.open());
        QVERIFY(!f.vault->followsOutsideLinks());
        QVERIFY(f.vault->skippedOutsideLinks()); // (the app's one-time check of earlier users)
        QCOMPARE(f.files(), QStringList{u"a.md"_s});
        QVERIFY(f.folders().isEmpty());
        QVERIFY(!f.vault->reaches(u"Dokumenty"_s) && !f.vault->reaches(u"Dokumenty/plan.md"_s));
        QVERIFY(f.vault->reaches(u"a.md"_s) && f.vault->reaches(u"nowa.md"_s));
        bool ok = true;
        QVERIFY(f.vault->readText(u"Dokumenty/plan.md"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(f.vault->errorString().contains(u"Dokumenty/plan.md"_s));
        QVERIFY(f.vault->readBinary(u"Dokumenty/Podatki/zeznanie.pdf"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!f.vault->writeText(u"Dokumenty/plan.md"_s, u"nadpisany"_s));
        QVERIFY(!f.vault->writeText(u"Dokumenty/nowy.md"_s, u"nowy"_s));
        QCOMPARE(f.vault->writeTextIfUnchanged(u"Dokumenty/plan.md"_s, u"nadpisany"_s, u"tajny plan zażółć"_s),
                 WriteOutcome::Failed);
        QVERIFY(!f.vault->rename(u"Dokumenty/plan.md"_s, u"plan.md"_s));
        QVERIFY(!f.vault->rename(u"a.md"_s, u"Dokumenty/a.md"_s));
        QVERIFY(!f.vault->removePermanently(u"Dokumenty/plan.md"_s));
        QVERIFY(f.vault->remove(u"Dokumenty/plan.md"_s)); // not the vault's: nothing to delete
        f.vault->processPendingEvents();
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.changes().isEmpty());
        QCOMPARE(diskFiles(outside), theirs);
        QCOMPARE(readBehind(outside, u"plan.md"_s), "tajny plan zażółć"_ba);
        QCOMPARE(f.read(u"a.md"_s), "notatka"_ba);
        // Allowed: the linked files come as changes, and go again when it is switched off.
        f.vault->setFollowsOutsideLinks(true);
        QCOMPARE(sorted(f.changes()), (QStringList{u"create:Dokumenty/Podatki/zeznanie.pdf@external"_s,
                                                   u"create:Dokumenty/plan.md@external"_s}));
        QCOMPARE(f.text(u"Dokumenty/plan.md"_s), u"tajny plan zażółć"_s);
        QVERIFY(f.vault->reaches(u"Dokumenty/plan.md"_s));
        QVERIFY(!f.vault->skippedOutsideLinks());
        QCOMPARE(f.folders(), (QStringList{u"Dokumenty"_s, u"Dokumenty/Podatki"_s}));
        f.spy->clear();
        f.vault->setFollowsOutsideLinks(false);
        QCOMPARE(sorted(f.changes()), (QStringList{u"delete:Dokumenty/Podatki/zeznanie.pdf@external"_s,
                                                   u"delete:Dokumenty/plan.md@external"_s}));
        QVERIFY(f.vault->scanStatus().isOk());
        QCOMPARE(f.files(), QStringList{u"a.md"_s});
        QVERIFY(f.folders().isEmpty());
        QCOMPARE(diskFiles(outside), theirs);
        // Allowed before open(): listed at once.
        f.followOutsideLinks = true;
        QVERIFY(f.open());
        QCOMPARE(f.files(), (QStringList{u"Dokumenty/Podatki/zeznanie.pdf"_s, u"Dokumenty/plan.md"_s, u"a.md"_s}));
        // A link into the vault (a duplicate) is none to outside.
        Fixture g;
        g.write(u"Projekty/a.md"_s, "a");
        QVERIFY(QFile::link(g.root() + u"/Projekty"_s, g.root() + u"/kopia"_s));
        QVERIFY(g.open());
        QVERIFY(!g.vault->skippedOutsideLinks());
    }

    void trashOperationsNeverGoThroughLinks()
    {
        // A ".trash" link (planted by a sync peer) would carry deleted notes out of the vault,
        // list foreign files as deleted notes, restore them into the (synced) vault and delete
        // them "permanently" where they really are.
        BRULION_SKIP_WITHOUT_SYMLINKS();
        Fixture f;
        f.write(u"a.md"_s, "notatka");
        f.write(u"Projekt/b.md"_s, "b");
        const QString outside = f.tmp.path() + u"/cudze"_s;
        writeBehind(outside, u"prywatne.txt"_s, "sekret");
        QVERIFY(f.open());
        QVERIFY(QFile::link(outside, f.root() + u"/.trash"_s));
        QVERIFY(f.vault->trash().isEmpty());
        QVERIFY(!f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->errorString().contains(u".trash"_s));
        QCOMPARE(f.read(u"a.md"_s), "notatka"_ba);
        QVERIFY(f.vault->exists(u"a.md"_s));
        QVERIFY(f.vault->restore(u".trash/prywatne.txt"_s).isEmpty());
        QVERIFY(!f.vault->remove(u".trash/prywatne.txt"_s));
        QVERIFY(!f.vault->removePermanently(u".trash/prywatne.txt"_s));
        // Nor by hand: rename() into the trash or out of it.
        QVERIFY(!f.vault->rename(u"a.md"_s, u".trash/a.md"_s));
        QVERIFY(f.vault->errorString().contains(u".trash/a.md"_s));
        QVERIFY(!f.vault->rename(u".trash/prywatne.txt"_s, u"prywatne.txt"_s));
        QVERIFY(!QFileInfo::exists(f.root() + u"/prywatne.txt"_s));
        QCOMPARE(f.read(u"a.md"_s), "notatka"_ba);
        QCOMPARE(readBehind(outside, u"prywatne.txt"_s), "sekret"_ba);
        QVERIFY(diskFiles(outside) == QStringList{u"prywatne.txt"_s});
        QVERIFY(f.changes().isEmpty());
        QVERIFY(f.vault->emptyTrash()); // removes the link itself, nothing behind it
        QVERIFY(!QFileInfo(f.root() + u"/.trash"_s).isSymLink());
        QCOMPARE(readBehind(outside, u"prywatne.txt"_s), "sekret"_ba);
        // A link deeper inside a real trash is not followed either.
        QVERIFY(QDir().mkpath(f.root() + u"/.trash"_s));
        QVERIFY(QFile::link(outside, f.root() + u"/.trash/Projekt"_s));
        QVERIFY(!f.vault->remove(u"Projekt/b.md"_s));
        QVERIFY(f.vault->restore(u".trash/Projekt/prywatne.txt"_s).isEmpty());
        QVERIFY(!f.vault->rename(u"Projekt/b.md"_s, u".trash/Projekt/b.md"_s));
        QVERIFY(!f.vault->rename(u".trash/Projekt/prywatne.txt"_s, u"Projekt/prywatne.txt"_s));
        // Also when the link's name is spelled in NFD (from a Mac) and the path given in NFC.
        QVERIFY(QFile::link(outside, f.root() + u"/.trash/"_s + u"Książki"_s.normalized(QString::NormalizationForm_D)));
        QVERIFY(f.vault->restore(u".trash/Książki/prywatne.txt"_s).isEmpty());
        QVERIFY(!f.vault->removePermanently(u".trash/Książki/prywatne.txt"_s));
        QVERIFY(!f.vault->rename(u".trash/Książki/prywatne.txt"_s, u"Książki.txt"_s));
        QVERIFY(diskFiles(outside) == QStringList{u"prywatne.txt"_s});
        QCOMPARE(f.files(), (QStringList{u"Projekt/b.md"_s, u"a.md"_s}));
        QVERIFY(f.changes().isEmpty());
        // By hand into a real trash folder and out of it again: allowed.
        QVERIFY(f.vault->rename(u"a.md"_s, u".trash/Ręcznie/a.md"_s));
        QVERIFY(f.vault->rename(u".trash/Ręcznie/a.md"_s, u"a.md"_s));
        QVERIFY(f.vault->remove(u"a.md"_s)); // an ordinary trash works
        QCOMPARE(f.read(u".trash/a.md"_s), "notatka"_ba);
    }

    // ---- byte order marks ----------------------------------------------------------------------
    void byteOrderMarkOfAnUnseenFileIsKept()
    {
        Fixture f;
        QVERIFY(f.open());
        f.write(u"z Notatnika.md"_s, "\xEF\xBB\xBFpierwsza\r\n"); // the vault has not seen it yet
        QVERIFY(f.vault->writeText(u"z Notatnika.md"_s, u"druga\r\n"_s));
        QCOMPARE(f.read(u"z Notatnika.md"_s), QByteArray("\xEF\xBB\xBF"_ba + "druga\r\n"_ba));
        QCOMPARE(f.changes(), (QStringList{u"create:z Notatnika.md@external"_s, u"modify:z Notatnika.md@user"_s}));
        f.write(u".obsidian/app.json"_s, "\xEF\xBB\xBF{}");
        QVERIFY(f.vault->writeText(u".obsidian/app.json"_s, u"{\"a\":1}"_s)); // untracked too
        QCOMPARE(f.read(u".obsidian/app.json"_s), "\xEF\xBB\xBF{\"a\":1}"_ba);
    }

    void byteOrderMarkRemovedBehindTheVaultsBack()
    {
        Fixture f;
        f.write(u"a.md"_s, "\xEF\xBB\xBFz BOM");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"z BOM"_s);
        f.write(u"a.md"_s, "bez BOM-u, dłuższa");
        QVERIFY(f.vault->writeText(u"a.md"_s, u"nowa"_s));
        QCOMPARE(f.read(u"a.md"_s), "nowa"_ba);
    }

    // ---- trash entries with NFD names ------------------------------------------------------------
    void trashEntriesWithNfdNamesFromOtherDevices()
    {
        // Files deleted on a Mac arrive in .trash/ with NFD names; trash() lists them under
        // their normalised (NFC) paths, which restore() and remove() must accept.
        Fixture f;
        const auto nfd = [](const QString &s) { return s.normalized(QString::NormalizationForm_D); };
        f.write(u".trash/"_s + nfd(u"Żółta kartka.md"_s), "z innego urządzenia");
        f.write(u".trash/"_s + nfd(u"Książki"_s) + u"/Lalka.md"_s, "Prus");
        f.write(u".trash/"_s + nfd(u"Ćma.md"_s), "ćma");
        QVERIFY(f.open());
        QCOMPARE(statPaths(f.vault->trash()),
                 (QStringList{u".trash/Książki/Lalka.md"_s, u".trash/Ćma.md"_s, u".trash/Żółta kartka.md"_s}));
        QCOMPARE(f.vault->trashInfo(u".trash/Żółta kartka.md"_s).original, u"Żółta kartka.md"_s);
        QCOMPARE(f.text(u".trash/Ćma.md"_s), u"ćma"_s); // preview in the trash view
        QCOMPARE(f.vault->restore(u".trash/Żółta kartka.md"_s), u"Żółta kartka.md"_s);
        QCOMPARE(f.text(u"Żółta kartka.md"_s), u"z innego urządzenia"_s);
        QCOMPARE(f.vault->restore(u".trash/Książki/Lalka.md"_s), u"Książki/Lalka.md"_s);
        QCOMPARE(f.text(u"Książki/Lalka.md"_s), u"Prus"_s);
        QVERIFY(f.vault->remove(u".trash/Ćma.md"_s)); // inside the trash: permanent
        QVERIFY(f.vault->trash().isEmpty());
        QCOMPARE(f.changes(), (QStringList{u"create:Żółta kartka.md@user"_s, u"create:Książki/Lalka.md@user"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void foldersRespelledByOtherPrograms()
    {
        // Another program re-spells a folder in NFD (a macOS client of a shared folder): the
        // notes are the same, but the vault must read and write them under the new spelling.
        Fixture f;
        const auto nfd = [](const QString &s) { return s.normalized(QString::NormalizationForm_D); };
        f.write(u"Książki/"_s + nfd(u"Żaba.md"_s), "rechocze");
        f.write(u"Książki/Lalka.md"_s, "Prus");
        QVERIFY(f.open());
        QVERIFY(renameBehind(f.root(), u"Książki"_s, nfd(u"Książki"_s)));
        QVERIFY(f.vault->rescan().isEmpty()); // same notes, other spelling
        QCOMPARE(f.text(u"Książki/Lalka.md"_s), u"Prus"_s);
        QCOMPARE(f.text(u"Książki/Żaba.md"_s), u"rechocze"_s);
        QVERIFY(f.vault->writeText(u"Książki/Nowa.md"_s, u"nowa"_s));
        QCOMPARE(diskFiles(f.root()),
                 sorted({nfd(u"Książki/Lalka.md"_s), nfd(u"Książki"_s) + u"/Nowa.md"_s, nfd(u"Książki/Żaba.md"_s)}));
        QVERIFY(f.vault->rename(u"Książki"_s, u"Biblioteka"_s));
        QCOMPARE(diskFiles(f.root()),
                 sorted({u"Biblioteka/Lalka.md"_s, u"Biblioteka/Nowa.md"_s, u"Biblioteka/"_s + nfd(u"Żaba.md"_s)}));
        QCOMPARE(f.text(u"Biblioteka/Żaba.md"_s), u"rechocze"_s);
        // A shallow listing (the watcher's) respells the folder and the spellings below it.
        QVERIFY(f.vault->rename(u"Biblioteka"_s, u"Bibliotęka"_s));
        QCOMPARE(f.changes().size(), 7);
        f.vault->startWatching();
        QVERIFY(renameBehind(f.root(), u"Bibliotęka"_s, nfd(u"Bibliotęka"_s)));
        QTRY_COMPARE(f.text(u"Bibliotęka/Żaba.md"_s), u"rechocze"_s);
        QCOMPARE(f.text(u"Bibliotęka/Lalka.md"_s), u"Prus"_s);
        QTest::qWait(100);
        f.vault->processPendingEvents();
        QCOMPARE(f.changes().size(), 7); // no changes: same notes
        QVERIFY(f.vault->rescan().isEmpty());
        // Operations right after an unseen respelling find the notes too: a delete must move
        // the note to the trash, not just forget it.
        f.vault->stopWatching();
        QVERIFY(renameBehind(f.root(), nfd(u"Bibliotęka"_s), u"Bibliotęka"_s));
        QVERIFY(f.vault->remove(u"Bibliotęka/Lalka.md"_s));
        QCOMPARE(f.read(u".trash/Bibliotęka/Lalka.md"_s), "Prus"_ba);
        QVERIFY(f.vault->rename(u"Bibliotęka/Żaba.md"_s, u"Żaba.md"_s));
        QCOMPARE(f.text(u"Żaba.md"_s), u"rechocze"_s);
        QCOMPARE(f.changes().mid(7),
                 (QStringList{u"delete:Bibliotęka/Lalka.md@user"_s, u"rename:Bibliotęka/Żaba.md->Żaba.md@user"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void renameAfterTheKindChangedBehindTheVaultsBack()
    {
        // A folder became a file (and a file a folder) behind the vault's back: rename() must
        // take that in first instead of leaving the old entries behind as ghosts.
        Fixture f;
        f.write(u"Szkic/a.md"_s, "a");
        f.write(u"Plik"_s, "plik");
        QVERIFY(f.open());
        QVERIFY(removeBehind(f.root(), u"Szkic"_s));
        f.write(u"Szkic"_s, "teraz plik");
        QVERIFY(f.vault->rename(u"Szkic"_s, u"Gotowe"_s));
        QCOMPARE(f.changes(), (QStringList{u"delete:Szkic/a.md@external"_s, u"create:Szkic@external"_s,
                                           u"rename:Szkic->Gotowe@user"_s}));
        f.spy->clear();
        QVERIFY(removeBehind(f.root(), u"Plik"_s));
        f.write(u"Plik/b.md"_s, "b");
        QVERIFY(f.vault->rename(u"Plik"_s, u"Folder"_s));
        QCOMPARE(f.changes(), (QStringList{u"delete:Plik@external"_s, u"create:Plik/b.md@external"_s,
                                           u"rename:Plik/b.md->Folder/b.md@user"_s}));
        QCOMPARE(f.files(), (QStringList{u"Folder/b.md"_s, u"Gotowe"_s}));
        QCOMPARE(f.folders(), QStringList{u"Folder"_s});
        QVERIFY(f.vault->rescan().isEmpty());
        // remove() likewise reports what other programs did before its own deletion.
        f.spy->clear();
        QVERIFY(removeBehind(f.root(), u"Folder"_s));
        f.write(u"Folder"_s, "plik");
        QVERIFY(f.vault->remove(u"Folder"_s));
        QCOMPARE(f.changes(),
                 (QStringList{u"delete:Folder/b.md@external"_s, u"create:Folder@external"_s, u"delete:Folder@user"_s}));
        QCOMPARE(f.read(u".trash/Folder"_s), "plik"_ba);
        QCOMPARE(f.files(), QStringList{u"Gotowe"_s});
        QVERIFY(f.folders().isEmpty());
    }

    // ---- external renames of identical files -------------------------------------------------------
    void identicalFilesMovedTogetherKeepTheirOwnHistory()
    {
        // Two notes made from one template are moved into one folder by a file manager: each
        // must be paired with its own old path (by the folders they keep), not by alphabet.
        Fixture f;
        f.write(u"A/Projekt/index.md"_s, "# Szablon\n");
        f.write(u"B/Inne/index.md"_s, "# Szablon\n");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"A/Projekt/index.md"_s, u"projekt v1"_s));
        QVERIFY(f.vault->writeText(u"B/Inne/index.md"_s, u"inne v1"_s));
        f.clock.advanceMs(10 * 60'000);
        QVERIFY(f.vault->writeText(u"A/Projekt/index.md"_s, u"# Szablon\n"_s));
        QVERIFY(f.vault->writeText(u"B/Inne/index.md"_s, u"# Szablon\n"_s));
        f.spy->clear();
        QVERIFY(renameBehind(f.root(), u"A/Projekt"_s, u"Archiwum/Projekt"_s));
        QVERIFY(renameBehind(f.root(), u"B/Inne"_s, u"Archiwum/Inne"_s));
        QCOMPARE(sorted(shortChanges(f.vault->rescan())),
                 (QStringList{u"rename:A/Projekt/index.md->Archiwum/Projekt/index.md@external"_s,
                              u"rename:B/Inne/index.md->Archiwum/Inne/index.md@external"_s}));
        QCOMPARE(f.historyContents(u"Archiwum/Projekt/index.md"_s), (QStringList{u"projekt v1"_s, u"# Szablon\n"_s}));
        QCOMPARE(f.historyContents(u"Archiwum/Inne/index.md"_s), (QStringList{u"inne v1"_s, u"# Szablon\n"_s}));
    }

    // ---- config --------------------------------------------------------------------------------
    void anOversizedConfigIsNotRead()
    {
        // A synced .brulion/config.json can be anything; a settings file of megabytes is not
        // read into memory (nor parsed) — the defaults apply.
        Fixture f;
        QByteArray json("{\"journalsFolder\":\"Dni\"}");
        json.append(QByteArray(2 * 1024 * 1024, ' '));
        f.write(u".brulion/config.json"_s, json);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"too big"_s));
        QVERIFY(f.open());
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QCOMPARE(f.vault->configWarnings().size(), 1);
    }

    // ---- watching ------------------------------------------------------------------------------
    void watchingRequestedBeforeOpen()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/Notatki"_s;
        Vault vault(root);
        vault.startWatching(); // the app may wire this up before the vault is opened
        QVERIFY(vault.open());
        QVERIFY(vault.isWatching());
        QSignalSpy spy(&vault, &Vault::changed);
        writeBehind(root, u"z zewnątrz.md"_s, "x");
        QTRY_COMPARE(collectedShort(spy), QStringList{u"create:z zewnątrz.md@external"_s});
        vault.stopWatching();
        QVERIFY(vault.open());
        QVERIFY(!vault.isWatching());
    }
};

QTEST_GUILESS_MAIN(TestVaultEdge)
#include "tst_vault_edge.moc"
