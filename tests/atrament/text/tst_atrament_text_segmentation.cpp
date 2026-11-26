// Atrament's text segmentation: grapheme clusters, word movement, line break opportunities.
#include "text/segmentation.h"

#include "textfixtures.h"

#include <QtTest>

using namespace brulion::atrament;
using namespace Qt::StringLiterals;
using textfixtures::u16;

namespace {

QList<int> boundariesForward(std::u16string_view text)
{
    QList<int> out{0};
    int b = 0;
    while (b < int(text.size())) {
        const int next = nextGraphemeBoundary(text, b);
        if (next <= b)
            return {-1}; // no progress: a bug
        out.push_back(next);
        b = next;
    }
    return out;
}

QList<int> boundariesBackward(std::u16string_view text)
{
    QList<int> out{int(text.size())};
    int b = int(text.size());
    while (b > 0) {
        const int previous = previousGraphemeBoundary(text, b);
        if (previous >= b)
            return {-1};
        out.push_front(previous);
        b = previous;
    }
    return out;
}

// The text with "|" at every allowed break and "‖" at every mandatory one (but the last).
QString markedBreaks(const QString &text)
{
    const std::u16string t = u16(text);
    const std::vector<BreakKind> breaks = lineBreaks(t);
    QString out;
    for (qsizetype i = 0; i <= text.size(); ++i) {
        if (i > 0 && i < text.size()) {
            if (breaks[std::size_t(i)] == BreakKind::Allowed)
                out += u'|';
            else if (breaks[std::size_t(i)] == BreakKind::Mandatory)
                out += u'‖';
        }
        if (i < text.size())
            out += text[i];
    }
    return out;
}

} // namespace

class TestAtramentTextSegmentation : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void readCodepoints();
    void graphemes_data();
    void graphemes();
    void graphemeInsideCluster();
    void graphemeOutOfRange();
    void nextWordEnds_data();
    void nextWordEnds();
    void previousWordStarts_data();
    void previousWordStarts();
    void wordsAt_data();
    void wordsAt();
    void wordsOnEmptyAndOutOfRange();
    void breaks_data();
    void breaks();
    void breaksInvariants_data();
    void breaksInvariants();
};

void TestAtramentTextSegmentation::readCodepoints()
{
    const std::u16string text = u"ą😀\xD800x\xDC00";
    int i = 0;
    QCOMPARE(readCodepoint(text, i), char32_t(0x105));
    QCOMPARE(i, 1);
    QCOMPARE(readCodepoint(text, i), char32_t(0x1F600));
    QCOMPARE(i, 3);
    QCOMPARE(readCodepoint(text, i), char32_t(0xD800)); // a lone high surrogate reads as itself
    QCOMPARE(i, 4);
    QCOMPARE(readCodepoint(text, i), char32_t('x'));
    QCOMPARE(readCodepoint(text, i), char32_t(0xDC00));
    QCOMPARE(i, 6);
    QCOMPARE(readCodepoint(text, i), char32_t(0)); // at the end: nothing, no move
    QCOMPARE(i, 6);
    int negative = -3;
    QCOMPARE(readCodepoint(text, negative), char32_t(0x105));
    QCOMPARE(negative, 1);
}

