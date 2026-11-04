// Sorting and filtering GFM tables (calc/tablesort.h): column kinds, Polish order, dates, stable
// order, empty cells last, summary rows in place, rows kept byte for byte, references following
// their rows, filters, malformed and huge input.
#include "calc/tablesort.h"

#include <QTest>

using namespace brulion::calc;
using namespace Qt::StringLiterals;

namespace {

const QDate kToday(2026, 11, 3);

TableSortOptions options(CalcLocale locale = CalcLocale::Polish)
{
    TableSortOptions o;
    o.locale = locale;
    o.today = kToday;
    return o;
}

/// The first cells of the rows of `table` after sorting by `column` ("" when nothing changed).
QStringList firstCells(const QString &table, int column, SortOrder order, CalcLocale locale = CalcLocale::Polish)
{
    const auto sorted = sortTableText(table, column, order, options(locale));
    const auto model = parseTable(sorted ? *sorted : table);
    QStringList out;
    if (!model)
        return out;
    for (const QStringList &row : model->rows)
        out.append(row.value(0));
    return out;
}

QString table(const QStringList &rows, const QString &header = u"| Nazwa | Wartość |\n|---|---|"_s)
{
    return header + u'\n' + rows.join(u'\n');
}

} // namespace

class TestCalcTableSort : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void kinds_data()
    {
        QTest::addColumn<QStringList>("values");
        QTest::addColumn<int>("kind");
        QTest::newRow("numbers") << QStringList{u"12"_s, u"3,5"_s, u"-1"_s} << int(SortKind::Number);
        QTest::newRow("money and percent") << QStringList{u"12,50 zł"_s, u"23%"_s, u"1 200"_s} << int(SortKind::Number);
        QTest::newRow("empty cells do not count") << QStringList{u"1"_s, QString(), u"2"_s} << int(SortKind::Number);
        QTest::newRow("iso dates") << QStringList{u"2026-10-15"_s, u"2025-01-01 09:30"_s} << int(SortKind::Date);
        QTest::newRow("polish dates") << QStringList{u"15.10.2026"_s, u"3 listopada 2025"_s, u"[[2026-01-02]]"_s}
                                      << int(SortKind::Date);
        QTest::newRow("text") << QStringList{u"żółw"_s, u"12"_s} << int(SortKind::Text);
        QTest::newRow("words are no dates") << QStringList{u"jutro"_s, u"piątek"_s} << int(SortKind::Text);
        QTest::newRow("all empty") << QStringList{QString(), QString()} << int(SortKind::Text);
    }
    void kinds()
    {
        QFETCH(QStringList, values);
        QFETCH(int, kind);
        TableModel m;
        m.header = {u"A"_s};
        m.aligns = {TableAlign::None};
        for (const QString &v : values)
            m.rows.append({v});
        QCOMPARE(int(tableColumnKind(m, evaluateTable(m), 0, options())), kind);
    }

    void sort_data()
    {
        QTest::addColumn<QStringList>("rows");
        QTest::addColumn<int>("column");
        QTest::addColumn<bool>("descending");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("polish alphabet") << QStringList{u"| żaba | 1 |"_s, u"| zebra | 2 |"_s, u"| łódź | 3 |"_s,
                                                        u"| lis | 4 |"_s,  u"| Ćma | 5 |"_s,   u"| cebula | 6 |"_s,
                                                        u"| ąbc | 7 |"_s,  u"| azot | 8 |"_s}
                                         << 0 << false << QStringList{u"azot"_s, u"ąbc"_s,  u"cebula"_s, u"Ćma"_s,
                                                                      u"lis"_s,  u"łódź"_s, u"zebra"_s,  u"żaba"_s};
        QTest::newRow("polish descending") << QStringList{u"| łódź | 1 |"_s, u"| lis | 2 |"_s, u"| mak | 3 |"_s} << 0
                                           << true << QStringList{u"mak"_s, u"łódź"_s, u"lis"_s};
        QTest::newRow("digit runs by value") << QStringList{u"| Pokój 10 | |"_s, u"| Pokój 2 | |"_s, u"| Pokój 1 | |"_s}
                                             << 0 << false << QStringList{u"Pokój 1"_s, u"Pokój 2"_s, u"Pokój 10"_s};
        QTest::newRow("numbers, not text")
                << QStringList{u"| a | 10 |"_s, u"| b | 9 |"_s, u"| c | 100 |"_s, u"| d | -2,5 |"_s} << 1 << false
                << QStringList{u"d"_s, u"b"_s, u"a"_s, u"c"_s};
        QTest::newRow("money") << QStringList{u"| a | 12,50 zł |"_s, u"| b | 1 200 zł |"_s, u"| c | 5 zł |"_s} << 1
                               << true << QStringList{u"b"_s, u"a"_s, u"c"_s};
        QTest::newRow("dates") << QStringList{u"| a | 15.10.2026 |"_s, u"| b | 2026-01-02 |"_s,
                                              u"| c | 3 listopada 2025 |"_s, u"| d | 2026-01-02 08:00 |"_s}
                               << 1 << false << QStringList{u"c"_s, u"b"_s, u"d"_s, u"a"_s};
        QTest::newRow("stable") << QStringList{u"| a | 1 |"_s, u"| b | 0 |"_s, u"| c | 1 |"_s, u"| d | 0 |"_s} << 1
                                << false << QStringList{u"b"_s, u"d"_s, u"a"_s, u"c"_s};
        QTest::newRow("stable descending")
                << QStringList{u"| a | 1 |"_s, u"| b | 0 |"_s, u"| c | 1 |"_s, u"| d | 0 |"_s} << 1 << true
                << QStringList{u"a"_s, u"c"_s, u"b"_s, u"d"_s};
        QTest::newRow("empty last ascending") << QStringList{u"| a | |"_s, u"| b | 2 |"_s, u"| c | 1 |"_s} << 1 << false
                                              << QStringList{u"c"_s, u"b"_s, u"a"_s};
        QTest::newRow("empty last descending") << QStringList{u"| a | |"_s, u"| b | 2 |"_s, u"| c | 1 |"_s} << 1 << true
                                               << QStringList{u"b"_s, u"c"_s, u"a"_s};
        QTest::newRow("links by their text")
                << QStringList{u"| [[Zebra]] | |"_s, u"| [[Ala\\|Bartek]] | |"_s, u"| **Celina** | |"_s} << 0 << false
                << QStringList{u"[[Ala|Bartek]]"_s, u"**Celina**"_s, u"[[Zebra]]"_s};
        QTest::newRow("formula results") << QStringList{u"| a | =2*5 |"_s, u"| b | =1 |"_s, u"| c | 3 |"_s} << 1
                                         << false << QStringList{u"b"_s, u"c"_s, u"a"_s};
        QTest::newRow("summary row stays at the end")
                << QStringList{u"| b | 2 |"_s, u"| a | 1 |"_s, u"| Suma | =sum(above) |"_s} << 0 << false
                << QStringList{u"a"_s, u"b"_s, u"Suma"_s};
        QTest::newRow("summary by a range") << QStringList{u"| b | 2 |"_s, u"| a | 1 |"_s, u"| Razem | =sum(B1:B2) |"_s}
                                            << 1 << true << QStringList{u"b"_s, u"a"_s, u"Razem"_s};
        QTest::newRow("summary by a column, polish")
                << QStringList{u"| b | 2 |"_s, u"| a | 1 |"_s, u"| Razem | =suma(powyżej) |"_s} << 0 << false
                << QStringList{u"a"_s, u"b"_s, u"Razem"_s};
        QTest::newRow("leading row reading below")
                << QStringList{u"| Ogółem | =sum(below) |"_s, u"| b | 2 |"_s, u"| a | 1 |"_s} << 0 << false
                << QStringList{u"Ogółem"_s, u"a"_s, u"b"_s};
        QTest::newRow("english numbers") << QStringList{u"| a | 1,200.5 |"_s, u"| b | 99.5 |"_s} << 1 << false
                                         << QStringList{u"b"_s, u"a"_s};
    }
    void sort()
    {
        QFETCH(QStringList, rows);
        QFETCH(int, column);
        QFETCH(bool, descending);
        QFETCH(QStringList, expected);
        const CalcLocale locale =
                QByteArray(QTest::currentDataTag()).startsWith("english") ? CalcLocale::English : CalcLocale::Polish;
        QCOMPARE(firstCells(table(rows), column, descending ? SortOrder::Descending : SortOrder::Ascending, locale),
                 expected);
    }

    void linesKeptByteForByte()
    {
        // Odd padding, escaped pipes, a centred column, CRLF and a last row without a line break.
        const QString t =
                u"|  Kto | Ile \\| razy |\r\n|:--:|---:|\r\n| Żaneta |  3 \\| x |\r\n|Adam|1|\r\n| Łucja | 2 |"_s;
        const auto sorted = sortTableText(t, 0, SortOrder::Ascending, options());
        QVERIFY(sorted);
        QCOMPARE(*sorted,
                 u"|  Kto | Ile \\| razy |\r\n|:--:|---:|\r\n|Adam|1|\r\n| Łucja | 2 |\r\n| Żaneta |  3 \\| x |"_s);
        // In order already: nothing to do.
        QVERIFY(!sortTableText(*sorted, 0, SortOrder::Ascending, options()));
        // Back.
        const auto back = sortTableText(*sorted, 0, SortOrder::Descending, options());
        QVERIFY(back);
        QCOMPARE(*back,
                 u"|  Kto | Ile \\| razy |\r\n|:--:|---:|\r\n| Żaneta |  3 \\| x |\r\n| Łucja | 2 |\r\n|Adam|1|"_s);
    }

    void textAroundKept()
    {
        // Blank lines before the table and lines after it (parseTable() stops at a blank line).
        const QString t = u"\n| a |\n|---|\n| 2 |\n| 1 |\n\nreszta\n"_s;
        const auto sorted = sortTableText(t, 0, SortOrder::Ascending, options());
        QVERIFY(sorted);
        QCOMPARE(*sorted, u"\n| a |\n|---|\n| 1 |\n| 2 |\n\nreszta\n"_s);
    }

    void referencesFollowRows()
    {
        // =B1*2 names row 1 by address: after the sort it names the same row, now row 2.
        const QString t = u"| x | y | z |\n|---|---|---|\n| b | 5 | =B1*2 |\n| a | 7 | =B2*2 |"_s;
        const auto sorted = sortTableText(t, 0, SortOrder::Ascending, options());
        QVERIFY(sorted);
        const auto m = parseTable(*sorted);
        QVERIFY(m);
        QCOMPARE(m->rows.at(0), (QStringList{u"a"_s, u"7"_s, u"=B1*2"_s}));
        QCOMPARE(m->rows.at(1), (QStringList{u"b"_s, u"5"_s, u"=B2*2"_s}));
        // The results are the same as before for each row.
        const TableEval eval = evaluateTable(*m);
        QCOMPARE(eval.values.at(0).at(2), u"14"_s);
        QCOMPARE(eval.values.at(1).at(2), u"10"_s);
        // Header references and relative ones need nothing: the lines move as they are.
        const QString relative = u"| x | y | z |\n|---|---|---|\n| b | 5 |  =[y]*2 |\n| a | 7 | =[y]*2 |"_s;
        QCOMPARE(*sortTableText(relative, 0, SortOrder::Ascending, options()),
                 u"| x | y | z |\n|---|---|---|\n| a | 7 | =[y]*2 |\n| b | 5 |  =[y]*2 |"_s);
    }

    void permute()
    {
        TableModel m;
        m.header = {u"A"_s, u"B"_s};
        m.aligns = {TableAlign::None, TableAlign::None};
        m.rows = {{u"1"_s, u"=A3"_s}, {u"2"_s, u"=sum(A1:A3)"_s}, {u"3"_s, u"=A1+A2"_s}};
        QVERIFY(!permuteTableRows(m, {0, 1, 2})); // nothing moves
        QVERIFY(!permuteTableRows(m, {0, 0, 2})); // no permutation
        QVERIFY(!permuteTableRows(m, {0, 1}));    // too short
        QVERIFY(!permuteTableRows(m, {0, 1, 3})); // out of range
        QVERIFY(permuteTableRows(m, {2, 0, 1}));
        QCOMPARE(m.rows.at(0), (QStringList{u"3"_s, u"=A2+A3"_s}));
        QCOMPARE(m.rows.at(1), (QStringList{u"1"_s, u"=A1"_s}));
        QCOMPARE(m.rows.at(2), (QStringList{u"2"_s, u"=sum(A1:A3)"_s})); // a range keeps its place
    }

    void malformed_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("column");
        QTest::newRow("empty") << QString() << 0;
        QTest::newRow("no table") << u"zwykły tekst"_s << 0;
        QTest::newRow("no delimiter") << u"| a | b |\n| 1 | 2 |"_s << 0;
        QTest::newRow("column out of range") << u"| a |\n|---|\n| 2 |\n| 1 |"_s << 1;
        QTest::newRow("negative column") << u"| a |\n|---|\n| 2 |\n| 1 |"_s << -1;
        QTest::newRow("one row") << u"| a |\n|---|\n| 1 |"_s << 0;
        QTest::newRow("only a header") << u"| a |\n|---|"_s << 0;
        QTest::newRow("broken formula") << u"| a |\n|---|\n| =((( |\n| =1/0 |"_s << 0;
    }
    void malformed()
    {
        QFETCH(QString, text);
        QFETCH(int, column);
        const auto sorted = sortTableText(text, column, SortOrder::Ascending, options());
        if (sorted) // only a reordering, never a crash
            QVERIFY(parseTable(*sorted).has_value());
    }

    void shortRowsAndNaN()
    {
        // Short rows are padded; "nan" and "inf" are no numbers (text order).
        const QString t = u"| a | b |\n|---|---|\n| x |\n| y | 1 |\n| z | nan |"_s;
        const auto sorted = sortTableText(t, 1, SortOrder::Ascending, options());
        QVERIFY(sorted);
        QCOMPARE(parseTable(*sorted)->rows.value(2).value(0), u"x"_s); // the empty one last
    }

    void hugeTable()
    {
        // A table past the bound is not sorted (and does not take long to refuse).
        QString t = u"| a |\n|---|"_s;
        for (int i = kMaxSortRows + 1; i > 0; --i)
            t += u"\n| "_s + QString::number(i) + u" |"_s;
        QVERIFY(!sortTableText(t, 0, SortOrder::Ascending, options()));
        // A big one inside it is.
        QString big = u"| a | b |\n|---|---|"_s;
        for (int i = 3000; i > 0; --i)
            big += u"\n| "_s + QString::number(i) + u" | Wiersz "_s + QString::number(i) + u" |"_s;
        const auto sorted = sortTableText(big, 1, SortOrder::Ascending, options());
        QVERIFY(sorted);
        QCOMPARE(parseTable(*sorted)->rows.value(2999).value(0), u"3000"_s);
    }

    void filter_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("filter");
        QTest::addColumn<bool>("matches");
        QTest::newRow("empty filter") << u"x"_s << u"  "_s << true;
        QTest::newRow("contains") << u"Żółta farba"_s << u"farb"_s << true;
        QTest::newRow("no diacritics") << u"Żółta farba"_s << u"zolta"_s << true;
        QTest::newRow("case") << u"żółw"_s << u"ŻÓŁW"_s << true;
        QTest::newRow("not contained") << u"Wałek"_s << u"farba"_s << false;
        QTest::newRow("negated") << u"Wałek"_s << u"!farba"_s << true;
        QTest::newRow("negated contained") << u"Żółta farba"_s << u"!FARBA"_s << false;
        QTest::newRow("greater") << u"120 zł"_s << u"> 100"_s << true;
        QTest::newRow("greater not") << u"12,50 zł"_s << u">100"_s << false;
        QTest::newRow("greater or equal") << u"100"_s << u">= 100"_s << true;
        QTest::newRow("less with comma") << u"2,5"_s << u"< 2,75"_s << true;
        QTest::newRow("unicode operator") << u"3"_s << u"≤ 3"_s << true;
        QTest::newRow("number equal") << u"1 000"_s << u"= 1000"_s << true;
        QTest::newRow("number not equal") << u"7"_s << u"<> 7"_s << false;
        QTest::newRow("greater on text") << u"dużo"_s << u"> 100"_s << false;
        QTest::newRow("date after") << u"15.10.2026"_s << u"> 2026-10-01"_s << true;
        QTest::newRow("date before") << u"2026-10-15"_s << u"< 1 października 2026"_s << false;
        QTest::newRow("date on text") << u"wkrótce"_s << u">= 2026-01-01"_s << false;
        QTest::newRow("text equal") << u"Łódź"_s << u"= lodz"_s << true;
        QTest::newRow("text not equal") << u"Łódź"_s << u"!= Kraków"_s << true;
        QTest::newRow("text alphabetical") << u"Łódź"_s << u"> Lublin"_s << true;
        QTest::newRow("empty cells") << QString() << u"="_s << true;
        QTest::newRow("empty cells not") << u"x"_s << u"="_s << false;
        QTest::newRow("cells with text") << u"x"_s << u"!="_s << true;
        QTest::newRow("operator being typed") << u"x"_s << u">"_s << true;
        QTest::newRow("lone exclamation") << u"x"_s << u"!"_s << false;
    }
    void filter()
    {
        QFETCH(QString, value);
        QFETCH(QString, filter);
        QFETCH(bool, matches);
        QCOMPARE(tableValueMatches(value, filter, options()), matches);
    }

    void rowsShown()
    {
        const auto m = parseTable(u"| Pozycja | Cena |\n|---|---|\n| Płytki | 89 |\n| Żółta farba | 40 |\n"
                                  u"| Wałek | 12,50 |\n| Suma | =sum(above) |"_s);
        QVERIFY(m);
        const TableEval eval = evaluateTable(*m);
        QCOMPARE(tableRowsShown(*m, eval, {}, options()), (QList<bool>{true, true, true, true}));
        QCOMPARE(tableRowsShown(*m, eval, {QString(), u"> 20"_s}, options()), (QList<bool>{true, true, false, true}));
        QCOMPARE(tableRowsShown(*m, eval, {u"a"_s, u"< 50"_s}, options()), (QList<bool>{false, true, true, true}));
        // More filters than columns: the extra ones are ignored.
        QCOMPARE(tableRowsShown(*m, eval, {u"wał"_s, QString(), u"x"_s}, options()),
                 (QList<bool>{false, false, true, true}));
        // A formula column filters by its results.
        const auto f = parseTable(u"| a | b |\n|---|---|\n| x | =2*3 |\n| y | =1 |"_s);
        QCOMPARE(tableRowsShown(*f, evaluateTable(*f), {QString(), u">= 6"_s}, options()), (QList<bool>{true, false}));
    }
};

QTEST_GUILESS_MAIN(TestCalcTableSort)
#include "tst_calc_tablesort.moc"
