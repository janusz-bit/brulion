// The terminal emulator (src/core/term): sequences of vttest's basics and what programs send —
// cursor motion, wrapping at the last column with the pending wrap, erasing, inserting and deleting
// characters and lines, scroll regions, the alternate screen and its restore, wide characters at
// the edge, combining marks, SGR in every colour form, the replies to DSR / DA / DECRQM, OSC titles,
// working directories and hyperlinks, OSC 52 ignored, UTF-8 split across reads and invalid bytes,
// very long and very large parameters, resizing, the scrollback and which rows changed.
#include "term/term.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::term;

namespace {

/// The screen's rows without the empty ones at the end.
QStringList rowsOf(const Screen &s)
{
    QStringList out;
    for (int row = 0; row < s.rows(); ++row)
        out.append(s.line(row).trimmedText());
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}

/// A terminal that collects its replies.
struct Term {
    Terminal t;
    QByteArray replies;

    explicit Term(int columns = 80, int rows = 24, int scrollback = 5000) : t(columns, rows, scrollback)
    {
        QObject::connect(&t, &Terminal::reply, &t, [this](const QByteArray &data) { replies += data; });
    }
    Term &feed(const QByteArray &data)
    {
        t.feed(data);
        return *this;
    }
    const Screen &screen() const { return t.screen(); }
    const Cell &cell(int row, int column) const { return t.screen().line(row).cells.at(column); }
};

} // namespace

class TestTerm : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void sequences_data();
    void sequences();
    void pendingWrap();
    void wideCharacters();
    void combiningMarks();
    void graphemeClusters();
    void polishText();
    void sgr_data();
    void sgr();
    void replies_data();
    void replies();
    void osc();
    void osc52Ignored();
    void hyperlinks();
    void utf8_data();
    void utf8();
    void utf8SplitAcrossFeeds();
    void sequenceSplitAcrossFeeds();
    void longParameterLists();
    void scrollRegion();
    void scrollback();
    void alternateScreen();
    void modes();
    void cursorStyle();
    void resize();
    void resizePushesIntoScrollback();
    void resizeReflows();
    void resetKeepsScrollback();
    void dirtyRows();
    void bell();
    void charWidths_data();
    void charWidths();
};

