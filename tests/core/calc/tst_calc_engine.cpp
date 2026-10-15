// The expression engine: tokenizer, operators and precedence, functions, units API and
// resource limits (ported from the v1 lexer/transform/engine/guard tests where they apply).
#include "calc/calc.h"
#include "calc/lexer_p.h"
#include "calc/util_p.h"

#include "calctest.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <climits>
#include <functional>

using namespace brulion::calc;
using namespace Qt::StringLiterals;
using calctest::En;
using calctest::inl;
using calctest::last;
using calctest::nb;
using calctest::Pl;

namespace {

/// "kind:text" of every token after number normalisation.
QStringList tokens(const QString &src, CalcLocale locale = CalcLocale::Polish)
{
    QList<Token> list = lex(src);
    normalizeNumbers(list, src, locale);
    QStringList out;
    for (const Token &t : list) {
        QString kind;
        switch (t.kind) {
        case TokenKind::Number: kind = u"num"_s; break;
        case TokenKind::Ident: kind = u"id"_s; break;
        case TokenKind::String: kind = u"str"_s; break;
        case TokenKind::Operator: kind = u"op"_s; break;
        case TokenKind::LParen:
        case TokenKind::RParen:
        case TokenKind::LBracket:
        case TokenKind::RBracket: kind = u"br"_s; break;
        case TokenKind::Comma: kind = u"comma"_s; break;
        case TokenKind::Semicolon: kind = u"semi"_s; break;
        case TokenKind::Currency: kind = u"cur"_s; break;
        case TokenKind::Superscript: kind = u"sup"_s; break;
        case TokenKind::Unknown: kind = u"?"_s; break;
        }
        out.append(kind + u':' + t.text);
    }
    return out;
}

void addCases(const QList<std::pair<QString, QString>> &cases)
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("expected");
    for (const auto &[source, expected] : cases)
        QTest::addRow("%s", qUtf8Printable(source)) << source << expected;
}

} // namespace

