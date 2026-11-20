// Macros of extensions in the text (markdown/macros.h, docs/SYNTAX.md §13 "Macros"): what is one
// and what stays text (Logseq's macros, template variables, malformed and hostile input), their
// arguments, and how InlineRenderer shows them — pieces always escaped, placeholders as macro:
// links naming the macro's range, nothing of a macro in code.
#include "markdown/inlinerenderer.h"
#include "markdown/macros.h"

#include "testtiming.h"

#include <QTest>

using namespace brulion;
using namespace brulion::markdown;
using namespace Qt::StringLiterals;

namespace {

/// The macros of a text as "ext/name(arguments)@from-to" words.
QString describe(const QString &text)
{
    QStringList out;
    for (const MacroSpan &m : findMacros(text))
        out << m.extension + u'/' + m.name + u'(' + m.arguments + u")@"_s + QString::number(m.from) + u'-'
                        + QString::number(m.to);
    return out.join(u' ');
}

RenderContext context()
{
    RenderContext c;
    c.today = QDate(2026, 11, 20);
    Palette p;
    p.link = u"L"_s;
    p.unresolved = u"U"_s;
    p.muted = u"M"_s;
    p.codeBackground = u"C"_s;
    p.overdue = {u"OF"_s, u"OB"_s};
    p.reminder = {u"RF"_s, u"RB"_s};
    p.recurrence = {u"EF"_s, u"EB"_s};
    p.neutral = {u"NF"_s, u"NB"_s};
    p.due = {u"DF"_s, u"DB"_s};
    c.palette = p;
    c.resolvePage = [](const QString &target) -> std::optional<QString> {
        if (target == u"Brak")
            return std::nullopt;
        return target + u".md"_s;
    };
    // The macros of the tests' notebook: silownia/rekord, a few placeholders, hostile output.
    c.macro = [](const MacroSpan &m) -> MacroView {
        MacroView v;
        if (m.extension == u"silownia" && m.name == u"rekord") {
            v.pieces = {{MacroPiece::Kind::Text, u"120 kg × 3"_s, {}, {}, true},
                        {MacroPiece::Kind::Text, u" ("_s, {}, u"muted"_s, false},
                        {MacroPiece::Kind::Link, m.arguments, m.arguments, {}, false},
                        {MacroPiece::Kind::Text, u")"_s, {}, u"muted"_s, false}};
        } else if (m.name == u"zly") {
            v.pieces = {{MacroPiece::Kind::Text, u"<script>alert(1)</script> & \"x\""_s, {}, u"danger"_s, false},
                        {MacroPiece::Kind::Link, u"<b>"_s, u"Brak"_s, {}, false},
                        {MacroPiece::Kind::Code, u"a<b"_s, {}, {}, false}};
        } else if (m.name == u"pusty") {
            // nothing to show
        } else if (m.extension == u"nowe") {
            v.state = MacroView::State::Unapproved;
        } else if (m.extension == u"wylaczone") {
            v.state = MacroView::State::Off;
        } else if (m.name == u"blad") {
            v.state = MacroView::State::Error;
            v.message = u"blad:3: attempt to index nil with 'x'"_s;
        } else if (m.name == u"liczy") {
            v.state = MacroView::State::Pending;
        } else if (m.name == u"duzo") {
            v.state = MacroView::State::TooMany;
            v.message = u"200+"_s;
        } else {
            v.state = MacroView::State::Unknown;
        }
        return v;
    };
    return c;
}

QString render(const QString &text, const RenderContext &ctx = context())
{
    return InlineRenderer(ctx).render(text);
}

} // namespace

class TestMarkdownMacros : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void find_data();
    void find();
    void arguments_data();
    void arguments();
    void names();
    void source();
    void render_data();
    void render();
    void placeholderRanges();
    void unsetShowsSource();
    void viewText();
    void hostileInput();
    void viewBlock_data();
    void viewBlock();
    void inNote();
    void markdownOfView();
};

