// A rectangle of a table's cells (io/tablecells.h): copied as TSV and as Markdown, pasted from TSV,
// CSV, Markdown and lines, cleared — Polish text, formulas as results, quoting, line breaks,
// bounds and malformed input.
#include "calc/table.h"
#include "io/tablecells.h"
#include "io/tablecsv.h"

#include <QTest>

using namespace brulion;
using namespace brulion::io;
using namespace Qt::StringLiterals;

namespace {

calc::TableModel sample()
{
    const auto model = calc::parseTable(u"| Pozycja | Ilość | Cena | Razem |\n"
                                        "| :--- | ---: | ---: | ---: |\n"
                                        "| Płytki | 2 | 38,5 | =[Ilość]*[Cena] |\n"
                                        "| Żółć <br> druga | 4 | 45 | =[Ilość]*[Cena] |\n"
                                        "| Wałek \\| duży | 1 | 12 | =[Ilość]*[Cena] |"_s);
    return *model;
}

calc::TableEval evaluated(const calc::TableModel &model)
{
    return calc::evaluateTable(model, calc::CalcLocale::Polish);
}

TableCells cells(QList<int> rows, int first, int last)
{
    return TableCells{rows, first, last};
}

} // namespace

class TestIoTableCells : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void tsv_data()
    {
        QTest::addColumn<QList<int>>("rows");
        QTest::addColumn<int>("first");
        QTest::addColumn<int>("last");
        QTest::addColumn<QString>("tsv");
        QTest::newRow("one cell is its text") << QList<int>{0} << 0 << 0 << u"Płytki"_s;
        QTest::newRow("one formula cell is its result") << QList<int>{0} << 3 << 3 << u"77"_s;
        QTest::newRow("a block") << QList<int>{0, 2} << 1 << 2 << u"2\t38,5\n1\t12"_s;
        QTest::newRow("with the header") << QList<int>{-1, 0} << 0 << 1 << u"Pozycja\tIlość\nPłytki\t2"_s;
        QTest::newRow("a line break is quoted") << QList<int>{1} << 0 << 1 << u"\"Żółć \n druga\"\t4"_s;
        QTest::newRow("a pipe is unescaped") << QList<int>{2} << 0 << 0 << u"Wałek | duży"_s;
        QTest::newRow("rows out of range left out") << QList<int>{0, 9} << 0 << 1 << u"Płytki\t2"_s;
        QTest::newRow("columns clamped") << QList<int>{0} << 2 << 9 << u"38,5\t77"_s;
        QTest::newRow("no rows") << QList<int>{} << 0 << 1 << QString();
        QTest::newRow("a reversed range names nothing") << QList<int>{0} << 2 << 1 << QString();
    }
    void tsv()
    {
        QFETCH(QList<int>, rows);
        QFETCH(int, first);
        QFETCH(int, last);
        QFETCH(QString, tsv);
        const calc::TableModel model = sample();
        QCOMPARE(tableCellsToTsv(model, evaluated(model), cells(rows, first, last)), tsv);
    }
    void tsvQuotesTabsAndQuotes()
    {
        calc::TableModel model = sample();
        model.rows[0][0] = u"a\tb \"c\""_s;
        QCOMPARE(tableCellsToTsv(model, evaluated(model), cells({0}, 0, 1)), u"\"a\tb \"\"c\"\"\"\t2"_s);
        // …and it reads back as the same cells.
        const PastedCells back = parsePastedCells(tableCellsToTsv(model, evaluated(model), cells({0}, 0, 1)));
        QCOMPARE(back.grid, (QList<QStringList>{{u"a\tb \"c\""_s, u"2"_s}}));
    }

    void markdown_data()
    {
        QTest::addColumn<QList<int>>("rows");
        QTest::addColumn<int>("first");
        QTest::addColumn<int>("last");
        QTest::addColumn<QString>("markdown");
        QTest::newRow("a block names its columns")
                << QList<int>{0, 2} << 1 << 3
                << u"| Ilość | Cena | Razem |\n| ----: | ---: | ----: |\n|     2 | 38,5 |    77 |\n|     1 |   12 |    12 |"_s;
        QTest::newRow("the header row adds nothing but itself")
                << QList<int>{-1, 0} << 0 << 1 << u"| Pozycja | Ilość |\n| :------ | ----: |\n| Płytki  |     2 |"_s;
        QTest::newRow("only the header") << QList<int>{-1} << 0 << 0 << u"| Pozycja |\n| :------ |"_s;
        QTest::newRow("a pipe and a line break stay valid")
                << QList<int>{1, 2} << 0 << 0 << u"| Pozycja         |\n| :-------------- |\n| Żółć <br> druga |\n| Wałek \\| duży   |"_s;
        QTest::newRow("nothing") << QList<int>{} << 0 << 0 << QString();
    }
    void markdown()
    {
        QFETCH(QList<int>, rows);
        QFETCH(int, first);
        QFETCH(int, last);
        QFETCH(QString, markdown);
        const calc::TableModel model = sample();
        const QString out = tableCellsToMarkdown(model, evaluated(model), cells(rows, first, last));
        QCOMPARE(out, markdown);
        if (!out.isEmpty())
            QVERIFY(calc::parseTable(out).has_value());
    }

    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QList<QStringList>>("grid");
        QTest::newRow("one value") << u"  Żółć  "_s << QList<QStringList>{{u"Żółć"_s}};
        QTest::newRow("a single formula stays a formula") << u"=sum(above)"_s << QList<QStringList>{{u"=sum(above)"_s}};
        QTest::newRow("empty") << QString() << QList<QStringList>{};
        QTest::newRow("TSV, LF") << u"a\tb\nżó\tłć\n"_s << QList<QStringList>{{u"a"_s, u"b"_s}, {u"żó"_s, u"łć"_s}};
        QTest::newRow("TSV, CRLF, a BOM") << u"\uFEFFa\tb\r\nc\td\r\n"_s << QList<QStringList>{{u"a"_s, u"b"_s}, {u"c"_s, u"d"_s}};
        QTest::newRow("TSV with empty cells") << u"a\t\tc\n\t\t\n"_s << QList<QStringList>{{u"a"_s, QString(), u"c"_s}, {QString(), QString(), QString()}};
        QTest::newRow("TSV quoted line break") << u"\"linia 1\nlinia 2\"\tb\n"_s << QList<QStringList>{{u"linia 1\nlinia 2"_s, u"b"_s}};
        QTest::newRow("TSV formulas stay text") << u"=1+2\t=HYPERLINK(\"x\")\n"_s << QList<QStringList>{{u"\\=1+2"_s, u"\\=HYPERLINK(\"x\")"_s}};
        QTest::newRow("CSV with commas") << u"a,b\nc,d\n"_s << QList<QStringList>{{u"a"_s, u"b"_s}, {u"c"_s, u"d"_s}};
        QTest::newRow("CSV with semicolons and decimal commas") << u"Płytki;12,50\nWałek;3,00\n"_s
                                                                 << QList<QStringList>{{u"Płytki"_s, u"12,50"_s}, {u"Wałek"_s, u"3,00"_s}};
        QTest::newRow("lines are rows of one column") << u"jeden\ndwa\ntrzy"_s << QList<QStringList>{{u"jeden"_s}, {u"dwa"_s}, {u"trzy"_s}};
        QTest::newRow("a Markdown table, its delimiter row left out")
                << u"| Imię | Miasto |\n| --- | :-: |\n| Żaneta | Łódź |\n| Ćwiek | Gdańsk \\| port |\n"_s
                << QList<QStringList>{{u"Imię"_s, u"Miasto"_s}, {u"Żaneta"_s, u"Łódź"_s}, {u"Ćwiek"_s, u"Gdańsk | port"_s}};
        QTest::newRow("a pipe in prose is no table") << u"a | b\nc | d"_s << QList<QStringList>{{u"a | b"_s}, {u"c | d"_s}};
        QTest::newRow("unclosed quote is read leniently") << u"a\t\"b\nc\td\n"_s << QList<QStringList>{{u"a"_s, u"b\nc\td"_s}};
    }
    void parse()
    {
        QFETCH(QString, text);
        QFETCH(QList<QStringList>, grid);
        const PastedCells cells = parsePastedCells(text);
        QCOMPARE(cells.grid, grid);
        QVERIFY(!cells.truncated);
    }
    void parseBounds()
    {
        QString rows;
        for (int i = 0; i < MaxCsvRows + 50; ++i)
            rows += u"x\ty\n"_s;
        PastedCells cells = parsePastedCells(rows);
        QCOMPARE(cells.grid.size(), MaxCsvRows + 1);
        QVERIFY(cells.truncated);

        QStringList wide;
        for (int i = 0; i < MaxCsvColumns + 5; ++i)
            wide.append(u"c"_s);
        cells = parsePastedCells(wide.join(u'\t'));
        QCOMPARE(cells.grid.first().size(), MaxCsvColumns);
        QVERIFY(cells.truncated);

        cells = parsePastedCells(QString(QString(MaxCsvCellLength + 20, u'ż') + u"\tb"_s));
        QCOMPARE(cells.grid.first().first().size(), MaxCsvCellLength);
        QVERIFY(cells.grid.first().first().endsWith(u'…'));
        QVERIFY(cells.truncated);

        QVERIFY(parsePastedCells(QString(MaxCsvTextLength + 1, u'a')).grid.isEmpty());
    }

    void paste_data()
    {
        QTest::addColumn<QList<int>>("rows");
        QTest::addColumn<int>("column");
        QTest::addColumn<int>("selectedRows");
        QTest::addColumn<int>("selectedColumns");
        QTest::addColumn<QList<QStringList>>("grid");
        QTest::addColumn<int>("cellsChanged");
        QTest::addColumn<int>("rowsAdded");
        QTest::addColumn<int>("columnsCut");
        QTest::addColumn<QString>("after"); // the table's TSV after the paste, from the header down
        const QList<int> all{-1, 0, 1, 2};
        QTest::newRow("a block inside")
                << QList<int>{0, 1, 2} << 1 << 1 << 1 << QList<QStringList>{{u"7"_s, u"8"_s}, {u"9"_s, u"10"_s}} << 4 << 0 << 0
                << u"Pozycja\tIlość\tCena\tRazem\nPłytki\t7\t8\t56\n\"Żółć \n druga\"\t9\t10\t90\nWałek | duży\t1\t12\t12"_s;
        QTest::newRow("from a body cell down, the rest appended")
                << QList<int>{1, 2} << 0 << 1 << 1 << QList<QStringList>{{u"a"_s}, {u"b"_s}, {u"c"_s}, {u"ć"_s}} << 4 << 2 << 0
                << u"Pozycja\tIlość\tCena\tRazem\nPłytki\t2\t38,5\t77\na\t4\t45\t180\nb\t1\t12\t12\nc\t\t\t\nć\t\t\t"_s;
        QTest::newRow("columns past the last are left out")
                << QList<int>{0, 1} << 2 << 1 << 1 << QList<QStringList>{{u"x"_s, u"y"_s, u"z"_s}} << 2 << 0 << 1
                << u"Pozycja\tIlość\tCena\tRazem\nPłytki\t2\tx\ty\n\"Żółć \n druga\"\t4\t45\t180\nWałek | duży\t1\t12\t12"_s;
        QTest::newRow("one value fills the selection")
                << QList<int>{0, 1, 2} << 1 << 2 << 2 << QList<QStringList>{{u"0"_s}} << 4 << 0 << 0
                << u"Pozycja\tIlość\tCena\tRazem\nPłytki\t0\t0\t0\n\"Żółć \n druga\"\t0\t0\t0\nWałek | duży\t1\t12\t12"_s;
        QTest::newRow("a grid over a bigger selection is placed once")
                << QList<int>{0, 1, 2} << 0 << 3 << 1 << QList<QStringList>{{u"a"_s}, {u"b"_s}} << 2 << 0 << 0
                << u"Pozycja\tIlość\tCena\tRazem\na\t2\t38,5\t77\nb\t4\t45\t180\nWałek | duży\t1\t12\t12"_s;
        QTest::newRow("the header row can be pasted over")
                << all << 0 << 1 << 1 << QList<QStringList>{{u"Nazwa"_s, u"Szt."_s}, {u"Fuga"_s, u"5"_s}} << 4 << 0 << 0
                << u"Nazwa\tSzt.\tCena\tRazem\nFuga\t5\t38,5\t#ERR\n\"Żółć \n druga\"\t4\t45\t#ERR\nWałek | duży\t1\t12\t#ERR"_s; // [Ilość] is gone
        QTest::newRow("the same text changes nothing")
                << all << 0 << 1 << 1 << QList<QStringList>{{u"Pozycja"_s, u"Ilość"_s}} << 0 << 0 << 0
                << u"Pozycja\tIlość\tCena\tRazem\nPłytki\t2\t38,5\t77\n\"Żółć \n druga\"\t4\t45\t180\nWałek | duży\t1\t12\t12"_s;
    }
    void paste()
    {
        QFETCH(QList<int>, rows);
        QFETCH(int, column);
        QFETCH(int, selectedRows);
        QFETCH(int, selectedColumns);
        QFETCH(QList<QStringList>, grid);
        QFETCH(int, cellsChanged);
        QFETCH(int, rowsAdded);
        QFETCH(int, columnsCut);
        QFETCH(QString, after);
        calc::TableModel model = sample();
        const PasteResult r = pasteTableCells(model, PasteTarget{rows, column, selectedRows, selectedColumns}, grid);
        QCOMPARE(r.cells, cellsChanged);
        QCOMPARE(r.rowsAdded, rowsAdded);
        QCOMPARE(r.columnsCut, columnsCut);
        QCOMPARE(r.changed(), cellsChanged > 0 || rowsAdded > 0);
        if (after.isEmpty())
            return;
        // The table's text (formulas as results) from the header down, whatever it grew to.
        QList<int> every{-1};
        for (int i = 0; i < model.rows.size(); ++i)
            every.append(i);
        QCOMPARE(tableCellsToTsv(model, evaluated(model), cells(every, 0, int(model.header.size()) - 1)), after);
    }
    void pasteRefusals()
    {
        calc::TableModel model = sample();
        const calc::TableModel before = model;
        QVERIFY(!pasteTableCells(model, PasteTarget{{0}, 9, 1, 1}, {{u"x"_s}}).changed());       // no such column
        QVERIFY(!pasteTableCells(model, PasteTarget{{}, 0, 1, 1}, {{u"x"_s}}).changed());          // no row
        QVERIFY(!pasteTableCells(model, PasteTarget{{0}, 0, 1, 1}, {}).changed());                 // nothing
        QVERIFY(!pasteTableCells(model, PasteTarget{{0}, 0, 1, 1}, {QStringList{}}).changed());    // an empty record
        QVERIFY(!pasteTableCells(model, PasteTarget{{77}, 0, 1, 1}, {{u"x"_s}}).changed());        // a row that is gone
        QCOMPARE(model, before);
    }
    void pasteGrowthIsBounded()
    {
        calc::TableModel model = sample();
        QList<QStringList> grid;
        for (int i = 0; i < MaxCsvRows + 100; ++i)
            grid.append({u"x"_s});
        const PasteResult r = pasteTableCells(model, PasteTarget{{2}, 0, 1, 1}, grid);
        QCOMPARE(r.rowsAdded, MaxCsvRows);
        QCOMPARE(model.rows.size(), 3 + MaxCsvRows);
    }
    void pasteShortRows()
    {
        calc::TableModel model = sample();
        model.rows[0] = QStringList{u"Płytki"_s};
        QVERIFY(pasteTableCells(model, PasteTarget{{0}, 2, 1, 1}, {{u"5"_s}}).changed());
        QCOMPARE(model.rows[0].value(2), u"5"_s);
    }
    void pasteIsWrittenAsATable()
    {
        // The text of the page: what serializeTable writes, a line break as <br>, a pipe escaped.
        calc::TableModel model = sample();
        const PastedCells pasted = parsePastedCells(u"Żółw | wielki\t\"a\nb\"\n"_s);
        QCOMPARE(pasteTableCells(model, PasteTarget{{0}, 0, 1, 1}, pasted.grid).cells, 2);
        const QString text = calc::serializeTable(model);
        QVERIFY(text.contains(u"Żółw \\| wielki"_s));
        QVERIFY(text.contains(u"a<br>b"_s));
        QCOMPARE(calc::parseTable(text)->rows.first().first(), u"Żółw | wielki"_s);
    }

    void clear_data()
    {
        QTest::addColumn<QList<int>>("rows");
        QTest::addColumn<int>("first");
        QTest::addColumn<int>("last");
        QTest::addColumn<int>("cleared");
        QTest::newRow("a block") << QList<int>{0, 1} << 1 << 2 << 4;
        QTest::newRow("empty cells are not counted") << QList<int>{0} << 0 << 3 << 4;
        QTest::newRow("with the header") << QList<int>{-1} << 0 << 3 << 4;
        QTest::newRow("out of range") << QList<int>{9} << 0 << 1 << 0;
        QTest::newRow("a reversed range") << QList<int>{0} << 3 << 1 << 0;
    }
    void clear()
    {
        QFETCH(QList<int>, rows);
        QFETCH(int, first);
        QFETCH(int, last);
        QFETCH(int, cleared);
        calc::TableModel model = sample();
        QCOMPARE(clearTableCells(model, cells(rows, first, last)), cleared);
        QCOMPARE(clearTableCells(model, cells(rows, first, last)), 0); // twice: nothing left
        if (cleared > 0)
            QVERIFY(calc::parseTable(calc::serializeTable(model)).has_value());
    }
};

QTEST_MAIN(TestIoTableCells)
#include "tst_io_tablecells.moc"
