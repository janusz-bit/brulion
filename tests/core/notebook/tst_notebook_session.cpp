// Notebook (notebook/session.h): a missing folder is never created and a file is refused; the
// index is built on first use and follows the vault; edits are intents applied to the current
// text — retried when another writer changes the file between reading and writing, a conflict
// after the last attempt —, recorded in History with the reason "cli", refused on a wrong
// expected hash or invalid UTF-8, and a dry run writes nothing (a new file starts from its initial
// text in both); the write lock keeps a second writer out.
#include "notebook/session.h"

#include "index/vaultindex.h"
#include "notebook/journals.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::notebook;

namespace {

void writeBehind(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    QVERIFY(f.commit());
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Notebook::WriteOptions opts(const QString &expectHash = {}, bool dryRun = false, bool mayCreate = false,
                            int attempts = 3)
{
    Notebook::WriteOptions o;
    o.expectHash = expectHash;
    o.dryRun = dryRun;
    o.mayCreate = mayCreate;
    o.attempts = attempts;
    return o;
}

/// An edit that appends a block (what `journal append` does).
Notebook::Edit append(const QString &block)
{
    return [block](const QString &current, bool, QString *next) {
        *next = notebook::appendBlock(current, block);
        return true;
    };
}

} // namespace

class TestNotebookSession : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;
    int m_n = 0;

    QString vaultDir(const QHash<QString, QByteArray> &files)
    {
        const QString dir = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(dir);
        for (auto it = files.cbegin(); it != files.cend(); ++it)
            writeBehind(dir + u'/' + it.key(), it.value());
        return dir;
    }
    Notebook::Options options() { return {m_tmp.filePath(u"state%1"_s.arg(m_n)), false, u"cli"_s, {}}; }

private Q_SLOTS:
    void missingFolderIsNotCreated()
    {
        Notebook nb;
        QString error;
        Notebook::OpenError kind = Notebook::OpenError::None;
        const QString missing = m_tmp.filePath(u"brak/Notatki"_s);
        QVERIFY(!nb.open(missing, options(), &error, &kind));
        QCOMPARE(kind, Notebook::OpenError::Missing);
        QVERIFY(!QFileInfo::exists(missing));
        QVERIFY(!QFileInfo::exists(m_tmp.filePath(u"brak"_s)));
        QVERIFY(!error.isEmpty());
        QVERIFY(!nb.open(QString(), options(), &error, &kind));
        QCOMPARE(kind, Notebook::OpenError::Missing);
        // A file is no notebook.
        writeBehind(m_tmp.filePath(u"plik.md"_s), "x");
        QVERIFY(!nb.open(m_tmp.filePath(u"plik.md"_s), options(), &error, &kind));
        QCOMPARE(kind, Notebook::OpenError::NotAFolder);
        QVERIFY(!nb.isOpen());
    }

    void lazyIndexFollowsTheVault()
    {
        const QString dir = vaultDir({{u"Dom.md"_s, "- [[Ogród]] #żółw\n"}, {u"Ogród.md"_s, "x"}});
        Notebook nb;
        QVERIFY(nb.open(dir, options()));
        QVERIFY(!nb.hasIndex());
        QCOMPARE(nb.index().backlinks(u"Ogród.md"_s).size(), 1);
        QVERIFY(nb.hasIndex());
        const auto r = nb.edit(u"Nowa.md"_s, append(u"- [[Ogród]]"_s), opts({}, false, true));
        QCOMPARE(r.status, Notebook::WriteResult::Written);
        QVERIFY(r.created);
        QCOMPARE(nb.index().backlinks(u"Ogród.md"_s).size(), 2); // the vault's change reached the index
        QCOMPARE(nb.index().search(u"zolw"_s).size(), 1);
    }

