// What the terminal's input sends (src/core/term/keys.h): every special key with and without
// modifiers and application cursor keys, Ctrl's control codes, Alt's ESC prefix, Polish text,
// pastes (line breaks, control characters, bracketed paste that the text cannot end), focus
// reports and mouse reports in both encodings for each tracking mode.
#include "term/keys.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::term;

class TestTermKeys : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void keys_data();
    void keys();
    void paste_data();
    void paste();
    void text();
    void focus();
    void mouse_data();
    void mouse();
};

void TestTermKeys::keys_data()
{
    QTest::addColumn<int>("key");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("applicationCursor");
    QTest::addColumn<QByteArray>("expected");

    const int none = Qt::NoModifier;
    const int shift = Qt::ShiftModifier;
    const int ctrl = Qt::ControlModifier;
    const int alt = Qt::AltModifier;
    const int meta = Qt::MetaModifier;

    QTest::newRow("up") << int(Qt::Key_Up) << none << QString() << false << "\x1b[A"_ba;
    QTest::newRow("up, application") << int(Qt::Key_Up) << none << QString() << true << "\x1bOA"_ba;
    QTest::newRow("down") << int(Qt::Key_Down) << none << QString() << false << "\x1b[B"_ba;
    QTest::newRow("right") << int(Qt::Key_Right) << none << QString() << false << "\x1b[C"_ba;
    QTest::newRow("left, application") << int(Qt::Key_Left) << none << QString() << true << "\x1bOD"_ba;
    QTest::newRow("shift up") << int(Qt::Key_Up) << shift << QString() << false << "\x1b[1;2A"_ba;
    QTest::newRow("ctrl left, application") << int(Qt::Key_Left) << ctrl << QString() << true << "\x1b[1;5D"_ba;
    QTest::newRow("alt right") << int(Qt::Key_Right) << alt << QString() << false << "\x1b[1;3C"_ba;
    QTest::newRow("ctrl shift alt meta down")
            << int(Qt::Key_Down) << (ctrl | shift | alt | meta) << QString() << false << "\x1b[1;16B"_ba;
    QTest::newRow("keypad modifier ignored")
            << int(Qt::Key_Up) << int(Qt::KeypadModifier) << QString() << false << "\x1b[A"_ba;
    QTest::newRow("home") << int(Qt::Key_Home) << none << QString() << false << "\x1b[H"_ba;
    QTest::newRow("end, application") << int(Qt::Key_End) << none << QString() << true << "\x1bOF"_ba;
    QTest::newRow("ctrl end") << int(Qt::Key_End) << ctrl << QString() << false << "\x1b[1;5F"_ba;
    QTest::newRow("insert") << int(Qt::Key_Insert) << none << QString() << false << "\x1b[2~"_ba;
    QTest::newRow("delete") << int(Qt::Key_Delete) << none << QString() << false << "\x1b[3~"_ba;
    QTest::newRow("shift delete") << int(Qt::Key_Delete) << shift << QString() << false << "\x1b[3;2~"_ba;
    QTest::newRow("page up") << int(Qt::Key_PageUp) << none << QString() << false << "\x1b[5~"_ba;
    QTest::newRow("page down") << int(Qt::Key_PageDown) << none << QString() << false << "\x1b[6~"_ba;
    QTest::newRow("ctrl page down") << int(Qt::Key_PageDown) << ctrl << QString() << false << "\x1b[6;5~"_ba;
    QTest::newRow("F1") << int(Qt::Key_F1) << none << QString() << false << "\x1bOP"_ba;
    QTest::newRow("F2") << int(Qt::Key_F2) << none << QString() << false << "\x1bOQ"_ba;
    QTest::newRow("F3") << int(Qt::Key_F3) << none << QString() << false << "\x1bOR"_ba;
    QTest::newRow("F4") << int(Qt::Key_F4) << none << QString() << false << "\x1bOS"_ba;
    QTest::newRow("shift F1") << int(Qt::Key_F1) << shift << QString() << false << "\x1b[1;2P"_ba;
    QTest::newRow("F5") << int(Qt::Key_F5) << none << QString() << false << "\x1b[15~"_ba;
    QTest::newRow("F6") << int(Qt::Key_F6) << none << QString() << false << "\x1b[17~"_ba;
    QTest::newRow("F7") << int(Qt::Key_F7) << none << QString() << false << "\x1b[18~"_ba;
    QTest::newRow("F8") << int(Qt::Key_F8) << none << QString() << false << "\x1b[19~"_ba;
    QTest::newRow("F9") << int(Qt::Key_F9) << none << QString() << false << "\x1b[20~"_ba;
    QTest::newRow("F10") << int(Qt::Key_F10) << none << QString() << false << "\x1b[21~"_ba;
    QTest::newRow("F11") << int(Qt::Key_F11) << none << QString() << false << "\x1b[23~"_ba;
    QTest::newRow("F12") << int(Qt::Key_F12) << none << QString() << false << "\x1b[24~"_ba;
    QTest::newRow("ctrl F12") << int(Qt::Key_F12) << ctrl << QString() << false << "\x1b[24;5~"_ba;
    QTest::newRow("enter") << int(Qt::Key_Return) << none << u"\r"_s << false << "\r"_ba;
    QTest::newRow("keypad enter") << int(Qt::Key_Enter) << int(Qt::KeypadModifier) << u"\r"_s << false << "\r"_ba;
    QTest::newRow("alt enter") << int(Qt::Key_Return) << alt << u"\r"_s << false << "\x1b\r"_ba;
    QTest::newRow("backspace") << int(Qt::Key_Backspace) << none << u"\b"_s << false << "\x7f"_ba;
    QTest::newRow("ctrl backspace") << int(Qt::Key_Backspace) << ctrl << QString() << false << "\x08"_ba;
    QTest::newRow("alt backspace") << int(Qt::Key_Backspace) << alt << QString() << false << "\x1b\x7f"_ba;
    QTest::newRow("tab") << int(Qt::Key_Tab) << none << u"\t"_s << false << "\t"_ba;
    QTest::newRow("shift tab as backtab") << int(Qt::Key_Backtab) << shift << QString() << false << "\x1b[Z"_ba;
    QTest::newRow("shift tab as tab") << int(Qt::Key_Tab) << shift << QString() << false << "\x1b[Z"_ba;
    QTest::newRow("ctrl tab: nothing") << int(Qt::Key_Tab) << ctrl << QString() << false << ""_ba;
    QTest::newRow("escape") << int(Qt::Key_Escape) << none << u"\x1b"_s << false << "\x1b"_ba;
    QTest::newRow("alt escape") << int(Qt::Key_Escape) << alt << QString() << false << "\x1b\x1b"_ba;
    QTest::newRow("ctrl c") << int(Qt::Key_C) << ctrl << u"\x03"_s << false << "\x03"_ba;
    QTest::newRow("ctrl a") << int(Qt::Key_A) << ctrl << QString() << false << "\x01"_ba;
    QTest::newRow("ctrl z") << int(Qt::Key_Z) << ctrl << QString() << false << "\x1a"_ba;
    QTest::newRow("ctrl shift d") << int(Qt::Key_D) << (ctrl | shift) << QString() << false << "\x04"_ba;
    QTest::newRow("ctrl alt x") << int(Qt::Key_X) << (ctrl | alt) << QString() << false << "\x1b\x18"_ba;
    QTest::newRow("ctrl space") << int(Qt::Key_Space) << ctrl << u" "_s << false << QByteArray(1, '\0');
    QTest::newRow("ctrl @") << int(Qt::Key_At) << ctrl << QString() << false << QByteArray(1, '\0');
    QTest::newRow("ctrl 2") << int(Qt::Key_2) << ctrl << QString() << false << QByteArray(1, '\0');
    QTest::newRow("ctrl [") << int(Qt::Key_BracketLeft) << ctrl << QString() << false << "\x1b"_ba;
    QTest::newRow("ctrl \\") << int(Qt::Key_Backslash) << ctrl << QString() << false << "\x1c"_ba;
    QTest::newRow("ctrl ]") << int(Qt::Key_BracketRight) << ctrl << QString() << false << "\x1d"_ba;
    QTest::newRow("ctrl ^") << int(Qt::Key_AsciiCircum) << ctrl << QString() << false << "\x1e"_ba;
    QTest::newRow("ctrl 6") << int(Qt::Key_6) << ctrl << QString() << false << "\x1e"_ba;
    QTest::newRow("ctrl _") << int(Qt::Key_Underscore) << ctrl << QString() << false << "\x1f"_ba;
    QTest::newRow("ctrl /") << int(Qt::Key_Slash) << ctrl << QString() << false << "\x1f"_ba;
    QTest::newRow("ctrl ?") << int(Qt::Key_Question) << ctrl << QString() << false << "\x7f"_ba;
    QTest::newRow("ctrl 8") << int(Qt::Key_8) << ctrl << QString() << false << "\x7f"_ba;
    QTest::newRow("ctrl with another layout's text") << int(Qt::Key_unknown) << ctrl << u"\x11"_s << false << "\x11"_ba;
    QTest::newRow("ctrl with a key without a code") << int(Qt::Key_Comma) << ctrl << u","_s << false << ","_ba;
    QTest::newRow("alt a") << int(Qt::Key_A) << alt << u"a"_s << false
                           << "\x1b"
                              "a"_ba;
    QTest::newRow("alt ż") << int(Qt::Key_unknown) << alt << u"ż"_s << false << "\x1b\xc5\xbc"_ba;
    QTest::newRow("letter") << int(Qt::Key_A) << none << u"a"_s << false << "a"_ba;
    QTest::newRow("shifted letter") << int(Qt::Key_A) << shift << u"A"_s << false << "A"_ba;
    QTest::newRow("space") << int(Qt::Key_Space) << none << u" "_s << false << " "_ba;
    QTest::newRow("Polish") << int(Qt::Key_unknown) << int(Qt::GroupSwitchModifier) << u"ą"_s << false << "\xc4\x85"_ba;
    QTest::newRow("dead key composed") << int(Qt::Key_unknown) << none << u"ź"_s << false << "\xc5\xba"_ba;
    QTest::newRow("emoji") << int(Qt::Key_unknown) << none << u"\U0001F600"_s << false << "\xf0\x9f\x98\x80"_ba;
    QTest::newRow("shift alone") << int(Qt::Key_Shift) << shift << QString() << false << ""_ba;
    QTest::newRow("control alone") << int(Qt::Key_Control) << ctrl << QString() << false << ""_ba;
    QTest::newRow("unknown without text") << int(Qt::Key_LaunchMail) << none << QString() << false << ""_ba;
}

