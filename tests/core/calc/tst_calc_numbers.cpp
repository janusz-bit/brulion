// Number formatting and parsing (ported from the v1 numbers.test.ts).
#include "calc/calc.h"
#include "calc/numbers.h"

#include "calctest.h"

#include <QTest>

#include <cmath>
#include <limits>

using namespace brulion::calc;
using namespace Qt::StringLiterals;
using calctest::En;
using calctest::nb;
using calctest::Pl;

namespace {
const Currency kPln{QStringLiteral("PLN"), QStringLiteral("zł"), false};
const Currency kUsd{QStringLiteral("USD"), QStringLiteral("$"), true};
const Currency kEur{QStringLiteral("EUR"), QStringLiteral("€"), false};
} // namespace

class TestCalcNumbers : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void formatNumber_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("pl");
        QTest::addColumn<QString>("en");
        QTest::newRow("0") << 0.0 << u"0"_s << u"0"_s;
        QTest::newRow("-0") << -0.0 << u"0"_s << u"0"_s;
        QTest::newRow("5") << 5.0 << u"5"_s << u"5"_s;
        QTest::newRow("0.5") << 0.5 << u"0,5"_s << u"0.5"_s;
        QTest::newRow("1/3") << 1.0 / 3 << u"0,3333333333"_s << u"0.3333333333"_s;
        QTest::newRow("2/3") << 2.0 / 3 << u"0,6666666667"_s << u"0.6666666667"_s;
        QTest::newRow("0.1+0.2") << 0.1 + 0.2 << u"0,3"_s << u"0.3"_s;
        QTest::newRow("1.1*3") << 1.1 * 3 << u"3,3"_s << u"3.3"_s;
        QTest::newRow("1234") << 1234.0 << u"1234"_s << u"1,234"_s;
        QTest::newRow("9999") << 9999.0 << u"9999"_s << u"9,999"_s;
        QTest::newRow("10000") << 10000.0 << nb(u"10_000"_s) << u"10,000"_s;
        QTest::newRow("12345.678") << 12345.678 << nb(u"12_345,678"_s) << u"12,345.678"_s;
        QTest::newRow("-1234567.891") << -1234567.891 << nb(u"-1_234_567,891"_s) << u"-1,234,567.891"_s;
        QTest::newRow("123456789012") << 123456789012.0 << nb(u"123_456_789_012"_s) << u"123,456,789,012"_s;
        QTest::newRow("mile") << 3.1068559611866697 << u"3,106855961"_s << u"3.106855961"_s;
        QTest::newRow("9.99999999999") << 9.99999999999 << u"10"_s << u"10"_s;
        QTest::newRow("1e-6") << 0.000001 << u"0,000001"_s << u"0.000001"_s;
        QTest::newRow("0.05") << 0.05 << u"0,05"_s << u"0.05"_s;
        QTest::newRow("1000") << 1000.0 << u"1000"_s << u"1,000"_s;
        QTest::newRow("1e-7") << 1e-7 << u"1e-7"_s << u"1e-7"_s;
        QTest::newRow("-1.25e-7") << -1.25e-7 << u"-1,25e-7"_s << u"-1.25e-7"_s;
        QTest::newRow("1e15") << 1e15 << u"1e15"_s << u"1e15"_s;
        QTest::newRow("1.5e21") << 1.5e21 << u"1,5e21"_s << u"1.5e21"_s;
        QTest::newRow("999999999999999") << 999999999999999.0 << nb(u"999_999_999_999_999"_s)
                                         << u"999,999,999,999,999"_s;
    }
    void formatNumber()
    {
        QFETCH(double, value);
        QFETCH(QString, pl);
        QFETCH(QString, en);
        QCOMPARE(brulion::calc::formatNumber(value, Pl), pl);
        QCOMPARE(brulion::calc::formatNumber(value, En), en);
    }

    void formatNonFinite()
    {
        QCOMPARE(brulion::calc::formatNumber(std::nan(""), Pl), u"NaN"_s);
        QCOMPARE(brulion::calc::formatNumber(std::numeric_limits<double>::infinity(), Pl), u"∞"_s);
        QCOMPARE(brulion::calc::formatNumber(-std::numeric_limits<double>::infinity(), En), u"-∞"_s);
    }

    void formatWithoutGrouping()
    {
        QCOMPARE(brulion::calc::formatNumber(12345.5, En, {false}), u"12345.5"_s);
        QCOMPARE(brulion::calc::formatNumber(12345.5, Pl, {false}), u"12345,5"_s);
    }

    void formatFixed()
    {
        QCOMPARE(brulion::calc::formatFixed(12.5, 2, Pl), u"12,50"_s);
        QCOMPARE(brulion::calc::formatFixed(12345.5, 2, Pl), nb(u"12_345,50"_s));
        QCOMPARE(brulion::calc::formatFixed(-0.001, 2, Pl), u"0,00"_s);
        QCOMPARE(brulion::calc::formatFixed(std::numeric_limits<double>::infinity(), 2, Pl), u"∞"_s);
        QCOMPARE(brulion::calc::formatFixed(0.125, 2, En), u"0.13"_s); // ties round up like JS toFixed
    }

    void formatUnitNumber_data()
    {
        QTest::addColumn<double>("value");
        QTest::addColumn<QString>("pl");
        QTest::addColumn<QString>("en");
        QTest::newRow("miles") << 3.106855961 << u"3,11"_s << u"3.11"_s;
        QTest::newRow("hours") << 10.0 / 3 << u"3,33"_s << u"3.33"_s;
        QTest::newRow("trimmed") << 1.5 << u"1,5"_s << u"1.5"_s;
        QTest::newRow("integer") << 48.0 << u"48"_s << u"48"_s;
        QTest::newRow("rounds to integer") << 2.999 << u"3"_s << u"3"_s;
        QTest::newRow("negative") << -3.106855961 << u"-3,11"_s << u"-3.11"_s;
        QTest::newRow("grouped") << 12345.678 << nb(u"12_345,68"_s) << u"12,345.68"_s;
        QTest::newRow("below one") << 1.0 / 3 << u"0,33"_s << u"0.33"_s;
        QTest::newRow("small") << 0.0042 << u"0,0042"_s << u"0.0042"_s;
        QTest::newRow("small rounded") << 0.0166666 << u"0,017"_s << u"0.017"_s;
        QTest::newRow("zero") << 0.0 << u"0"_s << u"0"_s;
        QTest::newRow("negative zero") << -0.001 * 0 << u"0"_s << u"0"_s;
        QTest::newRow("huge") << 1.5e21 << u"1,5e21"_s << u"1.5e21"_s;
    }

    void formatUnitNumber()
    {
        QFETCH(double, value);
        QFETCH(QString, pl);
        QFETCH(QString, en);
        QCOMPARE(brulion::calc::formatUnitNumber(value, Pl), pl);
        QCOMPARE(brulion::calc::formatUnitNumber(value, En), en);
    }

    void unitValuesKeepPrecision()
    {
        // Only the display is rounded: 5 km to mi and back is 5 km again.
        brulion::calc::Scope scope;
        const auto lines = brulion::calc::evaluateCalc(u"d = 5 km to mi\nd\nd to km"_s, scope, Pl);
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[1].value, u"3,11 mi"_s);
        QCOMPARE(lines[2].value, u"5 km"_s);
    }

    void formatMoney()
    {
        QCOMPARE(brulion::calc::formatMoney(17, kPln, Pl), u"17 zł"_s);
        QCOMPARE(brulion::calc::formatMoney(12.5, kPln, Pl), u"12,50 zł"_s);
        QCOMPARE(brulion::calc::formatMoney(1.0 / 3, kPln, Pl), u"0,33 zł"_s);
        QCOMPARE(brulion::calc::formatMoney(17, kUsd, En), u"$17"_s);
        QCOMPARE(brulion::calc::formatMoney(-5.5, kUsd, En), u"-$5.50"_s);
        QCOMPARE(brulion::calc::formatMoney(1234.5, kEur, En), u"1,234.50 €"_s);
        QCOMPARE(brulion::calc::formatMoney(25000, kPln, Pl), nb(u"25_000 zł"_s));
        QCOMPARE(brulion::calc::formatMoney(0.999, kPln, Pl), u"1 zł"_s);
    }

    void parse_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<double>("value");
        QTest::newRow("int") << u"12"_s << 12.0;
        QTest::newRow("comma") << u"12,5"_s << 12.5;
        QTest::newRow("dot") << u"12.5"_s << 12.5;
        QTest::newRow("minus") << u"-3"_s << -3.0;
        QTest::newRow("unicode minus") << u"−5"_s << -5.0;
        QTest::newRow("plus") << u"+7"_s << 7.0;
        QTest::newRow(",5") << u",5"_s << 0.5;
        QTest::newRow("1e3") << u"1e3"_s << 1000.0;
        QTest::newRow("1,5e3") << u"1,5e3"_s << 1500.0;
        QTest::newRow("space group") << u"12 000"_s << 12000.0;
        QTest::newRow("nbsp group") << nb(u"12_000"_s) << 12000.0;
        QTest::newRow("group + decimals") << u"12 000,25"_s << 12000.25;
        QTest::newRow("dot groups + comma") << u"1.234,56"_s << 1234.56;
        QTest::newRow("dot groups") << u"1.000.000"_s << 1000000.0;
        QTest::newRow("bold") << u"**12**"_s << 12.0;
        QTest::newRow("code") << u"`7`"_s << 7.0;
    }
    void parse()
    {
        QFETCH(QString, text);
        QFETCH(double, value);
        const auto parsed = parseLocaleNumber(text, Pl);
        QVERIFY(parsed);
        QCOMPARE(parsed->value, value);
    }

    void parseByLocale()
    {
        QCOMPARE(parseLocaleNumber(u"1,234", Pl)->value, 1.234);
        QCOMPARE(parseLocaleNumber(u"1,234", En)->value, 1234.0);
        QCOMPARE(parseLocaleNumber(u"1,234.56", En)->value, 1234.56);
        QCOMPARE(parseLocaleNumber(u"1,234,567", En)->value, 1234567.0);
        QCOMPARE(parseLocaleNumber(u"12,5", En)->value, 12.5);
    }

    void parseCurrencies()
    {
        auto p = parseLocaleNumber(u"1 234,50 zł", Pl);
        QVERIFY(p);
        QCOMPARE(p->value, 1234.5);
        QCOMPARE(p->currency, kPln);
        p = parseLocaleNumber(u"€12.50", Pl);
        QCOMPARE(p->value, 12.5);
        QCOMPARE(p->currency, (Currency{u"EUR"_s, u"€"_s, true}));
        QCOMPARE(parseLocaleNumber(u"12 PLN", Pl)->currency.code, u"PLN"_s);
        QCOMPARE(parseLocaleNumber(u"PLN 12", Pl)->currency, (Currency{u"PLN"_s, u"PLN"_s, true}));
        QCOMPARE(parseLocaleNumber(u"-12,50 zł", Pl)->value, -12.5);
        p = parseLocaleNumber(u"-$5", Pl);
        QCOMPARE(p->value, -5.0);
        QCOMPARE(p->currency, kUsd);
        QCOMPARE(parseLocaleNumber(u"$-5", Pl)->value, -5.0);
        QCOMPARE(parseLocaleNumber(u"7 CHF", Pl)->currency.code, u"CHF"_s);
        QCOMPARE(parseLocaleNumber(u"£3", Pl)->currency.code, u"GBP"_s);
    }

    void parsePercent()
    {
        const auto p = parseLocaleNumber(u"23%", Pl);
        QVERIFY(p && p->percent);
        QCOMPARE(p->value, 0.23);
        QVERIFY(!p->currency.isValid());
        QVERIFY(qFuzzyCompare(parseLocaleNumber(u"12,5 %", Pl)->value, 0.125));
    }

    void parseRejects_data()
    {
        QTest::addColumn<QString>("text");
        for (const char16_t *s : {u"", u"   ", u"abc", u"12 abc", u"1 2", u"5 zł 3", u"zł", u"1,2,3", u"12..5", u"--5",
                                  u"1 0000", u"0x1F", u"ąę", u"1e", u"%", u"."})
            QTest::addRow("%s", qUtf8Printable(QString::fromUtf16(s))) << QString::fromUtf16(s);
    }
    void parseRejects()
    {
        QFETCH(QString, text);
        QVERIFY(!parseLocaleNumber(text, Pl));
    }

    void stripMarkup()
    {
        QCOMPARE(stripInlineMarkup(u"**_12_**"), u"12"_s);
        QCOMPARE(stripInlineMarkup(u"~~usunięte~~"), u"usunięte"_s);
        QCOMPARE(stripInlineMarkup(u"a*b"), u"a*b"_s);
        QCOMPARE(stripInlineMarkup(u"  **żółć**  "), u"żółć"_s);
        QCOMPARE(stripInlineMarkup(u"**"), u"**"_s);
    }

    void currencyCodes()
    {
        QCOMPARE(currencyCode(u"zł"), u"PLN"_s);
        QCOMPARE(currencyCode(u"€"), u"EUR"_s);
        QCOMPARE(currencyCode(u"$"), u"USD"_s);
        QCOMPARE(currencyCode(u"£"), u"GBP"_s);
        QCOMPARE(currencyCode(u"CHF"), u"CHF"_s);
        QVERIFY(currencyCode(u"zl").isEmpty());
        QVERIFY(currencyCode(u"pln").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCalcNumbers)
#include "tst_calc_numbers.moc"