void TestMarkdownMacros::find_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("macros");

    QTest::newRow("one with an argument")
            << u"Rekord: {{ext:silownia/rekord Przysiad}}!"_s << u"silownia/rekord(Przysiad)@8-40"_s;
    QTest::newRow("no arguments") << u"{{ext:licznik-slow/dzis}}"_s << u"licznik-slow/dzis()@0-25"_s;
    QTest::newRow("polish arguments") << u"{{ext:nawyki/seria Czytanie książek, 30}}"_s
                                      << u"nawyki/seria(Czytanie książek, 30)@0-41"_s;
    QTest::newRow("blanks inside the braces") << u"{{  ext:a/b   x y  }}"_s << u"a/b(x y)@0-21"_s;
    QTest::newRow("prefix and id in any case") << u"{{EXT:Silownia/Rekord}}"_s << u"silownia/Rekord()@0-23"_s;
    QTest::newRow("polish name") << u"{{ext:waga/średnia_tygodnia}}"_s << u"waga/średnia_tygodnia()@0-29"_s;
    QTest::newRow("two on a line") << u"{{ext:a/b}} i {{ext:c/d 1}}"_s << u"a/b()@0-11 c/d(1)@14-27"_s;
    QTest::newRow("third brace closes after") << u"{{ext:a/b}}}"_s << u"a/b()@0-11"_s;
    QTest::newRow("open brace before") << u"{{{ext:a/b}}"_s << u"a/b()@1-12"_s;
    QTest::newRow("inner macro, the outer is text") << u"{{ext:a/b {{ext:c/d}} }}"_s << u"c/d()@10-21"_s;
    QTest::newRow("escaped") << u"\\{{ext:a/b}}"_s << QString();
    QTest::newRow("logseq query") << u"{{query (and [[Dom]] (task TODO))}}"_s << QString();
    QTest::newRow("logseq embed") << u"{{embed [[Dom]]}}"_s << QString();
    QTest::newRow("logseq renderer") << u"{{renderer :todomaster}}"_s << QString();
    QTest::newRow("logseq cloze") << u"{{cloze stolica}}"_s << QString();
    QTest::newRow("template variable") << u"{{date:yyyy-MM-dd}} {{title}}"_s << QString();
    QTest::newRow("template variable of an extension") << u"{{silownia/plan}}"_s << QString();
    QTest::newRow("templater") << u"<% tp.date.now() %>"_s << QString();
    QTest::newRow("no id") << u"{{ext:/rekord}}"_s << QString();
    QTest::newRow("no name") << u"{{ext:silownia/}}"_s << QString();
    QTest::newRow("no slash") << u"{{ext:silownia}}"_s << QString();
    QTest::newRow("blank in the id") << u"{{ext:sil ownia/rekord}}"_s << QString();
    QTest::newRow("character after the name") << u"{{ext:a/b!}}"_s << QString();
    QTest::newRow("unclosed") << u"{{ext:a/b x"_s << QString();
    QTest::newRow("across lines") << u"{{ext:a/b x\ny}}"_s << QString();
    QTest::newRow("single brace") << u"{ext:a/b}"_s << QString();
    QTest::newRow("too long") << u"{{ext:a/b "_s + QString(2000, u'x') + u"}}"_s << QString();
    QTest::newRow("just long enough") << u"{{ext:a/b "_s + QString(1984, u'x') + u"}}"_s
                                      << u"a/b("_s + QString(1984, u'x') + u")@0-1996"_s;
    QTest::newRow("empty") << QString() << QString();
}

void TestMarkdownMacros::find()
{
    QFETCH(QString, text);
    QFETCH(QString, macros);
    QCOMPARE(describe(text), macros);
}

void TestMarkdownMacros::arguments_data()
{
    QTest::addColumn<QString>("arguments");
    QTest::addColumn<QStringList>("list");

    QTest::newRow("none") << QString() << QStringList();
    QTest::newRow("blanks") << u"   "_s << QStringList();
    QTest::newRow("one with spaces") << u"Wyciskanie leżąc"_s << QStringList{u"Wyciskanie leżąc"_s};
    QTest::newRow("commas") << u"a, b ,c"_s << QStringList{u"a"_s, u"b"_s, u"c"_s};
    QTest::newRow("quoted comma") << u"a, \"b, c\""_s << QStringList{u"a"_s, u"b, c"_s};
    QTest::newRow("empty quoted") << u"\"\""_s << QStringList{QString()};
    QTest::newRow("empty between commas") << u"a,,b"_s << QStringList{u"a"_s, QString(), u"b"_s};
    QTest::newRow("unclosed quote") << u"\"a, b"_s << QStringList{u"\"a, b"_s};
    QTest::newRow("thirty-three") << QString(u"x,"_s.repeated(33) + u'y') << [] {
        QStringList l(31, u"x"_s);
        l << QString(u"x,"_s.repeated(2) + u'y');
        return l;
    }();
}