void TestTerm::sequences_data()
{
    QTest::addColumn<int>("columns");
    QTest::addColumn<int>("rows");
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QStringList>("expected");
    QTest::addColumn<int>("cursorRow");
    QTest::addColumn<int>("cursorColumn");

    QTest::newRow("plain") << 80 << 24 << "hello"_ba << QStringList{u"hello"_s} << 0 << 5;
    QTest::newRow("CR LF") << 80 << 24 << "a\r\nb"_ba << QStringList{u"a"_s, u"b"_s} << 1 << 1;
    QTest::newRow("LF keeps the column") << 80 << 24 << "a\nb"_ba << QStringList{u"a"_s, u" b"_s} << 1 << 2;
    QTest::newRow("VT and FF are LF") << 80 << 24 << "a\x0b\x0c"_ba << QStringList{u"a"_s} << 2 << 1;
    QTest::newRow("wrap at the last column") << 5 << 3 << "abcdef"_ba << QStringList{u"abcde"_s, u"f"_s} << 1 << 1;
    QTest::newRow("CR LF after the last column")
            << 5 << 3 << "abcde\r\nf"_ba << QStringList{u"abcde"_s, u"f"_s} << 1 << 1;
    QTest::newRow("no autowrap") << 5 << 3 << "\x1b[?7labcdefg"_ba << QStringList{u"abcdg"_s} << 0 << 4;
    QTest::newRow("backspace") << 80 << 24 << "abc\b\bX"_ba << QStringList{u"aXc"_s} << 0 << 2;
    QTest::newRow("backspace at column 0") << 80 << 24 << "\bX"_ba << QStringList{u"X"_s} << 0 << 1;
    QTest::newRow("tab") << 80 << 24 << "a\tb"_ba << QStringList{u"a       b"_s} << 0 << 9;
    QTest::newRow("tab stops at the end") << 10 << 2 << "\t\t\tX"_ba << QStringList{u"         X"_s} << 0 << 9;
    QTest::newRow("CUP") << 80 << 24 << "\x1b[2;3HX"_ba << QStringList{u""_s, u"  X"_s} << 1 << 3;
    QTest::newRow("CUP home") << 80 << 24 << "abc\x1b[HX"_ba << QStringList{u"Xbc"_s} << 0 << 1;
    QTest::newRow("CUP 0;0 is home") << 80 << 24 << "abc\x1b[0;0HX"_ba << QStringList{u"Xbc"_s} << 0 << 1;
    QTest::newRow("CUP clamped") << 10 << 5 << "\x1b[99;99HX"_ba
                                 << QStringList{u""_s, u""_s, u""_s, u""_s, u"         X"_s} << 4 << 9;
    QTest::newRow("HVP") << 80 << 24 << "\x1b[3;2fX"_ba << QStringList{u""_s, u""_s, u" X"_s} << 2 << 2;
    QTest::newRow("CUU CUD CUF CUB") << 80 << 24 << "\x1b[5;5H\x1b[2A\x1b[3D\x1b[1B\x1b[2CX"_ba
                                     << QStringList{u""_s, u""_s, u""_s, u"   X"_s} << 3 << 4;
    QTest::newRow("CUU stops at row 0") << 80 << 24 << "\x1b[9AX"_ba << QStringList{u"X"_s} << 0 << 1;
    QTest::newRow("CNL CPL") << 80 << 24 << "abc\x1b[2EX\x1b[1FY"_ba << QStringList{u"abc"_s, u"Y"_s, u"X"_s} << 1 << 1;
    QTest::newRow("CHA VPA") << 80 << 24 << "\x1b[3d\x1b[4GX"_ba << QStringList{u""_s, u""_s, u"   X"_s} << 2 << 4;
    QTest::newRow("HPA HPR VPR") << 80 << 24 << "\x1b[3`\x1b[2a\x1b[e\x1b[2eX"_ba
                                 << QStringList{u""_s, u""_s, u""_s, u"    X"_s} << 3 << 5;
    QTest::newRow("ED 2") << 80 << 24 << "abc\r\ndef\x1b[2J"_ba << QStringList{} << 1 << 3;
    QTest::newRow("ED 0") << 80 << 24 << "abc\r\ndef\x1b[1;2H\x1b[J"_ba << QStringList{u"a"_s} << 0 << 1;
    QTest::newRow("ED 1") << 80 << 24 << "abc\r\ndef\r\nghi\x1b[2;2H\x1b[1J"_ba
                          << QStringList{u""_s, u"  f"_s, u"ghi"_s} << 1 << 1;
    QTest::newRow("EL 0") << 80 << 24 << "abcdef\x1b[1;3H\x1b[K"_ba << QStringList{u"ab"_s} << 0 << 2;
    QTest::newRow("EL 1") << 80 << 24 << "abcdef\x1b[1;3H\x1b[1K"_ba << QStringList{u"   def"_s} << 0 << 2;
    QTest::newRow("EL 2") << 80 << 24 << "abcdef\x1b[1;3H\x1b[2K"_ba << QStringList{} << 0 << 2;
    QTest::newRow("ICH") << 80 << 24 << "abcdef\x1b[1;3H\x1b[2@"_ba << QStringList{u"ab  cdef"_s} << 0 << 2;
    QTest::newRow("ICH pushes off the end")
            << 6 << 2 << "abcdef\x1b[1;3H\x1b[2@"_ba << QStringList{u"ab  cd"_s} << 0 << 2;
    QTest::newRow("DCH") << 80 << 24 << "abcdef\x1b[1;2H\x1b[2P"_ba << QStringList{u"adef"_s} << 0 << 1;
    QTest::newRow("DCH beyond the end") << 10 << 2 << "abcdef\x1b[1;2H\x1b[99P"_ba << QStringList{u"a"_s} << 0 << 1;
    QTest::newRow("ECH") << 80 << 24 << "abcdef\x1b[1;2H\x1b[2X"_ba << QStringList{u"a  def"_s} << 0 << 1;
    QTest::newRow("IL") << 80 << 4 << "a\r\nb\r\nc\x1b[2;1H\x1b[L"_ba << QStringList{u"a"_s, u""_s, u"b"_s, u"c"_s} << 1
                        << 0;
    QTest::newRow("IL drops the bottom") << 80 << 3 << "a\r\nb\r\nc\x1b[1;1H\x1b[2L"_ba
                                         << QStringList{u""_s, u""_s, u"a"_s} << 0 << 0;
    QTest::newRow("DL") << 80 << 4 << "a\r\nb\r\nc\x1b[1;1H\x1b[M"_ba << QStringList{u"b"_s, u"c"_s} << 0 << 0;
    QTest::newRow("SU") << 80 << 3 << "a\r\nb\r\nc\x1b[S"_ba << QStringList{u"b"_s, u"c"_s} << 2 << 1;
    QTest::newRow("SD") << 80 << 3 << "a\r\nb\x1b[T"_ba << QStringList{u""_s, u"a"_s, u"b"_s} << 1 << 1;
    QTest::newRow("IND scrolls at the bottom") << 80 << 2
                                               << "a\r\nb\x1b"
                                                  "Dc"_ba
                                               << QStringList{u"b"_s, u" c"_s} << 1 << 2;
    QTest::newRow("NEL") << 80 << 3
                         << "a\x1b"
                            "Eb"_ba
                         << QStringList{u"a"_s, u"b"_s} << 1 << 1;
    QTest::newRow("RI at the top") << 80 << 3 << "a\x1b[H\x1bM"_ba << QStringList{u""_s, u"a"_s} << 0 << 0;
    QTest::newRow("DECSC DECRC") << 80 << 24
                                 << "\x1b[3;3H\x1b"
                                    "7\x1b[HX\x1b"
                                    "8Y"_ba
                                 << QStringList{u"X"_s, u""_s, u"  Y"_s} << 2 << 3;
    QTest::newRow("SCOSC SCORC") << 80 << 24 << "\x1b[2;2H\x1b[s\x1b[HX\x1b[uY"_ba << QStringList{u"X"_s, u" Y"_s} << 1
                                 << 2;
    QTest::newRow("DECRC without DECSC") << 80 << 24
                                         << "abc\x1b"
                                            "8X"_ba
                                         << QStringList{u"Xbc"_s} << 0 << 1;
    QTest::newRow("DECALN") << 3 << 2 << "\x1b#8"_ba << QStringList{u"EEE"_s, u"EEE"_s} << 0 << 0;
    QTest::newRow("DEC special graphics")
            << 80 << 24 << "\x1b(0lqk\x1b(Bq"_ba << QStringList{QString::fromUcs4(U"\x250C\x2500\x2510q")} << 0 << 4;
    QTest::newRow("SO SI with G1") << 80 << 24 << "\x1b)0a\x0eq\x0fq"_ba << QStringList{QString::fromUcs4(U"a\x2500q")}
                                   << 0 << 3;
    QTest::newRow("REP") << 80 << 24 << "a\x1b[3b"_ba << QStringList{u"aaaa"_s} << 0 << 4;
    QTest::newRow("insert mode") << 80 << 24 << "abc\x1b[1;1H\x1b[4hX\x1b[4lY"_ba << QStringList{u"XYbc"_s} << 0 << 2;
    QTest::newRow("LNM") << 80 << 24 << "\x1b[20ha\nb"_ba << QStringList{u"a"_s, u"b"_s} << 1 << 1;
    QTest::newRow("origin mode") << 80 << 6 << "\x1b[2;4r\x1b[?6h\x1b[HX\x1b[9;1HY"_ba
                                 << QStringList{u""_s, u"X"_s, u""_s, u"Y"_s} << 3 << 1;
    QTest::newRow("tab stops: TBC, HTS") << 80 << 24 << "\x1b[3g\x1b[1;5H\x1bH\x1b[1;1H\tX"_ba
                                         << QStringList{u"    X"_s} << 0 << 5;
    QTest::newRow("CHT") << 80 << 24 << "\x1b[2IX"_ba << QStringList{u"                X"_s} << 0 << 17;
    QTest::newRow("CBT") << 80 << 24 << "\x1b[1;20H\x1b[ZX"_ba << QStringList{u"                X"_s} << 0 << 17;
    QTest::newRow("CAN cancels a sequence") << 80 << 24
                                            << "\x1b[3\x18"
                                               "1mX"_ba
                                            << QStringList{u"1mX"_s} << 0 << 3;
    QTest::newRow("SUB cancels a sequence") << 80 << 24
                                            << "\x1b[3\x1a"
                                               "1mX"_ba
                                            << QStringList{u"1mX"_s} << 0 << 3;
    QTest::newRow("ESC restarts a sequence") << 80 << 24 << "\x1b[3\x1b[2CX"_ba << QStringList{u"  X"_s} << 0 << 3;
    QTest::newRow("C0 inside CSI is executed") << 80 << 24 << "ab\x1b[\r2CX"_ba << QStringList{u"abX"_s} << 0 << 3;
    QTest::newRow("DEL ignored") << 80 << 24
                                 << "a\x7f"
                                    "b"_ba
                                 << QStringList{u"ab"_s} << 0 << 2;
    QTest::newRow("unknown CSI ignored") << 80 << 24 << "a\x1b[?99999zb\x1b[>5;1ub"_ba << QStringList{u"abb"_s} << 0
                                         << 3;
    QTest::newRow("DCS consumed") << 80 << 24 << "a\x1bP+q544e\x1b\\b"_ba << QStringList{u"ab"_s} << 0 << 2;
    QTest::newRow("APC PM SOS consumed") << 80 << 24 << "a\x1b_Gx=1;AAAA\x1b\\b\x1b^pm\x1b\\c\x1bXsos\x1b\\d"_ba
                                         << QStringList{u"abcd"_s} << 0 << 4;
    QTest::newRow("OSC consumed with BEL") << 80 << 24
                                           << "a\x1b]0;title\x07"
                                              "b"_ba
                                           << QStringList{u"ab"_s} << 0 << 2;
    QTest::newRow("OSC consumed with ST") << 80 << 24 << "a\x1b]2;title\x1b\\b"_ba << QStringList{u"ab"_s} << 0 << 2;
    QTest::newRow("zero parameters are defaults")
            << 80 << 24 << "\x1b[3;3H\x1b[0A\x1b[0DX"_ba << QStringList{u""_s, u" X"_s} << 1 << 2;
    QTest::newRow("huge parameters saturate") << 10 << 5 << "\x1b[99999999999999999999B\x1b[99999999999999999999CX"_ba
                                              << QStringList{u""_s, u""_s, u""_s, u""_s, u"         X"_s} << 4 << 9;
    QTest::newRow("DECSTBM homes the cursor") << 80 << 5 << "abc\x1b[2;3rX"_ba << QStringList{u"Xbc"_s} << 0 << 1;
    QTest::newRow("invalid DECSTBM ignored")
            << 80 << 5 << "\x1b[2;2H\x1b[3;2rX"_ba << QStringList{u""_s, u" X"_s} << 1 << 2;
}

void TestTerm::sequences()
{
    QFETCH(int, columns);
    QFETCH(int, rows);
    QFETCH(QByteArray, input);
    QFETCH(QStringList, expected);
    QFETCH(int, cursorRow);
    QFETCH(int, cursorColumn);

    Term term(columns, rows);
    term.feed(input);
    QCOMPARE(rowsOf(term.screen()), expected);
    QCOMPARE(term.screen().cursorRow(), cursorRow);
    QCOMPARE(term.screen().cursorColumn(), cursorColumn);
    for (int row = 0; row < rows; ++row)
        QCOMPARE(term.screen().line(row).cells.size(), columns);
}

