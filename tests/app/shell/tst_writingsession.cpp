// The editor's AI assistant on the real Workspace (WritingSession, docs/UI.md "AI in the editor"),
// an OpenAI-compatible server played (../fakeaiserver.h): a rewrite of part of a block's text
// streamed — the result cleaned as it comes, the change word by word, where it goes (change()) —
// and put into the block with PageController.pasteAndEdit as one undo step; "Rewrite…" without a
// preset asks nothing until one is chosen; a continuation and a summary of the selected blocks
// (insertAfterBlock, one undo step, at the block's depth); source mode's paragraph at the caret;
// Try again, Stop (what came is kept), a failure (no model set up, the server's error) and the
// request counted in aiPending.
#include "../fakeaiserver.h"

#include "pagecontroller.h"
#include "workspace.h"
#include "workspaceextensions.h"
#include "writingsession.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::PageController;
using brulion::shell::Workspace;
using brulion::shell::WritingSession;

class TestWritingSession : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;
    int m_n = 0;

    std::unique_ptr<Workspace> open(const QString &page, const QString &content, const FakeAiServer &server)
    {
        const QString root = m_tmp.filePath(u"v%1"_s.arg(++m_n));
        QDir().mkpath(root);
        QFile f(root + u'/' + page);
        if (f.open(QIODevice::WriteOnly))
            f.write(content.toUtf8());
        f.close();
        auto ws = std::make_unique<Workspace>();
        ws->setUiLanguage(u"pl"_s);
        if (!ws->open(root, root + u"-state"_s))
            return nullptr;
        ws->finishIndexing();
        brulion::ai::Settings settings;
        settings.provider = u"openai"_s;
        settings.baseUrl = server.url();
        settings.model = u"fake"_s;
        ws->extensions()->setAi(settings);
        return ws;
    }

