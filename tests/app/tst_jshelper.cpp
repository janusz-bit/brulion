// Tests for JsHelper (QML singleton `Js`, src/app/ui/jshelper.h): every function gives what the
// JavaScript built-in it replaces gives — each case is evaluated by QJSEngine too and compared —
// with Polish text, UTF-16 edge cases, negative indices, malformed numbers and patterns.
#include "jshelper.h"

#include <QJSEngine>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;
using brulion::ui::JsHelper;

namespace {

/// A JavaScript string literal for `s`.
QString lit(const QString &s)
{
    QString out = u"\""_s;
    for (const QChar c : s) {
        if (c == u'"' || c == u'\\')
            out += u'\\';
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) {
            out += u"\\u"_s + QString::number(c.unicode(), 16).rightJustified(4, u'0');
            continue;
        }
        out += c;
    }
    return out + u'"';
}

QString litList(const QStringList &list)
{
    QStringList parts;
    for (const QString &s : list)
        parts.append(lit(s));
    return u"["_s + parts.join(u',') + u']';
}

} // namespace

class TstJsHelper : public QObject {
    Q_OBJECT

    JsHelper js;
    QJSEngine engine;

    QJSValue eval(const QString &code)
    {
        const QJSValue v = engine.evaluate(code);
        if (v.isError())
            qWarning() << "JavaScript error in" << code << v.toString();
        return v;
    }
    QString evalString(const QString &code) { return eval(code).toString(); }
    QStringList evalStrings(const QString &code) { return eval(code).toVariant().toStringList(); }

private Q_SLOTS:
    void strings_data();
    void strings();
    void indices_data();
    void indices();
    void trim_data();
    void trim();
    void split_data();
    void split();
    void pad_data();
    void pad();
    void uriComponent_data();
    void uriComponent();
    void numbers_data();
    void numbers();
    void numberToString_data();
    void numberToString();
    void dateNow();
    void toFixed_data();
    void toFixed();
    void atan2_data();
    void atan2();
    void regExp_data();
    void regExp();
    void regExpBeyondTheEngine();
    void lists();
    void objects();
    void stringify_data();
    void stringify();
};

void TstJsHelper::strings_data()
{
    QTest::addColumn<QString>("s");
    QTest::addColumn<QString>("arg");
    QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s << u"gęś"_s;
    QTest::newRow("empty") << QString() << u"a"_s;
    QTest::newRow("empty search") << u"abc"_s << QString();
    QTest::newRow("absent") << u"abc"_s << u"x"_s;
    QTest::newRow("repeated") << u"ąbąbą"_s << u"ą"_s;
    QTest::newRow("surrogates") << u"a😀b😀"_s << u"😀"_s;
    QTest::newRow("upper") << u"ŻÓŁW Straße"_s << u"Ż"_s;
}

void TstJsHelper::strings()
{
    QFETCH(QString, s);
    QFETCH(QString, arg);
    const QString S = lit(s);
    const QString A = lit(arg);
    QCOMPARE(js.indexOf(s, arg), eval(S + u".indexOf("_s + A + u')').toInt());
    QCOMPARE(js.lastIndexOf(s, arg), eval(S + u".lastIndexOf("_s + A + u')').toInt());
    QCOMPARE(js.includes(s, arg), eval(S + u".includes("_s + A + u')').toBool());
    QCOMPARE(js.startsWith(s, arg), eval(S + u".startsWith("_s + A + u')').toBool());
    QCOMPARE(js.endsWith(s, arg), eval(S + u".endsWith("_s + A + u')').toBool());
    QCOMPARE(js.toLowerCase(s), evalString(S + u".toLowerCase()"_s));
    QCOMPARE(js.toUpperCase(s), evalString(S + u".toUpperCase()"_s));
    QCOMPARE(js.replace(s, arg, u"<>"_s),
             evalString(S + u".replace("_s + A + u", () => \"<>\")"_s));
    // The engine has no replaceAll: split + join is the same for a non-empty search.
    if (arg.isEmpty())
        QCOMPARE(js.replaceAll(u"abc"_s, arg, u"–"_s), u"–a–b–c–"_s);
    else
        QCOMPARE(js.replaceAll(s, arg, u"–"_s), evalString(S + u".split("_s + A + u").join(\"–\")"_s));
    QCOMPARE(js.repeat(s, 3), evalString(S + u".repeat(3)"_s));
    QCOMPARE(js.repeat(s, 0), evalString(S + u".repeat(0)"_s));
    for (int i = -1; i <= int(s.size()); ++i) {
        const QString I = QString::number(i);
        QCOMPARE(js.charAt(s, i), evalString(S + u".charAt("_s + I + u')'));
        const double code = js.charCodeAt(s, i);
        const QJSValue expected = eval(S + u".charCodeAt("_s + I + u')');
        if (std::isnan(expected.toNumber()))
            QVERIFY(std::isnan(code));
        else
            QCOMPARE(code, expected.toNumber());
        QCOMPARE(js.indexOfFrom(s, arg, i), eval(S + u".indexOf("_s + A + u',' + I + u')').toInt());
        QCOMPARE(js.startsWithAt(s, arg, i), eval(S + u".startsWith("_s + A + u',' + I + u')').toBool());
    }
}

