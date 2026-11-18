// The block editor's saves on the real Workspace (Vault::writeTextIfUnchanged through
// EditorHost::writeIfUnchanged): an agent's brulion-cli or Syncthing writes the page while an edit
// waits for its debounced save — the save merges that change instead of overwriting it, also
// before the vault's watcher has reported it.
#include "pagecontroller.h"
#include "workspace.h"

#include "backup/archive.h"
#include "index/vaultindex.h"
#include "vault/vault.h"

#include <QThread>

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

    void notebookReplaceSavesFirstAndOpenEditorsFollow()
    {
        const QString root = m_tmp.filePath(u"Zamiana"_s);
        QVERIFY(writeBehind(root + u"/Otwarta.md"_s, "- kot\n"));
        QVERIFY(writeBehind(root + u"/Inna.md"_s, "- kot\n"));
        Workspace ws;
        QVERIFY(ws.open(root, m_tmp.filePath(u"state-replace"_s)));
        ws.finishIndexing();

        PageController page;
        page.setSaveDelay(10000); // an edit waits for its debounced save
        page.setHost(&ws);
        page.setPath(u"Otwarta.md"_s);
        page.setEditText(0, u"kot napisany przed chwilą"_s, 3);
        QVERIFY(page.dirty());

        auto *session = ws.replacing();
        QSignalSpy applied(session, &brulion::shell::ReplaceSession::applied);
        QSignalSpy undone(session, &brulion::shell::ReplaceSession::undone);
        session->setQuery(u"kot"_s);
        session->setReplacement(u"pies"_s);
        session->scanNow();
        QVERIFY(!page.dirty()); // saved before the plan looked at the file
        QTRY_VERIFY(!session->scanning() && session->fileCount() == 2);
        QVERIFY(session->apply());
        QTRY_COMPARE(applied.size(), 1);
        QCOMPARE(readRaw(root + u"/Otwarta.md"_s), QStringLiteral(u"- pies napisany przed chwilą\n").toUtf8());
        QTRY_COMPARE(page.text(), u"- pies napisany przed chwilą\n"_s); // the editor follows the file
        QVERIFY(!page.dirty());

        QVERIFY(session->undo(applied.first().at(0).toInt()));
        QTRY_COMPARE(undone.size(), 1);
        QTRY_COMPARE(page.text(), u"- kot napisany przed chwilą\n"_s);
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

    // --- background saves (Vault::writeTextIfUnchangedAsync through the Workspace) --------------

    // The autosave runs on the notebook's writer: the file, the index (the note parsed there) and
    // the changes as before — Modified by the user, never External, no "external" snapshot.
    void autosaveRunsOnTheWriter()
    {
        const QString root = m_tmp.filePath(u"TłoZapis"_s);
        QVERIFY(writeBehind(root + u"/Dziennik.md"_s, "- rano\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        QSignalSpy changes(ws.vault(), &brulion::Vault::changed);
        PageController editor;
        editor.setSaveDelay(20);
        editor.setHost(&ws);
        editor.setPath(u"Dziennik.md"_s);
        for (int i = 1; i <= 4; ++i) {
            editor.setEditText(0, u"rano #kawa ąę %1"_s.arg(i), 15);
            QTRY_VERIFY(!editor.dirty() && !editor.writeInFlight());
        }
        QCOMPARE(readRaw(root + u"/Dziennik.md"_s), QByteArray("- rano #kawa ąę 4\n"));
        QTest::qWait(300); // the watcher's echoes
        ws.vault()->processPendingEvents();
        for (const QList<QVariant> &args : std::as_const(changes)) {
            for (const brulion::VaultChange &change : args.at(0).value<QList<brulion::VaultChange>>())
                QCOMPARE(change.source, brulion::ChangeSource::User);
        }
        const brulion::ParsedNote *note = ws.index()->note(u"Dziennik.md"_s);
        QVERIFY(note);
        QCOMPARE(ws.index()->content(u"Dziennik.md"_s), u"- rano #kawa ąę 4\n"_s);
        for (const brulion::HistoryEntry &entry : ws.vault()->history()->list(u"Dziennik.md"_s))
            QVERIFY(entry.reason != u"external");
    }

    // A rename or a deletion while a save is in flight: the save lands first, the old name never
    // comes back, the trash holds the newest text, the editor follows.
    void renameOrDeleteDuringABackgroundSave_data()
    {
        QTest::addColumn<bool>("rename");
        QTest::newRow("rename") << true;
        QTest::newRow("delete") << false;
    }
    void renameOrDeleteDuringABackgroundSave()
    {
        QFETCH(bool, rename);
        const QString root = m_tmp.filePath(rename ? u"TłoNazwa"_s : u"TłoKosz"_s);
        QVERIFY(writeBehind(root + u"/Stara.md"_s, "- tekst\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        ws.vault()->setBeforeRenameHook([](const brulion::VaultPath &) { QThread::msleep(150); });
        PageController editor;
        editor.setSaveDelay(100000);
        editor.setHost(&ws);
        editor.setPath(u"Stara.md"_s);
        QObject::connect(&ws, &Workspace::pathRenamed, &editor, &PageController::followRename);
        editor.setEditText(0, u"tekst najnowszy żółw"_s, 20);
        editor.saveInBackground();
        QVERIFY(editor.writeInFlight());
        if (rename) {
            QCOMPARE(ws.renamePage(u"Stara.md"_s, u"Nowa"_s), u"Nowa.md"_s);
            QVERIFY(!QFileInfo::exists(root + u"/Stara.md"_s));
            QCOMPARE(readRaw(root + u"/Nowa.md"_s), QByteArray("- tekst najnowszy żółw\n"));
            QCOMPARE(editor.path(), u"Nowa.md"_s);
            QVERIFY(!editor.dirty());
        } else {
            QVERIFY(!ws.deletePage(u"Stara.md"_s).isEmpty());
            QVERIFY(!QFileInfo::exists(root + u"/Stara.md"_s));
            QCOMPARE(readRaw(root + u"/.trash/Stara.md"_s), QByteArray("- tekst najnowszy żółw\n"));
        }
        QVERIFY(!editor.writeInFlight());
        ws.vault()->setBeforeRenameHook({});
        QTest::qWait(200);
        QVERIFY(!QFileInfo::exists(root + u"/Stara.md"_s));
    }

    // A dataview query that stats the page being saved in the background (file.mtime): the stat
    // sees the write, the index the query holds does not change under it (its report comes from
    // the event loop; under the sanitizers a merge inside the query was a use after free).
    void dataviewDuringABackgroundSave()
    {
        const QString root = m_tmp.filePath(u"TłoDataview"_s);
        QVERIFY(writeBehind(root + u"/Dziennik.md"_s, "- rano #kawa\n"));
        QVERIFY(writeBehind(root + u"/Inna.md"_s, "- wieczór #kawa\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        ws.vault()->setBeforeRenameHook([](const brulion::VaultPath &) { QThread::msleep(150); });
        PageController editor;
        editor.setSaveDelay(100000);
        editor.setHost(&ws);
        editor.setPath(u"Dziennik.md"_s);
        editor.setEditText(0, u"rano #kawa źdźbło"_s, 17);
        editor.saveInBackground();
        QVERIFY(editor.writeInFlight());
        const quint64 version = ws.index()->version();
        const QVariantMap result = ws.runDataview(u"TABLE file.mtime, file.name FROM #kawa SORT file.name"_s, {});
        QVERIFY2(result.value(u"ok"_s).toBool(), qPrintable(result.value(u"error"_s).toString()));
        QCOMPARE(result.value(u"total"_s).toInt(), 2);
        QCOMPARE(ws.index()->version(), version); // (nothing merged into the index inside the query)
        QCOMPARE(readRaw(root + u"/Dziennik.md"_s), QByteArray("- rano #kawa źdźbło\n"));
        QTRY_VERIFY(!editor.writeInFlight());
        QVERIFY(!editor.dirty());
        QCOMPARE(ws.index()->content(u"Dziennik.md"_s), u"- rano #kawa źdźbło\n"_s);
        ws.vault()->setBeforeRenameHook({});
    }

    // Closing the notebook with a save in flight: it lands, and is reported while all listen.
    void closeWithABackgroundSaveInFlight()
    {
        const QString root = m_tmp.filePath(u"TłoZamknięcie"_s);
        QVERIFY(writeBehind(root + u"/a.md"_s, "- a\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        ws.vault()->setBeforeRenameHook([](const brulion::VaultPath &) { QThread::msleep(100); });
        auto editor = std::make_unique<PageController>();
        editor->setSaveDelay(100000);
        editor->setHost(&ws);
        editor->setPath(u"a.md"_s);
        editor->setEditText(0, u"a ostatnie"_s, 10);
        editor->saveInBackground();
        QSignalSpy saved(editor.get(), &PageController::saved);
        ws.close();
        QCOMPARE(saved.size(), 1);
        QVERIFY(!editor->dirty());
        QCOMPARE(readRaw(root + u"/a.md"_s), QByteArray("- a ostatnie\n"));
    }

    // The async variant reports another program's change as the synchronous one does.
    void backgroundSaveMergesAnUnreportedChange()
    {
        const QString root = m_tmp.filePath(u"TłoScalanie"_s);
        QVERIFY(writeBehind(root + u"/a.md"_s, "- a\n- b\n"));
        Workspace ws;
        ws.sync()->setSyncthingDiscovery(false);
        QVERIFY(ws.open(root, root + u"-state"_s));
        ws.finishIndexing();
        ws.vault()->stopWatching(); // (the other program's write stays unreported)
        PageController editor;
        editor.setSaveDelay(100000);
        editor.setHost(&ws);
        editor.setPath(u"a.md"_s);
        editor.setEditText(0, u"a lokalnie"_s, 10);
        QVERIFY(writeBehind(root + u"/a.md"_s, "- a\n- b\n- od agenta\n"));
        editor.saveInBackground();
        QTRY_VERIFY(!editor.dirty() && !editor.writeInFlight());
#ifndef BRULION_TEST_NO_SYNC
        if (!editor.conflict())
            QCOMPARE(readRaw(root + u"/a.md"_s), QByteArray("- a lokalnie\n- b\n- od agenta\n"));
#endif
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