void TestAtramentTextSegmentation::graphemes_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QList<int>>("boundaries");
    QTest::newRow("plain Polish") << u"Zażółć"_s << QList<int>{0, 1, 2, 3, 4, 5, 6};
    QTest::newRow("é precomposed") << u"é"_s << QList<int>{0, 1};
    QTest::newRow("é decomposed") << u"e\u0301"_s << QList<int>{0, 2};
    QTest::newRow("ą precomposed") << u"ąx"_s << QList<int>{0, 1, 2};
    QTest::newRow("ą decomposed") << u"a\u0328x"_s << QList<int>{0, 2, 3};
    QTest::newRow("Ą with two marks") << u"A\u0328\u0301b"_s << QList<int>{0, 3, 4};
    QTest::newRow("CR LF") << u"a\r\nb"_s << QList<int>{0, 1, 3, 4};
    QTest::newRow("CR CR LF") << u"\r\r\n"_s << QList<int>{0, 1, 3};
    QTest::newRow("LF CR") << u"\n\r"_s << QList<int>{0, 1, 2};
    QTest::newRow("mark after LF") << u"\n\u0301"_s << QList<int>{0, 1, 2};
    QTest::newRow("skin tone") << u"👍🏽!"_s << QList<int>{0, 4, 5};
    QTest::newRow("ZWJ family") << u"👨\u200D👩\u200D👧x"_s << QList<int>{0, 8, 9};
    QTest::newRow("ZWJ after a letter") << u"a\u200Db"_s << QList<int>{0, 2, 3};
    QTest::newRow("ZWJ then a letter after emoji") << u"👨\u200Db"_s << QList<int>{0, 3, 4};
    QTest::newRow("flag") << u"🇵🇱"_s << QList<int>{0, 4};
    QTest::newRow("two flags") << u"🇵🇱🇩🇪"_s << QList<int>{0, 4, 8};
    QTest::newRow("three indicators") << u"🇵🇱🇩"_s << QList<int>{0, 4, 6};
    QTest::newRow("indicators after a letter") << u"a🇵🇱🇩🇪"_s << QList<int>{0, 1, 5, 9};
    QTest::newRow("keycap") << u"1\uFE0F\u20E3 2"_s << QList<int>{0, 3, 4, 5};
    QTest::newRow("heart VS16") << u"❤\uFE0F"_s << QList<int>{0, 2};
    QTest::newRow("tag sequence") << u"🏴\U000E0067\U000E0062\U000E0065\U000E006E\U000E0067\U000E007Fa"_s
                                  << QList<int>{0, 14, 15};
    QTest::newRow("Hangul syllables") << u"한글"_s << QList<int>{0, 1, 2};
    QTest::newRow("Hangul jamo") << u"\u1112\u1161\u11AB\u1100\u1173"_s << QList<int>{0, 3, 5};
    QTest::newRow("LVT + T") << u"한\u11ABa"_s << QList<int>{0, 2, 3};     // 한 is LVT: GB8
    QTest::newRow("LV syllable + T") << u"하\u11AB"_s << QList<int>{0, 2}; // 하 is LV
    QTest::newRow("Devanagari spacing mark") << u"कि"_s << QList<int>{0, 2};
    QTest::newRow("prepend") << u"\u0600١"_s << QList<int>{0, 2};
    QTest::newRow("lone high surrogate") << u"a\xD800"
                                            u"b"_s
                                         << QList<int>{0, 1, 2, 3};
    QTest::newRow("lone surrogates swapped") << u"\xDC00\xD83D"_s << QList<int>{0, 1, 2};
    QTest::newRow("high surrogate at the end") << u"x\xD83D"_s << QList<int>{0, 1, 2};
    QTest::newRow("replacement character") << u"a�b"_s << QList<int>{0, 1, 2, 3};
    QTest::newRow("empty") << QString() << QList<int>{0};
}

void TestAtramentTextSegmentation::graphemes()
{
    QFETCH(QString, text);
    QFETCH(QList<int>, boundaries);
    const std::u16string t = u16(text);
    QCOMPARE(boundariesForward(t), boundaries);
    QCOMPARE(boundariesBackward(t), boundaries);
    for (int i = 0; i <= int(t.size()); ++i) {
        QCOMPARE(isGraphemeBoundary(t, i), boundaries.contains(i));
        // Never between the halves of a surrogate pair.
        if (i > 0 && i < int(t.size()) && QChar::isHighSurrogate(t[std::size_t(i - 1)])
            && QChar::isLowSurrogate(t[std::size_t(i)]))
            QVERIFY(!isGraphemeBoundary(t, i));
    }
}

void TestAtramentTextSegmentation::graphemeInsideCluster()
{
    const std::u16string text = u"x👨\u200D👩\u200D👧ą\u0301y"; // x, the family (1..9), a + ogonek + acute (9..12), y
    for (int inside = 2; inside < 9; ++inside) {
        QCOMPARE(nextGraphemeBoundary(text, inside), 9);
        QCOMPARE(previousGraphemeBoundary(text, inside), 1);
    }
    QCOMPARE(nextGraphemeBoundary(text, 9), 11);
    QCOMPARE(nextGraphemeBoundary(text, 10), 11);
    QCOMPARE(previousGraphemeBoundary(text, 10), 9);
    QCOMPARE(previousGraphemeBoundary(text, 11), 9);
    QCOMPARE(nextGraphemeBoundary(text, 11), 12);
}

void TestAtramentTextSegmentation::graphemeOutOfRange()
{
    const std::u16string text = u"ab";
    QCOMPARE(nextGraphemeBoundary(text, -5), 1);
    QCOMPARE(nextGraphemeBoundary(text, 7), 2);
    QCOMPARE(previousGraphemeBoundary(text, -1), 0);
    QCOMPARE(previousGraphemeBoundary(text, 9), 1);
    QVERIFY(isGraphemeBoundary(text, 0));
    QVERIFY(isGraphemeBoundary(text, 2));
    QVERIFY(!isGraphemeBoundary(text, 3));
    QVERIFY(!isGraphemeBoundary(text, -1));
    QCOMPARE(nextGraphemeBoundary(u"", 0), 0);
    QCOMPARE(previousGraphemeBoundary(u"", 0), 0);
    // Many regional indicators: pairs counted from the start, wherever the search begins.
    std::u16string flags;
    for (int i = 0; i < 9; ++i)
        flags += u"\U0001F1F5";
    QCOMPARE(previousGraphemeBoundary(flags, int(flags.size())), 16);
    QCOMPARE(nextGraphemeBoundary(flags, 13), 16);
    QCOMPARE(previousGraphemeBoundary(flags, 13), 12);
}

