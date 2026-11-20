// Luau syntax colours (syntax/luau.h): spans of every kind, the state long strings and comments
// carry from line to line, interpolated strings, Polish text, malformed and hostile code.
#include "syntax/luau.h"

#include "testtiming.h"

#include <QTest>

using namespace Qt::StringLiterals;
using brulion::syntax::LuauSpan;
using brulion::syntax::LuauState;
using brulion::syntax::LuauToken;

namespace {

/// The spans as "kind:text" words, in order: easy to read in a failure.
QString describe(QStringView text, const QList<LuauSpan> &spans)
{
    QStringList out;
    for (const LuauSpan &s : spans)
        out << brulion::syntax::luauTokenName(s.kind) + u':' + text.sliced(s.from, s.length).toString();
    return out.join(u" | ");
}

} // namespace

class TestSyntaxLuau : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void line_data();
    void line();
    void multiline_data();
    void multiline();
    void statePacking();
    void spansInOrder_data();
    void spansInOrder();
    void languages();
    void longLines();
};

void TestSyntaxLuau::line_data()
{
    QTest::addColumn<QString>("code");
    QTest::addColumn<QString>("spans");

    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("keywords") << u"local x = 1 if x then return end"_s
                              << u"keyword:local | number:1 | keyword:if | keyword:then | keyword:return | keyword:end"_s;
    QTest::newRow("constants") << u"x = nil or true and false"_s
                               << u"constant:nil | keyword:or | constant:true | keyword:and | constant:false"_s;
    QTest::newRow("b and its fields") << u"b.command(\"hi\", fn)"_s << u"builtin:b | api:command | string:\"hi\""_s;
    QTest::newRow("b.ui chain") << u"return b.ui.text(\"Żółw\")"_s
                                << u"keyword:return | builtin:b | api:ui | api:text | string:\"Żółw\""_s;
    QTest::newRow("a field named b is no b") << u"t.b.command()"_s << QString();
    QTest::newRow("b after concatenation") << u"x = a .. b.today()"_s << u"builtin:b | api:today"_s;
    QTest::newRow("library") << u"local n = string.len(s) + math.floor(2.5)"_s
                             << u"keyword:local | builtin:string | builtin:math | number:2.5"_s;
    QTest::newRow("library field is not coloured") << u"string.format(\"%d\", 3)"_s
                                                   << u"builtin:string | string:\"%d\" | number:3"_s;
    QTest::newRow("function name") << u"local function walk(blocks, depth)"_s
                                   << u"keyword:local | keyword:function | function:walk"_s;
    QTest::newRow("method name") << u"function M.go:run() end"_s
                                 << u"keyword:function | function:M.go:run | keyword:end"_s;
    QTest::newRow("anonymous function") << u"f = function(a) return a end"_s
                                        << u"keyword:function | keyword:return | keyword:end"_s;
    QTest::newRow("numbers") << u"x = 0x1F + 0b1010 + 1_000 + 2.5e-3 + .5"_s
                             << u"number:0x1F | number:0b1010 | number:1_000 | number:2.5e-3 | number:.5"_s;
    QTest::newRow("range is no number dot") << u"for i = 1, 10 do end"_s
                                            << u"keyword:for | number:1 | number:10 | keyword:do | keyword:end"_s;
    QTest::newRow("digit in a name") << u"local x2 = y3"_s << u"keyword:local"_s;
    QTest::newRow("strings with escapes") << uR"(s = "a \"q\" b" .. 'it\'s')"_s
                                          << uR"(string:"a \"q\" b" | string:'it\'s')"_s;
    QTest::newRow("unclosed quote") << u"s = \"zażółć gęślą"_s << u"string:\"zażółć gęślą"_s;
    QTest::newRow("long string on one line") << u"s = [==[ a ]] b ]==] .. x"_s << u"string:[==[ a ]] b ]==]"_s;
    QTest::newRow("comment") << u"x = 1 -- to jest komentarz: local"_s
                             << u"number:1 | comment:-- to jest komentarz: local"_s;
    QTest::newRow("long comment on one line") << u"--[[ a ]] local y"_s << u"comment:--[[ a ]] | keyword:local"_s;
    QTest::newRow("comment in a string") << u"s = \"-- no\""_s << u"string:\"-- no\""_s;
    QTest::newRow("interpolation") << u"return `Dziś: {b.today()} ({n + 1})`"_s
                                   << u"keyword:return | string:`Dziś:  | interpolation:{ | builtin:b | api:today "
                                      u"| interpolation:} | string: ( | interpolation:{ | number:1 | interpolation:} "
                                      u"| string:)`"_s;
    QTest::newRow("escaped brace in backticks") << u"s = `a \\{b} c`"_s << u"string:`a \\{b} c`"_s;
    QTest::newRow("table in an interpolation") << u"s = `{#{1, 2}}`"_s
                                               << u"string:` | interpolation:{ | number:1 | number:2 | "
                                                  u"interpolation:} | string:`"_s;
    QTest::newRow("nested backticks") << u"s = `a {`b {c}`} d`"_s
                                      << u"string:`a  | interpolation:{ | string:`b  | interpolation:{ | "
                                         u"interpolation:} | string:` | interpolation:} | string: d`"_s;
    QTest::newRow("continue and compound") << u"for _, v in list do if v then continue end; n += 1 end"_s
                                           << u"keyword:for | keyword:in | keyword:do | keyword:if | keyword:then "
                                              u"| keyword:continue | keyword:end | number:1 | keyword:end"_s;
    QTest::newRow("type annotations") << u"local n: number = 0"_s << u"keyword:local | number:0"_s;
    QTest::newRow("polish identifier bytes are no names") << u"local ż = 1"_s << u"keyword:local | number:1"_s;
    QTest::newRow("unbalanced brackets") << u")))]]}}{{[["_s << u"string:[["_s; // a long string opens
    QTest::newRow("lone backslash in backticks") << u"`\\"_s << u"string:`\\"_s;
}