private Q_SLOTS:
    void initTestCase() { QVERIFY(m_tmp.isValid()); }

    void aRewriteOfPartOfABlock()
    {
        FakeAiServer server;
        server.answers = {u"```markdown\nżółtego kota\n```"_s};
        auto ws = open(u"Dom.md"_s, u"- Ala ma bardzo dużego kota i psa.\n- Drugi\n"_s, server);
        QVERIFY(ws);
        PageController editor;
        editor.setHost(ws.get());
        editor.setPath(u"Dom.md"_s);
        QTRY_VERIFY(editor.loaded());
        const QString text = u"Ala ma bardzo dużego kota i psa."_s;
        WritingSession session;
        session.setWorkspace(ws.get());
        QSignalSpy results(&session, &WritingSession::resultChanged);
        // "bardzo dużego kota" (7..25), Clearer.
        QVERIFY(session.startInline(u"rewrite"_s, u"clearer"_s, 0, text, 7, 25, u"Dom"_s));
        QCOMPARE(session.state(), u"waiting"_s);
        QCOMPARE(session.target(), u"inline"_s);
        QCOMPARE(session.instruction(), session.presetLabel(u"clearer"_s));
        QTRY_COMPARE(session.state(), u"done"_s);
        QVERIFY(results.size() >= 1);
        QCOMPARE(session.result(), u"żółtego kota"_s); // (the fence around it gone)
        QCOMPARE(server.requests.size(), 1);
        const QString asked = FakeAiServer::lastUserText(server.requests.first());
        QVERIFY2(asked.contains(u"<text>\nbardzo dużego kota\n</text>"_s), qPrintable(asked));
        QVERIFY(asked.contains(u"<before>\nAla ma\n</before>"_s));
        QVERIFY(asked.contains(u"give back one line"_s)); // (a part of one line)
        QVERIFY(server.requests.first().value(u"stream"_s).toBool());
        QCOMPARE(ws->extensions()->aiPending(), 0);
        QVERIFY(session.canAccept());
        // The change word by word, escaped.
        const QString changes = session.changesHtml(QColor(u"#aa0000"_s), QColor(u"#00aa00"_s));
        QVERIFY2(changes.contains(u"line-through"_s) && changes.contains(u"background-color:rgba(0,170,0"_s),
                 qPrintable(changes));
        // Where it goes; put in as one undo step.
        QCOMPARE(session.changeFrom(), 7);
        QCOMPARE(session.changeTo(), 25);
        QCOMPARE(session.changeText(), u"żółtego kota"_s);
        QCOMPARE(editor.pasteAndEdit(0, session.changeFrom(), session.changeTo(), session.changeText()), 0);
        QCOMPARE(editor.text(), u"- Ala ma żółtego kota i psa.\n- Drugi\n"_s);
        QCOMPARE(editor.editingRow(), 0);
        editor.undo();
        QCOMPARE(editor.text(), u"- Ala ma bardzo dużego kota i psa.\n- Drugi\n"_s);
    }

    void aRewriteWaitsForItsPreset()
    {
        FakeAiServer server;
        server.answers = {u"Ala ma kota."_s, u"Ala has a cat."_s};
        auto ws = open(u"Dom.md"_s, u"- Ala ma kota.\n"_s, server);
        QVERIFY(ws);
        WritingSession session;
        session.setWorkspace(ws.get());
        // The caret in the block, nothing selected: its whole text; nothing asked yet.
        QVERIFY(session.startInline(u"rewrite"_s, QString(), 0, u"Ala ma kota."_s, 4, 4, u"Dom"_s));
        QVERIFY(session.choosing());
        QCOMPARE(session.original(), u"Ala ma kota."_s);
        QCOMPARE(session.state(), QString());
        QTest::qWait(50);
        QCOMPARE(server.requests.size(), 0);
        QVERIFY(session.rewriteWith(QString(), u"jako wiersz"_s));
        QVERIFY(!session.choosing());
        QCOMPARE(session.instruction(), u"jako wiersz"_s);
        QTRY_COMPARE(session.state(), u"done"_s);
        QVERIFY(FakeAiServer::lastUserText(server.requests.first()).startsWith(u"Rewrite the text below: jako wiersz"_s));
        QCOMPARE(session.changeFrom(), 0);
        QCOMPARE(session.changeTo(), 12);
        // Try again: another answer.
        QVERIFY(session.retry());
        QTRY_COMPARE(session.state(), u"done"_s);
        QCOMPARE(session.result(), u"Ala has a cat."_s);
        QCOMPARE(server.requests.size(), 2);
        session.clear();
        QCOMPARE(session.target(), QString());
        QCOMPARE(session.original(), QString());
    }

    void aSummaryOfBlocksAndAContinuation()
    {
        FakeAiServer server;
        server.answers = {u"Here is the summary:\n\nKrótko o planie."_s, u"jedziemy dalej."_s};
        auto ws = open(u"Plan.md"_s, u"- Plan\n  - rano góry\n  - wieczorem kolacja\n- Inne\n"_s, server);
        QVERIFY(ws);
        PageController editor;
        editor.setHost(ws.get());
        editor.setPath(u"Plan.md"_s);
        QTRY_VERIFY(editor.loaded());
        editor.selectRange(0, 0);
        const QString selected = editor.selectionMarkdown();
        QVERIFY(selected.contains(u"rano góry"_s));
        WritingSession session;
        session.setWorkspace(ws.get());
        QVERIFY(session.startBlocks(u"summarize"_s, QString(), selected, editor.selectionFirstRow(),
                                    editor.selectionLastRow(), u"Plan"_s));
        QTRY_COMPARE(session.state(), u"done"_s);
        QCOMPARE(session.result(), u"Krótko o planie."_s); // (the preamble gone)
        QCOMPARE(session.target(), u"blocks"_s);
        // After the block with its children, at its depth: one undo step.
        QVERIFY(editor.insertAfterBlock(session.row(), session.result(), session.firstRow()) >= 0);
        QCOMPARE(editor.text(), u"- Plan\n  - rano góry\n  - wieczorem kolacja\n- Krótko o planie.\n- Inne\n"_s);
        editor.undo();
        QCOMPARE(editor.text(), u"- Plan\n  - rano góry\n  - wieczorem kolacja\n- Inne\n"_s);
        // A continuation of a block's text at the caret.
        QVERIFY(session.startInline(u"continue"_s, QString(), 3, u"Inne i"_s, 6, 6, u"Plan"_s));
        QTRY_COMPARE(session.state(), u"done"_s);
        QVERIFY(FakeAiServer::lastUserText(server.requests.last()).startsWith(u"Continue the text below"_s));
        QCOMPARE(session.changeFrom(), 6);
        QCOMPARE(session.changeText(), u" jedziemy dalej."_s);
    }

    void sourceModesParagraph()
    {
        FakeAiServer server;
        server.answers = {u"Drugi akapit, krócej."_s};
        auto ws = open(u"Tekst.md"_s, u"Pierwszy akapit.\n\nDrugi akapit, który jest długi.\n\nTrzeci.\n"_s, server);
        QVERIFY(ws);
        const QString source = u"Pierwszy akapit.\n\nDrugi akapit, który jest długi.\n\nTrzeci.\n"_s;
        WritingSession session;
        session.setWorkspace(ws.get());
        // The caret inside the second paragraph, nothing selected: that paragraph.
        QVERIFY(session.startSource(u"rewrite"_s, u"shorter"_s, source, 25, 25, u"Tekst"_s));
        QTRY_COMPARE(session.state(), u"done"_s);
        QVERIFY(FakeAiServer::lastUserText(server.requests.first()).contains(u"<text>\nDrugi akapit, który jest długi.\n</text>"_s));
        QCOMPARE(session.changeFrom(), 18);
        QCOMPARE(session.changeTo(), 49);
        QCOMPARE(QString(source.left(session.changeFrom()) + session.changeText() + source.mid(session.changeTo())),
                 u"Pierwszy akapit.\n\nDrugi akapit, krócej.\n\nTrzeci.\n"_s);
    }

    void stopKeepsWhatCameAndFailuresSaySo()
    {
        FakeAiServer server;
        server.hold = true;
        auto ws = open(u"Dom.md"_s, u"- Ala\n"_s, server);
        QVERIFY(ws);
        WritingSession session;
        session.setWorkspace(ws.get());
        QVERIFY(session.startInline(u"summarize"_s, QString(), 0, u"Ala"_s, 0, 3, u"Dom"_s));
        QTRY_COMPARE(server.waiting(), 1);
        QCOMPARE(ws->extensions()->aiPending(), 1);
        session.stop(); // nothing came: nothing to keep
        QCOMPARE(session.state(), QString());
        QVERIFY(!session.canAccept());
        QTRY_COMPARE(ws->extensions()->aiPending(), 0);
        server.release();
        QTest::qWait(50);
        QCOMPARE(session.state(), QString()); // (its answer, when it comes, is dropped)

        // Nothing to work on, or no instruction.
        QVERIFY(!session.startInline(u"summarize"_s, QString(), 0, u"   "_s, 0, 3, u"Dom"_s));
        QCOMPARE(session.state(), u"failed"_s);
        QVERIFY(!session.error().isEmpty());
        // No model set up.
        ws->extensions()->setAi(brulion::ai::Settings{u"openai"_s, QString(), QString(), server.url()});
        QVERIFY(!session.startInline(u"summarize"_s, QString(), 0, u"Ala"_s, 0, 3, u"Dom"_s));
        QCOMPARE(session.state(), u"failed"_s);
        QVERIFY2(session.error().contains(u"no model"_s), qPrintable(session.error()));
        // No notebook.
        WritingSession alone;
        QVERIFY(!alone.startInline(u"summarize"_s, QString(), 0, u"Ala"_s, 0, 3, u"Dom"_s));
        QCOMPARE(alone.state(), u"failed"_s);
    }
};

QTEST_MAIN(TestWritingSession)
#include "tst_writingsession.moc"
