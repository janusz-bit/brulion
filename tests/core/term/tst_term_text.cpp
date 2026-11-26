// The terminal's text as the user reads it (src/core/term/screentext.h, links.h): rows joined where
// the text wrapped and mapped back to cells (wide characters, combining marks, the scrollback),
// searching the scrollback (case, wrapped matches, the limit, Polish text), "Clear" keeping the
// prompt's row, and links without OSC 8 — URLs with their punctuation and brackets, file paths as
// compilers, test runners, grep, git and Python print them, words that are no paths, and resolving
// paths against the working directory, the home folder and a diff's a/ and b/.
#include "term/term.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::term;

namespace {

QStringList rowsOf(const Screen &s)
{
    QStringList out;
    for (int row = 0; row < s.rows(); ++row)
        out.append(s.line(row).trimmedText());
    while (!out.isEmpty() && out.last().isEmpty())
        out.removeLast();
    return out;
}

QString linkText(QStringView text, const TextLink &link)
{
    return text.mid(link.start, link.length).toString();
}

bool touch(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write("x") == 1;
}

} // namespace

class TestTermText : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void logicalLines();
    void logicalLineMapsCells();
    void logicalLineOutOfRange();
    void findInScrollback();
    void findCaseAndPolish_data();
    void findCaseAndPolish();
    void findAcrossWrappedRows();
    void findLimitAndEmpty();
    void clearKeepsPromptRow();
    void clearOnAlternateScreen();
    void links_data();
    void links();
    void urlPunctuation_data();
    void urlPunctuation();
    void noLinks_data();
    void noLinks();
    void linkAtIndex();
    void resolvePaths();
    void hostileLineStaysCheap();
};

void TestTermText::logicalLines()
{
    Terminal t(10, 5, 100);
    t.feed("abcdefghijKLM\r\nshort\r\n");
    const Screen &s = t.screen();
    // Row 0 wrapped into row 1: one line of text, from either row.
    for (int row : {0, 1}) {
        const LogicalLine l = logicalLine(s, row, false);
        QCOMPARE(l.first, 0);
        QCOMPARE(l.last, 1);
        QCOMPARE(l.text, u"abcdefghijKLM"_s);
        QVERIFY(l.cells.isEmpty());
    }
    const LogicalLine l = logicalLine(s, 2, false);
    QCOMPARE(l.first, 2);
    QCOMPARE(l.last, 2);
    QCOMPARE(l.text, u"short"_s);
}

void TestTermText::logicalLineMapsCells()
{
    Terminal t(20, 4, 100);
    // A wide character (2 cells), a combining mark joined to its letter, a character beyond the BMP.
    t.feed("a\xe4\xb8\xad" "e\xcc\x81" "\xf0\x9f\x98\x80z");
    const LogicalLine l = logicalLine(t.screen(), 0, true);
    QCOMPARE(l.text, u"a中e\u0301😀z"_s);
    QCOMPARE(l.cells.size(), l.text.size());
    QCOMPARE(l.cells.at(0), (CellPos{0, 0}));
    QCOMPARE(l.cells.at(1), (CellPos{0, 1})); // 中 at 1-2
    QCOMPARE(l.widths.at(1), quint8(2));
    QCOMPARE(l.cells.at(2), (CellPos{0, 3})); // e
    QCOMPARE(l.cells.at(3), (CellPos{0, 3})); // its combining mark: the same cell
    QCOMPARE(l.cells.at(4), (CellPos{0, 4})); // 😀 (two UTF-16 units, two cells)
    QCOMPARE(l.cells.at(5), (CellPos{0, 4}));
    QCOMPARE(l.cells.at(6), (CellPos{0, 6})); // z
    QCOMPARE(l.endOf(1), (CellPos{0, 3}));
    // Back from cells: the right half of 中 is 中, a blank after the text none.
    QCOMPARE(l.indexAt({0, 2}), qsizetype(1));
    QCOMPARE(l.indexAt({0, 3}), qsizetype(2));
    QCOMPARE(l.indexAt({0, 6}), qsizetype(6));
    QCOMPARE(l.indexAt({0, 10}), qsizetype(-1));
    QCOMPARE(l.indexAt({1, 0}), qsizetype(-1));
}