void TestSyntaxLuau::line()
{
    QFETCH(QString, code);
    QFETCH(QString, spans);
    LuauState state;
    QCOMPARE(describe(code, brulion::syntax::lexLuauLine(code, &state)), spans);
}

void TestSyntaxLuau::multiline_data()
{
    QTest::addColumn<QStringList>("lines");
    QTest::addColumn<QStringList>("spans");
    QTest::addColumn<int>("endKind");

    QTest::newRow("long string") << QStringList{u"s = [["_s, u"local w środku"_s, u"]] .. 1"_s}
                                 << QStringList{u"string:[["_s, u"string:local w środku"_s, u"string:]] | number:1"_s}
                                 << int(LuauState::Code);
    QTest::newRow("leveled long comment") << QStringList{u"--[=[ start"_s, u"]] still"_s, u"]=] return"_s}
                                          << QStringList{u"comment:--[=[ start"_s, u"comment:]] still"_s,
                                                         u"comment:]=] | keyword:return"_s}
                                          << int(LuauState::Code);
    QTest::newRow("backtick continued") << QStringList{u"s = `jeden \\"_s, u"dwa {x}`"_s}
                                        << QStringList{u"string:`jeden \\"_s,
                                                       u"string:dwa  | interpolation:{ | interpolation:} | string:`"_s}
                                        << int(LuauState::Code);
    QTest::newRow("unclosed long string") << QStringList{u"x = [==["_s, u"a ]] b"_s}
                                          << QStringList{u"string:[==["_s, u"string:a ]] b"_s}
                                          << int(LuauState::LongString);
    QTest::newRow("unclosed long comment") << QStringList{u"--[[ a"_s} << QStringList{u"comment:--[[ a"_s}
                                           << int(LuauState::LongComment);
}