class TestCalcEngine : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // --- tokenizer ------------------------------------------------------------------------------

    void lexSimple()
    {
        QCOMPARE(tokens(u"cena * 2"_s), (QStringList{u"id:cena"_s, u"op:*"_s, u"num:2"_s}));
        QCOMPARE(tokens(u"ilość + żółć_2"_s), (QStringList{u"id:ilość"_s, u"op:+"_s, u"id:żółć_2"_s}));
        QCOMPARE(tokens(u"1e5 + .5 + 1.25 + 2E-3"_s),
                 (QStringList{u"num:1e5"_s, u"op:+"_s, u"num:.5"_s, u"op:+"_s, u"num:1.25"_s, u"op:+"_s, u"num:2E-3"_s}));
        QCOMPARE(tokens(u"2em"_s), (QStringList{u"num:2"_s, u"id:em"_s}));
    }

    void lexComments()
    {
        QCOMPARE(tokens(u"2 + 3 # suma"_s), (QStringList{u"num:2"_s, u"op:+"_s, u"num:3"_s}));
        QCOMPARE(tokens(u"2 + 3 // suma"_s), (QStringList{u"num:2"_s, u"op:+"_s, u"num:3"_s}));
        QCOMPARE(tokens(u"\"a#b\" + 'c//d'"_s), (QStringList{u"str:a#b"_s, u"op:+"_s, u"str:c//d"_s}));
        QCOMPARE(tokens(u"6 / 3"_s), (QStringList{u"num:6"_s, u"op:/"_s, u"num:3"_s}));
    }

    void lexSymbols()
    {
        QCOMPARE(tokens(u"2 × 3 ÷ 4 − 1"_s),
                 (QStringList{u"num:2"_s, u"op:*"_s, u"num:3"_s, u"op:/"_s, u"num:4"_s, u"op:-"_s, u"num:1"_s}));
        QCOMPARE(tokens(u"a ≤ b ≥ c ≠ d <> e ** f"_s),
                 (QStringList{u"id:a"_s, u"op:<="_s, u"id:b"_s, u"op:>="_s, u"id:c"_s, u"op:!="_s, u"id:d"_s,
                              u"op:!="_s, u"id:e"_s, u"op:^"_s, u"id:f"_s}));
        QCOMPARE(tokens(u"m²"_s), (QStringList{u"id:m"_s, u"sup:²"_s}));
        QCOMPARE(tokens(u"20°C 90° 5℃ ∞ 2π"_s),
                 (QStringList{u"num:20"_s, u"id:°C"_s, u"num:90"_s, u"id:°"_s, u"num:5"_s, u"id:°C"_s, u"id:∞"_s,
                              u"num:2"_s, u"id:π"_s}));
        QCOMPARE(tokens(u"$12 + 5€ + £1"_s),
                 (QStringList{u"cur:$"_s, u"num:12"_s, u"op:+"_s, u"num:5"_s, u"cur:€"_s, u"op:+"_s, u"cur:£"_s, u"num:1"_s}));
        QCOMPARE(tokens(u"2 @ 3 😀"_s), (QStringList{u"num:2"_s, u"?:@"_s, u"num:3"_s, u"?:😀"_s}));
        QCOMPARE(tokens(u"a && b || c"_s), (QStringList{u"id:a"_s, u"op:and"_s, u"id:b"_s, u"op:or"_s, u"id:c"_s}));
    }

    void lexStrings()
    {
        const QList<Token> t = lex(u"\"abc");
        QCOMPARE(t.size(), 1);
        QVERIFY(t[0].unterminated);
        QCOMPARE(t[0].text, u"abc"_s);
        QVERIFY(!lex(u"\"a\\\"b\"")[0].unterminated);
        QCOMPARE(lex(u"\"a\\\"b\"")[0].text, u"a\"b"_s);
    }

    void lexApostrophes()
    {
        // An apostrophe right after a word is not a string quote ("Jan's"), as in the v1 engine.
        QCOMPARE(tokens(u"Jan's share"_s), (QStringList{u"id:Jan"_s, u"?:'"_s, u"id:s"_s, u"id:share"_s}));
        QCOMPARE(tokens(u"x' + 1"_s), (QStringList{u"id:x"_s, u"?:'"_s, u"op:+"_s, u"num:1"_s}));
        QCOMPARE(tokens(u"concat('a','b')"_s),
                 (QStringList{u"id:concat"_s, u"br:("_s, u"str:a"_s, u"comma:,"_s, u"str:b"_s, u"br:)"_s}));
        QCOMPARE(tokens(u"x = 'tekst'"_s), (QStringList{u"id:x"_s, u"op:="_s, u"str:tekst"_s}));
        QCOMPARE(inl(u"concat('a', 'b')"_s), u"ab"_s);
        QCOMPARE(inl(u"x'"_s), u"!Nieoczekiwany fragment „'”"_s);
    }

    void lexNeverFails()
    {
        for (const char16_t *s : {u"", u"((((", u"]]]", u"\"", u"'", u"\\", u"#", u"//", u"⁻", u"°", u"\xd800"})
            lex(QString::fromUtf16(s));
    }

    void numbers_data()
    {
        addCases({
            {u"12,5 + 1"_s, u"num:12.5 op:+ num:1"_s},
            {u"(12,5 + 1) * 2"_s, u"br:( num:12.5 op:+ num:1 br:) op:* num:2"_s},
            {u"max(1,5)"_s, u"id:max br:( num:1 comma:, num:5 br:)"_s},
            {u"max(1,5; 2,5)"_s, u"id:max br:( num:1.5 semi:; num:2.5 br:)"_s},
            {u"round(2,3456; 2)"_s, u"id:round br:( num:2.3456 semi:; num:2 br:)"_s},
            {u"[1,5]"_s, u"br:[ num:1 comma:, num:5 br:]"_s},
            {u"1 000 000"_s, u"num:1000000"_s},
            {u"1 000,5"_s, u"num:1000.5"_s},
            {u"1 000 + 2 500"_s, u"num:1000 op:+ num:2500"_s},
            {u"12 5"_s, u"num:12 num:5"_s},
            {u"1, 5"_s, u"num:1 comma:, num:5"_s},
            {u"1,5e3"_s, u"num:1.5e3"_s},
            {u"1  000"_s, u"num:1 num:000"_s},
            {u"20% z (12,5)"_s, u"num:20 op:% id:z br:( num:12.5 br:)"_s},
        });
    }
    void numbers()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(tokens(source).join(u' '), expected);
    }

    void englishNumbers_data()
    {
        addCases({
            {u"1,000,000.5"_s, u"num:1000000.5"_s},
            {u"1,000 + 2"_s, u"num:1000 op:+ num:2"_s},
            {u"max(1,000)"_s, u"id:max br:( num:1 comma:, num:000 br:)"_s},
            {u"12,5"_s, u"num:12.5"_s},
            {u"1 000"_s, u"num:1000"_s},
        });
    }
    void englishNumbers()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(tokens(source, CalcLocale::English).join(u' '), expected);
    }

    // --- operators and precedence -------------------------------------------------------------

    void operators_data()
    {
        addCases({
            {u"2 + 3 * 4"_s, u"14"_s},
            {u"(2 + 3) * 4"_s, u"20"_s},
            {u"2^3^2"_s, u"512"_s},
            {u"2**10"_s, u"1024"_s},
            {u"-2^2"_s, u"-4"_s},
            {u"2^-1"_s, u"0,5"_s},
            {u"-(2 + 3)"_s, u"-5"_s},
            {u"+5"_s, u"5"_s},
            {u"--5"_s, u"5"_s},
            {u"5!"_s, u"120"_s},
            {u"0!"_s, u"1"_s},
            {u"3!!"_s, u"720"_s},
            {u"10 mod 3"_s, u"1"_s},
            {u"10 MOD 4"_s, u"2"_s},
            {u"7 % 2.5"_s, u"2"_s},
            {u"2(3 + 4)"_s, u"14"_s},
            {u"(1 + 1)(2 + 2)"_s, u"8"_s},
            {u"2 pi"_s, u"6,283185307"_s},
            {u"√(9) + √16"_s, u"7"_s},
            {u"√16²"_s, u"16"_s},
            {u"1 - 2 - 3"_s, u"-4"_s},
            {u"12 / 3 / 2"_s, u"2"_s},
            {u"e"_s, u"2,718281828"_s},
            {u"0.1 + 0.2 == 0.3"_s, u"prawda"_s},
            {u"1 = 1"_s, u"prawda"_s},
            {u"2 <> 3"_s, u"prawda"_s},
        });
    }
    void operators()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(inl(source), expected);
    }

    // mathjs' "rule 2" (the v1 engine): `number / number factor` divides the numbers first, so
    // `1/2 kg` is half a kilogram, not 1/(2 kg). Other divisions keep the usual precedence.
    void numberDivisionBeforeImplicitMultiplication_data()
    {
        addCases({
            {u"1/2 kg"_s, u"0,5 kg"_s},
            {u"1/2kg"_s, u"0,5 kg"_s},
            {u"3/4 h to min"_s, u"45 min"_s},
            {u"1/2 l + 250 ml"_s, u"0,75 l"_s},
            {u"-1/2 kg"_s, u"-0,5 kg"_s},
            {u"2 * 1/2 kg"_s, u"1 kg"_s},
            {u"12,5 / 2 kg"_s, u"6,25 kg"_s},
            {u"1/2 (3 + 1)"_s, u"2"_s},
            {u"6 / 2 (1 + 2)"_s, u"9"_s},
            {u"1/2 pi"_s, u"1,570796327"_s},
            {u"1/2 PLN"_s, u"0,50 PLN"_s},
            {u"1/2 €"_s, u"0,50 €"_s},
            {u"20% of 1/2 kg"_s, u"0,1 kg"_s},
            {u"1 / 2 / 2 kg"_s, u"1 kg⁻¹"_s}, // only the last division is `number / number factor`
            {u"100 km / 2 h"_s, u"50 km/h"_s},
            {u"8 pi / 2 pi"_s, u"4"_s},
            {u"12 / 3 / 2"_s, u"2"_s},
            {u"2^1/2 kg"_s, u"1 kg⁻¹"_s},
            {u"1/2 to kg"_s, u"!Niezgodne jednostki"_s},
        });
    }
    void numberDivisionBeforeImplicitMultiplication()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(inl(source), expected);
    }

    void variableDivisionKeepsPrecedence()
    {
        QCOMPARE(last(u"x = 4\nx / 2 kg"_s), u"2 kg⁻¹"_s);
        QCOMPARE(last(u"x = 4\n1 / 2 x"_s), u"2"_s);
    }

    void logic_data()
    {
        addCases({
            {u"false and brak"_s, u"fałsz"_s},
            {u"true or brak"_s, u"prawda"_s},
            {u"3 < 2 < brak"_s, u"fałsz"_s},
            {u"1 < 2 < 3"_s, u"prawda"_s},
            {u"1 < 2 <= 2 > 1"_s, u"prawda"_s},
            {u"3 > 2 > 1 > 1"_s, u"fałsz"_s},
            {u"true and false"_s, u"fałsz"_s},
            {u"not true"_s, u"fałsz"_s},
            {u"not 1 == 2"_s, u"prawda"_s},
            {u"!false"_s, u"prawda"_s},
            {u"true xor false"_s, u"prawda"_s},
            {u"prawda && fałsz"_s, u"fałsz"_s},
            {u"0 || 5"_s, u"prawda"_s},
            {u"1 ? 2 : 3"_s, u"2"_s},
            {u"0 ? 2 : 1 ? 3 : 4"_s, u"3"_s},
            {u"\"a\" == \"a\""_s, u"prawda"_s},
            {u"\"a\" < \"b\""_s, u"prawda"_s},
            {u"true + 1"_s, u"2"_s},
            {u"1 km == 1000 m"_s, u"prawda"_s},
            {u"20°C > 60°F"_s, u"prawda"_s}, // absolute temperatures: 20 °C = 68 °F
            {u"1 km > 2 kg"_s, u"!Niezgodne jednostki"_s},
            {u"true and brak"_s, u"!Nieznana zmienna: brak"_s},
        });
    }
    void logic()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(inl(source), expected);
    }

    void englishBooleans()
    {
        QCOMPARE(inl(u"1 < 2"_s, En), u"true"_s);
        QCOMPARE(inl(u"1 > 2"_s, En), u"false"_s);
    }

    // --- functions --------------------------------------------------------------------------------

    void functions_data()
    {
        addCases({
            {u"sum(1, 2, 3)"_s, u"6"_s},
            {u"SUM(1; 2; 3)"_s, u"6"_s},
            {u"suma(1; 2,5)"_s, u"3,5"_s},
            {u"sum()"_s, u"0"_s},
            {u"avg(1, 2, 3, 4)"_s, u"2,5"_s},
            {u"average(2; 4)"_s, u"3"_s},
            {u"mean(2; 4)"_s, u"3"_s},
            {u"średnia(2; 4)"_s, u"3"_s},
            {u"Średnia(2; 4)"_s, u"3"_s},
            {u"min(3, 1, 2)"_s, u"1"_s},
            {u"max(3, 1, 2)"_s, u"3"_s},
            {u"max(2 h, 90 min)"_s, u"2 h"_s},
            {u"min(2 h, 90 min)"_s, u"90 min"_s},
            {u"count(1, \"a\", 2, true)"_s, u"2"_s},
            {u"ile(1; 2)"_s, u"2"_s},
            {u"median(3, 1, 2)"_s, u"2"_s},
            {u"mediana(4; 1; 3; 2)"_s, u"2,5"_s},
            {u"round(2.5)"_s, u"3"_s},
            {u"round(-2.5)"_s, u"-3"_s},
            {u"round(2.345, 2)"_s, u"2,35"_s},
            {u"round(1.005, 2)"_s, u"1,01"_s},
            {u"round(1234.5, -2)"_s, u"1200"_s},
            {u"round(0.004, 2)"_s, u"0"_s},
            {u"round(9.995, 2)"_s, u"10"_s},
            {u"zaokr(2,345; 1)"_s, u"2,3"_s},
            {u"round(12.345%, 1)"_s, u"12,3%"_s},
            {u"round(2.567 km, 1)"_s, u"2,6 km"_s},
            {u"floor(2.7)"_s, u"2"_s},
            {u"floor(-2.5)"_s, u"-3"_s},
            {u"ceil(2.1)"_s, u"3"_s},
            {u"trunc(-2.7)"_s, u"-2"_s},
            {u"abs(-5 zł)"_s, u"5 zł"_s},
            {u"sign(-3)"_s, u"-1"_s},
            {u"sqrt(2)"_s, u"1,414213562"_s},
            {u"pierwiastek(9)"_s, u"3"_s},
            {u"cbrt(-27)"_s, u"-3"_s},
            {u"pow(2, 10)"_s, u"1024"_s},
            {u"potęga(3; 2)"_s, u"9"_s},
            {u"exp(1)"_s, u"2,718281828"_s},
            {u"ln(e)"_s, u"1"_s},
            {u"log(1000)"_s, u"3"_s},
            {u"log(8, 2)"_s, u"3"_s},
            {u"log10(0.01)"_s, u"-2"_s},
            {u"log2(1024)"_s, u"10"_s},
            {u"sin(pi / 2)"_s, u"1"_s},
            {u"sin(90°)"_s, u"1"_s},
            {u"cos(0)"_s, u"1"_s},
            {u"tan(45 deg)"_s, u"1"_s},
            {u"asin(1) * 2"_s, u"3,141592654"_s},
            {u"acos(1)"_s, u"0"_s},
            {u"atan(1) * 4"_s, u"3,141592654"_s},
            {u"atan2(1, 1) * 4"_s, u"3,141592654"_s},
            {u"mod(-1, 3)"_s, u"2"_s},
            {u"MOD(7,5; 2)"_s, u"1,5"_s},
            {u"not (1 > 2)"_s, u"prawda"_s},
            {u"if(1 > 0, \"tak\", \"nie\")"_s, u"tak"_s},
            {u"jeżeli(0; 1; 2)"_s, u"2"_s},
            {u"jeśli(0; 1)"_s, u"fałsz"_s},
            {u"if(1, 5, brak)"_s, u"5"_s},
            {u"factorial(5)"_s, u"120"_s},
            {u"silnia(4)"_s, u"24"_s},
        });
    }
    void functions()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(inl(source), expected);
    }

    void functionErrors_data()
    {
        addCases({
            {u"foo(1)"_s, u"!Nieznana funkcja: foo"_s},
            {u"sin()"_s, u"!Za mało argumentów funkcji sin"_s},
            {u"round(1, 2, 3)"_s, u"!Za dużo argumentów funkcji round"_s},
            {u"avg()"_s, u"!Brak wartości liczbowych"_s},
            {u"max(\"a\")"_s, u"!Brak wartości liczbowych"_s},
            {u"sum(1 km, 2 kg)"_s, u"!Niezgodne jednostki"_s},
            {u"min(1 km, 2 kg)"_s, u"!Niezgodne jednostki"_s},
            {u"ln(0)"_s, u"!Argument poza dziedziną funkcji ln"_s},
            {u"log(-1)"_s, u"!Argument poza dziedziną funkcji log"_s},
            {u"log(8, 1)"_s, u"!Argument poza dziedziną funkcji log"_s},
            {u"asin(2)"_s, u"!Argument poza dziedziną funkcji asin"_s},
            {u"sqrt(2 m)"_s, u"!Niezgodne jednostki"_s},
            {u"exp(2 m)"_s, u"!Niezgodne jednostki"_s},
            {u"sin(2 m)"_s, u"!Niezgodne jednostki"_s},
            {u"(-1)!"_s, u"!Argument poza dziedziną funkcji !"_s},
            {u"2.5!"_s, u"!Argument poza dziedziną funkcji !"_s},
            {u"171!"_s, u"!Liczba jest za duża"_s},
            {u"(-8)^(1/3)"_s, u"!Argument poza dziedziną funkcji ^"_s},
            {u"x(1)"_s, u"!Nieznana funkcja: x"_s},
            {u"round(2.5, 0/0)"_s, u"!Argument poza dziedziną funkcji round"_s},
            {u"round(2.5, ∞)"_s, u"2,5"_s},
            {u"import(\"x\")"_s, u"!Nieznana funkcja: import"_s},
            {u"evaluate(\"1 + 1\")"_s, u"!Nieznana funkcja: evaluate"_s},
        });
    }
    void functionErrors()
    {
        QFETCH(QString, source);
        QFETCH(QString, expected);
        QCOMPARE(inl(source), expected);
    }

    void englishErrors()
    {
        QCOMPARE(inl(u"ln(0)"_s, En), u"!Argument out of range for ln"_s);
        QCOMPARE(inl(u"foo(1)"_s, En), u"!Unknown function: foo"_s);
        QCOMPARE(inl(u"1 km + 1 kg"_s, En), u"!Units do not match"_s);
        QCOMPARE(inl(u"\"x\" * 2"_s, En), u"!Cannot convert “x” to a number"_s);
    }

    // --- overflow and limits ------------------------------------------------------------------------

    void overflow()
    {
        QCOMPARE(inl(u"10^400"_s), u"!Liczba jest za duża"_s);
        QCOMPARE(inl(u"1e999"_s), u"!Liczba jest za duża"_s);
        QCOMPARE(inl(u"1e308 * 10"_s), u"!Liczba jest za duża"_s);
        QCOMPARE(inl(u"1e308 + 1e308"_s), u"!Liczba jest za duża"_s);
        QCOMPARE(inl(u"exp(1000)"_s), u"!Liczba jest za duża"_s);
        QCOMPARE(inl(u"0^-1"_s), u"∞"_s);
        QCOMPARE(inl(u"m^1000"_s), u"!Niezgodne jednostki"_s);
    }

    void unitPowersAreBounded()
    {
        // Each step is a legal exponent, but the unit's power would overflow an int (UB).
        QCOMPARE(inl(u"((((m^99)^99)^99)^99)^99"_s), u"!Niezgodne jednostki"_s);
        QCOMPARE(inl(u"(m^99)^2"_s), u"!Niezgodne jednostki"_s);
        QCOMPARE(inl(u"(m³)^40"_s), u"!Niezgodne jednostki"_s);
        QCOMPARE(inl(u"1 km to (m^99)^99"_s), u"!Niezgodne jednostki"_s);
        QCOMPARE(inl(u"(m^9)^9"_s), u"1 m⁸¹"_s);
        QCOMPARE(inl(u"(1 m³)^33"_s), u"1 m⁹⁹"_s);
        // The Unit API saturates instead of wrapping around.
        const Unit m(findUnit(u"m"));
        QVERIFY(m.power(INT_MAX).power(INT_MAX).dimension()[0] > 0);
        QVERIFY(m.power(INT_MIN).power(3).dimension()[0] < 0);
        QVERIFY(Unit::product(m.power(INT_MAX), m.power(INT_MAX)).dimension()[0] > 0);
    }

    void tooLong()
    {
        QString expr;
        for (int i = 0; i < 3000; ++i)
            expr += u"1+"_s;
        expr += u'1';
        QCOMPARE(inl(expr), u"!Wyrażenie jest za długie"_s);
        QCOMPARE(last(expr), u"!Wyrażenie jest za długie"_s);
    }

    void deepNesting()
    {
        QString open(150, u'(');
        QString close(150, u')');
        QCOMPARE(inl(open + u'1' + close), u"1"_s);
        const QString deep = QString(1500, u'(') + u'1' + QString(1500, u')');
        QCOMPARE(inl(deep), u"!Obliczenie jest zbyt złożone"_s);
        QString factorials = u"3"_s + QString(1000, u'!');
        QCOMPARE(inl(factorials), u"!Obliczenie jest zbyt złożone"_s);
        QString powers = u"2"_s;
        for (int i = 0; i < 1000; ++i)
            powers += u"^1"_s;
        QCOMPARE(inl(powers), u"!Obliczenie jest zbyt złożone"_s);
        QString minus(1500, u'-');
        QCOMPARE(inl(minus + u'1'), u"!Obliczenie jest zbyt złożone"_s);
    }

    void longOperatorChains()
    {
        // A long line of terms (as v1 allowed up to the 4000-character limit): left-associative
        // chains are evaluated iteratively, so their length is not a nesting depth.
        QString sum = u"1"_s;
        for (int i = 0; i < 1999; ++i)
            sum += u"+1"_s;
        QCOMPARE(inl(sum), u"2000"_s);
        QCOMPARE(last(sum), u"2000"_s);
        QString mixed = u"1000"_s;
        for (int i = 0; i < 600; ++i)
            mixed += (i % 3 == 0) ? u" - 1"_s : (i % 3 == 1) ? u" * 1"_s : u" / 1"_s;
        QCOMPARE(inl(mixed), u"800"_s);
        QString percents = u"100 zł"_s;
        for (int i = 0; i < 700; ++i)
            percents += u" + 0%"_s;
        QCOMPARE(inl(percents), u"100 zł"_s);
        QString implicit = u"x = 1\n1"_s;
        for (int i = 0; i < 1300; ++i)
            implicit += u" x"_s;
        QCOMPARE(last(implicit), u"1"_s);
        QString target = u"1 m to m"_s;
        for (int i = 0; i < 1300; ++i)
            target += u"*m"_s;
        QVERIFY(!inl(target).isEmpty()); // bounded, never a crash
    }

    void deepEvaluationFitsASmallStack()
    {
        // Calculations may run on worker threads (512 KiB stacks on some platforms): realistic
        // deep expressions must fit, and hostile ones that hit the evaluation-depth limit must
        // fit in 2 MiB (see the stack note in calc/calc.h).
        QString sum = u"1"_s;
        for (int i = 0; i < 1999; ++i)
            sum += u"+1"_s;
        QString nested = u"g(n - 1)"_s; // the recursion happens inside 90 levels of nesting
        for (int i = 0; i < 90; ++i)     // (within the parser's nesting limit)
            nested = u"20% of (%1)"_s.arg(nested);
        const QString hostile = u"g(n) = n < 1 ? 0 : %1\ng(99)"_s.arg(nested);
        const auto runWithStack = [](uint stackSize, const std::function<void()> &work) {
            QThread *thread = QThread::create(work);
            thread->setStackSize(stackSize);
            thread->start();
            const bool finished = thread->wait(120000);
            delete thread;
            return finished;
        };
        QString shownSum;
        QString recursive;
        QVERIFY(runWithStack(512 * 1024 * BRULION_TEST_SLOWDOWN, [&] {
            shownSum = inl(sum);
            recursive = last(u"f(n) = n < 1 ? 0 : 1 + f(n - 1)\nf(99)"_s);
        }));
        QCOMPARE(shownSum, u"2000"_s);
        QCOMPARE(recursive, u"99"_s);
        QString deepest;
        QVERIFY(runWithStack(2 * 1024 * 1024 * BRULION_TEST_SLOWDOWN, [&] { deepest = last(hostile); }));
        QCOMPARE(deepest, u"!Zbyt głęboka rekurencja"_s);
    }

    void infiniteRecursion()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"f(x) = f(x)\nf(1)\n2 + 2", scope);
        QCOMPARE(lines[1].error, u"Zbyt głęboka rekurencja"_s);
        QCOMPARE(lines[2].value, u"4"_s);
    }

    void exponentialRecursionIsBounded()
    {
        QElapsedTimer timer;
        timer.start();
        Scope scope;
        const auto lines = evaluateCalc(u"f(n) = n < 1 ? 1 : f(n - 1) + f(n - 1)\nf(40)\nf(10)\n2 + 2", scope);
        QCOMPARE(lines[1].error, u"Obliczenie jest zbyt złożone"_s);
        QCOMPARE(lines[2].value, u"1024"_s);
        QCOMPARE(lines[3].value, u"4"_s);
        QVERIFY2(timer.elapsed() < 20000, "the work budget must stop the evaluation quickly");
        QCOMPARE(last(u"fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 1 - 1)\nfib(15)"_s), u"610"_s);
    }

    void totalBudgetPerBlock()
    {
        // Each line alone fits the per-expression budget; together they exceed the total.
        QString block;
        for (int i = 0; i < 40; ++i)
            block += u"f(n) = n < 1 ? 1 : f(n - 1) + f(n - 1)\nf(14)\n"_s;
        Scope scope;
        const auto lines = evaluateCalc(block, scope);
        QCOMPARE(lines[1].value, nb(u"16_384"_s));
        QCOMPARE(lines[lines.size() - 2].error, u"Obliczenie jest zbyt złożone"_s);
        Scope fresh;
        QCOMPARE(evaluateCalc(u"f(n) = n < 1 ? 1 : f(n - 1) + f(n - 1)\nf(14)", fresh)[1].value, nb(u"16_384"_s));
    }

    void longStrings()
    {
        Scope scope;
        const auto lines = evaluateCalc(u"s = \"abcdefghij\"\nf(x, n) = n < 1 ? x : f(concat(x, x), n - 1)\nf(s, 20)", scope);
        QCOMPARE(lines[2].error, u"Wynik jest za duży (limit 100000)"_s);
        QCOMPARE(inl(u"concat(\"zażółć\", \" \", \"gęślą\")"_s), u"zażółć gęślą"_s);
    }

    void stringWorkIsMetered()
    {
        // Copying and comparing long strings costs work by length, not one unit per node: a
        // block cannot keep the CPU busy for seconds with 64 KiB strings.
        QString block = u"d(x, n) = n < 1 ? x : d(concat(x, x), n - 1)\nt = d(\"abcdefgh\", 13)\n"
                        u"h(n) = n < 1 ? concat(t, \"\") : if(h(n - 1) == h(n - 1), \"\", \"\")\n"_s;
        for (int i = 0; i < 5; ++i)
            block += u"h(17)\n"_s;
        Scope scope;
        QElapsedTimer timer;
        timer.start();
        const auto lines = evaluateCalc(block, scope);
        QVERIFY2(timer.elapsed() < 3000, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(lines[1].value.size(), 1000); // 65 536 characters, shown truncated
        QCOMPARE(lines[3].error, u"Obliczenie jest zbyt złożone"_s);
        QCOMPARE(inl(u"concat(\"a\", 1, \"b\") == \"a1b\""_s), u"prawda"_s);
    }

    void listsAreBoundedBeforeAllocation()
    {
        // Regression: list values were deep-copied on every use and charged only after being
        // built, so three short lines (x: 1300 items, y: 700 × x, sum(y, y, …)) allocated
        // gigabytes and crashed with std::bad_alloc.
        const QString x = u"x = ["_s + QStringList(1300, u"1"_s).join(u", "_s) + u']';
        const QString y = u"y = ["_s + QStringList(700, u"x"_s).join(u", "_s) + u']';
        const QString sum = u"sum("_s + QStringList(40, u"y"_s).join(u", "_s) + u')';
        Scope scope;
        QElapsedTimer timer;
        timer.start();
        const auto lines = evaluateCalc(QString(x + u'\n' + y + u'\n' + sum), scope);
        QVERIFY2(timer.elapsed() < 5000, qPrintable(u"%1 ms"_s.arg(timer.elapsed())));
        QCOMPARE(lines[0].assigned, u"x"_s);
        QCOMPARE(lines[1].error, u"Wynik jest za duży (limit 20000)"_s);
        QVERIFY(lines[2].hasError());
        // Realistic lists still work, also when nested and passed around.
        QCOMPARE(last(x + u"\nz = [x, x, x]\nsum(z) + count(z, z, z)"_s), nb(u"15_600"_s));
        QCOMPARE(inl(u"[[1, 2], [3]]"_s), u"[1; 2; 3]"_s);
        Scope copies;
        evaluateCalc(QString(x + u"\nw = x"_s), copies);
        QCOMPARE(copies.value(u"w"_s).items().size(), std::size_t(1300));
        QCOMPARE(copies.value(u"w"_s), copies.value(u"x"_s));
        QCOMPARE(Value::list({}).items().size(), std::size_t(0));
        QVERIFY(Value::number(1).items().empty());
    }

    // --- units API ----------------------------------------------------------------------------------

    void unitLookup()
    {
        QVERIFY(findUnit(u"km"));
        QVERIFY(findUnit(u"godz"));
        QCOMPARE(findUnit(u"godz"), findUnit(u"h"));
        QCOMPARE(findUnit(u"dni"), findUnit(u"d"));
        QCOMPARE(findUnit(u"°C"), findUnit(u"C"));
        QVERIFY(findUnit(u"MB") != findUnit(u"Mb"));
        QVERIFY(!findUnit(u"KM2"));
        QVERIFY(!findUnit(u""));
        QVERIFY(findUnit(u"°C")->isTemperature());
        QCOMPARE(findUnit(u"km")->factor, 1000.0);
    }

    void unitAlgebra()
    {
        const Unit km(findUnit(u"km"));
        const Unit h(findUnit(u"h"));
        const Unit m(findUnit(u"m"));
        QCOMPARE(Unit::quotient(km, h).toString(Pl), u"km/h"_s);
        QCOMPARE(Unit::quotient(km, h).dimension(), (Dimension{1, 0, -1, 0, 0, 0}));
        QVERIFY(Unit::quotient(km, m).isEmpty());
        QCOMPARE(Unit::product(km, m).toString(Pl), u"km²"_s);
        QCOMPARE(Unit::quotient(Unit(), h).toString(Pl), u"h⁻¹"_s);
        QCOMPARE(Unit::quotient(m, Unit::product(h, h)).toString(Pl), u"m/h²"_s);
        const Unit kg(findUnit(u"kg"));
        const Unit s(findUnit(u"s"));
        QCOMPARE(Unit::quotient(Unit::product(kg, m), s.power(2)).toString(Pl), u"N"_s);
        QCOMPARE(Unit::quotient(kg, Unit::product(m, s.power(2))).toString(Pl), u"Pa"_s);
        QCOMPARE(Unit::product(Unit(findUnit(u"kW")), h).toString(Pl), u"kWh"_s);
        QCOMPARE(Unit(findUnit(u"m2")).root(2)->toString(Pl), u"m"_s);
        QVERIFY(!m.root(2));
        QCOMPARE(m.power(3).toString(Pl), u"m³"_s);
        QCOMPARE(Unit(findUnit(u"km")).scale(), 1000.0);
        QCOMPARE(superscript(-12), u"⁻¹²"_s);
        QCOMPARE(Unit(findUnit(u"week")).toString(En), u"wk"_s);
        QCOMPARE(Unit(findUnit(u"rok")).toString(Pl), u"r."_s);
    }

    void valueApi()
    {
        const Value v = Value::quantity(5000, Unit(findUnit(u"km")));
        QCOMPARE(v.magnitude(), 5.0);
        QCOMPARE(v.format(Pl), u"5 km"_s);
        QCOMPARE(v.numeric(), std::optional<double>(5));
        QCOMPARE(Value::percent(0.15).format(Pl), u"15%"_s);
        QCOMPARE(Value::money(12.5, Currency{u"PLN"_s, u"zł"_s, false}).format(Pl), u"12,50 zł"_s);
        QCOMPARE(Value::boolean(true).format(En), u"true"_s);
        QVERIFY(!Value::boolean(true).numeric());
        QCOMPARE(Value::string(u"żółw"_s).format(Pl), u"żółw"_s);
        QVERIFY(Value().format(Pl).isEmpty());
        QCOMPARE(Value::number(2), Value::number(2));
        QVERIFY(!(Value::number(2) == Value::percent(2)));
        QCOMPARE(Value::string(QString(2000, u'x')).format(Pl).size(), 1000);
    }

    void scopeApi()
    {
        Scope scope;
        scope.set(u"ilość"_s, Value::number(3)); // stored as NFC
        QVERIFY(scope.contains(u"ilość"_s));
        QCOMPARE(scope.number(u"ilość"_s), std::optional<double>(3));
        QVERIFY(scope.find(u"ilość"_s));
        QVERIFY(!scope.ans());
        scope.set(u"ans"_s, Value::number(1));
        QCOMPARE(scope.ans()->si(), 1.0);
        QVERIFY(scope.remove(u"ans"_s));
        QCOMPARE(scope.size(), 1);
        QVERIFY(!scope.number(u"brak"_s));
        QVERIFY(scope.formatted(u"brak"_s, Pl).isEmpty());
        scope.clear();
        QVERIFY(scope.isEmpty());
    }

    void identifiers()
    {
        QVERIFY(detail::isIdentifier(u"ilość"));
        QVERIFY(detail::isIdentifier(u"_x"));
        QVERIFY(detail::isIdentifier(u"żółć_2"));
        QVERIFY(!detail::isIdentifier(u"2x"));
        QVERIFY(!detail::isIdentifier(u"a b"));
        QVERIFY(!detail::isIdentifier(u"a-b"));
        QVERIFY(!detail::isIdentifier(u""));
        QCOMPARE(detail::sanitizeIdentifier(u"due-date"), u"due_date"_s);
        QCOMPARE(detail::sanitizeIdentifier(u"cena netto"), u"cena_netto"_s);
        QCOMPARE(detail::sanitizeIdentifier(u"Cena"), u"Cena"_s);
        QCOMPARE(detail::sanitizeIdentifier(u"2026"), u"_2026"_s);
        QCOMPARE(detail::sanitizeIdentifier(u"  kurs EUR "), u"kurs_EUR"_s);
        QCOMPARE(detail::sanitizeIdentifier(u"---"), QString());
        QCOMPARE(detail::sanitizeIdentifier(u""), QString());
        QCOMPARE(detail::sanitizeIdentifier(u"zażółć gęślą"), u"zażółć_gęślą"_s);
    }

    void errorMessages()
    {
        QCOMPARE((CalcError{CalcErrorCode::RefError, u"B1"_s}.message(Pl)), u"Błąd w komórce B1"_s);
        QCOMPARE((CalcError{CalcErrorCode::RefError, u"B1"_s}.message(En)), u"Error in cell B1"_s);
        QCOMPARE((CalcError{CalcErrorCode::UndefinedSymbol, u"%1 %2"_s}.message(Pl)), u"Nieznana zmienna: %1 %2"_s);
        QVERIFY(!CalcError{}.isError());
        QVERIFY(CalcError{}.message(Pl).isEmpty());
        QCOMPARE(booleanWord(false, Pl), u"fałsz"_s);
    }
};

QTEST_GUILESS_MAIN(TestCalcEngine)
#include "tst_calc_engine.moc"
