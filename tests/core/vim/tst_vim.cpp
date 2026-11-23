// Tests for the Vim engine (core module vim): key sequences on a TextBuffer → its text and caret.
// The texts mark the caret with "|" (before the character it is on).
#include "vim/textbuffer.h"
#include "vim/vim.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::vim;

namespace {

/// `text` with "|" at the caret.
void load(TextBuffer &b, QString text)
{
    const int at = int(text.indexOf(u'|'));
    if (at >= 0)
        text.remove(at, 1);
    b.setText(text);
    if (at < 0)
        return;
    const QString before = text.left(at);
    const int line = int(before.count(u'\n'));
    const int column = int(before.size() - (before.lastIndexOf(u'\n') + 1));
    b.setCursor(Position{line, column});
}

QString dump(const TextBuffer &b)
{
    QStringList lines;
    for (int i = 0; i < b.lineCount(); ++i)
        lines.append(b.line(i));
    const Position c = b.cursor();
    lines[c.line].insert(c.column, u'|');
    return lines.join(u'\n');
}

/// The keys to the engine; what it leaves is the editor's typing (insert mode).
void type(Engine &e, TextBuffer &b, const QString &notation)
{
    for (const Key &k : keysFromNotation(notation)) {
        if (e.feed(k))
            continue;
        if (e.mode() != Mode::Insert)
            continue;
        const Position c = b.cursor();
        if (k.key == Qt::Key_Return) {
            b.replace(c, c, u"\n"_s);
            b.setCursor(Position{c.line + 1, 0});
        } else if (k.key == Qt::Key_Backspace) {
            if (c.column > 0) {
                b.replace(Position{c.line, c.column - 1}, c, {});
                b.setCursor(Position{c.line, c.column - 1});
            }
        } else if (!k.text.isEmpty() && k.text[0].unicode() >= 0x20) {
            b.replace(c, c, k.text);
            b.setCursor(Position{c.line, c.column + int(k.text.size())});
        }
    }
}

struct Fixture {
    Shared shared;
    TextBuffer buffer;
    Engine engine{&buffer, &shared};
    explicit Fixture(const QString &text) { load(buffer, text); }
    QString run(const QString &keys)
    {
        type(engine, buffer, keys);
        return dump(buffer);
    }
};

} // namespace

class TestVim : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keys_data();
    void keys();
    void modes_data();
    void modes();
    void undoGrouping();
    void dotRepeatAcrossCounts();
    void registers();
    void clipboardRegister();
    void commandLine();
    void substituteMessages();
    void marks();
    void searchWraps();
    void keysLeftToTheEditor();
    void polishLayoutKeys();
    void pendingAndCommandLineText();
    void readOnlyBuffer();
    void boundaries_data();
    void boundaries();
    void malformed_data();
    void malformed();
    void characters();
    void viewLines_data();
    void viewLines();
    void macroState();
    void macroNotation_data();
    void macroNotation();
    void macroStopsWhenItNeverEnds();
    void globalAnchorsFollowLines();
    void sentenceMessagesAndJumps();
};

