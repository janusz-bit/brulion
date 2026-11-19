// unicode::nfc: QString's NFC for every real text, in linear time for the texts that are not.
#include "util/unicode.h"

#include "testtiming.h"

#include <QRandomGenerator>
#include <QTest>

using namespace brulion;
using namespace Qt::StringLiterals;

namespace {

// Combining marks by their canonical class (the order NFC puts them in), written as escapes:
// what an editor shows of them depends on the letter before.
const QString Acute = u"\u0301"_s;      // above (230)
const QString Grave = u"\u0300"_s;      // above (230)
const QString Circumflex = u"\u0302"_s; // above (230)
const QString DotAbove = u"\u0307"_s;   // above (230)
const QString Ogonek = u"\u0328"_s;     // attached below (202)
const QString DotBelow = u"\u0323"_s;   // below (220)
const QString GraveBelow = u"\u0316"_s; // below (220)

QString qtNfc(const QString &s)
{
    return s.normalized(QString::NormalizationForm_C);
}

QString nfd(const QString &s)
{
    return s.normalized(QString::NormalizationForm_D);
}

/// `n` times `piece`.
QString times(const QString &piece, int n)
{
    return piece.repeated(n);
}

} // namespace

class TestUnicode : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void asQtNormalises_data()
    {
        QTest::addColumn<QString>("text");
        const QString polish = u"Zażółć gęślą jaźń"_s;
        const QString hangul = u"한글"_s; // two syllables; decomposed: six conjoining jamo
        QTest::newRow("empty") << QString();
        QTest::newRow("ASCII") << u"Projekty/Dom.md"_s;
        QTest::newRow("Polish, composed") << polish;
        QTest::newRow("Polish, decomposed") << nfd(polish);
        QTest::newRow("marks out of order")
                << (u"a"_s + Acute + Ogonek + u" o"_s + Circumflex + DotBelow + u" e"_s + Grave + GraveBelow + Acute);
        QTest::newRow("a mark first") << (Acute + u"a"_s + Ogonek);
        QTest::newRow("Hangul jamo") << nfd(hangul);
        QTest::newRow("a Hangul syllable and its final") << u"하\u11AB 가\u11A8"_s;
        QTest::newRow("a vowel sign that composes (Bengali)") << u"\u0995\u09C7\u09BE \u0995\u09C7\u09D7"_s;
        QTest::newRow("singleton decompositions") << u"\u0340a\u0341 \u212B \u2126 \u212A"_s;
        QTest::newRow("outside the BMP") << u"\U0001F600 \U0001D157\U0001D165 \U00011099\U000110BA \U0001D15E"_s;
        QTest::newRow("a lone surrogate") << (u"a"_s + QChar(0xD83D) + u"b"_s + Acute);
        // Longer than one call of QString::normalized() gets here: the pieces fit together.
        QTest::newRow("long, decomposed pairs") << times(nfd(u"ąé "_s), 700);
        QTest::newRow("long, composed after one decomposed pair") << (nfd(u"ą"_s) + times(u"ąęłżź"_s, 600));
        QTest::newRow("long, no space to cut at") << times(nfd(u"żółw"_s), 500);
        QTest::newRow("long Hangul jamo") << times(nfd(hangul), 400);
        QTest::newRow("long, composing vowel signs") << times(u"\u0995\u09C7\u09BE"_s, 400);
        QTest::newRow("long, every letter with as many marks as fit")
                << times(u"a"_s + times(GraveBelow + Acute, int(unicode::MaxMarks / 2) - 1), 40);
        QTest::newRow("ASCII, then one decomposed letter at the end") << (times(u"abc def "_s, 300) + nfd(u"é"_s));
    }
    void asQtNormalises()
    {
        QFETCH(QString, text);
        QCOMPARE(unicode::nfc(text), qtNfc(text));
        QCOMPARE(unicode::nfc(QStringView(text)), qtNfc(text));
        // Normalised text stays as it is.
        QCOMPARE(unicode::nfc(unicode::nfc(text)), unicode::nfc(text));
    }

    void asQtNormalisesRandomText()
    {
        // Letters of many kinds with a few marks each: every cut between pieces is one Qt's
        // normalisation would not reach across.
        // clang-format off
        const QStringList pool{
                u"a"_s, u"e"_s, u"Z"_s, u" "_s, u"/"_s, u"ą"_s, u"ż"_s, u"ł"_s,
                Acute, Grave, Circumflex, DotAbove, Ogonek, DotBelow, GraveBelow,
                u"\u0344"_s,                                        // decomposes into two marks
                u"\u1100"_s, u"\u1161"_s, u"\u11A8"_s,               // Hangul initial, vowel, final
                u"가"_s, u"각"_s,                                    // syllables without and with a final
                u"\u0995"_s, u"\u09C7"_s, u"\u09BE"_s, u"\u09D7"_s,  // Bengali: a vowel sign composes with the next
                u"\u212B"_s,                                        // the Angstrom sign: a singleton
                u"\U0001F600"_s,                                    // an emoji
                u"\u05D0"_s, u"\u05B4"_s, u"\u05B7"_s, u"\uFB1D"_s,  // Hebrew: points, a composition exclusion
                u"\u0F71"_s, u"\u0F72"_s,                           // Tibetan vowel signs (129, 130)
                u"\U0001D157"_s, u"\U0001D165"_s,                   // musical symbols that compose (excluded)
                u"\u01C6"_s,
        };
        // clang-format on
        QRandomGenerator rng(20261115);
        for (int round = 0; round < 400; ++round) {
            QString text;
            const int pieces = 1 + int(rng.bounded(1500));
            int marks = 0;
            for (int i = 0; i < pieces; ++i) {
                const QString &piece = pool[rng.bounded(pool.size())];
                const char32_t first = piece.toUcs4().constFirst();
                const bool mark = QChar::isMark(first) || (first >= 0x1161 && first <= 0x11C2);
                // (A letter keeps at most a dozen marks here: more than MaxMarks is the next test.)
                marks = mark ? marks + 1 : 0;
                if (marks <= 12)
                    text += piece;
            }
            if (unicode::nfc(text) != qtNfc(text)) {
                QString codes;
                for (const QChar c : std::as_const(text))
                    codes += QString::number(c.unicode(), 16) + u' ';
                QFAIL(qPrintable(u"round %1: %2"_s.arg(round).arg(codes.left(3000))));
            }
        }
    }

    void aLetterWithTooManyMarksStaysAsWritten()
    {
        // Before and after it the text is normalised; the letter keeps its marks in their order.
        const QString heap = u"a"_s + times(Acute, 40) + times(GraveBelow, 40);
        QVERIFY(heap.size() > unicode::MaxMarks);
        const QString text = nfd(u"żółw "_s) + heap + nfd(u" gęś"_s);
        QCOMPARE(unicode::nfc(text), u"żółw "_s + heap + u" gęś"_s);
        QCOMPARE(unicode::nfc(heap), heap);
        QVERIFY(qtNfc(heap) != heap); // (Qt puts the marks below first)
        // As many marks as the limit allows: Qt's result.
        const QString fits = u"a"_s + times(Acute, 31) + times(GraveBelow, 32);
        QCOMPARE(fits.size(), unicode::MaxMarks);
        QCOMPARE(unicode::nfc(fits), qtNfc(fits));
        QCOMPARE(unicode::nfc(u"x "_s + fits + u" y"_s), u"x "_s + qtNfc(fits) + u" y"_s);
    }

    void timeIsLinear_data()
    {
        QTest::addColumn<QString>("text");
        // What takes Qt's NFC seconds to hours (16 000 + 16 000 marks out of order: 4 s, and
        // four times that for twice as many).
        QTest::newRow("a tag of marks out of order") << (u"a"_s + times(Acute, 200'000) + times(GraveBelow, 200'000));
        QTest::newRow("many letters with their marks out of order")
                << times(u"a"_s + Acute + GraveBelow + Ogonek, 150'000);
        QTest::newRow("letters after one decomposed pair") << (nfd(u"ą"_s) + times(u"ą"_s, 400'000));
        QTest::newRow("a tone mark, then letters") << (u"\u0340"_s + times(u"żółw"_s, 100'000));
        QTest::newRow("decomposed pairs") << times(nfd(u"ą"_s), 300'000);
        QTest::newRow("Hangul jamo") << times(nfd(u"한"_s), 150'000);
        QTest::newRow("marks and nothing else") << times(GraveBelow + Acute, 200'000);
    }
    void timeIsLinear()
    {
        QFETCH(QString, text);
        brulion::test::CpuTimer timer;
        timer.start();
        const QString result = unicode::nfc(text);
        const qint64 elapsed = timer.elapsed();
        QVERIFY(!result.isEmpty());
        qInfo("%lld characters in %lld ms", static_cast<long long>(text.size()), static_cast<long long>(elapsed));
        // (Quadratic, the first row alone would take a quarter of an hour.)
        QVERIFY2(elapsed < 2000 * BRULION_TEST_SLOWDOWN, qPrintable(u"%1 ms"_s.arg(elapsed)));
    }
};

QTEST_GUILESS_MAIN(TestUnicode)
#include "tst_unicode.moc"