void TstJsHelper::indices_data()
{
    QTest::addColumn<int>("start");
    QTest::addColumn<int>("end");
    for (int a : {-9, -3, -1, 0, 1, 2, 5, 9}) {
        for (int b : {-9, -2, -1, 0, 1, 3, 5, 9})
            QTest::addRow("%d,%d", a, b) << a << b;
    }
}

void TstJsHelper::indices()
{
    QFETCH(int, start);
    QFETCH(int, end);
    const QString s = u"źdźbło"_s;
    const QString S = lit(s);
    const QString A = QString::number(start);
    const QString B = QString::number(end);
    QCOMPARE(js.slice(s, start, end), evalString(S + u".slice("_s + A + u',' + B + u')'));
    QCOMPARE(js.sliceFrom(s, start), evalString(S + u".slice("_s + A + u')'));
    QCOMPARE(js.substring(s, start, end), evalString(S + u".substring("_s + A + u',' + B + u')'));
    QCOMPARE(js.substringFrom(s, start), evalString(S + u".substring("_s + A + u')'));
}

void TstJsHelper::trim_data()
{
    QTest::addColumn<QString>("s");
    QTest::newRow("plain") << u"  łódź  "_s;
    QTest::newRow("tabs and lines") << u"\t\n\r\v\f x \r\n"_s;
    QTest::newRow("nbsp and bom") << u"\u00a0\ufeffx\u2028\u2029"_s;
    QTest::newRow("unicode spaces") << u"\u2003\u3000x\u202f"_s;
    QTest::newRow("NEL is not space") << u"\u0085x\u0085"_s;
    QTest::newRow("only spaces") << u" \t "_s;
    QTest::newRow("empty") << QString();
}

void TstJsHelper::trim()
{
    QFETCH(QString, s);
    const QString S = lit(s);
    QCOMPARE(js.trim(s), evalString(S + u".trim()"_s));
    // The engine has no trimStart / trimEnd: its trim() of a string with a marker after / before.
    QCOMPARE(js.trimStart(s) + u'|', evalString(u"("_s + S + u" + \"|\").trim()"_s));
    QCOMPARE(u'|' + js.trimEnd(s), evalString(u"(\"|\" + "_s + S + u").trim()"_s));
    QCOMPARE(js.trimmedStrings({s, s}), evalStrings(litList({s, s}) + u".map(x => x.trim())"_s));
}

void TstJsHelper::split_data()
{
    QTest::addColumn<QString>("s");
    QTest::addColumn<QString>("sep");
    QTest::newRow("comma") << u"a,ą,,b,"_s << u","_s;
    QTest::newRow("empty separator") << u"żółw"_s << QString();
    QTest::newRow("empty string") << QString() << u","_s;
    QTest::newRow("empty both") << QString() << QString();
    QTest::newRow("multi") << u"a::b::"_s << u"::"_s;
    QTest::newRow("absent") << u"abc"_s << u";"_s;
}

void TstJsHelper::split()
{
    QFETCH(QString, s);
    QFETCH(QString, sep);
    QCOMPARE(js.split(s, sep), evalStrings(lit(s) + u".split("_s + lit(sep) + u')'));
}

