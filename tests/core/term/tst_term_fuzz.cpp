// The terminal emulator under garbage (src/core/term): 10 MB of random bytes, and 10 MB of
// random escape sequences (CSI with random parameters and finals, DEC private modes, OSC strings,
// UTF-8 and broken UTF-8, wide characters and combining marks), fed in random chunks with random
// resizes in between, from a fixed seed. Nothing may crash or hang, and the screen must stay
// consistent: every row as wide as the screen, the cursor on the screen, no orphaned wide halves.
#include "term/term.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

using namespace brulion::term;

namespace {

constexpr qsizetype FuzzBytes = 10 * 1024 * 1024;

void checkInvariants(const Terminal &t)
{
    const Screen &s = t.screen();
    QVERIFY(s.columns() >= 1);
    QVERIFY(s.rows() >= 1);
    QVERIFY(s.cursorRow() >= 0 && s.cursorRow() < s.rows());
    QVERIFY(s.cursorColumn() >= 0 && s.cursorColumn() < s.columns());
    QVERIFY(s.scrollTop() >= 0 && s.scrollTop() < s.scrollBottom() + 1 && s.scrollBottom() < s.rows());
    QVERIFY(s.historySize() <= s.scrollbackLimit());
    for (int row = 0; row < s.rows(); ++row) {
        const Line &line = s.line(row);
        QCOMPARE(line.cells.size(), s.columns());
        for (int column = 0; column < s.columns(); ++column) {
            const Cell &cell = line.cells.at(column);
            if (cell.isWide()) {
                QVERIFY2(column + 1 < s.columns() && line.cells.at(column + 1).isWideTail(),
                         "a wide head without its tail");
            }
            if (cell.isWideTail()) {
                QVERIFY2(column > 0 && line.cells.at(column - 1).isWide(), "a wide tail without its head");
            }
            if (cell.attrs & ClusterFlag) {
                QVERIFY(line.clusters.contains(column));
                QVERIFY(line.clusters.value(column).size() <= 32);
            }
        }
        for (auto it = line.clusters.cbegin(); it != line.clusters.cend(); ++it) {
            QVERIFY(it.key() >= 0 && it.key() < s.columns());
            QVERIFY(line.cells.at(it.key()).attrs & ClusterFlag);
        }
    }
}

/// A random sequence-shaped chunk.
QByteArray sequence(QRandomGenerator &random)
{
    static const char finals[] = "@ABCDEFGHIJKLMPSTXZ`abcdefghlmnqrstu";
    static const char escFinals[] = "78DEHMcZ=>\\NO";
    static const int modes[] = {1, 5, 6, 7, 9, 12, 25, 47, 1000, 1002, 1003, 1004, 1006, 1047, 1048, 1049, 2004, 2026};
    static const char *const texts[] = {"za\xc5\xbc\xc3\xb3\xc5\x82\xc4\x87",
                                        "\xe4\xb8\xad\xe6\x96\x87",
                                        "\xf0\x9f\x98\x80",
                                        "e\xcc\x81",
                                        "\xe2\x80\x8d",
                                        "\xef\xb8\x8f",
                                        "\xc5",
                                        "\xff\xfe",
                                        "\xed\xa0\x80",
                                        "\r\n",
                                        "\t",
                                        "\b",
                                        "\x0e",
                                        "\x0f",
                                        "\x1b(0",
                                        "\x1b(B",
                                        "\x1b#8"};
    QByteArray out;
    switch (random.bounded(9)) {
    case 0: { // CSI with parameters
        out = "\x1b[";
        const int prefix = random.bounded(6);
        if (prefix < 4)
            out += "?><="[prefix];
        const int count = random.bounded(6);
        for (int i = 0; i < count; ++i) {
            if (i > 0)
                out += random.bounded(5) == 0 ? ':' : ';';
            if (random.bounded(4) != 0)
                out += QByteArray::number(random.bounded(random.bounded(2) == 0 ? 10 : 100000));
        }
        if (random.bounded(8) == 0)
            out += " !$"[random.bounded(3)];
        out += finals[random.bounded(int(sizeof(finals)) - 1)];
        break;
    }
    case 1: // DEC private modes
        out = "\x1b[?" + QByteArray::number(modes[random.bounded(int(std::size(modes)))])
              + (random.bounded(2) ? 'h' : 'l');
        break;
    case 2: // ESC sequences
        out = "\x1b";
        out += escFinals[random.bounded(int(sizeof(escFinals)) - 1)];
        break;
    case 3: { // OSC
        static const char *const heads[] = {
                "0;", "2;", "7;file:///tmp/", "8;;https://example.com/", "8;;", "52;c;", "10;?", "11;?", "4;1;", ""};
        out = "\x1b]";
        out += heads[random.bounded(int(std::size(heads)))];
        const int n = random.bounded(40);
        for (int i = 0; i < n; ++i)
            out += char(random.bounded(0x20, 0x100));
        out += random.bounded(3) == 0 ? "\x1b\\" : "\x07";
        break;
    }
    case 4: // SGR
        out = "\x1b[" + QByteArray::number(random.bounded(110)) + ";38;" + (random.bounded(2) ? "5;" : "2;1;2;")
              + QByteArray::number(random.bounded(300)) + 'm';
        break;
    case 5:
    case 6: out = texts[random.bounded(int(std::size(texts)))]; break;
    case 7: { // DCS, APC
        out = random.bounded(2) ? "\x1bP+q" : "\x1b_G";
        const int n = random.bounded(30);
        for (int i = 0; i < n; ++i)
            out += char(random.bounded(0x20, 0x7f));
        out += "\x1b\\";
        break;
    }
    default: { // printable text
        const int n = random.bounded(1, 120);
        for (int i = 0; i < n; ++i)
            out += char(random.bounded(0x20, 0x7f));
        break;
    }
    }
    return out;
}

/// Feeds `data` in random chunks with a resize now and then; checks the invariants as it goes.
void feedInChunks(Terminal &t, const QByteArray &data, QRandomGenerator &random)
{
    qsizetype at = 0;
    int chunks = 0;
    while (at < data.size()) {
        const qsizetype n = std::min<qsizetype>(data.size() - at, random.bounded(1, 8192));
        t.feed(QByteArrayView(data.constData() + at, n));
        at += n;
        if (++chunks % 64 == 0) {
            t.resize(random.bounded(1, 200), random.bounded(1, 80));
            checkInvariants(t);
            if (QTest::currentTestFailed())
                return;
        }
        if (chunks % 512 == 0)
            t.screen().clearDirty();
    }
    checkInvariants(t);
}

} // namespace