void TestVim::keys_data()
{
    QTest::addColumn<QString>("before");
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("after");

    // Motions.
    QTest::newRow("w") << u"|Zażółć gęślą jaźń"_s << u"w"_s << u"Zażółć |gęślą jaźń"_s;
    QTest::newRow("2w") << u"|Zażółć gęślą jaźń"_s << u"2w"_s << u"Zażółć gęślą |jaźń"_s;
    QTest::newRow("b") << u"Zażółć gęślą |jaźń"_s << u"b"_s << u"Zażółć |gęślą jaźń"_s;
    QTest::newRow("e") << u"|Zażółć gęślą"_s << u"e"_s << u"Zażół|ć gęślą"_s;
    QTest::newRow("ee") << u"|Zażółć gęślą"_s << u"ee"_s << u"Zażółć gęśl|ą"_s;
    QTest::newRow("ge") << u"Zażółć |gęślą"_s << u"ge"_s << u"Zażół|ć gęślą"_s;
    QTest::newRow("w punctuation") << u"|foo.bar baz"_s << u"w"_s << u"foo|.bar baz"_s;
    QTest::newRow("W") << u"|foo.bar baz"_s << u"W"_s << u"foo.bar |baz"_s;
    QTest::newRow("B") << u"foo.bar |baz"_s << u"B"_s << u"|foo.bar baz"_s;
    QTest::newRow("E") << u"|foo.bar baz"_s << u"E"_s << u"foo.ba|r baz"_s;
    QTest::newRow("w empty line") << u"|abc\n\ndef"_s << u"w"_s << u"abc\n|\ndef"_s;
    QTest::newRow("2w empty line") << u"|abc\n\ndef"_s << u"2w"_s << u"abc\n\n|def"_s;
    QTest::newRow("w next line") << u"a |b\n  c"_s << u"w"_s << u"a b\n  |c"_s;
    QTest::newRow("b previous line") << u"a b\n  |c"_s << u"b"_s << u"a |b\n  c"_s;
    QTest::newRow("0") << u"  ab|c"_s << u"0"_s << u"|  abc"_s;
    QTest::newRow("^") << u"|  abc"_s << u"^"_s << u"  |abc"_s;
    QTest::newRow("$") << u"|abc def"_s << u"$"_s << u"abc de|f"_s;
    QTest::newRow("2$") << u"|ab\ncd"_s << u"2$"_s << u"ab\nc|d"_s;
    QTest::newRow("g_") << u"|ab  "_s << u"g_"_s << u"a|b  "_s;
    QTest::newRow("j") << u"a|bc\nlonger line\nx"_s << u"j"_s << u"abc\nl|onger line\nx"_s;
    QTest::newRow("jj clamps") << u"a|bc\nlonger line\nx"_s << u"jj"_s << u"abc\nlonger line\n|x"_s;
    QTest::newRow("jjk keeps the column") << u"a|bc\nlonger line\nx"_s << u"jjk"_s << u"abc\nl|onger line\nx"_s;
    QTest::newRow("$ then j") << u"|abc\nlonger\nxy"_s << u"$jj"_s << u"abc\nlonger\nx|y"_s;
    QTest::newRow("$jk") << u"|abc\nlonger"_s << u"$jk"_s << u"ab|c\nlonger"_s;
    QTest::newRow("5j past the end") << u"|a\nb\nc"_s << u"5j"_s << u"a\nb\n|c"_s;
    QTest::newRow("k at the top") << u"|a\nb"_s << u"k"_s << u"|a\nb"_s;
    QTest::newRow("gg") << u"a\nb\n|c"_s << u"gg"_s << u"|a\nb\nc"_s;
    QTest::newRow("gg first non-blank") << u"  a\nb\n|c"_s << u"gg"_s << u"  |a\nb\nc"_s;
    QTest::newRow("G") << u"|a\nb\nc"_s << u"G"_s << u"a\nb\n|c"_s;
    QTest::newRow("2G") << u"|a\nb\nc"_s << u"2G"_s << u"a\n|b\nc"_s;
    QTest::newRow("3gg") << u"|a\nb\nc"_s << u"3gg"_s << u"a\nb\n|c"_s;
    QTest::newRow("}") << u"|a\nb\n\nc\nd"_s << u"}"_s << u"a\nb\n|\nc\nd"_s;
    QTest::newRow("}}") << u"|a\nb\n\nc\nd"_s << u"}}"_s << u"a\nb\n\nc\n|d"_s;
    QTest::newRow("{") << u"a\nb\n\nc\n|d"_s << u"{"_s << u"a\nb\n|\nc\nd"_s;
    QTest::newRow("{{") << u"a\nb\n\nc\n|d"_s << u"{{"_s << u"|a\nb\n\nc\nd"_s;
    QTest::newRow("fę") << u"|zażółć gęślą"_s << u"fę"_s << u"zażółć g|ęślą"_s;
    QTest::newRow("tę") << u"|zażółć gęślą"_s << u"tę"_s << u"zażółć |gęślą"_s;
    QTest::newRow("f;") << u"|a-b-c-d"_s << u"f-;"_s << u"a-b|-c-d"_s;
    QTest::newRow("f;,") << u"|a-b-c-d"_s << u"f-;,"_s << u"a|-b-c-d"_s;
    QTest::newRow("2f") << u"|a-b-c-d"_s << u"2f-"_s << u"a-b|-c-d"_s;
    QTest::newRow("F") << u"a-b-|c"_s << u"F-"_s << u"a-b|-c"_s;
    QTest::newRow("T") << u"a-bc|d"_s << u"T-"_s << u"a-|bcd"_s;
    QTest::newRow("t;") << u"|a-b-c"_s << u"t-;"_s << u"a-|b-c"_s;
    QTest::newRow("f missing") << u"|abc"_s << u"fx"_s << u"|abc"_s;
    QTest::newRow("%") << u"|(a [b] c)"_s << u"%"_s << u"(a [b] c|)"_s;
    QTest::newRow("% back") << u"(a [b] c|)"_s << u"%"_s << u"|(a [b] c)"_s;
    QTest::newRow("% inner") << u"(a |[b] c)"_s << u"%"_s << u"(a [b|] c)"_s;
    QTest::newRow("% before") << u"|x (a\nb)"_s << u"%"_s << u"x (a\nb|)"_s;
    QTest::newRow("% lines back") << u"{\n  x\n|}"_s << u"%"_s << u"|{\n  x\n}"_s;
    QTest::newRow("50%") << u"|a\nb\nc\nd"_s << u"50%"_s << u"a\n|b\nc\nd"_s;
    QTest::newRow("3w count") << u"|a b c d e"_s << u"3w"_s << u"a b c |d e"_s;
    QTest::newRow("|") << u"|abcdef"_s << u"4|"_s << u"abc|def"_s;
    QTest::newRow("Enter") << u"|a\n  b"_s << u"<CR>"_s << u"a\n  |b"_s;
    QTest::newRow("+") << u"|a\n  b"_s << u"+"_s << u"a\n  |b"_s;
    QTest::newRow("-") << u"a\n  |b"_s << u"-"_s << u"|a\n  b"_s;
    QTest::newRow("arrows") << u"|ab\ncd"_s << u"<Right><Down><Left><Up>"_s << u"|ab\ncd"_s;
    QTest::newRow("End Home") << u"|abc"_s << u"<End>"_s << u"ab|c"_s;
    QTest::newRow("space") << u"|abc"_s << u"<Space><Space>"_s << u"ab|c"_s;
    QTest::newRow("l stops") << u"a|b"_s << u"5l"_s << u"a|b"_s;
    QTest::newRow("h stops") << u"a|b"_s << u"5h"_s << u"|ab"_s;
    QTest::newRow("*") << u"|foo bar foo"_s << u"*"_s << u"foo bar |foo"_s;
    QTest::newRow("* whole words") << u"|foo foobar foo"_s << u"*"_s << u"foo foobar |foo"_s;
    QTest::newRow("#") << u"foo bar |foo"_s << u"#"_s << u"|foo bar foo"_s;
    QTest::newRow("* wraps") << u"foo bar |foo"_s << u"*"_s << u"|foo bar foo"_s;
    QTest::newRow("*n") << u"|ab x ab y ab"_s << u"*n"_s << u"ab x ab y |ab"_s;
    QTest::newRow("/") << u"|jeden dwa trzy dwa"_s << u"/dwa<CR>"_s << u"jeden |dwa trzy dwa"_s;
    QTest::newRow("/ n") << u"|jeden dwa trzy dwa"_s << u"/dwa<CR>n"_s << u"jeden dwa trzy |dwa"_s;
    QTest::newRow("/ nN") << u"|jeden dwa trzy dwa"_s << u"/dwa<CR>nN"_s << u"jeden |dwa trzy dwa"_s;
    QTest::newRow("/ smart case") << u"|Ala ala"_s << u"/ala<CR>"_s << u"Ala |ala"_s;
    QTest::newRow("/ capital") << u"|x ala Ala"_s << u"/Ala<CR>"_s << u"x ala |Ala"_s;
    QTest::newRow("/ Polish") << u"|źdźbło żółw"_s << u"/żółw<CR>"_s << u"źdźbło |żółw"_s;
    QTest::newRow("/ next line") << u"|a\nb\nc a"_s << u"/a<CR>"_s << u"a\nb\nc |a"_s;
    QTest::newRow("/ edited") << u"|x dwa"_s << u"/dwx<BS>a<CR>"_s << u"x |dwa"_s;
    QTest::newRow("/ trailing slash") << u"|x dwa"_s << u"/dwa/<CR>"_s << u"x |dwa"_s;
    QTest::newRow("?") << u"jeden dwa |trzy"_s << u"?dwa<CR>"_s << u"jeden |dwa trzy"_s;
    QTest::newRow("/ not found") << u"|abc"_s << u"/zzz<CR>"_s << u"|abc"_s;
    QTest::newRow("Ctrl+D") << u"|1\n2\n3\n4\n5\n6"_s << u"<C-d>"_s << u"1\n2\n3\n4\n5\n|6"_s;

    // Deleting.
    QTest::newRow("dw") << u"|Zażółć gęślą jaźń"_s << u"dw"_s << u"|gęślą jaźń"_s;
    QTest::newRow("dw last word") << u"abc |def\nghi"_s << u"dw"_s << u"abc| \nghi"_s;
    QTest::newRow("dw last of buffer") << u"abc |def"_s << u"dw"_s << u"abc| "_s;
    QTest::newRow("d2w") << u"|a b c d"_s << u"d2w"_s << u"|c d"_s;
    QTest::newRow("2dw") << u"|a b c d"_s << u"2dw"_s << u"|c d"_s;
    QTest::newRow("2d2w") << u"|a b c d e"_s << u"2d2w"_s << u"|e"_s;
    QTest::newRow("de") << u"|foo bar"_s << u"de"_s << u"| bar"_s;
    QTest::newRow("db") << u"foo |bar"_s << u"db"_s << u"|bar"_s;
    QTest::newRow("dd") << u"a\n|b\nc"_s << u"dd"_s << u"a\n|c"_s;
    QTest::newRow("2dd") << u"|a\nb\nc"_s << u"2dd"_s << u"|c"_s;
    QTest::newRow("dd last") << u"a\n|b"_s << u"dd"_s << u"|a"_s;
    QTest::newRow("dd only") << u"|a"_s << u"dd"_s << u"|"_s;
    QTest::newRow("dd indented next") << u"|a\n  b"_s << u"dd"_s << u"  |b"_s;
    QTest::newRow("5dd past the end") << u"a\n|b\nc"_s << u"5dd"_s << u"|a"_s;
    QTest::newRow("dj") << u"|a\nb\nc"_s << u"dj"_s << u"|c"_s;
    QTest::newRow("dk") << u"a\n|b\nc"_s << u"dk"_s << u"|c"_s;
    QTest::newRow("dG") << u"a\n|b\nc"_s << u"dG"_s << u"|a"_s;
    QTest::newRow("dgg") << u"a\n|b\nc"_s << u"dgg"_s << u"|c"_s;
    QTest::newRow("d$") << u"ab|cd"_s << u"d$"_s << u"a|b"_s;
    QTest::newRow("D") << u"ab|cd"_s << u"D"_s << u"a|b"_s;
    QTest::newRow("2D") << u"ab|cd\nef\ngh"_s << u"2D"_s << u"a|b\ngh"_s;
    QTest::newRow("d0") << u"ab|cd"_s << u"d0"_s << u"|cd"_s;
    QTest::newRow("d^") << u"  ab|cd"_s << u"d^"_s << u"  |cd"_s;
    QTest::newRow("x") << u"|ąb"_s << u"x"_s << u"|b"_s;
    QTest::newRow("3x") << u"|abcd"_s << u"3x"_s << u"|d"_s;
    QTest::newRow("x at the end") << u"ab|c"_s << u"x"_s << u"a|b"_s;
    QTest::newRow("5x past the end") << u"a|bc"_s << u"5x"_s << u"|a"_s;
    QTest::newRow("X") << u"ab|c"_s << u"X"_s << u"a|c"_s;
    QTest::newRow("X at the start") << u"|abc"_s << u"X"_s << u"|abc"_s;
    QTest::newRow("Delete") << u"|abc"_s << u"<Del>"_s << u"|bc"_s;
    QTest::newRow("dfx") << u"|abcxd"_s << u"dfx"_s << u"|d"_s;
    QTest::newRow("dtx") << u"|abcxd"_s << u"dtx"_s << u"|xd"_s;
    QTest::newRow("dFx") << u"axbc|d"_s << u"dFx"_s << u"a|d"_s;
    QTest::newRow("d}") << u"|a\nb\n\nc"_s << u"d}"_s << u"|\nc"_s;
    QTest::newRow("d} mid line") << u"a|b\nc\n\nd"_s << u"d}"_s << u"|a\n\nd"_s;
    QTest::newRow("dip") << u"a\n|b\n\nc"_s << u"dip"_s << u"|\nc"_s;
    QTest::newRow("dap") << u"a\n|b\n\nc"_s << u"dap"_s << u"|c"_s;
    QTest::newRow("dap last") << u"a\n\n|b\nc"_s << u"dap"_s << u"|a"_s;
    QTest::newRow("diw") << u"foo |bar baz"_s << u"diw"_s << u"foo | baz"_s;
    QTest::newRow("diw Polish") << u"zażółć gę|ślą jaźń"_s << u"diw"_s << u"zażółć | jaźń"_s;
    QTest::newRow("daw") << u"foo |bar baz"_s << u"daw"_s << u"foo |baz"_s;
    QTest::newRow("daw last word") << u"foo |bar"_s << u"daw"_s << u"fo|o"_s;
    QTest::newRow("daw on blank") << u"foo| bar baz"_s << u"daw"_s << u"foo| baz"_s;
    QTest::newRow("d2aw") << u"|a b c d"_s << u"d2aw"_s << u"|c d"_s;
    QTest::newRow("diW") << u"a |b.c d"_s << u"diW"_s << u"a | d"_s;
    QTest::newRow("daW") << u"a |b.c d"_s << u"daW"_s << u"a |d"_s;
    QTest::newRow("di\"") << u"say \"hel|lo\" now"_s << u"di\""_s << u"say \"|\" now"_s;
    QTest::newRow("da\"") << u"say \"hel|lo\" now"_s << u"da\""_s << u"say |now"_s;
    QTest::newRow("di\" before") << u"|say \"hello\""_s << u"di\""_s << u"say \"|\""_s;
    QTest::newRow("di'") << u"x = 'ż|ółw'"_s << u"di'"_s << u"x = '|'"_s;
    QTest::newRow("di`") << u"run `c|md` now"_s << u"di`"_s << u"run `|` now"_s;
    QTest::newRow("di(") << u"f(a, |b)"_s << u"di("_s << u"f(|)"_s;
    QTest::newRow("dib") << u"f(a, |b)"_s << u"dib"_s << u"f(|)"_s;
    QTest::newRow("di) on paren") << u"f(a, b|)"_s << u"di)"_s << u"f(|)"_s;
    QTest::newRow("da(") << u"f(a, |b)"_s << u"da("_s << u"|f"_s;
    QTest::newRow("2di(") << u"f(a(b|c)d)"_s << u"2di("_s << u"f(|)"_s;
    QTest::newRow("di[") << u"a[1, |2]"_s << u"di["_s << u"a[|]"_s;
    QTest::newRow("da[") << u"a[1, |2] b"_s << u"da["_s << u"a| b"_s;
    QTest::newRow("di{ lines") << u"if {\n  |x\n  y\n}"_s << u"di{"_s << u"if {\n|}"_s;
    QTest::newRow("diB") << u"{a |b}"_s << u"diB"_s << u"{|}"_s;
    QTest::newRow("di<") << u"<a |b>"_s << u"di<"_s << u"<|>"_s;
    QTest::newRow("di( none") << u"a |b"_s << u"di("_s << u"a |b"_s;

    // Changing.
    QTest::newRow("cw") << u"|foo bar"_s << u"cwxyz<Esc>"_s << u"xy|z bar"_s;
    QTest::newRow("cw last char") << u"fo|o bar"_s << u"cwX<Esc>"_s << u"fo|X bar"_s;
    QTest::newRow("c2w") << u"|a b c"_s << u"c2wX<Esc>"_s << u"|X c"_s;
    QTest::newRow("cw on blank") << u"a|  b"_s << u"cwX<Esc>"_s << u"a|Xb"_s;
    QTest::newRow("ce") << u"|foo bar"_s << u"ceX<Esc>"_s << u"|X bar"_s;
    QTest::newRow("cc") << u"  |foo\nbar"_s << u"ccx<Esc>"_s << u"  |x\nbar"_s;
    QTest::newRow("2cc") << u"|a\nb\nc"_s << u"2ccx<Esc>"_s << u"|x\nc"_s;
    QTest::newRow("S") << u"|a\nb"_s << u"Sx<Esc>"_s << u"|x\nb"_s;
    QTest::newRow("C") << u"a|bc"_s << u"Cx<Esc>"_s << u"a|x"_s;
    QTest::newRow("s") << u"|abc"_s << u"sx<Esc>"_s << u"|xbc"_s;
    QTest::newRow("3s") << u"|abcd"_s << u"3sx<Esc>"_s << u"|xd"_s;
    QTest::newRow("ci\"") << u"say \"hel|lo\" now"_s << u"ci\"X<Esc>"_s << u"say \"|X\" now"_s;
    QTest::newRow("ci{ lines") << u"if {\n  |x\n}"_s << u"ci{y<Esc>"_s << u"if {\n  |y\n}"_s;
    QTest::newRow("ciw Polish") << u"|źdźbło x"_s << u"ciwżółw<Esc>"_s << u"żół|w x"_s;
    QTest::newRow("c$") << u"a|bc"_s << u"c$x<Esc>"_s << u"a|x"_s;
    QTest::newRow("cj") << u"|a\nb\nc"_s << u"cjx<Esc>"_s << u"|x\nc"_s;
    QTest::newRow("c Enter") << u"|ab"_s << u"clx<CR>y<Esc>"_s << u"x\n|yb"_s;

    // Yanking and putting.
    QTest::newRow("yyp") << u"|a\nb"_s << u"yyp"_s << u"a\n|a\nb"_s;
    QTest::newRow("yyP") << u"a\n|b"_s << u"yyP"_s << u"a\n|b\nb"_s;
    QTest::newRow("2yyp") << u"|a\nb\nc"_s << u"2yyGp"_s << u"a\nb\nc\n|a\nb"_s;
    QTest::newRow("Yp") << u"|a\nb"_s << u"Yp"_s << u"a\n|a\nb"_s;
    QTest::newRow("ywP") << u"|foo bar"_s << u"ywP"_s << u"foo| foo bar"_s;
    QTest::newRow("ylp") << u"|ab"_s << u"ylp"_s << u"a|ab"_s;
    QTest::newRow("yl3p") << u"|x"_s << u"yl3p"_s << u"xxx|x"_s;
    QTest::newRow("3yyp count") << u"|a"_s << u"yy3p"_s << u"a\n|a\na\na"_s;
    QTest::newRow("ddp") << u"|a\nb\nc"_s << u"ddp"_s << u"b\n|a\nc"_s;
    QTest::newRow("xp") << u"|ab"_s << u"xp"_s << u"b|a"_s;
    QTest::newRow("yiw keeps the caret at the word") << u"foo b|ar"_s << u"yiw"_s << u"foo |bar"_s;
    QTest::newRow("yy keeps the caret") << u"a|b\ncd"_s << u"yy"_s << u"a|b\ncd"_s;
    QTest::newRow("yj") << u"a|b\ncd"_s << u"yjjp"_s << u"ab\ncd\n|ab\ncd"_s;
    QTest::newRow("gp") << u"|a\nb"_s << u"yygp"_s << u"a\na\n|b"_s;
    QTest::newRow("gP") << u"a\n|b"_s << u"yykgP"_s << u"b\n|a\nb"_s;
    QTest::newRow("p multi-line charwise") << u"|ab\ncd"_s << u"vjy$p"_s << u"ab|ab\nc\ncd"_s;
    QTest::newRow("p empty line") << u"|\nx"_s << u"jylkp"_s << u"|x\nx"_s;

    // Registers.
    QTest::newRow("\"a") << u"|a\nb"_s << u"\"ayyj\"ap"_s << u"a\nb\n|a"_s;
    QTest::newRow("\"A appends") << u"|a\nb"_s << u"\"ayyj\"Ayy\"ap"_s << u"a\nb\n|a\nb"_s;
    QTest::newRow("\"_ black hole") << u"|a\nb"_s << u"yyj\"_ddp"_s << u"a\n|a"_s;
    QTest::newRow("\"2 numbered") << u"|a\nb\nc"_s << u"dddd\"2p"_s << u"c\n|a"_s;
    QTest::newRow("\"0 yank") << u"|a\nb"_s << u"yyjdd\"0p"_s << u"a\n|a"_s;
    QTest::newRow("\"- small delete") << u"|ab"_s << u"x\"-p"_s << u"b|a"_s;
    QTest::newRow("\". last insert") << u"|"_s << u"ixy<Esc>\".p"_s << u"xyx|y"_s;
    QTest::newRow("count before register") << u"|a\nb"_s << u"2\"ayyG\"ap"_s << u"a\nb\n|a\nb"_s;

    // Joining.
    QTest::newRow("J") << u"|a\n  b"_s << u"J"_s << u"a| b"_s;
    QTest::newRow("3J") << u"|a\nb\nc"_s << u"3J"_s << u"a b| c"_s;
    QTest::newRow("J trailing space") << u"|a \nb"_s << u"J"_s << u"a| b"_s;
    QTest::newRow("J empty next") << u"|a\n\nb"_s << u"J"_s << u"|a\nb"_s;
    QTest::newRow("J )") << u"|f(a\n)"_s << u"J"_s << u"f(a|)"_s;
    QTest::newRow("gJ") << u"|a\n  b"_s << u"gJ"_s << u"a|  b"_s;
    QTest::newRow("J last line") << u"a\n|b"_s << u"J"_s << u"a\n|b"_s;

    // Replacing, case.
    QTest::newRow("r") << u"|abc"_s << u"rx"_s << u"|xbc"_s;
    QTest::newRow("3rź") << u"|abcd"_s << u"3rź"_s << u"źź|źd"_s;
    QTest::newRow("rą") << u"|a"_s << u"rą"_s << u"|ą"_s;
    QTest::newRow("r too few") << u"a|b"_s << u"3rx"_s << u"a|b"_s;
    QTest::newRow("r Enter") << u"a|bc"_s << u"r<CR>"_s << u"a\n|c"_s;
    QTest::newRow("~") << u"|ąbc"_s << u"~"_s << u"Ą|bc"_s;
    QTest::newRow("3~") << u"|ąbc"_s << u"3~"_s << u"ĄB|C"_s;
    QTest::newRow("g~iw") << u"|żółw x"_s << u"g~iw"_s << u"|ŻÓŁW x"_s;
    QTest::newRow("gUU") << u"|ab cd"_s << u"gUU"_s << u"|AB CD"_s;
    QTest::newRow("gUgU") << u"|ab"_s << u"gUgU"_s << u"|AB"_s;
    QTest::newRow("guiw") << u"|ŻÓŁW"_s << u"guiw"_s << u"|żółw"_s;
    QTest::newRow("gUw") << u"|ab cd"_s << u"gUw"_s << u"|AB cd"_s;
    QTest::newRow("g~~") << u"a|B"_s << u"g~~"_s << u"A|b"_s;

    // Shifting.
    QTest::newRow(">>") << u"|a"_s << u">>"_s << u"\t|a"_s;
    QTest::newRow("<<") << u"\t\t|a"_s << u"<<"_s << u"\t|a"_s;
    QTest::newRow("<< spaces") << u"        |a"_s << u"<<"_s << u"|a"_s;
    QTest::newRow("3>>") << u"|a\nb\nc\nd"_s << u"3>>"_s << u"\t|a\n\tb\n\tc\nd"_s;
    QTest::newRow(">j") << u"|a\nb\nc"_s << u">j"_s << u"\t|a\n\tb\nc"_s;
    QTest::newRow(">ip") << u"|a\nb\n\nc"_s << u">ip"_s << u"\t|a\n\tb\n\nc"_s;
    QTest::newRow(">> empty line") << u"|"_s << u">>"_s << u"|"_s;

    // Inserting.
    QTest::newRow("i") << u"a|b"_s << u"ix<Esc>"_s << u"a|xb"_s;
    QTest::newRow("a") << u"|ab"_s << u"ax<Esc>"_s << u"a|xb"_s;
    QTest::newRow("a empty") << u"|"_s << u"ax<Esc>"_s << u"|x"_s;
    QTest::newRow("A") << u"|ab"_s << u"Ax<Esc>"_s << u"ab|x"_s;
    QTest::newRow("I") << u"  a|b"_s << u"Ix<Esc>"_s << u"  |xab"_s;
    QTest::newRow("o") << u"|a\nb"_s << u"ox<Esc>"_s << u"a\n|x\nb"_s;
    QTest::newRow("O") << u"a\n|b"_s << u"Ox<Esc>"_s << u"a\n|x\nb"_s;
    QTest::newRow("o indent") << u"  |a"_s << u"ox<Esc>"_s << u"  a\n  |x"_s;
    QTest::newRow("Esc at the start") << u"|ab"_s << u"i<Esc>"_s << u"|ab"_s;
    QTest::newRow("Ctrl+[") << u"|a"_s << u"ix<C-[>"_s << u"|xa"_s;
    QTest::newRow("3i") << u"|a"_s << u"3ix<Esc>"_s << u"xx|xa"_s;
    QTest::newRow("3a") << u"|a"_s << u"3ab<Esc>"_s << u"abb|b"_s;
    QTest::newRow("2o") << u"|a"_s << u"2ox<Esc>"_s << u"a\nx\n|x"_s;
    QTest::newRow("Insert key") << u"a|b"_s << u"<Insert>x<Esc>"_s << u"a|xb"_s;
    QTest::newRow("i Enter") << u"a|b"_s << u"i<CR><Esc>"_s << u"a\n|b"_s;
    QTest::newRow("Polish insert") << u"|"_s << u"iZażółć gęślą jaźń<Esc>"_s << u"Zażółć gęślą jaź|ń"_s;
    QTest::newRow("gi") << u"|ab\ncd"_s << u"Ax<Esc>jgiy<Esc>"_s << u"abx|y\ncd"_s;

    // Replace mode.
    QTest::newRow("R") << u"|abc"_s << u"Rxy<Esc>"_s << u"x|yc"_s;
    QTest::newRow("R Backspace") << u"|abc"_s << u"Rxy<BS><Esc>"_s << u"|xbc"_s;
    QTest::newRow("R past the end") << u"|a"_s << u"Rxyz<Esc>"_s << u"xy|z"_s;
    QTest::newRow("R Polish") << u"|abc"_s << u"Rżó<Esc>"_s << u"ż|óc"_s;
    QTest::newRow("2R") << u"|abcdef"_s << u"2Rxy<Esc>"_s << u"xyx|yef"_s;

    // Visual mode.
    QTest::newRow("ved") << u"|foo bar"_s << u"ved"_s << u"| bar"_s;
    QTest::newRow("vjd") << u"a|bc\ndef"_s << u"vjd"_s << u"a|f"_s;
    QTest::newRow("Vd") << u"a\n|b\nc"_s << u"Vd"_s << u"a\n|c"_s;
    QTest::newRow("Vjd") << u"|a\nb\nc"_s << u"Vjd"_s << u"|c"_s;
    QTest::newRow("vy then p") << u"|ab"_s << u"vly$p"_s << u"aba|b"_s;
    QTest::newRow("viwc") << u"foo |bar baz"_s << u"viwcX<Esc>"_s << u"foo |X baz"_s;
    QTest::newRow("Vj>") << u"|a\nb"_s << u"Vj>"_s << u"\t|a\n\tb"_s;
    QTest::newRow("V2>") << u"|a"_s << u"V2>"_s << u"\t\t|a"_s;
    QTest::newRow("vU") << u"|abc"_s << u"vlU"_s << u"|ABc"_s;
    QTest::newRow("vu") << u"|ABC"_s << u"vlu"_s << u"|abC"_s;
    QTest::newRow("v~") << u"|aBc"_s << u"vl~"_s << u"|Abc"_s;
    QTest::newRow("vr") << u"|abc"_s << u"vlrx"_s << u"|xxc"_s;
    QTest::newRow("vo") << u"a|bcd"_s << u"vllohd"_s << u"|"_s;
    QTest::newRow("VjjJ") << u"|a\nb\nc"_s << u"VjjJ"_s << u"a b| c"_s;
    QTest::newRow("vp") << u"|foo bar"_s << u"yiwwviwp"_s << u"foo fo|o"_s;
    QTest::newRow("vp then p") << u"|foo bar"_s << u"yiwwviwp0P"_s << u"ba|rfoo foo"_s;
    QTest::newRow("Vp") << u"|a\nb"_s << u"yyjVp"_s << u"a\n|a"_s;
    QTest::newRow("gv") << u"|abc"_s << u"vl<Esc>gvd"_s << u"|c"_s;
    QTest::newRow("vv leaves") << u"|abc"_s << u"vlvx"_s << u"a|c"_s;
    QTest::newRow("vV switches") << u"a|b\ncd"_s << u"vVd"_s << u"|cd"_s;
    QTest::newRow("vx") << u"|abc"_s << u"vlx"_s << u"|c"_s;
    QTest::newRow("vX lines") << u"a|b\ncd"_s << u"vX"_s << u"|cd"_s;
    QTest::newRow("vY") << u"a|b\ncd"_s << u"vYjp"_s << u"ab\ncd\n|ab"_s;
    QTest::newRow("vap") << u"|a\nb\n\nc"_s << u"vapd"_s << u"|c"_s;
    QTest::newRow("vi(") << u"f(a|b)"_s << u"vi(d"_s << u"f(|)"_s;
    QTest::newRow("v$") << u"|ab\ncd"_s << u"v$d"_s << u"|cd"_s;
    QTest::newRow("vJ two lines") << u"|a\nb"_s << u"vJ"_s << u"a| b"_s;
    QTest::newRow("Vs") << u"|a\nb"_s << u"VSx<Esc>"_s << u"|x\nb"_s;
    QTest::newRow("v empty line") << u"a\n|\nb"_s << u"vd"_s << u"a\n|b"_s;

    // The dot repeat.
    QTest::newRow(". dw") << u"|a b c d"_s << u"dw."_s << u"|c d"_s;
    QTest::newRow(". dd") << u"|a\nb\nc"_s << u"dd."_s << u"|c"_s;
    QTest::newRow(". x") << u"|abcd"_s << u"x.."_s << u"|d"_s;
    QTest::newRow(". cw") << u"|foo bar"_s << u"cwX<Esc>w."_s << u"X |X"_s;
    QTest::newRow(". A") << u"|a\nb"_s << u"A;<Esc>j."_s << u"a;\nb|;"_s;
    QTest::newRow(". count") << u"|a b c d e f"_s << u"dw3."_s << u"|e f"_s;
    QTest::newRow(". o") << u"|a"_s << u"ox<Esc>."_s << u"a\nx\n|x"_s;
    QTest::newRow(". 3i") << u"|a"_s << u"3ix<Esc>."_s << u"xxxx|xxa"_s;
    QTest::newRow(". visual") << u"|abcdef"_s << u"vld."_s << u"|ef"_s;
    QTest::newRow(". visual lines") << u"|a\nb\nc\nd\ne"_s << u"Vjd."_s << u"|e"_s;
    QTest::newRow(". >>") << u"|a"_s << u">>."_s << u"\t\t|a"_s;
    QTest::newRow(". J") << u"|a\nb\nc"_s << u"J."_s << u"a b| c"_s;
    QTest::newRow(". r") << u"|abc"_s << u"rxl."_s << u"x|xc"_s;
    QTest::newRow(". ~") << u"|abc"_s << u"~."_s << u"AB|c"_s;
    QTest::newRow(". p") << u"|a"_s << u"yyp."_s << u"a\na\n|a"_s;
    QTest::newRow(". ciw") << u"|foo bar"_s << u"ciwX<Esc>w."_s << u"X |X"_s;
    QTest::newRow(". visual c") << u"|ab cd"_s << u"vlcX<Esc>w."_s << u"X |X"_s;
    QTest::newRow(". R") << u"|abcd"_s << u"Rxy<Esc>l."_s << u"xyx|y"_s;
    QTest::newRow(". d/") << u"|a x b x c"_s << u"d/x<CR>w."_s << u"x |x c"_s;
    QTest::newRow(". :s") << u"|a a\na a"_s << u":s/a/b/<CR>j&"_s << u"b a\n|b a"_s;
    QTest::newRow(". after u") << u"|a b c"_s << u"dwu."_s << u"|b c"_s;
    QTest::newRow(". nothing") << u"|abc"_s << u"."_s << u"|abc"_s;

    // Undo and redo.
    QTest::newRow("u") << u"|a b c"_s << u"dwu"_s << u"|a b c"_s;
    QTest::newRow("uu") << u"|a b c"_s << u"dwdwuu"_s << u"|a b c"_s;
    QTest::newRow("u Ctrl+R") << u"|a b c"_s << u"dwu<C-r>"_s << u"|b c"_s;
    QTest::newRow("u cw") << u"|foo bar"_s << u"cwxyz<Esc>u"_s << u"|foo bar"_s;
    QTest::newRow("u insert") << u"|a"_s << u"ixyz<Esc>u"_s << u"|a"_s;
    QTest::newRow("u insert Enter") << u"|a"_s << u"ix<CR>y<Esc>u"_s << u"|a"_s;
    QTest::newRow("u o") << u"|a"_s << u"ofoo<Esc>u"_s << u"|a"_s;
    QTest::newRow("u visual") << u"|ab\ncd"_s << u"Vjdu"_s << u"|ab\ncd"_s;
    QTest::newRow("2u") << u"|abc"_s << u"xxx2u"_s << u"|bc"_s;
    QTest::newRow("u nothing") << u"|abc"_s << u"u"_s << u"|abc"_s;
    QTest::newRow("u :s") << u"|a a\na a"_s << u":%s/a/b/g<CR>u"_s << u"|a a\na a"_s;
    QTest::newRow("u 3dd") << u"|a\nb\nc\nd"_s << u"3ddu"_s << u"|a\nb\nc\nd"_s;
    QTest::newRow("u J") << u"|a\nb\nc"_s << u"3Ju"_s << u"|a\nb\nc"_s;
    QTest::newRow("u R") << u"|abc"_s << u"Rxy<Esc>u"_s << u"|abc"_s;

    // The command line.
    QTest::newRow(":s") << u"|aaa"_s << u":s/a/b/<CR>"_s << u"|baa"_s;
    QTest::newRow(":s g") << u"|aaa"_s << u":s/a/b/g<CR>"_s << u"|bbb"_s;
    QTest::newRow(":%s") << u"|x\nx x"_s << u":%s/x/y/g<CR>"_s << u"y\n|y y"_s;
    QTest::newRow(":s plain text") << u"|abc a.c"_s << u":s/a.c/X/<CR>"_s << u"|abc X"_s;
    QTest::newRow(":s &") << u"|ab"_s << u":s/ab/[&]/<CR>"_s << u"|[ab]"_s;
    QTest::newRow(":s \\&") << u"|ab"_s << u":s/ab/\\&/<CR>"_s << u"|&"_s;
    QTest::newRow(":s Polish") << u"|żaba żuk"_s << u":s/ż/rz/g<CR>"_s << u"|rzaba rzuk"_s;
    QTest::newRow(":s delimiter #") << u"|a/b"_s << u":s#/#-#<CR>"_s << u"|a-b"_s;
    QTest::newRow(":s escaped slash") << u"|a/b"_s << u":s/\\//-/<CR>"_s << u"|a-b"_s;
    QTest::newRow(":s \\r") << u"|a,b"_s << u":s/,/\\r/g<CR>"_s << u"a\n|b"_s;
    QTest::newRow(":s i") << u"|Ab ab"_s << u":s/AB/x/gi<CR>"_s << u"|x x"_s;
    QTest::newRow(":s smart case") << u"|Ab ab"_s << u":s/ab/x/g<CR>"_s << u"|x x"_s;
    QTest::newRow(":s case") << u"|Ab ab"_s << u":s/Ab/x/g<CR>"_s << u"|x ab"_s;
    QTest::newRow(":s n") << u"|aaa"_s << u":s/a/b/gn<CR>"_s << u"|aaa"_s;
    QTest::newRow(":s empty replacement") << u"|a-b"_s << u":s/-//<CR>"_s << u"|ab"_s;
    QTest::newRow(":s range") << u"|a\na\na\na"_s << u":2,3s/a/b/<CR>"_s << u"a\nb\n|b\na"_s;
    QTest::newRow(":s .,+1") << u"a\n|a\na\na"_s << u":.,+1s/a/b/<CR>"_s << u"a\nb\n|b\na"_s;
    QTest::newRow(":s .,$") << u"a\n|a\na"_s << u":.,$s/a/b/<CR>"_s << u"a\nb\n|b"_s;
    QTest::newRow(":'<,'>s") << u"|a\na\na"_s << u"Vj:s/a/b/<CR>"_s << u"b\n|b\na"_s;
    QTest::newRow(":s repeat :s") << u"|a a\na a"_s << u":s/a/b/<CR>j:s<CR>"_s << u"b a\n|b a"_s;
    QTest::newRow(":s empty pattern") << u"|x ab ab"_s << u"/ab<CR>:s//c/g<CR>"_s << u"|x c c"_s;
    QTest::newRow(":2") << u"|a\nb\nc"_s << u":2<CR>"_s << u"a\n|b\nc"_s;
    QTest::newRow(":$") << u"|a\nb\nc"_s << u":$<CR>"_s << u"a\nb\n|c"_s;
    QTest::newRow(":d") << u"|a\nb"_s << u":d<CR>"_s << u"|b"_s;
    QTest::newRow(":2,3d") << u"|a\nb\nc\nd"_s << u":2,3d<CR>"_s << u"a\n|d"_s;
    QTest::newRow(":%d") << u"|a\nb"_s << u":%d<CR>"_s << u"|"_s;
    QTest::newRow(":y") << u"|a\nb"_s << u":y<CR>jp"_s << u"a\nb\n|a"_s;
    QTest::newRow(":>") << u"|a"_s << u":><CR>"_s << u"\t|a"_s;
    QTest::newRow(":j") << u"|a\nb"_s << u":j<CR>"_s << u"a| b"_s;
    QTest::newRow(":u") << u"|a b"_s << u"dw:u<CR>"_s << u"|a b"_s;
    QTest::newRow(": Esc") << u"|abc"_s << u":s/a/b/<Esc>"_s << u"|abc"_s;
    QTest::newRow(": Backspace past") << u"|abc"_s << u":<BS>x"_s << u"|bc"_s;
    QTest::newRow(": count") << u"|a\nb\nc"_s << u"2:s/b/x/<CR>"_s << u"a\n|x\nc"_s;
    QTest::newRow(": history") << u"|a a a"_s << u":s/a/b/<CR>:<Up><CR>"_s << u"|b b a"_s;
    QTest::newRow(":noh") << u"|a"_s << u":noh<CR>"_s << u"|a"_s;

    // Marks.
    QTest::newRow("m `") << u"a|b\nc\nd"_s << u"majj`a"_s << u"a|b\nc\nd"_s;
    QTest::newRow("m '") << u"  a|b\nc\nd"_s << u"majj'a"_s << u"  |ab\nc\nd"_s;
    QTest::newRow("d'a") << u"|a\nb\nc"_s << u"majjd'a"_s << u"|"_s;
    QTest::newRow("d`a") << u"ab|cd"_s << u"mal$d`a"_s << u"ab|d"_s;
    QTest::newRow("``") << u"|a\nb\nc"_s << u"G``"_s << u"|a\nb\nc"_s;
    QTest::newRow("''") << u"|a\nb\nc"_s << u"G''"_s << u"|a\nb\nc"_s;
    QTest::newRow("`<") << u"|abcd"_s << u"lvl<Esc>0`<"_s << u"a|bcd"_s;
    QTest::newRow("`>") << u"|abcd"_s << u"lvl<Esc>0`>"_s << u"ab|cd"_s;
    QTest::newRow("unset mark") << u"|ab"_s << u"`zl"_s << u"a|b"_s;

    // Folds and scrolling do not move the caret of a plain text.
    QTest::newRow("zz") << u"a|b"_s << u"zz"_s << u"a|b"_s;
    QTest::newRow("za") << u"a|b"_s << u"za"_s << u"a|b"_s;

    // Ctrl+A / Ctrl+X on numbers.
    QTest::newRow("C-a") << u"|Zażółć 5 gęślą"_s << u"<C-a>"_s << u"Zażółć |6 gęślą"_s;
    QTest::newRow("C-a in the number") << u"a1|23b"_s << u"<C-a>"_s << u"a12|4b"_s;
    QTest::newRow("C-a on the first digit") << u"a|123"_s << u"<C-a>"_s << u"a12|4"_s;
    QTest::newRow("C-a count") << u"|9"_s << u"5<C-a>"_s << u"1|4"_s;
    QTest::newRow("C-x") << u"|10"_s << u"<C-x>"_s << u"|9"_s;
    QTest::newRow("C-x count") << u"|100"_s << u"25<C-x>"_s << u"7|5"_s;
    QTest::newRow("C-x below zero") << u"|3"_s << u"5<C-x>"_s << u"-|2"_s;
    QTest::newRow("C-a negative") << u"|-5"_s << u"<C-a>"_s << u"-|4"_s;
    QTest::newRow("C-a negative from its digit") << u"-|5"_s << u"<C-a>"_s << u"-|4"_s;
    QTest::newRow("C-a negative to zero") << u"-|1"_s << u"<C-a>"_s << u"|0"_s;
    QTest::newRow("C-x zero") << u"|0"_s << u"<C-x>"_s << u"-|1"_s;
    QTest::newRow("C-a negative across zero") << u"|-2"_s << u"5<C-a>"_s << u"|3"_s;
    QTest::newRow("C-a the number after the caret") << u"|abc 12 de 7"_s << u"<C-a>"_s << u"abc 1|3 de 7"_s;
    QTest::newRow("C-a skips numbers behind") << u"1 2| x 3"_s << u"<C-a>"_s << u"1 2 x |4"_s;
    QTest::newRow("C-a no number") << u"|Zażółć"_s << u"<C-a>"_s << u"|Zażółć"_s;
    QTest::newRow("C-a number only behind") << u"a 1 b| c"_s << u"<C-a>"_s << u"a 1 b| c"_s;
    QTest::newRow("C-a only this line") << u"|a\n1"_s << u"<C-a>"_s << u"|a\n1"_s;
    QTest::newRow("C-a decimal point") << u"|v2.0"_s << u"<C-a>"_s << u"v|3.0"_s;
    QTest::newRow("C-a date day") << u"2026-11-|23"_s << u"<C-a>"_s << u"2026-11-2|4"_s;
    QTest::newRow("C-a date month") << u"2026-|11-23"_s << u"<C-a>"_s << u"2026-1|2-23"_s;
    QTest::newRow("C-a date year") << u"|2026-11-23"_s << u"<C-a>"_s << u"202|7-11-23"_s;
    QTest::newRow("C-x after a letter") << u"foo-|1"_s << u"<C-x>"_s << u"foo-|0"_s;
    QTest::newRow("C-a after a space") << u"a |-1"_s << u"<C-a>"_s << u"a |0"_s;
    QTest::newRow("C-a leading zeros") << u"|007"_s << u"<C-a>"_s << u"00|8"_s;
    QTest::newRow("C-a leading zeros carry") << u"|009"_s << u"<C-a>"_s << u"01|0"_s;
    QTest::newRow("C-x leading zeros") << u"|0010"_s << u"<C-x>"_s << u"000|9"_s;
    QTest::newRow("C-a leading zeros grow") << u"|099"_s << u"<C-a>"_s << u"10|0"_s;
    QTest::newRow("C-a hex") << u"|0xff"_s << u"<C-a>"_s << u"0x10|0"_s;
    QTest::newRow("C-a hex upper") << u"|0xFE"_s << u"<C-a>"_s << u"0xF|F"_s;
    QTest::newRow("C-a hex width") << u"|0x0f"_s << u"<C-a>"_s << u"0x1|0"_s;
    QTest::newRow("C-a hex letter") << u"|0x09"_s << u"<C-a>"_s << u"0x0|a"_s;
    QTest::newRow("C-a hex in the middle") << u"0x1|f"_s << u"<C-a>"_s << u"0x2|0"_s;
    QTest::newRow("C-a hex prefix") << u"0|x1f"_s << u"<C-a>"_s << u"0x2|0"_s;
    QTest::newRow("C-x hex wraps") << u"|0x0"_s << u"<C-x>"_s << u"0xfffffffffffffff|f"_s;
    QTest::newRow("C-a bin") << u"|0b101"_s << u"<C-a>"_s << u"0b11|0"_s;
    QTest::newRow("C-a bin grows") << u"|0b1"_s << u"<C-a>"_s << u"0b1|0"_s;
    QTest::newRow("C-x bin") << u"|0b100"_s << u"<C-x>"_s << u"0b01|1"_s;
    QTest::newRow("C-a 0x without digits is 0") << u"|0xg"_s << u"<C-a>"_s << u"|1xg"_s;
    QTest::newRow("C-a overflow stays") << u"|9223372036854775807"_s << u"<C-a>"_s << u"922337203685477580|7"_s;
    QTest::newRow("C-a dot") << u"|1 2 3"_s << u"<C-a>w."_s << u"2 |3 3"_s;
    QTest::newRow("C-a dot with a count") << u"|1 1 1"_s << u"3<C-a>w."_s << u"4 |4 1"_s;
    QTest::newRow("C-a dot with a new count") << u"|1 1 1"_s << u"3<C-a>w2."_s << u"4 |3 1"_s;
    QTest::newRow("C-x dot") << u"|5 5"_s << u"<C-x>w."_s << u"4 |4"_s;
    QTest::newRow("C-a undo") << u"|5"_s << u"<C-a>u"_s << u"|5"_s;
    QTest::newRow("C-a Polish text around") << u"|zażółć 99 gęślą"_s << u"<C-a>"_s << u"zażółć 10|0 gęślą"_s;
    QTest::newRow("V C-a") << u"|1\n2\n3"_s << u"VG<C-a>"_s << u"|2\n3\n4"_s;
    QTest::newRow("V C-x count") << u"|10\n20"_s << u"Vj5<C-x>"_s << u"|5\n15"_s;
    QTest::newRow("V g C-a") << u"|0\n0\n0"_s << u"VGg<C-a>"_s << u"|1\n2\n3"_s;
    QTest::newRow("V g C-x") << u"|9\n9\n9"_s << u"VGg<C-x>"_s << u"|8\n7\n6"_s;
    QTest::newRow("V g C-a skips lines without a number") << u"|0\nx\n0"_s << u"VGg<C-a>"_s << u"|1\nx\n2"_s;
    QTest::newRow("v C-a") << u"|a1 b2"_s << u"vl<C-a>"_s << u"|a2 b2"_s;
    QTest::newRow("v C-a a number outside the selection") << u"|a b2"_s << u"vl<C-a>"_s << u"|a b2"_s;
    QTest::newRow("v C-a two lines") << u"|1 1\n1 1"_s << u"vj<C-a>"_s << u"|2 1\n2 1"_s;
    QTest::newRow("V C-a dot") << u"|1\n2\n3\n4"_s << u"Vj<C-a>jj."_s << u"2\n3\n|4\n5"_s;

    // Sentences.
    QTest::newRow(")") << u"|Pierwsze zdanie. Drugie zdanie! Trzecie? Czwarte."_s << u")"_s
                       << u"Pierwsze zdanie. |Drugie zdanie! Trzecie? Czwarte."_s;
    QTest::newRow("2)") << u"|Pierwsze zdanie. Drugie zdanie! Trzecie? Czwarte."_s << u"2)"_s
                        << u"Pierwsze zdanie. Drugie zdanie! |Trzecie? Czwarte."_s;
    QTest::newRow("3)") << u"|Pierwsze zdanie. Drugie zdanie! Trzecie? Czwarte."_s << u"3)"_s
                        << u"Pierwsze zdanie. Drugie zdanie! Trzecie? |Czwarte."_s;
    QTest::newRow(") to the end") << u"Pierwsze zdanie. |Drugie."_s << u")"_s << u"Pierwsze zdanie. Drugie|."_s;
    QTest::newRow(") at the end stays") << u"Pierwsze zdanie. Drugie|."_s << u")"_s << u"Pierwsze zdanie. Drugie|."_s;
    QTest::newRow(") from the middle") << u"Pier|wsze zdanie. Drugie."_s << u")"_s << u"Pierwsze zdanie. |Drugie."_s;
    QTest::newRow(") from the blanks") << u"Pierwsze zdanie.| Drugie."_s << u")"_s << u"Pierwsze zdanie. |Drugie."_s;
    QTest::newRow(") from the terminator") << u"Pierwsze zdanie|. Drugie."_s << u")"_s << u"Pierwsze zdanie. |Drugie."_s;
    QTest::newRow("(") << u"Pierwsze zdanie. Dru|gie zdanie!"_s << u"("_s << u"Pierwsze zdanie. |Drugie zdanie!"_s;
    QTest::newRow("( from a start") << u"Pierwsze zdanie. |Drugie zdanie!"_s << u"("_s
                                    << u"|Pierwsze zdanie. Drugie zdanie!"_s;
    QTest::newRow("((") << u"Pierwsze zdanie. Dru|gie zdanie!"_s << u"(("_s << u"|Pierwsze zdanie. Drugie zdanie!"_s;
    QTest::newRow("( at the very start") << u"|Pierwsze."_s << u"("_s << u"|Pierwsze."_s;
    QTest::newRow("2(") << u"A. B. C. D|."_s << u"2("_s << u"A. B. |C. D."_s;
    QTest::newRow("9( past the start") << u"A. B|."_s << u"9("_s << u"|A. B."_s;
    QTest::newRow(") abbreviation") << u"|Kup np. mleko. Potem idź."_s << u")"_s << u"Kup np. mleko. |Potem idź."_s;
    QTest::newRow(") title") << u"|Dr. Kowalski przyszedł. Nowe zdanie."_s << u")"_s
                             << u"Dr. Kowalski przyszedł. |Nowe zdanie."_s;
    QTest::newRow(") small letter after a period") << u"|Waga 5 kg. i tak. Dalej."_s << u")"_s
                                                   << u"Waga 5 kg. i tak. |Dalej."_s;
    QTest::newRow(") approx.") << u"|Waga ok. 5 kg. Dalej."_s << u")"_s << u"Waga ok. 5 kg. |Dalej."_s;
    QTest::newRow(") ellipsis") << u"|Czekaj... Co? Nic."_s << u")"_s << u"Czekaj... |Co? Nic."_s;
    QTest::newRow(") ellipsis small letter") << u"|Hm... może tak. Dalej."_s << u")"_s << u"Hm... może tak. |Dalej."_s;
    QTest::newRow(") question and small letter") << u"|Czy to ok? tak. Nie."_s << u")"_s << u"Czy to ok? |tak. Nie."_s;
    QTest::newRow(") list number") << u"|1. Pierwszy punkt. Drugi."_s << u")"_s << u"1. Pierwszy punkt. |Drugi."_s;
    QTest::newRow(") closing quote") << u"|Powiedział \"Dość.\" Potem wyszedł."_s << u")"_s
                                     << u"Powiedział \"Dość.\" |Potem wyszedł."_s;
    QTest::newRow(") closing bracket") << u"|Zobacz (to jest ważne.) Dalej."_s << u")"_s
                                       << u"Zobacz (to jest ważne.) |Dalej."_s;
    QTest::newRow(") a number with a point") << u"|Pi to 3.14 mniej więcej. Dalej."_s << u")"_s
                                             << u"Pi to 3.14 mniej więcej. |Dalej."_s;
    QTest::newRow(") Polish letters") << u"|Zażółć gęślą jaźń. Łódź."_s << u")"_s << u"Zażółć gęślą jaźń. |Łódź."_s;
    QTest::newRow(") across lines") << u"|Ala ma\nkota. Kot ma\nAlę."_s << u")"_s << u"Ala ma\nkota. |Kot ma\nAlę."_s;
    QTest::newRow("2) across lines") << u"|Ala ma\nkota. Kot ma\nAlę."_s << u"2)"_s << u"Ala ma\nkota. Kot ma\nAlę|."_s;
    QTest::newRow(") a line ends a sentence") << u"|Ala ma kota.\nKot ma Alę."_s << u")"_s
                                              << u"Ala ma kota.\n|Kot ma Alę."_s;
    QTest::newRow(") indentation") << u"|Ala ma kota.\n   Kot ma Alę."_s << u")"_s << u"Ala ma kota.\n   |Kot ma Alę."_s;
    QTest::newRow(") paragraph") << u"|Jeden.\n\nDwa."_s << u")"_s << u"Jeden.\n|\nDwa."_s;
    QTest::newRow("2) paragraph") << u"|Jeden.\n\nDwa."_s << u"2)"_s << u"Jeden.\n\n|Dwa."_s;
    QTest::newRow(") paragraph without a period") << u"|Jeden\ndwa\n\ntrzy"_s << u")"_s << u"Jeden\ndwa\n|\ntrzy"_s;
    QTest::newRow(") empty lines are one") << u"|Jeden.\n\n\nDwa."_s << u"))"_s << u"Jeden.\n\n\n|Dwa."_s;
    QTest::newRow("( paragraph") << u"Jeden.\n\n|Dwa."_s << u"("_s << u"Jeden.\n|\nDwa."_s;
    QTest::newRow("(( paragraph") << u"Jeden.\n\n|Dwa."_s << u"(("_s << u"|Jeden.\n\nDwa."_s;
    QTest::newRow("( across lines") << u"Ala ma\nkota. Kot ma\nAl|ę."_s << u"("_s << u"Ala ma\nkota. |Kot ma\nAlę."_s;
    QTest::newRow("d)") << u"|Pierwsze zdanie. Drugie."_s << u"d)"_s << u"|Drugie."_s;
    QTest::newRow("d) last") << u"Pierwsze zdanie. Dru|gie."_s << u"d)"_s << u"Pierwsze zdanie. Dr|u"_s;
    QTest::newRow("d(") << u"Pierwsze zdanie. Dru|gie."_s << u"d("_s << u"Pierwsze zdanie. |gie."_s;
    QTest::newRow("c)") << u"|Ala. Ola."_s << u"c)Ela. <Esc>"_s << u"Ela.| Ola."_s;
    QTest::newRow("y) $p") << u"|Ala. Ola."_s << u"y)$p"_s << u"Ala. Ola.Ala.| "_s;
    QTest::newRow("is") << u"Pierwsze zdanie. Dru|gie zdanie! Trzecie?"_s << u"dis"_s
                        << u"Pierwsze zdanie. | Trzecie?"_s;
    QTest::newRow("as") << u"Pierwsze zdanie. Dru|gie zdanie! Trzecie?"_s << u"das"_s
                        << u"Pierwsze zdanie. |Trzecie?"_s;
    QTest::newRow("is first") << u"|Pierwsze zdanie. Drugie."_s << u"dis"_s << u"| Drugie."_s;
    QTest::newRow("as first") << u"|Pierwsze zdanie. Drugie."_s << u"das"_s << u"|Drugie."_s;
    QTest::newRow("as last takes the blanks before") << u"Pierwsze zdanie. Drug|ie."_s << u"das"_s
                                                     << u"Pierwsze zdanie|."_s;
    QTest::newRow("is last") << u"Pierwsze zdanie. Drug|ie."_s << u"dis"_s << u"Pierwsze zdanie.| "_s;
    QTest::newRow("is the only sentence") << u"Je|den."_s << u"dis"_s << u"|"_s;
    QTest::newRow("as the only sentence") << u"Je|den."_s << u"das"_s << u"|"_s;
    QTest::newRow("2as") << u"|A. B. C. D."_s << u"2das"_s << u"|C. D."_s;
    QTest::newRow("2is") << u"|A. B. C. D."_s << u"2dis"_s << u"|B. C. D."_s;
    QTest::newRow("3is") << u"|A. B. C. D."_s << u"3dis"_s << u"| C. D."_s;
    QTest::newRow("cis") << u"Pierwsze zdanie. Dru|gie."_s << u"cisNowe.<Esc>"_s << u"Pierwsze zdanie. Nowe|."_s;
    QTest::newRow("is on the blanks") << u"Pierwsze zdanie.| Drugie."_s << u"dis"_s << u"Pierwsze zdanie.|Drugie."_s;
    QTest::newRow("as on the blanks") << u"Pierwsze zdanie.| Drugie."_s << u"das"_s << u"Pierwsze zdanie|."_s;
    QTest::newRow("is across lines") << u"Ala ma\nko|ta. Kot ma\nAlę."_s << u"dis"_s << u"| Kot ma\nAlę."_s;
    QTest::newRow("as across lines") << u"Ala ma\nko|ta. Kot ma\nAlę."_s << u"das"_s << u"|Kot ma\nAlę."_s;
    QTest::newRow("as takes a line break between sentences") << u"Ala ma kota|.\nKot ma Alę."_s << u"das"_s
                                                             << u"|Kot ma Alę."_s;
    QTest::newRow("as at a paragraph's end keeps the break") << u"Jeden. Dw|a.\n\nTrzy."_s << u"das"_s
                                                             << u"Jeden|.\n\nTrzy."_s;
    QTest::newRow("is on an empty line") << u"a.\n|\nb."_s << u"dis"_s << u"a.\n|\nb."_s;
    QTest::newRow("yis $p") << u"|Ala. Ola."_s << u"yis$p"_s << u"Ala. Ola.Ala|."_s;
    QTest::newRow("vis d") << u"Ala. Ol|a. Ela."_s << u"visd"_s << u"Ala. | Ela."_s;
    QTest::newRow("vas d") << u"Ala. Ol|a. Ela."_s << u"vasd"_s << u"Ala. |Ela."_s;
    QTest::newRow("is with an abbreviation") << u"Kup np. mle|ko. Potem."_s << u"dis"_s << u"| Potem."_s;
    QTest::newRow("is in Polish") << u"Zażółć gęś|lą jaźń. Łódź."_s << u"dis"_s << u"| Łódź."_s;

    // Macros.
    QTest::newRow("qa @a") << u"|abcdef"_s << u"qaxq@a"_s << u"|cdef"_s;
    QTest::newRow("3@a") << u"|abcdef"_s << u"qaxq3@a"_s << u"|ef"_s;
    QTest::newRow("@@") << u"|abcdef"_s << u"qaxq@a@@"_s << u"|def"_s;
    QTest::newRow("2@@") << u"|abcdefg"_s << u"qaxq@a2@@"_s << u"|efg"_s;
    QTest::newRow("macro with insert and motion") << u"|a\nb\nc"_s << u"qqA!<Esc>jq2@q"_s << u"a!\nb!\nc|!"_s;
    QTest::newRow("recursive macro") << u"|a\nb\nc\nd"_s << u"qaqqaA.<Esc>j@aq@a"_s << u"a.\nb.\nc.\nd|."_s;
    QTest::newRow("macro runs keys, not text") << u"|aa bb\ncc dd"_s << u"qqdwqj@q"_s << u"bb\n|dd"_s;
    QTest::newRow("macro stops at a failed motion") << u"|axb axb axb"_s << u"qqfxrYq5@q"_s << u"aYb aYb a|Yb"_s;
    QTest::newRow("qA appends") << u"|abcdef"_s << u"qaxqqAxq@a"_s << u"|ef"_s;
    QTest::newRow("macro @:") << u"|a\na"_s << u":s/a/b/<CR>j@:"_s << u"b\n|b"_s;
    QTest::newRow("macro in the register") << u"|abc"_s << u"qaxq\"ap"_s << u"b|xc"_s;
    QTest::newRow("a yanked text runs") << u"|dw one two"_s << u"\"ayiww@a"_s << u"dw |two"_s;
    QTest::newRow("q typed in insert mode") << u"|abc"_s << u"qaiq<Esc>q@a"_s << u"|qqabc"_s;
    QTest::newRow("Enter in a macro") << u"|ab"_s << u"qqA<CR>x<Esc>q@q"_s << u"ab\nx\n|x"_s;
    QTest::newRow("Backspace in a macro") << u"|abc"_s << u"qqA<BS>x<Esc>q@q"_s << u"ab|x"_s;
    QTest::newRow("Polish text typed by a macro") << u"|a\nb"_s << u"qqAżółć<Esc>jq@q"_s << u"ażółć\nbżół|ć"_s;
    QTest::newRow("macro and the dot repeat") << u"|a b c d"_s << u"qqdwq@q."_s << u"|d"_s;
    QTest::newRow("a macro is one undo step") << u"|abc"_s << u"qqA1<Esc>A2<Esc>q@qu"_s << u"abc1|2"_s;
    QTest::newRow("@ of an empty register") << u"|abc"_s << u"@z"_s << u"|abc"_s;
    QTest::newRow("@@ without a macro") << u"|abc"_s << u"@@"_s << u"|abc"_s;

    // :normal and :g.
    QTest::newRow(":normal") << u"|a b c"_s << u":norm dw<CR>"_s << u"|b c"_s;
    QTest::newRow(":normal insert") << u"|abc"_s << u":normal ihello<lt>Esc><CR>"_s << u"hell|oabc"_s;
    QTest::newRow(":normal ends an insert") << u"|a\nb\nc"_s << u":%norm A!<CR>"_s << u"a!\nb!\nc|!"_s;
    QTest::newRow(":normal range") << u"|a\nb\nc"_s << u":2,3norm Ax<CR>"_s << u"a\nbx\nc|x"_s;
    QTest::newRow(":normal a failing line goes on") << u"|ax\nb\nax"_s << u":%norm fxrY<CR>"_s << u"aY\nb\na|Y"_s;
    QTest::newRow(":normal one undo step") << u"|a\nb"_s << u":%norm Ax<CR>u"_s << u"|a\nb"_s;
    QTest::newRow(":g d") << u"|a1\nb\na2\nb"_s << u":g/a/d<CR>"_s << u"b\n|b"_s;
    QTest::newRow(":v d") << u"|a1\nb\na2\nb"_s << u":v/a/d<CR>"_s << u"a1\n|a2"_s;
    QTest::newRow(":g! d") << u"|a1\nb\na2\nb"_s << u":g!/a/d<CR>"_s << u"a1\n|a2"_s;
    QTest::newRow(":g other delimiter") << u"|a1\nb\na2\nb"_s << u":g#a#d<CR>"_s << u"b\n|b"_s;
    QTest::newRow(":g s") << u"|a1\nb\na2\nb"_s << u":g/a/s/a/x/<CR>"_s << u"x1\nb\n|x2\nb"_s;
    QTest::newRow(":g without a command") << u"|a1\nb\na2\nb"_s << u":g/a/<CR>"_s << u"a1\nb\n|a2\nb"_s;
    QTest::newRow(":g range") << u"|a1\na2\nb\na3\na4"_s << u":2,4g/a/d<CR>"_s << u"a1\nb\n|a4"_s;
    QTest::newRow(":g smart case") << u"|a1\nb"_s << u":g/A/d<CR>"_s << u"|a1\nb"_s;
    QTest::newRow(":g ignores case") << u"|A1\nb"_s << u":g/a/d<CR>"_s << u"|b"_s;
    QTest::newRow(":g Polish") << u"|Zażółć\nx\nZażółć"_s << u":g/Zażółć/d<CR>"_s << u"|x"_s;
    QTest::newRow(":g all lines") << u"|a1\na2"_s << u":g/a/d<CR>"_s << u"|"_s;
    QTest::newRow(":g a dot is a dot") << u"|a\nb"_s << u":g/./d<CR>"_s << u"|a\nb"_s;
    QTest::newRow(":g j") << u"|a\nb\na\nc"_s << u":g/a/j<CR>"_s << u"a b\na| c"_s;
    QTest::newRow(":g normal") << u"|a\nb\nc\nb"_s << u":g/b/norm Ax<CR>"_s << u"a\nbx\nc\nb|x"_s;
    QTest::newRow(":g no match") << u"|a\nb"_s << u":g/zzz/d<CR>"_s << u"|a\nb"_s;
    QTest::newRow(":g y A") << u"|a1\nb\na2"_s << u":g/a/y A<CR>G\"ap"_s << u"a1\nb\na2\n|a1\na2"_s;
    QTest::newRow(":g is one undo step") << u"|a1\nb\na2"_s << u":g/a/d<CR>u"_s << u"|a1\nb\na2"_s;
    QTest::newRow(":g a deleted line is skipped") << u"|a\na\nb"_s << u":g/a/.,+1d<CR>"_s << u"|b"_s;
    QTest::newRow(":g sets the search") << u"|ab\nx\nab"_s << u":g/ab/<CR>gg/<CR>"_s << u"ab\nx\n|ab"_s;
    QTest::newRow(":g does not nest") << u"|a\nb"_s << u":g/a/g/b/d<CR>"_s << u"|a\nb"_s;
    QTest::newRow(":g empty pattern") << u"|ab\nc\nab"_s << u"/ab<CR>gg:g//d<CR>"_s << u"|c"_s;
}

