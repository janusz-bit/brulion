// The PDF viewer's text picked by pointing and what a PDF says about itself (src/platform/pdftext.h),
// pure, on every platform: the caret nearest to a point (inside a character's halves, beside a line,
// in a margin, between lines, units sharing a box — a surrogate pair, a combining mark —, empty and
// non-finite boxes), the range between two points in either order; the clipboard's text (line
// breaks, soft hyphens, pdfium's U+0002, controls, lone surrogates); outline titles (white space,
// controls, the cap without splitting a pair); the link addresses that may open (http, https,
// mailto — never javascript:, file:, data:, an app's scheme, white space, control characters).
#include "pdftext.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::platform;

Q_DECLARE_METATYPE(PdfTextRange)

namespace {

/// Boxes of a line of `count` characters, each 10 × 20 pt, from (x, y).
QList<QRectF> line(qreal x, qreal y, int count)
{
    QList<QRectF> out;
    for (int i = 0; i < count; ++i)
        out.append(QRectF(x + i * 10, y, 10, 20));
    return out;
}

/// "Zażółć gęślą" over two lines: "Zażółć" (6) at y 100, a synthetic line break (an empty box),
/// "gęślą" (5) at y 130.
QList<QRectF> twoLines()
{
    QList<QRectF> boxes = line(50, 100, 6);
    boxes.append(QRectF());
    boxes.append(line(50, 130, 5));
    return boxes;
}

} // namespace

