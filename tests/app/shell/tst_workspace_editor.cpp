// The block editor's saves on the real Workspace (Vault::writeTextIfUnchanged through
// EditorHost::writeIfUnchanged): an agent's brulion-cli or Syncthing writes the page while an edit
// waits for its debounced save — the save merges that change instead of overwriting it, also
// before the vault's watcher has reported it.
#include "pagecontroller.h"
#include "workspace.h"

#include "backup/archive.h"
#include "vault/vault.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::PageController;
using brulion::shell::Workspace;

namespace {

/// Write like another program (atomic replace, as Syncthing and brulion-cli do). Windows may refuse
/// the replace for a moment while something else has the file open (a virus scanner after the
/// app's read): retried, as those programs retry, and the reason logged when it stays refused.
bool writeBehind(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QString error;
    for (int attempt = 0; attempt < 20; ++attempt) {
        QSaveFile f(file);
        if (f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit())
            return true;
        error = f.errorString();
        if (attempt == 0) // (the reason in CI's log even when a retry succeeds)
            qInfo("writeBehind %s refused, retrying: %s", qPrintable(file), qPrintable(error));
        QTest::qWait(50);
    }
    qWarning("writeBehind %s: %s", qPrintable(file), qPrintable(error));
    return false;
}

QByteArray readRaw(const QString &file)
{
    QFile f(file);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestWorkspaceEditor : public QObject {
    Q_OBJECT
    QTemporaryDir m_tmp;

private Q_SLOTS:
    void aRestoreSavesOpenEditorsFirstAndDoesNotBringATrashedPageBack()
    {
        const QString root = m_tmp.filePath(u"Odtwarzane"_s);
        QVERIFY(writeBehind(root + u"/Stała.md"_s, "- stała\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        auto *backups = ws.backups();
        QVERIFY(backups->createNow());
        QTRY_VERIFY(!backups->running());
        QTRY_COMPARE(backups->backups().size(), 1);
        const QString source = backups->backups().first().toMap().value(u"name"_s).toString();

        // A page the backup does not hold, with edits waiting for their debounced save.
        QVERIFY(ws.writeFile(u"Późniejsza.md"_s, u"- zapisane\n"_s));
        auto editor = std::make_unique<PageController>();
        editor->setSaveDelay(100000);
        editor->setHost(&ws);
        editor->setPath(u"Późniejsza.md"_s);
        editor->setEditText(0, u"napisane, niezapisane"_s, 21);
        const QString typed = editor->text();
        QVERIFY(typed.contains(u"niezapisane"_s));
        QVERIFY(editor->dirty());

        QSignalSpy planned(backups, &brulion::shell::WorkspaceBackups::restorePlanReady);
        QTest::qWait(1100); // another second in the safety backup's name
        QVERIFY(backups->prepareRestore(source));
        QVERIFY(planned.wait(30000));
        QSignalSpy written(backups, &brulion::shell::WorkspaceBackups::backupWritten);
        backups->confirmRestore();
        // Typed on while the restore goes on: this page is about to go, and must stay gone.
        editor->setEditText(0, u"pisane dalej"_s, 12);
        QVERIFY(editor->dirty());
        QTRY_VERIFY_WITH_TIMEOUT(!backups->running(), 30000);

        // The typed text reached the safety backup, the page is gone, and nothing brings it back.
        QCOMPARE(written.size(), 1);
        brulion::backup::ArchiveReader reader(QDir(backups->folder()).filePath(written.first().first().toString()));
        const brulion::backup::ArchiveInfo info = reader.readEntries();
        QVERIFY2(info.ok(), qPrintable(info.detail));
        bool found = false;
        for (const brulion::backup::ArchiveEntry &entry : info.entries) {
            if (entry.path == u"Późniejsza.md") {
                found = true;
                QCOMPARE(QString::fromUtf8(*reader.read(entry)), typed);
            }
        }
        QVERIFY(found);
        QVERIFY(!QFileInfo::exists(root + u"/Późniejsza.md"_s));
        QVERIFY(!editor->dirty());
        editor.reset(); // ~PageController flushes
        QTest::qWait(10);
        QVERIFY(!QFileInfo::exists(root + u"/Późniejsza.md"_s));
    }

    void aPageDeletedByAnotherProgramStillKeepsItsEdits()
    {
        // Not a restore: as before, an edit of a page deleted elsewhere is written back.
        const QString root = m_tmp.filePath(u"Usuwane"_s);
        QVERIFY(writeBehind(root + u"/Strona.md"_s, "- a\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        {
            PageController editor;
            editor.setSaveDelay(100000);
            editor.setHost(&ws);
            editor.setPath(u"Strona.md"_s);
            editor.setEditText(0, u"edycja"_s, 6);
            QVERIFY(QFile::remove(root + u"/Strona.md"_s));
            ws.vault()->rescan();
            QVERIFY(editor.dirty());
        } // ~PageController flushes
        QVERIFY(QFileInfo::exists(root + u"/Strona.md"_s));
    }

    void externalAppendDuringADebouncedSaveIsKept()
    {
        const QString root = m_tmp.filePath(u"Notatki"_s);
        QVERIFY(writeBehind(root + u"/journals/2026-10-14.md"_s, "- rano\r\n- [ ] Kupić chleb\r\n"));
        Workspace ws;
        ws.setClock([] { return QDateTime(QDate(2026, 10, 14), QTime(10, 30)); });
        QVERIFY(ws.open(root, m_tmp.filePath(u"state"_s)));
        ws.finishIndexing();

        PageController page;
        page.setSaveDelay(10000); // the debounced save has not fired yet
        page.setHost(&ws);
        page.setPath(u"journals/2026-10-14.md"_s);
        QCOMPARE(page.text(), u"- rano\r\n- [ ] Kupić chleb\r\n"_s);
        page.setEditText(0, u"rano: kawa ☕"_s, 13);
        QVERIFY(page.dirty());

        // brulion-cli appends to today's journal; the watcher event is still on its way.
        QVERIFY(writeBehind(root + u"/journals/2026-10-14.md"_s,
                            "- rano\r\n- [ ] Kupić chleb\r\n- [ ] Zadzwonić do Łukasza ⏰ 2026-10-15 09:00\r\n"));
        QSignalSpy merged(&page, &PageController::externallyChanged);
        QVERIFY(page.save());
        QCOMPARE(merged.size(), 1);
        const QByteArray expected =
                "- rano: kawa ☕\r\n- [ ] Kupić chleb\r\n- [ ] Zadzwonić do Łukasza ⏰ 2026-10-15 09:00\r\n";
        QCOMPARE(readRaw(root + u"/journals/2026-10-14.md"_s), expected);
        QCOMPARE(page.text(), QString::fromUtf8(expected));
        QVERIFY(!page.dirty());
        // Nothing lost in between: the agent's version is in the history too.
        bool agentVersion = false;
        for (const QVariant &v : ws.history(u"journals/2026-10-14.md"_s)) {
            if (ws.historyText(v.toMap().value(u"id"_s).toString()).contains(u"Łukasza"_s))
                agentVersion = true;
        }
        QVERIFY(agentVersion);
    }

    void writeIfUnchangedReportsTheChange()
    {
        const QString root = m_tmp.filePath(u"Inne"_s);
        QVERIFY(writeBehind(root + u"/a.md"_s, "v0\n"));
        Workspace ws;
        QVERIFY(ws.open(root, QString()));
        ws.finishIndexing();
        QCOMPARE(ws.read(u"a.md"_s), u"v0\n"_s);
        QSignalSpy changed(&ws, &brulion::EditorHost::fileChanged);
        QVERIFY(writeBehind(root + u"/a.md"_s, "v0\n- żółw\n"));
        QCOMPARE(ws.writeIfUnchanged(u"a.md"_s, u"v1\n"_s, u"v0\n"_s), brulion::EditorHost::WriteResult::Changed);
        QCOMPARE(readRaw(root + u"/a.md"_s), "v0\n- żółw\n"_ba);
        QVERIFY(changed.contains(QVariantList{u"a.md"_s}));
        QCOMPARE(ws.writeIfUnchanged(u"a.md"_s, u"v1\n"_s, u"v0\n- żółw\n"_s),
                 brulion::EditorHost::WriteResult::Written);
        QCOMPARE(readRaw(root + u"/a.md"_s), "v1\n"_ba);
    }
};

QTEST_MAIN(TestWorkspaceEditor)
#include "tst_workspace_editor.moc"