void TestVim::keys()
{
    QFETCH(QString, before);
    QFETCH(QString, keys);
    QFETCH(QString, after);
    Fixture f(before);
    QCOMPARE(f.run(keys), after);
    QCOMPARE(f.engine.mode(), Mode::Normal);
    QVERIFY(!f.engine.commandLineActive());
    QCOMPARE(f.engine.pending(), QString());
}

void TestVim::modes_data()
{
    QTest::addColumn<QString>("keys");
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("shape");
    QTest::newRow("i") << u"i"_s << int(Mode::Insert) << int(SelectionShape::None);
    QTest::newRow("a") << u"a"_s << int(Mode::Insert) << int(SelectionShape::None);
    QTest::newRow("o") << u"o"_s << int(Mode::Insert) << int(SelectionShape::None);
    QTest::newRow("cw") << u"cw"_s << int(Mode::Insert) << int(SelectionShape::None);
    QTest::newRow("R") << u"R"_s << int(Mode::Replace) << int(SelectionShape::None);
    QTest::newRow("v") << u"v"_s << int(Mode::Visual) << int(SelectionShape::Characters);
    QTest::newRow("vl") << u"vl"_s << int(Mode::Visual) << int(SelectionShape::Characters);
    QTest::newRow("V") << u"V"_s << int(Mode::VisualLine) << int(SelectionShape::Lines);
    QTest::newRow("vV") << u"vV"_s << int(Mode::VisualLine) << int(SelectionShape::Lines);
    QTest::newRow("v Esc") << u"v<Esc>"_s << int(Mode::Normal) << int(SelectionShape::None);
    QTest::newRow("i Esc") << u"i<Esc>"_s << int(Mode::Normal) << int(SelectionShape::None);
    QTest::newRow("V y") << u"Vy"_s << int(Mode::Normal) << int(SelectionShape::None);
    QTest::newRow("Esc in normal") << u"<Esc><Esc>"_s << int(Mode::Normal) << int(SelectionShape::None);
}