void TstJsHelper::pad_data()
{
    QTest::addColumn<QString>("s");
    QTest::addColumn<int>("length");
    QTest::addColumn<QString>("fill");
    QTest::newRow("digits") << u"7"_s << 2 << u"0"_s;
    QTest::newRow("long") << u"123"_s << 2 << u"0"_s;
    QTest::newRow("multi fill") << u"x"_s << 6 << u"ąb"_s;
    QTest::newRow("empty fill") << u"x"_s << 6 << QString();
    QTest::newRow("negative") << u"x"_s << -1 << u" "_s;
}

void TstJsHelper::pad()
{
    QFETCH(QString, s);
    QFETCH(int, length);
    QFETCH(QString, fill);
    const QString call = u"("_s + QString::number(length) + u',' + lit(fill) + u')';
    QCOMPARE(js.padStart(s, length, fill), evalString(lit(s) + u".padStart"_s + call));
    QCOMPARE(js.padEnd(s, length, fill), evalString(lit(s) + u".padEnd"_s + call));
}

void TstJsHelper::uriComponent_data()
{
    QTest::addColumn<QString>("s");
    QTest::newRow("empty") << QString();
    QTest::newRow("plain") << u"praca-2026_v1.md~"_s;
    QTest::newRow("polish") << u"Zażółć gęślą jaźń"_s;
    QTest::newRow("reserved") << u"a b&c=d/e?f#g:h@i+j,k;l$"_s;
    QTest::newRow("marks") << u"!*'()"_s;
    QTest::newRow("percent") << u"100% pewne"_s;
    QTest::newRow("emoji") << u"tag 🎉"_s;
}

void TstJsHelper::uriComponent()
{
    QFETCH(QString, s);
    const QString encoded = js.encodeURIComponent(s);
    QCOMPARE(encoded, evalString(u"encodeURIComponent("_s + lit(s) + u')'));
    QCOMPARE(js.decodeURIComponent(encoded), evalString(u"decodeURIComponent("_s + lit(encoded) + u')'));
    QCOMPARE(js.decodeURIComponent(encoded), s);
    // Escapes of reserved characters are decoded too (decodeURI would keep them).
    QCOMPARE(js.decodeURIComponent(u"tag%3Apraca%2Fdom"_s), evalString(u"decodeURIComponent('tag%3Apraca%2Fdom')"_s));
    // Malformed: kept (the engine throws).
    QCOMPARE(js.decodeURIComponent(u"100%"_s), u"100%"_s);
    QCOMPARE(js.decodeURIComponent(u"%zz%41"_s), u"%zzA"_s);
}

void TstJsHelper::numbers_data()
{
    QTest::addColumn<QString>("s");
    for (const char *c : {"", " ", "0", "42", " 42 ", "-7", "+3", "3.5", ".5", "5.", "1e3", "1E-2", "-1.5e+2",
                          "0x1F", "0X1f", "0o17", "0b101", "0x", "0xg", "-0x10", "12px", "px12", "1,5", "1.2.3",
                          "Infinity", "-Infinity", "+Infinity", "infinity", "NaN", "1e400", "1e-400", "٣",
                          "  -12.5abc", "\t\n7", "00012", "1_000", "e5", ".", "-", "+", "-.5"}) {
        QTest::newRow(c) << QString::fromUtf8(c);
    }
}