void TestTerm::pendingWrap()
{
    Term term(5, 3);
    term.feed("abcde");
    QCOMPARE(term.screen().cursorColumn(), 4);
    QVERIFY(term.screen().pendingWrap());
    QVERIFY(!term.screen().line(0).wrapped);
    // A cursor motion cancels the wrap: the next character overwrites the last column.
    term.feed("\x1b[1C"
              "X");
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"abcdX"_s}));
    QVERIFY(term.screen().pendingWrap());
    term.feed("Y");
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"abcdX"_s, u"Y"_s}));
    QVERIFY(term.screen().line(0).wrapped);
    QVERIFY(!term.screen().pendingWrap());

    // Wrapping at the bottom scrolls.
    Term bottom(3, 2);
    bottom.feed("abcdefg");
    QCOMPARE(rowsOf(bottom.screen()), (QStringList{u"def"_s, u"g"_s}));
    QCOMPARE(bottom.screen().historySize(), 1);
    QCOMPARE(bottom.screen().lineText(-1), u"abc"_s);
    QVERIFY(bottom.screen().historyLine(0).wrapped);

    // DECAWM off then on: no pending wrap is left.
    Term toggled(3, 2);
    toggled.feed("abc\x1b[?7l\x1b[?7hd");
    QCOMPARE(rowsOf(toggled.screen()), (QStringList{u"abd"_s}));
}

void TestTerm::wideCharacters()
{
    Term term(10, 3);
    term.feed(QString::fromUcs4(U"\x4E2D\x6587!").toUtf8());
    QCOMPARE(term.screen().cursorColumn(), 5);
    QVERIFY(term.cell(0, 0).isWide());
    QVERIFY(term.cell(0, 1).isWideTail());
    QCOMPARE(term.cell(0, 2).ch, char32_t(0x6587));
    QCOMPARE(term.cell(0, 4).ch, char32_t(U'!'));
    QCOMPARE(term.screen().line(0).trimmedText(), QString::fromUcs4(U"\x4E2D\x6587!"));

    // At the last column a wide character wraps first; the column it leaves stays blank.
    Term edge(5, 3);
    edge.feed(QString::fromUcs4(U"abcd\x4E2D").toUtf8());
    QCOMPARE(rowsOf(edge.screen()), (QStringList{u"abcd"_s, QString::fromUcs4(U"\x4E2D")}));
    QVERIFY(edge.screen().line(0).wrapped);
    QCOMPARE(edge.screen().cursorRow(), 1);
    QCOMPARE(edge.screen().cursorColumn(), 2);

    // Without autowrap it lands on the last two columns.
    Term noWrap(5, 3);
    noWrap.feed(QString::fromUcs4(U"\x1b[?7labcd\x4E2D").toUtf8());
    QCOMPARE(rowsOf(noWrap.screen()), (QStringList{QString::fromUcs4(U"abc\x4E2D")}));

    // Overwriting either half blanks the other one.
    Term half(10, 2);
    half.feed(QString::fromUcs4(U"\x4E2D\x6587\x1b[1;2HX\x1b[1;3HY").toUtf8());
    QCOMPARE(half.screen().line(0).trimmedText(), u" XY"_s);
    QVERIFY(!half.cell(0, 0).isWide());
    QVERIFY(!half.cell(0, 3).isWideTail());

    // An emoji with an emoji presentation is wide; on a one-column screen it takes one column.
    Term emoji(10, 2);
    emoji.feed("\xf0\x9f\x98\x80x");
    QCOMPARE(emoji.screen().cursorColumn(), 3);
    Term narrow(1, 2);
    narrow.feed(QString::fromUcs4(U"\x4E2D").toUtf8());
    QCOMPARE(narrow.cell(0, 0).ch, char32_t(0x4E2D));
    QVERIFY(!narrow.cell(0, 0).isWide());

    // Erasing or deleting half of one, and inserting inside one, leave no half behind.
    Term edit(10, 2);
    edit.feed(QString::fromUcs4(U"a\x4E2D"
                                "b\x1b[1;3H\x1b[@")
                      .toUtf8());
    QCOMPARE(edit.screen().line(0).trimmedText(), u"a   b"_s);
    for (int column = 0; column < 10; ++column) {
        QVERIFY(!edit.cell(0, column).isWideTail());
        QVERIFY(!edit.cell(0, column).isWide());
    }
    Term erased(10, 2);
    erased.feed(QString::fromUcs4(U"a\x4E2D"
                                  "b\x1b[1;3H\x1b[1K")
                        .toUtf8());
    QCOMPARE(erased.screen().line(0).trimmedText(), u"   b"_s);
    Term deleted(10, 2);
    deleted.feed(QString::fromUcs4(U"a\x4E2D"
                                   "b\x1b[1;2H\x1b[P")
                         .toUtf8());
    QCOMPARE(deleted.screen().line(0).trimmedText(), u"a b"_s);
}

void TestTerm::combiningMarks()
{
    Term term(10, 2);
    // e + COMBINING ACUTE ACCENT: one cell.
    term.feed("e\xcc\x81x");
    QCOMPARE(term.screen().cursorColumn(), 2);
    QCOMPARE(term.screen().line(0).cellText(0), QString::fromUcs4(U"e\x0301"));
    QCOMPARE(term.screen().line(0).trimmedText(), QString::fromUcs4(U"e\x0301x"));
    // At the last column (the pending wrap) the mark joins that column.
    Term edge(3, 2);
    edge.feed("abc\xcc\x81");
    QCOMPARE(edge.screen().line(0).cellText(2), QString::fromUcs4(U"c\x0301"));
    QCOMPARE(edge.screen().cursorRow(), 0);
    // After a wide character it joins the character, not its right half.
    Term wide(10, 2);
    wide.feed(QString::fromUcs4(U"\x4E2D\x0301").toUtf8());
    QCOMPARE(wide.screen().line(0).cellText(0), QString::fromUcs4(U"\x4E2D\x0301"));
    // ZERO WIDTH JOINER and VARIATION SELECTOR-16 join too.
    Term joined(10, 2);
    joined.feed("\xe2\x9d\xa4\xef\xb8\x8f!");
    QCOMPARE(joined.screen().cursorColumn(), 2);
    // Nothing before it: dropped.
    Term alone(10, 2);
    alone.feed("\xcc\x81"
               "a");
    QCOMPARE(alone.screen().line(0).trimmedText(), u"a"_s);
    // A flood of marks stays bounded.
    Term flood(10, 2);
    QByteArray marks = "a";
    for (int i = 0; i < 10000; ++i)
        marks += "\xcc\x81";
    flood.feed(marks);
    QVERIFY(flood.screen().line(0).cellText(0).size() <= 32);
    // Overwriting the cell drops its cluster.
    flood.feed("\rb");
    QCOMPARE(flood.screen().line(0).cellText(0), u"b"_s);
    QVERIFY(flood.screen().line(0).clusters.isEmpty());
}

// Mode 2027 (grapheme clusters, as kitty, WezTerm and foot have it): emoji joined by ZERO WIDTH
// JOINER, a flag's two regional indicators and a skin tone take the cells of one character; off (the
// default, as wcwidth() counts them) each takes its own. DECRQM says which.
void TestTerm::graphemeClusters()
{
    const QByteArray family = QString::fromUcs4(U"\x1F468\x200D\x1F469\x200D\x1F467").toUtf8();
    const QByteArray flags = QString::fromUcs4(U"\x1F1F5\x1F1F1\x1F1E9\x1F1EA").toUtf8();
    const QByteArray thumb = QString::fromUcs4(U"\x1F44D\x1F3FD").toUtf8();
    Term off(20, 2);
    off.feed(family);
    QCOMPARE(off.screen().cursorColumn(), 6);
    off.feed("\x1b[?2027$p");
    QCOMPARE(off.replies, QByteArray("\x1b[?2027;2$y"));
    Term on(20, 4);
    on.feed("\x1b[?2027h\x1b[?2027$p");
    QCOMPARE(on.replies, QByteArray("\x1b[?2027;1$y"));
    on.feed(family + "!");
    QCOMPARE(on.screen().cursorColumn(), 3);
    QCOMPARE(on.screen().line(0).cellText(0), QString::fromUtf8(family));
    QCOMPARE(on.screen().line(0).cellText(2), u"!"_s);
    // Two flags: two characters of two cells each.
    on.feed("\r\n" + flags);
    QCOMPARE(on.screen().cursorColumn(), 4);
    QCOMPARE(on.screen().line(1).cellText(0), QString::fromUcs4(U"\x1F1F5\x1F1F1"));
    QCOMPARE(on.screen().line(1).cellText(2), QString::fromUcs4(U"\x1F1E9\x1F1EA"));
    // A skin tone after an emoji joins it; a ZWJ before plain text does not take it.
    on.feed("\r\n" + thumb + QString::fromUcs4(U"\x200D").toUtf8() + "a");
    QCOMPARE(on.screen().cursorColumn(), 3);
    QCOMPARE(on.screen().line(2).cellText(2), u"a"_s);
    // Off again: as wcwidth() counts.
    on.feed("\x1b[?2027l\r\n" + family);
    QCOMPARE(on.screen().cursorColumn(), 6);
}