void TestVim::modes()
{
    QFETCH(QString, keys);
    QFETCH(int, mode);
    QFETCH(int, shape);
    Fixture f(u"|zażółć gęślą\njaźń"_s);
    f.run(keys);
    QCOMPARE(int(f.engine.mode()), mode);
    QCOMPARE(int(f.buffer.mode()), mode);
    QCOMPARE(int(f.buffer.selectionShape()), shape);
}

void TestVim::undoGrouping()
{
    // One change of Vim is one undo step of the buffer.
    Fixture f(u"|foo bar baz"_s);
    f.run(u"dw"_s);
    QCOMPARE(f.buffer.undoSteps(), 1);
    f.run(u"cwxyz<Esc>"_s);
    QCOMPARE(f.buffer.undoSteps(), 2); // the delete and the typing: one step
    f.run(u"ofirst<CR>second<Esc>"_s);
    QCOMPARE(f.buffer.undoSteps(), 3);
    f.run(u"3x"_s);
    QCOMPARE(f.buffer.undoSteps(), 4);
    f.run(u":%s/a/A/g<CR>"_s);
    QCOMPARE(f.buffer.undoSteps(), 5);
    f.run(u"Vjd"_s);
    QCOMPARE(f.buffer.undoSteps(), 6);
    f.run(u"yy"_s); // no change
    QCOMPARE(f.buffer.undoSteps(), 6);
    f.run(u"uuuuuu"_s);
    QCOMPARE(f.buffer.text(), u"foo bar baz"_s);
    QCOMPARE(f.buffer.undoSteps(), 0);
    f.run(u"u"_s);
    QCOMPARE(f.engine.message(), Message::NothingToUndo);
    f.run(u"<C-r>"_s);
    QCOMPARE(f.buffer.text(), u"bar baz"_s);
    f.run(u"<C-r><C-r><C-r><C-r><C-r><C-r>"_s);
    QCOMPARE(f.engine.message(), Message::NothingToRedo);
}

