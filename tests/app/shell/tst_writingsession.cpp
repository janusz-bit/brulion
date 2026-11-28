// The editor's AI assistant on the real Workspace (WritingSession, docs/UI.md "AI in the editor"),
// an OpenAI-compatible server played (../fakeaiserver.h): a rewrite of part of a block's text
// streamed — the result cleaned as it comes, the change word by word, where it goes (change()) —
// and put into the block with PageController.pasteAndEdit as one undo step; "Rewrite…" without a
// preset asks nothing until one is chosen; a continuation and a summary of the selected blocks
// (insertAfterBlock, one undo step, at the block's depth); source mode's paragraph at the caret;
// Try again, Stop (what came is kept), a failure (no model set up, the server's error) and the
// request counted in aiPending. Code mode's texts by the same AI (codeai): AiAsk's one question (the
// commit message, the pull request), a new worktree's branch named from the thread's first message,
// a thread's title regenerated (renamed, kept, failed, nothing asked without the AI; the page deleted
// or the name taken meanwhile, the notebook closed, the AI not reached).
#include "../fakeaiserver.h"

#include "aiask.h"

#include "codeprojects.h"
#include "pagecontroller.h"
#include "workspace.h"
#include "workspacechats.h"
#include "workspaceextensions.h"
#include "writingsession.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
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

    // AiAsk (Code mode's commit message written by AI): one question, its answer as text — a fence
    // around all of it taken off; nothing asked without the AI set up or a prompt.
    void anAiAskAnswersOnce()
    {
        FakeAiServer server;
        server.answers = {u"```\nNaprawiono dodawanie w calc.py\n\nadd zwraca a + b.\n```"_s};
        auto ws = open(u"Dom.md"_s, u"- x\n"_s, server);
        QVERIFY(ws);
        brulion::shell::AiAsk ask;
        QVERIFY(!ask.available());
        QVERIFY(!ask.ask(u"system"_s, u"prompt"_s)); // (no workspace)
        QSignalSpy availableChanged(&ask, &brulion::shell::AiAsk::availableChanged);
        ask.setWorkspace(ws.get());
        QVERIFY(ask.available());
        QVERIFY(availableChanged.size() >= 1);
        QVERIFY(!ask.ask(u"system"_s, u"  "_s));
        QSignalSpy answered(&ask, &brulion::shell::AiAsk::answered);
        QVERIFY(ask.ask(u"You write git commit messages."_s, u"diff --git a/calc.py b/calc.py\n-    return a - b\n+    return a + b\n"_s));
        QVERIFY(ask.busy());
        QTRY_COMPARE_WITH_TIMEOUT(answered.size(), 1, 10000);
        QVERIFY(!ask.busy());
        QVERIFY(answered.first().at(0).toBool());
        QCOMPARE(answered.first().at(1).toString(), u"Naprawiono dodawanie w calc.py\n\nadd zwraca a + b."_s);
        QVERIFY(FakeAiServer::lastUserText(server.requests.first()).contains(u"return a + b"_s));
        // Pure: a fence around the whole answer only.
        QCOMPARE(brulion::shell::AiAsk::cleaned(u"  Zażółć  \n"_s), u"Zażółć"_s);
        QCOMPARE(brulion::shell::AiAsk::cleaned(u"```text\nA\n```"_s), u"A"_s);
        QCOMPARE(brulion::shell::AiAsk::cleaned(u"a ```b``` c"_s), u"a ```b``` c"_s);
    }

    // A new worktree's branch named by the AI of the settings from the thread's first message (T3
    // Code's AI-made branch name); without the AI set up nothing is asked: the message's slug.
    void anAiNamesAWorktreesBranch()
    {
        const QString gitProgram = QStandardPaths::findExecutable(u"git"_s);
        if (gitProgram.isEmpty())
            QSKIP("no git");
        FakeAiServer server;
        server.answers = {u"```\nfix-cart-total\n```"_s};
        auto ws = open(u"Dom.md"_s, u"- x\n"_s, server);
        QVERIFY(ws);
        const QString repo = m_tmp.filePath(u"koszyk%1"_s.arg(m_n));
        QVERIFY(QDir().mkpath(repo));
        QFile file(repo + u"/koszyk.py"_s);
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write("RABAT = 0\n") > 0);
        file.close();
        for (const QStringList &args : {QStringList{u"init"_s, u"-q"_s}, QStringList{u"add"_s, u"-A"_s},
                                        QStringList{u"-c"_s, u"user.name=Ala"_s, u"-c"_s, u"user.email=ala@example.com"_s,
                                                    u"-c"_s, u"commit.gpgsign=false"_s, u"commit"_s, u"-qm"_s, u"start"_s}})
            QCOMPARE(QProcess::execute(gitProgram, QStringList{u"-C"_s, repo} + args), 0);
        brulion::shell::CodeProjects *projects = ws->projects();
        projects->setWorktreesRoot(m_tmp.filePath(u"drzewa%1"_s.arg(m_n)));
        const QString id = projects->add(repo);
        QSignalSpy created(projects, &brulion::shell::CodeProjects::worktreeCreated);
        QVERIFY(projects->createWorktree(id, u"Popraw liczenie sumy w koszyku: rabat źle się zaokrągla"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 1, 20000);
        QVERIFY2(created.first().at(3).toString().isEmpty(), qPrintable(created.first().at(3).toString()));
        QCOMPARE(created.first().at(2).toString(), u"brulion/fix-cart-total"_s);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(FakeAiServer::lastUserText(server.requests.first()).contains(u"rabat źle się zaokrągla"_s));
        QCOMPARE(ws->extensions()->aiPending(), 0);
        // No AI set up: nothing asked, the message's slug — at once, not after the wait for a name.
        ws->extensions()->setAi(brulion::ai::Settings{});
        QVERIFY(projects->createWorktree(id, u"Dodaj rabat"_s));
        QTRY_COMPARE_WITH_TIMEOUT(created.size(), 2, brulion::shell::CodeProjects::BranchNameTimeoutMs / 4);
        QCOMPARE(created.at(1).at(2).toString(), u"brulion/dodaj-rabat"_s);
        QCOMPARE(server.requests.size(), 1);
    }

    // "Regenerate title" (T3 Code's): the thread's messages and its name now asked about, the page
    // renamed with its date kept; the same name again is kept; a failure said; one at a time;
    // nothing asked without the AI set up or a conversation.
    void anAiRegeneratesAThreadsTitle()
    {
        FakeAiServer server;
        server.answers = {u"„Naprawa dodawania w kalkulatorze.”"_s, u"Title: Naprawa dodawania w kalkulatorze"_s,
                          QString()};
        auto ws = open(u"Dom.md"_s, u"- x\n"_s, server);
        QVERIFY(ws);
        const QString path = u"AI/2026-11-26 Popraw dodawanie.md"_s;
        QVERIFY(ws->writeFile(path, u"---\ntasks: ignore\n---\n- 🧑 Popraw dodawanie w calc.py\n- 🤖 Gotowe: add dodaje.\n"_s));
        brulion::shell::WorkspaceChats *chats = ws->chats();
        QVERIFY(chats->canRegenerateTitles());
        // (Its messages as the AI's questions get them: a pull request's text too.)
        QVERIFY2(chats->threadContext(path).contains(u"Popraw dodawanie w calc.py"_s), qPrintable(chats->threadContext(path)));
        QCOMPARE(chats->threadContext(u"AI/nie ma.md"_s), QString());
        QSignalSpy done(chats, &brulion::shell::WorkspaceChats::titleRegenerated);
        QSignalSpy titles(chats, &brulion::shell::WorkspaceChats::titlesChanged);
        QVERIFY(chats->regenerateTitle(path));
        QVERIFY(chats->regeneratingTitle(path));
        QVERIFY(titles.size() >= 1);
        QVERIFY(!chats->regenerateTitle(path)); // (one on its way)
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        const QString renamed = u"AI/2026-11-26 Naprawa dodawania w kalkulatorze.md"_s;
        QCOMPARE(done.at(0).at(0).toString(), path);
        QCOMPARE(done.at(0).at(1).toString(), renamed);
        QCOMPARE(done.at(0).at(2).toString(), u"renamed"_s);
        QVERIFY(ws->exists(renamed) && !ws->exists(path));
        QVERIFY(!chats->regeneratingTitle(path));
        QCOMPARE(server.requests.size(), 1);
        const QString asked = FakeAiServer::lastUserText(server.requests.first());
        QVERIFY2(asked.contains(u"The title now: Popraw dodawanie"_s), qPrintable(asked));
        QVERIFY(asked.contains(u"USER:\nPopraw dodawanie w calc.py"_s));
        QVERIFY(asked.contains(u"ASSISTANT:\nGotowe: add dodaje."_s));
        // The same name: kept.
        QVERIFY(chats->regenerateTitle(renamed));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 10000);
        QCOMPARE(done.at(1).at(1).toString(), renamed);
        QCOMPARE(done.at(1).at(2).toString(), u"unchanged"_s);
        // No title in the answer: said, nothing renamed.
        QVERIFY(chats->regenerateTitle(renamed));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 10000);
        QCOMPARE(done.at(2).at(2).toString(), u"failed"_s);
        QVERIFY(!done.at(2).at(3).toString().isEmpty());
        QVERIFY(ws->exists(renamed));
        QCOMPARE(ws->extensions()->aiPending(), 0);
        // Nothing to ask about: a page that is no conversation, one that is not there.
        QVERIFY(!chats->regenerateTitle(u"Dom.md"_s));
        QVERIFY(!chats->regenerateTitle(u"AI/2026-11-26 Nie ma.md"_s));
        QVERIFY(!chats->regenerateTitle(QString()));
        // No AI set up: nothing asked.
        ws->extensions()->setAi(brulion::ai::Settings{});
        QVERIFY(!chats->canRegenerateTitles());
        QVERIFY(!chats->regenerateTitle(renamed));
        QCOMPARE(server.requests.size(), 3);
    }

    // While the AI writes the title: the page deleted meanwhile is left alone, a name another
    // conversation has is said, the notebook closed stops the question (no answer comes); the AI
    // not reached says why.
    void aRegeneratedTitleMeetsWhatChanged()
    {
        FakeAiServer server;
        server.hold = true;
        server.answers = {u"Nowa nazwa"_s, u"Zajęta nazwa"_s, u"Po zamknięciu"_s};
        auto ws = open(u"Dom.md"_s, u"- x\n"_s, server);
        QVERIFY(ws);
        const QString page = u"---\ntasks: ignore\n---\n- 🧑 Popraw dodawanie\n- 🤖 Gotowe.\n"_s;
        const QString path = u"AI/2026-11-26 Popraw dodawanie.md"_s;
        QVERIFY(ws->writeFile(path, page));
        brulion::shell::WorkspaceChats *chats = ws->chats();
        QSignalSpy done(chats, &brulion::shell::WorkspaceChats::titleRegenerated);
        // Deleted meanwhile.
        QVERIFY(chats->regenerateTitle(path));
        QTRY_COMPARE(server.waiting(), 1);
        QVERIFY(!chats->remove(path).isEmpty());
        server.release();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QCOMPARE(done.at(0).at(2).toString(), u"failed"_s);
        QCOMPARE(done.at(0).at(1).toString(), path);
        QVERIFY(!ws->exists(u"AI/2026-11-26 Nowa nazwa.md"_s));
        // A name another conversation has.
        QVERIFY(ws->writeFile(path, page));
        QVERIFY(ws->writeFile(u"AI/2026-11-26 Zajęta nazwa.md"_s, page));
        QVERIFY(chats->regenerateTitle(path));
        QTRY_COMPARE(server.waiting(), 1);
        server.release();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 10000);
        QCOMPARE(done.at(1).at(2).toString(), u"failed"_s);
        QVERIFY(done.at(1).at(3).toString().contains(u"Zajęta nazwa"_s));
        QVERIFY(ws->exists(path));
        // The notebook closed: stopped, nothing said.
        QSignalSpy titles(chats, &brulion::shell::WorkspaceChats::titlesChanged);
        QVERIFY(chats->regenerateTitle(path));
        QTRY_COMPARE(server.waiting(), 1);
        chats->reset();
        QVERIFY(!chats->regeneratingTitle(path));
        QVERIFY(titles.size() >= 2);
        QTRY_COMPARE_WITH_TIMEOUT(ws->extensions()->aiPending(), 0, 3000); // (the question stopped, not left running)
        server.release();
        QTest::qWait(200);
        QCOMPARE(done.size(), 2);
        QVERIFY(ws->exists(path));
        // The AI not reached: its error is said.
        QString closed;
        {
            const FakeAiServer gone;
            closed = gone.url();
        }
        brulion::ai::Settings unreachable;
        unreachable.provider = u"openai"_s;
        unreachable.baseUrl = closed;
        unreachable.model = u"fake"_s;
        ws->extensions()->setAi(unreachable);
        QVERIFY(chats->regenerateTitle(path));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 15000);
        QCOMPARE(done.at(2).at(2).toString(), u"failed"_s);
        QVERIFY(!done.at(2).at(3).toString().isEmpty());
        QVERIFY(ws->exists(path));
    }

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
        QVERIFY(FakeAiServer::lastUserText(server.requests.first())
                        .startsWith(u"Rewrite the text below: jako wiersz"_s));
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
        QVERIFY(FakeAiServer::lastUserText(server.requests.first())
                        .contains(u"<text>\nDrugi akapit, który jest długi.\n</text>"_s));
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
