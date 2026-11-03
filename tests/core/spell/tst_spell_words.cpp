// The words of a line of Markdown worth spell-checking (spell/spellwords.h): prose with Polish
// letters, hyphens and apostrophes; code, formulas, links, URLs, tags, task and date tokens, block
// ids, properties, HTML skipped; numbers, acronyms, mixed case, file names and paths skipped;
// surrogate pairs, decomposed letters and malformed syntax.
#include "spell/spellwords.h"

#include <QElapsedTimer>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::spell;

namespace {

QStringList wordsOf(const QString &text, bool lineStart = true)
{
    QStringList out;
    for (const WordSpan &w : findWords(text, lineStart))
        out.append(text.mid(w.from, w.length()));
    return out;
}

} // namespace

class TestSpellWords : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void words_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QStringList>("words");

        // Prose.
        QTest::newRow("empty") << QString() << QStringList{};
        QTest::newRow("only spaces") << u"   \t "_s << QStringList{};
        QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s << QStringList{u"Zażółć"_s, u"gęślą"_s, u"jaźń"_s};
        QTest::newRow("capitals with diacritics") << u"ŹRÓDŁO Źródło"_s << QStringList{u"Źródło"_s};
        QTest::newRow("punctuation") << u"Kot, pies; ryba: „słowo” (nawias) — myślnik!"_s
                                     << QStringList{u"Kot"_s,   u"pies"_s,   u"ryba"_s,
                                                    u"słowo"_s, u"nawias"_s, u"myślnik"_s};
        QTest::newRow("hyphenated") << u"biało-czerwony flag-ship"_s
                                    << QStringList{u"biało-czerwony"_s, u"flag-ship"_s};
        QTest::newRow("unicode hyphen") << u"biało\u2010czerwony"_s << QStringList{u"biało\u2010czerwony"_s};
        QTest::newRow("dash is no hyphen") << u"tak - nie – może"_s << QStringList{u"tak"_s, u"nie"_s, u"może"_s};
        QTest::newRow("hyphens at the ends") << u"-przed po- -oba-"_s << QStringList{u"przed"_s, u"po"_s, u"oba"_s};
        QTest::newRow("apostrophes") << u"don't O’Brien rock’n’roll"_s
                                     << QStringList{u"don't"_s, u"O’Brien"_s, u"rock’n’roll"_s};
        QTest::newRow("quotes are no apostrophes") << u"'cytat' ‘inny’"_s << QStringList{u"cytat"_s, u"inny"_s};
        QTest::newRow("single letters") << u"a i w z kot"_s << QStringList{u"kot"_s};
        QTest::newRow("numbers") << u"mam 3 koty i 2026 rok, 3.14 oraz 10x"_s
                                 << QStringList{u"mam"_s, u"koty"_s, u"rok"_s, u"oraz"_s};
        QTest::newRow("digits in words") << u"mp3 COVID-19 B2B v2 trzy"_s << QStringList{u"trzy"_s};
        QTest::newRow("underscore") << u"snake_case _x_ słowo"_s << QStringList{u"słowo"_s};
        QTest::newRow("acronyms") << u"NASA i PDF oraz OK"_s << QStringList{u"oraz"_s};
        QTest::newRow("acronym inflected") << u"PDF-y"_s << QStringList{u"PDF-y"_s};
        QTest::newRow("mixed case") << u"iPhone QtQuick McDonald Kraków"_s << QStringList{u"Kraków"_s};
        QTest::newRow("file names and domains")
                << u"plik notatka.md i example.com, e.g. tak"_s << QStringList{u"plik"_s, u"tak"_s};
        QTest::newRow("sentence end") << u"Koniec. Początek"_s << QStringList{u"Koniec"_s, u"Początek"_s};
        QTest::newRow("email") << u"pisz na jan.kowalski@example.com dziś"_s
                               << QStringList{u"pisz"_s, u"na"_s, u"dziś"_s};
        QTest::newRow("mention and path")
                << u"@janek src/core/spell ścieżka C:\\Users\\jan"_s << QStringList{u"ścieżka"_s};
        QTest::newRow("latex command outside a formula") << u"\\alpha jest"_s << QStringList{u"jest"_s};
        QTest::newRow("decomposed letters")
                << u"zaz\u0307o\u0301łc\u0301"_s << QStringList{u"zaz\u0307o\u0301łc\u0301"_s};
        QTest::newRow("emoji between words") << u"kot😀pies 🐈 mysz"_s << QStringList{u"kot"_s, u"pies"_s, u"mysz"_s};
        QTest::newRow("letters outside the BMP") << u"𝒜𝒷𝒸 słowo"_s << QStringList{u"𝒜𝒷𝒸"_s, u"słowo"_s};
        QTest::newRow("line break inside") << u"pierwsza\ndruga"_s << QStringList{u"pierwsza"_s, u"druga"_s};
        QTest::newRow("no-break space") << u"kot\u00A0pies"_s << QStringList{u"kot"_s, u"pies"_s};
        QTest::newRow("soft hyphen") << u"dłu\u00ADgie"_s << QStringList{u"dłu\u00ADgie"_s};

        // Emphasis and headings: prose.
        QTest::newRow("emphasis") << u"**gruby** *pochyły* _też_ ~~skreślony~~ ==zakreślony=="_s
                                  << QStringList{u"gruby"_s, u"pochyły"_s, u"też"_s, u"skreślony"_s, u"zakreślony"_s};
        QTest::newRow("heading") << u"## Nagłówek strony"_s << QStringList{u"Nagłówek"_s, u"strony"_s};
        QTest::newRow("list and quote") << u"> - pozycja listy"_s << QStringList{u"pozycja"_s, u"listy"_s};

        // Code, comments, formulas.
        QTest::newRow("inline code") << u"użyj `printf wartośc` teraz"_s << QStringList{u"użyj"_s, u"teraz"_s};
        QTest::newRow("double backticks") << u"a ``kod ` w środku`` koniec"_s << QStringList{u"koniec"_s};
        QTest::newRow("unclosed backtick") << u"zostaje `otwarty"_s << QStringList{u"zostaje"_s, u"otwarty"_s};
        QTest::newRow("comment") << u"widać %%ukryte słowa%% dalej"_s << QStringList{u"widać"_s, u"dalej"_s};
        QTest::newRow("html comment") << u"tekst <!-- uwaga autora --> tak"_s << QStringList{u"tekst"_s, u"tak"_s};
        QTest::newRow("inline math") << u"wzór $\\frac{abc}{xyz}$ obok"_s << QStringList{u"wzór"_s, u"obok"_s};
        QTest::newRow("display math") << u"$$sin cos tan$$ po"_s << QStringList{u"po"_s};
        QTest::newRow("dollars are no formula") << u"kosztuje $5 albo $10"_s << QStringList{u"kosztuje"_s, u"albo"_s};

        // Links.
        QTest::newRow("wiki link") << u"zobacz [[Projekty/Dom Wiśniewskich|dom]] jutro"_s
                                   << QStringList{u"zobacz"_s, u"jutro"_s};
        QTest::newRow("embed") << u"![[Szkic.excalidraw]] obraz"_s << QStringList{u"obraz"_s};
        QTest::newRow("block ref") << u"patrz ((abc-def)) tu"_s << QStringList{u"patrz"_s, u"tu"_s};
        QTest::newRow("markdown link text checked") << u"[strona główna](https://example.com/strona) koniec"_s
                                                    << QStringList{u"strona"_s, u"główna"_s, u"koniec"_s};
        QTest::newRow("markdown link angle") << u"[opis](<Moje notatki/plik.md>)"_s << QStringList{u"opis"_s};
        QTest::newRow("picture alt") << u"![zdjęcie kota](assets/kot.png)"_s << QStringList{u"zdjęcie"_s, u"kota"_s};
        QTest::newRow("bare url") << u"strona https://www.przykład.pl/ścieżka?q=słowo#kotwica i dalej"_s
                                  << QStringList{u"strona"_s, u"dalej"_s};
        QTest::newRow("autolink") << u"<https://example.com/abc> tekst"_s << QStringList{u"tekst"_s};
        QTest::newRow("www") << u"www.przyklad.pl"_s << QStringList{};
        QTest::newRow("malformed link") << u"[[niedomknięty link"_s << QStringList{u"niedomknięty"_s, u"link"_s};

        // Tags, tokens, ids, properties.
        QTest::newRow("tags") << u"#praca #[[Jan Kowalski]] #Remont/Łazienka zrobić"_s << QStringList{u"zrobić"_s};
        QTest::newRow("hash in a word is no tag") << u"C# i F#"_s << QStringList{};
        QTest::newRow("task tokens") << u"Zadzwonić 📅 2026-10-15 ⏰ 2026-10-14 09:00 🔁 co tydzień w poniedziałek"_s
                                     << QStringList{u"Zadzwonić"_s};
        QTest::newRow("priority and done") << u"zrobić ⏫ ✅ 2026-10-01"_s << QStringList{u"zrobić"_s};
        QTest::newRow("logseq priority") << u"[#A] zadanie"_s << QStringList{u"zadanie"_s};
        QTest::newRow("logseq planning") << u"SCHEDULED: <2026-10-15 Thu>"_s << QStringList{};
        QTest::newRow("logseq timestamp") << u"spotkanie <2026-10-15 Thu 10:00>"_s << QStringList{u"spotkanie"_s};
        QTest::newRow("task keyword") << u"TODO kupić mleko"_s << QStringList{u"kupić"_s, u"mleko"_s};
        QTest::newRow("task keyword in a list") << u"- DONE gotowe"_s << QStringList{u"gotowe"_s};
        QTest::newRow("checkbox") << u"- [x] zrobione"_s << QStringList{u"zrobione"_s};
        QTest::newRow("block id") << u"ważny blok ^abc123"_s << QStringList{u"ważny"_s, u"blok"_s};
        QTest::newRow("property") << u"status:: rozpoczęty"_s << QStringList{u"rozpoczęty"_s};
        QTest::newRow("callout") << u"> [!warning] Uwaga tutaj"_s << QStringList{u"Uwaga"_s, u"tutaj"_s};
        QTest::newRow("html") << u"<span class=\"duże\">tekst</span>&nbsp;koniec &amp;"_s
                              << QStringList{u"tekst"_s, u"koniec"_s};
        QTest::newRow("macro") << u"{{embed [[strona]]}} oraz {{query (todo now)}} tak"_s
                               << QStringList{u"oraz"_s, u"tak"_s};
        QTest::newRow("footnote") << u"tekst[^uwaga] dalej"_s << QStringList{u"tekst"_s, u"dalej"_s};
    }

    void words()
    {
        QFETCH(QString, text);
        QFETCH(QStringList, words);
        QCOMPARE(wordsOf(text), words);
    }

    void lineStartOnlyAtStart()
    {
        // Not the start of a line: "TODO" and "key::" are text then (TODO is an acronym anyway).
        QCOMPARE(wordsOf(u"status:: rozpoczęty"_s, false), (QStringList{u"status"_s, u"rozpoczęty"_s}));
        QCOMPARE(wordsOf(u"TODO kupić"_s, false), QStringList{u"kupić"_s});
    }

    void ranges()
    {
        const QString text = u"Zażółć gęślą, np. tak."_s;
        const QList<WordSpan> words = findWords(text);
        QCOMPARE(words.size(), 4);
        QCOMPARE(words[0], (WordSpan{0, 6, false}));
        QCOMPARE(words[1], (WordSpan{7, 12, false}));
        QCOMPARE(words[2], (WordSpan{14, 16, true}));
        QCOMPARE(words[3], (WordSpan{18, 21, true}));
        QCOMPARE(checkedText(text, words[2]), u"np."_s);
        QCOMPARE(checkedText(text, words[0]), u"Zażółć"_s);
        QCOMPARE(checkedText(text, WordSpan{5, 2, false}), QString()); // malformed
        QCOMPARE(checkedText(text, WordSpan{0, 999, false}), QString());
    }

    void surrogatesStayWhole()
    {
        const QString text = u"𝒜𝒷𝒸 słowo"_s;
        const QList<WordSpan> words = findWords(text);
        QCOMPARE(words.size(), 2);
        QCOMPARE(words[0].from, 0);
        QCOMPARE(words[0].to, 6);
    }

    void wordAt_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("position");
        QTest::addColumn<QString>("word");

        QTest::newRow("start") << u"zażółć gęślą"_s << 0 << u"zażółć"_s;
        QTest::newRow("inside") << u"zażółć gęślą"_s << 9 << u"gęślą"_s;
        QTest::newRow("caret after") << u"zażółć gęślą"_s << 6 << u"zażółć"_s;
        QTest::newRow("end") << u"zażółć gęślą"_s << 12 << u"gęślą"_s;
        QTest::newRow("in a link") << u"[[Strona]] tak"_s << 4 << QString();
        QTest::newRow("in code") << u"`kod` tak"_s << 2 << QString();
        QTest::newRow("between") << u"kot  pies"_s << 4 << QString();
        QTest::newRow("out of range") << u"kot"_s << 99 << QString();
        QTest::newRow("negative") << u"kot"_s << -1 << QString();
    }

    void wordAt()
    {
        QFETCH(QString, text);
        QFETCH(int, position);
        QFETCH(QString, word);
        const auto w = brulion::spell::wordAt(text, position);
        QCOMPARE(w ? text.mid(w->from, w->length()) : QString(), word);
    }

    void normalized_data()
    {
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("normalized");
        QTest::newRow("plain") << u"zażółć"_s << u"zażółć"_s;
        QTest::newRow("typographic apostrophe") << u"don’t"_s << u"don't"_s;
        QTest::newRow("modifier apostrophe") << u"donʼt"_s << u"don't"_s;
        QTest::newRow("unicode hyphens") << u"a\u2010b\u2011c"_s << u"a-b-c"_s;
        QTest::newRow("soft hyphen") << u"dłu\u00ADgie"_s << u"długie"_s;
        QTest::newRow("decomposed") << u"z\u0307o\u0301\u0142w"_s << u"żół\u0077"_s;
    }

    void normalized()
    {
        QFETCH(QString, word);
        QFETCH(QString, normalized);
        QCOMPARE(normalizedWord(word), normalized);
    }

    void hyphens()
    {
        QCOMPARE(hyphenParts(u"biało-czerwony"_s), (QStringList{u"biało"_s, u"czerwony"_s}));
        QCOMPARE(hyphenParts(u"a\u2010b"_s), (QStringList{u"a"_s, u"b"_s}));
        QCOMPARE(hyphenParts(u"słowo"_s), QStringList{});
        QCOMPARE(hyphenParts(u"-"_s), QStringList{});
    }

    void longLineIsLinear()
    {
        // A long paragraph (and one long run without spaces) is scanned quickly.
        QString text;
        for (int i = 0; i < 20000; ++i)
            text += u"słowo [[link]] `kod` #tag "_s;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(findWords(text).size(), 20000);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
        const QString run(100000, u'a');
        QCOMPARE(findWords(run).size(), 1);
    }
};

QTEST_GUILESS_MAIN(TestSpellWords)
#include "tst_spell_words.moc"
