// Finding and replacing literal text (text/find.h): case and diacritics off and on, whole words next
// to Polish letters, decomposed letters, emoji around matches, ranges of the original text.
#include "text/find.h"
#include "text/text.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::text;

namespace {
using Ranges = QList<TextRange>;
// Decomposed letters are written with escapes: an editor or a formatter could compose them.
const QString DecomposedA = u"a\u0328"_s;
const QString DecomposedN = u"n\u0301"_s;
const QString Bom = u"\uFEFF"_s;
const QString Nbsp = u"\u00A0"_s;
} // namespace

Q_DECLARE_METATYPE(brulion::text::FindOptions)

class TestTextFind : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void find_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("query");
        QTest::addColumn<FindOptions>("options");
        QTest::addColumn<QStringList>("found");

        const FindOptions none;
        const FindOptions mc{.matchCase = true};
        const FindOptions md{.matchDiacritics = true};
        const FindOptions both{.matchCase = true, .matchDiacritics = true};
        const FindOptions word{.wholeWord = true};
        const FindOptions wordMd{.matchDiacritics = true, .wholeWord = true};

        QTest::newRow("no diacritics, no case") << u"ŻÓŁW i żółw"_s << u"zolw"_s << none << QStringList{u"ŻÓŁW"_s, u"żółw"_s};
        QTest::newRow("query with diacritics finds without") << u"zolw"_s << u"ŻÓŁW"_s << none << QStringList{u"zolw"_s};
        QTest::newRow("every Polish letter, lower") << u"ąćęłńóśźż"_s << u"acelnoszz"_s << none << QStringList{u"ąćęłńóśźż"_s};
        QTest::newRow("every Polish letter, upper") << u"ĄĆĘŁŃÓŚŹŻ"_s << u"acelnoszz"_s << none << QStringList{u"ĄĆĘŁŃÓŚŹŻ"_s};
        QTest::newRow("gesla") << u"Zażółć gęślą jaźń"_s << u"gesla"_s << none << QStringList{u"gęślą"_s};
        QTest::newRow("match case keeps folding diacritics") << u"Łódź łódź"_s << u"Lodz"_s << mc << QStringList{u"Łódź"_s};
        QTest::newRow("match case, none") << u"ŻÓŁW"_s << u"zolw"_s << mc << QStringList{};
        QTest::newRow("match diacritics") << u"zolw żółw ŻÓŁW"_s << u"żółw"_s << md << QStringList{u"żółw"_s, u"ŻÓŁW"_s};
        QTest::newRow("match diacritics: plain does not find ą") << u"ą a"_s << u"a"_s << md << QStringList{u"a"_s};
        QTest::newRow("match both") << u"Żółw żółw"_s << u"żółw"_s << both << QStringList{u"żółw"_s};
        QTest::newRow("decomposed in the text") << u"z" + DecomposedA + u"b"_s << u"zab"_s << none
                                                << QStringList{u"z" + DecomposedA + u"b"_s};
        QTest::newRow("decomposed query, precomposed text") << u"ą"_s << DecomposedA << md << QStringList{u"ą"_s};
        QTest::newRow("precomposed query, decomposed text") << DecomposedA + u"b"_s << u"ą"_s << md << QStringList{DecomposedA};
        QTest::newRow("marks belong to the letter (folding)") << DecomposedA + u"b"_s << u"a"_s << none << QStringList{DecomposedA};
        QTest::newRow("match diacritics never splits a letter from its mark")
                << u"x\u0328 x"_s << u"x"_s << md << QStringList{u"x"_s};
        QTest::newRow("overlapping candidates") << u"aaa"_s << u"aa"_s << none << QStringList{u"aa"_s};
        QTest::newRow("overlapping, four") << u"aaaa"_s << u"aa"_s << none << QStringList{u"aa"_s, u"aa"_s};
        QTest::newRow("whole word") << u"kot kotek akot kot."_s << u"kot"_s << word << QStringList{u"kot"_s, u"kot"_s};
        QTest::newRow("whole word next to Polish letters") << u"łan żart ąart art"_s << u"art"_s << word << QStringList{u"art"_s};
        QTest::newRow("whole word, a Polish letter continues it") << u"panś pan"_s << u"pan"_s << word << QStringList{u"pan"_s};
        QTest::newRow("whole word without diacritics") << u"pań pan"_s << u"pan"_s << word << QStringList{u"pań"_s, u"pan"_s};
        QTest::newRow("whole word, underscore") << u"a_b a"_s << u"a"_s << word << QStringList{u"a"_s};
        QTest::newRow("whole word, decomposed letter after") << u"pa" + DecomposedN + u" pan"_s << u"pan"_s << wordMd
                                                             << QStringList{u"pan"_s};
        QTest::newRow("whole word rejected, then found") << u"aba ab"_s << u"ab"_s << word << QStringList{u"ab"_s};
        QTest::newRow("emoji around") << u"😀żółw😀"_s << u"zolw"_s << none << QStringList{u"żółw"_s};
        QTest::newRow("emoji inside") << u"a😀b"_s << u"a😀b"_s << none << QStringList{u"a😀b"_s};
        QTest::newRow("whole word after an emoji") << u"😀kot"_s << u"kot"_s << word << QStringList{u"kot"_s};
        QTest::newRow("no-break space equals space") << u"w"_s + Nbsp + u"domu"_s << u"w domu"_s << none
                                                     << QStringList{u"w"_s + Nbsp + u"domu"_s};
        QTest::newRow("empty query") << u"abc"_s << u""_s << none << QStringList{};
        QTest::newRow("empty text") << u""_s << u"a"_s << none << QStringList{};
        QTest::newRow("only marks fold to nothing") << u"\u0105"_s << u"\u0328"_s << none << QStringList{};
        QTest::newRow("a line break is no query") << u"a\nb"_s << u"a\nb"_s << none << QStringList{};
        QTest::newRow("regex characters are literal") << u"a.b axb (c)"_s << u"a.b"_s << none << QStringList{u"a.b"_s};
        QTest::newRow("markup is text") << u"- [ ] **pilne**"_s << u"[ ] **"_s << none << QStringList{u"[ ] **"_s};
    }
    void find()
    {
        QFETCH(QString, text);
        QFETCH(QString, query);
        QFETCH(FindOptions, options);
        QFETCH(QStringList, found);
        QStringList got;
        qsizetype last = 0;
        for (const TextRange &r : findMatches(text, query, options)) {
            QVERIFY(r.start >= last);
            QVERIFY(r.length > 0);
            got.append(text.sliced(r.start, r.length));
            last = r.end();
        }
        QCOMPARE(got, found);
    }

    void rangesAreOfTheOriginal()
    {
        // Letters before the match that fold to fewer units.
        const QString text = DecomposedA + u" żółw"_s;
        QCOMPARE(findMatches(text, u"zolw"), (Ranges{{3, 4}}));
        // A surrogate pair is two units, one character.
        QCOMPARE(findMatches(u"😀ą😀", u"a"), (Ranges{{2, 1}}));
        // İ lower-cases to two units, both of one character.
        QCOMPARE(findMatches(u"İ", u"i"), (Ranges{{0, 1}}));
    }

    void maxHits()
    {
        QCOMPARE(findMatches(u"a a a", u"a", {}, 2).size(), 2);
        QCOMPARE(findMatches(u"a a a", u"a", {}, 0).size(), 0);
        QCOMPARE(findMatches(u"a a a", u"a", {}, -1).size(), 3);
    }

    void diacriticsMap()
    {
        const FoldedText f = foldDiacriticsWithMap(QString(u"Ż"_s + DecomposedA));
        QCOMPARE(f.text, u"Za"_s);
        QCOMPARE(f.toOriginal(1), 1);
        QCOMPARE(f.toOriginal(2), 3);
    }

    void replace_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("query");
        QTest::addColumn<QString>("by");
        QTest::addColumn<QString>("result");
        QTest::newRow("CRLF kept") << u"żółw\r\nŻÓŁW\r\n"_s << u"zolw"_s << u"kot"_s << u"kot\r\nkot\r\n"_s;
        QTest::newRow("lone CR kept") << u"a\ra\n"_s << u"a"_s << u"b"_s << u"b\rb\n"_s;
        QTest::newRow("BOM kept") << Bom + u"żółw\n"_s << u"zolw"_s << u"kot"_s << Bom + u"kot\n"_s;
        QTest::newRow("no final line break") << u"x a"_s << u"a"_s << u"ą"_s << u"x ą"_s;
        QTest::newRow("empty replacement") << u"a b a"_s << u"a"_s << u""_s << u" b "_s;
        QTest::newRow("longer replacement") << u"a"_s << u"a"_s << u"😀😀"_s << u"😀😀"_s;
        QTest::newRow("decomposed goes whole") << DecomposedA + u" b"_s << u"a"_s << u"x"_s << u"x b"_s;
        QTest::newRow("nothing found") << u"a\r\n"_s << u"z"_s << u"x"_s << u"a\r\n"_s;
    }
    void replace()
    {
        QFETCH(QString, text);
        QFETCH(QString, query);
        QFETCH(QString, by);
        QFETCH(QString, result);
        QCOMPARE(replaceMatches(text, findMatches(text, query), by), result);
    }
    void replaceSkipsBadRanges()
    {
        QCOMPARE(replaceMatches(u"abcd", {{2, 1}, {0, 1}, {9, 1}}, u"x"), u"abxd"_s);
        QCOMPARE(replaceMatches(u"abcd", {{0, 2}, {1, 2}}, u"x"), u"xcd"_s);
    }
};

QTEST_GUILESS_MAIN(TestTextFind)
#include "tst_text_find.moc"
