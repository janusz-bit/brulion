// Code mode's texts written by the AI (codeai/codeai.h): what each question sends — the commit's
// changes, the branch's commits, stat and diff against its base, the thread's first message, the
// thread's messages and its title now —, each part bounded (a long diff cut, never inside a
// character); the answers read — a fence around them, JSON, a heading's "#", labels, quotes, a
// prefix on a branch name, a sentence instead of a name, CRLF, empty and malformed answers —; the
// messages a thread's title is made of (its first and newest ones, pictures left out). Polish text.
#include "codeai/codeai.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::codeai;

namespace {

/// The request's one user message ("" when it has another shape).
QString userText(const ai::Request &r)
{
    return r.messages.size() == 1 && r.messages.first().role == u"user" ? r.messages.first().content : QString();
}

chat::Message message(chat::Role role, const QString &text)
{
    chat::Message m;
    m.role = role;
    m.text = text;
    return m;
}

} // namespace

class TestCodeAi : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void boundedText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("max");
        QTest::addColumn<QString>("expected");
        QTest::newRow("short") << u"Zażółć"_s << 10 << u"Zażółć"_s;
        QTest::newRow("exactly the limit") << u"Zażółć"_s << 6 << u"Zażółć"_s;
        QTest::newRow("cut") << u"Zażółć gęślą jaźń"_s << 6 << u"Zażółć\n[truncated]"_s;
        QTest::newRow("a character's halves kept together") << u"ab😀cd"_s << 3 << u"ab\n[truncated]"_s;
        QTest::newRow("a whole character fits") << u"ab😀cd"_s << 4 << u"ab😀\n[truncated]"_s;
        QTest::newRow("empty") << QString() << 5 << QString();
        QTest::newRow("no room") << u"abc"_s << 0 << u"\n[truncated]"_s;
        QTest::newRow("a negative limit") << u"abc"_s << -3 << u"\n[truncated]"_s;
    }
    void boundedText()
    {
        QFETCH(QString, text);
        QFETCH(int, max);
        QFETCH(QString, expected);
        QCOMPARE(bounded(text, max), expected);
    }

    void commitQuestion()
    {
        const QString patch = u"diff --git a/calc.py b/calc.py\n-    return a - b\n+    return a + b  # dodawanie, nie odejmowanie\n"_s;
        const ai::Request r = commitMessage(u"brulion/popraw-dodawanie", patch);
        QVERIFY(r.system.contains(u"commit message"_s));
        QVERIFY(r.system.contains(u"72"_s));
        QCOMPARE(r.effort, u"low"_s);
        QVERIFY(r.maxTokens > 0);
        const QString asked = userText(r);
        QVERIFY2(asked.startsWith(u"Branch: brulion/popraw-dodawanie\n"_s), qPrintable(asked));
        QVERIFY(asked.endsWith(patch));
        QVERIFY(userText(commitMessage(u"", patch)).startsWith(u"Branch: (detached HEAD)\n"_s));
        // A long diff is cut, said so.
        const QString cut = userText(commitMessage(u"main", QString(MaxPatch + 500, u'x')));
        QVERIFY(cut.endsWith(u"\n[truncated]"_s));
        QVERIFY(cut.size() < MaxPatch + 200);
    }

    void pullRequestQuestion()
    {
        BranchRange range{u"origin/main"_s, u"brulion/rabat"_s,
                          u"a1b2c3d Dodaj rabat dla stałych klientów\n    Rabat 5 % od trzeciego zamówienia.\n"_s,
                          u" koszyk.py | 4 ++--\n 1 file changed, 2 insertions(+), 2 deletions(-)\n"_s,
                          u"diff --git a/koszyk.py b/koszyk.py\n+RABAT = 0.05  # zniżka\n"_s};
        const ai::Request r = pullRequest(range);
        QVERIFY(r.system.contains(u"pull request"_s));
        QVERIFY(r.system.contains(u"## Summary"_s) && r.system.contains(u"## Testing"_s));
        QVERIFY(r.system.contains(u"first line"_s));
        QCOMPARE(r.effort, u"low"_s);
        QVERIFY(r.maxTokens > 0);
        const QString asked = userText(r);
        QVERIFY2(asked.contains(u"Base branch: origin/main\nHead branch: brulion/rabat\n"_s), qPrintable(asked));
        const qsizetype commits = asked.indexOf(u"Commits:\n"_s + range.commits);
        const qsizetype stat = asked.indexOf(u"Diff stat:\n"_s + range.stat);
        const qsizetype patch = asked.indexOf(u"Diff:\n"_s + range.patch);
        QVERIFY(commits > 0 && stat > commits && patch > stat);
        // Each part bounded on its own.
        range.commits = QString(MaxCommits * 2, u'c');
        range.stat = QString(MaxStat * 2, u's');
        range.patch = QString(MaxPatch * 2, u'p');
        const QString big = userText(pullRequest(range));
        QCOMPARE(big.count(u"[truncated]"_s), 3);
        QVERIFY(big.size() < MaxCommits + MaxStat + MaxPatch + 1000);
        QVERIFY(big.contains(QString(MaxCommits, u'c') + u"\n[truncated]"_s));
    }

    void readPullRequestAnswer_data()
    {
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("body");
        QTest::newRow("title and description")
                << u"Dodaj rabat dla stałych klientów\n\n## Summary\n- Rabat 5 %.\n\n## Testing\n- Not run\n"_s
                << u"Dodaj rabat dla stałych klientów"_s << u"## Summary\n- Rabat 5 %.\n\n## Testing\n- Not run"_s;
        QTest::newRow("a title alone") << u"Popraw dodawanie"_s << u"Popraw dodawanie"_s << QString();
        QTest::newRow("a heading") << u"# Popraw dodawanie\n\nOpis zmiany."_s << u"Popraw dodawanie"_s
                                   << u"Opis zmiany."_s;
        QTest::newRow("a label") << u"Title: Popraw dodawanie\n\nOpis."_s << u"Popraw dodawanie"_s << u"Opis."_s;
        QTest::newRow("a bold Polish label") << u"**Tytuł:** Popraw dodawanie\nOpis."_s << u"Popraw dodawanie"_s
                                             << u"Opis."_s;
        QTest::newRow("quotes and a period") << u"\"Popraw dodawanie.\"\n\nOpis."_s << u"Popraw dodawanie"_s
                                             << u"Opis."_s;
        QTest::newRow("a question keeps its mark") << u"Czy rabat działa?\n\nOpis."_s << u"Czy rabat działa?"_s
                                                   << u"Opis."_s;
        QTest::newRow("in a fence") << u"```markdown\nPopraw dodawanie\n\n## Summary\n- add dodaje\n```"_s
                                    << u"Popraw dodawanie"_s << u"## Summary\n- add dodaje"_s;
        QTest::newRow("json") << u"{\"title\": \"Popraw dodawanie.\", \"body\": \"## Summary\\n- żółw\"}"_s
                              << u"Popraw dodawanie"_s << u"## Summary\n- żółw"_s;
        QTest::newRow("json in a fence") << u"```json\n{\"title\": \"Popraw dodawanie\", \"body\": \"\"}\n```"_s
                                         << u"Popraw dodawanie"_s << QString();
        QTest::newRow("blank lines and spaces first") << u"\n\n   Popraw dodawanie   \n\n  Opis.  \n"_s
                                                      << u"Popraw dodawanie"_s << u"Opis."_s;
        QTest::newRow("CRLF") << u"Popraw dodawanie\r\n\r\n## Summary\r\n- x\r\n"_s << u"Popraw dodawanie"_s
                              << u"## Summary\n- x"_s;
        QTest::newRow("empty") << QString() << QString() << QString();
        QTest::newRow("blank") << u"  \n\t\n "_s << QString() << QString();
        QTest::newRow("an empty fence") << u"```\n```"_s << QString() << QString();
        QTest::newRow("json without a title") << u"{\"body\": \"Opis.\"}"_s << QString() << u"Opis."_s;
        QTest::newRow("broken json") << u"{\"title\": \"Popraw"_s << QString() << QString();
        QTest::newRow("json of another shape") << u"[\"Popraw dodawanie\"]"_s << QString() << QString();
    }
    void readPullRequestAnswer()
    {
        QFETCH(QString, answer);
        QFETCH(QString, title);
        QFETCH(QString, body);
        const PullRequestText text = readPullRequest(answer);
        QCOMPARE(text.title, title);
        QCOMPARE(text.body, body);
    }

    void branchQuestion()
    {
        const QString first = u"Popraw liczenie sumy w koszyku: rabat dla stałych klientów źle się zaokrągla."_s;
        const ai::Request r = branchName(first);
        QVERIFY(r.system.contains(u"branch"_s));
        QVERIFY(r.system.contains(u"prefix"_s));
        QVERIFY(r.system.contains(u"Do not answer"_s));
        QCOMPARE(r.effort, u"low"_s);
        QVERIFY(r.maxTokens > 0);
        QVERIFY(userText(r).endsWith(first));
        // A long message: its start, bounded.
        const QString cut = userText(branchName(QString(MaxMessage * 2, u'ż')));
        QVERIFY(cut.endsWith(u"\n[truncated]"_s));
        QVERIFY(cut.size() < MaxMessage + 200);
    }

    void readBranchNameAnswer_data()
    {
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("name");
        QTest::newRow("a name") << u"fix-cart-total"_s << u"fix-cart-total"_s;
        QTest::newRow("spaces around") << u"  fix-cart-total \n"_s << u"fix-cart-total"_s;
        QTest::newRow("backticks") << u"`fix-cart-total`"_s << u"fix-cart-total"_s;
        QTest::newRow("quotes") << u"\"fix-cart-total\""_s << u"fix-cart-total"_s;
        QTest::newRow("a label") << u"Branch: fix-cart-total"_s << u"fix-cart-total"_s;
        QTest::newRow("a longer label") << u"Branch name: `fix-cart-total`"_s << u"fix-cart-total"_s;
        QTest::newRow("a preamble") << u"Here is a branch name for the work: fix-cart-total"_s << u"fix-cart-total"_s;
        QTest::newRow("a prefix") << u"feature/fix-cart-total"_s << u"fix-cart-total"_s;
        QTest::newRow("the app's prefix") << u"brulion/popraw-koszyk"_s << u"popraw-koszyk"_s;
        QTest::newRow("json") << u"{\"branch\": \"fix-cart-total\"}"_s << u"fix-cart-total"_s;
        QTest::newRow("in a fence") << u"```\nfix-cart-total\n```"_s << u"fix-cart-total"_s;
        QTest::newRow("words") << u"fix cart total"_s << u"fix cart total"_s;
        QTest::newRow("Polish") << u"popraw-zaokrąglanie-rabatu"_s << u"popraw-zaokrąglanie-rabatu"_s;
        QTest::newRow("an explanation after it") << u"fix-cart\n\nThis name says what the change does."_s
                                                 << u"fix-cart"_s;
        QTest::newRow("a sentence") << u"I would call it the cart fix, because it fixes the total of the cart for all"_s
                                    << QString();
        QTest::newRow("too long") << QString(MaxBranchChars + 1, u'a') << QString();
        QTest::newRow("the longest") << QString(MaxBranchChars, u'a') << QString(MaxBranchChars, u'a');
        QTest::newRow("a prefix alone") << u"feature/"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("blank") << u" \n "_s << QString();
        QTest::newRow("an empty fence") << u"```\n```"_s << QString();
        QTest::newRow("json without a branch") << u"{\"name\": \"fix-cart\"}"_s << QString();
        QTest::newRow("broken json") << u"{\"branch\": \"fix"_s << QString();
    }
    void readBranchNameAnswer()
    {
        QFETCH(QString, answer);
        QFETCH(QString, name);
        QCOMPARE(readBranchName(answer), name);
    }

    void titleContextOfAThread()
    {
        chat::Conversation c;
        c.messages = {message(chat::Role::User, u"Popraw dodawanie w calc.py"_s),
                      message(chat::Role::Assistant, u"Gotowe: add dodaje.\n![[Pasted image 2026.png]]"_s),
                      message(chat::Role::Assistant, u"![[zrzut.png]]"_s), // (no words: left out)
                      message(chat::Role::User, u"Dodaj też test z żółwiem"_s)};
        QCOMPARE(titleContext(c),
                 u"USER:\nPopraw dodawanie w calc.py\n\nASSISTANT:\nGotowe: add dodaje.\n\nUSER:\nDodaj też test z żółwiem"_s);
        QCOMPARE(titleContext(chat::Conversation{}), QString());
        chat::Conversation silent;
        silent.messages = {message(chat::Role::User, u"  \n"_s)};
        QCOMPARE(titleContext(silent), QString());
    }

    void titleContextOfALongThread()
    {
        // Thirty turns: the first question, the newest ones and the newest answers fit.
        chat::Conversation c;
        for (int i = 1; i <= 30; ++i) {
            c.messages << message(chat::Role::User, u"Pytanie %1 ż "_s.arg(i) + QString(400, u'u'));
            c.messages << message(chat::Role::Assistant, u"Odpowiedź %1 ó "_s.arg(i) + QString(400, u'a'));
        }
        const QString context = titleContext(c);
        QVERIFY(context.size() <= MaxTitleContext);
        QVERIFY2(context.startsWith(u"[Earlier content truncated]\n\nUSER:\nPytanie 1 ż "_s), qPrintable(context.left(80)));
        QVERIFY(context.contains(u"USER:\nPytanie 30 ż "_s));
        QVERIFY(context.contains(u"ASSISTANT:\nOdpowiedź 30 ó "_s));
        QVERIFY(!context.contains(u"Pytanie 2 ż"_s));
        // In their order.
        QVERIFY(context.indexOf(u"Pytanie 29 ż"_s) < context.indexOf(u"Odpowiedź 29 ó"_s));
        QVERIFY(context.indexOf(u"Odpowiedź 29 ó"_s) < context.indexOf(u"Pytanie 30 ż"_s));
    }

    void titleContextOfALongMessage()
    {
        chat::Conversation c;
        c.messages = {message(chat::Role::User, u"Początek. "_s + QString(5000, u'x') + u" Koniec."_s)};
        const QString context = titleContext(c);
        QVERIFY(context.size() <= MaxTitleMessage + 40);
        QVERIFY2(context.startsWith(u"[Earlier content truncated]\n\nUSER:\nPoczątek. "_s), qPrintable(context.left(60)));
        QVERIFY(context.endsWith(u" Koniec."_s));
        QVERIFY(context.contains(u"\n[Content truncated]\n"_s));
    }

    void titleQuestion()
    {
        const QString context = u"USER:\nPopraw dodawanie w calc.py"_s;
        const ai::Request r = threadTitle(u"Popraw dodawanie w calc", u"kalkulator", context);
        QVERIFY(r.system.contains(u"title"_s));
        QVERIFY(r.system.contains(u"language of the user"_s));
        QCOMPARE(r.effort, u"low"_s);
        QVERIFY(r.maxTokens > 0);
        const QString asked = userText(r);
        QVERIFY2(asked.contains(u"The project: kalkulator"_s), qPrintable(asked));
        QVERIFY(asked.contains(u"The title now: Popraw dodawanie w calc"_s));
        QVERIFY(asked.endsWith(u"The thread:\n"_s + context));
        QVERIFY(!userText(threadTitle(u"x", u"", context)).contains(u"The project:"_s));
    }

    void readTitleAnswer_data()
    {
        QTest::addColumn<QString>("answer");
        QTest::addColumn<QString>("title");
        QTest::newRow("a title") << u"Naprawa dodawania w kalkulatorze"_s << u"Naprawa dodawania w kalkulatorze"_s;
        QTest::newRow("Polish quotes") << u"„Naprawa dodawania”"_s << u"Naprawa dodawania"_s;
        QTest::newRow("quotes") << u"\"Fix the sync conflict\""_s << u"Fix the sync conflict"_s;
        QTest::newRow("a label") << u"Title: Naprawa dodawania"_s << u"Naprawa dodawania"_s;
        QTest::newRow("a Polish label") << u"Tytuł: Naprawa dodawania"_s << u"Naprawa dodawania"_s;
        QTest::newRow("a heading") << u"## Naprawa dodawania"_s << u"Naprawa dodawania"_s;
        QTest::newRow("a heading with a label") << u"# Title: Naprawa dodawania"_s << u"Naprawa dodawania"_s;
        QTest::newRow("bold") << u"**Naprawa dodawania**"_s << u"Naprawa dodawania"_s;
        QTest::newRow("a period") << u"Naprawa dodawania."_s << u"Naprawa dodawania"_s;
        QTest::newRow("a question") << u"Dlaczego synchronizacja zawodzi?"_s << u"Dlaczego synchronizacja zawodzi?"_s;
        QTest::newRow("brackets of its own") << u"[WIP] Naprawa synchronizacji"_s << u"[WIP] Naprawa synchronizacji"_s;
        QTest::newRow("a colon of its own") << u"Sync: rozwiązywanie konfliktów"_s << u"Sync: rozwiązywanie konfliktów"_s;
        QTest::newRow("json") << u"{\"title\": \"Naprawa dodawania\", \"needsRefinement\": false}"_s
                              << u"Naprawa dodawania"_s;
        QTest::newRow("in a fence") << u"```\nNaprawa dodawania\n```"_s << u"Naprawa dodawania"_s;
        QTest::newRow("spaces") << u"  Naprawa   dodawania \t w  calc  "_s << u"Naprawa dodawania w calc"_s;
        QTest::newRow("an explanation after it") << u"Naprawa dodawania\nThe thread is about adding."_s
                                                 << u"Naprawa dodawania"_s;
        QTest::newRow("too long: cut at a word")
                << u"Naprawa dodawania i odejmowania w kalkulatorze oraz testy jednostkowe dla nich"_s
                << u"Naprawa dodawania i odejmowania w kalkulatorze oraz testy"_s;
        QTest::newRow("one long word") << QString(70, u'ż') << QString(MaxTitleChars, u'ż');
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("blank") << u" \n "_s << QString();
        QTest::newRow("an empty fence") << u"```\n```"_s << QString();
        QTest::newRow("quotes alone") << u"\"\""_s << QString();
        QTest::newRow("json without a title") << u"{\"name\": \"x\"}"_s << QString();
        QTest::newRow("broken json") << u"{\"title\": \"Napr"_s << QString();
    }
    void readTitleAnswer()
    {
        QFETCH(QString, answer);
        QFETCH(QString, title);
        QCOMPARE(readTitle(answer), title);
    }
};

QTEST_GUILESS_MAIN(TestCodeAi)
#include "tst_codeai.moc"
