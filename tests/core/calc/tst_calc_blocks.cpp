// Calc blocks and inline calculations (SYNTAX.md §9), ported from the v1 calc.test.ts.
#include "calc/calc.h"

#include "calctest.h"

#include <QElapsedTimer>
#include <QTest>

using namespace brulion::calc;
using namespace Qt::StringLiterals;
using calctest::En;
using calctest::inl;
using calctest::last;
using calctest::nb;
using calctest::Pl;
using calctest::results;

class TestCalcBlocks : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- spec example -----------------------------------------------------------------------

    void specExample()
    {
        const QString src = u"cena = 12.50\nilość = 4\ncena * ilość\nrabat = 15%\nans - rabat\n5 km to mi\n20% of 150\nsum"_s;
        QCOMPARE(results(src), (QStringList{u"12,5"_s, u"4"_s, u"50"_s, u"15%"_s, u"42,5"_s, u"3,11 mi"_s,
                                            u"30"_s, u"139"_s}));
    }

    void reportsAssignedAndNumeric()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"ilość = 4\n5 km to mi\n10% + 5%", scope);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].line, 0);
        QCOMPARE(lines[0].input, u"ilość = 4"_s);
        QCOMPARE(lines[0].value, u"4"_s);
        QCOMPARE(lines[0].assigned, u"ilość"_s);
        QCOMPARE(lines[0].numeric, std::optional<double>(4));
        QVERIFY(qAbs(*lines[1].numeric - 3.1068559611866697) < 1e-12);
        QVERIFY(qAbs(*lines[2].numeric - 0.15) < 1e-12);
        QVERIFY(lines[1].assigned.isEmpty());
    }

    void onePerLineAnyLineEnding()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"1 + 1\r\n\r\n# c\r2\n", scope);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines[0].value, u"2"_s);
        QCOMPARE(lines[1].input, QString());
        QCOMPARE(lines[2].input, u"# c"_s);
        QVERIFY(lines[2].value.isEmpty() && lines[2].error.isEmpty());
        QCOMPARE(lines[3].value, u"2"_s);
        QCOMPARE(lines[3].line, 3);
        QCOMPARE(lines[4].input, QString());
    }

    void emptySource()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"", scope);
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines[0].line, 0);
        QVERIFY(!lines[0].hasValue() && !lines[0].hasError());
        QVERIFY(scope.isEmpty());
    }

    // --- variables ----------------------------------------------------------------------------

    void unicodeIdentifiers()
    {
        QCOMPARE(results(u"ilość = 4\nżółć = 3\nżółć * ilość"_s), (QStringList{u"4"_s, u"3"_s, u"12"_s}));
        QCOMPARE(last(u"цена = 5\nцена * 2"_s), u"10"_s);
        QCOMPARE(last(u"α = 2\nα^3"_s), u"8"_s);
        QCOMPARE(last(u"cena_netto = 100\ncena_netto * 1,23"_s), u"123"_s);
        QCOMPARE(last(u"ZAŻÓŁĆ = 1\nzażółć = 2\nZAŻÓŁĆ + zażółć * 10"_s), u"21"_s);
        // NFC: a decomposed name is the same variable as the composed one.
        QCOMPARE(last(u"ilość = 7\nilość"_s), u"7"_s);
    }

    void scopeIsUpdated()
    {
        Scope scope;
        evaluateCalc(u"ilość = 4\nżółć = ilość * 2", scope);
        QCOMPARE(scope.number(u"ilość"_s), std::optional<double>(4));
        QCOMPARE(scope.number(u"żółć"_s), std::optional<double>(8));
        QCOMPARE(scope.number(u"ans"_s), std::optional<double>(8));
        QCOMPARE(scope.names(), (QStringList{u"ans"_s, u"ilość"_s, u"żółć"_s}));
    }

    void unknownVariables()
    {
        QCOMPARE(last(u"brak_zmiennej + 1"_s), u"!Nieznana zmienna: brak_zmiennej"_s);
        QCOMPARE(last(u"zażółć + 1"_s), u"!Nieznana zmienna: zażółć"_s);
        QCOMPARE(last(u"zażółć + 1"_s, En), u"!Unknown variable: zażółć"_s);
    }

    void usesGivenScope()
    {
        Scope scope;
        scope.set(u"x"_s, Value::number(21));
        scope.set(u"cena_netto"_s, Value::number(10));
        Scope copy = scope;
        const auto lines = evaluateCalc(u"x * 2\ncena_netto + 1\nx = 1", copy);
        QCOMPARE(lines[0].value, u"42"_s);
        QCOMPARE(lines[1].value, u"11"_s);
        QCOMPARE(lines[2].value, u"1"_s);
        QCOMPARE(scope.number(u"x"_s), std::optional<double>(21)); // copies are independent
        QCOMPARE(copy.number(u"x"_s), std::optional<double>(1));
    }

    void ans()
    {
        QCOMPARE(results(u"2 + 3\nans * 2\nrate = 10%\nans + 1"_s), (QStringList{u"5"_s, u"10"_s, u"10%"_s, u"11"_s}));
        QCOMPARE(last(u"ans"_s), u"!Nieznana zmienna: ans"_s);
        QCOMPARE(results(u"2\nbłąd +\nans"_s), (QStringList{u"2"_s, u"!Niedokończone wyrażenie"_s, u"2"_s}));
    }

    void userFunctions()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"f(x) = x^2\nf(4)\nbrutto(netto) = netto * 1,23\nbrutto(100)\ng(a; b) = a - b\ng(5; 2)",
                                        scope);
        QStringList values;
        for (const auto &l : lines)
            values << calctest::show(l);
        QCOMPARE(values, (QStringList{u"f(x)"_s, u"16"_s, u"brutto(netto)"_s, u"123"_s, u"g(a, b)"_s, u"3"_s}));
        QCOMPARE(lines[0].assigned, u"f"_s);
        QVERIFY(scope.value(u"f"_s).isFunction());
        QCOMPARE(last(u"f(x) = x\nf(1, 2)"_s), u"!Za dużo argumentów funkcji f"_s);
        QCOMPARE(last(u"f(x, y) = x\nf(1)"_s), u"!Za mało argumentów funkcji f"_s);
        QCOMPARE(last(u"stała() = 42\nstała()"_s), u"42"_s);
        QCOMPARE(last(u"k = 3\nf(x) = x * k\nk = 4\nf(2)"_s), u"8"_s); // globals are read at call time
        QCOMPARE(last(u"f(x) = x * 2\ng = f\ng(21)"_s), u"42"_s);
    }

    void severalStatements()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"a = 1; b = 2\na + b\nc = round(2,5; 0)", scope);
        QCOMPARE(calctest::show(lines[0]), u"2"_s);
        QCOMPARE(lines[0].assigned, u"b"_s);
        QCOMPARE(calctest::show(lines[1]), u"3"_s);
        QCOMPARE(calctest::show(lines[2]), u"3"_s);
        QCOMPARE(scope.number(u"a"_s), std::optional<double>(1));
        QCOMPARE(scope.number(u"c"_s), std::optional<double>(3));
    }

    void variablesShadowKeywords()
    {
        QCOMPARE(results(u"total = 100\ntotal"_s), (QStringList{u"100"_s, u"100"_s}));
        QCOMPARE(results(u"h = 2\nm = 3\nh * m"_s), (QStringList{u"2"_s, u"3"_s, u"6"_s})); // not hour·metre
        QCOMPARE(last(u"pi = 3\npi * 2"_s), u"6"_s);
        QCOMPARE(last(u"to = 5"_s), u"!Nie można przypisać do „to”"_s);
        QCOMPARE(last(u"mod(x) = x"_s), u"!Nie można przypisać do „mod”"_s);
    }

    // --- labels and comments ------------------------------------------------------------------

    void labels()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"Czynsz: 1800 zł\nPrąd i gaz: 245,50 zł\nRazem:", scope);
        QCOMPARE(lines[0].label, u"Czynsz"_s);
        QCOMPARE(lines[0].value, u"1800 zł"_s);
        QCOMPARE(lines[1].label, u"Prąd i gaz"_s);
        QCOMPARE(lines[1].value, u"245,50 zł"_s);
        QCOMPARE(lines[2].label, u"Razem"_s);
        QCOMPARE(lines[2].value, u"2045,50 zł"_s);
    }

    void labelsWithApostrophes()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"Jan's share: 50\nMom's gift: 20 zł\nRazem:", scope);
        QCOMPARE(lines[0].label, u"Jan's share"_s);
        QCOMPARE(lines[0].value, u"50"_s);
        QCOMPARE(lines[1].label, u"Mom's gift"_s);
        QCOMPARE(lines[1].value, u"20 zł"_s);
        QCOMPARE(lines[2].value, u"70 zł"_s); // the only currency in the group labels the sum
    }

    void labelsStartingWithSupplementaryLetters()
    {
        // UTF-16: a letter outside the BMP is a surrogate pair (v1 matched \p{L} by code point).
        Scope scope;
        const auto lines = evaluateCalc(u"𝒜: 5\n𝒜 = 2\n𝒜 * 3"_s, scope);
        QCOMPARE(lines[0].label, u"𝒜"_s);
        QCOMPARE(lines[0].value, u"5"_s);
        QCOMPARE(lines[1].assigned, u"𝒜"_s);
        QCOMPARE(lines[2].value, u"6"_s);
    }

    void labelsWithAssignmentsAndKeywords()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"Koszt (stały): k = 50\nWynik: k * 2\nSuma: sum", scope);
        QCOMPARE(lines[0].label, u"Koszt (stały)"_s);
        QCOMPARE(lines[0].value, u"50"_s);
        QCOMPARE(lines[0].assigned, u"k"_s);
        QCOMPARE(lines[1].label, u"Wynik"_s);
        QCOMPARE(lines[1].value, u"100"_s);
        QVERIFY(lines[1].assigned.isEmpty());
        QCOMPARE(lines[2].label, u"Suma"_s);
        QCOMPARE(lines[2].value, u"150"_s);
    }

    void notLabels()
    {
        QCOMPARE(last(u"a = 5\na > 1 ? 2 : 3"_s), u"2"_s);
        QCOMPARE(last(u"a = 0\nWynik: a > 1 ? 2 : 3"_s), u"3"_s);
        Scope scope;
        QVERIFY(evaluateCalc(u"2026: 1", scope)[0].label.isEmpty());
        QVERIFY(evaluateCalc(u"x == 1 : 2", scope)[0].label.isEmpty());
        QVERIFY(evaluateCalc(u"\"a: b\"", scope)[0].label.isEmpty());
    }

    void labelWithoutExpression()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"Notatka:", scope);
        QCOMPARE(lines[0].label, u"Notatka"_s);
        QVERIFY(!lines[0].hasValue() && !lines[0].hasError());
    }

    void comments()
    {
        QCOMPARE(results(u"# Rachunki\n// Notatka\n2 + 2 # cztery\n3 * 3 // dziewięć\nconcat(\"#\", \"x\")"_s),
                 (QStringList{QString(), QString(), u"4"_s, u"9"_s, u"#x"_s}));
        QCOMPARE(results(u"  # wcięty komentarz\n5"_s), (QStringList{QString(), u"5"_s}));
    }

    // --- aggregates ---------------------------------------------------------------------------

    void aggregates()
    {
        QCOMPARE(results(u"10\n20\nsuma\navg\nrazem\nŚrednia\ntotal\naverage\nmean"_s),
                 (QStringList{u"10"_s, u"20"_s, u"30"_s, u"15"_s, u"30"_s, u"15"_s, u"30"_s, u"15"_s, u"15"_s}));
    }

    void groupsResetAtBlankLines()
    {
        QCOMPARE(results(u"10\n20\n\n5\nsum"_s), (QStringList{u"10"_s, u"20"_s, QString(), u"5"_s, u"5"_s}));
        QCOMPARE(results(u"10\n   \n5\nsum"_s), (QStringList{u"10"_s, QString(), u"5"_s, u"5"_s}));
    }

    void aggregatesInsideExpressions()
    {
        QCOMPARE(results(u"100\n200\nvat = suma * 23%\nsum"_s), (QStringList{u"100"_s, u"200"_s, u"69"_s, u"369"_s}));
    }

    void emptyGroups()
    {
        QCOMPARE(last(u"sum"_s), u"0"_s);
        QCOMPARE(last(u"avg"_s), u"!Brak wartości liczbowych"_s);
        QCOMPARE(last(u"# x\nśrednia"_s), u"!Brak wartości liczbowych"_s);
        QCOMPARE(last(u"avg"_s, En), u"!No numeric values"_s);
    }

    void aggregatesSkipNonResults()
    {
        QCOMPARE(last(u"10\nnope\n5%\ntrue\n\"x\"\n20\nsum"_s), u"30"_s);
        QCOMPARE(last(u"10\nf(x) = x\n20\nsum"_s), u"30"_s);
    }

    void aggregatesWithUnits()
    {
        QCOMPARE(last(u"1 km\n500 m\nsum"_s), u"1,5 km"_s);
        QCOMPARE(last(u"1 km\n2\n3\nsum"_s), u"5"_s);
        QCOMPARE(last(u"1 km\n2 kg\nsum"_s), u"!Niezgodne jednostki"_s);
        QCOMPARE(last(u"2 h\n30 min\navg"_s), u"1,25 h"_s);
    }

    void aggregatesWithCurrencies()
    {
        QCOMPARE(last(u"12 zł\n5,50 zł\n2\nsuma"_s), u"19,50 zł"_s);
        QCOMPARE(last(u"12 zł\n5 EUR\nsuma"_s), u"17"_s);
        QCOMPARE(last(u"12 zł\n5 EUR\n3 zł\nsuma"_s), u"20"_s); // once mixed, always mixed
    }

    void aggregatesAreMetered()
    {
        // Every `sum` line adds up the whole group above it: quadratic work that must be bounded
        // by the block's work budget (a synced note must not freeze the app).
        QString block;
        for (int i = 0; i < 6000; ++i)
            block += u"1\nsum\n"_s;
        Scope scope;
        QElapsedTimer timer;
        timer.start();
        const auto lines = evaluateCalc(block, scope);
        QVERIFY2(timer.elapsed() < 20000 * BRULION_TEST_SLOWDOWN, "aggregates must be charged to the work budget");
        QCOMPARE(lines[1].value, u"1"_s);
        QCOMPARE(lines[199].value, u"100"_s);
        QCOMPARE(lines[lines.size() - 2].error, u"Obliczenie jest zbyt złożone"_s);
        Scope fresh;
        QCOMPARE(evaluateCalc(u"1\n2\nsum", fresh)[2].value, u"3"_s); // a new call has a new budget
    }

    void assignedAggregate()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"1\n2\nx = sum\nx * 10\nsum", scope);
        QStringList values;
        for (const auto &l : lines)
            values << calctest::show(l);
        QCOMPARE(values, (QStringList{u"1"_s, u"2"_s, u"3"_s, u"30"_s, u"33"_s}));
        QCOMPARE(scope.number(u"x"_s), std::optional<double>(3));
    }

    // --- numbers and locales ------------------------------------------------------------------

    void decimalCommas()
    {
        QCOMPARE(results(u"12,5 * 2\nmax(1,5)\n(12,5 + 1) * 2\nround(2,3456; 2)\nmax(1,5; 2,5)"_s),
                 (QStringList{u"25"_s, u"5"_s, u"27"_s, u"2,35"_s, u"2,5"_s}));
    }

    void thousandsSeparators()
    {
        QCOMPARE(results(u"1 000 + 1\n12 345,5 * 2\n1 000 000,5"_s),
                 (QStringList{u"1001"_s, nb(u"24_691"_s), nb(u"1_000_000,5"_s)}));
        QCOMPARE(last(nb(u"12_345 + 0"_s)), nb(u"12_345"_s));
        QCOMPARE(last(u"12 345 + 0"_s), nb(u"12_345"_s)); // thin space
        QCOMPARE(last(u"2 3"_s), u"!Nieoczekiwany fragment „3”"_s);
    }

    void englishFormatting()
    {
        QCOMPARE(results(u"1,000 + 0.5\n12345.5 * 2\n5 > 3\n12,5 * 2"_s, En),
                 (QStringList{u"1,000.5"_s, u"24,691"_s, u"true"_s, u"25"_s}));
        QCOMPARE(last(u"1,000,000.5 * 2"_s, En), u"2,000,001"_s);
        QCOMPARE(last(u"max(1,000)"_s, En), u"1"_s); // ',' separates arguments
    }

    void specialValues()
    {
        QCOMPARE(results(u"1/3\n1/0\n0.1 + 0.2\n1e21\n5 > 3\n5 < 3\n\"abc\"\n-1/0"_s),
                 (QStringList{u"0,3333333333"_s, u"∞"_s, u"0,3"_s, u"1e21"_s, u"prawda"_s, u"fałsz"_s, u"abc"_s, u"-∞"_s}));
        QCOMPARE(last(u"sqrt(-4)"_s), u"!Argument poza dziedziną funkcji sqrt"_s);
        QCOMPARE(last(u"0/0"_s), u"NaN"_s);
    }

    void unicodeMath()
    {
        QCOMPARE(results(u"2 × 3\n6 ÷ 4\n5 − 2\n√16\n2³\nπ\n2 ≤ 3\n3 ≠ 3\n5 m²\n10⁻¹\n2 · 4\n∞"_s),
                 (QStringList{u"6"_s, u"1,5"_s, u"3"_s, u"4"_s, u"8"_s, u"3,141592654"_s, u"prawda"_s, u"fałsz"_s,
                              u"5 m²"_s, u"0,1"_s, u"8"_s, u"∞"_s}));
    }

    // --- percentages --------------------------------------------------------------------------

    void percentages_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        const QList<std::pair<QString, QString>> cases = {
            {u"100 + 10%"_s, u"110"_s},
            {u"100 - 10%"_s, u"90"_s},
            {u"100 + 5% + 5%"_s, u"110,25"_s},
            {u"20% of 150"_s, u"30"_s},
            {u"20% z 150"_s, u"30"_s},
            {u"20% od 150"_s, u"30"_s},
            {u"200 * 10%"_s, u"20"_s},
            {u"15%"_s, u"15%"_s},
            {u"10% + 5%"_s, u"15%"_s},
            {u"50 as % of 200"_s, u"25%"_s},
            {u"50 jako % z 200"_s, u"25%"_s},
            {u"10 % 3"_s, u"1"_s},
            {u"mod(10, 3)"_s, u"1"_s},
            {u"x = 80\nx + (25%)"_s, u"100"_s},
            {u"-10% of 50"_s, u"-5"_s},
            {u"12,5%"_s, u"12,5%"_s},
            {u"20% of 100 + 5"_s, u"25"_s},
            {u"-10 % 3"_s, u"2"_s},
            {u"100 + 10% - 5"_s, u"105"_s},
            {u"5 km + 10%"_s, u"5,5 km"_s},
        };
        for (const auto &[source, expected] : cases)
            QTest::addRow("%s", qUtf8Printable(source)) << source << expected;
    }
    void percentages()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(last(source), expected);
    }

    void percentVariables()
    {
        QCOMPARE(results(u"vat = 23%\nnetto = 100\nnetto + vat\nvat of netto\nvat z netto\nnetto - vat"_s),
                 (QStringList{u"23%"_s, u"100"_s, u"123"_s, u"23"_s, u"23"_s, u"77"_s}));
    }

    void percentOfNeedsAPercentage()
    {
        // `of`/`z`/`od` only follow a percentage (as in v1): "5 z 10" is not 50.
        QCOMPARE(last(u"5 z 10"_s), u"!Nieoczekiwany fragment „z”"_s);
        QCOMPARE(last(u"0.2 of 150"_s), u"!Nieoczekiwany fragment „of”"_s);
        QCOMPARE(last(u"x = 2\nx od 3"_s), u"!Nieoczekiwany fragment „od”"_s);
        QCOMPARE(last(u"(10% + 5%) of 200"_s), u"30"_s);
        QCOMPARE(last(u"rabat = 15%\n-rabat z 200 zł"_s), u"-30 zł"_s);
        QCOMPARE(last(u"z = 2\n3 z"_s), u"6"_s); // a variable named z
    }

    // --- units --------------------------------------------------------------------------------

    void units_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        const QList<std::pair<QString, QString>> cases = {
            {u"5 km to mi"_s, u"3,11 mi"_s},
            {u"3 h + 20 min"_s, u"3,33 h"_s},
            {u"2 kg in lb"_s, u"4,41 lb"_s},
            {u"90 min to h"_s, u"1,5 h"_s},
            {u"1500 m"_s, u"1500 m"_s},
            {u"2 kg * 3 m / s^2"_s, u"6 N"_s},
            {u"5 km na mi"_s, u"3,11 mi"_s},
            {u"5 km w mi"_s, u"3,11 mi"_s},
            {u"20°C to °F"_s, u"68°F"_s},
            {u"3 godz + 30 minut"_s, u"3,5 h"_s},
            {u"2 dni to godz"_s, u"48 h"_s},
            {u"1 tydzień to dni"_s, u"7 d"_s},
            {u"100 km / 2 h"_s, u"50 km/h"_s},
            {u"10 m / 2 m"_s, u"5"_s},
            {u"5 km + 2 kg"_s, u"!Niezgodne jednostki"_s},
            {u"2 km + 3 m to cm"_s, nb(u"200_300 cm"_s)},
            {u"120 km/h to m/s"_s, u"33,33 m/s"_s},
            {u"60 mph to km/h"_s, u"96,56 km/h"_s},
            {u"1 GiB to MB"_s, u"1073,74 MB"_s},
            {u"2 kW * 3 h"_s, u"6 kWh"_s},
            {u"1 kWh to kJ"_s, u"3600 kJ"_s},
            {u"2 l + 500 ml"_s, u"2,5 l"_s},
            {u"3 m * 4 m"_s, u"12 m²"_s},
            {u"1 ha to m2"_s, nb(u"10_000 m²"_s)},
            {u"300 K to °C"_s, u"26,85°C"_s},
            {u"100 °F to C"_s, u"37,78°C"_s},
            {u"20°C + 5°C"_s, u"25°C"_s},
            {u"1 rok to dni"_s, u"365,25 d"_s},
            {u"5 km to kg"_s, u"!Niezgodne jednostki"_s},
            {u"5 to km"_s, u"!Niezgodne jednostki"_s},
            {u"5 km to foo"_s, u"!„foo” nie jest jednostką"_s},
            {u"5 cm in in"_s, u"1,97 in"_s},
            {u"1 dag + 1 dkg"_s, u"2 dag"_s},
            {u"sqrt(16 m²)"_s, u"4 m"_s},
            {u"(3 m)^2"_s, u"9 m²"_s},
            {u"2 km * 3 m"_s, u"0,006 km²"_s},
            // Quantities show at most two decimals; below 1 two significant digits remain.
            {u"1 m to km"_s, u"0,001 km"_s},
            {u"4,2 m to km"_s, u"0,0042 km"_s},
            {u"1 cm to km"_s, u"0,00001 km"_s},
            {u"20 min to h"_s, u"0,33 h"_s},
            {u"1 min to h"_s, u"0,017 h"_s},
            {u"12345,678 m"_s, nb(u"12_345,68 m"_s)},
            {u"2,005 kg"_s, u"2 kg"_s},
            {u"-5 km to mi"_s, u"-3,11 mi"_s},
            {u"1 GB / (10 MB/s)"_s, u"100 s"_s},
            {u"5 a to m2"_s, u"500 m²"_s},
            {u"1 ha to a"_s, u"100 a"_s},
            {u"3 ar + 1 a"_s, u"4 a"_s},
            {u"-2 a"_s, u"-2 a"_s},
            {u"a + 1"_s, u"!Nieznana zmienna: a"_s},
            {u"a = 2\n5 a"_s, u"10"_s},
            {u"2 ha na a"_s, u"200 a"_s},
        };
        for (const auto &[source, expected] : cases)
            QTest::addRow("%s", qUtf8Printable(source)) << source << expected;
    }
    void units()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(last(source), expected);
    }

    void englishUnits()
    {
        QCOMPARE(last(u"5 km to mi"_s, En), u"3.11 mi"_s);
        QCOMPARE(last(u"3 h + 20 min"_s, En), u"3.33 h"_s);
        QCOMPARE(last(u"14 d to week"_s, En), u"2 wk"_s);
        QCOMPARE(last(u"14 d to week"_s, Pl), u"2 tydz."_s);
    }

    // --- currencies ---------------------------------------------------------------------------

    void currencies_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<QString>("expected");
        const QList<std::pair<QString, QString>> cases = {
            {u"12 zł + 5 zł"_s, u"17 zł"_s},
            {u"12,50 zł * 3"_s, u"37,50 zł"_s},
            {u"12 PLN + 5 PLN"_s, u"17 PLN"_s},
            {u"12 zł + 5 PLN"_s, u"17 zł"_s},
            {u"12 zł + 5 EUR"_s, u"17"_s},
            {u"$12 + $5"_s, u"$17"_s},
            {u"€10 * 1,5"_s, u"€15"_s},
            {u"10 € + 2 €"_s, u"12 €"_s},
            {u"£3 - £5"_s, u"-£2"_s},
            {u"100 PLN - 10%"_s, u"90 PLN"_s},
            {u"20% of 150 zł"_s, u"30 zł"_s},
            {u"12 zł / kg * 2 kg"_s, u"24 zł"_s},
            {u"12 zł/kg"_s, u"12 zł/kg"_s},
            {u"100 USD to PLN"_s, u"!Przeliczanie walut nie jest obsługiwane"_s},
            {u"5 zł in EUR"_s, u"!Przeliczanie walut nie jest obsługiwane"_s},
            {u"1 000 zł * 12"_s, nb(u"12_000 zł"_s)},
            {u"100 zł / 4 zł"_s, u"25"_s},
            {u"PLN 12 + 3"_s, u"15 PLN"_s},
            {u"10 CHF * 2"_s, u"20 CHF"_s},
            {u"(1/3) zł"_s, u"0,33 zł"_s},
            {u"1/3 zł"_s, u"0,33 zł"_s},                // (1/3) zł: `number / number factor`, as in v1
            {u"1 / (3 zł)"_s, u"0,3333333333"_s},      // a number per currency has no tag
        };
        for (const auto &[source, expected] : cases)
            QTest::addRow("%s", qUtf8Printable(source)) << source << expected;
    }
    void currencies()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(last(source), expected);
    }

    void currenciesThroughVariables()
    {
        QCOMPARE(results(u"x = 10 zł\nx * 3\ny = 2\nx * y"_s), (QStringList{u"10 zł"_s, u"30 zł"_s, u"2"_s, u"20 zł"_s}));
    }

    void currencyCodeAsVariable()
    {
        QCOMPARE(last(u"EUR = 4,30\n100 EUR"_s), u"430"_s);
        QCOMPARE(last(u"EUR = 4,30\nEUR 100"_s), u"430"_s);
    }

    // --- errors -------------------------------------------------------------------------------

    void errorsNeverThrow_data()
    {
        QTest::addColumn<QString>("source");
        for (const char16_t *s : {u"1 +", u"(1", u"2 3", u")", u"@", u"\"abc", u"f(", u"sin()", u"1 = 2", u"[1, 2", u"{", u"%",
                                  u"√", u"°", u"$", u"::", u"to", u"zł", u"((((((", u"]]]", u"\\", u"\u0000", u"😀", u"1e999",
                                  u"x =", u"= 5", u"f(x) =", u"a ? b", u"1 ? 2 :", u"km to", u"[", u"ąęść", u"5 mod",
                                  u"10 % % 3", u"!", u"2 ^ ^ 3", u"--5", u"jako % z", u"20% z"})
            QTest::addRow("%s", qUtf8Printable(QString::fromUtf16(s))) << QString::fromUtf16(s);
    }
    void errorsNeverThrow()
    {
        QFETCH(QString, source);
        Scope scope;
        const auto lines = evaluateCalc(source, scope);
        QCOMPARE(lines.size(), 1);
        const auto inlineResult = evaluateInline(source, Scope());
        Q_UNUSED(inlineResult);
    }

    void readableSyntaxErrors()
    {
        QCOMPARE(results(u"1 +\n(1\n2 3\nfoo(1)\n\"a\" * 2\n\"abc\n)\n@"_s),
                 (QStringList{u"!Niedokończone wyrażenie"_s, u"!Brakuje nawiasu „)”"_s, u"!Nieoczekiwany fragment „3”"_s,
                              u"!Nieznana funkcja: foo"_s, u"!Nie można zamienić „a” na liczbę"_s,
                              u"!Brakuje cudzysłowu zamykającego"_s, u"!Nieoczekiwany fragment „)”"_s,
                              u"!Nieoczekiwany fragment „@”"_s}));
        QCOMPARE(results(u"1 +\n(1"_s, En), (QStringList{u"!Unexpected end of expression"_s, u"!Missing “)”"_s}));
    }

    void continuesAfterErrors()
    {
        QCOMPARE(results(u"x = 1\nbad +\nx + 1"_s), (QStringList{u"1"_s, u"!Niedokończone wyrażenie"_s, u"2"_s}));
    }

    void bareUnitsShowNothing()
    {
        QCOMPARE(results(u"zł\nkm\n$\nPLN"_s), (QStringList{QString(), QString(), QString(), QString()}));
    }

    void notAFunction()
    {
        QCOMPARE(last(u"x = 2\nx(3)"_s), u"!„x” nie jest funkcją"_s);
        QCOMPARE(last(u"x = 2\nx(3)"_s, En), u"!“x” is not a function"_s);
    }

    void stringsKeepWhitespace()
    {
        QCOMPARE(inl(u"\"a  b\""_s), u"a  b"_s);
        QCOMPARE(last(u"s = \"zażółć   gęślą\"\ns"_s), u"zażółć   gęślą"_s);
        QCOMPARE(inl(u"concat(\"x\", \"  \", \"y\")"_s), u"x  y"_s);
        QCOMPARE(inl(u"concat(\"a\"; 1,5; \" \"; 2 km)"_s), u"a1,5 2 km"_s);
        QCOMPARE(inl(u"concat(\"a\", 1,5)"_s), u"a15"_s); // ',' separates arguments without ';'
        QCOMPARE(inl(u"'pojedyncze'"_s), u"pojedyncze"_s);
        QCOMPARE(inl(u"\"cytat \\\" w środku\""_s), u"cytat \" w środku"_s);
        QCOMPARE(inl(u"\"12\" * 2"_s), u"24"_s);
        QCOMPARE(inl(u"\"$ffffff$\" + 1"_s), u"!Nie można zamienić „$ffffff$” na liczbę"_s);
    }

    void lists()
    {
        QCOMPARE(inl(u"[1000, 2500.5]"_s, En), u"[1000, 2500.5]"_s);
        QCOMPARE(inl(u"[1000, 2500.5]"_s, Pl), u"[1000; 2500,5]"_s);
        QCOMPARE(inl(u"[10000, 2]"_s, Pl), nb(u"[10_000; 2]"_s));
        QCOMPARE(inl(u"12345"_s, En), u"12,345"_s);
        QCOMPARE(inl(u"sum([1, 2, 3])"_s), u"6"_s);
        QCOMPARE(inl(u"[1, 2] * 2"_s), u"!Zakres lub lista może być tylko argumentem funkcji"_s);
        QCOMPARE(inl(u"[]"_s), u"[]"_s);
    }

    // --- inline -------------------------------------------------------------------------------

    void inlineWithScope()
    {
        Scope scope;
        scope.set(u"cena"_s, Value::number(12.5));
        scope.set(u"ilość"_s, Value::number(3));
        QCOMPARE(inl(u"cena * 2"_s, Pl, scope), u"25"_s);
        QCOMPARE(inl(u"= 2 + 2"_s), u"4"_s);
        QCOMPARE(inl(u"ilość * 2"_s, En, scope), u"6"_s);
        QCOMPARE(evaluateInline(u"cena * 2", scope).numeric, std::optional<double>(25));
    }

    void inlineEmpty()
    {
        for (const char16_t *s : {u"", u"=", u"   ", u"= # komentarz"}) {
            const auto r = evaluateInline(QString::fromUtf16(s), Scope());
            QVERIFY(!r.hasValue());
            QVERIFY(!r.hasError());
        }
    }

    void inlineErrors()
    {
        QCOMPARE(inl(u"nope * 2"_s), u"!Nieznana zmienna: nope"_s);
        QCOMPARE(inl(u"1 +"_s, En), u"!Unexpected end of expression"_s);
    }

    void inlineDoesNotModifyScope()
    {
        Scope scope;
        scope.set(u"x"_s, Value::number(1));
        const auto r = evaluateInline(u"x = 5", scope);
        QCOMPARE(r.value, u"5"_s);
        QCOMPARE(r.assigned, u"x"_s);
        QCOMPARE(inl(u"g(a) = a"_s, Pl, scope), u"g(a)"_s);
        QCOMPARE(scope.names(), QStringList{u"x"_s});
        QCOMPARE(scope.number(u"x"_s), std::optional<double>(1));
    }

    void inlineKeepsPercentAndCurrency()
    {
        Scope scope;
        evaluateCalc(u"rabat = 15%\nkwota = 10 zł", scope);
        QCOMPARE(inl(u"100 - rabat"_s, Pl, scope), u"85"_s);
        QCOMPARE(inl(u"rabat"_s, Pl, scope), u"15%"_s);
        QCOMPARE(inl(u"kwota * 2"_s, Pl, scope), u"20 zł"_s);
        const Scope copy = scope;
        QCOMPARE(inl(u"kwota * 2"_s, Pl, copy), u"20 zł"_s);
        QCOMPARE(scope.formatted(u"rabat"_s, Pl), u"15%"_s);
        QCOMPARE(scope.number(u"rabat"_s), std::optional<double>(0.15));
    }

    void inlineHasNoAggregates()
    {
        Scope scope;
        scope.set(u"suma"_s, Value::number(7));
        QCOMPARE(inl(u"suma"_s, Pl, scope), u"7"_s);
        QCOMPARE(inl(u"sum(1, 2, 3)"_s), u"6"_s);
        QCOMPARE(inl(u"sum"_s), u"!Nieznana zmienna: sum"_s);
    }
};

QTEST_GUILESS_MAIN(TestCalcBlocks)
#include "tst_calc_blocks.moc"