void TestTermText::logicalLineOutOfRange()
{
    Terminal t(10, 3, 100);
    const LogicalLine below = logicalLine(t.screen(), 7, true);
    QVERIFY(below.text.isEmpty());
    QCOMPARE(below.first, 7);
    const LogicalLine above = logicalLine(t.screen(), -1, true);
    QVERIFY(above.text.isEmpty());
}

void TestTermText::findInScrollback()
{
    Terminal t(40, 4, 1000);
    for (int i = 0; i < 30; ++i)
        t.feed(QByteArray(QByteArray("line ") + QByteArray::number(i) + (i % 10 == 0 ? " error here" : "") + "\r\n"));
    const Screen &s = t.screen();
    QVERIFY(s.historySize() > 20);
    const QList<TextRange> found = findText(s, u"error", Qt::CaseInsensitive, 100);
    QCOMPARE(found.size(), 3);
    // Oldest first; each in the line that holds it.
    for (int i = 0; i < found.size(); ++i) {
        const TextRange &r = found.at(i);
        QCOMPARE(r.start.line, r.end.line);
        QCOMPARE(s.lineAt(r.start.line).trimmedText(), u"line %1 error here"_s.arg(i * 10));
        QCOMPARE(r.start.column, u"line %1 "_s.arg(i * 10).size());
        QCOMPARE(r.end.column - r.start.column, 5);
        if (i > 0)
            QVERIFY(found.at(i - 1).start.line < r.start.line);
    }
    QVERIFY(found.first().start.line < 0);
}

void TestTermText::findCaseAndPolish_data()
{
    QTest::addColumn<QString>("needle");
    QTest::addColumn<bool>("caseSensitive");
    QTest::addColumn<int>("count");
    QTest::newRow("insensitive") << u"zażółć"_s << false << 2;
    QTest::newRow("insensitive upper") << u"ZAŻÓŁĆ"_s << false << 2;
    QTest::newRow("sensitive") << u"Zażółć"_s << true << 1;
    QTest::newRow("sensitive none") << u"ZAŻÓŁĆ"_s << true << 0;
    QTest::newRow("diacritics matter") << u"zazolc"_s << false << 0;
    QTest::newRow("across a space") << u"gęślą jaźń"_s << false << 2;
}

void TestTermText::findCaseAndPolish()
{
    QFETCH(QString, needle);
    QFETCH(bool, caseSensitive);
    QFETCH(int, count);
    Terminal t(40, 6, 100);
    t.feed("Zażółć gęślą jaźń\r\nzażółć GĘŚLĄ JAŹŃ\r\n");
    const QList<TextRange> found =
            findText(t.screen(), needle, caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive, 100);
    QCOMPARE(found.size(), count);
}

void TestTermText::findAcrossWrappedRows()
{
    Terminal t(10, 4, 100);
    t.feed("0123456789abcdef\r\n");
    const QList<TextRange> found = findText(t.screen(), u"89ab", Qt::CaseSensitive, 10);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().start, (CellPos{0, 8}));
    QCOMPARE(found.first().end, (CellPos{1, 2}));
    // Two lines of text are never one match.
    t.feed("end\r\nstart\r\n");
    QCOMPARE(findText(t.screen(), u"endstart", Qt::CaseSensitive, 10).size(), 0);
}

void TestTermText::findLimitAndEmpty()
{
    Terminal t(80, 4, 100);
    t.feed("aaaaaaaaaa\r\n");
    QCOMPARE(findText(t.screen(), u"a", Qt::CaseSensitive, 3).size(), 3);
    // Not overlapping.
    QCOMPARE(findText(t.screen(), u"aa", Qt::CaseSensitive, 100).size(), 5);
    QCOMPARE(findText(t.screen(), u"", Qt::CaseSensitive, 100).size(), 0);
    QCOMPARE(findText(t.screen(), u"a", Qt::CaseSensitive, 0).size(), 0);
}

