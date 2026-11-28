// The outline read from a PDF's bytes (pdfoutline/pdfoutline.h): files built here object by object
// — a cross-reference table, one with wrong offsets, none, a cross-reference stream with its
// objects in an object stream —, destinations of every kind, titles in every encoding, and hostile
// outlines (loops, depth, many entries, encryption).
#include "pdfoutline/pdfoutline.h"

#include <QByteArray>
#include <QFile>
#include <QList>
#include <QTest>

#include <cstdlib>

using namespace Qt::StringLiterals;
using brulion::pdfoutline::Entry;
using brulion::pdfoutline::Limits;
using brulion::pdfoutline::readOutline;

namespace {

enum class Xref { Table, WrongOffsets, None, Stream };

/// How a cross-reference stream's PDF is broken on purpose ("" each: as it should be).
struct StreamFaults {
    QByteArray objStmLength; ///< the object stream's /Length as written
    QByteArray widths;       ///< the xref stream's /W
};

/// A PDF of `objects` (object n is objects[n-1]; a stream object holds its own "stream" part),
/// its trailer naming /Root 1 0 R (`trailerExtra` more of it).
QByteArray pdfOf(const QList<QByteArray> &objects, Xref xref = Xref::Table, const QByteArray &trailerExtra = {},
                 const StreamFaults &faults = {})
{
    QByteArray out = "%PDF-1.7\n%\xe2\xe3\xcf\xd3\n";
    QList<qsizetype> offsets;
    if (xref == Xref::Stream) {
        // Objects 1… in an object stream (number objects.size() + 1), the xref stream after it.
        const int objStm = int(objects.size()) + 1;
        const int xrefNum = objStm + 1;
        QByteArray header;
        QByteArray body;
        for (int i = 0; i < objects.size(); ++i) {
            header += QByteArray::number(i + 1) + ' ' + QByteArray::number(body.size()) + ' ';
            body += objects.at(i) + '\n';
        }
        const QByteArray data = header + body;
        const QByteArray packed = qCompress(data).mid(4); // a zlib stream (qCompress: 4 bytes of length first)
        const qsizetype objStmAt = out.size();
        out += QByteArray::number(objStm) + " 0 obj\n<< /Type /ObjStm /N " + QByteArray::number(objects.size())
               + " /First " + QByteArray::number(header.size()) + " /Filter /FlateDecode /Length "
               + (faults.objStmLength.isEmpty() ? QByteArray::number(packed.size()) : faults.objStmLength)
               + " >>\nstream\r\n" + packed + "\nendstream\nendobj\n";
        const qsizetype xrefAt = out.size();
        // Rows of /W [1 4 2]: type, field 2, field 3 — PNG-Up predicted (each row's bytes minus the row above).
        QList<QByteArray> rows;
        const auto row = [](int type, quint32 a, quint16 b) {
            QByteArray r(7, '\0');
            r[0] = char(type);
            for (int k = 0; k < 4; ++k)
                r[1 + k] = char((a >> (8 * (3 - k))) & 0xff);
            r[5] = char(b >> 8);
            r[6] = char(b & 0xff);
            return r;
        };
        rows.append(row(0, 0, 0xffff));
        for (int i = 0; i < objects.size(); ++i)
            rows.append(row(2, quint32(objStm), quint16(i)));
        rows.append(row(1, quint32(objStmAt), 0));
        rows.append(row(1, quint32(xrefAt), 0));
        // Each row with the next of PNG's five filters (None, Sub, Up, Average, Paeth).
        const auto paeth = [](int a, int b, int c) {
            const int p = a + b - c;
            const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
            return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
        };
        QByteArray predicted;
        QByteArray above(7, '\0');
        for (qsizetype n = 0; n < rows.size(); ++n) {
            const QByteArray &r = rows.at(n);
            const int filter = int(n % 5);
            predicted += char(filter);
            for (int k = 0; k < 7; ++k) {
                const int x = quint8(r[k]), left = k ? quint8(r[k - 1]) : 0, up = quint8(above[k]);
                const int upLeft = k ? quint8(above[k - 1]) : 0;
                const int by[] = {0, left, up, (left + up) / 2, paeth(left, up, upLeft)};
                predicted += char(x - by[filter]);
            }
            above = r;
        }
        const QByteArray xrefData = qCompress(predicted).mid(4);
        out += QByteArray::number(xrefNum) + " 0 obj\n<< /Type /XRef /Size " + QByteArray::number(xrefNum + 1)
               + " /W " + (faults.widths.isEmpty() ? QByteArray("[1 4 2]") : faults.widths) + " /Root 1 0 R " + trailerExtra
               + " /Filter /FlateDecode /DecodeParms << /Predictor 12 /Columns 7 >> /Length "
               + QByteArray::number(xrefData.size()) + " >>\nstream\n" + xrefData + "\nendstream\nendobj\n";
        out += "startxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";
        return out;
    }
    for (int i = 0; i < objects.size(); ++i) {
        offsets.append(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
    }
    if (xref == Xref::None) {
        out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R " + trailerExtra
               + " >>\n%%EOF\n";
        return out;
    }
    const qsizetype xrefAt = out.size();
    out += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (const qsizetype at : offsets) {
        const qsizetype written = xref == Xref::WrongOffsets ? at + 7 : at;
        out += QByteArray::number(written).rightJustified(10, '0') + " 00000 n \n";
    }
    out += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R " + trailerExtra
           + " >>\nstartxref\n" + QByteArray::number(xrefAt) + "\n%%EOF\n";
    return out;
}

/// Three A4 pages (842 pt high: objects 3, 4, 5), the outline at object 6, its entries from 7 on.
QList<QByteArray> withOutline(const QList<QByteArray> &entries, const QByteArray &catalogExtra = {})
{
    QList<QByteArray> objects{
            "<< /Type /Catalog /Pages 2 0 R /Outlines 6 0 R " + catalogExtra + " >>",
            "<< /Type /Pages /Kids [3 0 R 4 0 R 5 0 R] /Count 3 /MediaBox [0 0 595 842] >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Page /Parent 2 0 R >>",
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 842 595] >>",
            "<< /Type /Outlines /First 7 0 R /Last 7 0 R >>",
    };
    objects += entries;
    return objects;
}

QStringList lines(const QList<Entry> &entries)
{
    QStringList out;
    for (const Entry &e : entries)
        out.append(QString(e.level * 2, u' ') + e.title + u" → "_s + QString::number(e.page)
                   + (e.top >= 0 ? u" @"_s + QString::number(e.top, 'f', 2) : QString()));
    return out;
}

} // namespace

