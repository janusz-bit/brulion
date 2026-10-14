// Table formulas (ported from the v1 tableEval.test.ts).
#include "calc/calc.h"

#include "calctest.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion::calc;
using namespace Qt::StringLiterals;
using calctest::En;
using calctest::nb;
using calctest::Pl;

namespace {

TableEval evaluate(const QString &markdown, CalcLocale locale = CalcLocale::Polish, const Scope &scope = Scope())
{
    const std::optional<TableModel> model = parseTable(markdown);
    if (!model)
        qFatal("not a table");
    return evaluateTable(*model, locale, scope);
}

/// Values with non-formula cells as "-" (null strings).
QList<QStringList> shown(const TableEval &e)
{
    QList<QStringList> out;
    for (const QStringList &row : e.values) {
        QStringList r;
        for (const QString &v : row)
            r.append(v.isNull() ? u"-"_s : v);
        out.append(r);
    }
    return out;
}

QStringList column(const TableEval &e, int c)
{
    QStringList out;
    for (const QStringList &row : e.values)
        out.append(row.at(c).isNull() ? u"-"_s : row.at(c));
    return out;
}

const QString kShop = QStringList{
    u"| Produkt | Cena | Ilość | Wartość |"_s,
    u"|:--|--:|:-:|--:|"_s,
    u"| Jabłka | 3,50 zł | 4 | =[Cena]*[Ilość] |"_s,
    u"| Gruszki | 5 zł | 2 | =B2*C2 |"_s,
    u"| Kawa | 20 zł | 1 | =[cena] * [ILOŚĆ] + 10% |"_s,
    u"| Suma | =sum(above) | =count(above) | =sum(D1:D3) |"_s,
}.join(u'\n');

TableModel oneColumn(const QStringList &cells)
{
    TableModel m;
    m.header = {u"n"_s};
    m.aligns = {TableAlign::None};
    for (const QString &c : cells)
        m.rows.append({c});
    return m;
}

} // namespace