void TestAtramentTextSegmentation::nextWordEnds_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QList<int>>("ends"); // nextWordEnd from 0, repeated
    QTest::newRow("Polish") << u"Zażółć gęślą jaźń"_s << QList<int>{6, 12, 17};
    QTest::newRow("apostrophe") << u"don't stop"_s << QList<int>{5, 10};
    QTest::newRow("typographic apostrophe") << u"don’t"_s << QList<int>{5};
    QTest::newRow("decimal point") << u"3.14 i 1,5"_s << QList<int>{4, 6, 10};
    QTest::newRow("thousands") << u"10.000 zł"_s << QList<int>{6, 9};
    QTest::newRow("comma between letters") << u"a,b"_s << QList<int>{1, 2, 3};
    QTest::newRow("punctuation") << u"Tak, nie!"_s << QList<int>{3, 4, 8, 9};
    QTest::newRow("dots") << u"e.g. ..."_s << QList<int>{3, 4, 6, 7, 8};
    QTest::newRow("spaces") << u"  a   b  "_s << QList<int>{3, 7, 9};
    QTest::newRow("letters and digits") << u"abc123 x2"_s << QList<int>{6, 9};
    QTest::newRow("underscore") << u"snake_case"_s << QList<int>{10};
    QTest::newRow("combining mark") << u"za\u0328b c"_s << QList<int>{4, 6};
    QTest::newRow("emoji") << u"ab👍cd"_s << QList<int>{2, 4, 6};
    QTest::newRow("ZWJ emoji") << u"👨\u200D👩\u200D👧 ok"_s << QList<int>{8, 11};
    QTest::newRow("ideographs") << u"日本語"_s << QList<int>{1, 2, 3};
    QTest::newRow("trailing apostrophe") << u"ludzie' x"_s << QList<int>{6, 7, 9};
    QTest::newRow("line break") << u"raz\ndwa"_s << QList<int>{3, 7};
    QTest::newRow("NBSP") << u"a\u00A0b"_s << QList<int>{1, 3};
}

void TestAtramentTextSegmentation::nextWordEnds()
{
    QFETCH(QString, text);
    QFETCH(QList<int>, ends);
    const std::u16string t = u16(text);
    QList<int> got;
    for (int o = 0; o < int(t.size());) {
        const int next = nextWordEnd(t, o);
        QVERIFY(next > o);
        got.push_back(next);
        o = next;
    }
    QCOMPARE(got, ends);
}

void TestAtramentTextSegmentation::previousWordStarts_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QList<int>>("starts"); // previousWordStart from the end, repeated
    QTest::newRow("Polish") << u"Zażółć gęślą jaźń"_s << QList<int>{13, 7, 0};
    QTest::newRow("apostrophe") << u"don't stop"_s << QList<int>{6, 0};
    QTest::newRow("decimal point") << u"3.14 i 1,5"_s << QList<int>{7, 5, 0};
    QTest::newRow("punctuation") << u"Tak, nie!"_s << QList<int>{8, 5, 3, 0};
    QTest::newRow("spaces") << u"  a   b  "_s << QList<int>{6, 2, 0};
    QTest::newRow("emoji") << u"ab👍cd"_s << QList<int>{4, 2, 0};
    QTest::newRow("ideographs") << u"日本語"_s << QList<int>{2, 1, 0};
}

void TestAtramentTextSegmentation::previousWordStarts()
{
    QFETCH(QString, text);
    QFETCH(QList<int>, starts);
    const std::u16string t = u16(text);
    QList<int> got;
    for (int o = int(t.size()); o > 0;) {
        const int previous = previousWordStart(t, o);
        QVERIFY(previous < o);
        got.push_back(previous);
        o = previous;
    }
    QCOMPARE(got, starts);
    // From inside a word: its start.
    if (text == u"Zażółć gęślą jaźń"_s) {
        QCOMPARE(previousWordStart(t, 10), 7);
        QCOMPARE(nextWordEnd(t, 9), 12);
    }
}