void TestSyntaxLuau::multiline()
{
    QFETCH(QStringList, lines);
    QFETCH(QStringList, spans);
    QFETCH(int, endKind);
    LuauState state;
    QStringList got;
    for (const QString &line : std::as_const(lines))
        got << describe(line, brulion::syntax::lexLuauLine(line, &state));
    QCOMPARE(got, spans);
    QCOMPARE(int(state.kind), endKind);

    // The whole text lexed at once gives the same spans, at the text's positions.
    const QString text = lines.join(u"\r\n");
    QStringList joined;
    for (const LuauSpan &s : brulion::syntax::lexLuau(text))
        joined << brulion::syntax::luauTokenName(s.kind) + u':' + text.sliced(s.from, s.length);
    QStringList expected;
    for (const QString &line : std::as_const(spans)) {
        if (!line.isEmpty())
            expected << line.split(u" | "_s);
    }
    QCOMPARE(joined, expected);
}

void TestSyntaxLuau::statePacking()
{
    for (const auto kind : {LuauState::Code, LuauState::LongString, LuauState::LongComment, LuauState::Backtick}) {
        for (const int level : {0, 1, 7, 255}) {
            const LuauState s{kind, level};
            QCOMPARE(LuauState::unpack(s.pack()), s);
            QVERIFY(s.pack() >= 0 && s.pack() < 1024);
        }
    }
    // A level beyond 255 is kept at 255 (a `[` and 300 `=` signs is no real code).
    QString code = u"x = ["_s + QString(300, u'=') + u'[';
    LuauState state;
    brulion::syntax::lexLuauLine(code, &state);
    QCOMPARE(state.kind, LuauState::LongString);
    QCOMPARE(state.level, 255);
}

void TestSyntaxLuau::spansInOrder_data()
{
    QTest::addColumn<QString>("code");
    QTest::newRow("mixed") << u"local t = { a = `x{b.y}`, [\"k\"] = 0x10 } -- c"_s;
    QTest::newRow("garbage") << u"`{`{`{`{\"'[[--[==[ ]] `}}}`"_s;
    QTest::newRow("deep interpolation") << QString(u"`{"_s.repeated(100));
    QTest::newRow("control characters") << u"\x0001\x0000local\x0007 = \"\x0000\""_s;
    QTest::newRow("surrogates") << u"s = \"😀\" -- 😀 \xd83d"_s;
}

void TestSyntaxLuau::spansInOrder()
{
    QFETCH(QString, code);
    LuauState state;
    const QList<LuauSpan> spans = brulion::syntax::lexLuauLine(code, &state);
    int end = 0;
    for (const LuauSpan &s : spans) {
        QVERIFY2(s.from >= end, qPrintable(describe(code, spans)));
        QVERIFY(s.length > 0);
        end = s.from + s.length;
        QVERIFY(end <= code.size());
    }
}

void TestSyntaxLuau::languages()
{
    QVERIFY(brulion::syntax::isLuauLanguage(u"luau"));
    QVERIFY(brulion::syntax::isLuauLanguage(u"Lua"));
    QVERIFY(brulion::syntax::isLuauLanguage(u"LUAU"));
    QVERIFY(!brulion::syntax::isLuauLanguage(u"lu"));
    QVERIFY(!brulion::syntax::isLuauLanguage(u"python"));
    QVERIFY(!brulion::syntax::isLuauLanguage(u""));
}

void TestSyntaxLuau::longLines()
{
    // A line of 200 000 characters of every kind lexes in linear time (a note may hold anything).
    const QString line = u"local x = `a{b.c}` .. \"s\" -- "_s.repeated(1) + u"x = [[ ]] y "_s.repeated(15000)
                         + u"`{"_s.repeated(5000);
    QList<qint64> samples;
    for (int run = 0; run < 3; ++run) {
        brulion::test::CpuTimer cpu;
        cpu.start();
        LuauState state;
        QVERIFY(!brulion::syntax::lexLuauLine(line, &state).isEmpty());
        samples << cpu.elapsed();
    }
    const qint64 ms = brulion::test::medianOf(samples);
    QVERIFY2(ms < 400 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(ms)));
}

QTEST_GUILESS_MAIN(TestSyntaxLuau)
#include "tst_syntax_luau.moc"
