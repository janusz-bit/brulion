// Conversations with AI as pages of the notebook (chat/chat.h): a page's messages (the
// frontmatter and a title before them, lines under a message without their indentation, blank
// lines and fenced code in them, a Logseq outline with tabs, CRLF, a line typed between messages,
// look-alikes that are no messages), the page a conversation is about, a message written into a
// page and appended to one byte for byte (reminders kept quiet, code untouched, a fence left open
// closed), the first lines of a new page, its name, the journal's line, the history b.ai gets and
// the previews of the list; the time of each message (its `czas::` / `time::` line, old pages
// without one), pictures sent with a message (embeds, the newest of them as the model's images),
// who answers (the frontmatter's `partner:`) and a conversation too long for the model cut by
// whole turns.
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

    void timesOfMessages_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("roles");
        QTest::addColumn<QString>("times"); // "HH:mm" of each message, "-" none
        QTest::addColumn<QString>("last");

        QTest::newRow("Polish")
                << u"- 🧑 Pytanie\n  czas:: 2026-11-15 09:12\n- 🤖 Odpowiedź\n  czas:: 2026-11-15 09:13\n  dalej\n"_s
                << u"UA"_s << u"09:12 09:13"_s << u"Odpowiedź\ndalej"_s;
        QTest::newRow("English, a list first")
                << u"- 🧑 Q\n  time:: 2026-11-15 21:05\n- 🤖\n  time:: 2026-11-15 21:06\n  - one\n  - two\n"_s
                << u"UA"_s << u"21:05 21:06"_s << u"- one\n- two"_s;
        QTest::newRow("an old page: none")
                << u"- 🧑 Pytanie\n- 🤖 Odpowiedź\n"_s << u"UA"_s << u"- -"_s << u"Odpowiedź"_s;
        QTest::newRow("a tab (Logseq), any case") << u"- 🧑 Pytanie\n\tCZAS:: 2026-11-15 7:05\n\tdalej\n"_s << u"U"_s
                                                  << u"07:05"_s << u"Pytanie\ndalej"_s;
        QTest::newRow("not the first line under it: text")
                << u"- 🧑 Pytanie\n  dalej\n  czas:: 2026-11-15 09:12\n"_s << u"U"_s << u"-"_s
                << u"Pytanie\ndalej\nczas:: 2026-11-15 09:12"_s;
        QTest::newRow("malformed: text") << u"- 🧑 Pytanie\n  czas:: wczoraj\n"_s << u"U"_s << u"-"_s
                                         << u"Pytanie\nczas:: wczoraj"_s;
        QTest::newRow("no such day: text") << u"- 🧑 Pytanie\n  czas:: 2026-02-30 09:12\n"_s << u"U"_s << u"-"_s
                                           << u"Pytanie\nczas:: 2026-02-30 09:12"_s;
        QTest::newRow("no such hour: text") << u"- 🧑 Pytanie\n  czas:: 2026-11-15 25:00\n"_s << u"U"_s << u"-"_s
                                            << u"Pytanie\nczas:: 2026-11-15 25:00"_s;
        QTest::newRow("at the margin: no message's") << u"- 🧑 Pytanie\nczas:: 2026-11-15 09:12\n"_s << u"U"_s << u"-"_s
                                                     << u"Pytanie\nczas:: 2026-11-15 09:12"_s;
        QTest::newRow("CRLF") << u"- 🧑 Ą\r\n  czas:: 2026-11-15 09:12\r\n  ę\r\n"_s << u"U"_s << u"09:12"_s
                              << u"Ą\nę"_s;
    }
    void timesOfMessages()
    {
        QFETCH(QString, page);
        QFETCH(QString, roles);
        QFETCH(QString, times);
        QFETCH(QString, last);
        const Conversation c = parse(page);
        QCOMPARE(::roles(c), roles);
        QStringList got;
        for (const Message &m : c.messages)
            got << (m.time.isValid() ? m.time.toString(u"HH:mm"_s) : u"-"_s);
        QCOMPARE(got.join(u' '), times);
        QCOMPARE(c.messages.last().text, last);
        if (c.messages.first().time.isValid()) {
            QCOMPARE(c.messages.first().time.date(), QDate(2026, 11, 15));
            QCOMPARE(c.messages.first().time.timeSpec(), Qt::LocalTime);
        }
    }

    void writesTimes()
    {
        const QDateTime at(QDate(2026, 11, 15), QTime(9, 12));
        QCOMPARE(formatMessage(Role::User, u"Zażółć"_s, u"\n", at, u"pl"),
                 u"- 🧑 Zażółć\n  czas:: 2026-11-15 09:12\n"_s);
        QCOMPARE(formatMessage(Role::Assistant, u"- jeden\n- dwa"_s, u"\n", at, u"en"),
                 u"- 🤖\n  time:: 2026-11-15 09:12\n  - jeden\n  - dwa\n"_s);
        QCOMPARE(formatMessage(Role::Assistant, QString(), u"\r\n", at, u"pl"),
                 u"- 🤖\r\n  czas:: 2026-11-15 09:12\r\n"_s);
        QCOMPARE(formatMessage(Role::User, u"bez czasu"_s, u"\n", QDateTime(), u"pl"), u"- 🧑 bez czasu\n"_s);
        // The key the page uses already, else its frontmatter's language, else the one given.
        QCOMPARE(appendMessage(u"- 🧑 Q\n  time:: 2026-11-15 09:00\n"_s, Role::Assistant, u"A"_s, at, u"pl"),
                 u"- 🧑 Q\n  time:: 2026-11-15 09:00\n- 🤖 A\n  time:: 2026-11-15 09:12\n"_s);
        QCOMPARE(appendMessage(u"---\nzadania: ignoruj\n---\n- 🧑 Q\n"_s, Role::Assistant, u"A"_s, at, u"en"),
                 u"---\nzadania: ignoruj\n---\n- 🧑 Q\n- 🤖 A\n  czas:: 2026-11-15 09:12\n"_s);
        QCOMPARE(appendMessage(u"- 🧑 Q\n"_s, Role::Assistant, u"A"_s, at, u"en"),
                 u"- 🧑 Q\n- 🤖 A\n  time:: 2026-11-15 09:12\n"_s);
        // Written and read back: the same text and time.
        const QString page = appendMessage(newConversation(u"Co mam jutro?"_s, u"Dom"_s, u"pl"_s, at), Role::Assistant,
                                           u"Jutro:\n\n- czynsz"_s, at.addSecs(60), u"pl"_s);
        const Conversation c = parse(page);
        QCOMPARE(roles(c), u"UA"_s);
        QCOMPARE(c.page, u"Dom"_s);
        QCOMPARE(c.messages.at(0).text, u"Co mam jutro? ([[Dom]])"_s);
        QCOMPARE(c.messages.at(0).time, at);
        QCOMPARE(c.messages.at(0).endLine, 4);
        QCOMPARE(c.messages.at(1).text, u"Jutro:\n\n- czynsz"_s);
        QCOMPARE(c.messages.at(1).time, at.addSecs(60));
        // The model gets no time line.
        QCOMPARE(history(c).at(0).toMap().value(u"content"_s).toString(), u"Co mam jutro? ([[Dom]])"_s);
    }

    void partners_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("partner");
        QTest::addColumn<QString>("expected");

        QTest::newRow("added at the end of the frontmatter")
                << u"---\nzadania: ignoruj\n---\n- 🧑 Ą\n"_s << u"pirat/rozmowa"_s
                << u"---\nzadania: ignoruj\npartner: pirat/rozmowa\n---\n- 🧑 Ą\n"_s;
        QTest::newRow("replaced") << u"---\npartner: stary\ntasks: ignore\n---\n- 🧑 Ą\n"_s << u"nowy"_s
                                  << u"---\npartner: nowy\ntasks: ignore\n---\n- 🧑 Ą\n"_s;
        QTest::newRow("taken out") << u"---\nzadania: ignoruj\nPartner: stary\n---\n- 🧑 Ą\n"_s << QString()
                                   << u"---\nzadania: ignoruj\n---\n- 🧑 Ą\n"_s;
        QTest::newRow("taken out, none there") << u"- 🧑 Ą\n"_s << QString() << u"- 🧑 Ą\n"_s;
        QTest::newRow("no frontmatter") << u"- 🧑 Ą\n"_s << u"pirat"_s << u"---\npartner: pirat\n---\n- 🧑 Ą\n"_s;
        QTest::newRow("CRLF") << u"---\r\nzadania: ignoruj\r\npartner: a\r\n---\r\n- 🧑 Ą\r\n"_s << u"b"_s
                              << u"---\r\nzadania: ignoruj\r\npartner: b\r\n---\r\n- 🧑 Ą\r\n"_s;
        QTest::newRow("an unclosed frontmatter")
                << u"---\n- 🧑 Ą\n"_s << u"b"_s << u"---\npartner: b\n---\n---\n- 🧑 Ą\n"_s;
    }
    void partners()
    {
        QFETCH(QString, content);
        QFETCH(QString, partner);
        QFETCH(QString, expected);
        const QString changed = setPartner(content, partner);
        QCOMPARE(changed, expected);
        QCOMPARE(parse(changed).partner, partner);
        QCOMPARE(setPartner(changed, partner), changed); // (again: nothing more)
    }

    void partnerOfAPage()
    {
        QCOMPARE(parse(u"---\npartner: \"pirat/rozmowa\"\n---\n- 🧑 Ą\n"_s).partner, u"pirat/rozmowa"_s);
        QCOMPARE(parse(u"---\nzadania: ignoruj\n---\n- 🧑 Ą\n"_s).partner, QString());
        QCOMPARE(parse(u"- 🧑 Ą\n  partner: x\n"_s).partner, QString()); // (no frontmatter's)
        QCOMPARE(newConversation(u"Ahoj"_s, QString(), u"pl"_s, QDateTime(), u"pirat"_s),
                 u"---\nzadania: ignoruj\npartner: pirat\n---\n- 🧑 Ahoj\n"_s);
    }

    void pictures_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("pictures");

        QTest::newRow("an embed") << u"Co to?\n![[Pasted image 20261115091200.jpg]]"_s
                                  << QStringList{u"Pasted image 20261115091200.jpg"_s};
        QTest::newRow("a size and a Markdown picture") << u"![[a.PNG|300]] i ![zdjęcie](assets/żółw.webp \"tytuł\")"_s
                                                       << QStringList{u"a.PNG"_s, u"assets/żółw.webp"_s};
        QTest::newRow("each once") << u"![[a.jpg]] ![[a.jpg]]"_s << QStringList{u"a.jpg"_s};
        QTest::newRow("no pictures") << u"![[Notatka]] ![[plik.pdf]] [[b.jpg]]"_s << QStringList();
        QTest::newRow("from the internet") << u"![](https://example.com/a.jpg)"_s << QStringList();
        QTest::newRow("in code") << u"`![[a.jpg]]`\n```\n![[b.jpg]]\n```\n![[c.gif]]"_s << QStringList{u"c.gif"_s};
        QTest::newRow("malformed") << u"![[a.jpg\n![](b.jpg\n![[]]"_s << QStringList();
    }
    void pictures()
    {
        QFETCH(QString, markdown);
        QFETCH(QStringList, pictures);
        QCOMPARE(picturesIn(markdown), pictures);
    }

    void picturesSent()
    {
        QCOMPARE(withPictures(u"  Co to za roślina?  "_s, {u"a.jpg"_s, u"assets/b.png"_s}),
                 u"Co to za roślina?\n![[a.jpg]]\n![[assets/b.png]]"_s);
        QCOMPARE(withPictures(QString(), {u"a.jpg"_s}), u"![[a.jpg]]"_s);
        QCOMPARE(withoutPictures(u"Co to?\n![[a.jpg]]\nA to ![[b.png|200]] tu\n![[Notatka]]"_s),
                 u"Co to?\nA to  tu\n![[Notatka]]"_s);
        QCOMPARE(withoutPictures(u"![[a.jpg]]"_s), QString());
        // A message written with its pictures: the model gets them as images, not as text.
        QString page = appendMessage(QString(), Role::User, withPictures(u"Co to?"_s, {u"a.jpg"_s}));
        page = appendMessage(page, Role::Assistant, u"Paproć."_s);
        page = appendMessage(page, Role::User, withPictures(QString(), {u"b.jpg"_s, u"c.jpg"_s}));
        const Conversation c = parse(page);
        QCOMPARE(c.messages.at(0).pictures, QStringList{u"a.jpg"_s});
        const QVariantList h = history(c);
        QCOMPARE(h.size(), 3);
        QCOMPARE(h.at(0).toMap().value(u"content"_s).toString(), u"Co to?"_s);
        QCOMPARE(h.at(0).toMap().value(u"images"_s).toStringList(), QStringList{u"a.jpg"_s});
        QCOMPARE(h.at(1).toMap().contains(u"images"_s), false);
        // (A message of pictures alone is no empty message.)
        QCOMPARE(h.at(2).toMap().value(u"content"_s).toString(), QString());
        QCOMPARE(h.at(2).toMap().value(u"images"_s).toStringList(), (QStringList{u"b.jpg"_s, u"c.jpg"_s}));
        // Without pictures (a model that does not see): the embeds stay text.
        QCOMPARE(history(c, 0, false).at(0).toMap().value(u"content"_s).toString(), u"Co to?\n![[a.jpg]]"_s);
        QVERIFY(!history(c, 0, false).at(0).toMap().contains(u"images"_s));
    }

    void theNewestPictures()
    {
        // Ten pictures in three messages: the newest eight go as pictures, the oldest stay embeds.
        QString page;
        page = appendMessage(page, Role::User, withPictures(u"Pierwsze"_s, {u"1.jpg"_s, u"2.jpg"_s, u"3.jpg"_s}));
        page = appendMessage(page, Role::Assistant, u"Widzę."_s);
        page = appendMessage(page, Role::User, withPictures(u"Drugie"_s, {u"4.jpg"_s, u"5.jpg"_s, u"6.jpg"_s}));
        page = appendMessage(page, Role::Assistant, u"Też."_s);
        page = appendMessage(page, Role::User,
                             withPictures(u"Trzecie"_s, {u"7.jpg"_s, u"8.jpg"_s, u"9.jpg"_s, u"10.jpg"_s}));
        const QVariantList h = history(parse(page));
        QCOMPARE(h.size(), 5);
        QCOMPARE(h.at(0).toMap().value(u"images"_s).toStringList(), QStringList{u"3.jpg"_s});
        QCOMPARE(h.at(0).toMap().value(u"content"_s).toString(), u"Pierwsze\n![[1.jpg]]\n![[2.jpg]]"_s);
        QCOMPARE(h.at(2).toMap().value(u"images"_s).toStringList().size(), 3);
        QCOMPARE(h.at(4).toMap().value(u"images"_s).toStringList().size(), 4);
        int all = 0;
        for (const QVariant &m : h)
            all += int(m.toMap().value(u"images"_s).toStringList().size());
        QCOMPARE(all, MaxPictures);
    }

    void cutByTurns_data()
    {
        QTest::addColumn<QStringList>("messages"); // "U:text" / "A:text"
        QTest::addColumn<int>("budget");
        QTest::addColumn<int>("first");

        const QString ten(10, u'ż');
        const QStringList four{u"U:"_s + ten, u"A:"_s + ten, u"U:"_s + ten, u"A:"_s + ten, u"U:"_s + ten};
        QTest::newRow("no budget: all") << four << 0 << 0;
        QTest::newRow("it fits: all") << four << 50 << 0;
        QTest::newRow("the oldest turn out") << four << 49 << 2;
        QTest::newRow("only the latest turn") << four << 10 << 4;
        QTest::newRow("the latest too long: still sent") << four << 3 << 4;
        QTest::newRow("a run of the user's messages is one turn")
                << QStringList{u"U:"_s + ten, u"A:"_s + ten, u"U:"_s + ten, u"U:"_s + ten, u"A:"_s + ten, u"U:"_s + ten}
                << 45 << 2;
        QTest::newRow("the AI's first") << QStringList{u"A:"_s + ten, u"U:"_s + ten, u"A:"_s + ten, u"U:"_s + ten} << 25
                                        << 3;
        QTest::newRow("a picture counts") << QStringList{u"U:![[a.jpg]]"_s, u"A:"_s + ten, u"U:"_s + ten} << 1000 << 2;
    }
    void cutByTurns()
    {
        QFETCH(QStringList, messages);
        QFETCH(int, budget);
        QFETCH(int, first);
        QString page;
        for (const QString &m : messages)
            page = appendMessage(page, m.startsWith(u'U') ? Role::User : Role::Assistant, m.mid(2));
        const Conversation c = parse(page);
        QCOMPARE(c.messages.size(), messages.size());
        QCOMPARE(firstSent(c, budget), first);
        // What is sent then starts with the user's turn and ends with the question.
        const QVariantList h = history(c, first);
        QVERIFY(!h.isEmpty());
        QCOMPARE(h.first().toMap().value(u"role"_s).toString(), u"user"_s);
        QCOMPARE(h.last().toMap().value(u"role"_s).toString(), u"user"_s);
    }

    void costs()
    {
        Message m;
        m.text = u"Zażółć"_s;
        QCOMPARE(cost(m), 6);
        m.pictures = {u"a.jpg"_s, u"b.jpg"_s};
        QCOMPARE(cost(m), 6 + 2 * PictureChars);
        QCOMPARE(firstSent(Conversation{}, 10), 0);
        QCOMPARE(history(Conversation{}, 5).size(), 0);
        QCOMPARE(history(parse(u"- 🧑 Ą\n"_s), -3).size(), 1);
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
    // ---- rewinding, forking, prompts, quotes ----------------------------------------------------

    void truncates_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("from");
        QTest::addColumn<QString>("expected");
        const QString page = u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro? ([[Dom]])\n  czas:: 2026-11-15 09:12\n"
                             "- 🤖 Trzy zadania:\n\n  - zapłacić czynsz\n- 🧑 A pojutrze?\n- 🤖 Nic.\n"_s;
        QTest::newRow("from the second question") << page << 2
                                                  << u"---\nzadania: ignoruj\n---\n- 🧑 Co mam jutro? ([[Dom]])\n  czas:: 2026-11-15 09:12\n"
                                                     "- 🤖 Trzy zadania:\n\n  - zapłacić czynsz\n"_s;
        QTest::newRow("from the first") << page << 0 << u"---\nzadania: ignoruj\n---\n"_s;
        QTest::newRow("the last") << page << 3 << page.left(page.indexOf(u"- 🤖 Nic."_s));
        QTest::newRow("no such message") << page << 4 << page;
        QTest::newRow("negative") << page << -1 << page;
        QTest::newRow("crlf kept") << u"- 🧑 Pytanie\r\n- 🤖 Odpowiedź\r\n- 🧑 Źle\r\n"_s << 2
                                   << u"- 🧑 Pytanie\r\n- 🤖 Odpowiedź\r\n"_s;
        QTest::newRow("no final break") << u"- 🧑 A\n- 🤖 B"_s << 1 << u"- 🧑 A\n"_s;
        QTest::newRow("a fenced look-alike is no message")
                << u"- 🧑 Kod?\n- 🤖 Proszę:\n  ```\n- 🧑 nie wiadomość\n  ```\n- 🧑 Dzięki\n"_s << 2
                << u"- 🧑 Kod?\n- 🤖 Proszę:\n  ```\n- 🧑 nie wiadomość\n  ```\n"_s;
    }
    void truncates()
    {
        QFETCH(QString, content);
        QFETCH(int, from);
        QFETCH(QString, expected);
        QCOMPARE(truncated(content, from), expected);
    }

    void forks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("upTo");
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        const QString page = u"---\nzadania: ignoruj\npartner: acp:claude\n---\n- 🧑 Plan podróży ([[Kraków]])\n"
                             "  czas:: 2026-11-15 09:12\n- 🤖 Pociąg o 7:15.\n  czas:: 2026-11-15 09:13\n- 🧑 A autobus?\n"_s;
        QTest::newRow("up to the answer")
                << page << 1 << u"AI/2026-11-15 Plan podróży.md"_s
                << u"---\nzadania: ignoruj\npartner: acp:claude\nfork: \"[[AI/2026-11-15 Plan podróży]]\"\n---\n"
                   "- 🧑 Plan podróży ([[Kraków]])\n  czas:: 2026-11-15 09:12\n- 🤖 Pociąg o 7:15.\n  czas:: 2026-11-15 09:13\n"_s;
        QTest::newRow("the whole") << page << 2 << u"AI/x"_s
                                   << u"---\nzadania: ignoruj\npartner: acp:claude\nfork: \"[[AI/x]]\"\n---\n"
                                      "- 🧑 Plan podróży ([[Kraków]])\n  czas:: 2026-11-15 09:12\n- 🤖 Pociąg o 7:15.\n"
                                      "  czas:: 2026-11-15 09:13\n- 🧑 A autobus?\n"_s;
        QTest::newRow("no frontmatter, crlf, no final break")
                << u"- 🧑 Ąę\r\n- 🤖 Óź"_s << 1 << u"AI/b.md"_s
                << u"---\r\nfork: \"[[AI/b]]\"\r\n---\r\n- 🧑 Ąę\r\n- 🤖 Óź\r\n"_s;
        QTest::newRow("a fork of a fork: its link replaced")
                << u"---\nfork: \"[[AI/a]]\"\n---\n- 🧑 X\n"_s << 0 << u"AI/b"_s
                << u"---\nfork: \"[[AI/b]]\"\n---\n- 🧑 X\n"_s;
        QTest::newRow("no such message") << page << 3 << u"AI/x"_s << QString();
    }
    void forks()
    {
        QFETCH(QString, content);
        QFETCH(int, upTo);
        QFETCH(QString, source);
        QFETCH(QString, expected);
        const QString out = forked(content, upTo, source);
        QCOMPARE(out, expected);
        if (!expected.isEmpty()) {
            QString title = source;
            if (title.endsWith(u".md"_s))
                title.chop(3);
            QCOMPARE(parse(out).fork, title);
            QCOMPARE(parse(out).messages.size(), upTo + 1);
        }
    }

    void frontmatterValues_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("key");
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("expected");
        QTest::newRow("added") << u"---\nzadania: ignoruj\n---\n- 🧑 A\n"_s << u"fork"_s << u"x"_s
                               << u"---\nzadania: ignoruj\nfork: x\n---\n- 🧑 A\n"_s;
        QTest::newRow("replaced, case") << u"---\nFork: y\n---\n"_s << u"fork"_s << u"x"_s << u"---\nfork: x\n---\n"_s;
        QTest::newRow("removed") << u"---\nfork: y\nzadania: ignoruj\n---\n"_s << u"fork"_s << QString()
                                 << u"---\nzadania: ignoruj\n---\n"_s;
        QTest::newRow("removing what is not there") << u"- 🧑 A\n"_s << u"fork"_s << QString() << u"- 🧑 A\n"_s;
        QTest::newRow("made") << u"- 🧑 A\n"_s << u"fork"_s << u"x"_s << u"---\nfork: x\n---\n- 🧑 A\n"_s;
        QTest::newRow("a key that only starts alike stays") << u"---\nforkage: 1\n---\n"_s << u"fork"_s << u"2"_s
                                                            << u"---\nforkage: 1\nfork: 2\n---\n"_s;
        QTest::newRow("crlf") << u"---\r\na: b\r\n---\r\n"_s << u"c"_s << u"d"_s << u"---\r\na: b\r\nc: d\r\n---\r\n"_s;
    }
    void frontmatterValues()
    {
        QFETCH(QString, content);
        QFETCH(QString, key);
        QFETCH(QString, value);
        QFETCH(QString, expected);
        QCOMPARE(setFrontmatterValue(content, key, value), expected);
    }

    void promptsBack()
    {
        const Conversation c = parse(u"- 🧑 Co mam jutro? ([[Dom]])\n- 🤖 Nic.\n- 🧑 A pojutrze?\n  ![[foto.png]]\n"
                                     "- 🤖 Też nic.\n- 🧑 A pojutrze?\n- 🧑 Zażółć\n  gęślą jaźń\n"_s);
        QCOMPARE(prompts(c), (QStringList{u"Zażółć\ngęślą jaźń"_s, u"A pojutrze?"_s, u"Co mam jutro?"_s}));
        QCOMPARE(prompts(parse(u"- 🧑 ![[x.png]]\n"_s)), QStringList());
        QCOMPARE(prompts(parse(u"- 🤖 Cześć\n- 🧑 Pytanie ([[Strona]]) dalej\n"_s)),
                 QStringList{u"Pytanie ([[Strona]]) dalej"_s});
    }

    void quotes_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("expected");
        QTest::newRow("one line") << u"Pociąg o 7:15."_s << u"> Pociąg o 7:15.\n\n"_s;
        QTest::newRow("lines, a blank one") << u"Pierwsza\n\n- druga"_s << u"> Pierwsza\n>\n> - druga\n\n"_s;
        QTest::newRow("pictures out") << u"Zdjęcie:\n![[a.png]]"_s << u"> Zdjęcie:\n\n"_s;
        QTest::newRow("only a picture") << u"![[a.png]]"_s << QString();
    }
    void quotes()
    {
        QFETCH(QString, markdown);
        QFETCH(QString, expected);
        QCOMPARE(quoted(markdown), expected);
    }
};

QTEST_GUILESS_MAIN(TestChat)
#include "tst_chat.moc"