    void editKeepsBytesAndHistory()
    {
        const QString dir = vaultDir({{u"journals/2026-10-14.md"_s, "\xEF\xBB\xBF- rano\r\n"}});
        Notebook nb;
        QVERIFY(nb.open(dir, options()));
        const Notebook::Current before = nb.read(u"journals/2026-10-14.md"_s);
        QVERIFY(before.exists);
        QCOMPARE(before.text, u"- rano\r\n"_s);
        QCOMPARE(before.hash, contentHash("\xEF\xBB\xBF- rano\r\n"));
        const auto r = nb.edit(u"journals/2026-10-14.md"_s, append(u"- [ ] Zadzwonić do Łukasza"_s), opts());
        QCOMPARE(r.status, Notebook::WriteResult::Written);
        QCOMPARE(r.hashBefore, before.hash);
        QCOMPARE(readRaw(dir + u"/journals/2026-10-14.md"_s),
                 "\xEF\xBB\xBF- rano\r\n- [ ] Zadzwonić do Łukasza\r\n"_ba);
        QCOMPARE(r.hashAfter, contentHash(readRaw(dir + u"/journals/2026-10-14.md"_s)));
        // Every replaced text is kept, with the reason "cli" (no five-minute throttle).
        const auto again = nb.edit(u"journals/2026-10-14.md"_s, append(u"- drugi"_s), opts());
        QCOMPARE(again.status, Notebook::WriteResult::Written);
        const QList<HistoryEntry> versions = nb.vault().history()->list(u"journals/2026-10-14.md"_s);
        QCOMPARE(versions.size(), 2);
        QCOMPARE(versions.first().reason, u"cli"_s);
        QCOMPARE(versions.last().reason, u"cli"_s);
        QCOMPARE(nb.vault().history()->readBytes(versions.last().id), "\xEF\xBB\xBF- rano\r\n"_ba);
        // Unchanged: nothing written.
        const auto same = nb.edit(
                u"journals/2026-10-14.md"_s,
                [](const QString &c, bool, QString *n) {
                    *n = c;
                    return true;
                },
                opts());
        QCOMPARE(same.status, Notebook::WriteResult::Unchanged);
        QCOMPARE(nb.vault().history()->list(u"journals/2026-10-14.md"_s).size(), 2);
    }

    // A missing file created by an edit starts from initialText (a daily note's template): the
    // dry run shows what the real run writes; an existing file never gets it.
    void initialTextOfANewFile()
    {
        const QString dir = vaultDir({});
        Notebook nb;
        QVERIFY(nb.open(dir, options()));
        const QString path = u"journals/2026-10-14.md"_s;
        Notebook::WriteOptions o = opts({}, true, true);
        o.initialText = u"## środa\n- \n"_s;
        const auto dry = nb.edit(path, append(u"- żółw"_s), o);
        QCOMPARE(dry.status, Notebook::WriteResult::DryRun);
        QVERIFY(dry.created);
        QCOMPARE(dry.before, QString());
        QCOMPARE(dry.after, u"## środa\n- żółw\n"_s);
        QVERIFY(!QFile::exists(dir + u'/' + path));
        o.dryRun = false;
        const auto real = nb.edit(path, append(u"- żółw"_s), o);
        QCOMPARE(real.status, Notebook::WriteResult::Written);
        QVERIFY(real.created);
        QCOMPARE(real.after, dry.after);
        QCOMPARE(readRaw(dir + u'/' + path), "## środa\n- żółw\n"_ba);
        const auto again = nb.edit(path, append(u"- kot"_s), o);
        QVERIFY(!again.created);
        QCOMPARE(readRaw(dir + u'/' + path), "## środa\n- żółw\n- kot\n"_ba);
    }

    void retriesWhenAnotherWriterChangesTheFile()
    {
        const QString dir = vaultDir({{u"a.md"_s, "- 1\n"}});
        Notebook nb;
        QVERIFY(nb.open(dir, options()));
        // Between reading and writing another program appends (twice): the intent is applied to
        // its text each time and nothing is lost.
        int calls = 0;
        nb.setBeforeWrite([&](const VaultPath &, int attempt) {
            ++calls;
            if (attempt < 2)
                writeBehind(dir + u"/a.md"_s,
                            readRaw(dir + u"/a.md"_s) + "- z Syncthinga " + QByteArray::number(attempt) + '\n');
        });
        const auto r = nb.edit(u"a.md"_s, append(u"- od agenta"_s), opts());
        QCOMPARE(r.status, Notebook::WriteResult::Written);
        QCOMPARE(calls, 3);
        QCOMPARE(readRaw(dir + u"/a.md"_s), "- 1\n- z Syncthinga 0\n- z Syncthinga 1\n- od agenta\n"_ba);
        // A writer that never stops: a conflict, the file left to it.
        nb.setBeforeWrite([&](const VaultPath &, int attempt) {
            writeBehind(dir + u"/a.md"_s, "- inny " + QByteArray::number(attempt) + '\n');
        });
        const auto conflict = nb.edit(u"a.md"_s, append(u"- moje"_s), opts({}, false, false, 3));
        QCOMPARE(conflict.status, Notebook::WriteResult::Conflict);
        QCOMPARE(readRaw(dir + u"/a.md"_s), "- inny 2\n"_ba);
    }

