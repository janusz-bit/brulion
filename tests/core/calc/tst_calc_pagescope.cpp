// Page scopes and calc block discovery (ported from the v1 scope.test.ts).
#include "calc/calc.h"

#include "calctest.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <atomic>

using namespace brulion::calc;
using namespace Qt::StringLiterals;
using calctest::En;
using calctest::inl;
using calctest::Pl;

namespace {

const QString kPage = QStringList{
    u"---"_s,                    // 0
    u"title: Budżet domowy"_s,   // 1
    u"vat: 23%"_s,               // 2
    u"kurs-eur: 4,30"_s,         // 3
    u"tags: [dom]"_s,            // 4
    u"---"_s,                    // 5
    u"cena:: 100 zł"_s,          // 6
    u"- ilość:: 3"_s,            // 7
    u"```calc"_s,                // 8
    u"razem = cena * ilość"_s,   // 9
    u"```"_s,                    // 10
    u"Tekst `= razem + vat`"_s,  // 11
    u"```js"_s,                  // 12
    u"x:: 5"_s,                  // 13
    u"```"_s,                    // 14
    u"```calc"_s,                // 15
    u"po = 1"_s,                 // 16
    u"```"_s,                    // 17
}.join(u'\n');

std::optional<double> num(const Scope &s, const QString &name)
{
    return s.number(name);
}

} // namespace

