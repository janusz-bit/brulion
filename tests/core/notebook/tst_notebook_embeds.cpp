// A block placed right after the block that embeds an attachment (notebook::insertBlockAfterEmbed):
// the PDF viewer's "Draw on this page" puts the new drawing under the PDF it was drawn on, in a
// note no editor has open. Outlines (nested, spaces, other markers, ordered lists), document-mode
// paragraphs, CRLF, a missing final newline, frontmatter, stale line hints, two embeds, fragments
// and percent-encoded destinations, and embeds that do not count (code, frontmatter, links,
// malformed ones).
#include "notebook/embeds.h"

#include <QTest>

#include <functional>

using namespace Qt::StringLiterals;
using brulion::notebook::EmbedInsertion;
using brulion::notebook::insertBlockAfterEmbed;

class TestNotebookEmbeds : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void insertBlockAfterEmbed_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("hint");
        QTest::addColumn<QString>("pdf"); // the file the predicate accepts (by name, or any folder/name)
        QTest::addColumn<QString>("body");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("line");
        QTest::addColumn<bool>("afterEmbed");

        const QString drawing = u"![[wykład s. 2.svg]]"_s;
        QTest::newRow("outline")
                << u"- Wykład ![[Fizyka – wykład 3.pdf]]\n- Dalej\n"_s << 0 << u"Fizyka – wykład 3.pdf"_s
                << u"![[Fizyka – wykład 3 s. 4.svg]]"_s
                << u"- Wykład ![[Fizyka – wykład 3.pdf]]\n- ![[Fizyka – wykład 3 s. 4.svg]]\n- Dalej\n"_s << 1 << true;
        QTest::newRow("nested with tabs")
                << u"- Przedmiot\n\t- Slajdy ![[wykład.pdf]]\n- Inny\n"_s << 1 << u"wykład.pdf"_s << drawing
                << u"- Przedmiot\n\t- Slajdy ![[wykład.pdf]]\n\t- ![[wykład s. 2.svg]]\n- Inny\n"_s << 2 << true;
        QTest::newRow("spaces and stars")
                << u"* Temat\n  * Slajdy ![[wykład.pdf]]\n* Inny\n"_s << 1 << u"wykład.pdf"_s << drawing
                << u"* Temat\n  * Slajdy ![[wykład.pdf]]\n  * ![[wykład s. 2.svg]]\n* Inny\n"_s << 2 << true;
        QTest::newRow("after the item's children")
                << u"- Slajdy ![[wykład.pdf]]\n\t- uwaga\n- Inny\n"_s << 0 << u"wykład.pdf"_s << drawing
                << u"- Slajdy ![[wykład.pdf]]\n\t- uwaga\n- ![[wykład s. 2.svg]]\n- Inny\n"_s << 2 << true;
        // The later items are numbered on, as the editor's Enter does.
        QTest::newRow("ordered list") << u"1. ![[a.pdf]]\n2. Dalej\n"_s << 0 << u"a.pdf"_s << drawing
                                      << u"1. ![[a.pdf]]\n2. ![[wykład s. 2.svg]]\n3. Dalej\n"_s << 1 << true;
        QTest::newRow("document paragraphs")
                << u"Slajdy:\n\n![[wykład.pdf]]\n\nKoniec\n"_s << 2 << u"wykład.pdf"_s << drawing
                << u"Slajdy:\n\n![[wykład.pdf]]\n\n![[wykład s. 2.svg]]\n\nKoniec\n"_s << 4 << true;
        QTest::newRow("CRLF") << u"- A ![[w.pdf]]\r\n- B\r\n"_s << 0 << u"w.pdf"_s << drawing
                              << u"- A ![[w.pdf]]\r\n- ![[wykład s. 2.svg]]\r\n- B\r\n"_s << 1 << true;
        QTest::newRow("no final newline") << u"- A ![[w.pdf]]"_s << 0 << u"w.pdf"_s << drawing
                                          << u"- A ![[w.pdf]]\n- ![[wykład s. 2.svg]]"_s << 1 << true;
        QTest::newRow("frontmatter") << u"---\ntytuł: Wykład\n---\n- ![[w.pdf]]\n"_s << 3 << u"w.pdf"_s << drawing
                                     << u"---\ntytuł: Wykład\n---\n- ![[w.pdf]]\n- ![[wykład s. 2.svg]]\n"_s << 4
                                     << true;
        QTest::newRow("stale hint") << u"- Nowy\n- Drugi\n- Slajdy ![[w.pdf]]\n"_s << 0 << u"w.pdf"_s << drawing
                                    << u"- Nowy\n- Drugi\n- Slajdy ![[w.pdf]]\n- ![[wykład s. 2.svg]]\n"_s << 3 << true;
        QTest::newRow("no hint") << u"- Nowy\n- Slajdy ![[w.pdf]]\n"_s << -1 << u"w.pdf"_s << drawing
                                 << u"- Nowy\n- Slajdy ![[w.pdf]]\n- ![[wykład s. 2.svg]]\n"_s << 2 << true;
        const QString two = u"- ![[w.pdf]] pierwszy\n- a\n- b\n- c\n- ![[w.pdf]] drugi\n- d\n"_s;
        QTest::newRow("two embeds, the nearer") << two << 3 << u"w.pdf"_s << drawing
                                                << u"- ![[w.pdf]] pierwszy\n- a\n- b\n- c\n- ![[w.pdf]] drugi\n"
                                                   u"- ![[wykład s. 2.svg]]\n- d\n"_s
                                                << 5 << true;
        QTest::newRow("two embeds, a tie: the earlier")
                << two << 2 << u"w.pdf"_s << drawing
                << u"- ![[w.pdf]] pierwszy\n- ![[wykład s. 2.svg]]\n- a\n- b\n- c\n- ![[w.pdf]] drugi\n- d\n"_s << 1
                << true;
        QTest::newRow("page fragment") << u"- ![[wykład.pdf#page=3]]\n"_s << 0 << u"wykład.pdf"_s << drawing
                                       << u"- ![[wykład.pdf#page=3]]\n- ![[wykład s. 2.svg]]\n"_s << 1 << true;
        QTest::newRow("percent-encoded Markdown picture")
                << u"- ![slajd](assets/wyk%C5%82ad.pdf)\n"_s << 0 << u"wykład.pdf"_s << drawing
                << u"- ![slajd](assets/wyk%C5%82ad.pdf)\n- ![[wykład s. 2.svg]]\n"_s << 1 << true;
        QTest::newRow("Polish name, a body with | and #")
                << u"- Źródła ![[Ćwiczenia – źródła ąę.pdf]]\n"_s << 0 << u"Ćwiczenia – źródła ąę.pdf"_s
                << u"![[Ćwiczenia – źródła ąę s. 2.svg|szkic #1]]"_s
                << u"- Źródła ![[Ćwiczenia – źródła ąę.pdf]]\n- ![[Ćwiczenia – źródła ąę s. 2.svg|szkic #1]]\n"_s << 1
                << true;
        QTest::newRow("in a quote") << u"> Slajdy ![[w.pdf]]\n\nKoniec\n"_s << 0 << u"w.pdf"_s << drawing
                                    << u"> Slajdy ![[w.pdf]]\n\n![[wykład s. 2.svg]]\n\nKoniec\n"_s << 2 << true;

        // Appended: no block embeds it (any more).
        QTest::newRow("empty note") << QString() << 0 << u"w.pdf"_s << drawing << u"- ![[wykład s. 2.svg]]\n"_s << 0
                                    << false;
        QTest::newRow("only a heading") << u"# Wykład\n"_s << 0 << u"w.pdf"_s << drawing
                                        << u"# Wykład\n![[wykład s. 2.svg]]\n"_s << 1 << false;
        QTest::newRow("in a code block") << u"```\n![[w.pdf]]\n```\n- Inny\n"_s << 1 << u"w.pdf"_s << drawing
                                         << u"```\n![[w.pdf]]\n```\n- Inny\n- ![[wykład s. 2.svg]]\n"_s << 4 << false;
        QTest::newRow("in inline code") << u"Tekst `![[w.pdf]]`\n"_s << 0 << u"w.pdf"_s << drawing
                                        << u"Tekst `![[w.pdf]]`\n\n![[wykład s. 2.svg]]\n"_s << 2 << false;
        QTest::newRow("in the frontmatter")
                << u"---\nobraz: \"![[w.pdf]]\"\n---\nTekst\n"_s << 1 << u"w.pdf"_s << drawing
                << u"---\nobraz: \"![[w.pdf]]\"\n---\nTekst\n\n![[wykład s. 2.svg]]\n"_s << 5 << false;
        QTest::newRow("a link, not an embed") << u"- [[w.pdf]]\n"_s << 0 << u"w.pdf"_s << drawing
                                              << u"- [[w.pdf]]\n- ![[wykład s. 2.svg]]\n"_s << 1 << false;
        QTest::newRow("unclosed embed") << u"- ![[wykład.pdf\n- B\n"_s << 0 << u"wykład.pdf"_s << drawing
                                        << u"- ![[wykład.pdf\n- B\n- ![[wykład s. 2.svg]]\n"_s << 2 << false;
        QTest::newRow("another PDF") << u"- ![[inny.pdf]]\n"_s << 0 << u"wykład.pdf"_s << drawing
                                     << u"- ![[inny.pdf]]\n- ![[wykład s. 2.svg]]\n"_s << 1 << false;
        QTest::newRow("hint far beyond the end")
                << u"Tekst\n"_s << 999 << u"w.pdf"_s << drawing << u"Tekst\n\n![[wykład s. 2.svg]]\n"_s << 2 << false;
    }

    void insertBlockAfterEmbed()
    {
        QFETCH(QString, content);
        QFETCH(int, hint);
        QFETCH(QString, pdf);
        QFETCH(QString, body);
        QFETCH(QString, expected);
        QFETCH(int, line);
        QFETCH(bool, afterEmbed);
        const QString inFolder = u'/' + pdf;
        const std::function<bool(QStringView)> embedsIt = [&pdf, &inFolder](QStringView target) {
            return target == pdf || target.endsWith(inFolder);
        };
        const EmbedInsertion out = brulion::notebook::insertBlockAfterEmbed(content, hint, embedsIt, body);
        QCOMPARE(out.content, expected);
        QCOMPARE(out.line, line);
        QCOMPARE(out.afterEmbed, afterEmbed);
        // The inserted block is on that line.
        const QStringList lines = out.content.split(u'\n');
        QVERIFY(out.line >= 0 && out.line < lines.size());
        QVERIFY2(lines.at(out.line).contains(body), qPrintable(lines.at(out.line)));
    }

    void insertBlockBeforeEmbed_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("hint");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("line");
        QTest::addColumn<bool>("afterEmbed");
        QTest::newRow("outline") << u"- Wstęp\n- ![[wykład s. 4.svg]]\n- Dalej\n"_s << 1
                                 << u"- Wstęp\n- ![[wykład s. 3.svg]]\n- ![[wykład s. 4.svg]]\n- Dalej\n"_s << 1
                                 << true;
        QTest::newRow("first block") << u"- ![[wykład s. 4.svg]]\n"_s << 0
                                     << u"- ![[wykład s. 3.svg]]\n- ![[wykład s. 4.svg]]\n"_s << 0 << true;
        QTest::newRow("with its children") << u"- Temat\n\t- ![[wykład s. 4.svg]]\n\t\t- uwaga ąę\n- Inny\n"_s << 1
                                           << u"- Temat\n\t- ![[wykład s. 3.svg]]\n\t- ![[wykład s. 4.svg]]\n"
                                              u"\t\t- uwaga ąę\n- Inny\n"_s
                                           << 1 << true;
        QTest::newRow("document paragraphs")
                << u"Slajdy:\n\n![[wykład s. 4.svg]]\n\nKoniec\n"_s << 2
                << u"Slajdy:\n\n![[wykład s. 3.svg]]\n\n![[wykład s. 4.svg]]\n\nKoniec\n"_s << 2 << true;
        QTest::newRow("CRLF") << u"- A\r\n- ![[wykład s. 4.svg]]\r\n"_s << 1
                              << u"- A\r\n- ![[wykład s. 3.svg]]\r\n- ![[wykład s. 4.svg]]\r\n"_s << 1 << true;
        QTest::newRow("frontmatter") << u"---\ntytuł: W\n---\n- ![[wykład s. 4.svg]]\n"_s << 3
                                     << u"---\ntytuł: W\n---\n- ![[wykład s. 3.svg]]\n- ![[wykład s. 4.svg]]\n"_s << 3
                                     << true;
        QTest::newRow("not embedded: appended") << u"- A\n"_s << 0 << u"- A\n- ![[wykład s. 3.svg]]\n"_s << 1 << false;
    }
    void insertBlockBeforeEmbed()
    {
        QFETCH(QString, content);
        QFETCH(int, hint);
        QFETCH(QString, expected);
        QFETCH(int, line);
        QFETCH(bool, afterEmbed);
        const auto embedsIt = [](QStringView target) { return target == u"wykład s. 4.svg"; };
        const EmbedInsertion out =
                brulion::notebook::insertBlockBeforeEmbed(content, hint, embedsIt, u"![[wykład s. 3.svg]]"_s);
        QCOMPARE(out.content, expected);
        QCOMPARE(out.line, line);
        QCOMPARE(out.afterEmbed, afterEmbed);
    }

    void noPredicate()
    {
        // Without a predicate nothing embeds the file: appended.
        const EmbedInsertion out = brulion::notebook::insertBlockAfterEmbed(u"- A\n"_s, 0, {}, u"![[b.svg]]"_s);
        QCOMPARE(out.content, u"- A\n- ![[b.svg]]\n"_s);
        QVERIFY(!out.afterEmbed);
    }
};

QTEST_GUILESS_MAIN(TestNotebookEmbeds)
#include "tst_notebook_embeds.moc"
