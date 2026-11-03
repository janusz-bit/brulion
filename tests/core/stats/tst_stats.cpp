// Word and character counts and the reading time (stats/textstats.h).
#include "stats/textstats.h"

#include <QElapsedTimer>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::stats::CountOptions;
using brulion::stats::TextStats;

class TestStats : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void words_data();
    void words();
    void characters_data();
    void characters();
    void readingMinutes_data();
    void readingMinutes();
    void readable_data();
    void readable();
    void selection();
    void malformed_data();
    void malformed();
    void largePage();
};

void TestStats::words_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("words");

    QTest::newRow("empty") << QString() << 0;
    QTest::newRow("spaces only") << u"  \t \n "_s << 0;
    QTest::newRow("polish sentence") << u"Zażółć gęślą jaźń."_s << 3;
    QTest::newRow("capitals with diacritics") << u"ŁÓDŹ ŚWIĘTOKRZYSKIE ŻÓŁW"_s << 3;
    QTest::newRow("hyphenated") << u"biało-czerwony e-mail Bielsko-Biała"_s << 3;
    QTest::newRow("abbreviations") << u"m.in. np. tzw. itd. p.n.e."_s << 5;
    QTest::newRow("numbers") << u"3,14 10:30 2026-10-14 100% 1/2"_s << 5;
    QTest::newRow("dash alone is no word") << u"Warszawa — Kraków – Gdańsk - Sopot"_s << 4;
    QTest::newRow("punctuation alone") << u"Tak , nie ! ? … „ ”"_s << 2;
    QTest::newRow("quotes stick to words") << u"„Pan Tadeusz” (epopeja)"_s << 3;
    QTest::newRow("emoji alone") << u"Dobrze 👍 🙂 bardzo"_s << 2;
    QTest::newRow("emoji in a word") << u"super👍"_s << 1;
    QTest::newRow("address") << u"zob. https://example.com/a-b?c=d#e"_s << 2;
    QTest::newRow("e-mail address") << u"pisz: jan.kowalski@example.pl"_s << 2;
    // A no-break space after a one-letter word (Polish typesetting) still separates words…
    QTest::newRow("nbsp after one-letter word") << u"Idę w\u00a0las i\u00a0pole"_s << 5;
    // …but groups the thousands of a number.
    QTest::newRow("nbsp groups digits") << u"1\u00a0000\u00a0000 zł"_s << 2;
    QTest::newRow("thin space groups digits") << u"12\u2009345,67"_s << 1;
    QTest::newRow("nbsp between digit and letter") << u"5\u00a0kg"_s << 2;
    QTest::newRow("zero-width space separates") << u"jeden\u200bdwa"_s << 2;
    QTest::newRow("tabs and line breaks") << u"jeden\tdwa\ntrzy\r\ncztery"_s << 4;
    // "ą" as a + combining ogonek is one letter of one word.
    QTest::newRow("decomposed diacritics") << u"za\u0328b ga\u0328ska"_s << 2;
    QTest::newRow("letters beyond the BMP") << u"\U0001D400\U0001D401 abc"_s << 2;
    QTest::newRow("cyrillic and greek") << u"Привет мир αβγ"_s << 3;
    QTest::newRow("apostrophe") << u"don't rock'n'roll"_s << 2;
    QTest::newRow("lone surrogate") << QString(QChar(0xd800)) + u" słowo"_s << 1;
}

void TestStats::words()
{
    QFETCH(QString, text);
    QFETCH(int, words);
    QCOMPARE(brulion::stats::countWords(text), words);
    QCOMPARE(brulion::stats::countPlainText(text).words, words);
}

void TestStats::characters_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("characters");
    QTest::addColumn<int>("noSpaces");

    QTest::newRow("empty") << QString() << 0 << 0;
    QTest::newRow("polish") << u"Zażółć gęślą"_s << 12 << 11;
    QTest::newRow("line breaks are not characters") << u"ab\ncd\r\nef"_s << 6 << 6;
    QTest::newRow("tab and nbsp are spaces") << u"a\tb\u00a0c"_s << 5 << 3;
    QTest::newRow("decomposed ogonek is one") << u"a\u0328"_s << 1 << 1;
    QTest::newRow("emoji with skin tone is one") << u"👍🏽!"_s << 2 << 2;
    QTest::newRow("family emoji is one") << u"👨\u200d👩\u200d👧"_s << 1 << 1;
    QTest::newRow("flag is one") << u"🇵🇱"_s << 1 << 1;
}

