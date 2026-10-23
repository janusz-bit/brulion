// Vault operations: open and queries, reading and writing (atomic, no-op when unchanged,
// History snapshots, unseen external changes taken in first), create, trash and restore,
// rename/move, the vault config. Ported from Brulion v1 (src/core/storage/vault.test.ts).
#include "vaulttest.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <utility>

using namespace vaulttest;

namespace {

qint64 diskMtime(const QString &abs)
{
    return QFileInfo(abs).fileTime(QFileDevice::FileModificationTime, QTimeZone(QTimeZone::UTC)).toMSecsSinceEpoch();
}

/// Makes `dir` read-only for the duration of a scope; `effective()` is false when the process
/// ignores permissions (root), so tests can skip.
class ReadOnlyDir {
public:
    explicit ReadOnlyDir(QString dir) : m_dir(std::move(dir))
    {
        QFile::setPermissions(m_dir, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
    }
    ~ReadOnlyDir() { restore(); }
    Q_DISABLE_COPY_MOVE(ReadOnlyDir)
    bool effective() const { return !QFileInfo(m_dir).isWritable(); }
    void restore()
    {
        QFile::setPermissions(m_dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }

private:
    QString m_dir;
};

} // namespace

class TestVault : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- open / queries ------------------------------------------------------------------
    void listsTrackedFiles()
    {
        Fixture f;
        f.write(u"Strona.md"_s, "a");
        f.write(u"journals/2026-10-14.md"_s, "b");
        f.write(u"Strona.sync-conflict-20261014-101010-ABCDEFG.md"_s, "c");
        f.write(u"assets/zdjęcie.png"_s, "\x01\x02\x03");
        f.write(u".obsidian/app.json"_s, "{}");
        f.write(u".trash/old.md"_s, "x");
        f.write(u".git/HEAD"_s, "ref");
        f.write(u"~syncthing~Strona.md.tmp"_s, "tmp");
        f.write(u".brulion/config.json"_s, "{}");
        f.write(u"logseq/config.edn"_s, "{}");
        QVERIFY(f.open());
        Vault &v = *f.vault;
        QVERIFY(v.isOpen());
        QCOMPARE(f.files(), (QStringList{u"Strona.md"_s, u"Strona.sync-conflict-20261014-101010-ABCDEFG.md"_s,
                                         u"assets/zdjęcie.png"_s, u"journals/2026-10-14.md"_s}));
        QCOMPARE(statPaths(v.markdownFiles()),
                 (QStringList{u"Strona.md"_s, u"Strona.sync-conflict-20261014-101010-ABCDEFG.md"_s,
                              u"journals/2026-10-14.md"_s}));
        QCOMPARE(f.folders(), (QStringList{u"assets"_s, u"journals"_s}));
        QVERIFY(v.exists(u"journals"_s));
        QVERIFY(v.exists(u"./journals/2026-10-14.md"_s));
        QVERIFY(v.exists(u".brulion/config.json"_s));
        QVERIFY(!v.exists(u".brulion"_s));
        QVERIFY(!v.exists(u".obsidian/app.json"_s));
        QVERIFY(!v.exists(QString()));
        const std::optional<FileStat> st = v.stat(u"assets/zdjęcie.png"_s);
        QVERIFY(st);
        QCOMPARE(st->size, 3);
        QVERIFY(!st->isDir);
        QCOMPARE(st->mtime, diskMtime(f.root() + u"/assets/zdjęcie.png"_s));
        QVERIFY(v.stat(u"journals"_s)->isDir);
        QVERIFY(!v.stat(u"nope"_s));
        QCOMPARE(v.absolutePath(u"journals/2026-10-14.md"_s), f.root() + u"/journals/2026-10-14.md"_s);
        QCOMPARE(f.spy->count(), 0);
    }

    void openCreatesTheFolder()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/nowy/Mój skarbiec"_s;
        Vault vault(root + u"/"_s);
        QVERIFY(vault.open());
        QVERIFY(QFileInfo(root).isDir());
        QCOMPARE(vault.rootPath(), root);
        QVERIFY(vault.files().isEmpty());
        QVERIFY(!vault.history()); // no device state folder
        QVERIFY(vault.store());
        QVERIFY(!vault.store()->isPersistent());
    }

    void openFailsOnAFile()
    {
        QTemporaryDir tmp;
        writeBehind(tmp.path(), u"plik"_s, "x");
        Vault vault(tmp.path() + u"/plik"_s);
        QString error;
        QVERIFY(!vault.open(&error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(vault.errorString(), error);
        QVERIFY(!vault.isOpen());
        // Nothing works before a successful open().
        QVERIFY(!vault.writeText(u"a.md"_s, u"x"_s));
        bool ok = true;
        vault.readText(u"a.md"_s, &ok);
        QVERIFY(!ok);
        QVERIFY(vault.create(u"a.md"_s).isEmpty());
        QVERIFY(!vault.remove(u"a.md"_s));
        QVERIFY(vault.rescan().isEmpty());
        Vault empty({});
        QVERIFY(!empty.open());
    }

    void stableId()
    {
        QTemporaryDir tmp;
        const QString root = tmp.path() + u"/Notatki"_s;
        Vault a(root);
        Vault b(root + u"/./"_s);
        Vault c(tmp.path() + u"/Inne"_s);
        QVERIFY(a.open());
        QVERIFY(b.open());
        QVERIFY(c.open());
        QCOMPARE(a.id(), b.id());
        QCOMPARE(a.id(), Vault::idForPath(root));
        QCOMPARE(a.id().size(), 16);
        QVERIFY(a.id() != c.id());
        QVERIFY(QFile::link(root, tmp.path() + u"/skrót"_s));
        QCOMPARE(Vault::idForPath(tmp.path() + u"/skrót"_s), a.id()); // canonical path
    }

    void openAgainReloads()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u".obsidian/app.json"_s, "{}");
        QVERIFY(f.open());
        f.write(u"b.md"_s, "b");
        QVERIFY(f.vault->open());
        QCOMPARE(f.files(), (QStringList{u"a.md"_s, u"b.md"_s}));
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QVERIFY(removeBehind(f.root(), u"b.md"_s));
        QVERIFY(f.vault->open());
        QVERIFY(f.files().isEmpty());
        QVERIFY(f.vault->markdownFiles().isEmpty());
        QCOMPARE(f.spy->count(), 0); // open() never reports changes
    }

