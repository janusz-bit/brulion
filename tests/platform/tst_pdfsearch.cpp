// The PDF viewer's matcher (src/platform/pdfsearch.h), pure, on every platform: case- and
// diacritics-insensitive search in a page's text, runs of white space equal, soft hyphens ignored,
// ranges that never split a surrogate pair or a letter from its combining marks, the cap, malformed
// text; the rectangles of a range from per-character boxes (one per line); the distinct spellings
// Android's own search is asked for.
#include "pdfsearch.h"

#include <QElapsedTimer>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

using Ranges = QList<PdfTextRange>;
Q_DECLARE_METATYPE(PdfTextRange)

namespace {

QString describe(const Ranges &ranges)
{
    QStringList parts;
    for (const PdfTextRange &r : ranges)
        parts.append(u"(%1,%2)"_s.arg(r.start).arg(r.length));
    return parts.join(u' ');
}

} // namespace

class TestPdfSearch : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void findPdfText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("maxHits");
        QTest::addColumn<Ranges>("expected");

        QTest::newRow("without diacritics") << u"Zażółć gęślą jaźń"_s << u"gesla"_s << 10 << Ranges{{7, 5}};
        QTest::newRow("upper case") << u"Zażółć gęślą jaźń"_s << u"GĘŚLĄ"_s << 10 << Ranges{{7, 5}};
        QTest::newRow("both spellings") << u"ŻÓŁW i żółw"_s << u"zolw"_s << 10 << Ranges{{0, 4}, {7, 4}};
        QTest::newRow("across a line break") << u"wykład\r\n3"_s << u"wykład 3"_s << 10 << Ranges{{0, 9}};
        QTest::newRow("tab and spaces") << u"a \t  b"_s << u"a b"_s << 10 << Ranges{{0, 6}};
        QTest::newRow("no-break space") << u"strona\u00a03"_s << u"strona 3"_s << 10 << Ranges{{0, 8}};
        QTest::newRow("line separator") << u"strona\u20283"_s << u"strona 3"_s << 10 << Ranges{{0, 8}};
        QTest::newRow("query's own spaces") << u"a b"_s << u"  a   b  "_s << 10 << Ranges{{0, 3}};
        QTest::newRow("decomposed letter") << u"e\u0328"_s << u"ę"_s << 10 << Ranges{{0, 2}};
        QTest::newRow("composed query, decomposed text") << u"ge\u0328s"_s << u"gęs"_s << 10 << Ranges{{0, 4}};
        QTest::newRow("not overlapping") << u"aaaa"_s << u"aa"_s << 10 << Ranges{{0, 2}, {2, 2}};
        QTest::newRow("empty text") << QString() << u"x"_s << 10 << Ranges{};
        QTest::newRow("only spaces") << u"x"_s << u"   "_s << 10 << Ranges{};
        QTest::newRow("only a combining mark") << u"x"_s << u"\u0301"_s << 10 << Ranges{};
        QTest::newRow("empty query") << u"x"_s << QString() << 10 << Ranges{};
        QTest::newRow("longer than the text") << u"żółw"_s << u"żółwie"_s << 10 << Ranges{};
        QTest::newRow("after an emoji") << u"😀 łódź"_s << u"lodz"_s << 10 << Ranges{{3, 4}};
        QTest::newRow("an emoji itself") << u"a😀b"_s << u"😀"_s << 10 << Ranges{{1, 2}};
        QTest::newRow("lone surrogate") << (u"x"_s + QChar(0xd800) + u"łódź"_s) << u"lodz"_s << 10 << Ranges{{2, 4}};
        QTest::newRow("soft hyphen") << u"wy\u00adkład"_s << u"wykład"_s << 10 << Ranges{{0, 7}};
        QTest::newRow("pdfium's hyphen marker") << (u"wy"_s + QChar(0x0002) + u"kład"_s) << u"wykład"_s << 10
                                                 << Ranges{{0, 7}};
        QTest::newRow("cap") << u"ab ab ab ab ab ab ab ab ab ab"_s << u"ab"_s << 3 << Ranges{{0, 2}, {3, 2}, {6, 2}};
        QTest::newRow("no hits allowed") << u"ab"_s << u"ab"_s << 0 << Ranges{};
        QTest::newRow("one character") << u"żółw"_s << u"o"_s << 10 << Ranges{{1, 1}};
        QTest::newRow("not there") << u"Zażółć gęślą jaźń"_s << u"żółw"_s << 10 << Ranges{};
    }

    void findPdfText()
    {
        QFETCH(QString, text);
        QFETCH(QString, query);
        QFETCH(int, maxHits);
        QFETCH(Ranges, expected);
        const Ranges found = brulion::platform::findPdfText(text, query, maxHits);
        QVERIFY2(found == expected, qPrintable(describe(found) + u" ≠ "_s + describe(expected)));
        for (const PdfTextRange &r : found) {
            QVERIFY(r.start >= 0 && r.length > 0 && r.start + r.length <= text.size());
            // Never half of a surrogate pair at either end.
            QVERIFY(!text.at(r.start).isLowSurrogate());
            QVERIFY(r.start + r.length == text.size() || !text.at(r.start + r.length).isLowSurrogate());
        }
    }

    void manyHitsAreFast()
    {
        // 100 000 characters, 1000 hits: well under the viewer's frame of work per page.
        QString text;
        while (text.size() < 100'000)
            text += u"Zażółć gęślą jaźń, żółw. "_s;
        QElapsedTimer timer;
        timer.start();
        const Ranges found = brulion::platform::findPdfText(text, u"ZOLW"_s, 1000);
        const qint64 ms = timer.elapsed();
        qInfo("1000 hits in 100 000 characters: %lld ms", ms);
        QCOMPARE(found.size(), 1000);
        QVERIFY2(ms < 200, qPrintable(QString::number(ms)));
    }

    void rangeRects_data()
    {
        QTest::addColumn<QList<QRectF>>("boxes");
        QTest::addColumn<PdfTextRange>("range");
        QTest::addColumn<QList<QRectF>>("expected");
        // "abc def" on one line, 10 pt a letter, the space a synthetic (empty) box.
        const QList<QRectF> line{QRectF(0, 0, 10, 12),  QRectF(10, 0, 10, 12), QRectF(20, 0, 10, 12), QRectF(),
                                 QRectF(40, 0, 10, 12), QRectF(50, 0, 10, 12), QRectF(60, 0, 10, 12)};
        QTest::newRow("one line") << line << PdfTextRange{0, 7} << QList<QRectF>{QRectF(0, 0, 70, 12)};
        QTest::newRow("a word") << line << PdfTextRange{4, 3} << QList<QRectF>{QRectF(40, 0, 30, 12)};
        // "ab" / "cd" on two lines, a line break between them.
        const QList<QRectF> two{QRectF(0, 0, 10, 12), QRectF(10, 0, 10, 12), QRectF(), QRectF(0, 20, 10, 12),
                                QRectF(10, 20, 10, 12)};
        QTest::newRow("across a line break") << two << PdfTextRange{1, 3}
                                             << QList<QRectF>{QRectF(10, 0, 10, 12), QRectF(0, 20, 10, 12)};
        // Letters of differing heights on one line (a capital, a small letter, a subscript).
        const QList<QRectF> heights{QRectF(0, 0, 10, 14), QRectF(10, 4, 10, 10), QRectF(20, 6, 6, 9)};
        QTest::newRow("differing heights") << heights << PdfTextRange{0, 3} << QList<QRectF>{QRectF(0, 0, 26, 15)};
        QTest::newRow("only synthetic boxes") << QList<QRectF>{QRectF(), QRectF()} << PdfTextRange{0, 2}
                                              << QList<QRectF>{};
        QTest::newRow("range beyond the boxes") << line << PdfTextRange{5, 100}
                                                << QList<QRectF>{QRectF(50, 0, 20, 12)};
        QTest::newRow("negative range") << line << PdfTextRange{-3, 2} << QList<QRectF>{};
        QTest::newRow("not a number")
                << QList<QRectF>{QRectF(qQNaN(), 0, 10, 12), QRectF(10, 0, 10, 12)} << PdfTextRange{0, 2}
                << QList<QRectF>{QRectF(10, 0, 10, 12)};
    }

    void rangeRects()
    {
        QFETCH(QList<QRectF>, boxes);
        QFETCH(PdfTextRange, range);
        QFETCH(QList<QRectF>, expected);
        QCOMPARE(pdfRangeRects(boxes, range), expected);
    }

    void spellings_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("query");
        QTest::addColumn<int>("max");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("three spellings") << u"Żółw, żółw, ŻÓŁW, żółw"_s << u"zolw"_s << 16
                                         << QStringList{u"Żółw"_s, u"żółw"_s, u"ŻÓŁW"_s};
        QTest::newRow("white space as in the text") << u"wykład\n3 i wykład 3"_s << u"wykład 3"_s << 16
                                                    << QStringList{u"wykład\n3"_s, u"wykład 3"_s};
        QTest::newRow("capped") << u"a A a A"_s << u"a"_s << 1 << QStringList{u"a"_s};
        QTest::newRow("none") << u"tekst"_s << u"żółw"_s << 16 << QStringList{};
    }

    void spellings()
    {
        QFETCH(QString, text);
        QFETCH(QString, query);
        QFETCH(int, max);
        QFETCH(QStringList, expected);
        QCOMPARE(pdfMatchSpellings(text, brulion::platform::findPdfText(text, query, 100), max), expected);
        // Ranges outside the text are skipped, never read.
        QCOMPARE(pdfMatchSpellings(text, {{-1, 3}, {0, 1000}}, max), QStringList{});
    }

    void spellingsCapAtSixteen()
    {
        QString text;
        // 20 different spellings of "ab": every mix of cases of a longer word, then repeated.
        const QStringList forms{u"ab"_s, u"Ab"_s, u"aB"_s, u"AB"_s};
        for (int i = 0; i < 40; ++i)
            text += forms.at(i % 4) + QString(i / 4, u'x') + u' ';
        const Ranges ranges = brulion::platform::findPdfText(text, u"ab"_s, 100);
        QCOMPARE(ranges.size(), 40);
        QCOMPARE(pdfMatchSpellings(text, ranges).size(), 4);
        QStringList many;
        for (int i = 0; i < 30; ++i)
            many.append(u"ż"_s + QString::number(i));
        const QString joined = many.join(u' ');
        Ranges all;
        qsizetype at = 0;
        for (const QString &word : std::as_const(many)) {
            all.append({at, word.size()});
            at += word.size() + 1;
        }
        QCOMPARE(pdfMatchSpellings(joined, all).size(), 16);
    }
};

QTEST_GUILESS_MAIN(TestPdfSearch)
#include "tst_pdfsearch.moc"