void TestTermText::clearKeepsPromptRow()
{
    Terminal t(20, 5, 100);
    for (int i = 0; i < 8; ++i)
        t.feed(QByteArray(QByteArray("out ") + QByteArray::number(i) + "\r\n"));
    t.feed("$ ls -l");
    Screen &s = t.screen();
    QVERIFY(s.historySize() > 0);
    const int column = s.cursorColumn();
    s.clearDirty();
    s.clearKeepingCursorLine();
    QCOMPARE(s.historySize(), 0);
    QCOMPARE(rowsOf(s), QStringList{u"$ ls -l"_s});
    QCOMPARE(s.cursorRow(), 0);
    QCOMPARE(s.cursorColumn(), column);
    QVERIFY(s.isRowDirty(0) && s.isRowDirty(4));
    // Typing goes on where it was.
    t.feed("a\r\nnext");
    QCOMPARE(rowsOf(s), (QStringList{u"$ ls -la"_s, u"next"_s}));
    // At the top already: only the scrollback and the rows under the cursor.
    t.feed("\x1b[H");
    s.clearKeepingCursorLine();
    QCOMPARE(rowsOf(s), QStringList{u"$ ls -la"_s});
}

void TestTermText::clearOnAlternateScreen()
{
    Terminal t(20, 4, 100);
    for (int i = 0; i < 8; ++i)
        t.feed("history\r\n");
    t.feed("\x1b[?1049h\x1b[Hfull screen");
    t.screen().clearKeepingCursorLine();
    // The program's screen stays; the main screen's scrollback is gone once back.
    QCOMPARE(rowsOf(t.screen()), QStringList{u"full screen"_s});
    t.feed("\x1b[?1049l");
    QCOMPARE(t.screen().historySize(), 0);
}

void TestTermText::links_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("matched");
    QTest::addColumn<int>("kind");
    QTest::addColumn<QString>("target");
    QTest::addColumn<int>("line");
    QTest::addColumn<int>("column");
    const int url = TextLink::Url;
    const int path = TextLink::Path;
    QTest::newRow("https") << u"see https://example.com/a?b=1&c=ż#x now"_s << u"https://example.com/a?b=1&c=ż#x"_s << url
                           << u"https://example.com/a?b=1&c=ż#x"_s << 0 << 0;
    QTest::newRow("http upper") << u"HTTP://example.com"_s << u"HTTP://example.com"_s << url << u"HTTP://example.com"_s
                                << 0 << 0;
    QTest::newRow("file url") << u"open file:///home/me/x.txt"_s << u"file:///home/me/x.txt"_s << url
                              << u"file:///home/me/x.txt"_s << 0 << 0;
    QTest::newRow("in angle brackets") << u"<https://example.com/x>"_s << u"https://example.com/x"_s << url
                                       << u"https://example.com/x"_s << 0 << 0;
    QTest::newRow("gcc") << u"src/main.cpp:12:5: error: expected ';'"_s << u"src/main.cpp:12:5"_s << path
                         << u"src/main.cpp"_s << 12 << 5;
    QTest::newRow("grep -n") << u"src/a.h:7:#include <x>"_s << u"src/a.h:7"_s << path << u"src/a.h"_s << 7 << 0;
    QTest::newRow("rust arrow") << u"  --> src/main.rs:10:5"_s << u"src/main.rs:10:5"_s << path << u"src/main.rs"_s
                                << 10 << 5;
    QTest::newRow("typescript") << u"src/app.ts(10,5): error TS2322"_s << u"src/app.ts(10,5)"_s << path
                                << u"src/app.ts"_s << 10 << 5;
    QTest::newRow("msvc spaced") << u"x.cpp(3, 14): warning"_s << u"x.cpp(3, 14)"_s << path << u"x.cpp"_s << 3 << 14;
    QTest::newRow("paren line") << u"main.c(42) : error"_s << u"main.c(42)"_s << path << u"main.c"_s << 42 << 0;
    QTest::newRow("python") << u"  File \"tools/run.py\", line 12, in main"_s << u"tools/run.py\", line 12"_s << path
                            << u"tools/run.py"_s << 12 << 0;
    QTest::newRow("absolute") << u"wrote /tmp/out.log"_s << u"/tmp/out.log"_s << path << u"/tmp/out.log"_s << 0 << 0;
    QTest::newRow("home") << u"cat ~/notes/todo.md"_s << u"~/notes/todo.md"_s << path << u"~/notes/todo.md"_s << 0 << 0;
    QTest::newRow("dot slash") << u"run ./build/app now"_s << u"./build/app"_s << path << u"./build/app"_s << 0 << 0;
    QTest::newRow("parent") << u"../lib/x.js:3"_s << u"../lib/x.js:3"_s << path << u"../lib/x.js"_s << 3 << 0;
    QTest::newRow("sentence end") << u"See README.md."_s << u"README.md"_s << path << u"README.md"_s << 0 << 0;
    QTest::newRow("quoted") << u"'docs/CODE.md'"_s << u"docs/CODE.md"_s << path << u"docs/CODE.md"_s << 0 << 0;
    QTest::newRow("polish name") << u"notatki/zażółć.md:2"_s << u"notatki/zażółć.md:2"_s << path
                                 << u"notatki/zażółć.md"_s << 2 << 0;
    QTest::newRow("diff name") << u"+++ b/src/x.h"_s << u"b/src/x.h"_s << path << u"b/src/x.h"_s << 0 << 0;
    QTest::newRow("scoped package") << u"node_modules/@types/node/index.d.ts"_s
                                    << u"node_modules/@types/node/index.d.ts"_s << path
                                    << u"node_modules/@types/node/index.d.ts"_s << 0 << 0;
    QTest::newRow("colon not a line") << u"x.py:abc"_s << u"x.py"_s << path << u"x.py"_s << 0 << 0;
    QTest::newRow("line zero") << u"x.py:0"_s << u"x.py"_s << path << u"x.py"_s << 0 << 0;
}