void TestTerm::polishText()
{
    Term term(80, 3);
    const QString text = u"zażółć gęślą jaźń"_s;
    term.feed(text.toUtf8());
    QCOMPARE(term.screen().line(0).trimmedText(), text);
    QCOMPARE(term.screen().cursorColumn(), int(text.size()));
    term.feed("\r\n\x1b[1;32mZAŻÓŁĆ\x1b[0m GĘŚLĄ JAŹŃ");
    QCOMPARE(term.screen().line(1).trimmedText(), u"ZAŻÓŁĆ GĘŚLĄ JAŹŃ"_s);
    QCOMPARE(term.cell(1, 0).fg, Color::indexed(2));
    QVERIFY(term.cell(1, 0).attrs & Bold);
    QVERIFY(!(term.cell(1, 7).attrs & Bold));
    // Polish in a narrow screen wraps by characters, not bytes.
    Term narrow(5, 4);
    narrow.feed(text.toUtf8());
    QCOMPARE(rowsOf(narrow.screen()), (QStringList{u"zażół"_s, u"ć gęś"_s, u"lą ja"_s, u"źń"_s}));
}

void TestTerm::sgr_data()
{
    QTest::addColumn<QByteArray>("sgr");
    QTest::addColumn<quint32>("fg");
    QTest::addColumn<quint32>("bg");
    QTest::addColumn<int>("attrs");

    auto raw = [](Color c) { return c.raw(); };
    QTest::newRow("reset") << "\x1b[1;31m\x1b[m"_ba << raw(Color()) << raw(Color()) << 0;
    QTest::newRow("bold red") << "\x1b[1;31m"_ba << raw(Color::indexed(1)) << raw(Color()) << int(Bold);
    QTest::newRow("bright") << "\x1b[92;103m"_ba << raw(Color::indexed(10)) << raw(Color::indexed(11)) << 0;
    QTest::newRow("256 with ;") << "\x1b[38;5;200;48;5;17m"_ba << raw(Color::indexed(200)) << raw(Color::indexed(17))
                                << 0;
    QTest::newRow("256 with :") << "\x1b[38:5:200m"_ba << raw(Color::indexed(200)) << raw(Color()) << 0;
    QTest::newRow("RGB with ;") << "\x1b[38;2;10;20;30m"_ba << raw(Color::rgb(10, 20, 30)) << raw(Color()) << 0;
    QTest::newRow("RGB with : and colour space")
            << "\x1b[48:2::10:20:30m"_ba << raw(Color()) << raw(Color::rgb(10, 20, 30)) << 0;
    QTest::newRow("RGB with : without colour space")
            << "\x1b[48:2:10:20:30m"_ba << raw(Color()) << raw(Color::rgb(10, 20, 30)) << 0;
    QTest::newRow("RGB then more") << "\x1b[38;2;1;2;3;1;4m"_ba << raw(Color::rgb(1, 2, 3)) << raw(Color())
                                   << int(Bold | Underline);
    QTest::newRow("RGB out of range ignored") << "\x1b[38;2;1;2;300m"_ba << raw(Color()) << raw(Color()) << 0;
    QTest::newRow("truncated 38") << "\x1b[1;38;5m"_ba << raw(Color()) << raw(Color()) << int(Bold);
    QTest::newRow("all attributes") << "\x1b[1;2;3;4;5;7;8;9;53m"_ba << raw(Color()) << raw(Color())
                                    << int(Bold | Dim | Italic | Underline | Blink | Inverse | Invisible | Strikethrough
                                           | Overline);
    QTest::newRow("attributes off") << "\x1b[1;2;3;4;5;7;8;9;53m\x1b[22;23;24;25;27;28;29;55m"_ba << raw(Color())
                                    << raw(Color()) << 0;
    QTest::newRow("double underline") << "\x1b[21m"_ba << raw(Color()) << raw(Color()) << int(DoubleUnderline);
    QTest::newRow("underline styles") << "\x1b[4:2m"_ba << raw(Color()) << raw(Color()) << int(DoubleUnderline);
    QTest::newRow("curly underline") << "\x1b[4:3m"_ba << raw(Color()) << raw(Color()) << int(Underline);
    QTest::newRow("underline off by style") << "\x1b[4m\x1b[4:0m"_ba << raw(Color()) << raw(Color()) << 0;
    QTest::newRow("underline colour skipped") << "\x1b[58;2;1;2;3;1m"_ba << raw(Color()) << raw(Color()) << int(Bold);
    QTest::newRow("default colours") << "\x1b[31;41m\x1b[39;49m"_ba << raw(Color()) << raw(Color()) << 0;
    QTest::newRow("empty parameters are 0") << "\x1b[1;;31m"_ba << raw(Color::indexed(1)) << raw(Color()) << 0;
}

void TestTerm::sgr()
{
    QFETCH(QByteArray, sgr);
    QFETCH(quint32, fg);
    QFETCH(quint32, bg);
    QFETCH(int, attrs);

    Term term;
    term.feed(sgr + "X");
    const Cell &cell = term.cell(0, 0);
    QCOMPARE(cell.fg.raw(), fg);
    QCOMPARE(cell.bg.raw(), bg);
    QCOMPARE(int(cell.attrs & AttributeMask), attrs);
}

void TestTerm::replies_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QByteArray>("reply");

    QTest::newRow("DSR 5") << "\x1b[5n"_ba << "\x1b[0n"_ba;
    QTest::newRow("DSR 6") << "\x1b[3;6H\x1b[6n"_ba << "\x1b[3;6R"_ba;
    QTest::newRow("DSR 6 at the pending wrap") << "\x1b[1;80Hx\x1b[6n"_ba << "\x1b[1;80R"_ba;
    QTest::newRow("DSR 6 in origin mode") << "\x1b[5;10r\x1b[?6h\x1b[2;2H\x1b[6n"_ba << "\x1b[2;2R"_ba;
    QTest::newRow("DECXCPR") << "\x1b[2;2H\x1b[?6n"_ba << "\x1b[?2;2R"_ba;
    QTest::newRow("DA") << "\x1b[c"_ba << "\x1b[?62;22c"_ba;
    QTest::newRow("DA 0") << "\x1b[0c"_ba << "\x1b[?62;22c"_ba;
    QTest::newRow("DA2") << "\x1b[>c"_ba << "\x1b[>1;10;0c"_ba;
    QTest::newRow("DECID") << "\x1bZ"_ba << "\x1b[?62;22c"_ba;
    QTest::newRow("DECRQM set") << "\x1b[?2004h\x1b[?2004$p"_ba << "\x1b[?2004;1$y"_ba;
    QTest::newRow("DECRQM reset") << "\x1b[?2004$p"_ba << "\x1b[?2004;2$y"_ba;
    QTest::newRow("DECRQM unknown") << "\x1b[?31337$p"_ba << "\x1b[?31337;0$y"_ba;
    QTest::newRow("DECRQM ANSI") << "\x1b[4h\x1b[4$p"_ba << "\x1b[4;1$y"_ba;
    QTest::newRow("window size") << "\x1b[18t"_ba << "\x1b[8;24;80t"_ba;
    QTest::newRow("other window ops") << "\x1b[14t\x1b[22;0t\x1b[23;0t"_ba << ""_ba;
    QTest::newRow("colour query before colours are known") << "\x1b]11;?\x07"_ba << ""_ba;
    QTest::newRow("XTGETTCAP unanswered") << "\x1bP+q544e\x1b\\"_ba << ""_ba;
}