void TestVim::dotRepeatAcrossCounts()
{
    // The count of . replaces the change's own.
    Fixture f(u"|a b c d e f g h"_s);
    f.run(u"2dw"_s);
    QCOMPARE(dump(f.buffer), u"|c d e f g h"_s);
    f.run(u"."_s);
    QCOMPARE(dump(f.buffer), u"|e f g h"_s);
    f.run(u"1."_s);
    QCOMPARE(dump(f.buffer), u"|f g h"_s);
    // A change in another buffer repeats here (Vim's last change is global).
    TextBuffer other;
    load(other, u"|x y z"_s);
    Engine e2(&other, &f.shared);
    type(e2, other, u"."_s);
    QCOMPARE(dump(other), u"|y z"_s); // the count of the last "." (1) stays
}

void TestVim::registers()
{
    Shared s;
    s.yanked(QChar(), Register{u"a"_s, true, {}});
    QCOMPARE(s.get(u'0').text, u"a"_s);
    QCOMPARE(s.get(u'"').text, u"a"_s);
    s.deleted(QChar(), Register{u"x"_s, false, {}});
    QCOMPARE(s.get(u'-').text, u"x"_s);
    QCOMPARE(s.get(u'1').text, QString());
    s.deleted(QChar(), Register{u"l1"_s, true, {}});
    s.deleted(QChar(), Register{u"l2"_s, true, {}});
    QCOMPARE(s.get(u'1').text, u"l2"_s);
    QCOMPARE(s.get(u'2').text, u"l1"_s);
    QCOMPARE(s.get(u'0').text, u"a"_s); // a delete leaves the yank register
    s.yanked(u'q', Register{u"ą"_s, false, {}});
    s.yanked(u'Q', Register{u"ę"_s, false, {}});
    QCOMPARE(s.get(u'q').text, u"ąę"_s);
    QCOMPARE(s.get(u'Q').text, u"ąę"_s);
    s.yanked(u'Q', Register{u"line"_s, true, {}});
    QCOMPARE(s.get(u'q').text, u"ąę\nline"_s);
    QVERIFY(s.get(u'q').linewise);
    s.yanked(u'_', Register{u"gone"_s, false, {}});
    QVERIFY(s.get(u'_').isEmpty());
    QCOMPARE(s.get(u'"').text, u"ąę\nline"_s);
    QVERIFY(Shared::isRegister(u'a') && Shared::isRegister(u'+') && Shared::isRegister(u'.'));
    QVERIFY(!Shared::isRegister(u'!') && !Shared::isRegister(u'ą') && !Shared::isWritable(u'.'));
    // A put from an empty register says so.
    Fixture f(u"|a"_s);
    f.run(u"\"zp"_s);
    QCOMPARE(f.engine.message(), Message::RegisterEmpty);
    QCOMPARE(f.engine.messageArg(), u"z"_s);
    QCOMPARE(dump(f.buffer), u"|a"_s);
}

