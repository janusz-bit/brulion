// Log entries (log/entry.h): levels, the line format and parsing it back (Polish text, tabs and
// newlines in messages, CRLF, a BOM, truncated and garbage lines), truncation; the categories'
// UI groups; the size and duration texts of slowscope.h.
#include "log/categories.h"
#include "log/entry.h"
#include "log/slowscope.h"

#include <QDateTime>
#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion::logging;

class TestLogEntry : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void levels();
    void roundTrip_data();
    void roundTrip();
    void repeatLine();
    void parseRobust();
    void truncation();
    void groups_data();
    void groups();
    void formats_data();
    void formats();
};

namespace {

qint64 at(const char *local)
{
    return QDateTime::fromString(QString::fromLatin1(local), u"yyyy-MM-dd HH:mm:ss.zzz"_s).toMSecsSinceEpoch();
}

} // namespace

void TestLogEntry::levels()
{
    QCOMPARE(levelFromQt(QtDebugMsg), Level::Debug);
    QCOMPARE(levelFromQt(QtInfoMsg), Level::Info);
    QCOMPARE(levelFromQt(QtWarningMsg), Level::Warning);
    QCOMPARE(levelFromQt(QtCriticalMsg), Level::Error);
    QCOMPARE(levelFromQt(QtFatalMsg), Level::Fatal);
    for (Level l : {Level::Debug, Level::Info, Level::Warning, Level::Error, Level::Fatal}) {
        QCOMPARE(levelFromLetter(levelLetter(l)), l);
        QCOMPARE(levelFromName(levelName(l)), l);
    }
    QCOMPARE(levelFromLetter(u'X'), std::nullopt);
    QCOMPARE(levelFromName(u"WARNING"), Level::Warning);
    QCOMPARE(levelFromName(u"nope"), std::nullopt);
}

void TestLogEntry::roundTrip_data()
{
    QTest::addColumn<QString>("category");
    QTest::addColumn<QString>("message");
    QTest::addColumn<int>("level");
    QTest::newRow("polish") << u"brulion.sync"_s << u"#42 PUT Dziennik/Zażółć gęślą jaźń.md failed: HTTP 423"_s
                            << int(Level::Warning);
    QTest::newRow("tabs") << u"qml"_s << u"a\tb\tc"_s << int(Level::Info);
    QTest::newRow("multi-line") << u"default"_s << u"pierwsza linia\ndruga\n\ttrzecia z tabem"_s << int(Level::Error);
    QTest::newRow("empty message") << u"brulion.app"_s << QString() << int(Level::Debug);
    QTest::newRow("looks like a header") << u"brulion.app"_s << u"x\n2026-10-18 14:03:22.517\tI\tfake\tno"_s
                                         << int(Level::Info);
}

void TestLogEntry::roundTrip()
{
    QFETCH(QString, category);
    QFETCH(QString, message);
    QFETCH(int, level);
    Entry e;
    e.time = at("2026-10-18 14:03:22.517");
    e.level = Level(level);
    e.category = category;
    e.message = message;
    const QString line = formatLine(e);
    QVERIFY(line.startsWith(u"2026-10-18 14:03:22.517\t"_s));
    const QList<Entry> parsed = parseLines(QString(line + u"\n"_s));
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first(), e);
}

void TestLogEntry::repeatLine()
{
    Entry e{7,  at("2026-10-18 14:03:22.517"), Level::Warning, u"brulion.sync"_s, u"Brak sieci"_s,
            37, at("2026-10-18 14:05:01.002")};
    const QString line = formatLine(e);
    QCOMPARE(line, u"2026-10-18 14:03:22.517\tW\tbrulion.sync\tBrak sieci\n"
                   u"2026-10-18 14:05:01.002\tW\tbrulion.sync\t(previous message repeated 36 more times)"_s);
    const QList<Entry> parsed = parseLines(line);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed.first().repeat, 37);
    QCOMPARE(parsed.first().lastTime, e.lastTime);
    QCOMPARE(formatRepeatLine(Entry{}), QString());
}

void TestLogEntry::parseRobust()
{
    const QString text = QChar(0xFEFF)
                         + u"garbage before\r\n"
                           u"2026-10-18 14:03:22.517\tI\tbrulion.app\tstart\r\n"
                           u"\tcontinued\r\n"
                           u"not a header line\n"
                           u"2026-13-45 99:99:99.999\tI\tbad\ttime\n"
                           u"\n"
                           u"2026-10-18 14:03:23.000\tQ\tbrulion.app\tbad level\n"
                           u"2026-10-18 14:03:24.000\tW\tbrulion.sync\ttruncated la"_s;
    const QList<Entry> entries = parseLines(text);
    QCOMPARE(entries.size(), 3);
    QCOMPARE(entries.at(0).category, u"log"_s);
    QCOMPARE(entries.at(0).message, u"garbage before"_s);
    QCOMPARE(entries.at(1).message, u"start\ncontinued\nnot a header line\n2026-13-45 99:99:99.999\tI\tbad\ttime\n"
                                    u"2026-10-18 14:03:23.000\tQ\tbrulion.app\tbad level"_s);
    QCOMPARE(entries.at(2).level, Level::Warning);
    QCOMPARE(entries.at(2).message, u"truncated la"_s);
    QVERIFY(parseLines(u""_s).isEmpty());
    QCOMPARE(parseLines(QString::fromUtf8("\xff\xfe broken \xc5")).size(), 1); // invalid UTF-8 -> U+FFFD
}

