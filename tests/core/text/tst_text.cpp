#include "text/decode.h"
#include "text/text.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <memory>
#include <vector>

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

/// One code point (a surrogate code point: a lone surrogate). Not QString::fromUcs4(), which
/// drops U+FEFF as a byte order mark.
QString codePointString(char32_t cp)
{
    if (!QChar::requiresSurrogates(cp))
        return QString(QChar(char16_t(cp)));
    const QChar pair[2] = {QChar(QChar::highSurrogate(cp)), QChar(QChar::lowSurrogate(cp))};
    return QString(pair, 2);
}

/// The fold as first implemented (and as specified): NFD of the whole string, ł/Ł -> l/L,
/// combining marks dropped, lone surrogates -> U+FFFD. Quadratic on long texts, fine here.
QString wholeStringFoldDiacritics(const QString &s)
{
    QString decomposed = s.normalized(QString::NormalizationForm_D);
    decomposed.replace(u'ł', u'l');
    decomposed.replace(u'Ł', u'L');
    QString out;
    for (qsizetype i = 0; i < decomposed.size();) {
        char32_t cp = decomposed.at(i).unicode();
        if (QChar::isHighSurrogate(cp) && i + 1 < decomposed.size() && decomposed.at(i + 1).isLowSurrogate()) {
            cp = QChar::surrogateToUcs4(decomposed.at(i), decomposed.at(i + 1));
            ++i;
        }
        ++i;
        if (QChar::isMark(cp))
            continue;
        out += codePointString(QChar::isSurrogate(cp) ? char32_t(0xFFFD) : cp);
    }
    return out;
}

QString wholeStringFoldKey(const QString &s)
{
    return wholeStringFoldDiacritics(s.toLower());
}

/// Code points as hex, for failure messages.
QString hex(const QString &s)
{
    QStringList units;
    for (const QChar c : s)
        units.append(QString::number(c.unicode(), 16));
    return units.join(u' ');
}

/// Code point ranges that stress folding: ASCII, Latin-1 / Extended (Polish), combining marks
/// (incl. reordering), Greek (final sigma, tonos), Cyrillic, Hebrew points, Vietnamese stacked
/// marks, Greek Extended, letterlike signs (Ohm, Kelvin, Angstrom), kana with voicing marks,
/// Hangul syllables and Jamo, CJK compatibility ideographs, ligatures, fullwidth letters,
/// variation selectors, Deseret (supplementary case pairs), musical symbol decompositions,
/// emoji, lone surrogates, İ/ı, ł/Ł, U+FEFF and the last BMP code points.
const std::vector<std::pair<char32_t, char32_t>> &foldRanges()
{
    static const std::vector<std::pair<char32_t, char32_t>> ranges = {
            {0x20, 0x7E},       {0xA0, 0x24F},      {0x300, 0x36F},   {0x370, 0x3FF},     {0x400, 0x4FF},
            {0x591, 0x5C7},     {0x1E00, 0x1EFF},   {0x1F00, 0x1FFF}, {0x2126, 0x212B},   {0x3040, 0x30FF},
            {0xAC00, 0xAC40},   {0xD7A0, 0xD7A3},   {0x1100, 0x1112}, {0xF900, 0xF910},   {0xFB00, 0xFB06},
            {0xFB1D, 0xFB4E},   {0xFF21, 0xFF5A},   {0xFE00, 0xFE0F}, {0x10400, 0x1044F}, {0x1D15E, 0x1D164},
            {0x1F600, 0x1F64F}, {0xE0100, 0xE0110}, {0xD800, 0xDFFF}, {0x130, 0x131},     {0x141, 0x142},
            {0xFEFF, 0xFEFF},   {0xFFF9, 0xFFFF}};
    return ranges;
}

} // namespace