void TstJsHelper::numbers()
{
    QFETCH(QString, s);
    const auto same = [](double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
    const double number = js.toNumber(s);
    const double expectedNumber = eval(u"Number("_s + lit(s) + u')').toNumber();
    QVERIFY2(same(number, expectedNumber), qPrintable(u"Number: %1 vs %2"_s.arg(number).arg(expectedNumber)));
    const double integer = js.parseInt(s);
    const double expectedInteger = eval(u"parseInt("_s + lit(s) + u')').toNumber();
    QVERIFY2(same(integer, expectedInteger), qPrintable(u"parseInt: %1 vs %2"_s.arg(integer).arg(expectedInteger)));
    const double real = js.parseFloat(s);
    const double expectedReal = eval(u"parseFloat("_s + lit(s) + u')').toNumber();
    QVERIFY2(same(real, expectedReal), qPrintable(u"parseFloat: %1 vs %2"_s.arg(real).arg(expectedReal)));
    QCOMPARE(js.isNaN(number), eval(u"isNaN(Number("_s + lit(s) + u"))"_s).toBool());
    QCOMPARE(js.isFinite(number), eval(u"isFinite(Number("_s + lit(s) + u"))"_s).toBool());
}

void TstJsHelper::numberToString_data()
{
    QTest::addColumn<QString>("literal");
    for (const char *c : {"0", "-0", "1", "-1", "0.5", "0.1", "0.1 + 0.2", "1/3", "123456789012345680000",
                          "1e21", "1.5e21", "1e-6", "1e-7", "0.000001234", "123.456", "-1e-7", "2**53",
                          "2**53 + 2", "1/0", "-1/0", "0/0", "5e-324", "1.7976931348623157e308", "100", "1e20"}) {
        QTest::newRow(c) << QString::fromLatin1(c);
    }
}

void TstJsHelper::numberToString()
{
    QFETCH(QString, literal);
    const double x = eval(literal).toNumber();
    QCOMPARE(js.numberToString(x), evalString(u"String("_s + literal + u')'));
}

void TstJsHelper::atan2_data()
{
    QTest::addColumn<QString>("y");
    QTest::addColumn<QString>("x");
    for (const auto &[y, x] : std::initializer_list<std::pair<const char *, const char *>>{
                 {"1", "1"}, {"-1", "1"}, {"1", "-1"}, {"-1", "-1"}, {"0", "0"}, {"-0", "-1"}, {"0", "-1"},
                 {"1", "0"}, {"-1", "0"}, {"1/0", "1/0"}, {"0/0", "1"}, {"3", "-4"}, {"1e-300", "1e300"}}) {
        QTest::addRow("%s,%s", y, x) << QString::fromLatin1(y) << QString::fromLatin1(x);
    }
}

void TstJsHelper::atan2()
{
    QFETCH(QString, y);
    QFETCH(QString, x);
    const double got = js.atan2(eval(y).toNumber(), eval(x).toNumber());
    const double expected = eval(u"Math.atan2("_s + y + u", "_s + x + u')').toNumber();
    const auto same = [](double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; };
    QVERIFY2(same(got, expected), qPrintable(u"%1 vs %2"_s.arg(got).arg(expected)));
}

void TstJsHelper::dateNow()
{
    const double before = eval(u"Date.now()"_s).toNumber();
    const double now = js.dateNow();
    const double after = eval(u"Date.now()"_s).toNumber();
    QVERIFY(before <= now);
    QVERIFY(now <= after);
    QCOMPARE(now, std::floor(now));
}

void TstJsHelper::toFixed_data()
{
    QTest::addColumn<QString>("literal");
    QTest::addColumn<int>("digits");
    for (const char *c : {"0", "-0", "1.005", "2.5", "0.5", "-2.5", "1.45", "123.456", "-0.001", "1e21", "1e20",
                          "0/0", "1/0", "0.000001"}) {
        for (int d : {0, 1, 2, 5})
            QTest::addRow("%s,%d", c, d) << QString::fromLatin1(c) << d;
    }
}

void TstJsHelper::toFixed()
{
    QFETCH(QString, literal);
    QFETCH(int, digits);
    const double x = eval(literal).toNumber();
    QCOMPARE(js.toFixed(x, digits), evalString(u"("_s + literal + u").toFixed("_s + QString::number(digits) + u')'));
}

void TstJsHelper::regExp_data()
{
    QTest::addColumn<QString>("s");
    QTest::addColumn<QString>("pattern");
    QTest::addColumn<QString>("flags");
    QTest::addColumn<QString>("replacement");
    QTest::newRow("digits") << u"a1b22c333"_s << u"\\d+"_s << QString() << u"<$&>"_s;
    QTest::newRow("digits g") << u"a1b22c333"_s << u"\\d+"_s << u"g"_s << u"<$&>"_s;
    QTest::newRow("groups") << u"2026-11-05"_s << u"(\\d+)-(\\d+)-(\\d+)"_s << QString() << u"$3.$2.$1"_s;
    // The engine knows named groups but not $<name> in a replacement (kept as it is).
    QTest::newRow("named") << u"key: wartość"_s << u"(?<k>\\w+):\\s*(?<v>.*)"_s << QString() << u"$<v>=$<k>"_s;
    QTest::newRow("case") << u"Żółw ŻÓŁW żółw"_s << u"żółw"_s << u"gi"_s << u"[$&]"_s;
    QTest::newRow("multiline") << u"a\nb\nc"_s << u"^(\\w)$"_s << u"gm"_s << u"-$1-"_s;
    QTest::newRow("no match") << u"abc"_s << u"x+"_s << u"g"_s << u"Y"_s;
    QTest::newRow("dollars") << u"cena 5"_s << u"(\\d)"_s << QString() << u"$$$1 $9 $"_s;
    QTest::newRow("context") << u"abc"_s << u"b"_s << QString() << u"[$`|$']"_s;
    QTest::newRow("unicode escape") << u"zażółć"_s << u"\\u017c"_s << u"g"_s << u"Ż"_s;
    QTest::newRow("unicode escape braces") << u"zażółć"_s << u"\\u{17c}"_s << u"gu"_s << u"Ż"_s;
    QTest::newRow("optional group") << u"ab"_s << u"a(x)?b"_s << QString() << u"[$1]"_s;
    QTest::newRow("split spaces") << u" a  b c "_s << u"\\s+"_s << QString() << QString();
    QTest::newRow("split groups") << u"a1b2c"_s << u"(\\d)"_s << QString() << QString();
    QTest::newRow("split empty match") << u"abc"_s << u""_s << QString() << QString();
    QTest::newRow("empty string") << QString() << u"x"_s << QString() << QString();
    QTest::newRow("char classes") << u"[[link]] #tag"_s << u"\\[\\[([^\\]]+)\\]\\]"_s << u"g"_s << u"<$1>"_s;
}

void TstJsHelper::regExp()
{
    QFETCH(QString, s);
    QFETCH(QString, pattern);
    QFETCH(QString, flags);
    QFETCH(QString, replacement);
    const QString re = u"new RegExp("_s + lit(pattern) + u',' + lit(flags) + u')';
    const QString S = lit(s);
    QCOMPARE(js.test(s, pattern, flags), eval(re + u".test("_s + S + u')').toBool());
    QCOMPARE(js.search(s, pattern, flags), eval(S + u".search("_s + re + u')').toInt());
    QCOMPARE(js.match(s, pattern, flags),
             evalStrings(u"("_s + S + u".match("_s + re + u") || []).map(x => x === undefined ? \"\" : x)"_s));
    QCOMPARE(js.replaceRegExp(s, pattern, flags, replacement),
             evalString(S + u".replace("_s + re + u',' + lit(replacement) + u')'));
    QCOMPARE(js.splitRegExp(s, pattern, flags), evalStrings(S + u".split("_s + re + u')'));
}

void TstJsHelper::regExpBeyondTheEngine()
{
    // The s flag (the engine refuses it) and the split of "" by a pattern that matches "".
    QVERIFY(js.test(u"a\nb"_s, u"a.b"_s, u"s"_s));
    QVERIFY(!js.test(u"a\nb"_s, u"a.b"_s, QString()));
    QCOMPARE(js.splitRegExp(QString(), u"x*"_s, QString()), QStringList());
    // An invalid pattern matches nothing.
    QVERIFY(!js.test(u"(("_s, u"(("_s, QString()));
    QCOMPARE(js.replaceRegExp(u"(("_s, u"(("_s, u"g"_s, u"x"_s), u"(("_s);
}

void TstJsHelper::lists()
{
    const QVariantList list{1, u"ą"_s, true};
    QCOMPARE(js.appended(list, 2.5), (QVariantList{1, u"ą"_s, true, 2.5}));
    QCOMPARE(js.concat(list, {u"x"_s}), (QVariantList{1, u"ą"_s, true, u"x"_s}));
    QCOMPARE(js.removedAt(list, 1), (QVariantList{1, true}));
    QCOMPARE(js.removedAt(list, 3), list);
    QCOMPARE(js.removedAt(list, -1), list);
    QCOMPARE(js.insertedAt(list, 1, 0), (QVariantList{1, 0, u"ą"_s, true}));
    QCOMPARE(js.insertedAt(list, 9, 0), (QVariantList{1, u"ą"_s, true, 0}));
    QCOMPARE(js.insertedAt(list, -4, 0), (QVariantList{0, 1, u"ą"_s, true}));
    QCOMPARE(js.reversed(list), (QVariantList{true, u"ą"_s, 1}));
    QCOMPARE(js.reversed({}), QVariantList());
    QCOMPARE(js.filled(3, u"-"_s), (QVariantList{u"-"_s, u"-"_s, u"-"_s}));
    QCOMPARE(js.filled(-1, 0), QVariantList());
    QCOMPARE(js.range(4), (QList<int>{0, 1, 2, 3}));
    QCOMPARE(js.range(0), QList<int>());

    const QStringList strings{u"żaba"_s, u"Zebra"_s, u""_s, u"ąb"_s, u"ab"_s, u"żaba"_s, u"10"_s, u"9"_s};
    QCOMPARE(js.appendedString(strings, u"x"_s), strings + QStringList{u"x"_s});
    QCOMPARE(js.concatStrings({u"a"_s}, {u"b"_s, u"c"_s}), (QStringList{u"a"_s, u"b"_s, u"c"_s}));
    QCOMPARE(js.removedStringAt(strings, 0), strings.mid(1));
    QCOMPARE(js.insertedStringAt({u"a"_s, u"c"_s}, 1, u"b"_s), (QStringList{u"a"_s, u"b"_s, u"c"_s}));
    QCOMPARE(js.withoutString(strings, u"żaba"_s).size(), strings.size() - 2);
    QCOMPARE(js.sortedStrings(strings), evalStrings(litList(strings) + u".sort()"_s));
    QCOMPARE(js.uniqueStrings(strings), evalStrings(u"[...new Set("_s + litList(strings) + u")]"_s));
    QCOMPARE(js.reversedStrings(strings), evalStrings(litList(strings) + u".reverse()"_s));
    QCOMPARE(js.nonEmptyStrings(strings), evalStrings(litList(strings) + u".filter(s => s !== \"\")"_s));
}

void TstJsHelper::objects()
{
    const QVariantMap map{{u"b"_s, 2}, {u"a"_s, u"ą"_s}};
    QCOMPARE(js.keys(map), (QStringList{u"a"_s, u"b"_s}));
    QCOMPARE(js.keys({}), QStringList());
    QCOMPARE(js.merged(map, {{u"b"_s, 3}, {u"c"_s, true}}),
             (QVariantMap{{u"a"_s, u"ą"_s}, {u"b"_s, 3}, {u"c"_s, true}}));
    QCOMPARE(js.withKey(map, u"c"_s, 1), (QVariantMap{{u"a"_s, u"ą"_s}, {u"b"_s, 2}, {u"c"_s, 1}}));
    QCOMPARE(js.withoutKey(map, u"a"_s), (QVariantMap{{u"b"_s, 2}}));
    QCOMPARE(js.withoutKey(map, u"x"_s), map);
    QVERIFY(js.hasKey(map, u"a"_s));
    QVERIFY(!js.hasKey(map, u"c"_s));
}

void TstJsHelper::stringify_data()
{
    QTest::addColumn<QString>("literal");
    for (const char *c : {"1", "1.5", "-0", "\"zażółć \\\" \\\\ \\n\"", "true", "null", "[]", "{}", "[1,\"a\",null,[2]]",
                          "{\"a\":[true,{\"c\":\"ł\"}],\"b\":1}", "0/0", "1e21", "\"\\u0001\""}) {
        QTest::newRow(c) << QString::fromUtf8(c);
    }
}

void TstJsHelper::stringify()
{
    QFETCH(QString, literal);
    const QJSValue value = eval(u"("_s + literal + u')');
    QCOMPARE(js.stringify(value.toVariant(QJSValue::ConvertJSObjects)), evalString(u"JSON.stringify("_s + literal + u')'));
    QCOMPARE(js.stringify(QVariant()), QString());
}

QTEST_GUILESS_MAIN(TstJsHelper)
#include "tst_jshelper.moc"