void TestTermText::links()
{
    QFETCH(QString, text);
    QFETCH(QString, matched);
    QFETCH(int, kind);
    QFETCH(QString, target);
    QFETCH(int, line);
    QFETCH(int, column);
    const QList<TextLink> found = findLinks(text);
    QVERIFY2(!found.isEmpty(), qPrintable(text));
    const TextLink &link = found.first();
    QCOMPARE(linkText(text, link), matched);
    QCOMPARE(int(link.kind), kind);
    QCOMPARE(link.target, target);
    QCOMPARE(link.line, line);
    QCOMPARE(link.column, column);
}

void TestTermText::urlPunctuation_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("url");
    QTest::newRow("full stop") << u"Go to https://example.com."_s << u"https://example.com"_s;
    QTest::newRow("comma") << u"https://a.pl/x, then"_s << u"https://a.pl/x"_s;
    QTest::newRow("question") << u"Is it https://a.pl/x?"_s << u"https://a.pl/x"_s;
    QTest::newRow("in parentheses") << u"(see https://a.pl/x)"_s << u"https://a.pl/x"_s;
    QTest::newRow("own parentheses") << u"https://en.wikipedia.org/wiki/Rust_(language)"_s
                                     << u"https://en.wikipedia.org/wiki/Rust_(language)"_s;
    QTest::newRow("markdown") << u"[x](https://a.pl/y)"_s << u"https://a.pl/y"_s;
    QTest::newRow("quotes") << u"\"https://a.pl/q\""_s << u"https://a.pl/q"_s;
    QTest::newRow("backtick") << u"`https://a.pl/q`"_s << u"https://a.pl/q"_s;
    QTest::newRow("bracketed") << u"[https://a.pl/q]"_s << u"https://a.pl/q"_s;
}

void TestTermText::urlPunctuation()
{
    QFETCH(QString, text);
    QFETCH(QString, url);
    const QList<TextLink> found = findLinks(text);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().kind, TextLink::Url);
    QCOMPARE(found.first().target, url);
}

