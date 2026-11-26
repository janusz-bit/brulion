// The file viewer's syntax colours (docs/CODE-FILES.md "Syntax colours"): every lexer, the states
// carried over lines, garbage. A text's spans are written "kind:text" each, " " between spans,
// " / " between lines.
#include "codefiles/highlight.h"
#include "codefiles/viewer.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::codefiles;

namespace {

QString render(const QString &language, const QString &text)
{
    CodeState state;
    QStringList lines;
    for (const QString &line : text.split(u'\n')) {
        QStringList parts;
        int last = 0;
        for (const CodeSpan &s : lexCodeLine(language, line, &state)) {
            // (Spans in order, never overlapping, inside the line.)
            if (s.from < last || s.length <= 0 || s.from + s.length > line.size())
                return u"BAD SPAN"_s;
            last = s.from + s.length;
            parts.append(codeTokenName(s.kind) + u':' + line.mid(s.from, s.length));
        }
        lines.append(parts.join(u' '));
    }
    return lines.join(u" / "_s);
}

} // namespace

class TestCodeFilesHighlight : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void lexes_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("spans");
        // The C family.
        QTest::newRow("cpp include") << u"cpp"_s << u"#include <QString>"_s
                                     << u"preprocessor:#include string:<QString>"_s;
        QTest::newRow("cpp function") << u"cpp"_s << u"static int main(void) { return 0x1F; }"_s
                                      << u"keyword:static type:int function:main type:void keyword:return number:0x1F"_s;
        QTest::newRow("cpp strings") << u"cpp"_s << u"auto s = u\"żółw \\\" x\"; char c = 'a';"_s
                                     << u"type:auto string:u\"żółw \\\" x\" type:char string:'a'"_s;
        QTest::newRow("cpp comments") << u"cpp"_s << u"a /* b */ c // d"_s << u"comment:/* b */ comment:// d"_s;
        QTest::newRow("cpp block over lines") << u"cpp"_s << u"x /* a\nb\nc */ y"_s
                                              << u"comment:/* a / comment:b / comment:c */"_s;
        QTest::newRow("cpp raw string") << u"cpp"_s << u"R\"x(a)\"\n)x\" int"_s
                                        << u"string:R\"x(a)\" / string:)x\" type:int"_s;
        QTest::newRow("cpp class and constants") << u"cpp"_s << u"class Widget; MAX_SIZE nullptr"_s
                                                 << u"keyword:class type:Widget constant:MAX_SIZE constant:nullptr"_s;
        QTest::newRow("cpp numbers") << u"cpp"_s << u"1'000 2.5e-3f .5 1..2"_s
                                     << u"number:1'000 number:2.5e-3f number:.5 number:1 number:.2"_s;
        QTest::newRow("rust") << u"rust"_s << u"#[derive(Debug)] fn main<'a>(x: &'a str) { r#\"q\"# }"_s
                              << u"preprocessor:#[derive(Debug)] keyword:fn function:main variable:'a variable:'a type:str string:r#\"q\"#"_s;
        QTest::newRow("rust char") << u"rust"_s << u"let c = '\\n'; let d = 'x';"_s
                                   << u"keyword:let string:'\\n' keyword:let string:'x'"_s;
        QTest::newRow("rust nested comment") << u"rust"_s << u"/* a /* b */ c */ d"_s << u"comment:/* a /* b */ c */"_s;
        QTest::newRow("go raw string over lines") << u"go"_s << u"s := `a\\\nb` + x"_s << u"string:`a\\ / string:b`"_s;
        QTest::newRow("go func") << u"go"_s << u"func Run() error { return nil }"_s
                                 << u"keyword:func function:Run type:error keyword:return constant:nil"_s;
        QTest::newRow("java") << u"java"_s << u"@Override public String toString()"_s
                              << u"preprocessor:@Override keyword:public type:String function:toString"_s;
        QTest::newRow("kotlin triple") << u"kotlin"_s << u"val s = \"\"\"a\nb\"\"\" + 1"_s
                                       << u"keyword:val string:\"\"\"a / string:b\"\"\" number:1"_s;
        // JavaScript, TypeScript, QML.
        QTest::newRow("js template over lines") << u"js"_s << u"const t = `a ${b}\nc`; let x"_s
                                                << u"keyword:const string:`a ${b} / string:c` keyword:let"_s;
        QTest::newRow("ts") << u"ts"_s << u"interface Props { name: string }"_s
                            << u"keyword:interface type:Props type:string"_s;
        QTest::newRow("qml") << u"qml"_s << u"property int count: 0 // licznik"_s
                             << u"keyword:property type:int property:count number:0 comment:// licznik"_s;
        QTest::newRow("qml types") << u"qml"_s << u"Rectangle { color: \"red\" }"_s
                                   << u"type:Rectangle property:color string:\"red\""_s;
        // Python, shells.
        QTest::newRow("python") << u"python"_s << u"@dataclass\ndef f(x=None): return f\"{x}\" # ż"_s
                                << u"preprocessor:@dataclass / keyword:def function:f constant:None keyword:return string:f\"{x}\" comment:# ż"_s;
        QTest::newRow("python docstring") << u"python"_s << u"'''a\n# no comment\n''' True"_s
                                          << u"string:'''a / string:# no comment / string:''' constant:True"_s;
        QTest::newRow("sh") << u"sh"_s << u"echo \"$HOME\" ${A:-b} $1 a#b # c"_s
                            << u"builtin:echo string:\"$HOME\" variable:${A:-b} variable:$1 comment:# c"_s;
        QTest::newRow("sh keywords") << u"sh"_s << u"if [ -f x ]; then exit 1; fi"_s
                                     << u"keyword:if keyword:then keyword:exit number:1 keyword:fi"_s;
        QTest::newRow("cmake") << u"cmake"_s << u"SET(FOO ${BAR} \"x\") # c"_s
                               << u"keyword:SET constant:FOO variable:${BAR} string:\"x\" comment:# c"_s;
        QTest::newRow("nix") << u"nix"_s << u"let a = ''\n  x ''${y}\n''; in a"_s
                             << u"keyword:let string:'' / string:  x ''${y} / string:'' keyword:in"_s;
        QTest::newRow("sql") << u"sql"_s << u"SELECT name FROM t WHERE a = 'it''s' -- c"_s
                             << u"keyword:SELECT keyword:FROM keyword:WHERE string:'it''s' comment:-- c"_s;
        QTest::newRow("dockerfile") << u"dockerfile"_s << u"FROM node:20 AS build"_s
                                    << u"keyword:FROM number:20 keyword:AS"_s;
        QTest::newRow("makefile") << u"makefile"_s << u"all: $(CC) # x"_s << u"variable:$(CC) comment:# x"_s;
        // Markup and data.
        QTest::newRow("html") << u"html"_s << u"<a href=\"x\" disabled>Ż&amp;</a>"_s
                              << u"tag:<a attribute:href string:\"x\" attribute:disabled tag:> constant:&amp; tag:</a>"_s;
        QTest::newRow("html tag over lines") << u"html"_s << u"<div\n  class='x'>\n<!-- a\nb -->"_s
                                             << u"tag:<div / attribute:class string:'x' tag:> / comment:<!-- a / comment:b -->"_s;
        QTest::newRow("xml declaration") << u"xml"_s << u"<?xml version=\"1.0\"?><r/>"_s
                                         << u"preprocessor:<?xml version=\"1.0\"?> tag:<r/>"_s;
        QTest::newRow("json") << u"json"_s << u"{\"name\": \"Żółw\", \"n\": -1.5e3, \"ok\": true}"_s
                              << u"property:\"name\" string:\"Żółw\" property:\"n\" number:-1.5e3 property:\"ok\" constant:true"_s;
        QTest::newRow("yaml") << u"yaml"_s << u"name: test # c\n- run: echo \"x\"\nenabled: true\n---"_s
                              << u"property:name comment:# c / keyword:- property:run string:\"x\" / property:enabled constant:true / keyword:---"_s;
        QTest::newRow("yaml anchors") << u"yaml"_s << u"base: &b 12\nother: *b"_s
                                      << u"property:base variable:&b number:12 / property:other variable:*b"_s;
        QTest::newRow("toml") << u"toml"_s << u"[package]\nname = \"x\" # c\nv = 1"_s
                              << u"tag:[package] / property:name string:\"x\" comment:# c / property:v number:1"_s;
        QTest::newRow("toml multi-line") << u"toml"_s << u"s = \"\"\"a\nb\"\"\""_s
                                         << u"property:s string:\"\"\"a / string:b\"\"\""_s;
        QTest::newRow("ini") << u"ini"_s << u"; c\n[core]\neditor = vim"_s
                             << u"comment:; c / tag:[core] / property:editor"_s;
        QTest::newRow("markdown") << u"markdown"_s << u"# Tytuł\n- `kod` [x](a.md)\n```cpp\nint a;\n```\npo"_s
                                  << u"tag:# Tytuł / keyword:- string:`kod` attribute:a.md / comment:```cpp / string:int a; / comment:``` / "_s;
        QTest::newRow("diff") << u"diff"_s << u"--- a/x\n+++ b/x\n@@ -1 +1 @@ f\n-a\n+b\n c"_s
                              << u"keyword:--- a/x / keyword:+++ b/x / function:@@ -1 +1 @@ / deleted:-a / inserted:+b / "_s;
        QTest::newRow("css") << u"css"_s << u".btn:hover { color: #fff; margin: -2px !important; --x: 1 }"_s
                             << u"type:.btn property:color number:#fff property:margin number:-2px keyword:!important variable:--x number:1"_s;
        QTest::newRow("css at rule") << u"css"_s << u"@media (max-width: 600px) { a { width: calc(1px) } }"_s
                                     << u"keyword:@media property:max-width number:600px property:width function:calc number:1px"_s;
        QTest::newRow("gitignore") << u"gitignore"_s << u"# c\n!keep\nbuild/"_s << u"comment:# c / keyword:! / "_s;
        // Luau: core syntax's lexer, in this module's kinds.
        QTest::newRow("luau") << u"luau"_s << u"local x = b.command(\"a\") -- c"_s
                              << u"keyword:local builtin:b function:command string:\"a\" comment:-- c"_s;
        QTest::newRow("luau long string") << u"lua"_s << u"local s = [[a\nb]] .. 1"_s
                                          << u"keyword:local string:[[a / string:b]] number:1"_s;
        // Unknown languages and plain text: no spans.
        QTest::newRow("unknown") << u"xyz"_s << u"int main"_s << QString();
        QTest::newRow("text") << u"text"_s << u"if x"_s << QString();
        QTest::newRow("none") << QString() << u"if x"_s << QString();
    }
    void lexes()
    {
        QFETCH(QString, language);
        QFETCH(QString, text);
        QFETCH(QString, spans);
        QCOMPARE(render(language, text), spans);
    }

    // Garbage never fails nor leaves a span out of its line; open things stay open.
    void garbage_data()
    {
        QTest::addColumn<QString>("language");
        const QStringList all{u"cpp"_s,  u"c"_s,     u"rust"_s,     u"go"_s,       u"js"_s,  u"ts"_s,   u"qml"_s,
                              u"python"_s, u"sh"_s,  u"cmake"_s,    u"nix"_s,      u"sql"_s, u"html"_s, u"json"_s,
                              u"yaml"_s, u"toml"_s,  u"markdown"_s, u"diff"_s,     u"css"_s, u"scss"_s, u"luau"_s,
                              u"java"_s, u"kotlin"_s, u"swift"_s,   u"haskell"_s,  u"ocaml"_s, u"latex"_s,
                              u"powershell"_s, u"bat"_s, u"php"_s, u"clojure"_s, u"gitignore"_s, u"dockerfile"_s};
        for (const QString &l : all)
            QTest::newRow(qPrintable(l)) << l;
    }
    void garbage()
    {
        QFETCH(QString, language);
        QVERIFY(hasSyntax(language));
        const QStringList lines{u"\"unclosed"_s, u"/* open"_s, u"'''"_s, u"`"_s, u"R\"x("_s, u"r##\""_s, u"''"_s,
                                u"<!--"_s,      u"<a b='"_s, u"```"_s, u"\\"_s, u"$"_s, u"${"_s, u"#["_s, u"@"_s,
                                u"0x"_s,        u"1e"_s,     u"--[["_s, u"&;"_s, u"]]>"_s, u"żółw 🐢 ́"_s,
                                QString(12000, u'a'), QString()};
        CodeState state;
        for (int round = 0; round < 2; ++round) {
            for (const QString &line : lines) {
                for (const CodeSpan &s : lexCodeLine(language, line, &state)) {
                    QVERIFY(s.from >= 0 && s.length > 0);
                    QVERIFY(s.from + s.length <= line.size());
                    QVERIFY(s.from + s.length <= 10000);
                }
            }
        }
        // (A null state is allowed.)
        QVERIFY(lexCodeLine(language, u"x"_s, nullptr).size() >= 0);
    }

    void everyLanguageForPathIsKnown()
    {
        for (const char *name : {"a.c", "a.cpp", "a.cs", "a.java", "a.kt", "a.swift", "a.go", "a.rs", "a.js", "a.tsx",
                                 "a.qml", "a.py", "a.rb", "a.php", "a.lua", "a.luau", "a.sh", "a.ps1", "a.bat",
                                 "a.json", "a.yaml", "a.toml", "a.ini", "CMakeLists.txt", "a.nix", "Makefile",
                                 "Dockerfile", "a.md", "a.html", "a.xml", "a.css", "a.scss", "a.sql", "a.diff",
                                 "a.hs", "a.ml", "a.ex", "a.erl", "a.clj", "a.r", "a.jl", "a.tf", "a.proto",
                                 "a.graphql", "a.vim", "a.tex", "a.glsl", "a.wgsl", ".gitignore", "a.dart", "a.zig",
                                 "a.scala", "a.groovy", "a.vue", "a.svelte", "a.m", "a.pl", "a.fish", "a.zsh"})
            QVERIFY2(hasSyntax(languageForPath(QString::fromUtf8(name))), name);
        QVERIFY(!hasSyntax(u"text"));
        QVERIFY(!hasSyntax(QString()));
    }
};

QTEST_GUILESS_MAIN(TestCodeFilesHighlight)
#include "tst_codefiles_highlight.moc"