void TestLogEntry::truncation()
{
    const QString shortText = u"krótki"_s;
    QCOMPARE(truncateMessage(shortText), shortText);
    QString longText(kMaxMessageChars + 10, u'ż');
    const QString cut = truncateMessage(longText);
    QVERIFY(cut.startsWith(QString(kMaxMessageChars, u'ż')));
    QVERIFY(cut.endsWith(u"… [+10 chars]"_s));
    // Never between the halves of a surrogate pair.
    QString emoji = QString(kMaxMessageChars - 1, u'a') + u"😀😀"_s;
    const QString cutEmoji = truncateMessage(emoji);
    QCOMPARE(cutEmoji.at(kMaxMessageChars - 1), QChar(u'…'));
    QVERIFY(cutEmoji.endsWith(u"… [+4 chars]"_s));
}

void TestLogEntry::groups_data()
{
    QTest::addColumn<QString>("category");
    QTest::addColumn<QString>("group");
    QTest::newRow("sync") << u"brulion.sync"_s << u"sync"_s;
    QTest::newRow("sync sub") << u"brulion.sync.webdav"_s << u"sync"_s;
    QTest::newRow("net") << u"brulion.net"_s << u"net"_s;
    QTest::newRow("qt network") << u"qt.network.ssl"_s << u"net"_s;
    QTest::newRow("vault") << u"brulion.vault"_s << u"vault"_s;
    QTest::newRow("workspace") << u"brulion.workspace"_s << u"vault"_s;
    QTest::newRow("index") << u"brulion.index"_s << u"index"_s;
    QTest::newRow("reminders") << u"brulion.reminders"_s << u"reminders"_s;
    QTest::newRow("notify") << u"brulion.notify.android"_s << u"reminders"_s;
    QTest::newRow("io") << u"brulion.io"_s << u"io"_s;
    QTest::newRow("app") << u"brulion.app"_s << u"app"_s;
    QTest::newRow("android") << u"brulion.android"_s << u"app"_s;
    QTest::newRow("not a prefix") << u"brulion.syncthingx"_s << u"app"_s;
    QTest::newRow("qml") << u"qml"_s << u"system"_s;
    QTest::newRow("default") << u"default"_s << u"system"_s;
    QTest::newRow("brulionx") << u"brulionx"_s << u"system"_s;
}

void TestLogEntry::groups()
{
    QFETCH(QString, category);
    QFETCH(QString, group);
    QCOMPARE(categoryGroup(category), group);
    QVERIFY(categoryGroups().contains(group));
    QCOMPARE(lcSync().categoryName(), "brulion.sync");
    QCOMPARE(lcNet().categoryName(), "brulion.net");
    QVERIFY(!verbosityCategories().contains(u"brulion.touchflick"_s));
}

void TestLogEntry::formats_data()
{
    QTest::addColumn<qint64>("value");
    QTest::addColumn<QString>("bytes");
    QTest::addColumn<QString>("ms");
    QTest::newRow("0") << qint64(0) << u"0 B"_s << u"0 ms"_s;
    QTest::newRow("negative") << qint64(-5) << u"0 B"_s << u"0 ms"_s;
    QTest::newRow("312") << qint64(312) << u"312 B"_s << u"312 ms"_s;
    QTest::newRow("14200") << qint64(14200) << u"13.9 KB"_s << u"14.2 s"_s;
    QTest::newRow("185000") << qint64(185000) << u"180.7 KB"_s << u"3 min 05 s"_s;
    QTest::newRow("3720000") << qint64(3720000) << u"3.5 MB"_s << u"1 h 02 min"_s;
    QTest::newRow("2GB") << qint64(2LL * 1024 * 1024 * 1024) << u"2.0 GB"_s << u"596 h 31 min"_s;
}

void TestLogEntry::formats()
{
    QFETCH(qint64, value);
    QFETCH(QString, bytes);
    QFETCH(QString, ms);
    QCOMPARE(formatBytes(value), bytes);
    QCOMPARE(formatMs(value), ms);
}

QTEST_GUILESS_MAIN(TestLogEntry)
#include "tst_log_entry.moc"