void TestMarkdownMacros::arguments()
{
    QFETCH(QString, arguments);
    QFETCH(QStringList, list);
    QCOMPARE(macroArguments(arguments), list);
    // An argument longer than 500 characters is cut.
    QCOMPARE(macroArguments(QString(800, u'ż')).value(0).size(), 500);
}

void TestMarkdownMacros::names()
{
    QVERIFY(isMacroName(u"rekord"));
    QVERIFY(isMacroName(u"seria-dni"));
    QVERIFY(isMacroName(u"średnia_1.5"));
    QVERIFY(isMacroName(u"7dni"));
    QVERIFY(!isMacroName(u""));
    QVERIFY(!isMacroName(u"-x"));
    QVERIFY(!isMacroName(u"a b"));
    QVERIFY(!isMacroName(u"a/b"));
    QVERIFY(!isMacroName(QString(65, u'a')));
}

void TestMarkdownMacros::source()
{
    QCOMPARE(macroSource(u"silownia"_s, u"rekord"_s, u"Przysiad"_s), u"{{ext:silownia/rekord Przysiad}}"_s);
    QCOMPARE(macroSource(u"a"_s, u"b"_s, QString()), u"{{ext:a/b}}"_s);
    // What macroSource() writes is found again, the same.
    const QList<MacroSpan> found = findMacros(macroSource(u"nawyki"_s, u"seria"_s, u"Czytanie, 7"_s));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found[0].arguments, u"Czytanie, 7"_s);
}

void TestMarkdownMacros::render_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("html");

    QTest::newRow("pieces") << u"Rekord: {{ext:silownia/rekord Przysiad}}"_s
                            << u"Rekord: <b>120 kg × 3</b><span style=\"color:M\"> (</span><a href=\"page:Przysiad\" "
                               u"style=\"color:L;text-decoration:none\">Przysiad</a><span style=\"color:M\">)</span>"_s;
    QTest::newRow("escaped") << u"{{ext:x/zly}}"_s
                             << u"<span style=\"color:OF\">&lt;script&gt;alert(1)&lt;/script&gt; &amp; &quot;x&quot;"
                                u"</span><a href=\"page:Brak\" style=\"color:U;text-decoration:none\">&lt;b&gt;</a>"
                                u"<code style=\"background-color:C\">a&lt;b</code>"_s;
    QTest::newRow("nothing to show") << u"{{ext:x/pusty}}"_s << u"<span style=\"color:M\">—</span>"_s;
    QTest::newRow("pending") << u"{{ext:x/liczy}}"_s << u"<span style=\"color:M\">…</span>"_s;
    QTest::newRow("in code it is code") << u"`{{ext:silownia/rekord}}`"_s
                                        << u"<code style=\"background-color:C\">{{ext:silownia/rekord}}</code>"_s;
    QTest::newRow("in a comment it is hidden") << u"a %%{{ext:silownia/rekord}}%% b"_s << u"a  b"_s;
    QTest::newRow("escaped brace") << u"\\{{ext:silownia/rekord}}"_s << u"{{ext:silownia/rekord}}"_s;
    QTest::newRow("in a link's label") << u"[{{ext:x/pusty}}](Dom.md)"_s
                                       << u"<a href=\"page:Dom.md\" style=\"color:L;text-decoration:none\">"
                                          u"<span style=\"color:M\">—</span></a>"_s;
    QTest::newRow("logseq macro stays text") << u"{{query (todo)}}"_s << u"{{query (todo)}}"_s;
}

void TestMarkdownMacros::render()
{
    QFETCH(QString, text);
    QFETCH(QString, html);
    QCOMPARE(::render(text), html);
}

