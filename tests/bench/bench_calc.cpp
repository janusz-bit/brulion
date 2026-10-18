// Benchmarks of the calc module: calc blocks (a household budget, a long block, one line with
// thousands of terms), inline calculations with the page scope (cached and uncached), GFM tables
// (parse, find in a large page, evaluate formulas with column references and A1 chains).
#include "benchdata.h"

#include "calc/calc.h"

#include <QTest>

using namespace brulion;
using namespace brulion::calc;
using namespace Qt::StringLiterals;

namespace {

QString budgetBlock()
{
    return u"czynsz = 2400 zł\n"
           u"prąd = 180,50 zł + 23%\n"
           u"gaz = 95 zł\n"
           u"internet = 60 zł\n"
           u"jedzenie = 450 zł * 4\n"
           u"dojazd = 320 km * 0,85 zł/km\n"
           u"rabat = 15%\n"
           u"abonament = 49,99 zł - rabat\n"
           u"suma\n"
           u"\n"
           u"oszczędności = 20% z 8500 zł\n"
           u"cel = 12000 zł\n"
           u"miesiące = ceil(cel / oszczędności)\n"
           u"bieg = 5 km / 25 min to km/h\n"
           u"temperatura = 20°C to °F\n"
           u"f(x) = x^2 + 2x + 1\n"
           u"f(12)\n"
           u"średnia\n"_s;
}

QString longBlock(int lines)
{
    QString s;
    for (int i = 0; i < lines; ++i)
        s += i % 10 == 9 ? u"suma\n"_s : u"pozycja%1 = %2,50 zł * %3\n"_s.arg(i).arg(i % 97 + 1).arg(i % 5 + 1);
    return s;
}

QString longExpression(int terms)
{
    QString s;
    for (int i = 0; i < terms; ++i)
        s += (i ? u" + "_s : QString()) + u"%1,5 * 2"_s.arg(i % 100);
    return s + u'\n';
}

QString tableWithFormulas(int rows, bool a1Chain)
{
    QString t = u"| Pozycja | Ilość | Cena | Razem |\n|---|--:|--:|--:|\n"_s;
    for (int i = 0; i < rows; ++i) {
        const QString formula = a1Chain && i > 0 ? u"=D%1+B%2*C%2"_s.arg(i).arg(i + 1) : u"=[Ilość]*[Cena]"_s;
        t += u"| Rzecz %1 | %2 | %3,50 zł | %4 |\n"_s.arg(i).arg(i % 9 + 1).arg(i % 50 + 1).arg(formula);
    }
    t += u"| Suma | =sum(above) | | =sum(above) |\n"_s;
    return t;
}

/// A page with `blocks` calc blocks between paragraphs and `key:: value` properties.
QString pageWithCalc(int blocks)
{
    bench::Rng r(3);
    QString page = u"---\nbudżet: 12000\n---\n# Budżet domowy\n\ncena:: 120\nilość:: 4\n\n"_s;
    for (int b = 0; b < blocks; ++b) {
        page += bench::sentence(r) + u"\n\n"_s;
        page += bench::calcBlock(r, 8) + u'\n';
    }
    bench::reportInput(u"pageWithCalc %1"_s.arg(blocks), page);
    return page;
}

} // namespace