void TestStats::characters()
{
    QFETCH(QString, text);
    QFETCH(int, characters);
    QFETCH(int, noSpaces);
    const TextStats stats = brulion::stats::countPlainText(text);
    QCOMPARE(stats.characters, characters);
    QCOMPARE(stats.charactersNoSpaces, noSpaces);
}

void TestStats::readingMinutes_data()
{
    QTest::addColumn<int>("words");
    QTest::addColumn<int>("minutes");

    QTest::newRow("none") << 0 << 0;
    QTest::newRow("negative") << -5 << 0;
    QTest::newRow("one word") << 1 << 1;
    QTest::newRow("below one and a half") << 299 << 1;
    QTest::newRow("one and a half rounds up") << 300 << 2;
    QTest::newRow("exactly two") << 400 << 2;
    QTest::newRow("long") << 10'000 << 50;
}

void TestStats::readingMinutes()
{
    QFETCH(int, words);
    QFETCH(int, minutes);
    QCOMPARE(brulion::stats::readingMinutes(words), minutes);
}

void TestStats::readable_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<QString>("readable");
    QTest::addColumn<int>("words");

    QTest::newRow("plain paragraph") << u"Ala ma kota.\n"_s << u"Ala ma kota."_s << 3;
    QTest::newRow("frontmatter left out")
            << u"---\ntags: [praca, dom]\ntytuł: Zażółć\n---\n# Nagłówek\nTreść strony.\n"_s
            << u"Nagłówek\nTreść strony."_s << 3;
    QTest::newRow("frontmatter after BOM") << u"\ufeff---\na: b\n---\nTekst"_s << u"Tekst"_s << 1;
    QTest::newRow("page properties at the top")
            << u"tags:: praca\nalias:: Zadania\n\nPierwszy akapit"_s << u"Pierwszy akapit"_s << 2;
    QTest::newRow("block properties anywhere")
            << u"- Zadanie domowe\n  priorytet:: wysoki\n  id:: 650c1b2a-1234\n"_s << u"Zadanie domowe"_s << 2;
    QTest::newRow("headings") << u"# Jeden\n## Dwa trzy\n###### Sześć\n#tag zostaje"_s
                              << u"Jeden\nDwa trzy\nSześć\n#tag zostaje"_s << 6;
    QTest::newRow("setext underline") << u"Tytuł\n=====\nTekst"_s << u"Tytuł\nTekst"_s << 2;
    QTest::newRow("list markers") << u"- jeden\n* dwa\n+ trzy\n1. cztery\n12) pięć\n-5 stopni"_s
                                  << u"jeden\ndwa\ntrzy\ncztery\npięć\n-5 stopni"_s << 7;
    QTest::newRow("empty bullet") << u"-\n- \n- tekst"_s << u"tekst"_s << 1;
    QTest::newRow("tasks") << u"- [ ] Kupić mleko 📅 2026-10-15\n- [x] Zrobione ✅ 2026-10-14\n- TODO Zadzwonić [#A]"_s
                           << u"Kupić mleko\nZrobione\nZadzwonić"_s << 4;
    QTest::newRow("reminders and recurrence")
            << u"Spotkanie ⏰ 2026-10-15 09:00 🔁 every week\nPobudka ⏰! 2026-10-16 06:30"_s << u"Spotkanie\nPobudka"_s
            << 2;
    QTest::newRow("block ids") << u"Akapit z id ^abc123\n\n```\nkod\n```\n^kod1"_s << u"Akapit z id"_s << 3;
    QTest::newRow("emphasis") << u"**gruby** *pochyły* ~~skreślony~~ ==zakreślony== `kod`"_s
                              << u"gruby pochyły skreślony zakreślony kod"_s << 5;
    QTest::newRow("wiki links") << u"[[Strona]] [[Folder/Inna strona|etykieta]] [[Strona#Nagłówek]]"_s
                                << u"Strona etykieta Strona#Nagłówek"_s << 3;
    QTest::newRow("markdown link counts its label only")
            << u"Zobacz [ten artykuł](https://example.com/bardzo/dlugi-adres) teraz"_s << u"Zobacz ten artykuł teraz"_s
            << 4;
    QTest::newRow("bare address is one word") << u"https://example.com/a b"_s << u"https://example.com/a b"_s << 2;
    QTest::newRow("autolink") << u"<https://example.com> i <mailto:jan@example.pl>"_s
                              << u"https://example.com i mailto:jan@example.pl"_s << 3;
    QTest::newRow("embeds and pictures") << u"Przed ![[Rysunek.svg]] ![zdjęcie kota](assets/kot.png) po"_s
                                         << u"Przed po"_s << 2;
    QTest::newRow("embedded page alone") << u"![[Inna strona]]\nTekst"_s << u"Tekst"_s << 1;
    QTest::newRow("block references") << u"Jak w ((abc123)) wyżej"_s << u"Jak w wyżej"_s << 3;
    QTest::newRow("footnotes") << u"Zdanie[^1] dalej.\n[^1]: Przypis."_s << u"Zdanie dalej.\nPrzypis."_s << 3;
    QTest::newRow("comments") << u"Widać %%nie widać%% to\n%%\nukryte\nlinie\n%%\nKoniec <!-- html -->"_s
                              << u"Widać to\nKoniec"_s << 3;
    QTest::newRow("html comment over lines") << u"A <!-- jeden\ndwa --> B"_s << u"A\nB"_s << 2;
    QTest::newRow("html tags") << u"<b>pogrubiony</b> <span style=\"x\">tekst</span> a < b i 2 > 1"_s
                               << u"pogrubiony tekst a < b i 2 > 1"_s << 7;
    QTest::newRow("quotes and callouts") << u"> Cytat jeden\n> > zagnieżdżony\n> [!note] Tytuł uwagi\n> treść"_s
                                         << u"Cytat jeden\nzagnieżdżony\nTytuł uwagi\ntreść"_s << 6;
    QTest::newRow("rules") << u"Przed\n---\n***\n_ _ _\nPo"_s << u"Przed\nPo"_s << 2;
    QTest::newRow("tables") << u"| Imię | Wiek |\n|---|:---:|\n| Zażółć | 12 |\n| a \\| b | |"_s
                            << u"Imię Wiek\nZażółć 12\na | b"_s << 6;
    QTest::newRow("fenced code left out") << u"Tekst\n```js\nconst słowo = 1;\n```\nDalej"_s << u"Tekst\nDalej"_s << 2;
    QTest::newRow("tilde fence") << u"~~~~\n```\nwewnątrz\n```\n~~~~\nPo"_s << u"Po"_s << 1;
    QTest::newRow("unclosed fence runs to the end") << u"Tekst\n```\nkod do końca\n"_s << u"Tekst"_s << 1;
    QTest::newRow("fence in a list item")
            << u"- Przykład:\n  ```calc\n  2+2\n  ```\n- Dalej"_s << u"Przykład:\nDalej"_s << 2;
    QTest::newRow("fence in a quote") << u"> ```\n> kod\n> ```\n> cytat"_s << u"cytat"_s << 1;
    QTest::newRow("backticks in info string are no fence") << u"```a`b```\ntekst"_s << u"a`b\ntekst"_s << 2;
    QTest::newRow("query block") << u"```query\ntask where done = false\n```"_s << QString() << 0;
    QTest::newRow("display formula") << u"Wzór:\n$$\nE = mc^2\n$$\n$$a+b$$\nKoniec"_s << u"Wzór:\nKoniec"_s << 2;
    QTest::newRow("escapes") << u"\\*nie kursywa\\* 2\\*3"_s << u"*nie kursywa* 2*3"_s << 3;
    QTest::newRow("crlf") << u"# Tytuł\r\n- punkt\r\n"_s << u"Tytuł\npunkt"_s << 2;
    QTest::newRow("logseq planning") << u"- TODO Raport\n  SCHEDULED: <2026-10-15 Thu>"_s << u"Raport"_s << 1;
    QTest::newRow("tag with spaces") << u"#[[Wiele słów]] koniec"_s << u"#Wiele słów koniec"_s << 3;
}

