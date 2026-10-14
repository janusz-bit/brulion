#include "text/text.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

/// Straightforward reference FNV-1a 64 (byte loop written independently of the implementation).
QString referenceFnv(const QByteArray &bytes)
{
    quint64 h = 14695981039346656037ULL;
    for (qsizetype i = 0; i < bytes.size(); ++i) {
        h ^= static_cast<unsigned char>(bytes.at(i));
        h *= 1099511628211ULL;
    }
    return u"%1"_s.arg(h, 16, 16, QChar(u'0'));
}

} // namespace

class TestText : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- folding -----------------------------------------------------------------------
    void foldDiacritics_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s << u"Zazolc gesla jazn"_s;
        QTest::newRow("upper") << u"ŁÓDŹ ĄĘ ĆŃ ŚŻ"_s << u"LODZ AE CN SZ"_s;
        QTest::newRow("nfd input") << u"s\u0301rode\u0328"_s << u"srode"_s;
        QTest::newRow("ascii") << u"Hello, World 123"_s << u"Hello, World 123"_s;
        QTest::newRow("other latin") << u"Crème brûlée, Ærø"_s << u"Creme brulee, Ærø"_s;
        QTest::newRow("emoji kept") << u"2026-10-15 🔁 ⏰"_s << u"2026-10-15 🔁 ⏰"_s;
        QTest::newRow("empty") << QString() << QString();
    }
    void foldDiacritics()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(text::foldDiacritics(input), expected);
    }

    void foldKey_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("expected");
        QTest::newRow("polish") << u"Zażółć Gęślą Jaźń"_s << u"zazolc gesla jazn"_s;
        QTest::newRow("upper") << u"ŁÓDŹ Ąę Ćń Śż"_s << u"lodz ae cn sz"_s;
        QTest::newRow("weekday") << u"Środę"_s << u"srode"_s;
        QTest::newRow("nfd") << u"S\u0301RODA"_s << u"sroda"_s;
        QTest::newRow("dotted I") << u"İstanbul"_s << u"istanbul"_s;
        QTest::newRow("tokens kept") << u"2026-10-15 18:30 🔁"_s << u"2026-10-15 18:30 🔁"_s;
        QTest::newRow("variation selector dropped") << u"⏰️ 9:00"_s << u"⏰ 9:00"_s;
    }
    void foldKey()
    {
        QFETCH(QString, input);
        QFETCH(QString, expected);
        QCOMPARE(text::foldKey(input), expected);
        QCOMPARE(text::foldKeyWithMap(input).text, expected);
    }

    void foldKeyWithMap()
    {
        const QString input = u"Zadzwoń w śródę"_s;
        const auto f = text::foldKeyWithMap(input);
        QCOMPARE(f.text, u"zadzwon w srode"_s);
        QCOMPARE(f.map.size(), f.text.size() + 1);
        QCOMPARE(f.map.last(), input.size());
        const qsizetype start = f.text.indexOf(u"srode"_s);
        const qsizetype a = f.toOriginal(start);
        const qsizetype b = f.toOriginal(start + 5);
        QCOMPARE(input.sliced(a, b - a), u"śródę"_s);
    }
    void foldKeyWithMapDecomposed()
    {
        // Combining marks fold to nothing and stay attached to their base letter.
        const QString input = u"Termin: w s\u0301rode\u0328!"_s;
        const auto f = text::foldKeyWithMap(input);
        QCOMPARE(f.text, u"termin: w srode!"_s);
        const qsizetype start = f.text.indexOf(u"srode"_s);
        const qsizetype a = f.toOriginal(start);
        const qsizetype b = f.toOriginal(start + 5);
        QCOMPARE(input.sliced(a, b - a), u"s\u0301rode\u0328"_s);
    }
    void foldKeyWithMapAstral()
    {
        const QString input = u"🔁 Jutro"_s;
        const auto f = text::foldKeyWithMap(input);
        QCOMPARE(f.text, u"🔁 jutro"_s);
        const qsizetype start = f.text.indexOf(u"jutro"_s);
        QCOMPARE(input.sliced(f.toOriginal(start), 5), u"Jutro"_s);
        QCOMPARE(f.toOriginal(-5), 0);
        QCOMPARE(f.toOriginal(1000), input.size());
    }
    void foldLoneSurrogate()
    {
        const QString input = QString(QChar(0xD800)) + u" Jutro"_s;
        const auto f = text::foldKeyWithMap(input);
        QCOMPARE(f.text, u"� jutro"_s);
        QCOMPARE(text::foldKey(input), u"� jutro"_s);
        // The sanitised text is matchable again (Qt refuses subjects with invalid UTF-16).
        QVERIFY(QRegularExpression(u"jutro"_s).match(f.text).hasMatch());
    }

    // ---- words -------------------------------------------------------------------------
    void isWordChar_data()
    {
        QTest::addColumn<QString>("ch");
        QTest::addColumn<bool>("expected");
        for (const QString &c : {u"a"_s, u"Z"_s, u"ą"_s, u"ł"_s, u"Ż"_s, u"ś"_s, u"7"_s, u"_"_s, u"\u0301"_s, u"ж"_s})
            QTest::newRow(qPrintable(u"word "_s + c)) << c << true;
        for (const QString &c : {u" "_s, u"-"_s, u"."_s, u"#"_s, u"\u00A0"_s, u"("_s, u"–"_s})
            QTest::newRow(qPrintable(u"non-word U+"_s + QString::number(c.at(0).unicode(), 16))) << c << false;
    }
    void isWordChar()
    {
        QFETCH(QString, ch);
        QFETCH(bool, expected);
        QCOMPARE(text::isWordChar(ch.at(0)), expected);
        QCOMPARE(text::isWordChar(char32_t(ch.at(0).unicode())), expected);
    }
    void isWordCharAstral()
    {
        QVERIFY(text::isWordChar(char32_t(0x1D400))); // MATHEMATICAL BOLD CAPITAL A
        QVERIFY(!text::isWordChar(char32_t(0x1F501))); // 🔁
        QVERIFY(!text::isWordChar(QChar(0xD83D)));     // a surrogate half
    }

    void isWholeWord()
    {
        const QString s = u"Żółw żółwia, (żółw)"_s;
        QVERIFY(text::isWholeWord(s, 0, 4));
        QVERIFY(!text::isWholeWord(s, 5, 9)); // "żółw" inside "żółwia"
        QVERIFY(text::isWholeWord(s, 14, 18));
        QVERIFY(!text::isWholeWord(s, 1, 4));
        QVERIFY(!text::isWholeWord(s, -1, 4));
        QVERIFY(!text::isWholeWord(s, 3, 100));
        // Surrogate pairs around a word are not word characters.
        const QString emoji = u"🔁jutro🔁"_s;
        QVERIFY(text::isWholeWord(emoji, 2, 7));
    }

    void indexOfWholeWord_data()
    {
        QTest::addColumn<QString>("haystack");
        QTest::addColumn<QString>("needle");
        QTest::addColumn<int>("cs");
        QTest::addColumn<qsizetype>("expected");
        QTest::newRow("skip inside word") << u"żółwia żółw"_s << u"żółw"_s << int(Qt::CaseSensitive) << qsizetype(7);
        QTest::newRow("polish suffix") << u"Łódź Łódzki"_s << u"Łódź"_s << int(Qt::CaseSensitive) << qsizetype(0);
        QTest::newRow("only inside") << u"Łódzki"_s << u"Łódź"_s << int(Qt::CaseSensitive) << qsizetype(-1);
        QTest::newRow("case insensitive") << u"Idę do ŁODZI i łodzi"_s << u"łodzi"_s << int(Qt::CaseInsensitive)
                                          << qsizetype(7);
        QTest::newRow("underscore is a word char") << u"a_b b"_s << u"b"_s << int(Qt::CaseSensitive) << qsizetype(4);
        QTest::newRow("empty needle") << u"abc"_s << QString() << int(Qt::CaseSensitive) << qsizetype(-1);
    }
    void indexOfWholeWord()
    {
        QFETCH(QString, haystack);
        QFETCH(QString, needle);
        QFETCH(int, cs);
        QFETCH(qsizetype, expected);
        QCOMPARE(text::indexOfWholeWord(haystack, needle, 0, Qt::CaseSensitivity(cs)), expected);
    }

    void regexWordBoundaries()
    {
        // Documented PCRE2 behaviour: without UCP `\b` and `\s` are ASCII-only.
        const QString s = u"Mój żółw idzie"_s;
        QVERIFY(!QRegularExpression(uR"(\bżółw\b)"_s).match(s).hasMatch());
        QVERIFY(QRegularExpression(uR"(\bżółw\b)"_s, QRegularExpression::UseUnicodePropertiesOption).match(s).hasMatch());
        QVERIFY(!QRegularExpression(uR"(o\s9)"_s).match(u"o\u00A09"_s).hasMatch());
        QVERIFY(text::unicodeRegex(uR"(o\s9)"_s).match(u"o\u00A09"_s).hasMatch());
        QVERIFY(QRegularExpression(u"^abc$"_s).match(u"abc\n"_s).hasMatch()); // hence `\z`
        QVERIFY(!QRegularExpression(uR"(^abc\z)"_s).match(u"abc\n"_s).hasMatch());

        // Our assertions work with and without UCP.
        const QString pattern = text::wholeWordPattern(u"żółw");
        for (const auto opts : {QRegularExpression::NoPatternOption, QRegularExpression::UseUnicodePropertiesOption}) {
            const QRegularExpression re(pattern, opts);
            QVERIFY2(re.isValid(), qPrintable(re.errorString()));
            const auto m = re.match(s);
            QVERIFY(m.hasMatch());
            QCOMPARE(m.capturedStart(), 4);
            QVERIFY(!re.match(u"żółwie"_s).hasMatch());
            QVERIFY(!re.match(u"małyżółw"_s).hasMatch());
            QVERIFY(!re.match(u"s\u0301żółw"_s).hasMatch()); // a combining mark is part of the word
            QVERIFY(re.match(u"(żółw)"_s).hasMatch());
        }
        // Special characters are escaped.
        QVERIFY(QRegularExpression(text::wholeWordPattern(u"a.b")).match(u"x a.b y"_s).hasMatch());
        QVERIFY(!QRegularExpression(text::wholeWordPattern(u"a.b")).match(u"x axb y"_s).hasMatch());
    }

    void regexAlternation()
    {
        QCOMPARE(text::regexAlternation({u"a"_s, u"abc"_s, u"ab"_s, u"a.b"_s}), uR"(a\.b|abc|ab|a)"_s);
        QCOMPARE(text::regexAlternation({u"b"_s, u"a"_s, u"b"_s, QString()}), u"a|b"_s);
        QCOMPARE(text::regexAlternation({}), QString());
        const QRegularExpression re(u"^(?:"_s + text::regexAlternation({u"pt"_s, u"ptk"_s, u"środa"_s}) + u")"_s);
        QCOMPARE(re.match(u"ptk"_s).captured(0), u"ptk"_s);
        QCOMPARE(re.match(u"środa"_s).captured(0), u"środa"_s);
    }

    // ---- lines -------------------------------------------------------------------------
    void splitLines()
    {
        const auto lines = text::splitLines(u"a\nb\r\nc\rd"_s);
        QCOMPARE(lines.size(), 4);
        const QList<text::Line> expected = {
            {u"a"_s, u"\n"_s, 0}, {u"b"_s, u"\r\n"_s, 2}, {u"c"_s, u"\r"_s, 5}, {u"d"_s, QString(), 7}};
        QCOMPARE(lines, expected);
    }
    void splitLinesEdges_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QStringList>("texts");
        QTest::newRow("empty") << QString() << QStringList{QString()};
        QTest::newRow("trailing newline") << u"x\n"_s << QStringList{u"x"_s, QString()};
        QTest::newRow("only crlf") << u"\r\n\r\n"_s << QStringList{QString(), QString(), QString()};
        QTest::newRow("cr cr lf") << u"a\r\r\nb"_s << QStringList{u"a"_s, QString(), u"b"_s};
        QTest::newRow("polish") << u"zażółć\r\ngęślą\njaźń"_s << QStringList{u"zażółć"_s, u"gęślą"_s, u"jaźń"_s};
    }
    void splitLinesEdges()
    {
        QFETCH(QString, content);
        QFETCH(QStringList, texts);
        const auto lines = text::splitLines(content);
        QStringList got;
        for (const auto &l : lines)
            got.append(l.text);
        QCOMPARE(got, texts);
        QVERIFY(lines.last().eol.isEmpty());
    }
    void roundTrip_data()
    {
        QTest::addColumn<QString>("content");
        QTest::newRow("empty") << QString();
        QTest::newRow("one char") << u"a"_s;
        QTest::newRow("mixed eols") << u"a\r\nb\nc\r"_s;
        QTest::newRow("polish") << u"zażółć\r\ngęślą\njaźń\n"_s;
        QTest::newRow("only eols") << u"\n\n\r\r\n"_s;
        QTest::newRow("outline") << u"- [ ] Zadanie ⏰ 2026-10-15 09:00\r\n  - dziecko\r\n"_s;
    }
    void roundTrip()
    {
        QFETCH(QString, content);
        QCOMPARE(text::joinLines(text::splitLines(content)), content);
        const auto lines = text::splitLines(content);
        for (const auto &l : lines)
            QCOMPARE(content.sliced(l.start, l.text.size()), l.text);
    }
    void eol()
    {
        QCOMPARE(text::detectEol(u"a\r\nb\nc"_s), u"\r\n"_s);
        QCOMPARE(text::detectEol(u"a\nb\r\nc"_s), u"\n"_s);
        QCOMPARE(text::detectEol(u"a\rb"_s), u"\r"_s);
        QCOMPARE(text::detectEol(u"single"_s), u"\n"_s);
        QCOMPARE(text::detectEol(QString()), u"\n"_s);
        const auto lines = text::splitLines(u"a\r\nb"_s);
        QCOMPARE(text::preferredEol(lines, 0), u"\r\n"_s);
        QCOMPARE(text::preferredEol(lines, 1), u"\r\n"_s);
        QCOMPARE(text::preferredEol(lines, 99), u"\r\n"_s);
        QCOMPARE(text::preferredEol(text::splitLines(u"single"_s), 0), u"\n"_s);
    }
    void splitLinesLarge()
    {
        const QString big = u"Linia ąę\r\n"_s.repeated(100'000);
        QElapsedTimer t;
        t.start();
        const auto lines = text::splitLines(big);
        QCOMPARE(lines.size(), 100'001);
        QCOMPARE(text::joinLines(lines), big);
        QVERIFY(t.elapsed() < 5000);
    }

    // ---- hashing -----------------------------------------------------------------------
    void fnvVectors()
    {
        QCOMPARE(text::fnv1a64Hex(QByteArray()), u"cbf29ce484222325"_s);
        QCOMPARE(text::fnv1a64Hex("a"), u"af63dc4c8601ec8c"_s);
        QCOMPARE(text::fnv1a64Hex("foobar"), u"85944171f73967e8"_s);
        QCOMPARE(text::fnv1a64("a"), 0xaf63dc4c8601ec8cULL);
    }
    void fnvReference_data()
    {
        QTest::addColumn<QString>("input");
        QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("upper") << u"ĄĆĘŁŃÓŚŹŻ ąćęłńóśźż"_s;
        QTest::newRow("task") << u"- [ ] Zadzwonić do mamy ⏰ 2026-10-14 18:00 🔁 every week"_s;
        QTest::newRow("emoji") << u"😀👍🏽 emoji + 漢字"_s;
        QTest::newRow("long") << u"x"_s.repeated(10'000);
    }
    void fnvReference()
    {
        QFETCH(QString, input);
        QCOMPARE(text::contentHash(input), referenceFnv(input.toUtf8()));
        QCOMPARE(text::fnv1a64Hex(input.toUtf8()), referenceFnv(input.toUtf8()));
        QCOMPARE(text::contentHash(input).size(), 16);
    }
    void fnvBytes()
    {
        QByteArray random(4096, Qt::Uninitialized);
        for (int i = 0; i < random.size(); ++i)
            random[i] = char((i * 7919 + 13) & 0xff);
        QCOMPARE(text::fnv1a64Hex(random), referenceFnv(random));
    }
    void fnvDistinguishes()
    {
        QVERIFY(text::contentHash(u"notatka"_s) != text::contentHash(u"notatk\u0430"_s)); // Cyrillic "a"
        QVERIFY(text::contentHash(u"\u017C"_s) != text::contentHash(u"z\u0307"_s));      // NFC vs NFD
        QVERIFY(text::contentHash(u"a\nb"_s) != text::contentHash(u"a\r\nb"_s));
        QCOMPARE(text::contentHash(u"0"_s).size(), 16); // leading zeros are kept
    }
};

QTEST_GUILESS_MAIN(TestText)
#include "tst_text.moc"