class TestPdfOutline : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void nestedOutline_data()
    {
        QTest::addColumn<int>("xref");
        QTest::newRow("a cross-reference table") << int(Xref::Table);
        QTest::newRow("its offsets wrong") << int(Xref::WrongOffsets);
        QTest::newRow("no table at all") << int(Xref::None);
        QTest::newRow("a cross-reference stream, objects in a stream") << int(Xref::Stream);
    }
    void nestedOutline()
    {
        // Two chapters, the first with two sections; destinations of each kind; Polish titles.
        QFETCH(int, xref);
        const QList<QByteArray> objects = withOutline(
                {"<< /Title (Rozdzia\\233 1) /Parent 6 0 R /Next 10 0 R /First 8 0 R /Last 9 0 R /Count 2 "
                 "/Dest [3 0 R /XYZ 0 842 0] >>",
                 "<< /Title <FEFF017C00F3014200770020> /Parent 7 0 R /Next 9 0 R /Dest [4 0 R /XYZ 72 421 null] >>",
                 "<< /Title (Sekcja \\(2\\)) /Parent 7 0 R /Prev 8 0 R /A << /S /GoTo /D [4 0 R /FitH 210.5] >> >>",
                 "<< /Title (Rozdzia\\233 2) /Parent 6 0 R /Prev 7 0 R /Dest [5 0 R /Fit] >>"});
        QList<QByteArray> fixed = objects;
        fixed[5] = "<< /Type /Outlines /First 7 0 R /Last 10 0 R /Count 2 >>";
        const QList<Entry> got = readOutline(pdfOf(fixed, Xref(xref)));
        QCOMPARE(lines(got), (QStringList{u"Rozdział 1 → 1 @0.00"_s, u"  żółw  → 2 @0.50"_s, u"  Sekcja (2) → 2 @0.75"_s,
                                          u"Rozdział 2 → 3"_s}));
    }

    void destinations()
    {
        // (A name's #xx is its byte; "#2" without two hex digits is the sign itself.)
        // A named destination in the catalog's /Dests, one in the /Names tree (with /Kids and
        // /Limits), a page number instead of a page, /FitR's top, a link to another file, none.
        QList<QByteArray> objects{
                "<< /Type /Catalog /Pages 2 0 R /Outlines 5 0 R /Dests 12 0 R /Names << /Dests 13 0 R >> >>",
                "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 600 800] >>",
                "<< /Type /Page /Parent 2 0 R >>",
                "<< /Type /Page /Parent 2 0 R >>",
                "<< /Type /Outlines /First 6 0 R /Last 11 0 R >>",
                "<< /Title (Stara nazwa) /Next 7 0 R /Dest /wstep >>",
                "<< /Title (Z drzewa) /Next 8 0 R /Dest (koniec) >>",
                "<< /Title (Numer strony) /Next 9 0 R /Dest [1 /FitR 0 0 100 600] >>",
                "<< /Title (Inny plik) /Next 10 0 R /A << /S /GoToR /F (inny.pdf) /D [0 /Fit] >> >>",
                "<< /Title (Nieznana nazwa) /Next 11 0 R /Dest (nie-ma) >>",
                "<< /Title (Grupa) /Next 15 0 R >>",
                "<< /wstep << /D [3 0 R /XYZ 0 400 0] >> >>",
                "<< /Kids [14 0 R] >>",
                "<< /Limits [(a) (z)] /Names [(alfa) [3 0 R /Fit] (koniec) [4 0 R /FitBH 200]] >>",
                "<< /Title (Nazwa z #) /Dest /na#C5#BCwa#2 >>"};
        objects[11] = "<< /wstep << /D [3 0 R /XYZ 0 400 0] >> /na\xC5\xBCwa#2 [4 0 R /Fit] >>";
        QCOMPARE(lines(readOutline(pdfOf(objects))),
                 (QStringList{u"Stara nazwa → 1 @0.50"_s, u"Z drzewa → 2 @0.75"_s, u"Numer strony → 2 @0.25"_s,
                              u"Inny plik → 0"_s, u"Nieznana nazwa → 0"_s, u"Grupa → 0"_s, u"Nazwa z # → 2"_s}));
    }

    void titleEncodings()
    {
        const QList<QByteArray> objects = withOutline(
                {"<< /Title <EFBBBF5AC5BCC3B3C582C487> /Next 8 0 R >>",           // UTF-8 with its BOM
                 "<< /Title (\\225\\233\\204\\240 \\(2\\)\\\\) /Next 9 0 R >>",   // PDFDocEncoding: Ł ł — € (2) and a backslash
                 "<< /Title (dwie\\\nlinie\\r\\noraz \\164ekst \\\r\ni dalej) >>"}); // continued lines, \r\n, octal
        QList<QByteArray> fixed = objects;
        fixed[5] = "<< /Type /Outlines /First 7 0 R /Last 9 0 R >>";
        const QList<Entry> got = readOutline(pdfOf(fixed));
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0).title, u"Zżółć"_s);
        QCOMPARE(got.at(1).title, u"Łł—€ (2)\\"_s);
        QCOMPARE(got.at(2).title, u"dwielinie\r\noraz tekst i dalej"_s);
        // UTF-16BE to the last code unit: a surrogate pair, an odd last byte dropped, a BOM alone.
        QList<QByteArray> sixteen = withOutline({"<< /Title <FEFFD83DDE00017C> /Next 8 0 R >>",
                                                 "<< /Title (\\376\\377\\001\\101\\000) /Next 9 0 R >>", "<< /Title <FEFF> >>"});
        sixteen[5] = "<< /Type /Outlines /First 7 0 R >>";
        const QList<Entry> wide = readOutline(pdfOf(sixteen));
        QCOMPARE(wide.size(), 3);
        QCOMPARE(wide.at(0).title, u"😀ż"_s);
        QCOMPARE(wide.at(1).title, u"Ł"_s); // (FE FF 01 41 00: the odd last byte dropped)
        QCOMPARE(wide.at(2).title, QString());
    }

    void hostile_data()
    {
        QTest::addColumn<QList<QByteArray>>("entries");
        QTest::addColumn<int>("expected");
        // Two entries each other's /Next, the first its own /First child (tst_pdfpages' looping outline).
        QTest::newRow("a loop") << QList<QByteArray>{"<< /Title (Raz) /Next 8 0 R /First 7 0 R /Dest [3 0 R /Fit] >>",
                                                     "<< /Title (Dwa) /Next 7 0 R /Dest [3 0 R /Fit] >>"}
                                << 2;
        QTest::newRow("a child that is its parent's sibling")
                << QList<QByteArray>{"<< /Title (A) /First 8 0 R /Next 8 0 R >>", "<< /Title (B) /Next 7 0 R >>"} << 2;
        QTest::newRow("a missing object") << QList<QByteArray>{"<< /Title (A) /Next 99 0 R /First 98 0 R >>"} << 1;
        QTest::newRow("no title, a number for a title")
                << QList<QByteArray>{"<< /Next 8 0 R >>", "<< /Title 5 >>"} << 2;
        QTest::newRow("not a dictionary") << QList<QByteArray>{"[1 2 3]"} << 0;
    }
    void hostile()
    {
        QFETCH(QList<QByteArray>, entries);
        QFETCH(int, expected);
        QCOMPARE(readOutline(pdfOf(withOutline(entries))).size(), expected);
    }

    void deepAndLong()
    {
        // 40 levels deep: at most maxDepth; 6000 siblings: at most maxItems.
        QList<QByteArray> deep;
        for (int i = 0; i < 40; ++i)
            deep.append("<< /Title (Poziom " + QByteArray::number(i) + ") /First " + QByteArray::number(8 + i)
                        + " 0 R /Dest [3 0 R /Fit] >>");
        const QList<Entry> d = readOutline(pdfOf(withOutline(deep)));
        QCOMPARE(d.size(), 32);
        QCOMPARE(d.last().level, 31);
        QList<QByteArray> many;
        for (int i = 0; i < 6000; ++i)
            many.append("<< /Title (Wpis) /Next " + QByteArray::number(8 + i) + " 0 R >>");
        QCOMPARE(readOutline(pdfOf(withOutline(many))).size(), 5000);
        Limits small;
        small.maxItems = 10;
        small.maxDepth = 3;
        QCOMPARE(readOutline(pdfOf(withOutline(many)), small).size(), 10);
        QCOMPARE(readOutline(pdfOf(withOutline(deep)), small).size(), 3);
    }

    void aRealFile()
    {
        // tests/data/pdf/spis.pdf, written by Qt's own PDF writer and poppler-checked
        // (tst_pdfpages outline): the same entries, levels, pages and places.
        QFile f(QStringLiteral(BRULION_TEST_DATA) + u"/pdf/spis.pdf"_s);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QList<Entry> got = readOutline(f.readAll());
        QCOMPARE(got.size(), 5);
        const QStringList titles{u"Wstęp"_s, u"Rozdział 2 — żółw"_s, u"Podrozdział 2.1"_s, u"Rozdział 3"_s, u"Bez celu"_s};
        const QList<int> levels{0, 0, 1, 0, 0};
        const QList<int> pages{1, 2, 2, 3, 0};
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(got.at(i).title, titles.at(i));
            QCOMPARE(got.at(i).level, levels.at(i));
            QCOMPARE(got.at(i).page, pages.at(i));
        }
        QVERIFY(got.at(0).top < 0.01);
        QVERIFY2(qAbs(got.at(1).top - 0.5) < 0.02, qPrintable(QString::number(got.at(1).top)));
        QVERIFY(got.at(2).top < 0.01);
        QVERIFY2(qAbs(got.at(3).top - 142.0 / 842) < 0.02, qPrintable(QString::number(got.at(3).top)));
        QCOMPARE(got.at(4).top, -1.0);
    }

    void brokenStreams_data()
    {
        QTest::addColumn<QByteArray>("length");
        QTest::addColumn<QByteArray>("widths");
        QTest::addColumn<int>("expected");
        QTest::newRow("as it should be") << QByteArray() << QByteArray() << 4;
        // Its /Length an object inside the very stream: no endless loop, "endstream" found instead.
        QTest::newRow("its length inside itself") << QByteArray("2 0 R") << QByteArray() << 4;
        QTest::newRow("its length too long") << QByteArray("999999") << QByteArray() << 4;
        QTest::newRow("its length a word") << QByteArray("/Duzo") << QByteArray() << 4;
        // Rows it cannot read: the objects are only inside the object stream, so none.
        QTest::newRow("two widths") << QByteArray() << QByteArray("[1 4]") << 0;
        QTest::newRow("no widths") << QByteArray() << QByteArray("[0 0 0]") << 0;
    }
    void brokenStreams()
    {
        QFETCH(QByteArray, length);
        QFETCH(QByteArray, widths);
        QFETCH(int, expected);
        QList<QByteArray> objects = withOutline(
                {"<< /Title (A) /Next 8 0 R /First 10 0 R >>", "<< /Title (B) /Next 9 0 R >>", "<< /Title (C) >>",
                 "<< /Title (A.1) >>"});
        objects[5] = "<< /Type /Outlines /First 7 0 R >>";
        QCOMPARE(readOutline(pdfOf(objects, Xref::Stream, {}, {length, widths})).size(), expected);
    }

    void destinationEdges()
    {
        // Pages without any /MediaBox (no top then), a page number past the last or below the
        // first, destinations too short for their top, a /Names array of an odd length.
        const QList<QByteArray> objects{
                "<< /Type /Catalog /Pages 2 0 R /Outlines 4 0 R /Names << /Dests 12 0 R >> >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R >>",
                "<< /Type /Outlines /First 5 0 R >>",
                "<< /Title (Bez rozmiaru) /Next 6 0 R /Dest [3 0 R /XYZ 0 400 0] >>",
                "<< /Title (Za daleko) /Next 7 0 R /Dest [1 /Fit] >>",
                "<< /Title (Ujemna) /Next 8 0 R /Dest [-1 /Fit] >>",
                "<< /Title (Krotka) /Next 9 0 R /Dest [3 0 R /XYZ 0] >>",
                "<< /Title (Sama strona) /Next 10 0 R /Dest [3 0 R] >>",
                "<< /Title (Nieparzyste) /Next 11 0 R /Dest (b) >>",
                "<< /Title (\\200\\177\\240\\241) /Dest [0 /FitR 0 0 1] >>",
                "<< /Names [(a) [3 0 R /Fit] (b)] >>"};
        QCOMPARE(lines(readOutline(pdfOf(objects))),
                 (QStringList{u"Bez rozmiaru → 1"_s, u"Za daleko → 0"_s, u"Ujemna → 0"_s, u"Krotka → 1"_s,
                              u"Sama strona → 1"_s, u"Nieparzyste → 0"_s, u"•\u007F€¡ → 1"_s}));
    }

    void damagedFiles()
    {
        // Every prefix of a file, every byte of it changed, trailing junk past startxref's window:
        // never a crash, never more entries than the file has.
        QList<QByteArray> objects = withOutline({"<< /Title (Raz) /Next 8 0 R /Dest [3 0 R /XYZ 0 421 0] >>",
                                                 "<< /Title (Dwa \\\r3) /First 9 0 R >>", "<< /Title <FEFF0141> >>"});
        objects[5] = "<< /Type /Outlines /First 7 0 R >>";
        for (const Xref kind : {Xref::Table, Xref::Stream}) {
            const QByteArray whole = pdfOf(objects, kind);
            QCOMPARE(readOutline(whole).size(), 3);
            for (qsizetype n = 0; n < whole.size(); n += 3)
                QVERIFY(readOutline(whole.left(n)).size() <= 3);
            for (qsizetype at = 0; at < whole.size(); at += 2) {
                for (const char c : {'(', ')', '<', '>', '[', '\\', '/', '9', ' '}) {
                    QByteArray changed = whole;
                    changed[at] = c;
                    QVERIFY(readOutline(changed).size() <= 3);
                }
            }
            QCOMPARE(readOutline(QByteArray(whole + QByteArray(2000, ' '))).size(), kind == Xref::Table ? 3 : 0);
        }
        QCOMPARE(readOutline(pdfOf(objects)).at(1).title, u"Dwa 3"_s); // (a line continued by CR alone)
        // A dictionary closed by a single '>': not one.
        QVERIFY(readOutline(pdfOf(withOutline({"<< /Title (A) /Dest [3 0 R /Fit] > >>"}))).isEmpty());
    }

    void noOutline()
    {
        // No /Outlines, an encrypted file, garbage, an empty file, arrays nested past any sense.
        const QList<QByteArray> plain{"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [] /Count 0 >>"};
        QVERIFY(readOutline(pdfOf(plain)).isEmpty());
        const QList<QByteArray> objects = withOutline({"<< /Title (Tajne) >>"});
        QCOMPARE(readOutline(pdfOf(objects)).size(), 1);
        QVERIFY(readOutline(pdfOf(objects, Xref::Table, "/Encrypt 99 0 R")).isEmpty());
        QVERIFY(readOutline("to nie jest PDF").isEmpty());
        QVERIFY(readOutline(QByteArrayView()).isEmpty());
        QVERIFY(readOutline(pdfOf(withOutline({"<< /Title (A) /Dest " + QByteArray(100'000, '[') + " >>"}))).size() <= 1);
        QVERIFY(readOutline(pdfOf(withOutline({"<< /Title (Niedokończony"}))).size() <= 1);
    }
};

QTEST_GUILESS_MAIN(TestPdfOutline)
#include "tst_pdfoutline.moc"
