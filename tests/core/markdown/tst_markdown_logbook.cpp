// Time tracking and status history in Logseq's :LOGBOOK: drawer (markdown/logbook.h): reading
// CLOCK and State lines (malformed ones, overlaps, daylight-saving changes in local time), the
// exact format written, and the file edits (byte for byte elsewhere, CRLF, Polish text).
#include "markdown/edit.h"
#include "markdown/logbook.h"
#include "markdown/parse.h"

#include "centraleuropeantime.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::markdown;

namespace {

QDateTime at(int y, int mo, int d, int h, int mi, int s = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s));
}

} // namespace

class TstMarkdownLogbook : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Polish daylight-saving rules without a time zone database.
        brulion::test::useCentralEuropeanTime("CET-1CEST,M3.5.0,M10.5.0/3");
        const QString problem = brulion::test::centralEuropeanTimeProblem();
        if (!problem.isEmpty())
            QSKIP(qPrintable(problem));
    }

    void read_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<int>("clocks");
        QTest::addColumn<qint64>("seconds"); // clockedSeconds()
        QTest::addColumn<bool>("running");
        QTest::addColumn<int>("changes");
        QTest::addColumn<int>("malformed");

        QTest::newRow("logseq")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n:END:"_s << 1
                << qint64(25 * 60) << false << 0 << 0;
        QTest::newRow("running") << u"  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 10:00:00]\n  :END:"_s << 1 << qint64(0)
                                 << true << 0 << 0;
        QTest::newRow("two entries and a running one")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 08:00:00]--[2026-11-03 Tue 09:00:00] =>  01:00:00\n"
                   "CLOCK: [2026-11-03 Tue 09:30:00]--[2026-11-03 Tue 09:45:30] =>  00:15:30\n"
                   "CLOCK: [2026-11-03 Tue 10:00:00]\n:END:"_s
                << 3 << qint64(3600 + 15 * 60 + 30) << true << 0 << 0;
        QTest::newRow("org mode, no seconds, Polish day")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 wt. 10:00]--[2026-11-03 wt. 11:30] =>  1:30\n:END:"_s << 1
                << qint64(90 * 60) << false << 0 << 0;
        QTest::newRow("no day name") << u":LOGBOOK:\nCLOCK: [2026-11-03 10:00:00]--[2026-11-03 10:10:00]\n:END:"_s << 1
                                     << qint64(600) << false << 0 << 0;
        QTest::newRow("overlapping clocks count once")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 11:00:00] =>  01:00:00\n"
                   "CLOCK: [2026-11-03 Tue 10:30:00]--[2026-11-03 Tue 11:30:00] =>  01:00:00\n"
                   "CLOCK: [2026-11-03 Tue 10:40:00]--[2026-11-03 Tue 10:50:00] =>  00:10:00\n:END:"_s
                << 3 << qint64(90 * 60) << false << 0 << 0;
        QTest::newRow("the same clock twice")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]\n"
                   "CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]\n:END:"_s
                << 2 << qint64(20 * 60) << false << 0 << 0;
        QTest::newRow("bad date") << u":LOGBOOK:\nCLOCK: [2026-13-45 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]\n:END:"_s
                                  << 0 << qint64(0) << false << 0 << 1;
        QTest::newRow("bad time") << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 25:00:00]\n:END:"_s << 0 << qint64(0) << false
                                  << 0 << 1;
        QTest::newRow("garbage after the clock")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00] zażółć\n:END:"_s << 0 << qint64(0) << false << 0 << 1;
        QTest::newRow("unclosed bracket")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00\n:END:"_s << 0 << qint64(0) << false << 0 << 1;
        QTest::newRow("end before start, no duration")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 11:00:00]--[2026-11-03 Tue 10:00:00]\n:END:"_s << 0 << qint64(0)
                << false << 0 << 1;
        QTest::newRow("end before start, written duration")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 11:00:00]--[2026-11-03 Tue 10:00:00] =>  00:05:00\n:END:"_s << 1
                << qint64(300) << false << 0 << 0;
        QTest::newRow("two running clocks: the last runs")
                << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 08:00:00]\nCLOCK: [2026-11-03 Tue 10:00:00]\n:END:"_s << 1
                << qint64(0) << true << 0 << 1;
        QTest::newRow("outside a drawer") << u"CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]"_s << 0
                                          << qint64(0) << false << 0 << 0;
        QTest::newRow("another drawer")
                << u":PROPERTIES:\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]\n:END:"_s << 0
                << qint64(0) << false << 0 << 0;
        QTest::newRow("unclosed drawer") << u":LOGBOOK:\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00]"_s
                                         << 1 << qint64(20 * 60) << false << 0 << 0;
        QTest::newRow("CRLF")
                << u":LOGBOOK:\r\nCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:20:00] =>  00:20:00\r\n:END:\r\n"_s
                << 1 << qint64(20 * 60) << false << 0 << 0;
        QTest::newRow("status history")
                << u":LOGBOOK:\n- State \"DONE\"       from \"DOING\"      [2026-11-03 Tue 10:25]\n"
                   "- State \"DOING\"      from \"TODO\"       [2026-11-03 Tue 10:00]\n- State \"TODO\"       [2026-11-02 Mon 09:00]\n:END:"_s
                << 0 << qint64(0) << false << 3 << 0;
        QTest::newRow("status history as Brulion writes it")
                << u":LOGBOOK:\nState \"DONE\"       from \"DOING\"      [2026-11-03 Tue 10:25]\n:END:"_s << 0
                << qint64(0) << false << 1 << 0;
        QTest::newRow("bad status line") << u":LOGBOOK:\n- State \"DONE\" from \"TODO\" [wczoraj]\n:END:"_s << 0
                                         << qint64(0) << false << 0 << 1;
        QTest::newRow("notes are no entries")
                << u":LOGBOOK:\n- Notatka: zadzwonić do Łukasza\n:END:"_s << 0 << qint64(0) << false << 0 << 0;
        QTest::newRow("empty") << QString() << 0 << qint64(0) << false << 0 << 0;
    }
    void read()
    {
        QFETCH(QString, text);
        QFETCH(int, clocks);
        QFETCH(qint64, seconds);
        QFETCH(bool, running);
        QFETCH(int, changes);
        QFETCH(int, malformed);
        const Logbook book = readLogbook(text);
        QCOMPARE(book.clocks.size(), clocks);
        QCOMPARE(clockedSeconds(book.clocks), seconds);
        QCOMPARE(book.running() != nullptr, running);
        QCOMPARE(book.changes.size(), changes);
        QCOMPARE(book.malformed, malformed);
    }

    void statusHistoryFields()
    {
        const Logbook book =
                readLogbook(u":LOGBOOK:\n- State \"DONE\"       from \"DOING\"      [2026-11-03 Tue 10:25]\n:END:"_s);
        QCOMPARE(book.changes.size(), 1);
        QCOMPARE(book.changes[0].to, u"DONE"_s);
        QCOMPARE(book.changes[0].from, u"DOING"_s);
        QCOMPARE(book.changes[0].at, at(2026, 11, 3, 10, 25));
        QCOMPARE(book.changes[0].line, 1);
    }

    void daylightSaving_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<qint64>("seconds");
        // 2026-03-29: 02:00 -> 03:00 (an hour fewer), 2026-10-25: 03:00 -> 02:00 (an hour more).
        QTest::newRow("spring forward") << u"CLOCK: [2026-03-29 Sun 01:30:00]--[2026-03-29 Sun 03:30:00] =>  01:00:00"_s
                                        << qint64(3600);
        QTest::newRow("fall back") << u"CLOCK: [2026-10-25 Sun 01:30:00]--[2026-10-25 Sun 03:30:00] =>  03:00:00"_s
                                   << qint64(3 * 3600);
        QTest::newRow("fall back, written in the repeated hour")
                << u"CLOCK: [2026-10-25 Sun 02:50:00]--[2026-10-25 Sun 02:10:00] =>  00:20:00"_s << qint64(20 * 60);
        QTest::newRow("a normal day") << u"CLOCK: [2026-10-26 Mon 01:30:00]--[2026-10-26 Mon 03:30:00] =>  02:00:00"_s
                                      << qint64(2 * 3600);
    }
    void daylightSaving()
    {
        QFETCH(QString, line);
        QFETCH(qint64, seconds);
        const Logbook book = readLogbook(QString(u":LOGBOOK:\n"_s + line + u"\n:END:"_s));
        QCOMPARE(book.clocks.size(), 1);
        QCOMPARE(book.clocks[0].seconds, seconds);
        QCOMPARE(clockedSeconds(book.clocks), seconds);
        // Wall-clock times are kept as written.
        QCOMPARE(book.clocks[0].start.time().hour(), line.mid(23, 2).toInt());
    }

    void perDay()
    {
        // 23:30 -> 00:45 the next day, and a running clock counted up to `now`.
        const Logbook book =
                readLogbook(u":LOGBOOK:\nCLOCK: [2026-11-02 Mon 23:30:00]--[2026-11-03 Tue 00:45:00] =>  01:15:00\n"
                            "CLOCK: [2026-11-03 Tue 10:00:00]\n:END:"_s);
        const QMap<QDate, qint64> days = clockedSecondsByDay(book.clocks, at(2026, 11, 3, 10, 30));
        QCOMPARE(days.size(), 2);
        QCOMPARE(days.value(QDate(2026, 11, 2)), qint64(30 * 60));
        QCOMPARE(days.value(QDate(2026, 11, 3)), qint64(45 * 60 + 30 * 60));
        // Without `now` the running clock does not count.
        QCOMPARE(clockedSecondsByDay(book.clocks).value(QDate(2026, 11, 3)), qint64(45 * 60));
        // A day of 25 hours.
        const Logbook longDay =
                readLogbook(u":LOGBOOK:\nCLOCK: [2026-10-25 Sun 00:00:00]--[2026-10-26 Mon 00:00:00]\n:END:"_s);
        QCOMPARE(clockedSecondsByDay(longDay.clocks).value(QDate(2026, 10, 25)), qint64(25 * 3600));
    }

    void format()
    {
        QCOMPARE(formatClockTimestamp(at(2026, 11, 3, 10, 0)), u"[2026-11-03 Tue 10:00:00]"_s);
        QCOMPARE(formatClockTimestamp(at(2026, 11, 8, 9, 5), false), u"[2026-11-08 Sun 09:05]"_s);
        QCOMPARE(formatClockLine(at(2026, 11, 3, 10, 0), at(2026, 11, 3, 10, 25)),
                 u"CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00"_s);
        QCOMPARE(formatClockLine(at(2026, 11, 3, 10, 0)), u"CLOCK: [2026-11-03 Tue 10:00:00]"_s);
        QCOMPARE(formatClockDuration(123 * 3600 + 5 * 60 + 7), u"123:05:07"_s);
        QCOMPARE(formatClockDuration(-5), u"00:00:00"_s);
        QCOMPARE(formatStatusChange(u"DONE", u"DOING", at(2026, 11, 3, 10, 25)),
                 u"State \"DONE\"       from \"DOING\"      [2026-11-03 Tue 10:25]"_s);
        QCOMPARE(formatSpentTime(45), u"45 s"_s);
        QCOMPARE(formatSpentTime(25 * 60), u"25 min"_s);
        QCOMPARE(formatSpentTime(3600 + 5 * 60), u"1 h 05 min"_s);
        // Written lines read back.
        const QString line = formatClockLine(at(2026, 3, 29, 1, 30), at(2026, 3, 29, 3, 30));
        QCOMPARE(line, u"CLOCK: [2026-03-29 Sun 01:30:00]--[2026-03-29 Sun 03:30:00] =>  01:00:00"_s);
        QCOMPARE(clockedSeconds(readLogbook(QString(u":LOGBOOK:\n"_s + line + u"\n:END:"_s)).clocks), qint64(3600));
    }

    void startStop_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QString>("started"); // after startTaskClock at 10:00
        QTest::addColumn<QString>("stopped"); // then stopTaskClock at 10:25

        QTest::newRow("new drawer")
                << u"# Dziś\n- [ ] Napisać raport o żółwiach\n- [ ] inne\n"_s << 1
                << u"# Dziś\n- [ ] Napisać raport o żółwiach\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 10:00:00]\n  :END:\n- [ ] inne\n"_s
                << u"# Dziś\n- [ ] Napisać raport o żółwiach\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n  :END:\n- [ ] inne\n"_s;
        QTest::newRow("existing drawer, Logseq tabs")
                << u"- DOING zadanie\n\t:LOGBOOK:\n\tCLOCK: [2026-11-02 Mon 09:00:00]--[2026-11-02 Mon 09:30:00] =>  00:30:00\n\t:END:\n"_s
                << 0
                << u"- DOING zadanie\n\t:LOGBOOK:\n\tCLOCK: [2026-11-02 Mon 09:00:00]--[2026-11-02 Mon 09:30:00] =>  00:30:00\n\tCLOCK: [2026-11-03 Tue 10:00:00]\n\t:END:\n"_s
                << u"- DOING zadanie\n\t:LOGBOOK:\n\tCLOCK: [2026-11-02 Mon 09:00:00]--[2026-11-02 Mon 09:30:00] =>  00:30:00\n\tCLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n\t:END:\n"_s;
        QTest::newRow("after planning lines, nested, any line of the block")
                << u"- projekt\n    - TODO pod\n      SCHEDULED: <2026-11-03 Tue>\n      opis\n"_s << 3
                << u"- projekt\n    - TODO pod\n      SCHEDULED: <2026-11-03 Tue>\n      :LOGBOOK:\n      CLOCK: [2026-11-03 Tue 10:00:00]\n      :END:\n      opis\n"_s
                << u"- projekt\n    - TODO pod\n      SCHEDULED: <2026-11-03 Tue>\n      :LOGBOOK:\n      CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n      :END:\n      opis\n"_s;
        QTest::newRow("children stay below")
                << u"- [ ] rodzic\n  - [ ] dziecko\n"_s << 0
                << u"- [ ] rodzic\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 10:00:00]\n  :END:\n  - [ ] dziecko\n"_s
                << u"- [ ] rodzic\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n  :END:\n  - [ ] dziecko\n"_s;
        QTest::newRow("CRLF, no final line break")
                << u"- [ ] a\r\n- [ ] b"_s << 1
                << u"- [ ] a\r\n- [ ] b\r\n  :LOGBOOK:\r\n  CLOCK: [2026-11-03 Tue 10:00:00]\r\n  :END:"_s
                << u"- [ ] a\r\n- [ ] b\r\n  :LOGBOOK:\r\n  CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\r\n  :END:"_s;
        QTest::newRow("ordered list")
                << u"12. [ ] dwanaście\n"_s << 0
                << u"12. [ ] dwanaście\n    :LOGBOOK:\n    CLOCK: [2026-11-03 Tue 10:00:00]\n    :END:\n"_s
                << u"12. [ ] dwanaście\n    :LOGBOOK:\n    CLOCK: [2026-11-03 Tue 10:00:00]--[2026-11-03 Tue 10:25:00] =>  00:25:00\n    :END:\n"_s;
        QTest::newRow("no task") << u"- zwykły punkt\n"_s << 0 << u"- zwykły punkt\n"_s << u"- zwykły punkt\n"_s;
        QTest::newRow("a paragraph") << u"Akapit\n"_s << 0 << u"Akapit\n"_s << u"Akapit\n"_s;
        QTest::newRow("out of range") << u"- [ ] a\n"_s << 7 << u"- [ ] a\n"_s << u"- [ ] a\n"_s;
        QTest::newRow("in a quote") << u"> - [ ] cytat\n"_s << 0 << u"> - [ ] cytat\n"_s << u"> - [ ] cytat\n"_s;
    }
    void startStop()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QString, started);
        QFETCH(QString, stopped);
        const QString a = startTaskClock(content, line, at(2026, 11, 3, 10, 0));
        QCOMPARE(a, started);
        // Already running: unchanged.
        QCOMPARE(startTaskClock(a, line, at(2026, 11, 3, 10, 5)), a);
        const QString b = stopTaskClock(a, line, at(2026, 11, 3, 10, 25));
        QCOMPARE(b, stopped);
        // Not running: unchanged.
        QCOMPARE(stopTaskClock(b, line, at(2026, 11, 3, 10, 30)), b);
        if (started != content) {
            // The index reads it back.
            const ParsedNote note = parseNote(u"a.md"_s, b);
            const Task *task = nullptr;
            for (const Block &bl : note.blocks) {
                if (bl.task && bl.line == taskBlockLine(b, line))
                    task = &*bl.task;
            }
            QVERIFY(task);
            QVERIFY(task->clockedSeconds >= 25 * 60);
            QVERIFY(!task->clockStart.isValid());
            QCOMPARE(parseNote(u"a.md"_s, a).tasks.isEmpty(), false);
        }
    }

    void parsedTask()
    {
        const QString content = u"- DOING Raport\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 09:00:00]--[2026-11-03 Tue "
                                u"09:30:00] =>  00:30:00\n"
                                "  CLOCK: [2026-11-03 Tue 10:00:00]\n  :END:\n"_s;
        const ParsedNote note = parseNote(u"a.md"_s, content);
        QCOMPARE(note.tasks.size(), 1);
        QCOMPARE(note.tasks[0].clockedSeconds, qint64(30 * 60));
        QCOMPARE(note.tasks[0].clockStart, at(2026, 11, 3, 10, 0));
        QCOMPARE(note.tasks[0].text, u"Raport"_s);
        QCOMPARE(note.blocks[0].text, u"Raport"_s);
    }

    void stopBeforeStart()
    {
        // The clock went back: the entry ends where it started (never negative).
        const QString a = startTaskClock(u"- [ ] a\n"_s, 0, at(2026, 11, 3, 10, 0));
        const QString b = stopTaskClock(a, 0, at(2026, 11, 3, 9, 0));
        QVERIFY(b.contains(u"--[2026-11-03 Tue 10:00:00] =>  00:00:00"_s));
    }

    void statusLog_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("status");
        QTest::addColumn<QString>("expected");
        const QString clock = u"CLOCK: [2026-11-03 Tue 10:00:00]"_s;
        QTest::newRow("todo -> doing starts the clock")
                << u"- TODO pisać\n"_s << int(TaskStatus::Doing)
                << u"- DOING pisać\n  :LOGBOOK:\n  "_s + clock
                           + u"\n  State \"DOING\"      from \"TODO\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s;
        QTest::newRow("doing -> done stops it")
                << u"- [/] pisać\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 09:00:00]\n  :END:\n"_s << int(TaskStatus::Done)
                << u"- [x] pisać ✅ 2026-11-03\n  :LOGBOOK:\n  CLOCK: [2026-11-03 Tue 09:00:00]--[2026-11-03 Tue "
                   u"10:00:00] =>  01:00:00\n"
                   "  State \"DONE\"       from \"DOING\"      [2026-11-03 Tue 10:00]\n  :END:\n"_s;
        QTest::newRow("todo -> done")
                << u"- [ ] kupić żółty ser\n"_s << int(TaskStatus::Done)
                << u"- [x] kupić żółty ser ✅ 2026-11-03\n  :LOGBOOK:\n  State \"DONE\"       from \"TODO\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s;
        QTest::newRow("LATER keeps its workflow")
                << u"- LATER coś\n"_s << int(TaskStatus::Doing)
                << u"- NOW coś\n  :LOGBOOK:\n  "_s + clock
                           + u"\n  State \"NOW\"        from \"TODO\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s;
        QTest::newRow("same status: unchanged") << u"- [ ] a\n"_s << int(TaskStatus::Todo) << u"- [ ] a\n"_s;
        QTest::newRow("a new task is not logged") << u"- zwykły\n"_s << int(TaskStatus::Todo) << u"- [ ] zwykły\n"_s;
        QTest::newRow("recurring: the done one gets the history")
                << u"- [ ] podlać 🔁 every day 📅 2026-11-03\n"_s << int(TaskStatus::Done)
                << u"- [ ] podlać 🔁 every day 📅 2026-11-04\n- [x] podlać 🔁 every day 📅 2026-11-03 ✅ 2026-11-03\n"
                   "  :LOGBOOK:\n  State \"DONE\"       from \"TODO\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s;
    }
    void statusLog()
    {
        QFETCH(QString, content);
        QFETCH(int, status);
        QFETCH(QString, expected);
        StatusLogOptions options;
        options.history = true;
        options.now = at(2026, 11, 3, 10, 0);
        QCOMPARE(setTaskStatusLogged(content, 0, TaskStatus(status), options), expected);
        // Off: exactly setTaskStatus().
        options.history = false;
        QCOMPARE(setTaskStatusLogged(content, 0, TaskStatus(status), options),
                 setTaskStatus(content, 0, TaskStatus(status), QDate(2026, 11, 3)));
    }

    void toggleLogged()
    {
        StatusLogOptions options{true, at(2026, 11, 3, 10, 0)};
        const QString done = toggleTaskLogged(u"- [ ] a\n"_s, 0, options);
        QCOMPARE(
                done,
                u"- [x] a ✅ 2026-11-03\n  :LOGBOOK:\n  State \"DONE\"       from \"TODO\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s);
        const QString again = toggleTaskLogged(done, 0, options);
        QCOMPARE(again, u"- [ ] a\n  :LOGBOOK:\n  State \"DONE\"       from \"TODO\"       [2026-11-03 Tue 10:00]\n"
                        "  State \"TODO\"       from \"DONE\"       [2026-11-03 Tue 10:00]\n  :END:\n"_s);
        QCOMPARE(taskLogbook(again, 0).changes.size(), 2);
    }
};

QTEST_GUILESS_MAIN(TstMarkdownLogbook)
#include "tst_markdown_logbook.moc"