class BenchCalc : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void evaluateBudgetBlock()
    {
        const QString source = budgetBlock();
        QBENCHMARK {
            Scope scope;
            const auto results = evaluateCalc(source, scope);
            Q_UNUSED(results);
        }
    }

    void evaluateLongBlock_data()
    {
        QTest::addColumn<int>("lines");
        for (int n : bench::sizes({100, 1000, 5000}, {100, 1000}))
            QTest::newRow(qPrintable(u"%1 lines"_s.arg(n))) << n;
    }
    void evaluateLongBlock()
    {
        QFETCH(int, lines);
        const QString source = longBlock(lines);
        QBENCHMARK {
            Scope scope;
            const auto results = evaluateCalc(source, scope);
            Q_UNUSED(results);
        }
    }

    // One line of n terms; a line is limited to 4000 characters (kMaxExpressionLength, about
    // 360 such terms), so the last row measures how fast an over-long line is rejected.
    void evaluateLongExpression_data()
    {
        QTest::addColumn<int>("terms");
        QTest::addColumn<bool>("rejected");
        for (int n : {50, 100, 350})
            QTest::newRow(qPrintable(u"%1 terms"_s.arg(n))) << n << false;
        QTest::newRow("1000 terms (over the length limit)") << 1000 << true;
    }
    void evaluateLongExpression()
    {
        QFETCH(int, terms);
        QFETCH(bool, rejected);
        const QString source = longExpression(terms);
        {
            Scope scope;
            const auto results = evaluateCalc(source, scope);
            QVERIFY(!results.isEmpty());
            QCOMPARE(!results.first().error.isEmpty(), rejected);
        }
        QBENCHMARK {
            Scope scope;
            const auto results = evaluateCalc(source, scope);
            Q_UNUSED(results);
        }
    }

    // One inline `= expr` as the renderer evaluates it: pageScope() (cached after the first call)
    // + evaluateInline().
    void inlineWithPageScope_data()
    {
        QTest::addColumn<int>("blocks");
        QTest::newRow("5 calc blocks") << 5;
        QTest::newRow("50 calc blocks") << 50;
    }
    void inlineWithPageScope()
    {
        QFETCH(int, blocks);
        const QString page = pageWithCalc(blocks);
        QBENCHMARK {
            const Scope scope = pageScope(page);
            const CalcLineResult r = evaluateInline(u"= cena * ilość + czynsz"_s, scope);
            Q_UNUSED(r);
        }
    }

    // The same page evaluated from scratch (what every edit of the page costs once).
    void pageCalcUncached_data() { inlineWithPageScope_data(); }
    void pageCalcUncached()
    {
        QFETCH(int, blocks);
        const QString page = pageWithCalc(blocks);
        QBENCHMARK {
            const PageCalc calc(page);
            const Scope scope = calc.scopeAt();
            Q_UNUSED(scope);
        }
    }

    void parseTable_data()
    {
        QTest::addColumn<int>("rows");
        for (int n : {10, 100, 1000})
            QTest::newRow(qPrintable(u"%1 rows"_s.arg(n))) << n;
    }
    void parseTable()
    {
        QFETCH(int, rows);
        const QString md = tableWithFormulas(rows, false);
        QBENCHMARK {
            const auto model = calc::parseTable(md);
            Q_UNUSED(model);
        }
    }

    void serializeTable_data() { parseTable_data(); }
    void serializeTable()
    {
        QFETCH(int, rows);
        const auto model = calc::parseTable(tableWithFormulas(rows, false));
        QVERIFY(model);
        QBENCHMARK {
            const QString s = calc::serializeTable(*model);
            Q_UNUSED(s);
        }
    }

    void evaluateTable_data()
    {
        QTest::addColumn<int>("rows");
        QTest::addColumn<bool>("chain");
        for (int n : {10, 100, 500}) {
            QTest::newRow(qPrintable(u"%1 rows [col] refs"_s.arg(n))) << n << false;
            QTest::newRow(qPrintable(u"%1 rows A1 chain"_s.arg(n))) << n << true;
        }
    }
    void evaluateTable()
    {
        QFETCH(int, rows);
        QFETCH(bool, chain);
        const auto model = calc::parseTable(tableWithFormulas(rows, chain));
        QVERIFY(model);
        QBENCHMARK {
            const TableEval e = calc::evaluateTable(*model);
            Q_UNUSED(e);
        }
    }

    // findTables / findCalcBlocks over a large page (every edit of a page with tables).
    void findTablesInPage_data()
    {
        QTest::addColumn<int>("kb");
        for (int n : bench::sizes({50, 1000}, {50, 300}))
            QTest::newRow(qPrintable(u"%1 KB"_s.arg(n))) << n;
    }
    void findTablesInPage()
    {
        QFETCH(int, kb);
        bench::Rng r(9);
        const QString page = bench::documentNote(r, 1, 100, qsizetype(kb) * 1024);
        QBENCHMARK {
            const auto tables = calc::findTables(page);
            const auto blocks = calc::findCalcBlocks(page);
            Q_UNUSED(tables);
            Q_UNUSED(blocks);
        }
    }
};

QTEST_GUILESS_MAIN(BenchCalc)
#include "bench_calc.moc"
