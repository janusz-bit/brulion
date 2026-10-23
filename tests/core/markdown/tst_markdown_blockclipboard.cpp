// What a block selection copies (markdown/blockclipboard.h, docs/UI.md "Selecting and copying"):
// the top rows of a selection, the exact Markdown of the blocks re-based to column 0 (tabs,
// spaces, mixed indentation, ranges rising through levels, CRLF, BOM, frontmatter, code fences,
// tables, formulas, ids, Polish text with no-break spaces), the plain text of "Kopiuj jako
// tekst", inline markup stripped, and block ids a paste must not duplicate — also on malformed
// input.
#include "markdown/blockclipboard.h"
#include "markdown/blockdocument.h"

#include <QElapsedTimer>
#include <QHash>
#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

using Rows = QList<int>;

namespace {

/// "Kupić żółć i źdźbło — ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ" with no-break spaces after one-letter words.
const QString Polish = u"Kupić żółć i\u00a0źdźbło w\u00a0domu — ąćęłńóśźż ĄĆĘŁŃÓŚŹŻ"_s;

} // namespace

class TestBlockClipboard : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void topRowsOfASelection_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<Rows>("rows");
        QTest::addColumn<Rows>("tops");
        const QString outline = u"- a\n\t- b\n\t\t- c\n\t- d\n- e\n"_s;
        QTest::newRow("a parent and its children") << outline << Rows{0, 1, 2, 3} << Rows{0};
        QTest::newRow("children only") << outline << Rows{2, 1} << Rows{1};
        QTest::newRow("rising through levels") << outline << Rows{2, 3, 4} << Rows{2, 3, 4};
        QTest::newRow("duplicates and order") << outline << Rows{4, 3, 3, 1, 4} << Rows{1, 3, 4};
        QTest::newRow("invalid rows") << outline << Rows{-1, 5, 99} << Rows{};
        QTest::newRow("empty") << outline << Rows{} << Rows{};
        QTest::newRow("frontmatter never") << u"---\na: 1\n---\ntyp:: plan\n\n- x\n"_s << Rows{0, 1, 2} << Rows{2};
    }
    void topRowsOfASelection()
    {
        QFETCH(QString, content);
        QFETCH(Rows, rows);
        QFETCH(Rows, tops);
        QCOMPARE(topRows(BlockDocument(content), rows), tops);
    }

    void markdownOfBlocks_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<Rows>("rows");
        QTest::addColumn<QString>("expected");

        QTest::newRow("one paragraph") << u"Akapit pierwszy.\n\nAkapit drugi.\n"_s << Rows{0} << u"Akapit pierwszy."_s;
        QTest::newRow("an item with tab-indented children")
                << u"- a\n\t- b\n\t\t- c\n- d\n"_s << Rows{0} << u"- a\n\t- b\n\t\t- c"_s;
        QTest::newRow("a parent and its children, selected one by one")
                << u"- a\n\t- b\n\t\t- c\n- d\n"_s << Rows{0, 1, 2} << u"- a\n\t- b\n\t\t- c"_s;
        QTest::newRow("nested, tabs") << u"- a\n\t- b\n\t\t- c\n\t- e\n"_s << Rows{1} << u"- b\n\t- c"_s;
        QTest::newRow("nested, two spaces") << u"- a\n  - b\n    - c\n"_s << Rows{1} << u"- b\n  - c"_s;
        QTest::newRow("nested, four spaces") << u"- a\n    - b\n        - c\n"_s << Rows{1} << u"- b\n    - c"_s;
        // b at column 2, c at column 4 written with a tab: the tab keeps 2 of its 4 columns.
        QTest::newRow("nested, mixed tabs and spaces") << u"- a\n  - b\n\t- c\n"_s << Rows{1} << u"- b\n  - c"_s;
        QTest::newRow("continuation lines") << u"- a\n\t- b\n\t  ciąg dalszy\n"_s << Rows{1} << u"- b\n  ciąg dalszy"_s;
        QTest::newRow("rising through levels (2 -> 1 -> 0)")
                << u"- a\n\t- b\n\t\t- c\n\t- d\n- e\n"_s << Rows{2, 3, 4} << u"- c\n- d\n- e"_s;
        QTest::newRow("rising with children") << u"- a\n\t- b\n\t\t- c\n\t\t\t- c1\n\t- d\n\t\t- d1\n"_s << Rows{2, 4}
                                              << u"- c\n\t- c1\n- d\n\t- d1"_s;
        QTest::newRow("CRLF in, LF out, no BOM")
                << u"\uFEFF- a\r\n\t- b\r\n- c\r\n"_s << Rows{0, 1, 2} << u"- a\n\t- b\n- c"_s;
        QTest::newRow("old Mac line ends") << u"Pierwszy\r\rDrugi\r"_s << Rows{0, 1} << u"Pierwszy\n\nDrugi"_s;
        QTest::newRow("blank lines kept, none trailing")
                << u"Akapit 1\n\n\nAkapit 2\n\nAkapit 3\n"_s << Rows{0, 1} << u"Akapit 1\n\n\nAkapit 2"_s;
        QTest::newRow("a loose list item")
                << u"- a\n\n  dalszy akapit\n- b\n"_s << Rows{0} << u"- a\n\n  dalszy akapit"_s;
        QTest::newRow("numbering as written") << u"1. a\n2. b\n3. c\n4. d\n"_s << Rows{2, 3} << u"3. c\n4. d"_s;
        QTest::newRow("tasks with their dates")
                << u"- [x] Zrobione ✅ 2026-10-15\n- [ ] "_s + Polish + u" 📅 2026-10-20\n"_s << Rows{0, 1}
                << u"- [x] Zrobione ✅ 2026-10-15\n- [ ] "_s + Polish + u" 📅 2026-10-20"_s;
        QTest::newRow("Polish text and no-break spaces exactly")
                << u"- "_s + Polish + u"\n"_s << Rows{0} << u"- "_s + Polish;
        QTest::newRow("a code fence") << u"Tekst\n\n```js\nconst a = 1;\n\n```\n\nPo\n"_s << Rows{1}
                                      << u"```js\nconst a = 1;\n\n```"_s;
        QTest::newRow("an unclosed code fence")
                << u"- a\n\n```\nkod bez końca\n"_s << Rows{1} << u"```\nkod bez końca"_s;
        QTest::newRow("a callout") << u"> [!note] Uwaga\n> treść\n\nPo\n"_s << Rows{0} << u"> [!note] Uwaga\n> treść"_s;
        QTest::newRow("a table") << u"| a | b |\n|---|---|\n| 1 | 2 |\n"_s << Rows{0}
                                 << u"| a | b |\n|---|---|\n| 1 | 2 |"_s;
        QTest::newRow("a formula") << u"$$\nx^2\n$$\n\nTekst\n"_s << Rows{0} << u"$$\nx^2\n$$"_s;
        QTest::newRow("ids and properties")
                << u"- zadanie ^abc123\n  typ:: plan\n- inne\n"_s << Rows{0} << u"- zadanie ^abc123\n  typ:: plan"_s;
        QTest::newRow("frontmatter left out")
                << u"---\ntytuł: x\n---\n# Nagłówek\n\nTekst\n"_s << Rows{0, 1, 2} << u"# Nagłówek\n\nTekst"_s;
        QTest::newRow("page properties left out") << u"tags:: x\n\n- a\n"_s << Rows{0, 1} << u"- a"_s;
        QTest::newRow("a list and a paragraph") << u"- a\n- b\n\nAkapit\n"_s << Rows{1, 2} << u"- b\n\nAkapit"_s;
        QTest::newRow("not adjacent: separated as written")
                << u"Akapit 1\n\nAkapit 2\n\nAkapit 3\n"_s << Rows{0, 2} << u"Akapit 1\n\nAkapit 3"_s;
        QTest::newRow("not adjacent: a blank line where they would merge")
                << u"- a\n- b\n\nAkapit\n"_s << Rows{0, 2} << u"- a\n\nAkapit"_s;
        QTest::newRow("nothing selected") << u"- a\n"_s << Rows{} << QString();
        QTest::newRow("invalid rows") << u"- a\n"_s << Rows{-3, 7} << QString();
        QTest::newRow("an empty document") << QString() << Rows{0} << QString();
        QTest::newRow("a lone empty bullet") << u"- \n- b\n"_s << Rows{0} << u"- "_s;
        QTest::newRow("lazy continuation") << u"- a\n\t- b\nleniwa linia\n"_s << Rows{1} << u"- b\nleniwa linia"_s;
    }
    void markdownOfBlocks()
    {
        QFETCH(QString, content);
        QFETCH(Rows, rows);
        QFETCH(QString, expected);
        const BlockDocument doc(content);
        QCOMPARE(blocksMarkdown(doc, topRows(doc, rows)), expected);
    }

    void copiedMarkdownReadsBackAsTheSameBlocks()
    {
        // The copy of a nested range parses back into the same blocks with the same relative depths.
        const QString content = u"- a\n\t- b\n\t\t- [ ] c 📅 2026-10-20\n\t\t\t- c1\n\t- d\n- e\n"_s;
        const BlockDocument doc(content);
        const BlockDocument copy(blocksMarkdown(doc, topRows(doc, {2, 3, 4, 5})));
        QCOMPARE(copy.count(), 4);
        QCOMPARE(copy.at(0).body, u"[ ] c 📅 2026-10-20"_s);
        QCOMPARE(copy.at(0).depth, 0);
        QCOMPARE(copy.at(1).depth, 1);
        QCOMPARE(copy.at(2).depth, 0);
        QCOMPARE(copy.at(3).depth, 0);
    }

    void plainText_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<Rows>("rows");
        QTest::addColumn<QString>("expected");

        QTest::newRow("bullets, two spaces per level")
                << u"- a\n\t- b\n\t\t- c\n- d\n"_s << Rows{0, 3} << u"• a\n  • b\n    • c\n• d"_s;
        QTest::newRow("relative to the top item") << u"- a\n\t- b\n\t\t- c\n"_s << Rows{1} << u"• b\n  • c"_s;
        QTest::newRow("numbers as written")
                << u"1. pierwszy\n2. drugi\n   1) pod\n"_s << Rows{0, 1} << u"1. pierwszy\n2. drugi\n  1) pod"_s;
        QTest::newRow("tasks") << u"- [ ] Kupić mleko\n- [x] Zrobione ✅ 2026-10-15\n- [-] Anulowane\n- [/] W toku\n"_s
                               << Rows{0, 1, 2, 3}
                               << u"☐ Kupić mleko\n☑ Zrobione ✅ 2026-10-15\n☒ Anulowane\n☐ W toku"_s;
        QTest::newRow("a numbered task") << u"1. [ ] pierwsze\n"_s << Rows{0} << u"1. ☐ pierwsze"_s;
        QTest::newRow("headings without #, emphasis gone")
                << u"# Tytuł\n\nTekst **pogrubiony** i *kursywa* oraz ==ważne==\n"_s << Rows{0, 1}
                << u"Tytuł\n\nTekst pogrubiony i kursywa oraz ważne"_s;
        QTest::newRow("a closed ATX heading") << u"## Plan dnia ##\n"_s << Rows{0} << u"Plan dnia"_s;
        QTest::newRow("a setext heading") << u"Rozdział\n===\n"_s << Rows{0} << u"Rozdział"_s;
        QTest::newRow("a Logseq heading item")
                << u"- ## Praca\n\t- zadanie\n"_s << Rows{0} << u"• Praca\n  • zadanie"_s;
        QTest::newRow("links as labels, tags and dates stay")
                << u"Zobacz [[Strona|etykieta]], [[Inna]] i [opis](https://example.com) #tag 📅 2026-10-20\n"_s
                << Rows{0} << u"Zobacz etykieta, Inna i opis #tag 📅 2026-10-20"_s;
        QTest::newRow("code without fences, verbatim")
                << u"```js\nconst a = 1; // **nie** zmieniaj\n\tif (x) {}\n```\n"_s << Rows{0}
                << u"const a = 1; // **nie** zmieniaj\n\tif (x) {}"_s;
        QTest::newRow("block references resolved")
                << u"Patrz ((abc123)) i ((nieznany))\n"_s << Rows{0} << u"Patrz Treść bloku i ((nieznany))"_s;
        QTest::newRow("ids left out") << u"- zadanie ^abc123\n  id:: 64f0-11\n  typ:: plan\n"_s << Rows{0}
                                      << u"• zadanie\n  typ:: plan"_s;
        QTest::newRow("a quote") << u"> Cytat *ważny*\n> druga linia\n"_s << Rows{0} << u"Cytat ważny\ndruga linia"_s;
        QTest::newRow("a callout") << u"> [!note]- Tytuł\n> treść\n"_s << Rows{0} << u"Tytuł\ntreść"_s;
        QTest::newRow("a table as written")
                << u"| a | **b** |\n|---|---|\n| 1 | 2 |\n"_s << Rows{0} << u"| a | **b** |\n|---|---|\n| 1 | 2 |"_s;
        QTest::newRow("Polish and no-break spaces") << u"- "_s + Polish + u"\n"_s << Rows{0} << u"• "_s + Polish;
        QTest::newRow("continuation lines") << u"- pierwsza\n  druga\n"_s << Rows{0} << u"• pierwsza\n  druga"_s;
        QTest::newRow("a rule") << u"Przed\n\n---\n\nPo\n"_s << Rows{0, 1, 2} << u"Przed\n\n———\n\nPo"_s;
        QTest::newRow("frontmatter left out") << u"---\na: 1\n---\nTekst\n"_s << Rows{0, 1} << u"Tekst"_s;
        QTest::newRow("an empty item") << u"- \n"_s << Rows{0} << u"•"_s;
        QTest::newRow("nothing") << u"- a\n"_s << Rows{} << QString();
    }
    void plainText()
    {
        QFETCH(QString, content);
        QFETCH(Rows, rows);
        QFETCH(QString, expected);
        const BlockDocument doc(content);
        PlainTextOptions options;
        options.blockText = [](const QString &id) { return id == u"abc123" ? u"Treść **bloku**"_s : QString(); };
        QCOMPARE(blocksPlainText(doc, topRows(doc, rows), options), expected);
    }

    void inlineMarkup_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("wiki links") << u"[[Strona]] i [[Strona|alias]]"_s << u"Strona i alias"_s;
        QTest::newRow("an embed") << u"![[obraz.png]]"_s << u"obraz.png"_s;
        QTest::newRow("Markdown links and images") << u"[tekst](https://a.pl) ![alt](b.png)"_s << u"tekst alt"_s;
        QTest::newRow("emphasis") << u"**gruby** __też__ ~~skreślony~~ ==zaznaczony== `kod`"_s
                                  << u"gruby też skreślony zaznaczony kod"_s;
        QTest::newRow("single marks around words") << u"*kursywa* i _też_"_s << u"kursywa i też"_s;
        QTest::newRow("marks inside words stay") << u"snake_case_name i 2*3*4"_s << u"snake_case_name i 2*3*4"_s;
        QTest::newRow("tags and dates stay")
                << u"#praca ⏰ 2026-10-15 09:00 📅 jutro"_s << u"#praca ⏰ 2026-10-15 09:00 📅 jutro"_s;
        QTest::newRow("lines kept") << u"a **b**\nc"_s << u"a b\nc"_s;
        QTest::newRow("unbalanced brackets") << u"[[niezamknięty i [x]("_s << u"[[niezamknięty i [x]("_s;
        QTest::newRow("Polish") << u"**Zażółć** gęślą jaźń"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("code spans are their code")
                << u"Warunek `x == 0` i `__init__` oraz `a*b*c`"_s << u"Warunek x == 0 i __init__ oraz a*b*c"_s;
        QTest::newRow("links inside code stay") << u"`[[Strona]]` i [[Strona]]"_s << u"[[Strona]] i Strona"_s;
        QTest::newRow("double backticks") << u"``a ` b`` i `` `x` ``"_s << u"a ` b i `x`"_s;
        QTest::newRow("an unclosed backtick") << u"jeden ` dwa\n`trzy`"_s << u"jeden ` dwa\ntrzy"_s;
        QTest::newRow("unpaired marks stay") << u"2**3 i a == b oraz ~~ i __"_s << u"2**3 i a == b oraz ~~ i __"_s;
        QTest::newRow("emphasis around code") << u"**ważne `x`** i ==`y`=="_s << u"ważne x i y"_s;
        QTest::newRow("nested pairs") << u"**__żółć__** ~~==b==~~"_s << u"żółć b"_s;
        QTest::newRow("private-use characters") << u"\ue000 `kod`"_s << u"\ue000 kod"_s;
    }
    void inlineMarkup()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        QCOMPARE(stripInlineMarkup(text), expected);
    }

    void blockIdsForAPaste_data()
    {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QStringList>("drop");
        QTest::addColumn<QString>("expected");
        QTest::newRow("a caret id") << u"- a ^abc123\n- b ^def456\n"_s << QStringList{u"abc123"_s}
                                    << u"- a\n- b ^def456\n"_s;
        QTest::newRow("an id property") << u"- a\n  id:: 64f0-11\n- b\n"_s << QStringList{u"64f0-11"_s}
                                        << u"- a\n- b\n"_s;
        QTest::newRow("kept") << u"- a ^abc123\n"_s << QStringList{} << u"- a ^abc123\n"_s;
        QTest::newRow("a code block's id line, its code kept")
                << u"```\nkod ^xyz\n```\n^xyz\n"_s << QStringList{u"xyz"_s} << u"```\nkod ^xyz\n```\n"_s;
        QTest::newRow("a paragraph, Polish") << u"Zażółć gęślą jaźń ^pl-1\n\nDalej\n"_s << QStringList{u"pl-1"_s}
                                             << u"Zażółć gęślą jaźń\n\nDalej\n"_s;
        QTest::newRow("no ids at all") << u"- a\n- b"_s << QStringList{u"a"_s} << u"- a\n- b"_s;
        QTest::newRow("empty") << QString() << QStringList{u"x"_s} << QString();
    }
    void blockIdsForAPaste()
    {
        QFETCH(QString, markdown);
        QFETCH(QStringList, drop);
        QFETCH(QString, expected);
        QCOMPARE(withoutBlockIds(markdown, [&](const QString &id) { return drop.contains(id); }), expected);
    }

    void idsOfPastedBlocks()
    {
        QCOMPARE(blockIdsOf(u"- a ^abc123\n  - b\n    id:: 64f0\n- c ^abc123\n"_s),
                 QStringList({u"abc123"_s, u"64f0"_s}));
        QCOMPARE(blockIdsOf(QString()), QStringList());
    }

    void malformedInputNeverBreaks()
    {
        // Random fragments: the copy of any range never crashes and parses back.
        const QStringList pieces{u"- "_s,      u"  "_s,  u"\t"_s, u"[ ]"_s,   u"#"_s,      u">"_s,
                                 u"[!note]"_s, u"```"_s, u"|"_s,  u"---"_s,   u"%%"_s,     u"[["_s,
                                 u"]]"_s,      u"^id"_s, u"::"_s, u"\n"_s,    u"\n"_s,     u"\r\n"_s,
                                 u"zażółć"_s,  u"1. "_s, u"$$"_s, u"((x))"_s, u"\uFEFF"_s, u"\u00a0"_s};
        quint32 seed = 7;
        const auto rnd = [&](quint32 n) {
            seed = (seed * 1103515245u + 12345u) % 2147483648u;
            return n ? seed % n : 0;
        };
        for (int k = 0; k < 400; ++k) {
            QString s;
            for (quint32 i = 0, len = 1 + rnd(60); i < len; ++i)
                s += pieces[rnd(quint32(pieces.size()))];
            const BlockDocument doc(s);
            const int a = int(rnd(quint32(doc.count() + 1)));
            const int b = int(rnd(quint32(doc.count() + 1)));
            Rows rows;
            for (int r = std::min(a, b); r <= std::max(a, b); ++r)
                rows.append(r);
            const QString md = blocksMarkdown(doc, topRows(doc, rows));
            QVERIFY(!md.contains(u'\r'));
            QCOMPARE(BlockDocument(md).toString(), md);
            (void)blocksPlainText(doc, topRows(doc, rows));
            (void)withoutBlockIds(s, [](const QString &) { return true; });
        }
    }

    void largeSelectionIsLinear()
    {
        QString content;
        for (int i = 0; i < 5000; ++i)
            content += (i % 3 == 0 ? u"- punkt "_s : u"\t- podpunkt "_s) + QString::number(i) + u"\n"_s;
        const BlockDocument doc(content);
        Rows all;
        for (int i = 0; i < doc.count(); ++i)
            all.append(i);
        QElapsedTimer timer;
        timer.start();
        const QList<int> tops = topRows(doc, all);
        const QString md = blocksMarkdown(doc, tops);
        const QString plain = blocksPlainText(doc, tops);
        QCOMPARE(md + u'\n', content);
        QVERIFY(plain.startsWith(u"• punkt 0\n  • podpunkt 1"_s));
        QVERIFY2(timer.elapsed() < 5000, qPrintable(QString::number(timer.elapsed())));
    }
};

QTEST_GUILESS_MAIN(TestBlockClipboard)
#include "tst_markdown_blockclipboard.moc"
