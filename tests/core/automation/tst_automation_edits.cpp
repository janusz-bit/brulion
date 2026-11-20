// What brulion:// addresses write (automation/edits.h): a block at the end or the start of a page,
// of a heading's section, among a block's children — every other byte kept (CRLF, a BOM, the
// frontmatter, a template's empty bullet filled) — and the task line an address describes, with
// Polish and English dates, a reminder's default time, alarms, recurrence and priorities, and the
// errors of what cannot be read.
#include "automation/edits.h"

#include <QTest>

using namespace Qt::StringLiterals;
using namespace brulion;
using namespace brulion::automation;

namespace {
const QDateTime kNow(QDate(2026, 11, 20), QTime(10, 0)); // a Friday
}

class TestAutomationEdits : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void insert_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("block");
        QTest::addColumn<bool>("start");
        QTest::addColumn<QString>("heading");
        QTest::addColumn<QString>("blockId");
        QTest::addColumn<QString>("expected");
        QTest::addColumn<int>("line");

        QTest::newRow("end") << u"- chleb\n"_s << u"- mleko"_s << false << QString() << QString()
                             << u"- chleb\n- mleko\n"_s << 1;
        QTest::newRow("end, empty page") << QString() << u"- mleko"_s << false << QString() << QString()
                                         << u"- mleko\n"_s << 0;
        QTest::newRow("end, no final newline") << u"- chleb"_s << u"- mleko"_s << false << QString() << QString()
                                               << u"- chleb\n- mleko\n"_s << 1;
        QTest::newRow("end, CRLF") << u"# Zakupy\r\n\r\n- chleb\r\n"_s << u"- mleko\n  2 litry"_s << false
                                   << QString() << QString() << u"# Zakupy\r\n\r\n- chleb\r\n- mleko\r\n  2 litry\r\n"_s
                                   << 3;
        QTest::newRow("end, BOM") << u"﻿- chleb\n"_s << u"- mleko"_s << false << QString() << QString()
                                  << u"﻿- chleb\n- mleko\n"_s << 1;
        QTest::newRow("end, template's empty bullet")
                << u"## Plan dnia\n- \n"_s << u"- Zadzwonić do mamy"_s << false << QString() << QString()
                << u"## Plan dnia\n- Zadzwonić do mamy\n"_s << 1;
        QTest::newRow("start, empty page") << QString() << u"- mleko"_s << true << QString() << QString()
                                           << u"- mleko\n"_s << 0;
        QTest::newRow("start") << u"- chleb\n- masło\n"_s << u"- mleko"_s << true << QString() << QString()
                               << u"- mleko\n- chleb\n- masło\n"_s << 0;
        QTest::newRow("start, after frontmatter")
                << u"---\ntags: [zakupy]\n---\n- chleb\n"_s << u"- mleko"_s << true << QString() << QString()
                << u"---\ntags: [zakupy]\n---\n- mleko\n- chleb\n"_s << 3;
        QTest::newRow("start, after the title")
                << u"# Zakupy\n\n- chleb\n"_s << u"- mleko"_s << true << QString() << QString()
                << u"# Zakupy\n\n- mleko\n- chleb\n"_s << 2;
        QTest::newRow("heading's section end")
                << u"# Zakupy\n\n## Dziś\n- chleb\n  - razowy\n\n## Jutro\n- ser\n"_s << u"- mleko"_s << false
                << u"Dziś"_s << QString() << u"# Zakupy\n\n## Dziś\n- chleb\n  - razowy\n- mleko\n\n## Jutro\n- ser\n"_s
                << 5;
        QTest::newRow("heading with hashes, other case")
                << u"## Dziś\n- chleb\n## Jutro\n"_s << u"- mleko"_s << false << u"## DZIS"_s << QString()
                << u"## Dziś\n- chleb\n- mleko\n## Jutro\n"_s << 2;
        QTest::newRow("heading, last section") << u"## Jutro\n- ser\n## Dziś\n- chleb\n"_s << u"- mleko"_s << false
                                               << u"Dziś"_s << QString()
                                               << u"## Jutro\n- ser\n## Dziś\n- chleb\n- mleko\n"_s << 4;
        QTest::newRow("heading, empty section") << u"## Dziś\n## Jutro\n"_s << u"- mleko"_s << false << u"Dziś"_s
                                                << QString() << u"## Dziś\n- mleko\n## Jutro\n"_s << 1;
        QTest::newRow("heading, subheadings stay inside")
                << u"## Dom\n- a\n### Kuchnia\n- b\n## Praca\n"_s << u"- c"_s << false << u"Dom"_s << QString()
                << u"## Dom\n- a\n### Kuchnia\n- b\n- c\n## Praca\n"_s << 4;
        QTest::newRow("heading path") << u"# A\n## Plan\n- x\n# B\n## Plan\n- y\n"_s << u"- z"_s << false
                                      << u"B#Plan"_s << QString() << u"# A\n## Plan\n- x\n# B\n## Plan\n- y\n- z\n"_s
                                      << 6;
        QTest::newRow("heading, start") << u"## Dziś\n- chleb\n"_s << u"- mleko"_s << true << u"Dziś"_s << QString()
                                        << u"## Dziś\n- mleko\n- chleb\n"_s << 1;
        QTest::newRow("heading, CRLF") << u"## Dziś\r\n- chleb\r\n\r\n## Jutro\r\n"_s << u"- mleko"_s << false
                                       << u"Dziś"_s << QString()
                                       << u"## Dziś\r\n- chleb\r\n- mleko\r\n\r\n## Jutro\r\n"_s << 2;
        QTest::newRow("block, last child")
                << u"- Projekt ^p1\n  - krok 1\n- inne\n"_s << u"- krok 2"_s << false << QString() << u"^p1"_s
                << u"- Projekt ^p1\n  - krok 1\n  - krok 2\n- inne\n"_s << 2;
        QTest::newRow("block, first child")
                << u"- Projekt ^p1\n  - krok 1\n"_s << u"- krok 0"_s << true << QString() << u"((p1))"_s
                << u"- Projekt ^p1\n  - krok 0\n  - krok 1\n"_s << 1;
        QTest::newRow("block without children") << u"- Projekt ^p1\n- inne\n"_s << u"- [ ] krok"_s << false
                                                << QString() << u"p1"_s << u"- Projekt ^p1\n\t- [ ] krok\n- inne\n"_s
                                                << 1;
    }

    void insert()
    {
        QFETCH(QString, content);
        QFETCH(QString, block);
        QFETCH(bool, start);
        QFETCH(QString, heading);
        QFETCH(QString, blockId);
        QFETCH(QString, expected);
        QFETCH(int, line);
        const Inserted r = insertBlock(content, {block, start ? Place::Start : Place::End, heading, blockId});
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.content, expected);
        QCOMPARE(r.line, line);
    }

    void insertErrors_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("heading");
        QTest::addColumn<QString>("blockId");
        QTest::addColumn<QString>("error");
        QTest::newRow("no such heading") << u"## Dziś\n"_s << u"- a"_s << u"Wczoraj"_s << QString()
                                         << u"heading_not_found"_s;
        QTest::newRow("no headings") << u"- a\n"_s << u"- b"_s << u"Dziś"_s << QString() << u"heading_not_found"_s;
        QTest::newRow("no such block") << u"- a ^x1\n"_s << u"- b"_s << QString() << u"y2"_s << u"block_not_found"_s;
        QTest::newRow("blank block") << u"- a\n"_s << u"  \n"_s << QString() << QString() << u"empty"_s;
    }

    void insertErrors()
    {
        QFETCH(QString, content);
        QFETCH(QString, block);
        QFETCH(QString, heading);
        QFETCH(QString, blockId);
        QFETCH(QString, error);
        const Inserted r = insertBlock(content, {block, Place::End, heading, blockId});
        QVERIFY(!r.ok);
        QCOMPARE(r.error, error);
    }

    void task_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("due");
        QTest::addColumn<QString>("remind");
        QTest::addColumn<bool>("alarm");
        QTest::addColumn<QString>("every");
        QTest::addColumn<QString>("priority");
        QTest::addColumn<bool>("parseText");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("reminder");

        QTest::newRow("plain") << u"Kupić żółty ser"_s << QString() << QString() << false << QString() << QString()
                               << true << u"- [ ] Kupić żółty ser"_s << QString();
        QTest::newRow("date in the text: reminder")
                << u"Zadzwonić do mamy jutro o 18"_s << QString() << QString() << false << QString() << QString()
                << true << u"- [ ] Zadzwonić do mamy ⏰ 2026-11-21 18:00"_s << u"2026-11-21 18:00"_s;
        QTest::newRow("date in the text: due") << u"Oddać książkę w poniedziałek"_s << QString() << QString() << false
                                               << QString() << QString() << true
                                               << u"- [ ] Oddać książkę 📅 2026-11-23"_s << QString();
        QTest::newRow("date in the text, alarm")
                << u"Pobudka jutro o 6:30"_s << QString() << QString() << true << QString() << QString() << true
                << u"- [ ] Pobudka ⏰! 2026-11-21 06:30"_s << u"2026-11-21 06:30"_s;
        QTest::newRow("date in the text left as text") << u"Kupić mleko jutro"_s << QString() << QString() << false
                                                       << QString() << QString() << false
                                                       << u"- [ ] Kupić mleko jutro"_s << QString();
        QTest::newRow("English") << u"Call mum tomorrow at 6pm"_s << QString() << QString() << false << QString()
                                 << QString() << true << u"- [ ] Call mum ⏰ 2026-11-21 18:00"_s << u"2026-11-21 18:00"_s;
        QTest::newRow("explicit due") << u"Raport"_s << u"w piątek"_s << QString() << false << QString() << QString()
                                      << true << u"- [ ] Raport 📅 2026-11-27"_s << QString();
        QTest::newRow("ISO due") << u"Raport"_s << u"2026-12-01"_s << QString() << false << QString() << QString()
                                 << true << u"- [ ] Raport 📅 2026-12-01"_s << QString();
        QTest::newRow("reminder without a time") << u"Wizyta"_s << QString() << u"pojutrze"_s << false << QString()
                                                 << QString() << true << u"- [ ] Wizyta ⏰ 2026-11-22 08:15"_s
                                                 << u"2026-11-22 08:15"_s;
        QTest::newRow("alarm") << u"Pociąg"_s << QString() << u"2026-11-21 05:40"_s << true << QString() << QString()
                               << true << u"- [ ] Pociąg ⏰! 2026-11-21 05:40"_s << u"2026-11-21 05:40"_s;
        QTest::newRow("due and reminder, priority, recurrence")
                << u"- [ ] Podlać kwiaty\nw salonie"_s << u"jutro"_s << u"jutro o 9"_s << false << u"co tydzień"_s
                << u"wysoki"_s << true << u"- [ ] Podlać kwiaty w salonie 📅 2026-11-21 ⏰ 2026-11-21 09:00 ⏫ 🔁 every week"_s
                << u"2026-11-21 09:00"_s;
        QTest::newRow("priority of no task") << u"Coś"_s << QString() << QString() << false << QString() << u"none"_s
                                             << true << u"- [ ] Coś"_s << QString();
    }

    void task()
    {
        QFETCH(QString, text);
        QFETCH(QString, due);
        QFETCH(QString, remind);
        QFETCH(bool, alarm);
        QFETCH(QString, every);
        QFETCH(QString, priority);
        QFETCH(bool, parseText);
        QFETCH(QString, block);
        QFETCH(QString, reminder);
        TaskSpec spec;
        spec.text = text;
        spec.due = due;
        spec.remind = remind;
        spec.alarm = alarm;
        spec.every = every;
        spec.priority = priority;
        spec.parseText = parseText;
        const BuiltTask t = buildTask(spec, kNow, Qt::Monday, dates::UiLocale::Polish, QTime(8, 15));
        QVERIFY2(t.error.isEmpty(), qPrintable(t.error + u": "_s + t.detail));
        QCOMPARE(t.block, block);
        QCOMPARE(t.reminder.isValid() ? t.reminder.toString() : QString(), reminder);
        QCOMPARE(t.alarm, alarm && !reminder.isEmpty());
    }

    void taskErrors_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("due");
        QTest::addColumn<QString>("remind");
        QTest::addColumn<QString>("every");
        QTest::addColumn<QString>("priority");
        QTest::addColumn<QString>("error");
        QTest::newRow("no text") << u"  \n "_s << QString() << QString() << QString() << QString() << u"missing_text"_s;
        QTest::newRow("only a marker") << u"- [ ] "_s << QString() << QString() << QString() << QString()
                                       << u"missing_text"_s;
        QTest::newRow("due no date") << u"a"_s << u"kiedyś"_s << QString() << QString() << QString()
                                     << u"date_not_understood"_s;
        QTest::newRow("remind no date") << u"a"_s << QString() << u"???"_s << QString() << QString()
                                        << u"date_not_understood"_s;
        QTest::newRow("bad recurrence") << u"a"_s << QString() << QString() << u"czasami"_s << QString()
                                        << u"recurrence_not_understood"_s;
        QTest::newRow("bad priority") << u"a"_s << QString() << QString() << QString() << u"pilne!!"_s
                                      << u"bad_priority"_s;
    }

    void taskErrors()
    {
        QFETCH(QString, text);
        QFETCH(QString, due);
        QFETCH(QString, remind);
        QFETCH(QString, every);
        QFETCH(QString, priority);
        QFETCH(QString, error);
        TaskSpec spec;
        spec.text = text;
        spec.due = due;
        spec.remind = remind;
        spec.every = every;
        spec.priority = priority;
        const BuiltTask t = buildTask(spec, kNow, Qt::Monday, dates::UiLocale::Polish);
        QCOMPARE(t.error, error);
        QVERIFY(t.block.isEmpty());
    }

    void dayOf_data()
    {
        QTest::addColumn<QString>("phrase");
        QTest::addColumn<QString>("day");
        QTest::newRow("iso") << u"2026-02-03"_s << u"2026-02-03"_s;
        QTest::newRow("today") << u"today"_s << u"2026-11-20"_s;
        QTest::newRow("dziś") << u"dziś"_s << u"2026-11-20"_s;
        QTest::newRow("wczoraj") << u"wczoraj"_s << u"2026-11-19"_s;
        QTest::newRow("w poniedziałek") << u"w poniedziałek"_s << u"2026-11-23"_s;
        QTest::newRow("without diacritics") << u"przedwczoraj"_s << u"2026-11-18"_s;
        QTest::newRow("English") << u"yesterday"_s << u"2026-11-19"_s;
        QTest::newRow("other words") << u"wczoraj u lekarza"_s << QString();
        QTest::newRow("no date") << u"Zakupy"_s << QString();
        QTest::newRow("empty") << QString() << QString();
        QTest::newRow("bad iso") << u"2026-02-30"_s << QString();
    }

    void dayOf()
    {
        QFETCH(QString, phrase);
        QFETCH(QString, day);
        const auto d = brulion::automation::dayOf(phrase, kNow, Qt::Monday, dates::UiLocale::Polish);
        QCOMPARE(d ? d->toString(Qt::ISODate) : QString(), day);
    }
};

QTEST_GUILESS_MAIN(TestAutomationEdits)
#include "tst_automation_edits.moc"