void TestAtramentTextSegmentation::wordsAt_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("offset");
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("end");
    const QString polish = u"Zażółć gęślą jaźń"_s;
    QTest::newRow("first word") << polish << 2 << 0 << 6;
    QTest::newRow("at a word's start") << polish << 7 << 7 << 12;
    QTest::newRow("just after a word") << polish << 6 << 0 << 6;
    QTest::newRow("at the end") << polish << 17 << 13 << 17;
    QTest::newRow("space run") << u"a   b"_s << 2 << 1 << 4;
    QTest::newRow("apostrophe") << u"don't stop"_s << 3 << 0 << 5;
    QTest::newRow("number") << u"x 1,5 y"_s << 3 << 2 << 5;
    QTest::newRow("punctuation alone") << u"a ! b"_s << 2 << 2 << 3;
    QTest::newRow("after punctuation") << u"Tak, nie"_s << 4 << 4 << 5;
    QTest::newRow("emoji") << u"ab👍cd"_s << 2 << 2 << 4;
    QTest::newRow("inside a surrogate pair") << u"ab👍cd"_s << 3 << 2 << 4;
    QTest::newRow("combining mark") << u"za\u0328b c"_s << 2 << 0 << 4;
}

void TestAtramentTextSegmentation::wordsAt()
{
    QFETCH(QString, text);
    QFETCH(int, offset);
    QFETCH(int, start);
    QFETCH(int, end);
    const WordRange r = wordAt(u16(text), offset);
    QCOMPARE(r.start, start);
    QCOMPARE(r.end, end);
}

void TestAtramentTextSegmentation::wordsOnEmptyAndOutOfRange()
{
    QCOMPARE(wordAt(u"", 0).start, 0);
    QCOMPARE(wordAt(u"", 3).end, 0);
    QCOMPARE(nextWordEnd(u"", 0), 0);
    QCOMPARE(previousWordStart(u"", 5), 0);
    QCOMPARE(nextWordEnd(u"abc def", 100), 7);
    QCOMPARE(nextWordEnd(u"abc def", -4), 3);
    QCOMPARE(previousWordStart(u"abc def", 100), 4);
    QCOMPARE(previousWordStart(u"   ", 3), 0);
    QCOMPARE(nextWordEnd(u"   ", 0), 3);
    const WordRange r = wordAt(u"abc", 99);
    QCOMPARE(r.start, 0);
    QCOMPARE(r.end, 3);
    // Lone surrogates are words of their own and are never split.
    QCOMPARE(nextWordEnd(u"\xD800\xD800", 0), 1);
}