    void nfdFileNames()
    {
        // Notes copied from macOS keep NFD names: found, edited and renamed without duplicates.
        Fixture f;
        const QString nfc = u"Spotkania/Żółta kartka.md"_s;
        const QString nfd = nfc.normalized(QString::NormalizationForm_D);
        QVERIFY(nfd != nfc);
        f.write(nfd, "- pierwsza wersja");
        QVERIFY(f.open());
        QCOMPARE(f.files(), QStringList{nfc});
        QVERIFY(f.vault->exists(nfd));
        QCOMPARE(f.text(nfd), u"- pierwsza wersja"_s);
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->writeText(nfc, u"- druga wersja"_s));
        QCOMPARE(diskFiles(f.root()), QStringList{nfd});
        QCOMPARE(f.read(nfd), "- druga wersja"_ba);
        QCOMPARE(f.historyContents(nfc), QStringList{u"- pierwsza wersja"_s});
        QVERIFY(f.vault->rename(nfc, u"Spotkania/Zielona kartka.md"_s));
        QCOMPARE(diskFiles(f.root()), QStringList{u"Spotkania/Zielona kartka.md"_s});
        QCOMPARE(f.changes(), (QStringList{u"modify:Spotkania/Żółta kartka.md@user"_s,
                                           u"rename:Spotkania/Żółta kartka.md->Spotkania/Zielona kartka.md@user"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void nfdFolderNames()
    {
        Fixture f;
        const QString nfdDir = u"Książki"_s.normalized(QString::NormalizationForm_D);
        f.write(nfdDir + u"/Lalka.md"_s, "Prus");
        QVERIFY(f.open());
        QCOMPARE(f.files(), QStringList{u"Książki/Lalka.md"_s});
        QCOMPARE(f.folders(), QStringList{u"Książki"_s});
        QVERIFY(f.vault->writeText(u"Książki/Potop.md"_s, u"Sienkiewicz"_s));
        QCOMPARE(diskFiles(f.root()), (QStringList{nfdDir + u"/Lalka.md"_s, nfdDir + u"/Potop.md"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->rename(u"Książki"_s, u"Biblioteka/Polska"_s));
        QCOMPARE(diskFiles(f.root()), (QStringList{u"Biblioteka/Polska/Lalka.md"_s, u"Biblioteka/Polska/Potop.md"_s}));
        QCOMPARE(f.text(u"Biblioteka/Polska/Lalka.md"_s), u"Prus"_s);
        QVERIFY(f.vault->rescan().isEmpty());
    }

    // ---- config --------------------------------------------------------------------------
    void defaultConfigWhenMissing()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(f.vault->configWarnings().isEmpty());
    }

    void configMergedOverDefaults()
    {
        Fixture f;
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Dziennik","weekStartsOn":0,"future":true})");
        QVERIFY(f.open());
        VaultConfig expected;
        expected.journalsFolder = u"Dziennik"_s;
        expected.weekStartsOn = Qt::Sunday;
        QVERIFY(sameVaultConfig(f.vault->config(), expected));
        QVERIFY(f.text(u".brulion/config.json"_s).contains(u"Dziennik"_s));
        QVERIFY(!f.files().contains(u".brulion/config.json"_s));
    }

    void invalidConfigFallsBackToDefaults()
    {
        Fixture f;
        f.write(u".brulion/config.json"_s, "{ to nie jest JSON");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(u"Invalid JSON"_s));
        QVERIFY(f.open());
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QCOMPARE(f.vault->configWarnings().size(), 1);
    }

    void saveConfigKeepsUnknownKeys()
    {
        Fixture f;
        f.write(u".brulion/config.json"_s, R"({"pluginX":{"on":true}})");
        QVERIFY(f.open());
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        VaultConfig config = f.vault->config();
        config.journalsFolder = u"Dziennik"_s;
        config.defaultReminderTime = QTime(8, 30);
        QVERIFY(f.vault->saveConfig(config));
        const QJsonObject saved = QJsonDocument::fromJson(f.read(u".brulion/config.json"_s)).object();
        QCOMPARE(saved.value("journalsFolder"_L1).toString(), u"Dziennik"_s);
        QCOMPARE(saved.value("defaultReminderTime"_L1).toString(), u"08:30"_s);
        QCOMPARE(saved.value("pluginX"_L1).toObject().value("on"_L1).toBool(), true);
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QCOMPARE(f.changes(), QStringList{u"modify:.brulion/config.json@user"_s});
        QCOMPARE(configSpy.count(), 1);
        QVERIFY(!f.files().contains(u".brulion/config.json"_s));
        QVERIFY(f.vault->saveConfig(config)); // unchanged: nothing written or emitted
        QCOMPARE(f.spy->count(), 1);
        QCOMPARE(configSpy.count(), 1);
    }

    void saveConfigCreatesTheFile()
    {
        Fixture f;
        QVERIFY(f.open());
        VaultConfig config;
        config.weekStartsOn = Qt::Sunday;
        QVERIFY(f.vault->saveConfig(config));
        const ParsedVaultConfig parsed = parseVaultConfig(f.read(u".brulion/config.json"_s));
        QVERIFY(sameVaultConfig(parsed.config, config));
        QCOMPARE(parsed.raw.value("weekStartsOn"_L1).toInt(), 0);
        QCOMPARE(f.changes(), QStringList{u"create:.brulion/config.json@user"_s});
        QVERIFY(f.vault->exists(u".brulion/config.json"_s));
        QVERIFY(f.folders().isEmpty()); // ".brulion" is not a vault folder
    }

    // ---- reading -------------------------------------------------------------------------
    void readTextAndBinary()
    {
        Fixture f;
        f.write(u"crlf.md"_s, "linia 1\r\nlinia 2\r\n");
        f.write(u"bom.md"_s, "\xEF\xBB\xBF# Tytuł\n");
        f.write(u"img.png"_s, QByteArray("\x89PNG\x00\x01", 6));
        QVERIFY(f.open());
        bool ok = false;
        QCOMPARE(f.vault->readText(u"crlf.md"_s, &ok), u"linia 1\r\nlinia 2\r\n"_s);
        QVERIFY(ok);
        QCOMPARE(f.text(u"bom.md"_s), u"# Tytuł\n"_s);
        QCOMPARE(f.vault->readBinary(u"bom.md"_s), "\xEF\xBB\xBF# Tytuł\n"_ba);
        QCOMPARE(f.vault->readBinary(u"img.png"_s, &ok), QByteArray("\x89PNG\x00\x01", 6));
        QVERIFY(ok);
        QVERIFY(f.vault->readText(u"missing.md"_s, &ok).isEmpty());
        QVERIFY(!ok);
        QVERIFY(!f.vault->errorString().isEmpty());
        f.vault->readText(QString(), &ok);
        QVERIFY(!ok);
        f.vault->readText(u"assets"_s, &ok);
        QVERIFY(!ok);
        f.write(u".obsidian/app.json"_s, R"({"a":1})");
        QCOMPARE(f.vault->readText(u".obsidian/app.json"_s, &ok), QString::fromUtf8(R"({"a":1})"));
        QVERIFY(ok);
        f.vault->processPendingEvents();
        QCOMPARE(f.spy->count(), 0);
    }

    // ---- writing -------------------------------------------------------------------------
    void writeEmitsCreateThenModify()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"Notatki/Żółw.md"_s, u"pierwsza"_s));
        QVERIFY(f.vault->writeText(u"Notatki/Żółw.md"_s, u"druga"_s, ChangeSource::Sync));
        QCOMPARE(f.changes(), (QStringList{u"create:Notatki/Żółw.md@user"_s, u"modify:Notatki/Żółw.md@sync"_s}));
        QCOMPARE(f.spy->count(), 2); // one signal per operation
        QVERIFY(f.vault->exists(u"Notatki"_s));
        QCOMPARE(f.folders(), QStringList{u"Notatki"_s});
        QCOMPARE(f.vault->stat(u"Notatki/Żółw.md"_s)->size, 5);
        QCOMPARE(f.text(u"Notatki/Żółw.md"_s), u"druga"_s);
        QCOMPARE(f.read(u"Notatki/Żółw.md"_s), "druga"_ba);
    }

    void writeIsANoopWhenUnchanged()
    {
        Fixture f;
        f.write(u"a.md"_s, "same");
        QVERIFY(f.open());
        const QString abs = f.root() + u"/a.md"_s;
        const qint64 before = diskMtime(abs);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"same"_s)); // never read: compared with the disk
        QVERIFY(f.vault->writeText(u"a.md"_s, u"same"_s));
        QCOMPARE(diskMtime(abs), before); // not rewritten
        QVERIFY(f.vault->writeText(u"a.md"_s, u"changed"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"changed"_s));
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@user"_s});
    }

    void writeSnapshotsThePreviousContent()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v1"_s));
        f.clock.advanceMs(10 * 60'000);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v2"_s, ChangeSource::Sync));
        QCOMPARE(f.historyReasons(u"a.md"_s), (QStringList{u"sync"_s, u"user"_s}));
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"v1"_s, u"v0"_s}));
        QCOMPARE(f.vault->history()->list(u"a.md"_s).first().time, f.clock.now);
    }

    void noSnapshotsOfNewOrBinaryFiles()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"new.md"_s, u"x"_s));
        QVERIFY(f.vault->writeBinary(u"img.png"_s, "\x01"_ba));
        QVERIFY(f.vault->writeBinary(u"img.png"_s, "\x02"_ba));
        QVERIFY(f.vault->history()->paths().isEmpty());
    }

    void worksWithoutHistory()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open(false));
        QVERIFY(!f.vault->history());
        QVERIFY(f.vault->writeText(u"a.md"_s, u"b"_s));
        QCOMPARE(f.text(u"a.md"_s), u"b"_s);
        QVERIFY(!QFileInfo::exists(f.state()));
    }

    void writesToIgnoredPathsSilently()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u".obsidian/workspace.json"_s, u"{}"_s));
        QCOMPARE(f.read(u".obsidian/workspace.json"_s), "{}"_ba);
        QVERIFY(f.changes().isEmpty());
        QVERIFY(f.files().isEmpty());
        QVERIFY(!f.vault->exists(u".obsidian/workspace.json"_s));
    }

    void unknownFileOnDiskIsTakenInFirst()
    {
        Fixture f;
        QVERIFY(f.open());
        f.write(u"surprise.md"_s, "from Syncthing");
        QVERIFY(f.vault->writeText(u"surprise.md"_s, u"mine"_s));
        QCOMPARE(f.changes(), (QStringList{u"create:surprise.md@external"_s, u"modify:surprise.md@user"_s}));
        QCOMPARE(f.spy->count(), 1);
        QCOMPARE(f.historyContents(u"surprise.md"_s), QStringList{u"from Syncthing"_s});
    }

    void writeNeverOverwritesAnUnseenExternalChange()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v1"_s));
        f.clock.advanceMs(2000);
        // Syncthing replaces the file; the watcher event is still on its way (or not watching).
        f.write(u"a.md"_s, "z Syncthinga");
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"moje zmiany"_s));
        QCOMPARE(f.read(u"a.md"_s), "moje zmiany"_ba);
        QCOMPARE(f.changes(), (QStringList{u"modify:a.md@user"_s, u"modify:a.md@external"_s, u"modify:a.md@user"_s}));
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"z Syncthinga"_s, u"v1"_s, u"v0"_s}));
        QCOMPARE(f.historyReasons(u"a.md"_s), (QStringList{u"user"_s, u"external"_s, u"user"_s}));
    }

    // writeTextIfUnchanged(): the compare-and-swap of the editor's saves and brulion-cli's edits.
    void writeIfUnchangedWritesOverTheExpectedText()
    {
        Fixture f;
        f.write(u"a.md"_s, "- żółw\n");
        QVERIFY(f.open());
        QCOMPARE(f.vault->writeTextIfUnchanged(u"a.md"_s, u"- żółw\n- jeż\n"_s, u"- żółw\n"_s), WriteOutcome::Written);
        QCOMPARE(f.read(u"a.md"_s), "- żółw\n- jeż\n"_ba);
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@user"_s});
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"- żółw\n"_s});
        // The same text again: nothing to write, still "Written" (the file holds it).
        QCOMPARE(f.vault->writeTextIfUnchanged(u"a.md"_s, u"- żółw\n- jeż\n"_s, u"- żółw\n- jeż\n"_s),
                 WriteOutcome::Written);
        QCOMPARE(f.spy->count(), 1);
    }

    void writeIfUnchangedKeepsAnExternalChange()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0\n");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"v0\n"_s);
        // An agent appends a line; the watcher has not reported it yet.
        f.write(u"a.md"_s, "v0\n- od agenta\n");
        QCOMPARE(f.vault->writeTextIfUnchanged(u"a.md"_s, u"v0 edytowane\n"_s, u"v0\n"_s), WriteOutcome::Changed);
        QCOMPARE(f.read(u"a.md"_s), "v0\n- od agenta\n"_ba);           // nothing overwritten
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@external"_s}); // … and taken in at once
        QCOMPARE(f.text(u"a.md"_s), u"v0\n- od agenta\n"_s);
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"v0\n"_s});
        // Same size, same timestamp (coarse file systems): found by the content.
        const qint64 mtime = nextFakeMtime();
        f.write(u"b.md"_s, "aaaa", mtime);
        QVERIFY(f.vault->rescan().size() == 1);
        QCOMPARE(f.text(u"b.md"_s), u"aaaa"_s);
        f.write(u"b.md"_s, "bbbb", mtime);
        QCOMPARE(f.vault->writeTextIfUnchanged(u"b.md"_s, u"cccc"_s, u"aaaa"_s), WriteOutcome::Changed);
        QCOMPARE(f.read(u"b.md"_s), "bbbb"_ba);
        QCOMPARE(f.text(u"b.md"_s), u"bbbb"_s);
    }

    void writeIfUnchangedCreatesAMissingFile()
    {
        Fixture f;
        QVERIFY(f.open());
        // Missing: written whatever was expected (a page deleted behind an editor comes back).
        QCOMPARE(f.vault->writeTextIfUnchanged(u"Nowe/strona.md"_s, u"treść"_s, QString()), WriteOutcome::Written);
        QCOMPARE(f.read(u"Nowe/strona.md"_s), "treść"_ba);
        QCOMPARE(f.vault->writeTextIfUnchanged(u"c.md"_s, u"x"_s, u"była treść"_s), WriteOutcome::Written);
        // Another program created it meanwhile: not overwritten.
        f.write(u"d.md"_s, "czyjeś");
        QCOMPARE(f.vault->writeTextIfUnchanged(u"d.md"_s, u"moje"_s, QString()), WriteOutcome::Changed);
        QCOMPARE(f.read(u"d.md"_s), "czyjeś"_ba);
        QVERIFY(f.vault->exists(u"d.md"_s));
    }

    void writeIfUnchangedKeepsTheBomAndLineEndings()
    {
        Fixture f;
        f.write(u"bom.md"_s, "\xEF\xBB\xBF- a\r\n");
        QVERIFY(f.open());
        // The expected text is what readText() gives: without the BOM.
        QCOMPARE(f.vault->writeTextIfUnchanged(u"bom.md"_s, u"- a\r\n- ą\r\n"_s, u"- a\r\n"_s), WriteOutcome::Written);
        QCOMPARE(f.read(u"bom.md"_s), "\xEF\xBB\xBF- a\r\n- ą\r\n"_ba);
        QCOMPARE(f.vault->writeTextIfUnchanged(u"bom.md"_s, u"x"_s, u"- a\n- ą\n"_s), WriteOutcome::Changed);
    }

    void writeIfUnchangedFailures()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "x");
        QVERIFY(f.open());
        QCOMPARE(f.vault->writeTextIfUnchanged(QString(), u"x"_s, QString()), WriteOutcome::Failed);
        QCOMPARE(f.vault->writeTextIfUnchanged(u"F"_s, u"x"_s, QString()), WriteOutcome::Failed); // a folder
        Vault closed(f.root());
        QCOMPARE(closed.writeTextIfUnchanged(u"F/a.md"_s, u"y"_s, u"x"_s), WriteOutcome::Failed);
    }

    void historyReasonOfOwnWrites()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        f.vault->setHistoryReason(u"cli"_s);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v1"_s));
        f.clock.advanceMs(1000);
        f.write(u"a.md"_s, "z innego programu");
        QVERIFY(f.vault->writeText(u"a.md"_s, u"v2"_s));
        // Its own writes say "cli"; what another program replaced stays "external".
        QCOMPARE(f.historyReasons(u"a.md"_s), (QStringList{u"cli"_s, u"external"_s, u"cli"_s}));
        QCOMPARE(f.historyContents(u"a.md"_s), (QStringList{u"z innego programu"_s, u"v1"_s, u"v0"_s}));
    }

    void writeAfterAnUnseenDelete()
    {
        Fixture f;
        f.write(u"a.md"_s, "stara treść");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"stara treść"_s);
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"nowa treść"_s));
        QCOMPARE(f.changes(), (QStringList{u"delete:a.md@external"_s, u"create:a.md@user"_s}));
        QCOMPARE(f.historyContents(u"a.md"_s), QStringList{u"stara treść"_s});
    }

    void unseenIdenticalChangeMakesTheWriteANoop()
    {
        Fixture f;
        f.write(u"a.md"_s, "v0");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"a.md"_s), u"v0"_s);
        const qint64 mtime = nextFakeMtime();
        f.write(u"a.md"_s, "to samo", mtime);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"to samo"_s));
        QCOMPARE(f.changes(), QStringList{u"modify:a.md@external"_s});
        QCOMPARE(diskMtime(f.root() + u"/a.md"_s), mtime); // not rewritten by us
    }

    void failedWriteKeepsTheOriginal()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "disk");
        QVERIFY(f.open());
        {
            ReadOnlyDir readOnly(f.root() + u"/F"_s);
            if (!readOnly.effective())
                QSKIP("running with privileges that ignore file permissions");
            QVERIFY(!f.vault->writeText(u"F/a.md"_s, u"lost"_s));
            QVERIFY(!f.vault->errorString().isEmpty());
            QVERIFY(f.vault->create(u"F/b.md"_s, u"x"_s).isEmpty());
        }
        QCOMPARE(f.read(u"F/a.md"_s), "disk"_ba);
        QCOMPARE(f.text(u"F/a.md"_s), u"disk"_s);
        QVERIFY(f.vault->writeText(u"F/a.md"_s, u"disk"_s)); // same as the disk: no-op
        QVERIFY(f.changes().isEmpty());
        QCOMPARE(diskFiles(f.root()), QStringList{u"F/a.md"_s}); // no temporary files left
    }

    void atomicWritesLeaveNoTemporaryFiles()
    {
        Fixture f;
        QVERIFY(f.open());
        for (int i = 0; i < 20; ++i)
            QVERIFY(f.vault->writeText(u"Dziennik/2026-10-14.md"_s, u"- wersja %1\r\n"_s.arg(i)));
        QCOMPARE(diskFiles(f.root()), QStringList{u"Dziennik/2026-10-14.md"_s});
        QCOMPARE(f.read(u"Dziennik/2026-10-14.md"_s), "- wersja 19\r\n"_ba);
        QCOMPARE(f.spy->count(), 20);
    }

    void contentIsPreservedByteForByte()
    {
        Fixture f;
        QVERIFY(f.open());
        const QString content = u"# Zażółć gęślą jaźń\r\n- [ ] zadanie ⏰ 2026-10-15 09:00\r\n\r\n\tkod\rstary mac\n"_s;
        QVERIFY(f.vault->writeText(u"Dziennik/2026-10-14 środa.md"_s, content));
        QCOMPARE(f.read(u"Dziennik/2026-10-14 środa.md"_s), content.toUtf8());
        QCOMPARE(f.text(u"Dziennik/2026-10-14 środa.md"_s), content);
        const VaultPath created = f.vault->create(u"Ćwiczenia ąę.md"_s, content);
        QCOMPARE(created, u"Ćwiczenia ąę.md"_s);
        QCOMPARE(f.read(created), content.toUtf8());
    }

    void byteOrderMarkIsKept()
    {
        Fixture f;
        f.write(u"bom.md"_s, "\xEF\xBB\xBFstara\r\n");
        f.write(u"bom2.md"_s, "\xEF\xBB\xBFx");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"bom.md"_s), u"stara\r\n"_s);
        QVERIFY(f.vault->writeText(u"bom.md"_s, u"nowa\r\n"_s));
        QCOMPARE(f.read(u"bom.md"_s), "\xEF\xBB\xBFnowa\r\n"_ba);
        QVERIFY(f.vault->writeText(u"bom2.md"_s, u"y"_s)); // never read before
        QCOMPARE(f.read(u"bom2.md"_s), "\xEF\xBB\xBFy"_ba);
        QVERIFY(f.vault->writeText(u"plain.md"_s, u"z"_s));
        QCOMPARE(f.read(u"plain.md"_s), "z"_ba);
        QVERIFY(f.vault->writeText(u"bom.md"_s, u"nowa\r\n"_s)); // unchanged incl. the BOM
        QCOMPARE(f.spy->count(), 3);
    }

    void writeBinaryReportsChangesAndSkipsIdenticalBytes()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.vault->writeBinary(u"assets/a.bin"_s, "\x01\x02"_ba));
        QVERIFY(f.vault->writeBinary(u"assets/a.bin"_s, "\x01\x02"_ba));
        QVERIFY(f.vault->writeBinary(u"assets/a.bin"_s, "\x03"_ba, ChangeSource::Import));
        QCOMPARE(f.changes(), (QStringList{u"create:assets/a.bin@user"_s, u"modify:assets/a.bin@import"_s}));
        QCOMPARE(f.vault->readBinary(u"assets/a.bin"_s), "\x03"_ba);
    }

    void writeBinarySnapshotsTheTextOfANote()
    {
        Fixture f;
        f.write(u"Notatka.md"_s, "lokalna wersja — zażółć");
        f.write(u"ćma.md"_s, "ćma");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeBinary(u"Notatka.md"_s, u"zdalna wersja"_s.toUtf8(), ChangeSource::Sync));
        QCOMPARE(f.historyReasons(u"Notatka.md"_s), QStringList{u"sync"_s});
        QCOMPARE(f.historyContents(u"Notatka.md"_s), QStringList{u"lokalna wersja — zażółć"_s});
        QCOMPARE(f.text(u"Notatka.md"_s), u"zdalna wersja"_s);
        const qint64 before = diskMtime(f.root() + u"/ćma.md"_s);
        QVERIFY(f.vault->writeBinary(u"ćma.md"_s, u"ćma"_s.toUtf8(), ChangeSource::Sync)); // same bytes
        QCOMPARE(diskMtime(f.root() + u"/ćma.md"_s), before);
        QCOMPARE(f.changes(), QStringList{u"modify:Notatka.md@sync"_s});
    }

    void writeOntoAFolderOrEmptyPathFails()
    {
        Fixture f;
        f.write(u"folder/a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(!f.vault->writeText(u"folder"_s, u"x"_s));
        QVERIFY(f.vault->errorString().contains(u"folder"_s));
        QVERIFY(!f.vault->writeText(QString(), u"x"_s));
        QVERIFY(!f.vault->writeText(u"folder/a.md/x.md"_s, u"x"_s)); // a file is in the way
        QVERIFY(f.changes().isEmpty());
        QCOMPARE(diskFiles(f.root()), QStringList{u"folder/a.md"_s});
    }

    void writeToADistinctCaseVariant()
    {
        // On case-sensitive storage "notatka.md" is a different file from "Notatka.md".
        Fixture f;
        f.write(u"Notatka.md"_s, "duża");
        QVERIFY(f.open());
        f.write(u"notatka.md"_s, "mała, inna");
        QVERIFY(f.vault->writeText(u"notatka.md"_s, u"mała, nowa"_s));
        QCOMPARE(f.read(u"Notatka.md"_s), "duża"_ba);
        QCOMPARE(f.read(u"notatka.md"_s), "mała, nowa"_ba);
        QCOMPARE(f.files(), (QStringList{u"Notatka.md"_s, u"notatka.md"_s}));
    }

    // ---- create --------------------------------------------------------------------------
    void createPicksUniquePaths()
    {
        Fixture f;
        f.write(u"Nowa notatka.md"_s, "x");
        f.write(u"folder/strona.md"_s, "y");
        QVERIFY(f.open());
        QCOMPARE(f.vault->create(u"Inna.md"_s, u"a"_s), u"Inna.md"_s);
        QCOMPARE(f.vault->create(u"Nowa notatka.md"_s, u"b"_s), u"Nowa notatka 2.md"_s);
        QCOMPARE(f.vault->create(u"Nowa notatka.md"_s, u"c"_s), u"Nowa notatka 3.md"_s);
        QCOMPARE(f.vault->create(u"folder/Strona.md"_s, u"d"_s), u"folder/Strona 2.md"_s); // case-insensitive
        QCOMPARE(f.text(u"Nowa notatka.md"_s), u"x"_s);
        QCOMPARE(f.text(u"Nowa notatka 3.md"_s), u"c"_s);
        QCOMPARE(f.changes(), (QStringList{u"create:Inna.md@user"_s, u"create:Nowa notatka 2.md@user"_s,
                                           u"create:Nowa notatka 3.md@user"_s, u"create:folder/Strona 2.md@user"_s}));
        QVERIFY(f.vault->create(QString(), u"x"_s).isEmpty());
    }

    void createUsesFoldersAsSpelled()
    {
        // A folder existing in another letter case is that folder: case-insensitive storage
        // (Android shared storage, FAT, macOS) holds one folder for both spellings.
        Fixture f;
        f.write(u"Projekty/Dom.md"_s, "dom");
        f.write(u"Projekty/Łąki/Żółw.md"_s, "żółw");
        QVERIFY(f.open());
        QCOMPARE(f.vault->create(u"projekty/Ogród.md"_s, u"a"_s), u"Projekty/Ogród.md"_s);
        QCOMPARE(f.vault->create(u"PROJEKTY/łąki/Nowa.md"_s, u"b"_s), u"Projekty/Łąki/Nowa.md"_s);
        QCOMPARE(f.vault->create(u"projekty/dom.md"_s, u"c"_s), u"Projekty/dom 2.md"_s); // the file is taken
        QCOMPARE(f.vault->createBinary(u"projekty/nowy/zdjęcie.png"_s, "\x01"_ba), u"Projekty/nowy/zdjęcie.png"_s);
        QCOMPARE(diskFiles(f.root()),
                 (QStringList{u"Projekty/Dom.md"_s, u"Projekty/Ogród.md"_s, u"Projekty/dom 2.md"_s,
                              u"Projekty/nowy/zdjęcie.png"_s, u"Projekty/Łąki/Nowa.md"_s, u"Projekty/Łąki/Żółw.md"_s}));
        QCOMPARE(f.vault->existingCase(u"projekty/łąki/żółw.md"_s), u"Projekty/Łąki/Żółw.md"_s);
        QCOMPARE(f.vault->existingCase(u"projekty/inne/x.md"_s), u"Projekty/inne/x.md"_s);
        QCOMPARE(f.vault->existingCase(u"Projekty/Dom.md"_s), u"Projekty/Dom.md"_s);
        QCOMPARE(f.vault->existingCase(QString()), QString());
    }

    void createNeverOverwritesAnUnknownFile()
    {
        Fixture f;
        QVERIFY(f.open());
        f.write(u"Pomysł.md"_s, "cudzy");
        QCOMPARE(f.vault->create(u"Pomysł.md"_s, u"mój"_s), u"Pomysł 2.md"_s);
        QCOMPARE(f.read(u"Pomysł.md"_s), "cudzy"_ba);
    }

    void createAvoidsFoldersAndKeepsExtensionCase()
    {
        Fixture f;
        f.write(u"Projekt/a.md"_s, "a");
        f.write(u"Raport.PDF"_s, "\x01");
        QVERIFY(f.open());
        QCOMPARE(f.vault->create(u"Projekt"_s, u"x"_s), u"Projekt 2"_s);
        QCOMPARE(f.vault->createBinary(u"Raport.PDF"_s, "\x02"_ba), u"Raport 2.PDF"_s);
        QCOMPARE(f.vault->create(u"Szkic.Md"_s, u"a"_s), u"Szkic.Md"_s);
        QCOMPARE(f.vault->create(u"szkic.md"_s, u"b"_s), u"szkic 2.md"_s);
        QCOMPARE(f.vault->createBinary(u"assets/img.png"_s, "\x01"_ba), u"assets/img.png"_s);
        QCOMPARE(f.vault->createBinary(u"assets/img.png"_s, "\x02"_ba), u"assets/img 2.png"_s);
    }

    void createSeesNamesFreedOrTakenByOtherOperations()
    {
        Fixture f;
        f.write(u"Ala.md"_s, "a");
        f.write(u"F/x.md"_s, "x");
        QVERIFY(f.open());
        QVERIFY(f.vault->rename(u"Ala.md"_s, u"Ola.md"_s));
        QCOMPARE(f.vault->create(u"ala.md"_s, u"1"_s), u"ala.md"_s);
        QCOMPARE(f.vault->create(u"OLA.md"_s, u"2"_s), u"OLA 2.md"_s);
        QVERIFY(f.vault->remove(u"Ola.md"_s));
        QCOMPARE(f.vault->create(u"ola.md"_s, u"3"_s), u"ola.md"_s);
        QVERIFY(f.vault->rename(u"F"_s, u"G"_s));
        QCOMPARE(f.vault->create(u"f"_s, u"4"_s), u"f"_s);
        QCOMPARE(f.vault->create(u"g"_s, u"5"_s), u"g 2"_s);
        QVERIFY(removeBehind(f.root(), u"ala.md"_s));
        f.vault->rescan();
        QCOMPARE(f.vault->create(u"Ala.md"_s, u"6"_s), u"Ala.md"_s);
    }

    void createFolder()
    {
        Fixture f;
        QVERIFY(f.open());
        QVERIFY(f.vault->createFolder(u"Projekty/Dom/Łazienka"_s));
        QCOMPARE(f.folders(), (QStringList{u"Projekty"_s, u"Projekty/Dom"_s, u"Projekty/Dom/Łazienka"_s}));
        QVERIFY(QFileInfo(f.root() + u"/Projekty/Dom/Łazienka"_s).isDir());
        QVERIFY(f.vault->createFolder(u"Projekty"_s)); // exists already: fine
        QVERIFY(f.vault->writeText(u"plik.md"_s, u"x"_s));
        QVERIFY(!f.vault->createFolder(u"plik.md"_s));
        QVERIFY(!f.vault->createFolder(u"plik.md/podfolder"_s));
        QVERIFY(!f.vault->createFolder(QString()));
        QCOMPARE(f.changes(), QStringList{u"create:plik.md@user"_s});
    }

    // ---- trash ---------------------------------------------------------------------------
    void removeMovesToTheTrash()
    {
        Fixture f;
        f.write(u"Projekty/Dom.md"_s, "dom");
        QVERIFY(f.open());
        QCOMPARE(f.text(u"Projekty/Dom.md"_s), u"dom"_s);
        QVERIFY(f.vault->remove(u"Projekty/Dom.md"_s));
        QCOMPARE(f.changes(), QStringList{u"delete:Projekty/Dom.md@user"_s});
        QVERIFY(!f.vault->exists(u"Projekty/Dom.md"_s));
        QCOMPARE(f.read(u".trash/Projekty/Dom.md"_s), "dom"_ba);
        QCOMPARE(statPaths(f.vault->trash()), QStringList{u".trash/Projekty/Dom.md"_s});
        bool ok = true;
        f.vault->readText(u"Projekty/Dom.md"_s, &ok);
        QVERIFY(!ok);
        QCOMPARE(f.folders(), QStringList{u"Projekty"_s}); // the folder stays
    }

    void trashPathsGetAStampWhenTaken()
    {
        Fixture f;
        f.write(u"a.md"_s, "first");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"second"_s));
        QVERIFY(f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"third"_s));
        QVERIFY(f.vault->remove(u"a.md"_s));
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->writeText(u"a.md"_s, u"fourth"_s));
        QVERIFY(f.vault->remove(u"a.md"_s));
        QCOMPARE(f.read(u".trash/a.md"_s), "first"_ba);
        QCOMPARE(f.read(u".trash/a (20261014-101112).md"_s), "second"_ba);
        QCOMPARE(f.read(u".trash/a (20261014-101112) 2.md"_s), "third"_ba);
        QCOMPARE(f.read(u".trash/a (20261014-101113).md"_s), "fourth"_ba);
    }

    void removeFoldersRecursively()
    {
        Fixture f;
        f.write(u"P/a.md"_s, "a");
        f.write(u"P/sub/b.md"_s, "b");
        f.write(u"P2/c.md"_s, "c");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"P"_s));
        QCOMPARE(sorted(f.changes()), (QStringList{u"delete:P/a.md@user"_s, u"delete:P/sub/b.md@user"_s}));
        QCOMPARE(f.spy->count(), 1);
        QCOMPARE(f.folders(), QStringList{u"P2"_s});
        QCOMPARE(diskFiles(f.root()), (QStringList{u".trash/P/a.md"_s, u".trash/P/sub/b.md"_s, u"P2/c.md"_s}));
    }

    void removingSomethingMissing()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"missing.md"_s));
        QVERIFY(f.changes().isEmpty());
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QVERIFY(f.vault->remove(u"a.md"_s)); // stale cache entry: just forgotten
        QCOMPARE(f.changes(), QStringList{u"delete:a.md@user"_s});
        QVERIFY(!f.vault->exists(u"a.md"_s));
        QVERIFY(!f.vault->remove(QString()));
    }

    void restoreToTheOriginalPath()
    {
        Fixture f;
        f.write(u"Książki/Lalka.md"_s, "Prus");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"Książki/Lalka.md"_s));
        const TrashInfo info = f.vault->trashInfo(u".trash/Książki/Lalka.md"_s);
        QCOMPARE(info.original, u"Książki/Lalka.md"_s);
        QCOMPARE(info.deletedAt, f.clock.now);
        QCOMPARE(f.vault->restore(u".trash/Książki/Lalka.md"_s), u"Książki/Lalka.md"_s);
        QCOMPARE(f.text(u"Książki/Lalka.md"_s), u"Prus"_s);
        QCOMPARE(f.changes(), (QStringList{u"delete:Książki/Lalka.md@user"_s, u"create:Książki/Lalka.md@user"_s}));
        QVERIFY(f.vault->trash().isEmpty());
        QVERIFY(!QFileInfo::exists(f.root() + u"/.trash/Książki"_s)); // emptied trash folders go
        QVERIFY(f.vault->store()->keys(u"trash/"_s).isEmpty());
    }

    void restoreStampedCopiesToFreePaths()
    {
        Fixture f;
        f.write(u"a.md"_s, "one");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"two"_s));
        QVERIFY(f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->writeText(u"a.md"_s, u"three"_s));
        const QString stamped = u".trash/a (20261014-101112).md"_s;
        QCOMPARE(f.vault->trashInfo(stamped).original, u"a.md"_s);
        QCOMPARE(f.vault->restore(stamped), u"a 2.md"_s);
        QCOMPARE(f.text(u"a 2.md"_s), u"two"_s);
        QCOMPARE(f.vault->restore(u".trash/a.md"_s), u"a 3.md"_s);
        QCOMPARE(f.text(u"a 3.md"_s), u"one"_s);
    }

    void restoreFoldersAndFilesOfTrashedFolders()
    {
        Fixture f;
        f.write(u"F/a.md"_s, "a");
        f.write(u"F/b.md"_s, "b");
        f.write(u"G/c.md"_s, "c");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"F"_s));
        QVERIFY(f.vault->remove(u"G"_s));
        f.spy->clear();
        QCOMPARE(f.vault->trashInfo(u".trash/G/c.md"_s).original, u"G/c.md"_s); // from the folder's metadata
        QCOMPARE(f.vault->restore(u".trash/G/c.md"_s), u"G/c.md"_s);
        QCOMPARE(f.vault->restore(u".trash/F"_s), u"F"_s);
        QCOMPARE(sorted(f.changes()),
                 (QStringList{u"create:F/a.md@user"_s, u"create:F/b.md@user"_s, u"create:G/c.md@user"_s}));
        QCOMPARE(f.files(), (QStringList{u"F/a.md"_s, u"F/b.md"_s, u"G/c.md"_s}));
        QCOMPARE(f.folders(), (QStringList{u"F"_s, u"G"_s}));
    }

    void restoreWithoutMetadata()
    {
        // Deleted on another device: the original path comes from the trash path.
        Fixture f;
        const QString trashed = u".trash/Notatki (20261001-080000)/x (20261002-090000) 3.md"_s;
        f.write(trashed, "x");
        QVERIFY(f.open());
        const TrashInfo info = f.vault->trashInfo(trashed);
        QCOMPARE(info.original, u"Notatki/x.md"_s);
        QVERIFY(!info.deletedAt.isValid());
        QCOMPARE(f.vault->restore(trashed), u"Notatki/x.md"_s);
        QCOMPARE(f.read(u"Notatki/x.md"_s), "x"_ba);
    }

    void unusableTrashMetadataIsIgnored()
    {
        Fixture f;
        f.write(u".trash/x.md"_s, "x");
        f.write(u".trash/y.md"_s, "y");
        QVERIFY(f.open());
        f.vault->store()->set(u"trash/.trash/x.md"_s, QJsonObject{{u"original"_s, QString()}, {u"deletedAt"_s, 1}});
        f.vault->store()->set(u"trash/.trash/y.md"_s, QJsonObject{{u"original"_s, u".trash/z.md"_s}});
        QCOMPARE(f.vault->trashInfo(u".trash/x.md"_s).original, u"x.md"_s);
        QCOMPARE(f.vault->trashInfo(u".trash/y.md"_s).original, u"y.md"_s);
        QCOMPARE(f.vault->restore(u".trash/x.md"_s), u"x.md"_s);
    }

    void restoreRejectsBadPaths()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(f.vault->restore(u"a.md"_s).isEmpty());
        QVERIFY(!f.vault->errorString().isEmpty());
        QVERIFY(f.vault->restore(u".trash/missing.md"_s).isEmpty());
        QVERIFY(f.vault->restore(u".trash"_s).isEmpty());
        QVERIFY(f.vault->trashInfo(u"a.md"_s).original.isEmpty());
    }

    void deletingInsideTheTrashIsPermanent()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"a.md"_s));
        QVERIFY(f.vault->remove(u"b.md"_s));
        QVERIFY(f.vault->remove(u".trash/a.md"_s));
        QVERIFY(!QFileInfo::exists(f.root() + u"/.trash/a.md"_s));
        QCOMPARE(f.read(u".trash/b.md"_s), "b"_ba);
        QCOMPARE(f.vault->store()->keys(u"trash/"_s), QStringList{u"trash/.trash/b.md"_s});
        QVERIFY(f.vault->emptyTrash());
        QVERIFY(f.vault->trash().isEmpty());
        QVERIFY(f.vault->store()->keys(u"trash/"_s).isEmpty());
        QVERIFY(!QFileInfo::exists(f.root() + u"/.trash"_s));
        QCOMPARE(f.changes(), (QStringList{u"delete:a.md@user"_s, u"delete:b.md@user"_s}));
    }

    void removePermanently()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"F/b.md"_s, "b");
        QVERIFY(f.open());
        QVERIFY(f.vault->removePermanently(u"a.md"_s));
        QVERIFY(f.vault->removePermanently(u"F"_s));
        QVERIFY(diskFiles(f.root()).isEmpty());
        QCOMPARE(f.changes(), (QStringList{u"delete:a.md@user"_s, u"delete:F/b.md@user"_s}));
        QVERIFY(f.vault->trash().isEmpty());
        QVERIFY(f.folders().isEmpty());
    }

    void trashFolderNameTakenByATrashedFile()
    {
        Fixture f;
        f.write(u"Zakupy"_s, "plik bez rozszerzenia");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"Zakupy"_s));
        QCOMPARE(diskFiles(f.root()), QStringList{u".trash/Zakupy"_s});
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->writeText(u"Zakupy/Lista ż.md"_s, u"- masło"_s));
        QVERIFY(f.vault->remove(u"Zakupy/Lista ż.md"_s));
        QCOMPARE(diskFiles(f.root()),
                 (QStringList{u".trash/Zakupy"_s, u".trash/Zakupy (20261014-101113)/Lista ż.md"_s}));
        QVERIFY(f.files().isEmpty());
        QCOMPARE(f.vault->restore(u".trash/Zakupy (20261014-101113)/Lista ż.md"_s), u"Zakupy/Lista ż.md"_s);
        QCOMPARE(f.text(u"Zakupy/Lista ż.md"_s), u"- masło"_s);
    }

    void trashFolderNameTakenWithoutMetadata()
    {
        Fixture f;
        f.write(u"Projekt"_s, "plik");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"Projekt"_s));
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->writeText(u"Projekt/Etap 1/zadania.md"_s, u"- [ ] ćwiczenie"_s));
        QVERIFY(f.vault->remove(u"Projekt/Etap 1"_s));
        const QString trashed = u".trash/Projekt (20261014-101113)/Etap 1/zadania.md"_s;
        QCOMPARE(diskFiles(f.root()), (QStringList{u".trash/Projekt"_s, trashed}));
        f.vault->store()->clear();
        QCOMPARE(f.vault->restore(trashed), u"Projekt/Etap 1/zadania.md"_s);
    }

    void restoreIntoAFreeFolderWhenTheOriginalIsNowAFile()
    {
        Fixture f;
        f.write(u"Przepisy/Żurek.md"_s, "- zakwas");
        QVERIFY(f.open());
        QVERIFY(f.vault->remove(u"Przepisy"_s));
        QVERIFY(f.vault->writeText(u"Przepisy"_s, u"teraz to plik"_s));
        QCOMPARE(f.vault->restore(u".trash/Przepisy/Żurek.md"_s), u"Przepisy 2/Żurek.md"_s);
        QCOMPARE(diskFiles(f.root()), (QStringList{u"Przepisy"_s, u"Przepisy 2/Żurek.md"_s}));
        QCOMPARE(f.files(), (QStringList{u"Przepisy"_s, u"Przepisy 2/Żurek.md"_s}));
    }

    void deletingTheBrulionFolderResetsTheConfig()
    {
        Fixture f;
        f.write(u".brulion/config.json"_s, R"({"journalsFolder":"Dziennik"})");
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QCOMPARE(f.vault->config().journalsFolder, u"Dziennik"_s);
        QSignalSpy configSpy(f.vault.get(), &Vault::configChanged);
        QVERIFY(f.vault->remove(u".brulion"_s));
        QVERIFY(sameVaultConfig(f.vault->config(), VaultConfig{}));
        QVERIFY(!f.vault->exists(u".brulion/config.json"_s));
        QCOMPARE(f.changes(), QStringList{u"delete:.brulion/config.json@user"_s});
        QCOMPARE(configSpy.count(), 1);
    }

    // ---- rename --------------------------------------------------------------------------
    void renameAFileKeepsContentAndHistory()
    {
        Fixture f;
        f.write(u"stara.md"_s, "v0");
        QVERIFY(f.open());
        QVERIFY(f.vault->writeText(u"stara.md"_s, u"v1"_s));
        f.clock.advanceMs(1000);
        QVERIFY(f.vault->rename(u"stara.md"_s, u"Archiwum/nowa.md"_s));
        QCOMPARE(f.changes(), (QStringList{u"modify:stara.md@user"_s, u"rename:stara.md->Archiwum/nowa.md@user"_s}));
        QCOMPARE(f.text(u"Archiwum/nowa.md"_s), u"v1"_s);
        QCOMPARE(f.historyContents(u"Archiwum/nowa.md"_s), QStringList{u"v0"_s});
        QVERIFY(f.vault->history()->list(u"stara.md"_s).isEmpty());
        QVERIFY(!f.vault->exists(u"stara.md"_s));
        QVERIFY(f.vault->exists(u"Archiwum"_s));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void renameAFolderEmitsOneChangePerFile()
    {
        Fixture f;
        f.write(u"Projekty/a.md"_s, "a");
        f.write(u"Projekty/Dom/b.md"_s, "b");
        f.write(u"Projekty/img.png"_s, "\x01");
        f.write(u"Inne/c.md"_s, "c");
        QVERIFY(f.open());
        QVERIFY(f.vault->rename(u"Projekty"_s, u"Archiwum/Projekty 2025"_s));
        QCOMPARE(sorted(f.changes()), (QStringList{u"rename:Projekty/Dom/b.md->Archiwum/Projekty 2025/Dom/b.md@user"_s,
                                                   u"rename:Projekty/a.md->Archiwum/Projekty 2025/a.md@user"_s,
                                                   u"rename:Projekty/img.png->Archiwum/Projekty 2025/img.png@user"_s}));
        QCOMPARE(f.spy->count(), 1);
        QCOMPARE(f.folders(),
                 (QStringList{u"Archiwum"_s, u"Archiwum/Projekty 2025"_s, u"Archiwum/Projekty 2025/Dom"_s, u"Inne"_s}));
        QCOMPARE(f.text(u"Archiwum/Projekty 2025/Dom/b.md"_s), u"b"_s);
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void renameRefusals()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        f.write(u"b.md"_s, "b");
        f.write(u"F/x.md"_s, "x");
        QVERIFY(f.open());
        QVERIFY(!f.vault->rename(u"a.md"_s, u"b.md"_s));
        QVERIFY(!f.vault->errorString().isEmpty());
        QVERIFY(!f.vault->rename(u"a.md"_s, u"F"_s));
        QVERIFY(!f.vault->rename(u"zz.md"_s, u"y.md"_s));
        QVERIFY(!f.vault->rename(u"F"_s, u"F/G"_s));
        QVERIFY(!f.vault->rename(QString(), u"y.md"_s));
        QVERIFY(f.vault->rename(u"a.md"_s, u"./a.md"_s)); // same path
        f.write(u"c.md"_s, "cudzy");                      // unknown to the vault
        QVERIFY(!f.vault->rename(u"a.md"_s, u"c.md"_s));
        QVERIFY(!f.vault->rename(u"a.md"_s, u"b.md/x.md"_s)); // a file is in the way
        QCOMPARE(diskFiles(f.root()), (QStringList{u"F/x.md"_s, u"a.md"_s, u"b.md"_s, u"c.md"_s}));
        QCOMPARE(f.text(u"a.md"_s), u"a"_s);
        QVERIFY(f.changes().isEmpty());
    }

    void renameOntoAnotherSpellingIsRefused()
    {
        // "dom.md" next to "Dom.md": one file on case-insensitive storage, one page for links.
        Fixture f;
        f.write(u"Dom.md"_s, "dom");
        f.write(u"Inne.md"_s, "inne");
        f.write(u"Projekty/a.md"_s, "a");
        f.write(u"Stare/b.md"_s, "b");
        QVERIFY(f.open());
        QVERIFY(!f.vault->rename(u"Inne.md"_s, u"dom.md"_s));
        QVERIFY2(f.vault->errorString().contains(u"Dom.md"_s), qPrintable(f.vault->errorString()));
        QVERIFY(!f.vault->rename(u"Stare"_s, u"projekty"_s));
        QVERIFY(f.vault->rename(u"Inne.md"_s, u"inne.md"_s)); // its own spelling
        QCOMPARE(diskFiles(f.root()), (QStringList{u"Dom.md"_s, u"Projekty/a.md"_s, u"Stare/b.md"_s, u"inne.md"_s}));
    }

    void caseOnlyRenames()
    {
        Fixture f;
        f.write(u"notatka.md"_s, "n");
        f.write(u"zakupy/lista.md"_s, "x");
        QVERIFY(f.open());
        QVERIFY(f.vault->rename(u"notatka.md"_s, u"Notatka.md"_s));
        QVERIFY(f.vault->rename(u"zakupy"_s, u"Zakupy"_s));
        QCOMPARE(f.changes(), (QStringList{u"rename:notatka.md->Notatka.md@user"_s,
                                           u"rename:zakupy/lista.md->Zakupy/lista.md@user"_s}));
        QCOMPARE(diskFiles(f.root()), (QStringList{u"Notatka.md"_s, u"Zakupy/lista.md"_s}));
        QCOMPARE(f.folders(), QStringList{u"Zakupy"_s});
    }

    void caseOnlyRenameOntoADistinctFileIsRefused()
    {
        Fixture f;
        f.write(u"Notatka.md"_s, "duża");
        f.write(u"notatka.md"_s, "mała");
        f.write(u"Żaba.md"_s, "moja żaba");
        QVERIFY(f.open());
        QVERIFY(!f.vault->rename(u"Notatka.md"_s, u"notatka.md"_s));
        f.write(u"żaba.md"_s, "cudza żaba, dłuższa"); // unknown to the vault
        QVERIFY(!f.vault->rename(u"Żaba.md"_s, u"żaba.md"_s));
        QCOMPARE(f.read(u"Notatka.md"_s), "duża"_ba);
        QCOMPARE(f.read(u"notatka.md"_s), "mała"_ba);
        QCOMPARE(f.read(u"Żaba.md"_s), "moja żaba"_ba);
        QCOMPARE(f.read(u"żaba.md"_s), "cudza żaba, dłuższa"_ba);
        QCOMPARE(f.files(), (QStringList{u"Notatka.md"_s, u"notatka.md"_s, u"Żaba.md"_s}));
    }

    void movingIntoAnIgnoredFolderReportsADelete()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(f.vault->rename(u"a.md"_s, u".archive/../.obsidian/a.md"_s));
        QCOMPARE(f.changes(), QStringList{u"delete:a.md@user"_s});
        QCOMPARE(diskFiles(f.root()), QStringList{u".obsidian/a.md"_s});
    }

    void movingInFromAnIgnoredFolderReportsCreates()
    {
        Fixture f;
        f.write(u".stversions/Old/a.md"_s, "a");
        f.write(u".stversions/Old/b/c.md"_s, "c");
        QVERIFY(f.open());
        QVERIFY(f.vault->rename(u".stversions/Old"_s, u"Odzyskane"_s));
        QCOMPARE(sorted(f.changes()),
                 (QStringList{u"create:Odzyskane/a.md@user"_s, u"create:Odzyskane/b/c.md@user"_s}));
        QCOMPARE(f.folders(), (QStringList{u"Odzyskane"_s, u"Odzyskane/b"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    // ---- symlinks -------------------------------------------------------------------------
    void symlinkedFolders()
    {
        Fixture f;
        const QString outside = f.tmp.path() + u"/Zdjęcia"_s;
        writeBehind(outside, u"lato.png"_s, "\x01");
        writeBehind(outside, u"Góry/szczyt.png"_s, "\x02");
        QVERIFY(QFile::link(outside, outside + u"/Góry/pętla"_s));  // a loop outside the vault
        QVERIFY(QFile::link(f.tmp.path(), outside + u"/w górę"_s)); // contains the vault: a loop too
        f.write(u"Projekty/a.md"_s, "a");
        QVERIFY(QFile::link(outside, f.root() + u"/assets"_s));                  // followed
        QVERIFY(QFile::link(f.root() + u"/Projekty"_s, f.root() + u"/kopia"_s)); // into the vault: not
        QVERIFY(f.open());
        QCOMPARE(f.files(), (QStringList{u"Projekty/a.md"_s, u"assets/Góry/szczyt.png"_s, u"assets/lato.png"_s}));
        QCOMPARE(f.folders(), (QStringList{u"Projekty"_s, u"assets"_s, u"assets/Góry"_s}));
        QVERIFY(f.vault->writeBinary(u"assets/nowe.png"_s, "\x03"_ba));
        QVERIFY(QFileInfo::exists(outside + u"/nowe.png"_s));
        QVERIFY(!f.vault->writeText(u"kopia/b.md"_s, u"b"_s));
        QVERIFY(f.vault->rescan().isEmpty());
        QVERIFY(f.vault->removePermanently(u"assets"_s)); // removes the link, not the photos
        QVERIFY(QFileInfo::exists(outside + u"/lato.png"_s));
        QVERIFY(!QFileInfo(f.root() + u"/assets"_s).isSymLink());
        QCOMPARE(sorted(f.changes()),
                 (QStringList{u"create:assets/nowe.png@user"_s, u"delete:assets/Góry/szczyt.png@user"_s,
                              u"delete:assets/lato.png@user"_s, u"delete:assets/nowe.png@user"_s}));
        QVERIFY(f.vault->rescan().isEmpty());
    }

    void renameAStaleEntryFails()
    {
        Fixture f;
        f.write(u"a.md"_s, "a");
        QVERIFY(f.open());
        QVERIFY(removeBehind(f.root(), u"a.md"_s));
        QVERIFY(!f.vault->rename(u"a.md"_s, u"b.md"_s));
        QTRY_COMPARE(f.changes(), QStringList{u"delete:a.md@external"_s}); // reported soon after
    }
};

QTEST_GUILESS_MAIN(TestVault)
#include "tst_vault.moc"