void TestTermText::noLinks_data()
{
    QTest::addColumn<QString>("text");
    QTest::newRow("words") << u"zażółć gęślą jaźń and more"_s;
    QTest::newRow("version") << u"version 3.14.2 and 1.5"_s;
    QTest::newRow("scheme only") << u"https:// nothing"_s;
    QTest::newRow("slashes") << u"a // b / c ... ."_s;
    QTest::newRow("home alone") << u"cd ~/ and ~"_s;
    QTest::newRow("mid-word scheme") << u"xhttps://a.pl"_s;
    QTest::newRow("empty") << QString();
    QTest::newRow("long extension") << u"name.verylongextension1"_s;
}

void TestTermText::noLinks()
{
    QFETCH(QString, text);
    const QList<TextLink> found = findLinks(text);
    QVERIFY2(found.isEmpty(), qPrintable(found.isEmpty() ? QString() : linkText(text, found.first())));
}

void TestTermText::linkAtIndex()
{
    const QString text = u"a.cpp:1 and https://x.pl"_s;
    const QList<TextLink> links = findLinks(text);
    QCOMPARE(links.size(), 2);
    QCOMPARE(linkAt(links, 0), qsizetype(0));
    QCOMPARE(linkAt(links, 6), qsizetype(0));
    QCOMPARE(linkAt(links, 7), qsizetype(-1));
    QCOMPARE(linkAt(links, 12), qsizetype(1));
    QCOMPARE(linkAt(links, text.size()), qsizetype(-1));
}

void TestTermText::resolvePaths()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString root = QDir::cleanPath(dir.path());
    QVERIFY(touch(root + u"/project/src/main.cpp"_s));
    QVERIFY(touch(root + u"/home/notatki/zażółć.md"_s));
    QVERIFY(QDir().mkpath(root + u"/project/folder"_s));
    const QString project = root + u"/project"_s;
    const QString home = root + u"/home"_s;
    QCOMPARE(resolvePath(u"src/main.cpp"_s, project, home), project + u"/src/main.cpp"_s);
    QCOMPARE(resolvePath(u"./src/../src/main.cpp"_s, project, home), project + u"/src/main.cpp"_s);
    QCOMPARE(resolvePath(project + u"/src/main.cpp"_s, QString(), home), project + u"/src/main.cpp"_s);
    QCOMPARE(resolvePath(u"~/notatki/zażółć.md"_s, project, home), home + u"/notatki/zażółć.md"_s);
    QCOMPARE(resolvePath(u"a/src/main.cpp"_s, project, home), project + u"/src/main.cpp"_s);
    QCOMPARE(resolvePath(u"b/src/main.cpp"_s, project, home), project + u"/src/main.cpp"_s);
    // Nothing there, a folder, no working directory, a URL.
    QCOMPARE(resolvePath(u"src/other.cpp"_s, project, home), QString());
    QCOMPARE(resolvePath(u"folder"_s, project, home), QString());
    QCOMPARE(resolvePath(u"src/main.cpp"_s, QString(), home), QString());
    QCOMPARE(resolvePath(u"src/main.cpp"_s, u"relative/dir"_s, home), QString());
    QCOMPARE(resolvePath(u"~/notatki/zażółć.md"_s, project, QString()), QString());
    QCOMPARE(resolvePath(u"file:///etc/passwd"_s, project, home), QString());
    QCOMPARE(resolvePath(QString(), project, home), QString());
}

void TestTermText::hostileLineStaysCheap()
{
    // A line of 1000 columns full of path-like and URL-like noise: found in one pass, bounded.
    QString text;
    for (int i = 0; i < 200; ++i)
        text += u"a/b. https://(((( x.y:9:"_s;
    QElapsedTimer timer;
    timer.start();
    const QList<TextLink> found = findLinks(text);
    QVERIFY(!found.isEmpty());
    QVERIFY(found.size() <= text.size());
    QVERIFY2(timer.elapsed() < 2000, "findLinks took too long");
    for (const TextLink &link : found)
        QVERIFY(link.start >= 0 && link.length > 0 && link.start + link.length <= text.size());
}

QTEST_GUILESS_MAIN(TestTermText)
#include "tst_term_text.moc"