void TestMarkdownMacros::placeholderRanges()
{
    // A placeholder is a macro: link naming the macro's range in the body (the editor's menu).
    const QString text = u"Zadanie {{ext:nowe/x}} i {{ext:wylaczone/y}} oraz {{ext:a/blad}}"_s;
    const QString html = ::render(text);
    QVERIFY2(html.contains(u"href=\"macro:8-22\""_s), qPrintable(html));
    QVERIFY(html.contains(u"🔒 nowe/x · uruchomić?"_s.replace(u' ', u"&nbsp;"_s)) || html.contains(u"uruchomić?"_s));
    QVERIFY2(html.contains(u"href=\"macro:25-44\""_s), qPrintable(html));
    QVERIFY(html.contains(u"wyłączone"_s));
    QVERIFY2(html.contains(u"href=\"macro:50-64\""_s), qPrintable(html));
    QVERIFY(html.contains(u"attempt"_s));
    QVERIFY(html.contains(u"OB"_s)); // the error in the overdue colours
    QCOMPARE(text.sliced(8, 14), u"{{ext:nowe/x}}"_s);
    // English labels.
    RenderContext en = context();
    en.locale = dates::UiLocale::English;
    QVERIFY(::render(u"{{ext:nowe/x}}"_s, en).contains(u"run?"_s));
    QVERIFY(::render(u"{{ext:nieznane/x}}"_s, en).contains(u"nieznane/\u2060x?"_s));
    QVERIFY(::render(u"{{ext:a/duzo}}"_s, en).contains(u"200+"_s));
    // In a task's text, after its checkbox and before its date chip, the ranges stay the body's.
    const QString task = u"[ ] Trening {{ext:nowe/x}} 📅 2026-11-21"_s;
    QVERIFY2(::render(task).contains(u"href=\"macro:12-26\""_s), qPrintable(::render(task)));
}

void TestMarkdownMacros::unsetShowsSource()
{
    // Exports, the clipboard, brulion-cli: no RenderContext::macro, the macro as written.
    RenderContext plain = context();
    plain.macro = nullptr;
    QCOMPARE(::render(u"x {{ext:silownia/rekord Przysiad}} y"_s, plain), u"x {{ext:silownia/rekord Przysiad}} y"_s);
    QCOMPARE(::render(u"{{ext:a/b <i>}}"_s, plain), u"{{ext:a/b &lt;i&gt;}}"_s);
}

void TestMarkdownMacros::viewText()
{
    MacroView v;
    v.pieces = {{MacroPiece::Kind::Text, u"3 dni "_s, {}, {}, false},
                {MacroPiece::Kind::Link, u"Czytanie"_s, u"Nawyki/Czytanie"_s, {}, false},
                {MacroPiece::Kind::Code, u"x"_s, {}, {}, false}};
    QCOMPARE(macroViewText(v), u"3 dni [[Nawyki/Czytanie|Czytanie]]`x`"_s);
    v.state = MacroView::State::Error;
    v.message = u"boom"_s;
    QCOMPARE(macroViewText(v), u"error: boom"_s);
}

void TestMarkdownMacros::hostileInput()
{
    // Thousands of openers, braces and half macros: linear, and only whole macros count.
    const QString text = QString(u"{{ext:"_s.repeated(20000) + u"{{ext:a/b}}"_s + u"{{"_s.repeated(20000)
                                 + u"{{ext:a/b "_s.repeated(5000));
    QList<qint64> samples;
    for (int run = 0; run < 3; ++run) {
        brulion::test::CpuTimer cpu;
        cpu.start();
        QCOMPARE(findMacros(text).size(), 1);
        samples << cpu.elapsed();
    }
    const qint64 ms = brulion::test::medianOf(samples);
    QVERIFY2(ms < 1000 * BRULION_TEST_SLOWDOWN, qPrintable(QString::number(ms)));
    QVERIFY(!::render(text).isEmpty());
}