void TestAtramentTextSegmentation::breaks_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("marked");
    QTest::newRow("spaces") << u"Zażółć gęślą jaźń"_s << u"Zażółć |gęślą |jaźń"_s;
    QTest::newRow("space run") << u"a   b"_s << u"a   |b"_s;
    QTest::newRow("leading spaces") << u"  a"_s << u"  |a"_s;
    QTest::newRow("tab") << u"a\tb"_s << u"a\t|b"_s;
    QTest::newRow("hyphen between letters") << u"biało-czerwony"_s << u"biało-|czerwony"_s;
    QTest::newRow("en dash between letters") << u"Kraków–Gdańsk"_s << u"Kraków–|Gdańsk"_s;
    QTest::newRow("hyphen between digits") << u"2-3"_s << u"2-3"_s;
    QTest::newRow("hyphen before a digit") << u"a-3"_s << u"a-3"_s;
    QTest::newRow("hyphen after a space") << u"a -b"_s << u"a |-b"_s;
    QTest::newRow("double hyphen") << u"a--b"_s << u"a--b"_s;
    QTest::newRow("dash between spaces") << u"tak – nie"_s << u"tak |– |nie"_s;
    QTest::newRow("em dash") << u"a—b"_s << u"a|—|b"_s;
    QTest::newRow("slash") << u"and/or"_s << u"and/|or"_s;
    QTest::newRow("fraction") << u"1/2"_s << u"1/2"_s;
    QTest::newRow("URL") << u"a.pl/x/y"_s << u"a.pl/|x/|y"_s;
    QTest::newRow("double slash") << u"a//b"_s << u"a//|b"_s;
    QTest::newRow("ideographs") << u"日本語"_s << u"日|本|語"_s;
    QTest::newRow("CJK closing punctuation") << u"日本。語"_s << u"日|本。|語"_s;
    QTest::newRow("CJK brackets") << u"（日本）語"_s << u"（日|本）|語"_s;
    QTest::newRow("small kana") << u"きゃく"_s << u"きゃ|く"_s;
    QTest::newRow("ideograph and Latin") << u"a日b"_s << u"a|日|b"_s;
    QTest::newRow("emoji between spaces") << u"x 😀 y"_s << u"x |😀 |y"_s;
    QTest::newRow("emoji between letters") << u"a😀b"_s << u"a|😀|b"_s;
    QTest::newRow("emoji with skin tone") << u"a👍🏽b"_s << u"a|👍🏽|b"_s;
    QTest::newRow("flags") << u"🇵🇱🇩🇪"_s << u"🇵🇱|🇩🇪"_s;
    QTest::newRow("closing punctuation") << u"word."_s << u"word."_s;
    QTest::newRow("closing after space") << u"Bonjour !"_s << u"Bonjour !"_s;
    QTest::newRow("brackets") << u"(a b) c"_s << u"(a |b) |c"_s;
    QTest::newRow("opening then space") << u"( a"_s << u"( a"_s;
    QTest::newRow("Polish quotes") << u"„Cytat” – tak"_s << u"„Cytat” |– |tak"_s;
    QTest::newRow("guillemets") << u"«a» b"_s << u"«a» |b"_s;
    QTest::newRow("ASCII quotes") << u"x \"a\"b"_s << u"x |\"a\"b"_s;
    QTest::newRow("numbers") << u"1,5 i 10.000"_s << u"1,5 |i |10.000"_s;
    QTest::newRow("no break after a comma") << u"a,b"_s << u"a,b"_s;
    QTest::newRow("question") << u"co? nie!"_s << u"co? |nie!"_s;
    QTest::newRow("NBSP") << u"10\u00A0zł"_s << u"10\u00A0zł"_s;
    QTest::newRow("NBSP after space") << u"a \u00A0b"_s << u"a |\u00A0b"_s;
    QTest::newRow("figure space") << u"1\u20070"_s << u"1\u20070"_s;
    QTest::newRow("narrow NBSP") << u"a\u202Fb"_s << u"a\u202Fb"_s;
    QTest::newRow("word joiner") << u"a\u2060b c"_s << u"a\u2060b |c"_s;
    QTest::newRow("zero width space") << u"a\u200Bb"_s << u"a\u200B|b"_s;
    QTest::newRow("soft hyphen") << u"kon\u00ADstan"_s << u"kon\u00AD|stan"_s;
    QTest::newRow("LF") << u"a\nb"_s << u"a\n‖b"_s;
    QTest::newRow("CR LF") << u"a\r\nb"_s << u"a\r\n‖b"_s;
    QTest::newRow("CR") << u"a\rb"_s << u"a\r‖b"_s;
    QTest::newRow("line separator") << u"a\u2028b"_s << u"a\u2028‖b"_s;
    QTest::newRow("paragraph separator") << u"a\u2029b"_s << u"a\u2029‖b"_s;
    QTest::newRow("space before LF") << u"a \nb"_s << u"a \n‖b"_s;
    QTest::newRow("double LF") << u"a\n\nb"_s << u"a\n‖\n‖b"_s;
    QTest::newRow("combining mark") << u"za\u0328 b"_s << u"za\u0328 |b"_s;
}

void TestAtramentTextSegmentation::breaks()
{
    QFETCH(QString, text);
    QFETCH(QString, marked);
    QCOMPARE(markedBreaks(text), marked);
}

void TestAtramentTextSegmentation::breaksInvariants_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("empty") << QString();
    QTest::newRow("one") << u"a"_s;
    QTest::newRow("Polish") << u"Zażółć gęślą jaźń, ĄĆĘŁŃÓŚŹŻ — 1,5 zł/kg"_s;
    QTest::newRow("emoji") << u"👨\u200D👩\u200D👧 🇵🇱 1\uFE0F\u20E3 👍🏽"_s;
    QTest::newRow("surrogates") << u"\xD800 \xDC00\xD83D"_s;
    QTest::newRow("trailing LF") << u"abc\n"_s;
}

void TestAtramentTextSegmentation::breaksInvariants()
{
    QFETCH(QString, text);
    const std::u16string t = u16(text);
    const std::vector<BreakKind> b = lineBreaks(t);
    QCOMPARE(b.size(), t.size() + 1);
    QCOMPARE(b.back(), BreakKind::Mandatory);
    if (!t.empty())
        QCOMPARE(b.front(), BreakKind::None);
    for (int i = 1; i < int(t.size()); ++i) {
        if (b[std::size_t(i)] != BreakKind::None)
            QVERIFY2(isGraphemeBoundary(t, i), qPrintable(QString::number(i)));
    }
}

QTEST_GUILESS_MAIN(TestAtramentTextSegmentation)
#include "tst_atrament_text_segmentation.moc"
