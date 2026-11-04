// One Markdown table as CSV and back (io/tablecsv.h): separators, quotes, CRLF, BOM, Polish text,
// formulas as results, escaping, bounds and malformed input.
#include "calc/table.h"
#include "io/csv.h"
#include "io/tablecsv.h"

#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

class TestIoTableCsv : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void separator_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QChar>("separator");
        QTest::newRow("comma") << u"a,b,c\n1,2,3\n"_s << QChar(u',');
        QTest::newRow("semicolon with decimal commas") << u"Nazwa;Cena\nPłytki;12,50\nWałek;3,00\n"_s << QChar(u';');
        QTest::newRow("tab") << u"a\tb\n1\t2\n"_s << QChar(u'\t');
        QTest::newRow("quoted separators") << u"\"a;b\",c\n\"x;y\",z\n"_s << QChar(u',');
        QTest::newRow("excel sep line") << u"sep=;\r\na,b;c\r\n"_s << QChar(u';');
        QTest::newRow("excel sep after a BOM") << u"\uFEFFsep=\t\na\tb\n"_s << QChar(u'\t');
        QTest::newRow("one column") << u"tylko\njedna\n"_s << QChar(u',');
        QTest::newRow("empty") << QString() << QChar(u',');
    }
    void separator()
    {
        QFETCH(QString, text);
        QFETCH(QChar, separator);
        QCOMPARE(detectCsvSeparator(text), separator);
    }

    void import_data()
    {
        QTest::addColumn<QString>("csv");
        QTest::addColumn<QStringList>("header");
        QTest::addColumn<QList<QStringList>>("rows");
        QTest::newRow("comma, LF") << u"Imię,Miasto\nŻaneta,Łódź\nĆwiek,Gdańsk\n"_s
                                   << QStringList{u"Imię"_s, u"Miasto"_s}
                                   << QList<QStringList>{{u"Żaneta"_s, u"Łódź"_s}, {u"Ćwiek"_s, u"Gdańsk"_s}};
        QTest::newRow("semicolon, CRLF, BOM")
                << u"\uFEFFPozycja;Cena\r\nPłytki;12,50 zł\r\n"_s << QStringList{u"Pozycja"_s, u"Cena"_s}
                << QList<QStringList>{{u"Płytki"_s, u"12,50 zł"_s}};
        QTest::newRow("quotes and line breaks")
                << u"a,b\n\"x, \"\"y\"\"\",\"linia 1\nlinia 2\"\n"_s << QStringList{u"a"_s, u"b"_s}
                << QList<QStringList>{{u"x, \"y\""_s, u"linia 1\nlinia 2"_s}};
        QTest::newRow("ragged rows padded")
                << u"a,b,c\n1\n1,2,3,4\n"_s << QStringList{u"a"_s, u"b"_s, u"c"_s, QString()}
                << QList<QStringList>{{u"1"_s, QString(), QString(), QString()}, {u"1"_s, u"2"_s, u"3"_s, u"4"_s}};
        QTest::newRow("formulas stay text") << u"a,b\n=1+2,=HYPERLINK(\"x\")\n"_s << QStringList{u"a"_s, u"b"_s}
                                            << QList<QStringList>{{u"\\=1+2"_s, u"\\=HYPERLINK(\"x\")"_s}};
        QTest::newRow("blank records skipped")
                << u"a;b\n\n;;\n1;2\n"_s << QStringList{u"a"_s, u"b"_s} << QList<QStringList>{{u"1"_s, u"2"_s}};
        QTest::newRow("only a header") << u"a,b\n"_s << QStringList{u"a"_s, u"b"_s} << QList<QStringList>{};
        QTest::newRow("CR only") << u"a,b\r1,2\r"_s << QStringList{u"a"_s, u"b"_s}
                                 << QList<QStringList>{{u"1"_s, u"2"_s}};
        QTest::newRow("unclosed quote") << u"a,b\n\"x,y\n"_s << QStringList{u"a"_s, u"b"_s}
                                        << QList<QStringList>{{u"x,y"_s, QString()}};
    }
    void import()
    {
        QFETCH(QString, csv);
        QFETCH(QStringList, header);
        QFETCH(QList<QStringList>, rows);
        const std::optional<CsvTable> t = tableFromCsv(csv);
        QVERIFY(t);
        QCOMPARE(t->model.header, header);
        QCOMPARE(t->model.rows, rows);
        QCOMPARE(t->model.aligns.size(), header.size());
        QVERIFY(!t->truncated);
        // Written as a table and read back: the same cells (pipes and line breaks escaped).
        const auto back = calc::parseTable(calc::serializeTable(t->model));
        QVERIFY(back);
        QCOMPARE(back->header, header);
    }

    void pipesAndBreaksEscaped()
    {
        const auto t = tableFromCsv(u"a|b,c\n\"x|y\",\"1\n2\"\n"_s);
        QVERIFY(t);
        const QString md = calc::serializeTable(t->model);
        QVERIFY(md.contains(u"a\\|b"_s));
        QVERIFY(md.contains(u"x\\|y"_s));
        QVERIFY(md.contains(u"1<br>2"_s));
        QCOMPARE(md.count(u'\n'), 2); // header, delimiter, one row
        const auto back = calc::parseTable(md);
        QCOMPARE(back->rows.value(0).value(0), u"x|y"_s);
    }

    void nothing_data()
    {
        QTest::addColumn<QString>("csv");
        QTest::newRow("empty") << QString();
        QTest::newRow("BOM only") << u"\uFEFF"_s;
        QTest::newRow("blank lines") << u"\n\r\n  \n"_s;
        QTest::newRow("separators only") << u",,,\n , ,\n"_s;
        QTest::newRow("sep line only") << u"sep=;"_s;
    }
    void nothing()
    {
        QFETCH(QString, csv);
        QVERIFY(!tableFromCsv(csv));
    }

    void bounds()
    {
        // Too many rows and columns, a too long cell: cut and reported.
        QString csv;
        for (int c = 0; c < MaxCsvColumns + 5; ++c)
            csv += (c ? u","_s : QString()) + u"k"_s + QString::number(c);
        csv += u'\n';
        for (int r = 0; r < MaxCsvRows + 10; ++r)
            csv += QString::number(r) + u'\n';
        csv += QString(MaxCsvCellLength + 10, u'x') + u'\n';
        const auto t = tableFromCsv(csv);
        QVERIFY(t);
        QVERIFY(t->truncated);
        QCOMPARE(t->model.header.size(), MaxCsvColumns);
        QCOMPARE(t->model.rows.size(), MaxCsvRows);
        const auto longCell = tableFromCsv(QString(u"a\n"_s + QString(MaxCsvCellLength + 10, u'ż')));
        QVERIFY(longCell && longCell->truncated);
        QCOMPARE(longCell->model.rows.value(0).value(0).size(), MaxCsvCellLength);
        QVERIFY(longCell->model.rows.value(0).value(0).endsWith(u'…'));
        // Far too large: not read at all.
        QVERIFY(!tableFromCsv(QString(MaxCsvTextLength + 1, u'a')));
    }

    void exportTable()
    {
        const auto m = calc::parseTable(u"| Pozycja | Cena | Ilość | Razem |\n|---|--:|---|---|\n"
                                        u"| Żółta farba | 12,50 zł | 2 | =[Cena]*[Ilość] |\n"
                                        u"| A \\| B | =nieznana_zmienna | | linia<br>druga |\n"
                                        u"| =x | -5 | +48 600 | @kto |"_s);
        QVERIFY(m);
        const calc::TableEval eval = calc::evaluateTable(*m);
        const QString csv = tableToCsv(*m, eval);
        const QList<QStringList> rows = parseCsv(csv);
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows.at(0), (QStringList{u"Pozycja"_s, u"Cena"_s, u"Ilość"_s, u"Razem"_s}));
        QCOMPARE(rows.at(1), (QStringList{u"Żółta farba"_s, u"12,50 zł"_s, u"2"_s, u"25 zł"_s}));
        QCOMPARE(rows.at(2), (QStringList{u"A | B"_s, u"#ERR"_s, QString(), u"linia\ndruga"_s}));
        // Spreadsheet formulas neutralised; numbers kept.
        QCOMPARE(rows.at(3).value(1), u"-5"_s);
        QCOMPARE(rows.at(3).value(2), u"'+48 600"_s);
        QCOMPARE(rows.at(3).value(3), u"'@kto"_s);
        QVERIFY(csv.endsWith(u"\r\n"_s));
        QVERIFY(csv.startsWith(u"Pozycja,Cena,"_s));
        // With a semicolon.
        QVERIFY(tableToCsv(*m, eval, u';').startsWith(u"Pozycja;Cena;"_s));
    }

    void roundTrip()
    {
        const auto m = calc::parseTable(u"| Imię | Uwagi |\n|---|---|\n| Łucja | \"cytat\", przecinek; średnik |\n"
                                        u"| Źdźbło | a \\| b |"_s);
        QVERIFY(m);
        const QString csv = tableToCsv(*m, calc::evaluateTable(*m));
        const auto back = tableFromCsv(csv);
        QVERIFY(back);
        QCOMPARE(back->separator, QChar(u','));
        QCOMPARE(back->model.header, m->header);
        QCOMPARE(back->model.rows, m->rows);
    }
};

QTEST_MAIN(TestIoTableCsv)
#include "tst_io_tablecsv.moc"
