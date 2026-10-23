// The block editor's saves on the real Workspace (Vault::writeTextIfUnchanged through
// EditorHost::writeIfUnchanged): an agent's brulion-cli or Syncthing writes the page while an edit
// waits for its debounced save — the save merges that change instead of overwriting it, also
// before the vault's watcher has reported it.
#include "pagecontroller.h"
#include "workspace.h"

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

/// Write like another program (atomic replace, as Syncthing and brulion-cli do).
bool writeBehind(const QString &file, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size() && f.commit();
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