void TestStats::readable()
{
    QFETCH(QString, markdown);
    QFETCH(QString, readable);
    QFETCH(int, words);
    QCOMPARE(brulion::stats::readableText(markdown), readable);
    const TextStats stats = brulion::stats::countMarkdown(markdown);
    QCOMPARE(stats.words, words);
    QCOMPARE(stats, brulion::stats::countPlainText(readable));
}

void TestStats::selection()
{
    // A selection is no page: a `---` line at its start is a rule, the lines after it are text.
    const QString text = u"---\nsłowo: wartość\n---\nDalej"_s;
    QCOMPARE(brulion::stats::readableText(text, CountOptions{.page = false}), u"słowo: wartość\nDalej"_s);
    QCOMPARE(brulion::stats::readableText(text), u"Dalej"_s);
    // Markup inside a block's selected text is handled as in a page.
    const TextStats inBlock =
            brulion::stats::countMarkdown(u"**Ważne** [[Strona|zadanie]] 📅 2026-10-15"_s, CountOptions{.page = false});
    QCOMPARE(inBlock.words, 2);
    QCOMPARE(inBlock.characters, 13);
    QCOMPARE(inBlock.charactersNoSpaces, 12);
    QCOMPARE(inBlock.readingMinutes, 1);
}

void TestStats::malformed_data()
{
    QTest::addColumn<QString>("markdown");
    QTest::addColumn<int>("words");

    // Frontmatter never closed: no frontmatter — its opening line is a rule, its lines text.
    QTest::newRow("unclosed frontmatter") << u"---\ntytuł: Zażółć\nTreść"_s << 3;
    QTest::newRow("only frontmatter") << u"---\na: b\n---"_s << 0;
    QTest::newRow("unclosed comment hides the rest") << u"Widać %% a potem\nnic"_s << 1;
    QTest::newRow("unclosed html comment") << u"A <!-- B\nC"_s << 1;
    QTest::newRow("unclosed display formula") << u"Tekst\n$$\nx = 1\n"_s << 1;
    QTest::newRow("unpaired emphasis stays") << u"2**3 a == b **pół"_s << 4;
    QTest::newRow("broken link") << u"[[Bez końca i [tekst](bez nawiasu"_s << 5;
    QTest::newRow("lone brackets") << u"[ ] ( ) [[ ]] (( ))"_s << 0;
    QTest::newRow("only markup") << u"# \n- \n> \n|---|\n**"_s << 0;
    QTest::newRow("control characters") << u"a\u0000b\u0007 c"_s << 2;
    QTest::newRow("lone surrogates") << QString(QChar(0xdc00)) + u" "_s + QChar(0xd800) + u"x"_s << 1;
    QTest::newRow("only a BOM") << u"\ufeff"_s << 0;
    QTest::newRow("deep quotes") << QString(u"> "_s.repeated(500) + u"głęboko"_s) << 1;
    QTest::newRow("long list marker number") << u"1234567890. nie lista"_s << 3;
}

void TestStats::malformed()
{
    QFETCH(QString, markdown);
    QFETCH(int, words);
    QCOMPARE(brulion::stats::countMarkdown(markdown).words, words);
}

void TestStats::largePage()
{
    // A long page is counted in one linear pass (the footer counts after a pause in typing).
    QString page = u"---\ntags: [test]\n---\n"_s;
    const QString paragraph = u"- [ ] Zażółć **gęślą** jaźń [[Strona|link]] 📅 2026-10-15 ^abc12%1\n"
                              u"  ```\n  kod się nie liczy\n  ```\n"_s;
    for (int i = 0; i < 20'000; ++i)
        page += paragraph.arg(i);
    QElapsedTimer timer;
    timer.start();
    const TextStats stats = brulion::stats::countMarkdown(page);
    QCOMPARE(stats.words, 20'000 * 4);
    QCOMPARE(stats.readingMinutes, 400);
    QVERIFY2(timer.elapsed() < 10'000, qPrintable(QString::number(timer.elapsed())));
}

QTEST_GUILESS_MAIN(TestStats)
#include "tst_stats.moc"