void TestTermKeys::keys()
{
    QFETCH(int, key);
    QFETCH(int, modifiers);
    QFETCH(QString, text);
    QFETCH(bool, applicationCursor);
    QFETCH(QByteArray, expected);

    InputModes modes;
    modes.applicationCursorKeys = applicationCursor;
    QCOMPARE(encodeKey(key, Qt::KeyboardModifiers(modifiers), text, modes), expected);
}

void TestTermKeys::paste_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<bool>("bracketed");
    QTest::addColumn<QByteArray>("expected");

    QTest::newRow("plain") << u"ls -la"_s << false << "ls -la"_ba;
    QTest::newRow("LF") << u"a\nb\n"_s << false << "a\rb\r"_ba;
    QTest::newRow("CR LF") << u"a\r\nb\r\n"_s << false << "a\rb\r"_ba;
    QTest::newRow("CR") << u"a\rb"_s << false << "a\rb"_ba;
    QTest::newRow("tab kept") << u"a\tb"_s << false << "a\tb"_ba;
    QTest::newRow("controls dropped") << u"a\x03"
                                         "b\x1b[31mc\x7f\x9b"
                                         "d"_s
                                      << false << "ab[31mcd"_ba;
    QTest::newRow("Polish") << u"zażółć gęślą jaźń"_s << false << u"zażółć gęślą jaźń"_s.toUtf8();
    QTest::newRow("bracketed") << u"echo hi\n"_s << true << "\x1b[200~echo hi\r\x1b[201~"_ba;
    QTest::newRow("bracketed cannot be ended by the text")
            << u"x\x1b[201~rm -rf ~\n"_s << true << "\x1b[200~x[201~rm -rf ~\r\x1b[201~"_ba;
    QTest::newRow("empty bracketed") << QString() << true << "\x1b[200~\x1b[201~"_ba;
}