class TestTermFuzz : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void randomBytes();
    void randomSequences();
    void floodOfLines();
};

void TestTermFuzz::randomBytes()
{
    QRandomGenerator random(20261125);
    QByteArray data(FuzzBytes, Qt::Uninitialized);
    for (qsizetype i = 0; i < data.size(); ++i)
        data[i] = char(random.bounded(256));
    Terminal t(80, 24, 1000);
    QByteArray replies;
    connect(&t, &Terminal::reply, this, [&replies](const QByteArray &r) {
        if (replies.size() < 1 << 20)
            replies += r;
    });
    QElapsedTimer timer;
    timer.start();
    feedInChunks(t, data, random);
    qInfo("10 MB of random bytes in %lld ms", qint64(timer.elapsed()));
}

void TestTermFuzz::randomSequences()
{
    QRandomGenerator random(42);
    QByteArray data;
    data.reserve(FuzzBytes + 1024);
    while (data.size() < FuzzBytes)
        data += sequence(random);
    Terminal t(120, 40, 2000);
    QElapsedTimer timer;
    timer.start();
    feedInChunks(t, data, random);
    qInfo("10 MB of random sequences in %lld ms", qint64(timer.elapsed()));
    // Still a working terminal afterwards.
    t.feed("\x1b"
           "c\x1b[?1049l\x1b[H\x1b[2Jzażółć gęślą jaźń");
    QCOMPARE(t.screen().line(0).trimmedText(), QStringLiteral("zażółć gęślą jaźń"));
}

void TestTermFuzz::floodOfLines()
{
    // `yes | head -100000`: the scrollback keeps its limit, the screen its last lines.
    Terminal t(80, 24, 5000);
    QByteArray data;
    for (int i = 0; i < 100000; ++i)
        data += "y\r\n";
    QElapsedTimer timer;
    timer.start();
    t.feed(data);
    qInfo("100000 lines in %lld ms", qint64(timer.elapsed()));
    QCOMPARE(t.screen().historySize(), 5000);
    QCOMPARE(t.screen().line(22).trimmedText(), QStringLiteral("y"));
    QCOMPARE(t.screen().line(23).trimmedText(), QString());
    checkInvariants(t);
}

QTEST_GUILESS_MAIN(TestTermFuzz)
#include "tst_term_fuzz.moc"