void TestTerm::replies()
{
    QFETCH(QByteArray, input);
    QFETCH(QByteArray, reply);
    Term term;
    term.feed(input);
    QCOMPARE(term.replies, reply);
}

void TestTerm::osc()
{
    Term term;
    QSignalSpy titles(&term.t, &Terminal::titleChanged);
    QSignalSpy cwds(&term.t, &Terminal::workingDirectoryChanged);
    term.feed("\x1b]0;Mój tytuł — zażółć\x07");
    QCOMPARE(term.t.title(), u"Mój tytuł — zażółć"_s);
    QCOMPARE(titles.size(), 1);
    term.feed("\x1b]2;second\x1b\\");
    QCOMPARE(term.t.title(), u"second"_s);
    term.feed("\x1b]2;second\x07");
    QCOMPARE(titles.size(), 2); // unchanged: no signal
    // Control characters are dropped, a long title is cut.
    term.feed("\x1b]2;a\x01"
              "b\xc2\x85"
              "c\x07");
    QCOMPARE(term.t.title(), u"abc"_s);
    term.feed("\x1b]2;" + QByteArray(10000, 'x') + "\x07");
    QCOMPARE(term.t.title().size(), 256);
    // An icon name is not the title.
    term.feed("\x1b]1;icon\x07");
    QCOMPARE(term.t.title().size(), 256);

    term.feed("\x1b]7;file://laptop/home/u%C5%BCytkownik/kod%20projekt\x07");
    QCOMPARE(term.t.workingDirectory(), u"/home/użytkownik/kod projekt"_s);
    QCOMPARE(cwds.size(), 1);
    term.feed("\x1b]7;kitty-shell-cwd://laptop/elsewhere\x07");
    QCOMPARE(term.t.workingDirectory(), u"/home/użytkownik/kod projekt"_s);
    term.feed("\x1b]7;file:///tmp\x1b\\");
    QCOMPARE(term.t.workingDirectory(), u"/tmp"_s);

    // Colour queries are answered once the view said its colours; setting colours is ignored.
    term.t.setReportedColors(0xd3d8e2, 0x16171b, 0xff7a66);
    term.feed("\x1b]11;?\x07\x1b]10;?\x1b\\\x1b]11;#ffffff\x07");
    QCOMPARE(term.replies, "\x1b]11;rgb:1616/1717/1b1b\x1b\\\x1b]10;rgb:d3d3/d8d8/e2e2\x1b\\"_ba);

    // Malformed OSC strings.
    term.feed("\x1b];;;\x07\x1b]x;y\x07\x1b]\x07\x1b]99999999999;z\x07ok");
    QCOMPARE(term.screen().line(0).trimmedText(), u"ok"_s);
    // A very long OSC string is cut, its end still found.
    term.feed("\x1b]2;" + QByteArray(1 << 20, 'y')
              + "\x07"
                "after");
    QCOMPARE(term.screen().line(0).trimmedText(), u"okafter"_s);
}

void TestTerm::osc52Ignored()
{
    Term term;
    term.feed("\x1b]52;c;aGVsbG8=\x07\x1b]52;c;?\x07\x1b]52;p;?\x1b\\x");
    QCOMPARE(term.replies, QByteArray());
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"x"_s}));
}

void TestTerm::hyperlinks()
{
    Term term;
    term.feed("\x1b]8;;https://example.com/a\x07Link\x1b]8;;\x07!\x1b]8;id=1;https://example.com/a\x07Z");
    const Screen &s = term.screen();
    const quint16 id = term.cell(0, 0).link;
    QVERIFY(id != 0);
    QCOMPARE(s.hyperlink(id), u"https://example.com/a"_s);
    QCOMPARE(term.cell(0, 3).link, id);
    QCOMPARE(term.cell(0, 4).link, quint16(0));
    QCOMPARE(term.cell(0, 5).link, id); // the same URI, the same id
    QCOMPARE(s.hyperlink(0), QString());
    QCOMPARE(s.hyperlink(9999), QString());
    // Malformed: no ';' between the parameters and the URI.
    term.feed("\x1b]8;https://x\x07Q");
    QCOMPARE(term.cell(0, 6).link, id);
}

void TestTerm::utf8_data()
{
    QTest::addColumn<QByteArray>("input");
    QTest::addColumn<QString>("expected");

    const QString r = QString(QChar(0xFFFD));
    QTest::newRow("valid 2, 3, 4 bytes") << "\xc5\xbc\xe2\x82\xac\xf0\x9f\x98\x80"_ba
                                         << QString::fromUcs4(U"\x017C\x20AC\x1F600");
    QTest::newRow("invalid byte") << "a\xff"
                                     "b"_ba
                                  << u"a"_s + r + u"b"_s;
    QTest::newRow("lone continuation") << "a\x80"
                                          "b"_ba
                                       << u"a"_s + r + u"b"_s;
    QTest::newRow("cut short by ASCII") << "\xc3(x"_ba << r + u"(x"_s;
    QTest::newRow("overlong 2 bytes") << "\xc0\xaf"_ba << r + r;
    QTest::newRow("overlong 3 bytes") << "\xe0\x80\xaf"_ba << r + r + r;
    QTest::newRow("surrogate") << "\xed\xa0\x80"_ba << r + r + r;
    QTest::newRow("beyond U+10FFFF") << "\xf4\x90\x80\x80"_ba << r + r + r + r;
    QTest::newRow("F5") << "\xf5\x80"_ba << r + r;
    QTest::newRow("truncated 4 bytes") << "\xf0\x9f\x98"
                                          "A"_ba
                                       << r + u"A"_s;
    QTest::newRow("cut short by ESC") << "\xe2\x82\x1b[1mx"_ba << r + u"x"_s;
    QTest::newRow("C1 code points are dropped") << "\xc2\x9b"
                                                   "1mx"_ba
                                                << u"1mx"_s;
}

void TestTerm::utf8()
{
    QFETCH(QByteArray, input);
    QFETCH(QString, expected);
    Term term;
    term.feed(input);
    QCOMPARE(term.screen().line(0).trimmedText(), expected);
}

void TestTerm::utf8SplitAcrossFeeds()
{
    const QByteArray text = u"zażółć gęślą jaźń \U0001F600 中"_s.toUtf8();
    // Every split of the bytes, and one byte a feed.
    for (int at = 0; at <= text.size(); ++at) {
        Term term;
        term.feed(text.left(at));
        term.feed(text.mid(at));
        QCOMPARE(term.screen().line(0).trimmedText(), QString::fromUtf8(text));
    }
    Term bytes;
    for (char c : text)
        bytes.feed(QByteArray(1, c));
    QCOMPARE(bytes.screen().line(0).trimmedText(), QString::fromUtf8(text));
}

void TestTerm::sequenceSplitAcrossFeeds()
{
    const QByteArray input = "\x1b[1;38;2;10;20;30mA\x1b]0;tytuł\x1b\\\x1b[2;3HB\x1b[?1049h\x1b[?1049l";
    for (int at = 0; at <= input.size(); ++at) {
        Term term;
        term.feed(input.left(at));
        term.feed(input.mid(at));
        QCOMPARE(term.cell(0, 0).fg, Color::rgb(10, 20, 30));
        QVERIFY(term.cell(0, 0).attrs & Bold);
        QCOMPARE(term.t.title(), u"tytuł"_s);
        QCOMPARE(term.cell(1, 2).ch, char32_t(U'B'));
        QVERIFY(!term.screen().alternateScreen());
    }
}

