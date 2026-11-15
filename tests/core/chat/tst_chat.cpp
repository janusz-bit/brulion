// Conversations with AI as pages of the notebook (chat/chat.h): a page's messages (the
// frontmatter and a title before them, lines under a message without their indentation, blank
// lines and fenced code in them, a Logseq outline with tabs, CRLF, a line typed between messages,
// look-alikes that are no messages), the page a conversation is about, a message written into a
// page and appended to one byte for byte (reminders kept quiet, code untouched, a fence left open
// closed), the first lines of a new page, its name, the journal's line, the history b.ai gets and
// the previews of the list.
#include "chat/chat.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::chat;

Q_DECLARE_METATYPE(brulion::chat::Role)

namespace {

QString roles(const Conversation &c)
{
    QString out;
    for (const Message &m : c.messages)
        out += m.role == Role::User ? u'U' : u'A';
    return out;
}

} // namespace

class TestChat : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void parsesMessages()
    {
        const QString page = u"---\nzadania: ignoruj\n---\n"
                             "- 🧑 Co mam jutro do zrobienia? ([[Dom]])\n"
                             "- 🤖 Jutro masz trzy zadania:\n"
                             "\n"
                             "  - zapłacić czynsz — [[Dom]]\n"
                             "    - przelew\n"
                             "  - oddać raport\n"
                             "\n"
                             "  Coś jeszcze?\n"
                             "\n"
                             "- 🧑 Przesuń czynsz na piątek\n"_s;
        const Conversation c = parse(page);
        QCOMPARE(roles(c), u"UAU"_s);
        QCOMPARE(c.page, u"Dom"_s);
        QCOMPARE(c.messages.at(0).text, u"Co mam jutro do zrobienia? ([[Dom]])"_s);
        QCOMPARE(c.messages.at(0).line, 3);
        QCOMPARE(c.messages.at(1).text, u"Jutro masz trzy zadania:\n\n- zapłacić czynsz — [[Dom]]\n  - przelew\n"
                                        "- oddać raport\n\nCoś jeszcze?"_s);
        QCOMPARE(c.messages.at(1).line, 4);
        QCOMPARE(c.messages.at(1).endLine, 10); // (the blank line after it is not the message's)
        QCOMPARE(c.messages.at(2).text, u"Przesuń czynsz na piątek"_s);
        QCOMPARE(c.messages.at(2).line, 12);
    }

    void parsesCodeAndOutlines_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("roles");
        QTest::addColumn<QString>("last");

        QTest::newRow("code under a message")
                << u"- 🤖\n  ```luau\n  print(\"zażółć\")\n\n  - 🧑 not a message\n  ```\n"_s << u"A"_s
                << u"```luau\nprint(\"zażółć\")\n\n- 🧑 not a message\n```"_s;
        QTest::newRow("a fence at the start of a line")
                << u"- 🧑 Pokaż\n```\n- 🤖 inside the code\n```\n- 🤖 Gotowe\n"_s << u"UA"_s << u"Gotowe"_s;
        QTest::newRow("tabs (Logseq)") << u"- 🧑 Pytanie\n- 🤖 Odpowiedź\n\t- punkt\n\t\t- głębiej\n"_s << u"UA"_s
                                      << u"Odpowiedź\n- punkt\n\t- głębiej"_s;
        QTest::newRow("CRLF") << u"- 🧑 Ąę\r\n- 🤖 Óż\r\n  dalej\r\n"_s << u"UA"_s << u"Óż\ndalej"_s;
        QTest::newRow("a line typed between messages")
                << u"- 🧑 Pytanie\n- 🤖 Odpowiedź\n  więcej\n- moja notatka\n"_s << u"UA"_s
                << u"Odpowiedź\nmore"_s.replace(u"more"_s, u"więcej\n- moja notatka"_s);
        QTest::newRow("other markers") << u"* 🧑 gwiazdka\n+ 🤖 plus\n"_s << u"UA"_s << u"plus"_s;
        QTest::newRow("a bare marker") << u"- 🧑 Pytanie\n- 🤖\n"_s << u"UA"_s << QString();
        QTest::newRow("no final line break") << u"- 🧑 Pytanie\n- 🤖 bez końca"_s << u"UA"_s << u"bez końca"_s;
        QTest::newRow("look-alikes") << u"- 🧑x\n-🤖 no space\n  - 🤖 indented\n🧑 no marker\n"_s << QString()
                                     << QString();
        QTest::newRow("a title before the messages") << u"# Rozmowa\n\nWstęp.\n- 🧑 Pytanie\n"_s << u"U"_s
                                                     << u"Pytanie"_s;
        QTest::newRow("frontmatter that looks like one")
                << u"---\nlista:\n- 🧑 w frontmatterze\n---\n- 🤖 Odpowiedź\n"_s << u"A"_s << u"Odpowiedź"_s;
        QTest::newRow("an unclosed frontmatter") << u"---\n- 🧑 Pytanie\n"_s << u"U"_s << u"Pytanie"_s;
        QTest::newRow("empty") << QString() << QString() << QString();
    }
    void parsesCodeAndOutlines()
    {
        QFETCH(QString, page);
        QFETCH(QString, roles);
        QFETCH(QString, last);
        const Conversation c = parse(page);
        QCOMPARE(::roles(c), roles);
        if (!c.messages.isEmpty())
            QCOMPARE(c.messages.last().text, last);
    }

    void thePageItIsAbout_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("about");

        QTest::newRow("a page") << u"- 🧑 Streść ([[Dom]])\n"_s << u"Dom"_s;
        QTest::newRow("a path part and an alias") << u"- 🧑 Co tu? ([[Projekty/Dom|dom]])  \n"_s << u"Projekty/Dom"_s;
        QTest::newRow("a heading") << u"- 🧑 Co tu? ([[Dom#Ogród]])\n"_s << u"Dom"_s;
        QTest::newRow("a link inside the text") << u"- 🧑 Co z [[Dom]] i reszta?\n"_s << QString();
        QTest::newRow("on a later line") << u"- 🧑 Pytanie\n  ([[Dom]])\n"_s << QString();
        QTest::newRow("a later message's") << u"- 🧑 Pytanie\n- 🤖 Tak\n- 🧑 A to? ([[Dom]])\n"_s << QString();
        QTest::newRow("the AI's first message") << u"- 🤖 Cześć ([[Dom]])\n- 🧑 Hej ([[Praca]])\n"_s << u"Praca"_s;
    }
    void thePageItIsAbout()
    {
        QFETCH(QString, page);
        QFETCH(QString, about);
        QCOMPARE(parse(page).page, about);
    }

    void formatsMessages_data()
    {
        QTest::addColumn<Role>("role");
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("expected");

        QTest::newRow("one line") << Role::User << u"  Zażółć gęślą jaźń  "_s << u"- 🧑 Zażółć gęślą jaźń\n"_s;
        QTest::newRow("lines and blank lines")
                << Role::Assistant << u"\n\nPierwsza\n\n- punkt\n  - podpunkt\n\n\n"_s
                << u"- 🤖 Pierwsza\n\n  - punkt\n    - podpunkt\n"_s;
        QTest::newRow("a list first") << Role::Assistant << u"- jeden\n- dwa"_s << u"- 🤖\n  - jeden\n  - dwa\n"_s;
        QTest::newRow("a heading first") << Role::Assistant << u"## Plan\ntekst"_s << u"- 🤖\n  ## Plan\n  tekst\n"_s;
        QTest::newRow("code first") << Role::Assistant << u"```luau\nprint(1)\n\nprint(2)\n```"_s
                                    << u"- 🤖\n  ```luau\n  print(1)\n\n  print(2)\n  ```\n"_s;
        QTest::newRow("an unclosed fence") << Role::Assistant << u"Kod:\n~~~~\nx = 1"_s
                                           << u"- 🤖 Kod:\n  ~~~~\n  x = 1\n  ~~~~\n"_s;
        QTest::newRow("reminders") << Role::Assistant
                                   << u"Masz ⏰ 2026-11-16 09:00 i ⏰! 2026-11-17, (@2026-11-18 10:00) oraz ⏰ jutro"_s
                                   << u"- 🤖 Masz `⏰ 2026-11-16 09:00` i `⏰! 2026-11-17`, `(@2026-11-18 10:00)` oraz ⏰ jutro\n"_s;
        QTest::newRow("a reminder in a code span") << Role::User << u"To `⏰ 2026-11-16` i ``a ⏰ 2026-11-16 b``"_s
                                                   << u"- 🧑 To `⏰ 2026-11-16` i ``a ⏰ 2026-11-16 b``\n"_s;
        QTest::newRow("an unmatched backtick") << Role::User << u"a ` b ⏰ 2026-11-16"_s
                                               << u"- 🧑 a ` b `⏰ 2026-11-16`\n"_s;
        QTest::newRow("a reminder in code") << Role::Assistant << u"x\n```\n⏰ 2026-11-16 09:00\n```"_s
                                            << u"- 🤖 x\n  ```\n  ⏰ 2026-11-16 09:00\n  ```\n"_s;
        QTest::newRow("a reminder on a later line") << Role::Assistant << u"x\n- [ ] czynsz ⏰ 2026-11-16"_s
                                                    << u"- 🤖 x\n  - [ ] czynsz `⏰ 2026-11-16`\n"_s;
        QTest::newRow("empty") << Role::Assistant << u"  \n "_s << u"- 🤖\n"_s;
    }
    void formatsMessages()
    {
        QFETCH(Role, role);
        QFETCH(QString, markdown);
        QFETCH(QString, expected);
        QCOMPARE(formatMessage(role, markdown), expected);
    }

    void roundTrips_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::newRow("paragraphs") << u"Pierwszy akapit.\n\nDrugi, z [[Linkiem]]."_s;
        QTest::newRow("a list") << u"Lista:\n- a\n  - b\n1. c"_s;
        QTest::newRow("code") << u"```luau\nlocal x = 1\n\n  return x\n```\nKoniec."_s;
        QTest::newRow("a list first") << u"- [ ] zadanie\n- [x] zrobione"_s;
    }
    void roundTrips()
    {
        QFETCH(QString, markdown);
        const QString page = appendMessage(appendMessage(QString(), Role::User, u"Pytanie"_s), Role::Assistant, markdown);
        const Conversation c = parse(page);
        QCOMPARE(roles(c), u"UA"_s);
        QCOMPARE(c.messages.last().text, markdown);
    }

    void appendsByteForByte_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("expected");

        QTest::newRow("empty") << QString() << u"- 🤖 Tak\n  dobrze\n"_s;
        QTest::newRow("a line break at the end") << u"- 🧑 Ą\n"_s << u"- 🧑 Ą\n- 🤖 Tak\n  dobrze\n"_s;
        QTest::newRow("none at the end") << u"- 🧑 Ą"_s << u"- 🧑 Ą\n- 🤖 Tak\n  dobrze\n"_s;
        QTest::newRow("CRLF") << u"---\r\nzadania: ignoruj\r\n---\r\n- 🧑 Ą"_s
                              << u"---\r\nzadania: ignoruj\r\n---\r\n- 🧑 Ą\r\n- 🤖 Tak\r\n  dobrze\r\n"_s;
        QTest::newRow("blank lines at the end") << u"- 🧑 Ą\n\n"_s << u"- 🧑 Ą\n\n- 🤖 Tak\n  dobrze\n"_s;
    }
    void appendsByteForByte()
    {
        QFETCH(QString, content);
        QFETCH(QString, expected);
        QCOMPARE(appendMessage(content, Role::Assistant, u"Tak\r\ndobrze"_s), expected);
    }

    void newConversations_data()
    {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("expected");

        QTest::newRow("Polish") << u"Co mam jutro?"_s << QString() << u"pl"_s
                                << u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro?\n"_s;
        QTest::newRow("English, about a page") << u"What is due?\nAnd later?"_s << u"Dom"_s << u"en"_s
                                               << u"---\ntasks: ignore\n---\n- 🧑 What is due? ([[Dom]])\n  And later?\n"_s;
        QTest::newRow("a page and no words") << u"  "_s << u"Dom"_s << u"pl"_s
                                             << u"---\nzadania: ignoruj\n---\n- 🧑 ([[Dom]])\n"_s;
        QTest::newRow("code first") << u"```\nx\n```"_s << u"Dom"_s << u"pl"_s
                                    << u"---\nzadania: ignoruj\n---\n- 🧑 ([[Dom]])\n  ```\n  x\n  ```\n"_s;
    }
    void newConversations()
    {
        QFETCH(QString, question);
        QFETCH(QString, page);
        QFETCH(QString, language);
        QFETCH(QString, expected);
        const QString content = newConversation(question, page, language);
        QCOMPARE(content, expected);
        const Conversation c = parse(content);
        QCOMPARE(roles(c), u"U"_s);
        QCOMPARE(c.page, page);
    }

    void pageTitles_data()
    {
        QTest::addColumn<QString>("question");
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("title");

        QTest::newRow("a question") << u"Co mam jutro do zrobienia?"_s << u"pl"_s << u"AI/2026-11-15 Co mam jutro do zrobienia"_s;
        QTest::newRow("links and marks") << u"Streść [[Projekty/Dom|dom]] i **[[Praca#Plan]]** `x`!"_s << u"pl"_s
                                         << u"AI/2026-11-15 Streść dom i Praca x"_s;
        QTest::newRow("what a file name cannot hold") << u"a/b: c? <d> | e #f ^g"_s << u"pl"_s
                                                      << u"AI/2026-11-15 a b c d e f g"_s;
        QTest::newRow("cut at a word")
                << u"Przygotuj mi proszę dokładny plan przeprowadzki z listą rzeczy do spakowania"_s << u"pl"_s
                << u"AI/2026-11-15 Przygotuj mi proszę dokładny plan przeprowadzki z listą"_s;
        QTest::newRow("one long word") << QString(80, u'ż') << u"pl"_s << u"AI/2026-11-15 "_s + QString(60, u'ż');
        QTest::newRow("the first line, not code") << u"\n```\nkod\n```\n- Druga linia\n"_s << u"pl"_s
                                                  << u"AI/2026-11-15 kod"_s;
        QTest::newRow("a list item") << u"- [ ] Zadanie"_s << u"pl"_s << u"AI/2026-11-15 Zadanie"_s;
        QTest::newRow("nothing left, Polish") << u"???"_s << u"pl"_s << u"AI/2026-11-15 Rozmowa"_s;
        QTest::newRow("nothing left, English") << QString() << u"en"_s << u"AI/2026-11-15 Conversation"_s;
        QTest::newRow("a leading dot") << u"...ukryty"_s << u"pl"_s << u"AI/2026-11-15 ukryty"_s;
    }
    void pageTitles()
    {
        QFETCH(QString, question);
        QFETCH(QString, language);
        QFETCH(QString, title);
        QCOMPARE(pageTitle(question, QDate(2026, 11, 15), language), title);
        QVERIFY(inFolder(title));
    }

    void folders()
    {
        QCOMPARE(folder(), u"AI"_s);
        QVERIFY(inFolder(u"AI/2026-11-15 Pytanie"_s));
        QVERIFY(inFolder(u"ai/x.md"_s));
        QVERIFY(!inFolder(u"AI"_s));
        QVERIFY(!inFolder(u"AIx/a"_s));
        QVERIFY(!inFolder(u"Projekty/AI/a"_s));
        QCOMPARE(journalEntry(u"AI/2026-11-15 Pytanie"_s), u"- 🤖 [[AI/2026-11-15 Pytanie]]"_s);
    }

    void histories()
    {
        Conversation c = parse(u"- 🤖 Witaj\n- 🧑 Pierwsze\n- 🧑 Drugie\n- 🤖\n- 🤖 Odpowiedź\n  dalej\n- 🧑 Trzecie\n"_s);
        const QVariantList h = history(c);
        QCOMPARE(h.size(), 3);
        QCOMPARE(h.at(0).toMap().value(u"role"_s).toString(), u"user"_s);
        QCOMPARE(h.at(0).toMap().value(u"content"_s).toString(), u"Pierwsze\n\nDrugie"_s);
        QCOMPARE(h.at(1).toMap().value(u"role"_s).toString(), u"assistant"_s);
        QCOMPARE(h.at(1).toMap().value(u"content"_s).toString(), u"Odpowiedź\ndalej"_s);
        QCOMPARE(h.at(2).toMap().value(u"content"_s).toString(), u"Trzecie"_s);
        QVERIFY(history(parse(u"- 🤖 tylko AI\n"_s)).isEmpty());
        QVERIFY(history(Conversation{}).isEmpty());
    }

    void previews_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<int>("max");
        QTest::addColumn<QString>("preview");

        QTest::newRow("links and marks") << u"Masz **dwa** zadania w [[Projekty/Dom|domu]] i [[Praca#Plan]]: `x` ==y=="_s << 120
                                         << u"Masz dwa zadania w domu i Praca: x y"_s;
        QTest::newRow("lists and lines") << u"Plan:\n\n- [ ] jeden\n  - dwa\n## Nagłówek\n> cytat"_s << 120
                                         << u"Plan: jeden dwa Nagłówek cytat"_s;
        QTest::newRow("code") << u"```luau\nprint(1)\n```"_s << 120 << u"print(1)"_s;
        QTest::newRow("cut") << u"Zażółć gęślą jaźń"_s << 8 << u"Zażółć…"_s;
        QTest::newRow("a Markdown link") << u"Zobacz [stronę](https://example.com)"_s << 120 << u"Zobacz stronę"_s;
        QTest::newRow("empty") << u"\n\n"_s << 120 << QString();
    }
    void previews()
    {
        QFETCH(QString, markdown);
        QFETCH(int, max);
        QFETCH(QString, preview);
        QCOMPARE(chat::preview(markdown, max), preview);
    }
};

QTEST_GUILESS_MAIN(TestChat)
#include "tst_chat.moc"
