// YAML frontmatter subset: location, parsing, serialization and single-key edits.
// Test cases ported from archive/web-v1 (frontmatter tests).
#include "markdown/frontmatter.h"

#include <QDate>
#include <QDateTime>
#include <QTest>

using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

QVariant ll(qlonglong n)
{
    return QVariant(n);
}

} // namespace

class TestFrontmatter : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void noFrontmatter()
    {
        for (const QString &s : {u"# Title\n"_s, QString(), u"\n---\na: 1\n---\n"_s, u"---\na: 1\nno closing"_s}) {
            const auto r = parseFrontmatter(s);
            QVERIFY(r.data.isEmpty());
            QCOMPARE(r.bodyStart, 0);
            QCOMPARE(r.bodyStartLine, 0);
        }
        QVERIFY(!findFrontmatter(u"---"));
    }

    void bodyStart()
    {
        const QString src = u"---\ntitle: Dom\n---\nBody"_s;
        const auto r = parseFrontmatter(src);
        QCOMPARE(r.data, QVariantMap({{u"title"_s, u"Dom"_s}}));
        QCOMPARE(r.bodyStart, src.indexOf(u"Body"));
        QCOMPARE(r.bodyStartLine, 3);
    }

    void crlfBomDotsTrailingSpaces()
    {
        const QString src = u"\uFEFF--- \r\na: 1\r\n...\r\nBody"_s;
        const auto r = parseFrontmatter(src);
        QCOMPARE(r.data, QVariantMap({{u"a"_s, ll(1)}}));
        QCOMPARE(src.sliced(r.bodyStart), u"Body"_s);
        QCOMPARE(r.bodyStartLine, 3);
    }

    void emptyAndAtEnd()
    {
        auto r = parseFrontmatter(u"---\n---\nx");
        QVERIFY(r.data.isEmpty());
        QCOMPARE(r.bodyStart, 8);
        QCOMPARE(r.bodyStartLine, 2);
        r = parseFrontmatter(u"---\na: b\n---");
        QCOMPARE(r.data, QVariantMap({{u"a"_s, u"b"_s}}));
        QCOMPARE(r.bodyStart, 12);
        const auto fm = findFrontmatter(u"---\na: b\n---");
        QVERIFY(fm);
        QCOMPARE(fm->endLine, 2);
        QCOMPARE(fm->yaml, u"a: b\n"_s);
    }

    void plainScalars()
    {
        const QVariantMap r = parseYaml(
                u"a: text\nb: 12\nc: -3.5\nd: true\ne: False\nf: null\ng: ~\nh:\ni: 2026-10-14\nj: 007\nk: 1e3");
        QCOMPARE(r.value(u"a"_s), QVariant(u"text"_s));
        QCOMPARE(r.value(u"b"_s), ll(12));
        QCOMPARE(r.value(u"c"_s).toDouble(), -3.5);
        QCOMPARE(r.value(u"d"_s), QVariant(true));
        QCOMPARE(r.value(u"e"_s), QVariant(false));
        QVERIFY(r.contains(u"f"_s) && !r.value(u"f"_s).isValid());
        QVERIFY(r.contains(u"g"_s) && !r.value(u"g"_s).isValid());
        QVERIFY(r.contains(u"h"_s) && !r.value(u"h"_s).isValid());
        QCOMPARE(r.value(u"i"_s), QVariant(u"2026-10-14"_s));
        QCOMPARE(r.value(u"j"_s), QVariant(u"007"_s));
        QCOMPARE(r.value(u"k"_s).typeId(), QMetaType::Double);
        QCOMPARE(r.value(u"k"_s).toDouble(), 1000.0);
        QCOMPARE(r.size(), 11);
    }

    void hugeIntegersAndTimes()
    {
        const QVariantMap r = parseYaml(u"id: 12345678901234567890\nt: 10:30\nx: 1e400");
        QCOMPARE(r.value(u"id"_s), QVariant(u"12345678901234567890"_s));
        QCOMPARE(r.value(u"t"_s), QVariant(u"10:30"_s));
        QCOMPARE(r.value(u"x"_s), QVariant(u"1e400"_s));
    }

    void quotingEscapesComments()
    {
        const QString y = u"a: \"zażółć \\\"gęślą\\\" \\n jaźń \\u0105 \\x41\"\n"
                          "b: 'it''s # not a comment'\n"
                          "c: value # comment\n"
                          "d: \"#tag\" # c\n"
                          "e: http://example.com/#frag\n"
                          "# full line comment\n"
                          "\"quoted key\": 1\n"
                          "'single key': 2"_s;
        const QVariantMap r = parseYaml(y);
        QCOMPARE(r.value(u"a"_s), QVariant(u"zażółć \"gęślą\" \n jaźń ą A"_s));
        QCOMPARE(r.value(u"b"_s), QVariant(u"it's # not a comment"_s));
        QCOMPARE(r.value(u"c"_s), QVariant(u"value"_s));
        QCOMPARE(r.value(u"d"_s), QVariant(u"#tag"_s));
        QCOMPARE(r.value(u"e"_s), QVariant(u"http://example.com/#frag"_s));
        QCOMPARE(r.value(u"quoted key"_s), ll(1));
        QCOMPARE(r.value(u"single key"_s), ll(2));
        QCOMPARE(r.size(), 7);
    }

    void multiLineScalars()
    {
        const QVariantMap r = parseYaml(u"a: one\n  two\n  three\nb: \"x\n  y\"");
        QCOMPARE(r.value(u"a"_s), QVariant(u"one two three"_s));
        QCOMPARE(r.value(u"b"_s), QVariant(u"x y"_s));
    }

    void flowCollections()
    {
        const QVariantMap r = parseYaml(u"tags: [a, \"b c\", 3, true]\nm: {x: 1, \"y z\": [p, q]}\nempty: []\nem: {}");
        QCOMPARE(r.value(u"tags"_s).toList(), QVariantList({u"a"_s, u"b c"_s, ll(3), true}));
        QCOMPARE(r.value(u"m"_s).toMap(), QVariantMap({{u"x"_s, ll(1)}, {u"y z"_s, QVariantList{u"p"_s, u"q"_s}}}));
        QCOMPARE(r.value(u"empty"_s).toList(), QVariantList());
        QCOMPARE(r.value(u"empty"_s).typeId(), QMetaType::QVariantList);
        QCOMPARE(r.value(u"em"_s).typeId(), QMetaType::QVariantMap);
        const QVariantMap multi = parseYaml(u"tags: [\n  a,\n  b\n]\nnext: 1");
        QCOMPARE(multi.value(u"tags"_s).toList(), QVariantList({u"a"_s, u"b"_s}));
        QCOMPARE(multi.value(u"next"_s), ll(1));
    }

    void unquotedWikiLinksAreNestedLists()
    {
        const QVariantMap r = parseYaml(u"related: [[Strona A]]");
        QCOMPARE(r.value(u"related"_s).toList(), QVariantList({QVariantList{u"Strona A"_s}}));
    }

    void blockSequences()
    {
        const QVariantMap r = parseYaml(u"aliases:\n  - Dom\n  - \"Mój dom\"\ntags:\n- a\n- b # c\n");
        QCOMPARE(r.value(u"aliases"_s).toList(), QVariantList({u"Dom"_s, u"Mój dom"_s}));
        QCOMPARE(r.value(u"tags"_s).toList(), QVariantList({u"a"_s, u"b"_s}));
    }

    void nestedMapsAndSequencesOfMaps()
    {
        const QVariantMap r = parseYaml(u"author:\n  name: Jan\n  links:\n    - a\n    - b\nitems:\n  - name: x\n    "
                                        u"qty: 2\n  - name: y\n    qty: 3\n  - plain\n");
        QCOMPARE(r.value(u"author"_s).toMap(),
                 QVariantMap({{u"name"_s, u"Jan"_s}, {u"links"_s, QVariantList{u"a"_s, u"b"_s}}}));
        QCOMPARE(r.value(u"items"_s).toList(),
                 QVariantList({QVariantMap{{u"name"_s, u"x"_s}, {u"qty"_s, ll(2)}},
                               QVariantMap{{u"name"_s, u"y"_s}, {u"qty"_s, ll(3)}}, u"plain"_s}));
    }

    void blockScalars()
    {
        const QVariantMap r = parseYaml(
                u"lit: |\n  line 1\n  line 2\n\nfold: >\n  a\n  b\n\n  c\nstrip: |-\n  x\nkeep: |+\n  y\n\nlast: 1");
        QCOMPARE(r.value(u"lit"_s), QVariant(u"line 1\nline 2\n"_s));
        QCOMPARE(r.value(u"fold"_s), QVariant(u"a b\nc\n"_s));
        QCOMPARE(r.value(u"strip"_s), QVariant(u"x"_s));
        QCOMPARE(r.value(u"keep"_s), QVariant(u"y\n\n"_s));
        QCOMPARE(r.value(u"last"_s), ll(1));
        const QVariantMap c = parseYaml(u"a: |\n  # not a comment\n  text\nb: 2");
        QCOMPARE(c.value(u"a"_s), QVariant(u"# not a comment\ntext\n"_s));
        QCOMPARE(c.value(u"b"_s), ll(2));
    }

    void keepChompingAtEnd()
    {
        QCOMPARE(parseYaml(u"k: |+\n  two\n\n").value(u"k"_s), QVariant(u"two\n\n"_s));
        QCOMPARE(parseYaml(u"k: |+\n  two\n").value(u"k"_s), QVariant(u"two\n"_s));
        QCOMPARE(parseYaml(u"k: |+\n  two\n\nz: 1").value(u"k"_s), QVariant(u"two\n\n"_s));
        for (const QString &v : {u"two\n\n"_s, u"a\nb\n\n\n"_s, u"zażółć\n"_s}) {
            const QVariantMap data{{u"k"_s, v}, {u"z"_s, ll(1)}};
            QCOMPARE(parseFrontmatter(stringifyFrontmatter(data)).data, data);
            const QVariantMap single{{u"k"_s, v}};
            QCOMPARE(parseFrontmatter(stringifyFrontmatter(single)).data, single);
        }
    }

    void missingNestedValues()
    {
        const QVariantMap r = parseYaml(u"a:\nb: 1");
        QVERIFY(r.contains(u"a"_s) && !r.value(u"a"_s).isValid());
        QCOMPARE(r.value(u"b"_s), ll(1));
    }

    void malformedNeverFails()
    {
        QVariantMap r = parseYaml(u"a: 1\n???\n  stray: deeper\nb: [unclosed\nc: 3");
        QCOMPARE(r.value(u"a"_s), ll(1));
        QCOMPARE(r.value(u"c"_s), ll(3));
        QVERIFY(parseYaml(u"- top level list\n- x").isEmpty());
        QVERIFY(parseYaml(u"just text").isEmpty());
        r = parseYaml(u"a: \"unterminated\nb: 2");
        QCOMPARE(r.value(u"a"_s).typeId(), QMetaType::QString);
        QCOMPARE(parseYaml(u"a: 1\na: 2").value(u"a"_s), ll(2));
    }

    void deepAndRandomInput()
    {
        QString deep;
        for (int i = 0; i < 200; ++i)
            deep += QString(i * 2, u' ') + u"k"_s + QString::number(i) + u":\n"_s;
        parseYaml(deep);
        parseYaml(QString(u"a: "_s + QString(5000, u'[')));
        const QString chars = u"ab: -[]{}\"'|>#\n\t ,!&*"_s;
        quint32 seed = 7;
        for (int n = 0; n < 200; ++n) {
            QString s;
            for (int i = 0; i < 80; ++i) {
                seed = (seed * 1103515245u + 12345u) % 2147483648u;
                s += chars[seed % chars.size()];
            }
            parseYaml(s);
        }
        QVERIFY(true);
    }

    void keyOrder()
    {
        QStringList keys;
        parseYaml(u"zeta: 1\nalpha: 2\nmid: 3\nalpha: 4", &keys);
        QCOMPARE(keys, QStringList({u"zeta"_s, u"alpha"_s, u"mid"_s}));
        const auto r = parseFrontmatter(u"---\nb: 1\na: 2\n---\n");
        QCOMPARE(r.keys, QStringList({u"b"_s, u"a"_s}));
        QCOMPARE(stringifyYaml(r.data, r.keys), u"b: 1\na: 2\n"_s);
        QCOMPARE(stringifyYaml(r.data), u"a: 2\nb: 1\n"_s);
    }

    void stringifyEmpty() { QCOMPARE(stringifyFrontmatter({}), QString()); }

    void stringifyObsidianStyle()
    {
        const QVariantMap data{{u"title"_s, u"Mój dom"_s},
                               {u"tags"_s, QVariantList{u"dom"_s, u"projekt"_s}},
                               {u"aliases"_s, QVariantList()},
                               {u"count"_s, 3},
                               {u"done"_s, false},
                               {u"empty"_s, QVariant()},
                               {u"date"_s, u"2026-10-14"_s},
                               {u"link"_s, u"[[Strona]]"_s},
                               {u"colon"_s, u"a: b"_s},
                               {u"num"_s, u"42"_s},
                               {u"hash"_s, u"#tag"_s}};
        const QStringList order{u"title"_s, u"tags"_s, u"aliases"_s, u"count"_s, u"done"_s, u"empty"_s,
                                u"date"_s,  u"link"_s, u"colon"_s,   u"num"_s,   u"hash"_s};
        QCOMPARE(stringifyFrontmatter(data, order), u"---\n"
                                                    "title: Mój dom\n"
                                                    "tags:\n"
                                                    "  - dom\n"
                                                    "  - projekt\n"
                                                    "aliases: []\n"
                                                    "count: 3\n"
                                                    "done: false\n"
                                                    "empty:\n"
                                                    "date: 2026-10-14\n"
                                                    "link: \"[[Strona]]\"\n"
                                                    "colon: \"a: b\"\n"
                                                    "num: \"42\"\n"
                                                    "hash: \"#tag\"\n"
                                                    "---\n"_s);
    }

    void roundTrip()
    {
        const QVariantMap data{
                {u"title"_s, u"Zażółć gęślą jaźń"_s},
                {u"key with space"_s, u"v"_s},
                {u"tags"_s, QVariantList{u"a"_s, u"b c"_s, u"#x"_s, u"1"_s}},
                {u"nested"_s, QVariantMap{{u"a"_s, ll(1)},
                                          {u"b"_s, QVariantList{u"x"_s, u"y"_s}},
                                          {u"deeper"_s, QVariantMap{{u"z"_s, u"ż"_s}}}}},
                {u"items"_s,
                 QVariantList{QVariantMap{{u"name"_s, u"x"_s}, {u"qty"_s, ll(2)}}, QVariantMap{{u"name"_s, u"y"_s}},
                              QVariantList{u"p"_s, u"q"_s}, ll(5), QVariant()}},
                {u"multi"_s, u"line 1\nline 2\n"_s},
                {u"strip"_s, u"a\nb"_s},
                {u"keep"_s, u"k\n\n"_s},
                {u"lead"_s, u"  spaced\nx"_s},
                {u"quote"_s, u"say \"hi\""_s},
                {u"apostrophe"_s, u"it's"_s},
                {u"empty"_s, u""_s},
                {u"bool"_s, true},
                {u"float"_s, 1.5},
                {u"neg"_s, ll(-2)},
                {u"control"_s, u"tab\there"_s},
        };
        const QString out = stringifyFrontmatter(data);
        const auto parsed = parseFrontmatter(QString(out + u"body"_s));
        QCOMPARE(parsed.data, data);
        QCOMPARE(parsed.bodyStart, out.size());
    }

    void datesAsLocalDates()
    {
        const QVariantMap data{{u"d"_s, QDate(2026, 10, 14)}, {u"t"_s, QDateTime(QDate(2026, 10, 14), QTime(9, 5))}};
        QCOMPARE(stringifyYaml(data), u"d: 2026-10-14\nt: 2026-10-14 09:05\n"_s);
    }

    void quotesForeignScalars()
    {
        const QVariantMap data{{u"a"_s, u"yes"_s},   {u"b"_s, u"No"_s},   {u"c"_s, u"on"_s},    {u"d"_s, u"OFF"_s},
                               {u"e"_s, u"y"_s},     {u"f"_s, u"007"_s},  {u"g"_s, u"0x1F"_s},  {u"h"_s, u"0o17"_s},
                               {u"i"_s, u"1_000"_s}, {u"j"_s, u".inf"_s}, {u"k"_s, u"-.Inf"_s}, {u"l"_s, u".NaN"_s},
                               {u"m"_s, u"+12"_s},   {u"o"_s, u"1:20"_s}};
        const QString yaml = stringifyYaml(data);
        for (const QVariant &v : data)
            QVERIFY2(yaml.contains(u'"' + v.toString() + u'"'), qPrintable(v.toString()));
        QCOMPARE(parseYaml(yaml), data);
    }

    void keepsOrdinaryStringsPlain()
    {
        const QVariantMap data{{u"a"_s, u"yesterday"_s},
                               {u"d"_s, u"2026-10-14"_s},
                               {u"t"_s, u"Notatka 1"_s},
                               {u"x"_s, u"none"_s},
                               {u"ż"_s, u"ółw"_s}};
        QCOMPARE(stringifyYaml(data), u"a: yesterday\nd: 2026-10-14\nt: Notatka 1\nx: none\nż: ółw\n"_s);
        QCOMPARE(stringifyYaml({{u"n"_s, 1}}), u"\"n\": 1\n"_s);
        QCOMPARE(parseYaml(stringifyYaml({{u"n"_s, ll(1)}})), QVariantMap({{u"n"_s, ll(1)}}));
    }

    void setValueInPlace()
    {
        const QString src =
                u"---\r\ntitle: Stary\r\ntags:\r\n  - a\r\n  - b\r\n# komentarz\r\nz: 1\r\n---\r\nTreść\r\n"_s;
        QCOMPARE(setFrontmatterValue(src, u"title"_s, u"Nowy tytuł"_s),
                 u"---\r\ntitle: Nowy tytuł\r\ntags:\r\n  - a\r\n  - b\r\n# komentarz\r\nz: 1\r\n---\r\nTreść\r\n"_s);
        QCOMPARE(setFrontmatterValue(src, u"tags"_s, QVariantList{u"ż"_s}),
                 u"---\r\ntitle: Stary\r\ntags:\r\n  - ż\r\n# komentarz\r\nz: 1\r\n---\r\nTreść\r\n"_s);
        QCOMPARE(
                setFrontmatterValue(src, u"nowy"_s, true),
                u"---\r\ntitle: Stary\r\ntags:\r\n  - a\r\n  - b\r\n# komentarz\r\nz: 1\r\nnowy: true\r\n---\r\nTreść\r\n"_s);
        QCOMPARE(removeFrontmatterValue(src, u"tags"_s),
                 u"---\r\ntitle: Stary\r\n# komentarz\r\nz: 1\r\n---\r\nTreść\r\n"_s);
        QCOMPARE(removeFrontmatterValue(src, u"brak"_s), src);
        // Sequence items at the key's indentation belong to the key.
        QCOMPARE(setFrontmatterValue(u"---\ntags:\n- a\n- b\nx: 1\n---\n"_s, u"tags"_s, u"c"_s),
                 u"---\ntags: c\nx: 1\n---\n"_s);
    }

    void setValueCreatesFrontmatter()
    {
        QCOMPARE(setFrontmatterValue(u"# Tytuł\n"_s, u"alias"_s, u"Dom"_s), u"---\nalias: Dom\n---\n# Tytuł\n"_s);
        QCOMPARE(setFrontmatterValue(QString(), u"a"_s, ll(1)), u"---\na: 1\n---\n"_s);
        QCOMPARE(setFrontmatterValue(u"\uFEFFx"_s, u"a"_s, QVariant()), u"\uFEFF---\na:\n---\nx"_s);
        QCOMPARE(removeFrontmatterValue(u"# x"_s, u"a"_s), u"# x"_s);
    }
};

QTEST_GUILESS_MAIN(TestFrontmatter)
#include "tst_markdown_frontmatter.moc"