void TestTerm::longParameterLists()
{
    Term term;
    QByteArray many = "\x1b[";
    for (int i = 0; i < 1000; ++i)
        many += "1;";
    many += "31mX";
    term.feed(many);
    // Parameters beyond the 32nd are dropped: the 31 never arrives, the 1s do.
    QCOMPARE(term.cell(0, 0).ch, char32_t(U'X'));
    QVERIFY(term.cell(0, 0).attrs & Bold);
    QCOMPARE(term.cell(0, 0).fg, Color());

    QByteArray digits = "\x1b[" + QByteArray(100000, '9') + "CY";
    term.feed(digits);
    QCOMPARE(term.screen().cursorColumn(), 79);

    QByteArray colons = "\r\n\x1b[38" + QByteArray(5000, ':') + "mZ";
    term.feed(colons);
    QCOMPARE(term.screen().line(1).trimmedText(), u"Z"_s);

    // Many intermediates: the sequence is dropped.
    term.feed("\x1b[1 !!!qW");
    QCOMPARE(term.screen().line(1).trimmedText(), u"ZW"_s);
}

void TestTerm::scrollRegion()
{
    Term term(10, 5);
    term.feed("top\x1b[2;4r\x1b[2;1H"
              "1\r\n2\r\n3\r\n4\r\n5");
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"top"_s, u"3"_s, u"4"_s, u"5"_s}));
    // Lines leaving a region that does not start at the top are gone.
    QCOMPARE(term.screen().historySize(), 0);
    // Below the region LF moves down to the last row without scrolling.
    term.feed("\x1b[5;1Hbottom\r\n\r\n");
    QCOMPARE(term.screen().cursorRow(), 4);
    QCOMPARE(term.screen().line(4).trimmedText(), u"bottom"_s);
    // RI at the region's top scrolls the region down.
    term.feed("\x1b[2;1H\x1bM");
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"top"_s, u""_s, u"3"_s, u"4"_s, u"bottom"_s}));
    // IL / DL outside the region do nothing.
    term.feed("\x1b[1;1H\x1b[L\x1b[M");
    QCOMPARE(term.screen().line(0).trimmedText(), u"top"_s);
    // A region from the top: lines go into the scrollback (a status line below stays).
    Term status(10, 4);
    status.feed("\x1b[4;1Hstatus\x1b[1;3r\x1b[1;1H"
                "a\r\nb\r\nc\r\nd\r\ne");
    QCOMPARE(rowsOf(status.screen()), (QStringList{u"c"_s, u"d"_s, u"e"_s, u"status"_s}));
    QCOMPARE(status.screen().historySize(), 2);
    QCOMPARE(status.screen().lineText(-2), u"a"_s);
    // Erasing uses the current background (bce).
    Term bce(4, 2);
    bce.feed("\x1b[44m\x1b[2J");
    QCOMPARE(bce.cell(1, 3).bg, Color::indexed(4));
    bce.feed("\x1b[m\x1b[41m\r\n\r\n");
    QCOMPARE(bce.cell(1, 0).bg, Color::indexed(1)); // the scrolled-in line
}

void TestTerm::scrollback()
{
    Term term(10, 3, 10);
    for (int i = 0; i < 100; ++i)
        term.feed(QByteArray::number(i) + "\r\n");
    const Screen &s = term.screen();
    QCOMPARE(s.historySize(), 10);
    // The screen shows 98, 99 and the empty row; the scrollback the ten before.
    QCOMPARE(s.lineText(0), u"98"_s);
    QCOMPARE(s.lineText(-1), u"97"_s);
    QCOMPARE(s.lineText(-10), u"88"_s);
    QCOMPARE(s.historyLine(0).trimmedText(), u"88"_s);
    // Trailing blanks of the scrollback's lines are dropped.
    QCOMPARE(s.historyLine(0).cells.size(), 2);
    // Out of range: an empty line.
    QCOMPARE(s.historyLine(10).cells.size(), 0);
    QCOMPARE(s.historyLine(-1).cells.size(), 0);

    // A smaller limit keeps the newest.
    term.t.screen().setScrollbackLimit(3);
    QCOMPARE(s.historySize(), 3);
    QCOMPARE(s.lineText(-1), u"97"_s);
    QCOMPARE(s.lineText(-3), u"95"_s);
    term.feed("x\r\n");
    QCOMPARE(s.historySize(), 3);
    QCOMPARE(s.lineText(-1), u"98"_s);
    // ED 3 clears it.
    term.feed("\x1b[3J");
    QCOMPARE(s.historySize(), 0);
    // No scrollback at all.
    Term none(10, 2, 0);
    none.feed("a\r\nb\r\nc\r\n");
    QCOMPARE(none.screen().historySize(), 0);
}

void TestTerm::alternateScreen()
{
    Term term(20, 4);
    term.feed("main 1\r\nmain 2\x1b[?1049h");
    QVERIFY(term.screen().alternateScreen());
    QCOMPARE(rowsOf(term.screen()), QStringList());
    term.feed("\x1b[Halt\r\n\r\n\r\n\r\n\r\nfull screen\x1b[2;5r\x1b[31m");
    // The alternate screen has no scrollback.
    QCOMPARE(term.screen().historySize(), 0);
    term.feed("\x1b[?1049l");
    QVERIFY(!term.screen().alternateScreen());
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"main 1"_s, u"main 2"_s}));
    QCOMPARE(term.screen().cursorRow(), 1);
    QCOMPARE(term.screen().cursorColumn(), 6);
    QCOMPARE(term.screen().pen().fg, Color());
    // Entering it again starts empty.
    term.feed("\x1b[?1049h");
    QCOMPARE(rowsOf(term.screen()), QStringList());
    term.feed("\x1b[?1049l");

    // ?47 keeps the alternate screen's content, ?1047 clears it when leaving.
    term.feed("\x1b[?47hA\x1b[?47l\x1b[?47h");
    QCOMPARE(term.screen().line(1).trimmedText().trimmed(), u"A"_s);
    term.feed("\x1b[?47l\x1b[?1047hB\x1b[?1047l\x1b[?1047h");
    QCOMPARE(rowsOf(term.screen()), QStringList());
    term.feed("\x1b[?1047l");
    QCOMPARE(term.screen().line(0).trimmedText(), u"main 1"_s);
    // ?1048 saves and restores the cursor.
    term.feed("\x1b[3;3H\x1b[?1048h\x1b[H\x1b[?1048lX");
    QCOMPARE(term.cell(2, 2).ch, char32_t(U'X'));
}

void TestTerm::modes()
{
    Term term;
    QSignalSpy changed(&term.t, &Terminal::modesChanged);
    term.feed("\x1b[?1h\x1b[?2004h\x1b[?1000;1006h\x1b[?1004h\x1b=");
    const InputModes &m = term.t.modes();
    QVERIFY(m.applicationCursorKeys);
    QVERIFY(m.bracketedPaste);
    QCOMPARE(m.mouseTracking, MouseTracking::Click);
    QVERIFY(m.mouseSgr);
    QVERIFY(m.focusEvents);
    QVERIFY(m.applicationKeypad);
    QVERIFY(changed.size() >= 5);
    term.feed("\x1b[?1002h");
    QCOMPARE(term.t.modes().mouseTracking, MouseTracking::Drag);
    term.feed("\x1b[?1000l"); // not the current one: unchanged
    QCOMPARE(term.t.modes().mouseTracking, MouseTracking::Drag);
    term.feed("\x1b[?1002l\x1b[?1l\x1b>");
    QCOMPARE(term.t.modes().mouseTracking, MouseTracking::Off);
    QVERIFY(!term.t.modes().applicationCursorKeys);
    QVERIFY(!term.t.modes().applicationKeypad);
    term.feed("\x1b[?2026h");
    QVERIFY(term.t.modes().synchronizedOutput);
    term.feed("\x1b[?25l");
    QVERIFY(!term.screen().cursorVisible());
    term.feed("\x1b[?5h");
    QVERIFY(term.screen().reverseVideo());
    // DECSTR: the cursor shows again, keys back to normal; content stays.
    term.feed("text\x1b[?1h\x1b[!p");
    QVERIFY(term.screen().cursorVisible());
    QVERIFY(!term.t.modes().applicationCursorKeys);
    QCOMPARE(term.screen().line(0).trimmedText(), u"text"_s);
    // RIS: modes off.
    term.feed("\x1b"
              "c");
    QVERIFY(!term.t.modes().bracketedPaste);
    QVERIFY(!term.screen().reverseVideo());
}