void TestVim::clipboardRegister()
{
    QString clipboard;
    Fixture f(u"|zażółć\ngęślą"_s);
    f.shared.readClipboard = [&] { return clipboard; };
    f.shared.writeClipboard = [&](const QString &t) { clipboard = t; };
    f.run(u"\"+yy"_s);
    QCOMPARE(clipboard, u"zażółć\n"_s); // linewise: with its line break
    f.run(u"\"+yiw"_s);
    QCOMPARE(clipboard, u"zażółć"_s);
    clipboard = u"jaźń"_s;
    f.run(u"j0\"+P"_s);
    QCOMPARE(dump(f.buffer), u"zażółć\njaź|ńgęślą"_s);
    clipboard = u"wiersz\r\n"_s; // a line from another program
    f.run(u"\"*p"_s);
    QCOMPARE(dump(f.buffer), u"zażółć\njaźńgęślą\n|wiersz"_s);
    // The unnamed register is not the clipboard (Vim's default).
    f.run(u"yy"_s);
    QCOMPARE(clipboard, u"wiersz\r\n"_s);
}

void TestVim::commandLine()
{
    Fixture f(u"|a\nb"_s);
    f.run(u":w<CR>"_s);
    QCOMPARE(f.buffer.saves, 1);
    QCOMPARE(f.engine.message(), Message::Saved);
    f.run(u":q<CR>"_s);
    QCOMPARE(f.buffer.quits, 1);
    QVERIFY(!f.buffer.forcedQuit);
    f.run(u":q!<CR>"_s);
    QCOMPARE(f.buffer.quits, 2);
    QVERIFY(f.buffer.forcedQuit);
    f.run(u":wq<CR>"_s);
    QCOMPARE(f.buffer.saves, 2);
    QCOMPARE(f.buffer.quits, 3);
    f.run(u":x<CR>"_s);
    QCOMPARE(f.buffer.saves, 3);
    QCOMPARE(f.buffer.quits, 4);
    f.run(u"ZZ"_s);
    QCOMPARE(f.buffer.saves, 4);
    QCOMPARE(f.buffer.quits, 5);
    f.run(u"ZQ"_s);
    QCOMPARE(f.buffer.saves, 4);
    QCOMPARE(f.buffer.quits, 6);
    f.run(u":write<CR>:quit<CR>"_s);
    QCOMPARE(f.buffer.saves, 5);
    QCOMPARE(f.buffer.quits, 7);
    f.run(u":nie-ma-takiej<CR>"_s);
    QCOMPARE(f.engine.message(), Message::NotAnEditorCommand);
    QCOMPARE(f.engine.messageArg(), u"nie-ma-takiej"_s);
    f.run(u":99<CR>"_s);
    QCOMPARE(f.engine.message(), Message::InvalidRange);
    f.run(u":'q<CR>"_s);
    QCOMPARE(f.engine.message(), Message::MarkNotSet);
    QCOMPARE(f.shared.lastCommand, u"'q"_s);
    QCOMPARE(f.shared.get(u':').text, u"'q"_s);
}

void TestVim::substituteMessages()
{
    Fixture f(u"|ab ab\nab\ncd"_s);
    f.run(u":%s/ab/x/g<CR>"_s);
    QCOMPARE(f.engine.message(), Message::Substituted);
    QCOMPARE(f.engine.messageCount(), 3);
    QCOMPARE(f.engine.messageCount2(), 2);
    f.run(u":%s/zz/x/<CR>"_s);
    QCOMPARE(f.engine.message(), Message::PatternNotFound);
    QCOMPARE(f.engine.messageArg(), u"zz"_s);
    f.run(u":%s/x/y/gn<CR>"_s);
    QCOMPARE(f.engine.message(), Message::Substituted);
    QCOMPARE(f.engine.messageCount(), 3);
    QCOMPARE(f.buffer.text(), u"x x\nx\ncd"_s);
}

void TestVim::marks()
{
    Fixture f(u"|a\nb\nc\nd"_s);
    f.run(u"jmbG"_s);
    f.run(u"'b"_s);
    QCOMPARE(f.buffer.cursor().line, 1);
    f.run(u"G:'b,.d<CR>"_s);
    QCOMPARE(f.buffer.text(), u"a"_s);
    // `. — the last change.
    Fixture g(u"|abc\ndef"_s);
    g.run(u"jlxgg`."_s);
    QCOMPARE(g.buffer.cursor(), (Position{1, 1}));
}

void TestVim::searchWraps()
{
    Fixture f(u"a x\nb\n|c x"_s);
    f.run(u"/x<CR>"_s);
    QCOMPARE(dump(f.buffer), u"a x\nb\nc |x"_s);
    f.run(u"n"_s);
    QCOMPARE(dump(f.buffer), u"a |x\nb\nc x"_s);
    QCOMPARE(f.engine.message(), Message::SearchHitBottom);
    f.run(u"N"_s);
    QCOMPARE(dump(f.buffer), u"a x\nb\nc |x"_s);
    QCOMPARE(f.engine.message(), Message::SearchHitTop);
    f.run(u"/nic<CR>"_s);
    QCOMPARE(f.engine.message(), Message::PatternNotFound);
    QCOMPARE(dump(f.buffer), u"a x\nb\nc |x"_s);
    QCOMPARE(f.shared.get(u'/').text, u"nic"_s);
}

