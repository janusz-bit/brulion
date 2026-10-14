// snoozeReminder(): in-place token edits (CRLF and every other byte preserved), one-off child
// bullets for recurring reminders, and the content left alone where there is no reminder.
#include "markdown/parse.h"
#include "reminders/reminders.h"
#include "reminderstest.h"

#include <QRegularExpression>
#include <QTest>
#include <QTimeZone>

using namespace brulion;
using namespace remtest;
using namespace Qt::StringLiterals;

class TestRemindersSnooze : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void replacesToken_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QDateTime>("newAt");
        QTest::addColumn<QString>("expected");
        const QDateTime t = dt(2026, 10, 15, 9, 10);
        QTest::newRow("task") << u"- [ ] Zadzwonić do Łucji ⏰ 2026-10-15 09:00 #praca"_s << 0 << t
                              << u"- [ ] Zadzwonić do Łucji ⏰ 2026-10-15 09:10 #praca"_s;
        QTest::newRow("alarm keeps !") << u"- Pobudka ⏰! 2026-10-15 06:30"_s << 0 << t
                                       << u"- Pobudka ⏰! 2026-10-15 09:10"_s;
        QTest::newRow("date only gets a time") << u"- Termin ⏰ 2026-10-15 i dalej"_s << 0 << dt(2026, 10, 16, 9, 0)
                                               << u"- Termin ⏰ 2026-10-16 09:00 i dalej"_s;
        QTest::newRow("first token only") << u"- Dwa ⏰ 2026-10-15 09:00 ⏰ 2026-10-15 18:00"_s << 0 << t
                                          << u"- Dwa ⏰ 2026-10-15 09:10 ⏰ 2026-10-15 18:00"_s;
        QTest::newRow("obsidian-reminder syntax")
                << u"- Spotkanie (@2026-10-20 07:30) z ekipą"_s << 0 << dt(2026, 10, 20, 8, 0)
                << u"- Spotkanie (@2026-10-20 08:00) z ekipą"_s;
        QTest::newRow("⏰ preferred over (@…)") << u"- X (@2026-10-20 07:30) ⏰ 2026-10-21 09:00"_s << 0 << t
                                                << u"- X (@2026-10-20 07:30) ⏰ 2026-10-15 09:10"_s;
        QTest::newRow("seconds dropped") << u"- A ⏰ 2026-10-15 09:00"_s << 0 << dt(2026, 10, 15, 9, 10, 59, 999)
                                         << u"- A ⏰ 2026-10-15 09:10"_s;
        QTest::newRow("variation selector")
                << u"- A ⏰"_s + QChar(0xFE0F) + u" 2026-10-15 09:00 b"_s << 0 << t << u"- A ⏰ 2026-10-15 09:10 b"_s;
        QTest::newRow("continuation line") << u"- Zadanie\n  notatka ⏰ 2026-10-15 09:00\n- inne"_s << 1 << t
                                           << u"- Zadanie\n  notatka ⏰ 2026-10-15 09:10\n- inne"_s;
        QTest::newRow("paragraph line") << u"Akapit\ndruga ⏰ 2026-10-15 09:00\ntrzecia"_s << 1 << t
                                        << u"Akapit\ndruga ⏰ 2026-10-15 09:10\ntrzecia"_s;
        QTest::newRow("inline code skipped") << u"- `⏰ 2026-10-01 00:00` prawdziwe ⏰ 2026-10-15 09:00"_s << 0 << t
                                             << u"- `⏰ 2026-10-01 00:00` prawdziwe ⏰ 2026-10-15 09:10"_s;
        const QString bom(QChar(0xFEFF));
        QTest::newRow("bom and trailing spaces")
                << bom + u"- A ⏰ 2026-10-15 09:00  \n"_s << 0 << t << bom + u"- A ⏰ 2026-10-15 09:10  \n"_s;
        QTest::newRow("done task") << u"- [x] Zrobione ⏰ 2026-10-15 09:00 ✅ 2026-10-15"_s << 0 << t
                                   << u"- [x] Zrobione ⏰ 2026-10-15 09:10 ✅ 2026-10-15"_s;
        // A `⏰` hidden by a %%comment%% opened on an earlier line is not the reminder.
        QTest::newRow("comment from an earlier line")
                << u"- A %%ukryte\n  ⏰ 2026-10-01 09:00%% ⏰ 2026-10-15 09:00"_s << 1 << t
                << u"- A %%ukryte\n  ⏰ 2026-10-01 09:00%% ⏰ 2026-10-15 09:10"_s;
        QTest::newRow("bare snoozed bullet")
                << u"- [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\n\t- ⏰ 2026-10-15 08:10\n"_s << 1
                << dt(2026, 10, 15, 8, 20)
                << u"- [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\n\t- ⏰ 2026-10-15 08:20\n"_s;
    }

    void replacesToken()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QDateTime, newAt);
        QFETCH(QString, expected);
        QCOMPARE(snoozeReminder(content, line, newAt), expected);
    }

    void crlfPreserved()
    {
        const QString content =
                u"# Plan\r\n\r\n- [ ] Zadzwonić ⏰ 2026-10-15 09:00\r\n- inne ⏰ 2026-10-15 09:00\r\n"_s;
        QCOMPARE(snoozeReminder(content, 2, dt(2026, 10, 15, 9, 15)),
                 u"# Plan\r\n\r\n- [ ] Zadzwonić ⏰ 2026-10-15 09:15\r\n- inne ⏰ 2026-10-15 09:00\r\n"_s);
        // Lone CR line endings too.
        QCOMPARE(snoozeReminder(u"a\r- B ⏰ 2026-10-15 09:00\rc"_s, 1, dt(2026, 10, 15, 9, 15)),
                 u"a\r- B ⏰ 2026-10-15 09:15\rc"_s);
    }

    void otherTimeZone()
    {
        // `newAt` is an instant: given in another zone it is written in local wall-clock time.
        const QString content = u"- A ⏰ 2026-10-15 09:00\n- B 🔁 every day ⏰ 2026-10-15 09:00\n"_s;
        const QDateTime local = dt(2026, 10, 15, 9, 10);
        for (const int hours : {-12, 14}) {
            const QDateTime other = local.toTimeZone(QTimeZone::fromSecondsAheadOfUtc(hours * 3600));
            QCOMPARE(snoozeReminder(content, 0, other), snoozeReminder(content, 0, local));
            QCOMPARE(snoozeReminder(content, 1, other), snoozeReminder(content, 1, local));
        }
    }

    void unchanged_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QDateTime>("newAt");
        const QDateTime t = dt(2026, 10, 15, 9, 10);
        const QString one = u"- A ⏰ 2026-10-15 09:00"_s;
        QTest::newRow("line out of range") << one << 1 << t;
        QTest::newRow("negative line") << one << -1 << t;
        QTest::newRow("invalid time") << one << 0 << QDateTime();
        QTest::newRow("empty content") << QString() << 0 << t;
        QTest::newRow("no reminder") << u"- zwykła linia\n- A ⏰ 2026-10-15 09:00"_s << 0 << t;
        QTest::newRow("inline code only") << u"- `⏰ 2026-10-15 09:00`"_s << 0 << t;
        QTest::newRow("fenced code") << u"```\n⏰ 2026-10-15 09:00\n```"_s << 1 << t;
        QTest::newRow("hidden comment") << u"- A %%⏰ 2026-10-15 09:00%%"_s << 0 << t;
        QTest::newRow("frontmatter") << u"---\nremind: ⏰ 2026-10-15 09:00\n---\n- tekst"_s << 1 << t;
        QTest::newRow("malformed date") << u"- A ⏰ 2026-02-30 09:00"_s << 0 << t;
        QTest::newRow("blank line") << u"- A ⏰ 2026-10-15 09:00\n\n- B"_s << 1 << t;
    }

    void unchanged()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QDateTime, newAt);
        QCOMPARE(snoozeReminder(content, line, newAt), content);
    }

    void recurringGetsChildBullet_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<int>("line");
        QTest::addColumn<QDateTime>("newAt");
        QTest::addColumn<QString>("expected");
        QTest::newRow("first child, tab")
                << u"- [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\n- następne\n"_s << 0 << dt(2026, 10, 15, 8, 10)
                << u"- [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\n\t- ⏰ 2026-10-15 08:10\n- następne\n"_s;
        QTest::newRow("before existing children (their style)")
                << u"- [ ] Leki 🔁 codziennie ⏰ 2026-10-15 08:00\n  - notatka\n"_s << 0 << dt(2026, 10, 15, 8, 10)
                << u"- [ ] Leki 🔁 codziennie ⏰ 2026-10-15 08:00\n  - ⏰ 2026-10-15 08:10\n  - notatka\n"_s;
        QTest::newRow("alarm, no final newline")
                << u"- Pobudka 🔁 every weekday ⏰! 2026-10-15 06:30"_s << 0 << dt(2026, 10, 15, 6, 40)
                << u"- Pobudka 🔁 every weekday ⏰! 2026-10-15 06:30\n\t- ⏰! 2026-10-15 06:40"_s;
        QTest::newRow("CRLF")
                << u"- [ ] Leki 🔁 codziennie ⏰ 2026-10-15 08:00\r\n- inne\r\n"_s << 0 << dt(2026, 10, 15, 8, 10)
                << u"- [ ] Leki 🔁 codziennie ⏰ 2026-10-15 08:00\r\n\t- ⏰ 2026-10-15 08:10\r\n- inne\r\n"_s;
        QTest::newRow("nested item")
                << u"- Projekt\n\t- [ ] Raport 🔁 every week ⏰ 2026-10-16 10:00\n- koniec\n"_s << 1
                << dt(2026, 10, 16, 11, 0)
                << u"- Projekt\n\t- [ ] Raport 🔁 every week ⏰ 2026-10-16 10:00\n\t\t- ⏰ 2026-10-16 11:00\n- koniec\n"_s;
        QTest::newRow("after the block's own lines")
                << u"- TODO Podlać ⏰ 2026-10-15 19:00\n  SCHEDULED: <2026-10-15 Thu .+1d>\n"_s << 0
                << dt(2026, 10, 15, 19, 30)
                << u"- TODO Podlać ⏰ 2026-10-15 19:00\n  SCHEDULED: <2026-10-15 Thu .+1d>\n\t- ⏰ 2026-10-15 19:30\n"_s;
        QTest::newRow("the alarm of the ⏰! token, not of an earlier (@…)")
                << u"- Pobudka (@2026-10-15 06:00) ⏰! 2026-10-15 06:30 🔁 every day\n"_s << 0
                << dt(2026, 10, 15, 6, 40)
                << u"- Pobudka (@2026-10-15 06:00) ⏰! 2026-10-15 06:30 🔁 every day\n\t- ⏰! 2026-10-15 06:40\n"_s;
        QTest::newRow("reminder on a continuation line")
                << u"- Basen 🔁 co tydzień\n  przypomnij ⏰ 2026-10-15 07:00\n"_s << 1 << dt(2026, 10, 15, 7, 30)
                << u"- Basen 🔁 co tydzień\n  przypomnij ⏰ 2026-10-15 07:00\n\t- ⏰ 2026-10-15 07:30\n"_s;
    }

    void recurringGetsChildBullet()
    {
        QFETCH(QString, content);
        QFETCH(int, line);
        QFETCH(QDateTime, newAt);
        QFETCH(QString, expected);
        QCOMPARE(snoozeReminder(content, line, newAt), expected);
    }

    void recurringParagraph()
    {
        const QString content = u"Spotkanie 🔁 every week ⏰ 2026-10-15 09:00\n\nInny akapit\n"_s;
        const QString out = snoozeReminder(content, 0, dt(2026, 10, 15, 9, 30));
        QVERIFY2(out.startsWith(u"Spotkanie 🔁 every week ⏰ 2026-10-15 09:00\n"_s), qPrintable(out));
        QVERIFY2(out.endsWith(u"Inny akapit\n"_s), qPrintable(out));
        const ParsedNote note = markdown::parseNote(u"a.md"_s, out);
        QCOMPARE(note.reminders.size(), 2);
        QCOMPARE(note.reminders.at(0).recurrence, u"every week"_s);
        QCOMPARE(note.reminders.at(1).at, (DateSpec{QDate(2026, 10, 15), QTime(9, 30)}));
        QVERIFY(note.reminders.at(1).recurrence.isEmpty());
        QCOMPARE(note.blocks.at(markdown::blockAtLine(note, note.reminders.at(1).line)).kind, BlockKind::ListItem);
    }

    void recurringInCallout()
    {
        // A recurring reminder in a callout: the callout stays byte-for-byte, a one-off bullet
        // follows it.
        const QString callout = u"> [!todo] Codziennie\r\n> - [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\r\n"_s;
        const QString content = callout + u"\r\nDalej\r\n"_s;
        const QString out = snoozeReminder(content, 1, dt(2026, 10, 15, 8, 10));
        QVERIFY2(out.startsWith(callout), qPrintable(out));
        QVERIFY2(out.endsWith(u"Dalej\r\n"_s), qPrintable(out));
        QVERIFY2(!out.contains(QRegularExpression(u"(?<!\\r)\\n"_s)), qPrintable(out)); // CRLF kept
        const ParsedNote note = markdown::parseNote(u"a.md"_s, out);
        QCOMPARE(note.reminders.size(), 2);
        QCOMPARE(note.reminders.at(0).recurrence, u"every day"_s);
        QCOMPARE(note.reminders.at(1).at, (DateSpec{QDate(2026, 10, 15), QTime(8, 10)}));
        QVERIFY(note.reminders.at(1).recurrence.isEmpty());
    }

    void snoozedOccurrenceIsOneOff()
    {
        const QString content = u"- [ ] Leki 🔁 every day ⏰ 2026-10-15 08:00\n- inne\n"_s;
        const QString snoozed = snoozeReminder(content, 0, dt(2026, 10, 15, 8, 10));
        VaultIndex index;
        fill(index, {{u"Zdrowie.md"_s, snoozed}});
        const auto list = upcomingNotifications(index, dt(2026, 10, 15, 8, 5), 2);
        QCOMPARE(keys(list),
                 QStringList({u"2026-10-15 08:10 Leki"_s, u"2026-10-16 08:00 Leki"_s, u"2026-10-17 08:00 Leki"_s}));
        QCOMPARE(list.first().line, 1);
        // Completing the task silences the snoozed occurrence too.
        index.update(u"Zdrowie.md"_s, QString(snoozed).replace(u"- [ ]"_s, u"- [x]"_s));
        QVERIFY(upcomingNotifications(index, dt(2026, 10, 15, 8, 5), 2).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestRemindersSnooze)
#include "tst_reminders_snooze.moc"