void TestMarkdownMacros::viewBlock_data()
{
    QTest::addColumn<QString>("source");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<QString>("view");
    QTest::addColumn<QVariantMap>("params");
    QTest::addColumn<QString>("problem");

    QTest::newRow("a view") << u"silownia/dzis"_s << u"silownia"_s << u"dzis"_s << QVariantMap() << QString();
    QTest::newRow("params") << u"\n# komentarz\nSilownia/dzis\nćwiczenie: Wyciskanie leżąc\n// też\nseria:3\n"_s
                            << u"silownia"_s << u"dzis"_s
                            << QVariantMap{{u"ćwiczenie"_s, u"Wyciskanie leżąc"_s}, {u"seria"_s, u"3"_s}} << QString();
    QTest::newRow("view: prefix") << u"view: nawyki/miesiac"_s << u"nawyki"_s << u"miesiac"_s << QVariantMap()
                                  << QString();
    QTest::newRow("a key again") << u"a/b\nk: 1\nk: 2"_s << u"a"_s << u"b"_s << QVariantMap{{u"k"_s, u"2"_s}}
                                 << QString();
    QTest::newRow("a value with a colon")
            << u"a/b\nczas: 10:30"_s << u"a"_s << u"b"_s << QVariantMap{{u"czas"_s, u"10:30"_s}} << QString();
    QTest::newRow("a bad line") << u"a/b\nto nie jest para\nk: v"_s << u"a"_s << u"b"_s << QVariantMap{{u"k"_s, u"v"_s}}
                                << u"line 2: not key: value"_s;
    QTest::newRow("empty") << QString() << QString() << QString() << QVariantMap()
                           << u"the first line names the view, <extension>/<view>"_s;
    QTest::newRow("no slash") << u"silownia"_s << QString() << QString() << QVariantMap()
                              << u"line 1: the first line names the view, <extension>/<view>"_s;
    QTest::newRow("bad id") << u"sił ownia/dzis"_s << QString() << QString() << QVariantMap()
                            << u"line 1: the first line names the view, <extension>/<view>"_s;
    QTest::newRow("bad view") << u"a/b c"_s << QString() << QString() << QVariantMap()
                              << u"line 1: the first line names the view, <extension>/<view>"_s;
    QTest::newRow("windows line ends") << u"a/b\r\nk: v\r\n"_s << u"a"_s << u"b"_s << QVariantMap{{u"k"_s, u"v"_s}}
                                       << QString();
}

void TestMarkdownMacros::viewBlock()
{
    QFETCH(QString, source);
    QFETCH(QString, extension);
    QFETCH(QString, view);
    QFETCH(QVariantMap, params);
    QFETCH(QString, problem);
    const ViewBlock block = parseViewBlock(source);
    QCOMPARE(block.extension, extension);
    QCOMPARE(block.view, view);
    QCOMPARE(block.params, params);
    QCOMPARE(block.problem, problem);
}

void TestMarkdownMacros::inNote()
{
    // A whole note: macros in fenced code (a list item's too), code spans and comments are text.
    const QString note =
            u"# Trening\r\n- Rekord: {{ext:a/rekord Przysiad}}\r\n- ```\r\n  {{ext:a/w-kodzie}}\r\n  ```\r\n"
            u"`{{ext:a/span}}` %%{{ext:a/komentarz}}%% {{ext:a/b}}\r\n~~~~\r\n{{ext:a/x}}\r\n```\r\n"
            u"~~~~\r\nPo {{ext:a/po}}"_s;
    QStringList names;
    for (const MacroSpan &m : findMacrosInNote(note))
        names << m.name + u'@' + QString::number(m.from);
    QCOMPARE(names, (QStringList{u"rekord@21"_s, u"b@125"_s, u"po@171"_s}));
    QCOMPARE(note.sliced(21, 25), u"{{ext:a/rekord Przysiad}}"_s);
    // Replaced where a value is given; everything else byte for byte (\r\n, the other macros).
    const QString replaced = replaceMacros(note, [](const MacroSpan &m) -> std::optional<QString> {
        if (m.name == u"rekord")
            return u"**120 kg**"_s;
        return std::nullopt;
    });
    QCOMPARE(replaced, QString(note).replace(u"{{ext:a/rekord Przysiad}}"_s, u"**120 kg**"_s));
    QCOMPARE(replaceMacros(note, nullptr), note);
    QVERIFY(findMacrosInNote(QString()).isEmpty());
}

void TestMarkdownMacros::markdownOfView()
{
    MacroView v;
    v.pieces = {{MacroPiece::Kind::Text, u"120 kg × 3 *"_s, {}, {}, true},
                {MacroPiece::Kind::Text, u" [x] #tag"_s, {}, u"muted"_s, false},
                {MacroPiece::Kind::Link, u"Wyc|iskanie"_s, u"Ćwiczenia/Wyciskanie [leżąc]"_s, {}, true},
                {MacroPiece::Kind::Code, u"a`b"_s, {}, {}, false}};
    QCOMPARE(macroMarkdown(v), u"**120 kg × 3 \\*** \\[x\\] \\#tag[[Ćwiczenia/Wyciskanie leżąc|Wyciskanie]]`a'b`"_s);
    v.state = MacroView::State::Pending;
    QCOMPARE(macroMarkdown(v), QString());
}

QTEST_GUILESS_MAIN(TestMarkdownMacros)
#include "tst_markdown_macros.moc"