void TestTerm::cursorStyle()
{
    Term term;
    QCOMPARE(term.screen().cursorShape(), CursorShape::Block);
    QVERIFY(term.screen().cursorBlinking());
    term.feed("\x1b[5 q");
    QCOMPARE(term.screen().cursorShape(), CursorShape::Bar);
    QVERIFY(term.screen().cursorBlinking());
    term.feed("\x1b[4 q");
    QCOMPARE(term.screen().cursorShape(), CursorShape::Underline);
    QVERIFY(!term.screen().cursorBlinking());
    term.feed("\x1b[2 q");
    QCOMPARE(term.screen().cursorShape(), CursorShape::Block);
    QVERIFY(!term.screen().cursorBlinking());
    term.feed("\x1b[?12h");
    QVERIFY(term.screen().cursorBlinking());
    term.feed("\x1b[ q");
    QCOMPARE(term.screen().cursorShape(), CursorShape::Block);
}

void TestTerm::resize()
{
    // Without reflow (as the alternate screen always is): lines cut and padded.
    Term term(10, 4);
    term.t.screen().setReflow(false);
    term.feed("0123456789abc\r\nline 2\x1b[4;10H");
    term.t.resize(5, 4);
    QCOMPARE(term.screen().columns(), 5);
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"01234"_s, u"abc"_s, u"line"_s}));
    QCOMPARE(term.screen().cursorColumn(), 4);
    QCOMPARE(term.screen().cursorRow(), 3);
    for (int row = 0; row < 4; ++row)
        QCOMPARE(term.screen().line(row).cells.size(), 5);
    // Wider: padded with blanks, content not reflowed.
    term.t.resize(20, 4);
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"01234"_s, u"abc"_s, u"line"_s}));
    term.feed("\x1b[1;20HZ");
    QCOMPARE(term.screen().line(0).trimmedText(), u"01234"_s + QString(14, u' ') + u"Z"_s);
    // Tab stops follow the new width.
    term.feed("\r\n\x1b[2K\t\t\tT");
    QCOMPARE(term.screen().cursorColumn(), 19);
    // A wide character cut by the new width goes.
    Term wide(4, 2);
    wide.t.screen().setReflow(false);
    wide.feed(QString::fromUcs4(U"ab\x4E2D").toUtf8());
    wide.t.resize(3, 2);
    QCOMPARE(wide.screen().line(0).trimmedText(), u"ab"_s);
    QVERIFY(!wide.cell(0, 2).isWide());
    // Odd sizes are clamped, never zero.
    term.t.resize(0, 0);
    QCOMPARE(term.screen().columns(), 1);
    QCOMPARE(term.screen().rows(), 1);
    term.feed("xyz\r\n\x1b[5;5H\x1b[3@\x1b[3P\x1b[L\x1b[M\x1b[2J"
              "ok");
    QCOMPARE(term.screen().line(0).trimmedText(), u"k"_s);
    term.t.resize(100000, 100000);
    QVERIFY(term.screen().columns() <= 1000);
    QVERIFY(term.screen().rows() <= 500);
    // The scroll region resets.
    Term region(10, 6);
    region.feed("\x1b[2;3r");
    region.t.resize(10, 8);
    QCOMPARE(region.screen().scrollTop(), 0);
    QCOMPARE(region.screen().scrollBottom(), 7);
}

void TestTerm::resizePushesIntoScrollback()
{
    Term term(10, 5);
    term.feed("1\r\n2\r\n3\r\n4\r\n5");
    term.t.resize(10, 3);
    // The cursor's row stays: the rows above it go into the scrollback.
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"3"_s, u"4"_s, u"5"_s}));
    QCOMPARE(term.screen().cursorRow(), 2);
    QCOMPARE(term.screen().historySize(), 2);
    // Taller again: they come back.
    term.t.resize(10, 5);
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"1"_s, u"2"_s, u"3"_s, u"4"_s, u"5"_s}));
    QCOMPARE(term.screen().historySize(), 0);
    QCOMPARE(term.screen().cursorRow(), 4);
    // With the cursor at the top, the bottom rows go instead.
    Term top(10, 5);
    top.feed("a\x1b[H");
    top.t.resize(10, 2);
    QCOMPARE(top.screen().historySize(), 0);
    QCOMPARE(top.screen().line(0).trimmedText(), u"a"_s);
    // On the alternate screen the main screen's saved cursor decides.
    Term alt(10, 4);
    alt.feed("1\r\n2\r\n3\r\n4\x1b[?1049h\x1b[4;1Hx");
    alt.t.resize(10, 2);
    QCOMPARE(alt.screen().line(1).trimmedText(), u"x"_s);
    alt.feed("\x1b[?1049l");
    QCOMPARE(rowsOf(alt.screen()), (QStringList{u"3"_s, u"4"_s}));
    QCOMPARE(alt.screen().cursorRow(), 1);
}