    void refusals()
    {
        const QString dir = vaultDir({{u"a.md"_s, "- a\n"}, {u"zly.md"_s, "\xFF\xFE- zły\n"}});
        Notebook nb;
        QVERIFY(nb.open(dir, options()));
        const QString hash = nb.read(u"a.md"_s).hash;
        // A wrong expected hash.
        auto r = nb.edit(u"a.md"_s, append(u"- b"_s), opts(u"0000000000000000"_s));
        QCOMPARE(r.status, Notebook::WriteResult::HashMismatch);
        r = nb.edit(u"a.md"_s, append(u"- b"_s), opts(hash));
        QCOMPARE(r.status, Notebook::WriteResult::Written);
        // A dry run computes, writes nothing.
        const QByteArray now = readRaw(dir + u"/a.md"_s);
        r = nb.edit(u"a.md"_s, append(u"- c"_s), opts({}, true));
        QCOMPARE(r.status, Notebook::WriteResult::DryRun);
        QCOMPARE(r.after, u"- a\n- b\n- c\n"_s);
        QCOMPARE(readRaw(dir + u"/a.md"_s), now);
        // Missing and not allowed to create.
        QCOMPARE(nb.edit(u"brak.md"_s, append(u"- x"_s), opts()).status, Notebook::WriteResult::NotFound);
        QVERIFY(!QFile::exists(dir + u"/brak.md"_s));
        // Not UTF-8: rewriting would change bytes nobody touched.
        QCOMPARE(nb.edit(u"zly.md"_s, append(u"- x"_s), opts()).status, Notebook::WriteResult::Encoding);
        QCOMPARE(readRaw(dir + u"/zly.md"_s), "\xFF\xFE- zły\n"_ba);
        QVERIFY(!nb.read(u"zly.md"_s).validUtf8);
        // The edit itself refuses.
        QCOMPARE(nb.edit(
                           u"a.md"_s, [](const QString &, bool, QString *) { return false; }, opts())
                         .status,
                 Notebook::WriteResult::Refused);
    }

    void writeLock()
    {
        const QString dir = vaultDir({});
        Notebook a;
        QVERIFY(a.open(dir, options()));
        QString error;
        auto lock = a.lock(1000, &error);
        QVERIFY2(lock, qPrintable(error));
        QVERIFY(QFile::exists(QDir(a.stateDir()).filePath(u"cli.lock"_s)));
        Notebook b;
        QVERIFY(b.open(dir, options()));
        QVERIFY(!b.lock(50, &error));
        QVERIFY(error.contains(u"cli.lock"_s));
        lock.reset();
        QVERIFY(b.lock(1000));
        // Without a state folder the lock is in the temp folder, named by the vault.
        Notebook c;
        QVERIFY(c.open(dir, {QString(), false, u"cli"_s, {}}));
        QVERIFY(c.lock(1000));
    }

    void deviceStatePathIsTheApps()
    {
        const QString dir = vaultDir({});
        QCOMPARE(deviceStatePath(u"/home/ala/.local/share/Brulion/Brulion"_s, dir),
                 u"/home/ala/.local/share/Brulion/Brulion/vaults/"_s + Vault::idForPath(dir));
        QCOMPARE(decodeText("\xEF\xBB\xBFżółw"), u"żółw"_s);
    }
};

QTEST_GUILESS_MAIN(TestNotebookSession)
#include "tst_notebook_session.moc"
