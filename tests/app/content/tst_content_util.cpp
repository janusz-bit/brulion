// Pure helpers of the content views (src/app/content/contentutil.h): turning a plain mention
// into a [[link]] (whole words, case- and diacritic-aware, never inside code / links / tags,
// line endings kept), search-term highlight positions, tag keys and page tags, sizes and the
// Polish name order.
#include "contentutil.h"

#include "markdown/parse.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::content;

namespace {

/// The characters of `text` at `positions`, joined (to check highlights readably).
QString marked(const QString &text, const QList<int> &positions)
{
    QString out;
    int last = -2;
    for (const int p : positions) {
        if (p != last + 1 && !out.isEmpty())
            out += u'|';
        out += text.at(p);
        last = p;
    }
    return out;
}

} // namespace

class TestContentUtil : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void linkMention_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QStringList>("names");
        QTest::addColumn<QString>("expected");

        const QStringList remont{ u"Remont mieszkania"_s };
        QTest::newRow("plain, lower case") << u"- Zadzwonić w sprawie remont mieszkania jutro\n"_s << 0 << remont
                                           << u"- Zadzwonić w sprawie [[remont mieszkania]] jutro\n"_s;
        QTest::newRow("only the given line") << u"remont mieszkania\n- remont mieszkania\n"_s << 1 << remont
                                             << u"remont mieszkania\n- [[remont mieszkania]]\n"_s;
        QTest::newRow("whitespace run inside the name") << u"- Remont  mieszkania trwa\n"_s << 0 << remont
                                                        << u"- [[Remont  mieszkania]] trwa\n"_s;
        QTest::newRow("skips links, code and tags")
                << u"- [[Remont mieszkania]] `remont mieszkania` #remont-mieszkania i remont mieszkania\n"_s << 0 << remont
                << u"- [[Remont mieszkania]] `remont mieszkania` #remont-mieszkania i [[remont mieszkania]]\n"_s;
        QTest::newRow("not inside a longer word") << u"- Żółwik i żółw\n"_s << 0 << QStringList{ u"Żółw"_s }
                                                  << u"- Żółwik i [[żółw]]\n"_s;
        QTest::newRow("Polish capitals") << u"Spotkanie ŁÓDŹ\n"_s << 0 << QStringList{ u"Łódź"_s }
                                         << u"Spotkanie [[ŁÓDŹ]]\n"_s;
        QTest::newRow("longest name first") << u"- u Dom Jana na kawie\n"_s << 0 << QStringList{ u"Dom"_s, u"Dom Jana"_s }
                                            << u"- u [[Dom Jana]] na kawie\n"_s;
        QTest::newRow("earliest mention wins") << u"- dom, a potem Dom Jana\n"_s << 0 << QStringList{ u"Dom Jana"_s, u"Dom"_s }
                                               << u"- [[dom]], a potem Dom Jana\n"_s;
        QTest::newRow("CRLF kept") << u"pierwsza\r\n- remont mieszkania\r\nostatnia"_s << 1 << remont
                                   << u"pierwsza\r\n- [[remont mieszkania]]\r\nostatnia"_s;
        QTest::newRow("URL is not a mention") << u"https://example.com/remont mieszkania, remont mieszkania\n"_s << 0 << remont
                                              << u"https://example.com/remont mieszkania, [[remont mieszkania]]\n"_s;
        QTest::newRow("markdown link text") << u"[remont mieszkania](https://x.pl) remont mieszkania\n"_s << 0 << remont
                                            << u"[remont mieszkania](https://x.pl) [[remont mieszkania]]\n"_s;
    }

    void linkMention()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QStringList, names);
        QFETCH(QString, expected);
        const MentionEdit edit = content::linkMention(content, line, names);
        QVERIFY(edit.ok());
        QCOMPARE(edit.content, expected);
        QCOMPARE(edit.content.left(edit.from), content.left(edit.from));
        QCOMPARE(edit.content.mid(edit.from + edit.replacement.size()), content.mid(edit.to));
    }

    void linkMentionNotFound()
    {
        const QString content = u"- [[Remont mieszkania]] i `remont mieszkania`\n- nic\n"_s;
        for (int line : { 0, 1, 2, -1, 7 }) {
            const MentionEdit edit = content::linkMention(content, line, { u"Remont mieszkania"_s });
            QVERIFY2(!edit.ok(), qPrintable(QString::number(line)));
            QCOMPARE(edit.content, content);
        }
        // Names shorter than two characters are never looked for.
        QVERIFY(!content::linkMention(u"a b c\n"_s, 0, { u"a"_s }).ok());
        QVERIFY(!content::linkMention(u"tekst\n"_s, 0, {}).ok());
    }

    void linkMentionCustomLink()
    {
        const MentionEdit edit = content::linkMention(u"- kupić farbę do dom\n"_s, 0, { u"Dom"_s }, [](const QString &m) {
            return u"[[Projekty/Dom|"_s + m + u"]]"_s;
        });
        QVERIFY(edit.ok());
        QCOMPARE(edit.mention, u"dom"_s);
        QCOMPARE(edit.content, u"- kupić farbę do [[Projekty/Dom|dom]]\n"_s);
    }

    void searchTerms()
    {
        QCOMPARE(content::searchTerms(u"  Żółw  \"Remont\" -stary tag:Praca remont "_s),
                 (QStringList{ u"remont"_s, u"praca"_s, u"zolw"_s }));
        QCOMPARE(content::searchTerms(u"   "_s), QStringList());
        QCOMPARE(content::searchTerms(u"- !!"_s), QStringList());
    }

    void matchPositions()
    {
        const QString text = u"Żółw i żółtko, Mażółw"_s;
        QCOMPARE(marked(text, content::matchPositions(text, { u"zol"_s })), u"Żół|żół"_s);
        QCOMPARE(marked(text, content::matchPositions(text, { u"zol"_s }, false)), u"Żół|żół|żół"_s);
        QCOMPARE(content::matchPositions(text, {}), QList<int>());
        QCOMPARE(content::matchPositions(QString(), { u"a"_s }), QList<int>());
        // Combining marks (NFD input) are highlighted with their letter.
        const QString nfd = u"Zażółć"_s; // "Zażółć" decomposed
        QCOMPARE(content::matchPositions(nfd, { u"zazolc"_s }).size(), nfd.size());
        QVERIFY(content::containsAll(u"Remont mieszkania — Łódź"_s, { u"lodz"_s, u"remont"_s }));
        QVERIFY(!content::containsAll(u"Remont"_s, { u"lodz"_s }));
    }

    void mentionPositions()
    {
        const QString excerpt = u"…sprawie remont  mieszkania i Remontu…"_s;
        QCOMPARE(marked(excerpt, content::mentionPositions(excerpt, { u"Remont mieszkania"_s })), u"remont  mieszkania"_s);
    }

    void tags()
    {
        QCOMPARE(normalizeTag(u" #Praca/Projekt "_s), u"praca/projekt"_s);
        QCOMPARE(normalizeTag(u"#[[Do przeczytania]]"_s), u"do przeczytania"_s);
        QCOMPARE(normalizeTag(u"ŻÓŁW"_s), u"żółw"_s);

        const ParsedNote note = markdown::parseNote(u"Strona.md"_s,
                                                    u"---\ntags: [Praca, \"#Dom\"]\naliases: [Strona A]\n---\n- blok #inny\n"_s);
        QCOMPARE(pageTags(note), (QStringList{ u"praca"_s, u"dom"_s }));
        const ParsedNote logseq = markdown::parseNote(u"L.md"_s, u"tags:: [[Książki]], #czytanie\n\n- treść\n"_s);
        QCOMPARE(pageTags(logseq), (QStringList{ u"książki"_s, u"czytanie"_s }));
        const ParsedNote none = markdown::parseNote(u"N.md"_s, u"- tylko #blokowy tag\n"_s);
        QCOMPARE(pageTags(none), QStringList());
    }

    void sizesAndNames()
    {
        QCOMPARE(humanSize(512, dates::UiLocale::Polish), u"512 B"_s);
        QCOMPARE(humanSize(1229, dates::UiLocale::Polish), u"1,2 kB"_s);
        QCOMPARE(humanSize(1229, dates::UiLocale::English), u"1.2 kB"_s);
        QCOMPARE(humanSize(3 * 1024 * 1024 + 400 * 1024, dates::UiLocale::Polish), u"3,4 MB"_s);
        QVERIFY(compareNames(u"lody"_s, u"łódź"_s) < 0);
        QVERIFY(compareNames(u"Łódź"_s, u"mak"_s) < 0);
        QVERIFY(compareNames(u"żaba"_s, u"Zebra"_s) > 0);
        QVERIFY(compareNames(u"Strona 2"_s, u"Strona 10"_s) < 0);
        QVERIFY(compareNames(u"a"_s, u"A"_s) != 0); // total order
        QCOMPARE(oneLine(u"  wiele\n  linii\ttekstu  "_s), u"wiele linii tekstu"_s);
        QCOMPARE(oneLine(u"abcdefghij"_s, 5), u"abcd…"_s);
    }
};

QTEST_GUILESS_MAIN(TestContentUtil)
#include "tst_content_util.moc"