class TestCalcPageScope : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void collectsEverything()
    {
        const Scope scope = pageScope(kPage);
        QCOMPARE(num(scope, u"vat"_s), std::optional<double>(0.23));
        QCOMPARE(num(scope, u"kurs_eur"_s), std::optional<double>(4.3));
        QCOMPARE(num(scope, u"cena"_s), std::optional<double>(100));
        QCOMPARE(num(scope, u"ilość"_s), std::optional<double>(3));
        QCOMPARE(num(scope, u"razem"_s), std::optional<double>(300));
        QCOMPARE(num(scope, u"po"_s), std::optional<double>(1));
        QVERIFY(!scope.contains(u"title"_s));
        QVERIFY(!scope.contains(u"tags"_s));
        QVERIFY(!scope.contains(u"x"_s));
    }

    void feedsInlineCalculations()
    {
        const Scope scope = pageScope(kPage, 11);
        QCOMPARE(inl(u"razem + vat"_s, Pl, scope), u"369 zł"_s);
        QCOMPARE(inl(u"kurs_eur * 10"_s, Pl, scope), u"43"_s);
        QCOMPARE(scope.formatted(u"vat"_s, Pl), u"23%"_s);
        QCOMPARE(scope.formatted(u"cena"_s, Pl), u"100 zł"_s);
    }

    void onlyBlocksEndingBeforeTheLine()
    {
        QVERIFY(!pageScope(kPage, 11).contains(u"po"_s));
        QVERIFY(!pageScope(kPage, 10).contains(u"razem"_s));
        QCOMPARE(num(pageScope(kPage, 11), u"razem"_s), std::optional<double>(300));
        QVERIFY(!pageScope(kPage, 7).contains(u"ilość"_s));
        QCOMPARE(num(pageScope(kPage, 0), u"vat"_s), std::optional<double>(0.23));
    }

    void documentOrderLaterWins()
    {
        const Scope scope = pageScope(u"a:: 1\n```calc\nb = a + 1\na = 10\n```\n```calc\nc = a + b\n```");
        QCOMPARE(num(scope, u"a"_s), std::optional<double>(10));
        QCOMPARE(num(scope, u"b"_s), std::optional<double>(2));
        QCOMPARE(num(scope, u"c"_s), std::optional<double>(12));
    }

    void unclosedFences()
    {
        QCOMPARE(num(pageScope(u"```calc\na = 1"), u"a"_s), std::optional<double>(1));
        QVERIFY(!pageScope(u"```calc\na = 1", 1).contains(u"a"_s));
        QVERIFY(!pageScope(u"```calc\na = 1", 100).contains(u"a"_s));
    }

    void fenceVariants()
    {
        QCOMPARE(num(pageScope(u"- ```calc\n  a = 2\n  ```"), u"a"_s), std::optional<double>(2));
        QCOMPARE(num(pageScope(u"~~~calc\nb = 3\n~~~"), u"b"_s), std::optional<double>(3));
        QCOMPARE(num(pageScope(u"```Calc title\nc = 4\n```"), u"c"_s), std::optional<double>(4));
        QCOMPARE(num(pageScope(u"````calc\nd = 5\n```\nd = 6\n````"), u"d"_s), std::optional<double>(6));
        QCOMPARE(num(pageScope(u"1. ```calc\n   e = 7\n   ```"), u"e"_s), std::optional<double>(7));
        QCOMPARE(num(pageScope(u"```calc\r\nf = 8\r\n```\r\n"), u"f"_s), std::optional<double>(8));
    }

    void otherFencesAndBrokenLines()
    {
        const Scope scope = pageScope(u"```python\nx = 1\n```\n```calc\ny = 1\nbad +\nz = y + 1\n```");
        QVERIFY(!scope.contains(u"x"_s));
        QCOMPARE(num(scope, u"y"_s), std::optional<double>(1));
        QCOMPARE(num(scope, u"z"_s), std::optional<double>(2));
    }

    void propertyValues()
    {
        const Scope scope = pageScope(
            u"---\nkwota: \"1 234,50 zł\"\nliczba: 7\nnazwa: abc\n---\nstawka-godzinowa:: 85,5\nopis:: tekst");
        QCOMPARE(num(scope, u"kwota"_s), std::optional<double>(1234.5));
        QCOMPARE(scope.formatted(u"kwota"_s, Pl), u"1234,50 zł"_s);
        QCOMPARE(num(scope, u"liczba"_s), std::optional<double>(7));
        QCOMPARE(num(scope, u"stawka_godzinowa"_s), std::optional<double>(85.5));
        QVERIFY(!scope.contains(u"nazwa"_s));
        QVERIFY(!scope.contains(u"opis"_s));
    }

    void locale()
    {
        QCOMPARE(num(pageScope(u"x:: 1,500", -1, En), u"x"_s), std::optional<double>(1500));
        QCOMPARE(num(pageScope(u"x:: 1,500", -1, Pl), u"x"_s), std::optional<double>(1.5));
    }

    void unterminatedFrontmatter()
    {
        const Scope scope = pageScope(u"---\na: 1\nb:: 2");
        QCOMPARE(num(scope, u"b"_s), std::optional<double>(2));
        QVERIFY(!scope.contains(u"a"_s));
    }

    void neverThrows()
    {
        for (const char16_t *page : {u"", u"---", u"```calc", u"```calc\n```", u"a:: ", u"::", u"- ```calc\n  1 +\n  ```",
                                     u"---\n---", u"```\n```calc\n```", u"x::5", u"żółć:: 3,5 zł"})
            pageScope(QString::fromUtf16(page));
        QCOMPARE(num(pageScope(u"żółć:: 3,5 zł"), u"żółć"_s), std::optional<double>(3.5));
    }

    void lowerCaseAliases()
    {
        const Scope scope = pageScope(u"Cena:: 5\nVAT-Stawka:: 23%");
        QCOMPARE(num(scope, u"Cena"_s), std::optional<double>(5));
        QCOMPARE(num(scope, u"cena"_s), std::optional<double>(5));
        QCOMPARE(num(scope, u"VAT_Stawka"_s), std::optional<double>(0.23));
        QCOMPARE(num(scope, u"vat_stawka"_s), std::optional<double>(0.23));
        QCOMPARE(inl(u"cena * (1 + vat_stawka)"_s, Pl, scope), u"6,15"_s);
        const Scope both = pageScope(u"cena:: 1\nCena:: 2");
        QCOMPARE(num(both, u"cena"_s), std::optional<double>(1));
        QCOMPARE(num(both, u"Cena"_s), std::optional<double>(2));
        const Scope updated = pageScope(u"Cena:: 1\nCena:: 2");
        QCOMPARE(num(updated, u"cena"_s), std::optional<double>(2));
    }

    void everyLineMatchesAFreshEvaluation()
    {
        const QString page = QStringList{u"a:: 1"_s, u"```calc"_s, u"b = a + 1"_s, u"```"_s, u"c:: 3"_s, u"```calc"_s,
                                         u"a = 10"_s, u"d = a + b"_s, u"```"_s, u"```calc"_s, u"e = 5"_s}
                                 .join(u'\n');
        QCOMPARE(pageScope(page, 0).names(), QStringList());
        QCOMPARE(pageScope(page, 1).names(), QStringList{u"a"_s});
        QCOMPARE(pageScope(page, 3).names(), QStringList{u"a"_s});
        QCOMPARE(pageScope(page, 4).names(), (QStringList{u"a"_s, u"ans"_s, u"b"_s}));
        QCOMPARE(pageScope(page, 5).names(), (QStringList{u"a"_s, u"ans"_s, u"b"_s, u"c"_s}));
        const Scope at9 = pageScope(page, 9);
        QCOMPARE(at9.names(), (QStringList{u"a"_s, u"ans"_s, u"b"_s, u"c"_s, u"d"_s}));
        QCOMPARE(num(at9, u"a"_s), std::optional<double>(10));
        QCOMPARE(num(at9, u"ans"_s), std::optional<double>(12));
        QCOMPARE(pageScope(page, 100), at9);
        const Scope all = pageScope(page);
        QCOMPARE(num(all, u"ans"_s), std::optional<double>(5));
        QCOMPARE(num(all, u"e"_s), std::optional<double>(5));
        // The same through PageCalc directly.
        const PageCalc calc(page);
        QCOMPARE(calc.scopeAt(9), at9);
        QCOMPARE(calc.scopeAt(), all);
    }

    void cachedPagesAreIndependent()
    {
        const QString page = u"```calc\nx = 2 zł\n```"_s;
        Scope a = pageScope(page);
        a.set(u"x"_s, Value::number(99));
        const Scope b = pageScope(page);
        QCOMPARE(num(b, u"x"_s), std::optional<double>(2));
        QCOMPARE(inl(u"x * 2"_s, Pl, b), u"4 zł"_s);
        QVERIFY(!pageScope(page, 0).contains(u"x"_s));
        QCOMPARE(num(pageScope(page, -1, En), u"x"_s), std::optional<double>(2));
    }

    void manyInlinePositionsAreCheap()
    {
        QStringList parts;
        for (int b = 0; b < 40; ++b) {
            parts << u"```calc"_s;
            for (int k = 0; k < 8; ++k)
                parts << u"v%1_%2 = %2 * 2 + %1"_s.arg(b).arg(k);
            parts << u"```"_s << u"tekst `= v0_1`"_s;
        }
        const QString page = parts.join(u'\n');
        QElapsedTimer timer;
        timer.start();
        pageScope(page, 5);
        const qint64 first = timer.elapsed();
        timer.restart();
        for (int line = 0; line < parts.size(); ++line)
            pageScope(page, line);
        const qint64 all = timer.elapsed();
        QVERIFY2(all < first * 20 + 500, qPrintable(u"first %1 ms, all %2 ms"_s.arg(first).arg(all)));
        const Scope scope = pageScope(page, static_cast<int>(parts.size()));
        QCOMPARE(num(scope, u"v39_7"_s), std::optional<double>(53));
        QCOMPARE(num(scope, u"v0_1"_s), std::optional<double>(2));
    }

    void thousandsOfProperties()
    {
        QStringList lines;
        for (int i = 0; i < 5000; ++i)
            lines << u"- blok %1"_s.arg(i) << u"  wartosc_%1:: %1"_s.arg(i);
        const QString page = lines.join(u'\n');
        QElapsedTimer timer;
        timer.start();
        const PageCalc calc(page);
        QCOMPARE(calc.scopeAt().number(u"wartosc_4999"_s), std::optional<double>(4999));
        QCOMPARE(calc.scopeAt(3).names(), QStringList{u"wartosc_0"_s});
        QVERIFY(timer.elapsed() < 10000);
    }

    void manyBlocksAndPropertiesScale()
    {
        // Each block end records only what the block assigned (not every variable of the page).
        QStringList lines;
        for (int i = 0; i < 5000; ++i)
            lines << u"- blok %1"_s.arg(i) << u"  wartosc_%1:: %1"_s.arg(i);
        for (int b = 0; b < 1000; ++b)
            lines << u"```calc"_s << u"v%1 = %1 * 2; w%1 = 1"_s.arg(b) << u"```"_s;
        const QString page = lines.join(u'\n');
        QElapsedTimer timer;
        timer.start();
        const PageCalc calc(page);
        QVERIFY2(timer.elapsed() < 4000 * BRULION_TEST_SLOWDOWN, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        const Scope all = calc.scopeAt();
        QCOMPARE(all.number(u"v999"_s), std::optional<double>(1998));
        QCOMPARE(all.number(u"w999"_s), std::optional<double>(1)); // not only the last assignment
        QCOMPARE(all.number(u"ans"_s), std::optional<double>(1));
        QCOMPARE(all.size(), 5000 + 2000 + 1);
        const Scope early = calc.scopeAt(10000 + 4); // after the first block
        QCOMPARE(early.number(u"w0"_s), std::optional<double>(1));
        QVERIFY(!early.contains(u"v1"_s));
    }

    void concurrentCallsAreSafe()
    {
        // The page cache is shared by all threads (e.g. rendering on a worker thread).
        QList<QString> pages;
        for (int p = 0; p < 24; ++p)
            pages << u"cena:: %1\n```calc\nx = cena * 2\nf(a) = a + x\n```\n`= f(1)`"_s.arg(p);
        std::atomic<int> wrong{0};
        QList<QThread *> threads;
        for (int t = 0; t < 4; ++t) {
            threads << QThread::create([&pages, &wrong, t] {
                for (int i = 0; i < 300; ++i) {
                    const int p = (i * 7 + t) % pages.size();
                    const Scope scope = pageScope(pages.at(p), 5);
                    if (inl(u"f(1)"_s, Pl, scope) != QString::number(2 * p + 1))
                        ++wrong;
                }
            });
            threads.last()->start();
        }
        for (QThread *thread : threads) {
            QVERIFY(thread->wait(60000));
            delete thread;
        }
        QCOMPARE(wrong.load(), 0);
    }

    // --- findCalcBlocks -----------------------------------------------------------------------

    void findsBlocks()
    {
        const QList<CalcBlock> blocks = findCalcBlocks(kPage);
        QCOMPARE(blocks.size(), 2);
        QCOMPARE(blocks[0], (CalcBlock{8, 10, 9, u"razem = cena * ilość"_s, true}));
        QCOMPARE(blocks[1], (CalcBlock{15, 17, 16, u"po = 1"_s, true}));
    }

    void removesFenceIndentation()
    {
        QCOMPARE(findCalcBlocks(u"- ```calc\n  a = 1\n    b = 2\n  ```")[0].source, u"a = 1\n  b = 2"_s);
    }

    void reportsUnclosedBlocks()
    {
        const QList<CalcBlock> blocks = findCalcBlocks(u"x\n```calc\n1 + 1\n");
        QCOMPARE(blocks.size(), 1);
        QCOMPARE(blocks[0], (CalcBlock{1, 3, 2, u"1 + 1\n"_s, false}));
    }

    void ignoresInlineBackticksAndOtherLanguages()
    {
        QVERIFY(findCalcBlocks(u"```calc` nope\n```js\n```calc\n```").isEmpty());
        QVERIFY(findCalcBlocks(u"``calc\n1\n``").isEmpty());
        QVERIFY(findCalcBlocks(u"```calculator\n1\n```").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCalcPageScope)
#include "tst_calc_pagescope.moc"