class TestPdfText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void caretAt_data()
    {
        QTest::addColumn<QList<QRectF>>("boxes");
        QTest::addColumn<QPointF>("point");
        QTest::addColumn<qsizetype>("caret");

        const QList<QRectF> two = twoLines();
        QTest::newRow("left half of the first") << two << QPointF(52, 110) << qsizetype(0);
        QTest::newRow("right half of the first") << two << QPointF(58, 110) << qsizetype(1);
        QTest::newRow("middle of a word") << two << QPointF(71, 105) << qsizetype(2);
        QTest::newRow("after the first line's end") << two << QPointF(400, 110) << qsizetype(6);
        QTest::newRow("before the second line") << two << QPointF(0, 140) << qsizetype(7);
        QTest::newRow("end of the text") << two << QPointF(500, 145) << qsizetype(12);
        QTest::newRow("top margin: the start") << two << QPointF(76, 10) << qsizetype(0);
        QTest::newRow("bottom margin: the end") << two << QPointF(51, 800) << qsizetype(12);
        QTest::newRow("left margin of the first line") << two << QPointF(0, 110) << qsizetype(0);
        QTest::newRow("between the lines, nearer the first") << two << QPointF(86, 122) << qsizetype(4);
        QTest::newRow("between the lines, nearer the second") << two << QPointF(86, 128) << qsizetype(11);

        // A surrogate pair (one emoji, two units, one box) and a combining mark sharing its letter's box.
        QList<QRectF> pair = line(0, 0, 1);
        pair.append(QRectF(10, 0, 10, 20));
        pair.append(QRectF(10, 0, 10, 20));
        pair.append(QRectF(20, 0, 10, 20));
        QTest::newRow("after a surrogate pair") << pair << QPointF(18, 10) << qsizetype(3);
        QTest::newRow("before a surrogate pair") << pair << QPointF(12, 10) << qsizetype(1);

        QList<QRectF> holes{QRectF(), QRectF(0, 0, 0, 20), QRectF(qQNaN(), 0, 10, 10), QRectF(30, 0, 10, 20)};
        QTest::newRow("empty and non-finite boxes skipped") << holes << QPointF(0, 5) << qsizetype(3);
        QTest::newRow("no boxes") << QList<QRectF>() << QPointF(0, 0) << qsizetype(-1);
        QTest::newRow("only empty boxes") << QList<QRectF>{QRectF(), QRectF()} << QPointF(0, 0) << qsizetype(-1);
        QTest::newRow("a point at infinity") << two << QPointF(qInf(), 0) << qsizetype(-1);
        QTest::newRow("a NaN point") << two << QPointF(qQNaN(), 10) << qsizetype(-1);
    }

    void caretAt()
    {
        QFETCH(QList<QRectF>, boxes);
        QFETCH(QPointF, point);
        QFETCH(qsizetype, caret);
        QCOMPARE(pdfCaretAt(boxes, point), caret);
    }

    void rangeBetween_data()
    {
        QTest::addColumn<QPointF>("from");
        QTest::addColumn<QPointF>("to");
        QTest::addColumn<PdfTextRange>("range");

        QTest::newRow("one word") << QPointF(49, 110) << QPointF(200, 110) << PdfTextRange{0, 6};
        QTest::newRow("backwards") << QPointF(200, 110) << QPointF(49, 110) << PdfTextRange{0, 6};
        QTest::newRow("over the line break") << QPointF(71, 105) << QPointF(66, 140) << PdfTextRange{2, 7};
        QTest::newRow("the whole text") << QPointF(0, 0) << QPointF(999, 999) << PdfTextRange{0, 12};
        QTest::newRow("one point: nothing") << QPointF(71, 105) << QPointF(72, 106) << PdfTextRange{};
    }

    void rangeBetween()
    {
        QFETCH(QPointF, from);
        QFETCH(QPointF, to);
        QFETCH(PdfTextRange, range);
        QCOMPARE(pdfRangeBetween(twoLines(), from, to), range);
        QCOMPARE(pdfRangeBetween({}, from, to), PdfTextRange{});
    }

    void rectsOfASelection()
    {
        // A selection over the line break: one rectangle per line, the break's empty box ignored.
        const QList<QRectF> rects = pdfRangeRects(twoLines(), pdfRangeBetween(twoLines(), {71, 105}, {66, 140}));
        QCOMPARE(rects.size(), 2);
        QCOMPARE(rects.at(0), QRectF(70, 100, 40, 20));
        QCOMPARE(rects.at(1), QRectF(50, 130, 20, 20));
    }

    void wordAt_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<qsizetype>("index");
        QTest::addColumn<PdfTextRange>("word");

        const QString line = u"Zażółć gęślą jaźń"_s;
        QTest::newRow("first letter") << line << qsizetype(0) << PdfTextRange{0, 6};
        QTest::newRow("inside") << line << qsizetype(9) << PdfTextRange{7, 5};
        QTest::newRow("last letter") << line << qsizetype(16) << PdfTextRange{13, 4};
        QTest::newRow("a space alone") << line << qsizetype(6) << PdfTextRange{6, 1};
        QTest::newRow("hyphen inside a word") << u"biało-czerwony flaga"_s << qsizetype(2) << PdfTextRange{0, 14};
        QTest::newRow("on the hyphen") << u"biało-czerwony"_s << qsizetype(5) << PdfTextRange{0, 14};
        QTest::newRow("a dash between words") << u"a - b"_s << qsizetype(2) << PdfTextRange{2, 1};
        QTest::newRow("apostrophe") << u"rock'n'roll!"_s << qsizetype(0) << PdfTextRange{0, 11};
        QTest::newRow("digits") << u"strona 123."_s << qsizetype(8) << PdfTextRange{7, 3};
        QTest::newRow("combining mark") << u"ge\u0328s x"_s << qsizetype(0) << PdfTextRange{0, 4};
        QTest::newRow("emoji pair") << u"a😀b c"_s << qsizetype(1) << PdfTextRange{0, 4};
        QTest::newRow("outside") << line << qsizetype(17) << PdfTextRange{};
        QTest::newRow("negative") << line << qsizetype(-1) << PdfTextRange{};
        QTest::newRow("empty") << QString() << qsizetype(0) << PdfTextRange{};
    }

    void wordAt()
    {
        QFETCH(QString, text);
        QFETCH(qsizetype, index);
        QFETCH(PdfTextRange, word);
        QCOMPARE(pdfWordAt(text, index), word);
    }

    void copyText_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("copied");

        QTest::newRow("plain") << u"Zażółć gęślą jaźń"_s << u"Zażółć gęślą jaźń"_s;
        QTest::newRow("CR LF") << u"wykład\r\n3"_s << u"wykład\n3"_s;
        QTest::newRow("lone CR") << u"a\rb"_s << u"a\nb"_s;
        QTest::newRow("line separator") << u"a\u2028b"_s << u"a\nb"_s;
        QTest::newRow("tab kept") << u"a\tb"_s << u"a\tb"_s;
        QTest::newRow("soft hyphen") << u"wy\u00adkład"_s << u"wykład"_s;
        QTest::newRow("pdfium's hyphen marker") << u"wy\u0002\r\nkład"_s << u"wy\nkład"_s;
        QTest::newRow("controls") << (u"a"_s + QChar(0) + u"b\u0007c\u200bd"_s) << u"abcd"_s;
        QTest::newRow("lone surrogates") << (u"ż"_s + QChar(0xd800) + u"ó"_s + QChar(0xdc00)) << u"żó"_s;
        QTest::newRow("emoji kept") << u"😀 żółw"_s << u"😀 żółw"_s;
        QTest::newRow("trailing white space") << u"żółw \r\n\t "_s << u"żółw"_s;
        QTest::newRow("leading white space kept") << u"  żółw"_s << u"  żółw"_s;
        QTest::newRow("empty") << QString() << QString();
    }

    void copyText()
    {
        QFETCH(QString, text);
        QFETCH(QString, copied);
        QCOMPARE(pdfCopyText(text), copied);
    }

    void cleanTitle_data()
    {
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("cleaned");

        QTest::newRow("plain") << u"Rozdział 2 — żółw"_s << u"Rozdział 2 — żółw"_s;
        QTest::newRow("line breaks and tabs") << u"Rozdział\r\n2\t— żółw"_s << u"Rozdział 2 — żółw"_s;
        QTest::newRow("trimmed") << u"   Wstęp  "_s << u"Wstęp"_s;
        QTest::newRow("controls") << (u"Wst"_s + QChar(0) + u"ęp\u0007"_s) << u"Wstęp"_s;
        QTest::newRow("bidi override as a space") << u"a\u202eb"_s << u"a b"_s;
        QTest::newRow("lone surrogate") << (u"a"_s + QChar(0xdc00) + u"b"_s) << u"ab"_s;
        QTest::newRow("emoji kept") << u"😀 Wstęp"_s << u"😀 Wstęp"_s;
        QTest::newRow("only spaces") << u" \t\n"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("long") << QString(400, u'ż') << QString(MaxPdfTitleLength - 1, u'ż') + u'…';
        QString pairs;
        for (int i = 0; i < 200; ++i)
            pairs += u"😀"_s;
        QTest::newRow("long, pairs kept whole") << pairs << pairs.left(MaxPdfTitleLength - 2) + u'…';
    }

    void cleanTitle()
    {
        QFETCH(QString, title);
        QFETCH(QString, cleaned);
        QCOMPARE(pdfCleanTitle(title), cleaned);
        QVERIFY(pdfCleanTitle(title).size() <= MaxPdfTitleLength);
    }

    void linkUrl_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");

        QTest::newRow("https") << u"https://example.org/brulion?q=%C5%BC%C3%B3%C5%82w"_s << true;
        QTest::newRow("http") << u"http://example.org"_s << true;
        QTest::newRow("upper-case scheme") << u"HTTPS://example.org/"_s << true;
        QTest::newRow("mailto") << u"mailto:jan@example.org"_s << true;
        QTest::newRow("Polish host (IDN)") << u"https://żółw.pl/"_s << true;
        QTest::newRow("no host") << u"https:///etc/passwd"_s << false;
        QTest::newRow("mailto without address") << u"mailto:"_s << false;
        QTest::newRow("mailto without @") << u"mailto:jan"_s << false;
        QTest::newRow("javascript") << u"javascript:alert(1)"_s << false;
        QTest::newRow("JavaScript, mixed case") << u"JaVaScRiPt:alert(1)"_s << false;
        QTest::newRow("file") << u"file:///home/jan/.ssh/id_rsa"_s << false;
        QTest::newRow("data") << u"data:text/html,<script>alert(1)</script>"_s << false;
        QTest::newRow("app scheme") << u"brulion://open?path=x"_s << false;
        QTest::newRow("smb") << u"smb://server/share"_s << false;
        QTest::newRow("relative") << u"../tajne.pdf"_s << false;
        QTest::newRow("leading space") << u" https://example.org"_s << false;
        QTest::newRow("tab inside") << u"https://exa\tmple.org"_s << false;
        QTest::newRow("newline inside") << u"https://example.org/\njavascript:x"_s << false;
        QTest::newRow("NUL") << (u"https://example.org/"_s + QChar(0)) << false;
        QTest::newRow("bidi override") << u"https://example.org/\u202egpj.exe"_s << false;
        QTest::newRow("empty") << QString() << false;
        QTest::newRow("too long") << u"https://example.org/"_s + QString(MaxPdfUrlLength, u'a') << false;
    }

    void linkUrl()
    {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(pdfLinkUrlAllowed(url), allowed);
    }
};

QTEST_GUILESS_MAIN(TestPdfText)
#include "tst_pdftext.moc"