void TestVim::keysLeftToTheEditor()
{
    Fixture f(u"|a"_s);
    // Shortcuts of the editor and the app stay theirs in normal mode.
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Z, Qt::ControlModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_C, Qt::ControlModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_K, Qt::ControlModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Tab)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Backtab, Qt::ShiftModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Up, Qt::AltModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Return, Qt::ControlModifier)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_F2)));
    QVERIFY(!f.engine.feed(Key{Qt::Key_E, Qt::ControlModifier, u"\x05"_s}));
    QVERIFY(!f.engine.feed(Key{Qt::Key_O, Qt::ControlModifier, u"\x0f"_s})); // the jump list is not there
    Key altX{Qt::Key_X, Qt::AltModifier, u"x"_s};
    QVERIFY(!f.engine.feed(altX));
    // Vim's own: Ctrl+R, Ctrl+D, Ctrl+U, Ctrl+[, Esc; typed characters always.
    QVERIFY(f.engine.feed(Key::special(Qt::Key_R, Qt::ControlModifier)));
    QVERIFY(f.engine.feed(Key::special(Qt::Key_D, Qt::ControlModifier)));
    QVERIFY(f.engine.feed(Key{Qt::Key_A, Qt::ControlModifier, u"\x01"_s})); // a number: the editor's select all gives way
    QVERIFY(f.engine.feed(Key{Qt::Key_X, Qt::ControlModifier, u"\x18"_s}));
    QVERIFY(f.engine.feed(Key::special(Qt::Key_BracketLeft, Qt::ControlModifier)));
    QVERIFY(f.engine.feed(Key::fromText(u"Q"_s))); // no command: taken all the same
    QVERIFY(f.engine.feed(Key::special(Qt::Key_Escape)));
    // Insert mode: only Esc.
    QVERIFY(f.engine.feed(Key::fromText(u"i"_s)));
    QCOMPARE(f.engine.mode(), Mode::Insert);
    QVERIFY(!f.engine.feed(Key::fromText(u"x"_s)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Return)));
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_R, Qt::ControlModifier)));
    QVERIFY(!f.engine.feed(Key{Qt::Key_A, Qt::ControlModifier, u"\x01"_s}));
    QVERIFY(!f.engine.feed(Key{Qt::Key_X, Qt::ControlModifier, u"\x18"_s}));
    QVERIFY(f.engine.feed(Key::special(Qt::Key_Escape)));
    QCOMPARE(f.engine.mode(), Mode::Normal);
    // Replace mode: Ctrl+A and Ctrl+X are the editor's too (select all, cut).
    QVERIFY(f.engine.feed(Key::fromText(u"R"_s)));
    QVERIFY(!f.engine.feed(Key{Qt::Key_A, Qt::ControlModifier, u"\x01"_s}));
    QVERIFY(!f.engine.feed(Key{Qt::Key_X, Qt::ControlModifier, u"\x18"_s}));
    QVERIFY(f.engine.feed(Key::special(Qt::Key_Escape)));
    QCOMPARE(f.engine.mode(), Mode::Normal);
    // Shift+arrows select in the editor.
    QVERIFY(!f.engine.feed(Key::special(Qt::Key_Down, Qt::ShiftModifier)));
    // A modifier alone keeps what is pending (d, then Shift for G).
    Fixture g(u"a\n|b\nc"_s);
    g.engine.feed(Key::fromText(u"d"_s));
    QVERIFY(!g.engine.feed(Key::special(Qt::Key_Shift, Qt::ShiftModifier)));
    QVERIFY(!g.engine.feed(Key::special(Qt::Key_AltGr)));
    QCOMPARE(g.engine.pending(), u"d"_s);
    g.engine.feed(Key{Qt::Key_G, Qt::ShiftModifier, u"G"_s});
    QCOMPARE(dump(g.buffer), u"|a"_s);
}

void TestVim::polishLayoutKeys()
{
    // AltGr on Windows arrives as Ctrl+Alt with the character: an argument like any other.
    Fixture f(u"|zażółć"_s);
    f.engine.feed(Key::fromText(u"f"_s));
    f.engine.feed(Key{Qt::Key_O, Qt::ControlModifier | Qt::AltModifier, u"ó"_s});
    QCOMPARE(dump(f.buffer), u"zaż|ółć"_s);
    f.engine.feed(Key::fromText(u"r"_s));
    f.engine.feed(Key{Qt::Key_L, Qt::GroupSwitchModifier, u"ł"_s});
    QCOMPARE(dump(f.buffer), u"zaż|łłć"_s);
    // ą is no command: nothing happens, nothing is typed.
    QVERIFY(f.engine.feed(Key{Qt::Key_A, Qt::NoModifier, u"ą"_s}));
    QCOMPARE(dump(f.buffer), u"zaż|łłć"_s);
    // Keypad digits are counts.
    f.engine.feed(Key{Qt::Key_2, Qt::KeypadModifier, u"2"_s});
    f.engine.feed(Key::fromText(u"x"_s));
    QCOMPARE(dump(f.buffer), u"zaż|ć"_s);
    // Shift with a character: the character's command (A, not a).
    f.engine.feed(Key{Qt::Key_A, Qt::ShiftModifier, u"A"_s});
    QCOMPARE(f.engine.mode(), Mode::Insert);
    QCOMPARE(f.buffer.cursor(), (Position{0, 4}));
}

void TestVim::pendingAndCommandLineText()
{
    Fixture f(u"|a b"_s);
    type(f.engine, f.buffer, u"2d"_s);
    QCOMPARE(f.engine.pending(), u"2d"_s);
    type(f.engine, f.buffer, u"<Esc>"_s);
    QCOMPARE(f.engine.pending(), QString());
    type(f.engine, f.buffer, u"\"a"_s);
    QCOMPARE(f.engine.pending(), u"\"a"_s);
    type(f.engine, f.buffer, u"<Esc>"_s);
    type(f.engine, f.buffer, u":s/ą/"_s);
    QVERIFY(f.engine.commandLineActive());
    QCOMPARE(f.engine.commandLine(), u":s/ą/"_s);
    type(f.engine, f.buffer, u"<C-u>"_s);
    QCOMPARE(f.engine.commandLine(), u":"_s);
    type(f.engine, f.buffer, u"abc def<C-w>"_s);
    QCOMPARE(f.engine.commandLine(), u":abc "_s);
    type(f.engine, f.buffer, u"<Esc>"_s);
    QVERIFY(!f.engine.commandLineActive());
    type(f.engine, f.buffer, u"/żółw"_s);
    QCOMPARE(f.engine.commandLine(), u"/żółw"_s);
    type(f.engine, f.buffer, u"<BS><BS><BS><BS>"_s);
    QCOMPARE(f.engine.commandLine(), u"/"_s);
    type(f.engine, f.buffer, u"<BS>"_s);
    QVERIFY(!f.engine.commandLineActive());
    // Ctrl+W in the command line is the command line's, not the window's.
    type(f.engine, f.buffer, u":"_s);
    QVERIFY(f.engine.feed(Key::special(Qt::Key_W, Qt::ControlModifier)));
    type(f.engine, f.buffer, u"<Esc>"_s);
}

void TestVim::readOnlyBuffer()
{
    Fixture f(u"|abc\ndef"_s);
    f.buffer.readOnly = true;
    f.run(u"x"_s);
    QCOMPARE(f.engine.message(), Message::ReadOnly);
    f.run(u"dd"_s);
    QCOMPARE(f.buffer.text(), u"abc\ndef"_s);
    f.run(u"jw"_s); // moving is fine
    QCOMPARE(f.buffer.cursor().line, 1);
    f.run(u"o"_s);
    QCOMPARE(f.engine.mode(), Mode::Normal);
    QCOMPARE(f.engine.message(), Message::ReadOnly);
}

void TestVim::boundaries_data()
{
    QTest::addColumn<QString>("before");
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("after");
    QTest::newRow("empty buffer x") << u"|"_s << u"x"_s << u"|"_s;
    QTest::newRow("empty buffer dd") << u"|"_s << u"dd"_s << u"|"_s;
    QTest::newRow("empty buffer w") << u"|"_s << u"wbe"_s << u"|"_s;
    QTest::newRow("empty buffer p") << u"|"_s << u"yyp"_s << u"\n|"_s;
    QTest::newRow("empty buffer J") << u"|"_s << u"J"_s << u"|"_s;
    QTest::newRow("empty buffer %") << u"|"_s << u"%"_s << u"|"_s;
    QTest::newRow("empty buffer ip") << u"|"_s << u"dip"_s << u"|"_s;
    QTest::newRow("empty buffer :s") << u"|"_s << u":s/a/b/<CR>"_s << u"|"_s;
    QTest::newRow("w at the end") << u"ab|c"_s << u"w"_s << u"ab|c"_s;
    QTest::newRow("b at the start") << u"|abc"_s << u"b"_s << u"|abc"_s;
    QTest::newRow("e at the end") << u"ab|c"_s << u"e"_s << u"ab|c"_s;
    QTest::newRow("G on the last line") << u"a\n|b"_s << u"G"_s << u"a\n|b"_s;
    QTest::newRow("dw on the last char") << u"ab|c"_s << u"dw"_s << u"a|b"_s;
    QTest::newRow("de at the end") << u"ab|c"_s << u"de"_s << u"a|b"_s;
    QTest::newRow("d} at the end") << u"|a\nb"_s << u"d}"_s << u"|"_s;
    QTest::newRow("dj on the last line") << u"a\n|b"_s << u"dj"_s << u"a\n|b"_s;
    QTest::newRow("dk on the first line") << u"|a\nb"_s << u"dk"_s << u"|a\nb"_s;
    QTest::newRow("dh at the start") << u"|ab"_s << u"dh"_s << u"|ab"_s;
    QTest::newRow("dl at the end") << u"a|b"_s << u"dl"_s << u"|a"_s;
    QTest::newRow("huge count") << u"|a b"_s << u"99999999w"_s << u"a |b"_s;
    QTest::newRow("huge count dd") << u"|a\nb"_s << u"99999dd"_s << u"|"_s;
    QTest::newRow("~ on empty") << u"a\n|"_s << u"~"_s << u"a\n|"_s;
    QTest::newRow("r on empty") << u"|"_s << u"rx"_s << u"|"_s;
    QTest::newRow("D on empty") << u"|\na"_s << u"D"_s << u"|\na"_s;
    QTest::newRow("A on empty") << u"|"_s << u"Ax<Esc>"_s << u"|x"_s;
    QTest::newRow("o at the end") << u"|a"_s << u"o<Esc>"_s << u"a\n|"_s;
    QTest::newRow("O at the start") << u"|a"_s << u"O<Esc>"_s << u"|\na"_s;
    QTest::newRow("V on the last line") << u"a\n|b"_s << u"Vjd"_s << u"|a"_s;
    QTest::newRow("Vk on the first") << u"|a\nb"_s << u"Vkd"_s << u"|b"_s;
    QTest::newRow("empty buffer C-a") << u"|"_s << u"<C-a><C-x>"_s << u"|"_s;
    QTest::newRow("empty buffer sentences") << u"|"_s << u")(d)d(disdas"_s << u"|"_s;
    QTest::newRow("empty buffer H M L") << u"|"_s << u"HMLdHdL"_s << u"|"_s;
    QTest::newRow("empty buffer macros") << u"|"_s << u"qaxq@a@@:g/x/d<CR>:norm x<CR>"_s << u"|"_s;
    QTest::newRow("sentences of a blank line") << u"|  "_s << u")(dis"_s << u"|"_s;
    QTest::newRow("a terminator alone") << u"|."_s << u")dis"_s << u"|"_s;
    QTest::newRow("huge count sentences") << u"|A. B."_s << u"99999)99999("_s << u"|A. B."_s;
    QTest::newRow("C-a huge count") << u"|1"_s << u"99999<C-a>"_s << u"10000|0"_s;
}

void TestVim::boundaries()
{
    QFETCH(QString, before);
    QFETCH(QString, keys);
    QFETCH(QString, after);
    Fixture f(before);
    QCOMPARE(f.run(keys), after);
    QCOMPARE(f.engine.mode(), Mode::Normal);
}

void TestVim::malformed_data()
{
    QTest::addColumn<QString>("keys");
    // Nothing of these may crash, leave a command pending or change the text.
    QTest::newRow("unknown") << u"QZgQzx!K"_s;
    QTest::newRow("operator Esc") << u"d<Esc>"_s;
    QTest::newRow("operator unknown") << u"dQ"_s;
    QTest::newRow("register unknown") << u"\"!p"_s;
    QTest::newRow("register Esc") << u"\"<Esc>"_s;
    QTest::newRow("f Esc") << u"f<Esc>"_s;
    QTest::newRow("r Esc") << u"r<Esc>"_s;
    QTest::newRow("m unknown") << u"m!"_s;
    QTest::newRow("text object unknown") << u"diq"_s;
    QTest::newRow("g unknown") << u"gq"_s;
    QTest::newRow(":s unclosed") << u":s/x<CR>"_s;
    QTest::newRow(":s empty") << u":s<CR>"_s;
    QTest::newRow(":s///") << u":s///<CR>"_s;
    QTest::newRow(": range only comma") << u":,<CR>"_s;
    QTest::newRow(": bad range") << u":5,1d<CR>"_s;
    QTest::newRow(": letters") << u":ąę<CR>"_s;
    QTest::newRow(": marks") << u":'x,'yd<CR>"_s;
    QTest::newRow("/ empty") << u"/<CR>"_s;
    QTest::newRow("? Esc") << u"?abc<Esc>"_s;
    QTest::newRow("counts only") << u"123<Esc>"_s;
    QTest::newRow("zero count") << u"0"_s;
    QTest::newRow("backtick unset") << u"`q'q"_s;
    QTest::newRow("dot nothing") << u"."_s;
    QTest::newRow("visual garbage") << u"vQ<Esc>"_s;
    QTest::newRow("q unknown") << u"q!"_s;
    QTest::newRow("@ unknown") << u"@!"_s;
    QTest::newRow("q Esc") << u"q<Esc>"_s;
    QTest::newRow("@ Esc") << u"@<Esc>"_s;
    QTest::newRow("@: nothing") << u"@:"_s;
    QTest::newRow(":g unclosed") << u":g/x<CR>"_s;
    QTest::newRow(":g empty") << u":g<CR>"_s;
    QTest::newRow(":g bad delimiter") << u":ga<CR>"_s;
    QTest::newRow(":g without a pattern") << u":g//d<CR>"_s;
    QTest::newRow(":normal empty") << u":norm<CR>"_s;
    QTest::newRow(":normal unknown keys") << u":norm QZgQzx!K<CR>"_s;
    QTest::newRow("is unknown in a visual") << u"visQ<Esc>"_s;
    QTest::newRow("g C-a without a number") << u"g<C-a>"_s;
    QTest::newRow("C-a on a word") << u"<C-a><C-x>"_s;
}