void TestTermKeys::paste()
{
    QFETCH(QString, text);
    QFETCH(bool, bracketed);
    QFETCH(QByteArray, expected);
    QCOMPARE(encodePaste(text, bracketed), expected);
}

void TestTermKeys::text()
{
    QCOMPARE(encodeText(u"ąę"_s), "\xc4\x85\xc4\x99"_ba);
    QCOMPARE(encodeText(u"a\x1b"
                        "b"_s),
             "ab"_ba);
    QCOMPARE(encodeText(u"a\nb"_s), "a\rb"_ba);
    QCOMPARE(encodeText(QString()), QByteArray());
}

void TestTermKeys::focus()
{
    InputModes modes;
    QCOMPARE(encodeFocus(true, modes), QByteArray());
    modes.focusEvents = true;
    QCOMPARE(encodeFocus(true, modes), "\x1b[I"_ba);
    QCOMPARE(encodeFocus(false, modes), "\x1b[O"_ba);
}

void TestTermKeys::mouse_data()
{
    QTest::addColumn<int>("tracking");
    QTest::addColumn<bool>("sgr");
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("button");
    QTest::addColumn<int>("column");
    QTest::addColumn<int>("row");
    QTest::addColumn<int>("modifiers");
    QTest::addColumn<QByteArray>("expected");

    const int off = int(MouseTracking::Off), x10 = int(MouseTracking::X10), click = int(MouseTracking::Click),
              drag = int(MouseTracking::Drag), motion = int(MouseTracking::Motion);
    const int press = MouseEvent::Press, release = MouseEvent::Release, move = MouseEvent::Move,
              up = MouseEvent::WheelUp, down = MouseEvent::WheelDown;
    const int none = Qt::NoModifier;

    QTest::newRow("off") << off << false << press << 0 << 0 << 0 << none << ""_ba;
    QTest::newRow("legacy press") << click << false << press << 0 << 4 << 2 << none << "\x1b[M %#"_ba;
    QTest::newRow("legacy release") << click << false << release << 0 << 4 << 2 << none << "\x1b[M#%#"_ba;
    QTest::newRow("legacy right with ctrl")
            << click << false << press << 2 << 0 << 0 << int(Qt::ControlModifier) << "\x1b[M2!!"_ba;
    QTest::newRow("legacy beyond 223") << click << false << press << 0 << 300 << 0 << none << ""_ba;
    QTest::newRow("legacy at 222") << click << false << press << 0 << 222 << 0 << none << QByteArray("\x1b[M \xff!", 6);
    QTest::newRow("SGR press") << click << true << press << 0 << 4 << 2 << none << "\x1b[<0;5;3M"_ba;
    QTest::newRow("SGR release") << click << true << release << 1 << 4 << 2 << none << "\x1b[<1;5;3m"_ba;
    QTest::newRow("SGR beyond 223") << click << true << press << 0 << 300 << 500 << none << "\x1b[<0;301;501M"_ba;
    QTest::newRow("SGR shift alt") << click << true << press << 0 << 0 << 0 << int(Qt::ShiftModifier | Qt::AltModifier)
                                   << "\x1b[<12;1;1M"_ba;
    QTest::newRow("wheel up") << click << true << up << -1 << 0 << 0 << none << "\x1b[<64;1;1M"_ba;
    QTest::newRow("wheel down legacy") << click << false << down << -1 << 0 << 0 << none << "\x1b[Ma!!"_ba;
    QTest::newRow("click: no motion") << click << true << move << 0 << 1 << 1 << none << ""_ba;
    QTest::newRow("drag: motion with a button") << drag << true << move << 0 << 1 << 1 << none << "\x1b[<32;2;2M"_ba;
    QTest::newRow("drag: no motion without one") << drag << true << move << -1 << 1 << 1 << none << ""_ba;
    QTest::newRow("motion: without a button") << motion << true << move << -1 << 1 << 1 << none << "\x1b[<35;2;2M"_ba;
    QTest::newRow("X10: press only") << x10 << false << press << 0 << 0 << 0 << int(Qt::ShiftModifier)
                                     << "\x1b[M !!"_ba;
    QTest::newRow("X10: no release") << x10 << false << release << 0 << 0 << 0 << none << ""_ba;
    QTest::newRow("press without a button") << click << true << press << -1 << 0 << 0 << none << ""_ba;
}

void TestTermKeys::mouse()
{
    QFETCH(int, tracking);
    QFETCH(bool, sgr);
    QFETCH(int, kind);
    QFETCH(int, button);
    QFETCH(int, column);
    QFETCH(int, row);
    QFETCH(int, modifiers);
    QFETCH(QByteArray, expected);

    InputModes modes;
    modes.mouseTracking = MouseTracking(tracking);
    modes.mouseSgr = sgr;
    MouseEvent event;
    event.kind = MouseEvent::Kind(kind);
    event.button = button;
    event.column = column;
    event.row = row;
    event.modifiers = Qt::KeyboardModifiers(modifiers);
    QCOMPARE(encodeMouse(event, modes), expected);
}

QTEST_GUILESS_MAIN(TestTermKeys)
#include "tst_term_keys.moc"