void TestTerm::resizeReflows()
{
    // Narrower: a long line wraps again; wider: what wrapped joins; the cursor stays on its character.
    Term term(20, 5, 100);
    term.feed("zażółć gęślą jaźń 0123456789\r\n$ ls");
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"zażółć gęślą jaźń 01"_s, u"23456789"_s, u"$ ls"_s}));
    term.t.resize(10, 5);
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"zażółć gęś"_s, u"lą jaźń 01"_s, u"23456789"_s, u"$ ls"_s}));
    QCOMPARE(term.screen().cursorRow(), 3);
    QCOMPARE(term.screen().cursorColumn(), 4);
    QVERIFY(term.screen().line(0).wrapped && term.screen().line(1).wrapped && !term.screen().line(2).wrapped);
    term.t.resize(40, 5);
    QCOMPARE(rowsOf(term.screen()), (QStringList{u"zażółć gęślą jaźń 0123456789"_s, u"$ ls"_s}));
    QCOMPARE(term.screen().cursorRow(), 1);
    QCOMPARE(term.screen().cursorColumn(), 4);
    term.feed("x");
    QCOMPARE(term.screen().line(1).trimmedText(), u"$ lsx"_s);

    // Rows beyond the screen go into the scrollback, and come back from it.
    Term small(10, 3, 100);
    small.feed("aaaaaaaaaabbbbbbbbbbcccccccccc\r\n$ ");
    QCOMPARE(small.screen().historySize(), 1);
    small.t.resize(30, 3);
    QCOMPARE(small.screen().historySize(), 0);
    QCOMPARE(rowsOf(small.screen()), (QStringList{u"aaaaaaaaaabbbbbbbbbbcccccccccc"_s, u"$"_s}));
    QCOMPARE(small.screen().cursorRow(), 1);
    QCOMPARE(small.screen().cursorColumn(), 2);
    small.t.resize(5, 3);
    QCOMPARE(small.screen().historySize(), 4);
    QCOMPARE(rowsOf(small.screen()), (QStringList{u"ccccc"_s, u"ccccc"_s, u"$"_s}));
    QCOMPARE(small.screen().cursorRow(), 2);
    QCOMPARE(small.screen().cursorColumn(), 2);
    QCOMPARE(logicalLine(small.screen(), -4, false).text, u"aaaaaaaaaabbbbbbbbbbcccccccccc"_s);
    // A cleared screen over a scrollback: the prompt stays at the top, the scrollback above it.
    Term cleared(10, 4, 100);
    cleared.feed("1\r\n2\r\n3\r\n4\r\n5\r\n6\r\n\x1b[2J\x1b[H$ ");
    const int history = cleared.screen().historySize();
    cleared.t.resize(8, 4);
    QCOMPARE(cleared.screen().cursorRow(), 0);
    QCOMPARE(cleared.screen().historySize(), history);
    QCOMPARE(cleared.screen().line(0).trimmedText(), u"$"_s);

    // A wide character is never split: one that does not fit wraps first.
    Term wide(6, 3, 10);
    wide.feed(QString::fromUcs4(U"ab\x4E2D\x6587\x5B57\r\n").toUtf8());
    wide.t.resize(5, 3);
    QCOMPARE(wide.screen().line(0).trimmedText(), QString::fromUcs4(U"ab\x4E2D"));
    QCOMPARE(wide.screen().line(1).trimmedText(), QString::fromUcs4(U"\x6587\x5B57"));
    QVERIFY(wide.screen().line(0).wrapped);
    QVERIFY(wide.cell(1, 0).isWide());
    QVERIFY(wide.cell(1, 1).isWideTail());

    // The blank a wide character left at the margin when it wrapped is no space of the line.
    Term padded(5, 3, 10);
    padded.feed(QString::fromUcs4(U"abcd\x597D").toUtf8());
    QVERIFY(padded.screen().line(0).wrapped);
    QCOMPARE(logicalLine(padded.screen(), 0, false).text, QString::fromUcs4(U"abcd\x597D"));
    padded.t.resize(10, 3);
    QCOMPARE(padded.screen().line(0).trimmedText(), QString::fromUcs4(U"abcd\x597D"));
    QCOMPARE(padded.screen().cursorColumn(), 6);

    // The saved cursor (DECSC) stays on its character too.
    Term saved(10, 5, 10);
    saved.feed("abc\x1b" "7defgh\r\n$ ");
    saved.t.resize(4, 5);
    saved.feed("\x1b" "8X");
    QCOMPARE(saved.screen().line(0).trimmedText(), u"abcX"_s);
    QCOMPARE(saved.screen().line(1).trimmedText(), u"efgh"_s);

    // Combining marks and links go with their characters.
    Term marks(6, 3, 10);
    marks.feed("abcde\xcc\x81" "f\x1b]8;;https://x.pl\x07LINK\x1b]8;;\x07");
    marks.t.resize(4, 4);
    QCOMPARE(rowsOf(marks.screen()), (QStringList{u"abcd"_s, u"e\u0301fLI"_s, u"NK"_s}));
    QCOMPARE(marks.screen().line(1).cellText(0), u"e\u0301"_s);
    QCOMPARE(marks.screen().hyperlink(marks.cell(1, 2).link), u"https://x.pl"_s);
    QCOMPARE(marks.screen().hyperlink(marks.cell(2, 1).link), u"https://x.pl"_s);
    QCOMPARE(marks.screen().cursorRow(), 2);
    QCOMPARE(marks.screen().cursorColumn(), 2);

    // The alternate screen is not reflowed (its program redraws it), nor the main one under it.
    Term alt(10, 3, 10);
    alt.feed("0123456789AB\x1b[?1049h\x1b[Hfull");
    alt.t.resize(5, 3);
    QCOMPARE(alt.screen().line(0).trimmedText(), u"full"_s);
    alt.feed("\x1b[?1049l");
    QCOMPARE(alt.screen().line(0).trimmedText(), u"01234"_s);

    // One column: wide characters become blanks, nothing breaks.
    Term narrow(8, 3, 10);
    narrow.feed(QString::fromUcs4(U"a\x4E2Db").toUtf8());
    narrow.t.resize(1, 3);
    for (int row = 0; row < 3; ++row)
        QCOMPARE(narrow.screen().line(row).cells.size(), 1);
    QCOMPARE(narrow.screen().cursorRow(), 2);
}

void TestTerm::resetKeepsScrollback()
{
    Term term(10, 2);
    term.feed("a\r\nb\r\nc\x1b]0;title\x07\x1b[?2004h");
    QCOMPARE(term.screen().historySize(), 1);
    term.feed("\x1b"
              "c");
    QCOMPARE(rowsOf(term.screen()), QStringList());
    QCOMPARE(term.screen().historySize(), 1);
    QCOMPARE(term.t.title(), u"title"_s);
    // Terminal::reset() is a new terminal.
    term.t.reset();
    QCOMPARE(term.screen().historySize(), 0);
    QCOMPARE(term.t.title(), QString());
    QCOMPARE(term.screen().cursorRow(), 0);
    // A sequence cut by reset() does not go on.
    term.feed("\x1b[3");
    term.t.reset();
    term.feed("1mX");
    QCOMPARE(term.screen().line(0).trimmedText(), u"1mX"_s);
}

void TestTerm::dirtyRows()
{
    Term term(10, 5);
    Screen &s = term.t.screen();
    QVERIFY(s.anyDirty());
    s.clearDirty();
    QVERIFY(!s.anyDirty());
    term.feed("\x1b[3;1Hx");
    QVERIFY(s.anyDirty());
    for (int row = 0; row < 5; ++row)
        QCOMPARE(s.isRowDirty(row), row == 2);
    s.clearDirty();
    // Cursor motion alone is no change of the cells.
    term.feed("\x1b[5;5H\x1b[?25l");
    QVERIFY(!s.anyDirty());
    // Scrolling changes every row and counts the lines into the scrollback.
    term.feed("\r\n\r\n");
    QCOMPARE(s.scrolledIntoHistory(), 2);
    for (int row = 0; row < 5; ++row)
        QVERIFY(s.isRowDirty(row));
    s.clearDirty();
    QCOMPARE(s.scrolledIntoHistory(), 0);
    term.feed("\x1b[2;1H\x1b[K");
    QVERIFY(s.isRowDirty(1));
    QVERIFY(!s.isRowDirty(0));
}

void TestTerm::bell()
{
    Term term;
    QSignalSpy bells(&term.t, &Terminal::bell);
    term.feed("a\x07"
              "b\x07");
    QCOMPARE(bells.size(), 2);
    // BEL ending an OSC is no bell.
    term.feed("\x1b]0;x\x07");
    QCOMPARE(bells.size(), 2);
    QCOMPARE(term.screen().line(0).trimmedText(), u"ab"_s);
}

void TestTerm::charWidths_data()
{
    QTest::addColumn<uint>("cp");
    QTest::addColumn<int>("width");
    QTest::newRow("a") << uint(U'a') << 1;
    QTest::newRow("ż") << uint(0x017C) << 1;
    QTest::newRow("soft hyphen") << uint(0x00AD) << 1;
    QTest::newRow("combining acute") << uint(0x0301) << 0;
    QTest::newRow("ZWJ") << uint(0x200D) << 0;
    QTest::newRow("VS16") << uint(0xFE0F) << 0;
    QTest::newRow("Hangul jungseong") << uint(0x1161) << 0;
    QTest::newRow("Hangul choseong") << uint(0x1100) << 2;
    QTest::newRow("CJK") << uint(0x4E2D) << 2;
    QTest::newRow("fullwidth A") << uint(0xFF21) << 2;
    QTest::newRow("hiragana") << uint(0x3042) << 2;
    QTest::newRow("emoji") << uint(0x1F600) << 2;
    QTest::newRow("watch (emoji presentation)") << uint(0x231A) << 2;
    QTest::newRow("heart (text presentation)") << uint(0x2764) << 1;
    QTest::newRow("box drawing") << uint(0x2500) << 1;
    QTest::newRow("plane 2") << uint(0x20000) << 2;
    QTest::newRow("plane 3 unassigned") << uint(0x3FFFD) << 2;
    QTest::newRow("regional indicator") << uint(0x1F1E6) << 1;
    QTest::newRow("last code point") << uint(0x10FFFF) << 1;
}

void TestTerm::charWidths()
{
    QFETCH(uint, cp);
    QFETCH(int, width);
    QCOMPARE(charWidth(char32_t(cp)), width);
}

QTEST_GUILESS_MAIN(TestTerm)
#include "tst_term.moc"