void TestVim::malformed()
{
    QFETCH(QString, keys);
    Fixture f(u"|zażółć gęślą\njaźń"_s);
    f.run(keys);
    QCOMPARE(f.buffer.text(), u"zażółć gęślą\njaźń"_s);
    QCOMPARE(f.engine.pending(), QString());
    QVERIFY(!f.engine.commandLineActive());
    QCOMPARE(f.engine.mode(), Mode::Normal);
}

void TestVim::viewLines_data()
{
    QTest::addColumn<QString>("before");
    QTest::addColumn<int>("top");
    QTest::addColumn<int>("pageLines");
    QTest::addColumn<QString>("keys");
    QTest::addColumn<QString>("after");
    const QString text = u"|a\n  b\nc\nd\ne\nf\ng\nh\ni\nj"_s;
    // The view shows lines 2-6 (c d e f g).
    QTest::newRow("H") << text << 2 << 5 << u"H"_s << u"a\n  b\n|c\nd\ne\nf\ng\nh\ni\nj"_s;
    QTest::newRow("2H") << text << 2 << 5 << u"2H"_s << u"a\n  b\nc\n|d\ne\nf\ng\nh\ni\nj"_s;
    QTest::newRow("3H") << text << 2 << 5 << u"3H"_s << u"a\n  b\nc\nd\n|e\nf\ng\nh\ni\nj"_s;
    QTest::newRow("9H stops at the bottom") << text << 2 << 5 << u"9H"_s << u"a\n  b\nc\nd\ne\nf\n|g\nh\ni\nj"_s;
    QTest::newRow("L") << text << 2 << 5 << u"L"_s << u"a\n  b\nc\nd\ne\nf\n|g\nh\ni\nj"_s;
    QTest::newRow("2L") << text << 2 << 5 << u"2L"_s << u"a\n  b\nc\nd\ne\n|f\ng\nh\ni\nj"_s;
    QTest::newRow("9L stops at the top") << text << 2 << 5 << u"9L"_s << u"a\n  b\n|c\nd\ne\nf\ng\nh\ni\nj"_s;
    QTest::newRow("M") << text << 2 << 5 << u"M"_s << u"a\n  b\nc\nd\n|e\nf\ng\nh\ni\nj"_s;
    QTest::newRow("M of an even view") << text << 2 << 4 << u"M"_s << u"a\n  b\nc\n|d\ne\nf\ng\nh\ni\nj"_s;
    QTest::newRow("H goes to the first non-blank") << u"|a\n  b\nc\nd"_s << 1 << 3 << u"H"_s
                                                  << u"a\n  |b\nc\nd"_s;
    QTest::newRow("dH") << u"a\n  b\nc\nd\ne\n|f\ng\nh\ni\nj"_s << 2 << 5 << u"dH"_s << u"a\n  b\n|g\nh\ni\nj"_s;
    QTest::newRow("dL") << u"a\n  b\n|c\nd\ne\nf\ng\nh\ni\nj"_s << 2 << 5 << u"dL"_s << u"a\n  b\n|h\ni\nj"_s;
    QTest::newRow("dM") << u"a\n  b\n|c\nd\ne\nf\ng\nh\ni\nj"_s << 2 << 5 << u"dM"_s << u"a\n  b\n|f\ng\nh\ni\nj"_s;
    QTest::newRow("H is a jump") << u"a\nb\n|c\nd\ne"_s << 0 << 5 << u"H``"_s << u"a\nb\n|c\nd\ne"_s;
    QTest::newRow("L of a short text") << u"|a\nb\nc"_s << 0 << 20 << u"L"_s << u"a\nb\n|c"_s;
    QTest::newRow("M of a short text") << u"|a\nb\nc\nd"_s << 0 << 20 << u"M"_s << u"a\n|b\nc\nd"_s;
    QTest::newRow("vLd") << u"a\n  b\n|c\nd\ne\nf\ng\nh\ni\nj"_s << 2 << 5 << u"vLd"_s << u"a\n  b\n|\nh\ni\nj"_s;
    QTest::newRow("VHd") << u"a\nb\nc\n|d\ne"_s << 1 << 3 << u"VHd"_s << u"a\n|e"_s;
    QTest::newRow("yL P") << u"|a\nb\nc"_s << 0 << 2 << u"yLGp"_s << u"a\nb\nc\n|a\nb"_s;
    QTest::newRow("H after the view moved") << u"|a\nb\nc\nd\ne\nf"_s << 3 << 3 << u"H"_s << u"a\nb\nc\n|d\ne\nf"_s;
}

void TestVim::viewLines()
{
    QFETCH(QString, before);
    QFETCH(int, top);
    QFETCH(int, pageLines);
    QFETCH(QString, keys);
    QFETCH(QString, after);
    Fixture f(before);
    f.buffer.setPageLines(pageLines);
    f.buffer.setViewTop(top);
    QCOMPARE(f.run(keys), after);
    QCOMPARE(f.engine.mode(), Mode::Normal);
}

void TestVim::macroState()
{
    Fixture f(u"|abc"_s);
    QCOMPARE(f.engine.recordingRegister(), QChar());
    f.run(u"qa"_s);
    QCOMPARE(f.engine.recordingRegister(), QChar(u'a'));
    f.run(u"x"_s);
    QCOMPARE(f.engine.recordingRegister(), QChar(u'a'));
    f.run(u"q"_s);
    QCOMPARE(f.engine.recordingRegister(), QChar());
    // The register holds the keys as text (the q that ended it is not in it).
    QCOMPARE(f.shared.get(u'a').text, u"x"_s);
    QVERIFY(!f.shared.get(u'a').linewise);
    // A macro of insert mode, with a special key and a "<".
    f.run(u"qbA<lt>b><CR><Esc>q"_s);
    QCOMPARE(f.shared.get(u'b').text, u"A<lt>b><CR><Esc>"_s);
    // The recorded keys run while the text is as recorded; edited as text, the text runs.
    f.run(u"gg"_s);
    f.buffer.setText(u"abc"_s);
    f.buffer.setCursor(Position{0, 0});
    f.run(u"@a"_s);
    QCOMPARE(dump(f.buffer), u"|bc"_s);
    QCOMPARE(f.shared.macroKeys(u'a').size(), 1);
    // Recording stops by itself when asked to (the setting went off): what was recorded stays.
    f.run(u"qcl"_s);
    QCOMPARE(f.engine.recordingRegister(), QChar(u'c'));
    f.engine.stopRecording();
    QCOMPARE(f.engine.recordingRegister(), QChar());
    QCOMPARE(f.shared.get(u'c').text, u"l"_s);
    // Recording is not nested: q while recording stops, @ inside records the keys, not what they play.
    f.run(u"qd@aq"_s);
    QCOMPARE(f.shared.get(u'd').text, u"@a"_s);
    // A digit is a register too; the unnamed one is not.
    f.run(u"q1xq"_s);
    QCOMPARE(f.shared.get(u'1').text, u"x"_s);
    // The last macro and the count of @@.
    QCOMPARE(f.shared.lastMacro, QChar(u'a'));
    QCOMPARE(f.engine.pending(), QString());
}

void TestVim::macroNotation_data()
{
    QTest::addColumn<QString>("notation");
    for (const QString &n : {u"dw"_s, u"ciwNowe<Esc>"_s, u":s/a/b/<CR>"_s, u"<C-r>"_s, u"i<lt>b>"_s,
                             u"<Left><Right><Up><Down><Home><End><PageUp><PageDown><Insert>"_s, u"<BS><Del><Tab><S-Tab>"_s,
                             u"<C-[>"_s, u"<A-x>"_s, u"Zażółć gęślą"_s, u"<C-a>g<C-a>"_s, u"<lt><lt>"_s, u""_s})
        QTest::newRow(qPrintable(n)) << n;
}

void TestVim::macroNotation()
{
    QFETCH(QString, notation);
    QCOMPARE(keysToNotation(keysFromNotation(notation)), notation);
}

void TestVim::macroStopsWhenItNeverEnds()
{
    Fixture f(u"|abc"_s);
    // A macro that calls itself and never fails would never end: it is stopped.
    f.shared.recorded(u'a', keysFromNotation(u"0@a"_s));
    f.run(u"@a"_s);
    QCOMPARE(f.engine.message(), Message::MacroStopped);
    QCOMPARE(dump(f.buffer), u"|abc"_s);
    QCOMPARE(f.engine.mode(), Mode::Normal);
    QCOMPARE(f.buffer.undoSteps(), 0);
    // The engine goes on as before.
    f.run(u"x"_s);
    QCOMPARE(dump(f.buffer), u"|bc"_s);
    // A macro that fails stops the rest of its count at once.
    f.shared.recorded(u'b', keysFromNotation(u"lx"_s));
    f.run(u"gg100@b"_s);
    QCOMPARE(dump(f.buffer), u"|b"_s);
    // The register of another macro, deeper: @b inside @c runs where @c is.
    f.buffer.setText(u"a b c d"_s);
    f.shared.recorded(u'b', keysFromNotation(u"dw"_s));
    f.shared.recorded(u'c', keysFromNotation(u"@b@b"_s));
    f.run(u"gg@c"_s);
    QCOMPARE(dump(f.buffer), u"|c d"_s);
    QCOMPARE(f.buffer.undoSteps(), 1); // (setText cleared the history; @c is one step)
}

void TestVim::globalAnchorsFollowLines()
{
    // :g marks the lines first: a command that removes or adds lines does not shift the others.
    Fixture f(u"|h\nx1\nx2\nx3"_s);
    f.run(u":g/x/normal ggdd<CR>"_s);
    QCOMPARE(dump(f.buffer), u"|x3"_s);
    Fixture g(u"|x1\ny\nx2"_s);
    g.run(u":g/x/normal yyp<CR>"_s);
    QCOMPARE(dump(g.buffer), u"x1\nx1\ny\nx2\n|x2"_s);
    // The whole of it is one step of undo, and the register of the last delete is the last line's.
    Fixture h(u"|a1\nb\na2\nb"_s);
    h.run(u":g/a/d<CR>"_s);
    QCOMPARE(h.buffer.undoSteps(), 1);
    QCOMPARE(h.shared.get(QChar()).text, u"a2"_s);
    h.run(u"u"_s);
    QCOMPARE(h.buffer.text(), u"a1\nb\na2\nb"_s);
    // The message of a pattern that is nowhere.
    h.run(u":g/zzz/d<CR>"_s);
    QCOMPARE(h.engine.message(), Message::PatternNotFound);
    QCOMPARE(h.engine.messageArg(), u"zzz"_s);
}

void TestVim::sentenceMessagesAndJumps()
{
    // ( and ) are jumps: `` goes back to where the last one started.
    Fixture f(u"|Ala. Ola. Ela."_s);
    f.run(u"))``"_s);
    QCOMPARE(dump(f.buffer), u"Ala. |Ola. Ela."_s);
    f.run(u"``"_s);
    QCOMPARE(dump(f.buffer), u"Ala. Ola. |Ela."_s);
}

void TestVim::characters()
{
    // Surrogate pairs and combining marks are one character to the caret.
    Fixture f(u"|😀x"_s);
    f.run(u"l"_s);
    QCOMPARE(f.buffer.cursor().column, 2);
    f.run(u"0x"_s);
    QCOMPARE(f.buffer.text(), u"x"_s);
    Fixture g(u"|éz"_s); // é as e + a combining acute
    g.run(u"l"_s);
    QCOMPARE(g.buffer.cursor().column, 2);
    g.run(u"0x"_s);
    QCOMPARE(g.buffer.text(), u"z"_s);
    QCOMPARE(charClass(u'ż'), 2);
    QCOMPARE(charClass(u'_'), 2);
    QCOMPARE(charClass(u'-'), 1);
    QCOMPARE(charClass(QChar(0x00A0)), 0);
    QCOMPARE(nextBoundary(u"a😀b"_s, 1), 3);
    QCOMPARE(previousBoundary(u"a😀b"_s, 3), 1);
    QCOMPARE(indentationOf(u"\t  x"_s), u"\t  "_s);
    // No-break spaces (Polish typography) are blanks to the word motions.
    Fixture h(u"|w domu"_s);
    h.run(u"w"_s);
    QCOMPARE(h.buffer.cursor().column, 2);
}

QTEST_GUILESS_MAIN(TestVim)
#include "tst_vim.moc"