class TestCalcTableEval : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void references()
    {
        const TableEval e = evaluate(kShop);
        QCOMPARE(shown(e), (QList<QStringList>{{u"-"_s, u"-"_s, u"-"_s, u"14 zł"_s},
                                               {u"-"_s, u"-"_s, u"-"_s, u"10 zł"_s},
                                               {u"-"_s, u"-"_s, u"-"_s, u"22 zł"_s},
                                               {u"-"_s, u"28,50 zł"_s, u"3"_s, u"46 zł"_s}}));
        for (const QStringList &row : e.errors) {
            for (const QString &err : row)
                QVERIFY(err.isNull());
        }
    }

    void directions()
    {
        const QString md = u"| a | b | c | d |\n|---|---|---|---|\n| 1 | 2 | 3 | =sum(left) |\n| =sum(right) | 5 | 6 | =sum(powyżej) |\n"
                           u"| =sum(below) | x | | |\n| 7 | | | =count(poniżej) |"_s;
        QCOMPARE(shown(evaluate(md)), (QList<QStringList>{{u"-"_s, u"-"_s, u"-"_s, u"6"_s},
                                                          {u"17"_s, u"-"_s, u"-"_s, u"6"_s},
                                                          {u"7"_s, u"-"_s, u"-"_s, u"-"_s},
                                                          {u"-"_s, u"-"_s, u"-"_s, u"0"_s}}));
    }

    void wholeColumns()
    {
        const QString md = u"| x | y |\n|---|---|\n| 1 | 10 |\n| 2 | 20 |\n| =sum(A:A) | =sum(A:B) |"_s;
        QCOMPARE(evaluate(md).values.at(2), (QStringList{u"3"_s, u"36"_s}));
    }

    void clampedAndReversedRanges()
    {
        const QString md = u"| x | y |\n|---|---|\n| 1 | =sum(A1:A99) |\n| 2 | =sum(A2:A1) |"_s;
        QCOMPARE(column(evaluate(md), 1), (QStringList{u"3"_s, u"3"_s}));
        QCOMPARE(evaluate(u"| a | b |\n|---|---|\n| 1 | =sum(A1:A9999999) |\n| 2 | |"_s).values[0][1], u"3"_s);
    }

    void laterFormulas()
    {
        const QString md = u"| a | b |\n|---|---|\n| =B1 * 2 | =B2 + 1 |\n| 5 | =A2 * 10 |"_s;
        QCOMPARE(shown(evaluate(md)), (QList<QStringList>{{u"102"_s, u"51"_s}, {u"-"_s, u"50"_s}}));
    }

    void headerMatching()
    {
        const QString md = u"| **Ilość** | Cena netto | Wartość |\n|---|---|---|\n| 2 | 50 | =[ilość] * [CENA  NETTO] |"_s;
        QCOMPARE(evaluate(md).values[0][2], u"100"_s);
    }

    void headersWithBrackets()
    {
        const QString md = u"| Cena (zł) | Ilość [szt] | Wartość |\n|---|---|---|\n| 12,50 | 4 | =[Cena (zł)] * [Ilość [szt]] |"_s;
        QCOMPARE(evaluate(md).values[0][2], u"50"_s);
    }

    void implicitMultiplicationWithHeaders()
    {
        // As in v1 (header references were plain identifiers there): `0,5 [Cena]` multiplies.
        const QString md = u"| Cena | Ilość | a | b | c |\n|---|---|---|---|---|\n"
                           u"| 10 | 3 | =0,5 [Cena] | =[Cena] [Ilość] | =1/2 [Cena] |\n| 4 | 2 | =2 [x] | =2 [1, 2] | |"_s;
        const TableEval e = evaluate(md);
        QCOMPARE(e.values[0], (QStringList{QString(), QString(), u"5"_s, u"30"_s, u"5"_s}));
        QCOMPARE(e.values[1][2], kErrorMarker.toString()); // [x] names no column: not a factor
        QCOMPARE(e.values[1][3], kErrorMarker.toString());
    }

    void headersWithApostrophesAndDecomposedLetters()
    {
        QCOMPARE(evaluate(u"| Jan's | b |\n|---|---|\n| 5 | =[Jan's] * 2 |"_s).values[0][1], u"10"_s);
        // NFD letters in the reference match the NFC header (and vice versa).
        const QString nfd = u"ilos\u0301c\u0301"_s;
        QCOMPARE(evaluate(u"| **Ilość** | b |\n|---|---|\n| 2 | =[%1] * 3 |"_s.arg(nfd)).values[0][1], u"6"_s);
        QCOMPARE(evaluate(u"| %1 | b |\n|---|---|\n| 2 | =[Ilość] * 3 |"_s.arg(nfd)).values[0][1], u"6"_s);
    }

    void listLiteralsThatAreNotHeaders()
    {
        QCOMPARE(evaluate(u"| a |\n|---|\n| =sum([1, 2, 3]) |"_s).values[0][0], u"6"_s);
        QCOMPARE(evaluate(u"| a | b |\n|---|---|\n| 1 | =sum([1, (2)]) |"_s).values[0][1], u"3"_s);
    }

    void localeNumbersCurrenciesPercentages()
    {
        const QString md = u"| kwota | vat | brutto |\n|---|---|---|\n| 1 234,50 zł | 23% | =[kwota] + [vat] |\n"
                           u"| **100** | 8 % | =A2 * (1 + B2) |"_s;
        QCOMPARE(column(evaluate(md), 2), (QStringList{u"1518,44 zł"_s, u"108"_s}));
    }

    void englishNumbers()
    {
        QCOMPARE(evaluate(u"| a | b |\n|---|---|\n| 1,500.5 | =A1 * 2 |"_s, En).values[0][1], u"3,001"_s);
    }

    void emptyCellsAndText()
    {
        const QString md = u"| a | b | c |\n|---|---|---|\n| 5 | | =[a] + [b] |\n| tekst | 2 | =sum(A1:B2) |\n"
                           u"| =count(A1:B2) | =avg(A1:B2) | |"_s;
        QCOMPARE(shown(evaluate(md)), (QList<QStringList>{{u"-"_s, u"-"_s, u"5"_s},
                                                          {u"-"_s, u"-"_s, u"7"_s},
                                                          {u"2"_s, u"3,5"_s, u"-"_s}}));
    }

    void currenciesOnlyWhenTheyAgree()
    {
        const QString md = u"| a |\n|---|\n| 10 zł |\n| 5 EUR |\n| =sum(above) |\n| =A1 * 2 |"_s;
        QCOMPARE(column(evaluate(md), 0), (QStringList{u"-"_s, u"-"_s, u"15"_s, u"20 zł"_s}));
    }

    void largeResults()
    {
        const QString md = u"| a |\n|---|\n| 12 000 |\n| =A1 * 2 |"_s;
        QCOMPARE(evaluate(md).values[1][0], nb(u"24_000"_s));
        QCOMPARE(evaluate(md, En).values[1][0], u"24,000"_s);
    }

    void notFormulas()
    {
        const QString md = u"| a | b | c | d |\n|---|---|---|---|\n| 1 | \\=A1 | = | =  |"_s;
        QCOMPARE(shown(evaluate(md)), (QList<QStringList>{{u"-"_s, u"-"_s, u"-"_s, u"-"_s}}));
    }

    void functions_data()
    {
        QTest::addColumn<QString>("formula");
        QTest::addColumn<QString>("expected");
        const QList<std::pair<QString, QString>> cases = {
            {u"=sum(A1:A3)"_s, u"6"_s},
            {u"=SUM(A1:A3)"_s, u"6"_s},
            {u"=suma(A1:A3; 4)"_s, u"10"_s},
            {u"=avg(A1:A3)"_s, u"2"_s},
            {u"=average(A1:A3)"_s, u"2"_s},
            {u"=średnia(A1:A3)"_s, u"2"_s},
            {u"=mean(A1:A3)"_s, u"2"_s},
            {u"=min(A1:A3)"_s, u"1"_s},
            {u"=max(A1:A3, 10)"_s, u"10"_s},
            {u"=count(A1:A3)"_s, u"3"_s},
            {u"=ile(A1:A3)"_s, u"3"_s},
            {u"=round(A1 / 3, 2)"_s, u"0,33"_s},
            {u"=zaokr(2,345; 1)"_s, u"2,3"_s},
            {u"=abs(A1 - A3)"_s, u"2"_s},
            {u"=if(A3 > 2, \"duże\", \"małe\")"_s, u"duże"_s},
            {u"=jeżeli(A1 > 2; 1; 0)"_s, u"0"_s},
            {u"=sqrt(A3^2)"_s, u"3"_s},
            {u"=1,5 * 2"_s, u"3"_s},
            {u"=A1 + 10%"_s, u"1,1"_s},
            {u"=sum(A1:A3) # komentarz"_s, u"6"_s},
            {u"=A1 = 1"_s, u"prawda"_s},
            {u"=LOG10(100)"_s, u"2"_s},
            {u"=median(A1:A3)"_s, u"2"_s},
            {u"=A1:A3"_s, u"[1; 2; 3]"_s},
        };
        for (const auto &[formula, expected] : cases)
            QTest::addRow("%s", qUtf8Printable(formula)) << formula << expected;
    }
    void functions()
    {
        QFETCH(QString, formula);
        QFETCH(QString, expected);
        TableModel model;
        model.header = {u"a"_s, u"b"_s};
        model.rows = {{u"1"_s, QString()}, {u"2"_s, QString()}, {u"3"_s, formula}};
        QCOMPARE(evaluateTable(model).values[2][1], expected);
    }

    void pageScopeVariables()
    {
        Scope scope;
        evaluateCalc(u"vat = 23%\nkurs = 4,30", scope);
        const QString md = u"| netto | brutto | eur |\n|---|---|---|\n| 100 | =[netto] + vat | =A1 / kurs |"_s;
        QCOMPARE(evaluate(md, Pl, scope).values[0], (QStringList{QString(), u"123"_s, u"23,25581395"_s}));
        QCOMPARE(evaluate(u"| a |\n|---|\n| =x = 5 |"_s).errors[0][0], u"Nieznana zmienna: x"_s);
    }

    void cycles()
    {
        const QString md = u"| a | b | c |\n|---|---|---|\n| =B1 + 1 | =A1 * 2 | =C1 |\n| =A2 | 1 | =A1 + 1 |"_s;
        const TableEval e = evaluate(md);
        QCOMPARE(shown(e), (QList<QStringList>{{u"#CYCLE"_s, u"#CYCLE"_s, u"#CYCLE"_s}, {u"#CYCLE"_s, u"-"_s, u"#ERR"_s}}));
        QCOMPARE(e.errors[0][0], u"Cykliczne odwołanie"_s);
        QCOMPARE(e.errors[1][2], u"Błąd w komórce A1"_s);
    }

    void ownColumnHeaderIsACycle()
    {
        QCOMPARE(evaluate(u"| a |\n|---|\n| =[a] + 1 |"_s).values[0][0], kCycleMarker.toString());
    }

    void errorsPerCell()
    {
        const QString md = u"| a | b |\n|---|---|\n| tekst | =A1 * 2 |\n| =A99 | =nope + 1 |\n| =1 + | =B1 + 1 |\n"
                           u"| =avg(C1:C3) | =Z1 |"_s;
        const TableEval e = evaluate(md);
        QCOMPARE(shown(e), (QList<QStringList>{{u"-"_s, u"#ERR"_s}, {u"#ERR"_s, u"#ERR"_s}, {u"#ERR"_s, u"#ERR"_s},
                                               {u"#ERR"_s, u"#ERR"_s}}));
        QCOMPARE(e.errors, (QList<QStringList>{{QString(), u"Nie można zamienić „tekst” na liczbę"_s},
                                               {u"Odwołanie poza tabelą: A99"_s, u"Nieznana zmienna: nope"_s},
                                               {u"Niedokończone wyrażenie"_s, u"Błąd w komórce B1"_s},
                                               {u"Brak wartości liczbowych"_s, u"Odwołanie poza tabelą: Z1"_s}}));
    }

    void englishErrors()
    {
        const TableEval e = evaluate(u"| a |\n|---|\n| =A1 |\n| =A9 |"_s, En);
        QCOMPARE(e.errors[0][0], u"Circular reference"_s);
        QCOMPARE(e.errors[1][0], u"Reference outside the table: A9"_s);
    }

    void noScriptFunctions()
    {
        const TableEval e = evaluate(u"| a |\n|---|\n| =import(1) |\n| =evaluate(\"1\") |"_s);
        QCOMPARE(column(e, 0), (QStringList{u"#ERR"_s, u"#ERR"_s}));
        QCOMPARE(e.errors[0][0], u"Nieznana funkcja: import"_s);
    }

    void raggedAndEmptyModels()
    {
        TableModel empty;
        empty.header = {u"a"_s};
        const TableEval e = evaluateTable(empty);
        QVERIFY(e.values.isEmpty() && e.errors.isEmpty());
        TableModel ragged;
        ragged.header = {u"a"_s};
        ragged.rows = {{u"1"_s, u"=A1 + 1"_s}};
        QCOMPARE(shown(evaluateTable(ragged)), (QList<QStringList>{{u"-"_s, u"2"_s}}));
        QVERIFY(evaluateTable(TableModel()).values.isEmpty());
    }

    void conditionalsAreNotRanges()
    {
        QCOMPARE(column(evaluate(u"| a | b | c |\n|---|---|---|\n| 1 | 2 | =A1 > 0 ? A1 : B1 |\n| -1 | 2 | =A2 > 0 ? A2 : B2 |"_s), 2),
                 (QStringList{u"1"_s, u"2"_s}));
        QCOMPARE(evaluate(u"| a | b |\n|---|---|\n| 1 | =A1 > 0 ? sum(A1:A1) : 0 |"_s).values[0][1], u"1"_s);
        QCOMPARE(evaluate(u"| a | b | c |\n|---|---|---|\n| 1 | 7 | =A1 ? (A1 > 0 ? A1 : 0) : B1 |"_s).values[0][2], u"1"_s);
        QCOMPARE(evaluate(u"| a | b | c |\n|---|---|---|\n| 0 | 7 | =A1 ? A1 : B1 |"_s).values[0][2], u"7"_s);
    }

    void meanFlattensAndSkipsText()
    {
        const TableEval e = evaluate(u"| a | b | c |\n|---|---|---|\n| 1 | 4 | =mean(A1:A2, B1) |\n| 3 | x | =mean(A2, B2) |"_s);
        QCOMPARE(column(e, 2), (QStringList{u"2,666666667"_s, u"3"_s}));
    }

    void unitsInCells()
    {
        const TableEval e = evaluate(u"| dystans | czas | tempo |\n|---|---|---|\n| =12 km | =1,5 h | =[dystans] / [czas] |"_s);
        QCOMPARE(e.values[0][2], u"8 km/h"_s);
    }

    // --- large tables -------------------------------------------------------------------------

    void manyRows()
    {
        QStringList cells;
        for (int i = 0; i < 100000; ++i)
            cells.append(QString::number(i));
        cells[0] = u"=1 + 1"_s;
        const TableEval e = evaluateTable(oneColumn(cells));
        QCOMPARE(e.values.size(), 100000);
        QCOMPARE(e.values[0][0], u"2"_s);
        QVERIFY(e.values[1][0].isNull());
    }

    void longReferenceChain()
    {
        QStringList cells{u"1"_s};
        for (int i = 1; i < 40000; ++i)
            cells.append(u"=A%1 + 1"_s.arg(i));
        const TableEval e = evaluateTable(oneColumn(cells));
        QCOMPARE(e.values[39999][0], nb(u"40_000"_s));
        for (const QStringList &row : e.errors)
            QVERIFY(row[0].isNull());
    }

    void cyclesAndIndependentCells()
    {
        const TableEval e = evaluate(u"| a | b |\n|---|---|\n| =B1 | =A1 |\n| =A3 + 1 | 5 |\n| =B2 | =B1 |"_s);
        QCOMPARE(shown(e), (QList<QStringList>{{u"#CYCLE"_s, u"#CYCLE"_s}, {u"6"_s, u"-"_s}, {u"5"_s, u"#ERR"_s}}));
    }

    void workLimits()
    {
        TableModel model;
        model.header = {u"n"_s, u"suma"_s};
        for (int i = 0; i < 5000; ++i)
            model.rows.append({QString::number(i), u"=sum(A1:A%1)"_s.arg(i + 1)});
        QElapsedTimer timer;
        timer.start();
        const TableEval e = evaluateTable(model);
        QCOMPARE(e.values[99][1], u"4950"_s);
        QCOMPARE(e.values[4999][1], kErrorMarker.toString());
        QCOMPARE(e.errors[4999][1], u"Obliczenie jest zbyt złożone"_s);
        QVERIFY(timer.elapsed() < 60000);
    }

    void denseChainsInBothDirections()
    {
        for (const QString &dir : {u"above"_s, u"below"_s}) {
            TableModel model;
            model.header = {u"n"_s, u"x"_s};
            for (int i = 0; i < 8000; ++i)
                model.rows.append({QString::number(i), u"=sum(%1) + 1"_s.arg(dir)});
            const TableEval e = evaluateTable(model);
            int ok = 0;
            for (const QStringList &row : e.values)
                ok += row[1] != kErrorMarker ? 1 : 0;
            QVERIFY2(ok > 1000 && ok < 8000, qPrintable(u"%1: %2 cells evaluated"_s.arg(dir).arg(ok)));
        }
    }
};

QTEST_GUILESS_MAIN(TestCalcTableEval)
#include "tst_calc_tableeval.moc"