class TestText : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // ---- folding -----------------------------------------------------------------------
    // First slot on purpose: the folding table is filled lazily, here by four threads at once.
    void foldTableConcurrent()
    {
        QString all;
        for (char32_t cp = 0x80; cp <= 0xFFFF; ++cp) {
            if (!QChar::isSurrogate(cp))
                all += QChar(char16_t(cp));
        }
        QString expectedKey;
        QString expectedDiacritics;
        for (const QChar c : std::as_const(all)) {
            expectedKey += wholeStringFoldKey(QString(c));
            expectedDiacritics += wholeStringFoldDiacritics(QString(c));
        }
        constexpr int Threads = 4;
        QString keys[Threads];
        QString diacritics[Threads];
        std::vector<std::unique_ptr<QThread>> threads;
        for (int t = 0; t < Threads; ++t) {
            threads.emplace_back(QThread::create([&, t] {
                // Alternate the order so the threads also race on the second half of an entry.
                if (t % 2 == 0) {
                    keys[t] = text::foldKey(all);
                    diacritics[t] = text::foldDiacritics(all);
                } else {
                    diacritics[t] = text::foldDiacritics(all);
                    keys[t] = text::foldKey(all);
                }
            }));
        }
        for (const auto &thread : threads)
            thread->start();
        for (const auto &thread : threads)
            QVERIFY(thread->wait(60000));
        for (int t = 0; t < Threads; ++t) {
            QCOMPARE(keys[t], expectedKey);
            QCOMPARE(diacritics[t], expectedDiacritics);
        }
    }

    void foldEveryCodePoint()
    {
        // Folding one code point at a time equals the whole-string NFD because NFD's canonical
        // reordering only moves characters with a non-zero combining class, and those are all
        // combining marks, which folding drops.
        for (char32_t cp = 0; cp <= 0x10FFFF; ++cp) {
            if (QChar::combiningClass(cp) != 0 && !QChar::isMark(cp))
                QFAIL(qPrintable(u"U+%1 has a combining class but is no mark"_s.arg(quint32(cp), 4, 16, QChar(u'0'))));
        }
        // Every BMP code point (the lazily filled table), plane 1 (case pairs, decompositions,
        // emoji), the CJK compatibility supplement and the variation selectors, alone and
        // between ASCII letters.
        const auto check = [](char32_t cp) {
            const QString one = codePointString(cp);
            const QString inWord = u"Ab"_s + one + u"Cd"_s;
            for (const QString &s : {one, inWord}) {
                const QString key = text::foldKey(s);
                if (key != wholeStringFoldKey(s))
                    return u"foldKey U+%1: %2 instead of %3"_s.arg(quint32(cp), 4, 16, QChar(u'0'))
                            .arg(hex(key), hex(wholeStringFoldKey(s)));
                const QString diacritics = text::foldDiacritics(s);
                if (diacritics != wholeStringFoldDiacritics(s))
                    return u"foldDiacritics U+%1: %2 instead of %3"_s.arg(quint32(cp), 4, 16, QChar(u'0'))
                            .arg(hex(diacritics), hex(wholeStringFoldDiacritics(s)));
                if (text::foldKeyWithMap(s).text != key)
                    return u"foldKeyWithMap U+%1"_s.arg(quint32(cp), 4, 16, QChar(u'0'));
            }
            return QString();
        };
        for (const auto &[first, last] :
             {std::pair<char32_t, char32_t>{0, 0x1FFFF}, {0x2F800, 0x2FA1F}, {0xE0000, 0xE01EF}}) {
            for (char32_t cp = first; cp <= last; ++cp) {
                if (const QString error = check(cp); !error.isEmpty())
                    QFAIL(qPrintable(error));
            }
        }
    }

    void foldRandomStrings()
    {
        // Property: the linear fold equals the whole-string fold on random Unicode (adjacent
        // marks of different classes, decomposed input, surrogate pairs and lone surrogates).
        const auto &ranges = foldRanges();
        QRandomGenerator rng(20261017);
        for (int k = 0; k < 20000; ++k) {
            QString s;
            const int len = int(rng.bounded(1, 24));
            for (int j = 0; j < len; ++j) {
                const auto &range = ranges[rng.bounded(int(ranges.size()))];
                s += codePointString(range.first + rng.bounded(quint32(range.second - range.first + 1)));
            }
            const QString key = text::foldKey(s);
            if (key != wholeStringFoldKey(s))
                QFAIL(qPrintable(u"foldKey(%1): %2 instead of %3"_s.arg(hex(s), hex(key), hex(wholeStringFoldKey(s)))));
            const QString diacritics = text::foldDiacritics(s);
            if (diacritics != wholeStringFoldDiacritics(s))
                QFAIL(qPrintable(u"foldDiacritics(%1): %2 instead of %3"_s.arg(hex(s), hex(diacritics),
                                                                               hex(wholeStringFoldDiacritics(s)))));
            // The map: one offset per folded unit, non-decreasing, each the start of a code
            // point of `s`, then s.size().
            const text::FoldedText f = text::foldKeyWithMap(s);
            QCOMPARE(f.text, key);
            QCOMPARE(f.map.size(), f.text.size() + 1);
            QCOMPARE(f.map.last(), s.size());
            for (qsizetype u = 0; u < f.text.size(); ++u) {
                const qsizetype at = f.map.at(u);
                QVERIFY(at >= 0 && at < s.size());
                QVERIFY(u == 0 || f.map.at(u - 1) <= at);
                QVERIFY(!(s.at(at).isLowSurrogate() && at > 0 && s.at(at - 1).isHighSurrogate()));
            }
        }
    }

    void foldLongText()
    {
        // A long Polish text with decomposed letters, emoji and Hangul (the result grows) folds
        // like the whole-string reference, and fast: the whole-string NFD was quadratic (this
        // 2 M character text took over 3 minutes on the x86_64 laptop).
        const QString paragraph = u"Zażółć gęślą jaźń — „Środa” o 9:00 🔁 S\u0301RODA Łódź İstanbul 한국어 ΣΊΣΥΦΟΣ\n"_s;
        const QString big = paragraph.repeated(50000 / paragraph.size() + 1);
        QCOMPARE(text::foldKey(big), wholeStringFoldKey(big));
        QCOMPARE(text::foldDiacritics(big), wholeStringFoldDiacritics(big));
        QCOMPARE(text::foldKeyWithMap(big).text, wholeStringFoldKey(big));
        const QString huge = paragraph.repeated(2000000 / paragraph.size() + 1);
        QElapsedTimer timer;
        timer.start();
        const QString folded = text::foldKey(huge);
        QVERIFY(folded.size() > 1000000);
        // Generous bound (Debug builds, slow machines); a few hundred ms on the laptop.
        QVERIFY2(timer.elapsed() < 5000 * BRULION_TEST_SLOWDOWN, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
    }

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
        QTest::newRow("zero width no-break space kept") << u"Ą\uFEFFb"_s << u"A\uFEFFb"_s;
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
        // Not a byte order mark inside a text (foldKeyWithMap used to drop it).
        QTest::newRow("zero width no-break space kept") << u"\uFEFFŚroda"_s << u"\uFEFFsroda"_s;
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

    // ---- names -------------------------------------------------------------------------
    // The order of the sidebar's tree and of All pages (the same in the browser: no ICU).
    void compareNames_data()
    {
        QTest::addColumn<QString>("a");
        QTest::addColumn<QString>("b");
        QTest::addColumn<int>("sign");
        QTest::newRow("ć after c, before d") << u"Ćwiczenia"_s << u"Dom"_s << -1;
        QTest::newRow("c before ć") << u"Cyrk"_s << u"Ćwiczenia"_s << -1;
        QTest::newRow("ł before m") << u"Łódź"_s << u"Mama"_s << -1;
        QTest::newRow("l before ł") << u"Lublin"_s << u"Łódź"_s << -1;
        QTest::newRow("z before ż") << u"Zakupy"_s << u"Żagle"_s << -1;
        QTest::newRow("ź before ż") << u"Źródła"_s << u"Żagle"_s << -1;
        QTest::newRow("ś after s, before t") << u"Śląsk"_s << u"Tatry"_s << -1;
        QTest::newRow("ś after every s") << u"Szczecin"_s << u"Śląsk"_s << -1;
        QTest::newRow("ą after a") << u"Azja"_s << u"Ąb"_s << -1;
        QTest::newRow("numbers by value") << u"Rozdział 2"_s << u"Rozdział 10"_s << -1;
        QTest::newRow("leading zeros ignored") << u"Rozdział 007"_s << u"Rozdział 8"_s << -1;
        QTest::newRow("numbers before letters") << u"2026"_s << u"Archiwum"_s << -1;
        QTest::newRow("case-insensitive") << u"apple"_s << u"Banana"_s << -1;
        QTest::newRow("case ties by code units") << u"Dom"_s << u"dom"_s << -1;
        QTest::newRow("prefix first") << u"Dom"_s << u"Dom Jana"_s << -1;
        QTest::newRow("space before letters") << u"Dom Jana"_s << u"Doma"_s << -1;
        QTest::newRow("other accents like the base letter") << u"éclair"_s << u"ecz"_s << -1;
        QTest::newRow("NFD like NFC") << u"Z\u0307agle"_s << u"Zakupy"_s << 1;
        QTest::newRow("equal") << u"Żółw"_s << u"Żółw"_s << 0;
        QTest::newRow("empty first") << QString() << u"a"_s << -1;
        QTest::newRow("both empty") << QString() << QString() << 0;
        QTest::newRow("emoji after letters") << u"😀"_s << u"Zebra"_s << 1;
        QTest::newRow("lone surrogate") << QString(QChar(0xD83D)) << u"a"_s << 1;
    }
    void compareNames()
    {
        QFETCH(QString, a);
        QFETCH(QString, b);
        QFETCH(int, sign);
        const int ab = text::compareNames(a, b);
        const int ba = text::compareNames(b, a);
        QCOMPARE(ab < 0 ? -1 : ab > 0 ? 1 : 0, sign);
        QCOMPARE(ba, -ab);
        QCOMPARE(text::compareNameKeys(text::nameSortKey(a), a, text::nameSortKey(b), b), ab);
    }
    void compareNamesSortsPolishList()
    {
        QStringList names{u"Żagle"_s,      u"Zakupy"_s, u"Łódź"_s, u"Śląsk"_s, u"Ćwiczenia"_s, u"Rozdział 10"_s,
                          u"Rozdział 2"_s, u"ąb"_s,     u"Azja"_s, u"lody"_s,  u"Mama"_s};
        std::sort(names.begin(), names.end(),
                  [](const QString &a, const QString &b) { return text::compareNames(a, b) < 0; });
        QCOMPARE(names, (QStringList{u"Azja"_s, u"ąb"_s, u"Ćwiczenia"_s, u"lody"_s, u"Łódź"_s, u"Mama"_s,
                                     u"Rozdział 2"_s, u"Rozdział 10"_s, u"Śląsk"_s, u"Zakupy"_s, u"Żagle"_s}));
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

    // text/decode.h: files of unknown origin (imports, shares) decode to some text, never fail.
    void decodeText_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<QString>("expected");
        QTest::newRow("utf-8") << u"Zażółć gęślą jaźń"_s.toUtf8() << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("utf-8 bom") << QByteArray("\xEF\xBB\xBF") + u"ćma\r\n"_s.toUtf8() << u"ćma\r\n"_s;
        QTest::newRow("utf-16 le bom") << QByteArray("\xFF\xFE\x7C\x01", 4) << u"ż"_s;
        QTest::newRow("utf-16 be bom") << QByteArray("\xFE\xFF\x01\x7A", 4) << u"ź"_s;
        QTest::newRow("windows-1250") << QByteArray("\xA3\xF3"
                                                    "d\x9F")
                                      << u"Łódź"_s;
        QTest::newRow("windows-1252") << QByteArray("na\xEFve \x93q\x94") << u"naïve \u201Cq\u201D"_s;
        QTest::newRow("lone continuation byte") << QByteArray("a\x80") << u"a\u20AC"_s;
    }
    void decodeText()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(QString, expected);
        QCOMPARE(text::decodeText(bytes), expected);
    }
};

QTEST_GUILESS_MAIN(TestText)
#include "tst_text.moc"
