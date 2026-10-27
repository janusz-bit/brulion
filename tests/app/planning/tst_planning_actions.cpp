// PlanningActions and the pure helpers behind it: toggling tasks through an EditorHost (✅ date,
// recurring tasks, a line that moved, a task that is gone, undo only while the file is
// unchanged), every status of the status menu (no status included), appending blocks to a page
// (template bullets, CRLF, continuation lines), the lines written by the calendar ("Dodaj
// zadanie" / "Dodaj przypomnienie") and natural-language quick add (Polish and English).
#include "fakeeditorhost.h"
#include "planningactions.h"
#include "planningtest.h"
#include "planningutil.h"

#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;
using brulion::editor::FakeEditorHost;
using brulion::planning::PlanningActions;

class TestPlanningActions : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void toggleAndUndo()
    {
        FakeEditorHost host;
        const QString page = u"- [ ] Zamówić farbę 📅 2026-10-16\n- [ ] Zerwać panele\n"_s;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QSignalSpy changed(&actions, &PlanningActions::taskChanged);

        QVERIFY(actions.toggleTask(u"Dom.md"_s, 1, u"Zerwać panele"_s));
        QCOMPARE(host.file(u"Dom.md"_s), u"- [ ] Zamówić farbę 📅 2026-10-16\n- [x] Zerwać panele ✅ 2026-10-15\n"_s);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(2).toString(), u"done"_s);
        QVERIFY(actions.canUndo());
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Dom.md"_s), page);
        QVERIFY(!actions.canUndo());

        // Undo refuses when the file changed in the meantime.
        QVERIFY(actions.setTaskStatus(u"Dom.md"_s, 0, u"doing"_s));
        QCOMPARE(host.file(u"Dom.md"_s).left(6), u"- [/] "_s);
        host.externalWrite(u"Dom.md"_s, host.file(u"Dom.md"_s) + u"- nowa linia\n"_s);
        QSignalSpy failed(&actions, &PlanningActions::failed);
        QVERIFY(!actions.undo());
        QCOMPARE(failed.count(), 1);
        QVERIFY(host.file(u"Dom.md"_s).endsWith(u"- nowa linia\n"_s));
    }

    void movedAndMissingTasks()
    {
        FakeEditorHost host;
        // The view still thinks "Zerwać panele" is on line 0; a line was added above it.
        host.setFile(u"Dom.md"_s, u"- nowy wpis\n- [ ] Zerwać panele\n- [ ] Inne\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QCOMPARE(actions.locateTask(u"Dom.md"_s, host.file(u"Dom.md"_s), 0, u"Zerwać panele"_s), 1);
        QVERIFY(actions.toggleTask(u"Dom.md"_s, 0, u"Zerwać panele"_s));
        QCOMPARE(host.file(u"Dom.md"_s), u"- nowy wpis\n- [x] Zerwać panele ✅ 2026-10-15\n- [ ] Inne\n"_s);

        // Gone: nothing is written (a plain line is never turned into a task).
        QSignalSpy failed(&actions, &PlanningActions::failed);
        const QString before = host.file(u"Dom.md"_s);
        QVERIFY(!actions.toggleTask(u"Dom.md"_s, 0, u"Usunięte zadanie"_s));
        QCOMPARE(host.file(u"Dom.md"_s), before);
        QCOMPARE(failed.count(), 1);
        QVERIFY(!actions.toggleTask(u"Brak.md"_s, 0));
        QVERIFY(!actions.setTaskStatus(u"Dom.md"_s, 2, u"bogus"_s));
        // Without a text the task must be exactly on the line.
        QVERIFY(!actions.toggleTask(u"Dom.md"_s, 0));
    }

    void statusMenuChoices_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("status");
        QTest::addColumn<QString>("expected");
        QTest::newRow("in progress") << u"- [ ] Pomalować ścianę"_s << u"doing"_s << u"- [/] Pomalować ścianę"_s;
        QTest::newRow("waiting") << u"- [ ] Pomalować ścianę"_s << u"deferred"_s << u"- [>] Pomalować ścianę"_s;
        QTest::newRow("cancelled") << u"- [/] Pomalować ścianę"_s << u"cancelled"_s
                                   << u"- [-] Pomalować ścianę ❌ 2026-10-15"_s;
        QTest::newRow("done") << u"- [>] Pomalować ścianę"_s << u"done"_s << u"- [x] Pomalować ścianę ✅ 2026-10-15"_s;
        QTest::newRow("to do") << u"- [x] Pomalować ścianę ✅ 2026-10-01"_s << u"todo"_s << u"- [ ] Pomalować ścianę"_s;
        QTest::newRow("no status") << u"- [x] Pomalować ścianę ✅ 2026-10-01"_s << QString() << u"- Pomalować ścianę"_s;
        QTest::newRow("no status, Logseq") << u"- LATER Pomalować ścianę"_s << QString() << u"- Pomalować ścianę"_s;
        QTest::newRow("Logseq style kept")
                << u"- LATER Pomalować ścianę"_s << u"doing"_s << u"- NOW Pomalować ścianę"_s;
    }

    void statusMenuChoices()
    {
        QFETCH(QString, line);
        QFETCH(QString, status);
        QFETCH(QString, expected);
        FakeEditorHost host;
        const QString page = u"# Remont\r\n"_s + line + u"\r\n- [ ] Inne\r\n"_s;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QSignalSpy changed(&actions, &PlanningActions::taskChanged);
        QVERIFY(actions.setTaskStatus(u"Dom.md"_s, 1, status, u"Pomalować ścianę"_s));
        QCOMPARE(host.file(u"Dom.md"_s), u"# Remont\r\n"_s + expected + u"\r\n- [ ] Inne\r\n"_s);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(2).toString(), status);
        // The same status again changes nothing (no status: there is no task to find any more);
        // the toast's Undo puts the line back.
        QVERIFY(!actions.setTaskStatus(u"Dom.md"_s, 1, status, u"Pomalować ścianę"_s));
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Dom.md"_s), page);
    }

    void editTokenChoices_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<int>("line"); // where the view thinks the task is
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("value"); // as the models give it
        QTest::addColumn<QString>("action");
        QTest::addColumn<QVariant>("a");
        QTest::addColumn<QVariant>("b");
        QTest::addColumn<QString>("expected"); // "" = nothing written

        const QString page = u"# Dom\r\n- [ ] Zamówić farbę 📅 2026-10-16 ⏳ 2026-10-14 🛫 2026-10-13 ⏫\r\n"
                             u"- [ ] Zapłacić za żłobek ⏰ 2026-10-15 🔁 every month\r\n"_s;
        const auto with = [&](const QString &from, const QString &to) { return QString(page).replace(from, to); };
        QTest::newRow("due a week later") << page << 1 << u"due"_s << u"2026-10-16"_s << u"shift"_s << QVariant(7)
                                          << QVariant(0) << with(u"📅 2026-10-16"_s, u"📅 2026-10-23"_s);
        QTest::newRow("scheduled today") << page << 1 << u"scheduled"_s << u"2026-10-14"_s << u"move"_s << QVariant(0)
                                         << QVariant() << with(u"⏳ 2026-10-14"_s, u"⏳ 2026-10-15"_s);
        QTest::newRow("start removed") << page << 1 << u"start"_s << u"2026-10-13"_s << u"remove"_s << QVariant()
                                       << QVariant() << with(u" 🛫 2026-10-13"_s, QString());
        QTest::newRow("priority") << page << 1 << u"priority"_s << u"high"_s << u"priority"_s << QVariant(u"low"_s)
                                  << QVariant() << with(u"⏫"_s, u"🔽"_s);
        // The index gives the reminder the default time (09:00).
        QTest::newRow("reminder hour later")
                << page << 2 << u"reminder"_s << u"2026-10-15 09:00"_s << u"shift"_s << QVariant(0) << QVariant(60)
                << with(u"⏰ 2026-10-15"_s, u"⏰ 2026-10-15 10:00"_s);
        QTest::newRow("reminder alarm") << page << 2 << u"reminder"_s << u"2026-10-15 09:00"_s << u"alarm"_s
                                        << QVariant(true) << QVariant()
                                        << with(u"⏰ 2026-10-15"_s, u"⏰! 2026-10-15"_s);
        QTest::newRow("rule") << page << 2 << u"recurrence"_s << u"every month"_s << u"recurrence"_s
                              << QVariant(u"co tydzień"_s) << QVariant()
                              << with(u"🔁 every month"_s, u"🔁 every week"_s);
        QTest::newRow("reminder's block priority")
                << page << 2 << u"reminder"_s << u"2026-10-15 09:00"_s << u"blockPriority"_s << QVariant(u"highest"_s)
                << QVariant() << with(u"🔁 every month\r\n"_s, u"🔁 every month 🔺\r\n"_s);
        QTest::newRow("value changed meanwhile")
                << page << 1 << u"due"_s << u"2026-10-17"_s << u"move"_s << QVariant(1) << QVariant() << QString();
        QTest::newRow("gone") << page << 1 << u"recurrence"_s << u"every day"_s << u"remove"_s << QVariant()
                              << QVariant() << QString();
    }

    void editTokenChoices()
    {
        QFETCH(QString, page);
        QFETCH(int, line);
        QFETCH(QString, kind);
        QFETCH(QString, value);
        QFETCH(QString, action);
        QFETCH(QVariant, a);
        QFETCH(QVariant, b);
        QFETCH(QString, expected);
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QSignalSpy failed(&actions, &PlanningActions::failed);
        const bool written = actions.editToken(u"Dom.md"_s, line, u"Zamówić farbę"_s, kind, value, action, a, b);
        if (expected.isEmpty()) {
            QVERIFY(!written);
            QCOMPARE(failed.count(), 1);
            QCOMPARE(host.file(u"Dom.md"_s), page);
            return;
        }
        QVERIFY(written);
        QCOMPARE(host.file(u"Dom.md"_s), expected);
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Dom.md"_s), page);
    }

    void movedTokenEdit()
    {
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, u"- nowy wpis\n- [ ] Zamówić farbę 📅 2026-10-16\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        // The view still has the task on line 0.
        QVERIFY(actions.editToken(u"Dom.md"_s, 0, u"Zamówić farbę"_s, u"due"_s, u"2026-10-16"_s, u"shift"_s, 1, 0));
        QCOMPARE(host.file(u"Dom.md"_s), u"- nowy wpis\n- [ ] Zamówić farbę 📅 2026-10-17\n"_s);
    }

    void staleLineWithAnEqualToken()
    {
        // A line was added above (a sync); the index still has "Zapłacić rachunek" on line 1,
        // which now holds "Kupić chleb" with the same date: the task by its text is edited.
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s,
                     u"- nowy wpis\n- [ ] Kupić chleb 📅 2026-10-20\n- [ ] Zapłacić rachunek 📅 2026-10-20\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QVERIFY(actions.editToken(u"Dom.md"_s, 1, u"Zapłacić rachunek"_s, u"due"_s, u"2026-10-20"_s, u"shift"_s, 1, 0));
        QCOMPARE(host.file(u"Dom.md"_s),
                 u"- nowy wpis\n- [ ] Kupić chleb 📅 2026-10-20\n- [ ] Zapłacić rachunek 📅 2026-10-21\n"_s);
        // A reminder of a plain bullet (no task with that text): its line.
        host.setFile(u"Leki.md"_s, u"- Witaminy ⏰ 2026-10-16 08:00\n"_s);
        QVERIFY(actions.editToken(u"Leki.md"_s, 0, u"Witaminy"_s, u"reminder"_s, u"2026-10-16 08:00"_s, u"alarm"_s,
                                  true));
        QCOMPARE(host.file(u"Leki.md"_s), u"- Witaminy ⏰! 2026-10-16 08:00\n"_s);
    }

    void tokenMenuInfo_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("value");
        QTest::addColumn<bool>("menu");
        QTest::addColumn<QString>("rule");
        QTest::addColumn<QString>("priority");
        QTest::addColumn<bool>("logseq");
        // The index's rule is the whole token's text; the menu edits the rule at its start.
        QTest::newRow("rule with words after it")
                << u"- [ ] Zakupy 🔁 co tydzień kupić mleko\n"_s << u"recurrence"_s << u"co tydzień kupić mleko"_s
                << true << u"every week"_s << QString() << false;
        QTest::newRow("reminder: the block's rule")
                << u"- [ ] Leki ⏰ 2026-10-20 09:00 🔁 co tydzień do mamy ⏫\n"_s << u"reminder"_s
                << u"2026-10-20 09:00"_s << true << u"every week"_s << u"high"_s << false;
        QTest::newRow("logseq priority") << u"- TODO [#B] Raport 📅 2026-10-20\n"_s << u"priority"_s << u"medium"_s
                                         << true << QString() << u"medium"_s << true;
        QTest::newRow("due of a logseq task") << u"- TODO [#A] Raport 📅 2026-10-20\n"_s << u"due"_s << u"2026-10-20"_s
                                              << true << QString() << u"high"_s << true;
        // Logseq planning lines are no tokens the menus edit: no menu.
        QTest::newRow("SCHEDULED: line") << u"- TODO Raport\n  SCHEDULED: <2026-10-20 Tue .+1w>\n"_s << u"scheduled"_s
                                         << u"2026-10-20"_s << false << QString() << QString() << false;
        QTest::newRow("value gone") << u"- [ ] Raport 📅 2026-10-21\n"_s << u"due"_s << u"2026-10-20"_s << false
                                    << QString() << QString() << false;
        QTest::newRow("unknown kind") << u"- [ ] Raport 📅 2026-10-20\n"_s << u"journal"_s << u"2026-10-20"_s << false
                                      << QString() << QString() << false;
    }

    void tokenMenuInfo()
    {
        QFETCH(QString, page);
        QFETCH(QString, kind);
        QFETCH(QString, value);
        QFETCH(bool, menu);
        QFETCH(QString, rule);
        QFETCH(QString, priority);
        QFETCH(bool, logseq);
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        const QVariantMap info = actions.tokenMenu(u"Dom.md"_s, 0, u"Raport"_s, kind, value);
        QCOMPARE(!info.isEmpty(), menu);
        if (!menu)
            return;
        QCOMPARE(info.value(u"rule"_s).toString(), rule);
        QCOMPARE(info.value(u"priority"_s).toString(), priority);
        QCOMPARE(info.value(u"logseq"_s).toBool(), logseq);
    }

    void snoozeAnOccurrence()
    {
        FakeEditorHost host;
        const QString page = u"- Leki ⏰ 2026-10-10 08:00 🔁 every day\n"_s;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QVERIFY(actions.snoozeReminder(u"Dom.md"_s, 0, u"2026-10-15 09:00"_s));
        QCOMPARE(host.file(u"Dom.md"_s), u"- Leki ⏰ 2026-10-10 08:00 🔁 every day\n\t- ⏰ 2026-10-15 09:00\n"_s);
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Dom.md"_s), page);
        QVERIFY(!actions.snoozeReminder(u"Dom.md"_s, 0, u"jutro"_s));
    }

    void copiedLinks()
    {
        FakeEditorHost host;
        host.setFile(u"Projekty/Dom.md"_s, u"- x\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        QCOMPARE(actions.linkText(u"tag:wa%C5%BCne%20sprawy"_s), u"#[[ważne sprawy]]"_s);
        QCOMPARE(actions.linkText(u"tag:dom"_s), u"#dom"_s);
        QCOMPARE(actions.linkText(u"page:Dom#Plan"_s, u"Inne.md"_s), u"[[Dom#Plan]]"_s);
        QCOMPARE(actions.linkText(u"page:Nowa%20strona"_s), u"[[Nowa strona]]"_s);
        QCOMPARE(actions.linkText(u"block:abc"_s), u"((abc))"_s);
        QCOMPARE(actions.linkText(u"ext:https://example.org"_s), u"https://example.org"_s);
        QCOMPARE(actions.linkText(u"date:1-2"_s), QString());
    }

    void recurringCompletion()
    {
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, u"- [ ] Podlać kwiaty 🔁 every week on Saturday 📅 2026-10-17\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-15"_s);
        QSignalSpy changed(&actions, &PlanningActions::taskChanged);
        QVERIFY(actions.toggleTask(u"Dom.md"_s, 0, u"Podlać kwiaty"_s));
        const QStringList lines = host.file(u"Dom.md"_s).split(u'\n');
        QCOMPARE(lines.at(0), u"- [ ] Podlać kwiaty 🔁 every week on Saturday 📅 2026-10-24"_s);
        QVERIFY(lines.at(1).startsWith(u"- [x] Podlać kwiaty"_s));
        QCOMPARE(changed.first().at(2).toString(), u"done"_s);
    }

    void appendBlockToPage()
    {
        FakeEditorHost host;
        host.setFile(u"journals/2026-10-16.md"_s, u"# Piątek\r\n- \r\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        QVERIFY(actions.appendBlock(u"journals/2026-10-16.md"_s, actions.taskLine(u"Kupić farbę"_s, u"2026-10-16"_s)));
        QCOMPARE(host.file(u"journals/2026-10-16.md"_s), u"# Piątek\r\n- [ ] Kupić farbę 📅 2026-10-16\r\n"_s);
        // A missing page is created.
        QVERIFY(actions.appendBlock(u"journals/2026-10-17.md"_s, u"- Pierwszy wpis"_s));
        QCOMPARE(host.file(u"journals/2026-10-17.md"_s), u"- Pierwszy wpis\n"_s);
        QVERIFY(!actions.appendBlock(u"journals/2026-10-17.md"_s, u"   "_s));
        QVERIFY(actions.canUndo());
    }

    void appendBlockPure_data()
    {
        QTest::addColumn<QString>("content");
        QTest::addColumn<QString>("block");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty page") << QString() << u"- a"_s << u"- a\n"_s;
        QTest::newRow("no final newline") << u"- x"_s << u"- a"_s << u"- x\n- a\n"_s;
        QTest::newRow("trailing blank lines") << u"- x\n\n\n"_s << u"- a"_s << u"- x\n- a\n"_s;
        QTest::newRow("template bullet") << u"## Plan\n- \n"_s << u"- a"_s << u"## Plan\n- a\n"_s;
        QTest::newRow("crlf kept") << u"- x\r\n"_s << u"- a"_s << u"- x\r\n- a\r\n"_s;
        QTest::newRow("continuation lines") << u"- x\n"_s << u"- a\nb\n c"_s << u"- x\n- a\n  b\n  c\n"_s;
        QTest::newRow("paragraph block") << u"Tekst\n"_s << u"Akapit\ndalej"_s << u"Tekst\nAkapit\ndalej\n"_s;
        QTest::newRow("only whitespace") << u"- x\n"_s << u"\n  \n"_s << u"- x\n"_s;
        QTest::newRow("diacritics") << u"- zażółć\n"_s << u"- gęślą jaźń"_s << u"- zażółć\n- gęślą jaźń\n"_s;
    }

    void appendBlockPure()
    {
        QFETCH(QString, content);
        QFETCH(QString, block);
        QFETCH(QString, expected);
        QCOMPARE(brulion::planning::appendBlock(content, block), expected);
    }

    void lines()
    {
        PlanningActions actions;
        QCOMPARE(actions.taskLine(u"  Kupić mleko \n i chleb "_s), u"- [ ] Kupić mleko i chleb"_s);
        QCOMPARE(actions.taskLine(u"- [ ] Już zadanie"_s, u"2026-10-16"_s), u"- [ ] Już zadanie 📅 2026-10-16"_s);
        QCOMPARE(actions.taskLine(u"Bez daty"_s, u"bogus"_s), u"- [ ] Bez daty"_s);
        QCOMPARE(actions.reminderLine(u"Zadzwonić do mamy"_s, u"2026-10-16 18:00"_s),
                 u"- Zadzwonić do mamy ⏰ 2026-10-16 18:00"_s);
        QCOMPARE(actions.reminderLine(u"Pobudka"_s, u"2026-10-16 05:45"_s, true, u"every weekday"_s),
                 u"- Pobudka ⏰! 2026-10-16 05:45 🔁 every weekday"_s);
        // A date without a time takes the vault's default reminder time.
        QCOMPARE(actions.reminderLine(u"Czynsz"_s, u"2026-11-10"_s), u"- Czynsz ⏰ 2026-11-10 09:00"_s);
    }

    void quickAdd_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("line");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("when");
        // Thursday 15 October 2026, 10:00.
        QTest::newRow("tomorrow at 9 with a tag") << u"Zadzwonić do Ani jutro o 9 #praca"_s << u"pl"_s
                                                  << u"- [ ] Zadzwonić do Ani #praca ⏰ 2026-10-16 09:00"_s
                                                  << u"reminder"_s << u"jutro 09:00"_s;
        QTest::newRow("weekday without time") << u"Oddać książkę w piątek"_s << u"pl"_s
                                              << u"- [ ] Oddać książkę 📅 2026-10-16"_s << u"due"_s << u"jutro"_s;
        QTest::newRow("explicit date") << u"Przegląd auta 20.10"_s << u"pl"_s << u"- [ ] Przegląd auta 📅 2026-10-20"_s
                                       << u"due"_s << u"wt 20 paź"_s;
        QTest::newRow("no date") << u"Kupić chleb"_s << u"pl"_s << u"- [ ] Kupić chleb"_s << u"task"_s << QString();
        QTest::newRow("past stays text") << u"Zapłacić za wczoraj"_s << u"pl"_s << u"- [ ] Zapłacić za wczoraj"_s
                                         << u"task"_s << QString();
        QTest::newRow("only a date") << u"jutro"_s << u"pl"_s << u"- [ ] jutro"_s << u"task"_s << QString();
        QTest::newRow("typed checkbox") << u"- [ ] Podlać kwiaty pojutrze"_s << u"pl"_s
                                        << u"- [ ] Podlać kwiaty 📅 2026-10-17"_s << u"due"_s << u"sob 17 paź"_s;
        QTest::newRow("english") << u"Call Ann tomorrow at 9am"_s << u"en"_s << u"- [ ] Call Ann ⏰ 2026-10-16 09:00"_s
                                 << u"reminder"_s << u"tomorrow 09:00"_s;
    }

    void quickAdd()
    {
        QFETCH(QString, input);
        QFETCH(QString, locale);
        QFETCH(QString, line);
        QFETCH(QString, kind);
        QFETCH(QString, when);
        PlanningActions actions;
        actions.setNow(u"2026-10-15 10:00"_s);
        actions.setLocale(locale);
        const QVariantMap r = actions.parseQuickAdd(input);
        QVERIFY(r.value(u"ok"_s).toBool());
        QCOMPARE(r.value(u"line"_s).toString(), line);
        QCOMPARE(r.value(u"kind"_s).toString(), kind);
        QCOMPARE(r.value(u"when"_s).toString(), when);
    }

    void quickAddEmpty()
    {
        PlanningActions actions;
        QVERIFY(!actions.parseQuickAdd(u"   "_s).value(u"ok"_s).toBool());
        QVERIFY(!actions.parseQuickAdd(u"- [ ] "_s).value(u"ok"_s).toBool());
    }

    void openLinks()
    {
        FakeEditorHost host;
        host.setFile(u"Projekty/Dom.md"_s, u"# Dom\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        QVERIFY(actions.openLink(u"page:Dom#%5Eabc123"_s, u"Zadania.md"_s));
        QCOMPARE(host.lastRoute().value(u"view"_s).toString(), u"page"_s);
        QCOMPARE(host.lastRoute().value(u"path"_s).toString(), u"Projekty/Dom.md"_s);
        QCOMPARE(host.lastRoute().value(u"blockId"_s).toString(), u"abc123"_s);
        // A page that does not exist yet: the host resolves the target.
        QVERIFY(actions.openLink(u"page:Nowa%20strona"_s, u"Zadania.md"_s));
        QCOMPARE(host.lastRoute().value(u"target"_s).toString(), u"Nowa strona"_s);
        QVERIFY(actions.openLink(u"tag:praca%2Fpilne"_s));
        QCOMPARE(host.lastRoute().value(u"view"_s).toString(), u"tag"_s);
        QCOMPARE(host.lastRoute().value(u"tag"_s).toString(), u"praca/pilne"_s);
        QVERIFY(actions.openLink(u"block:xyz789"_s));
        QCOMPARE(host.lastRoute().value(u"blockId"_s).toString(), u"xyz789"_s);
        QVERIFY(!actions.openLink(u"javascript:alert(1)"_s));
        QVERIFY(!actions.openLink(u"nonsense"_s));
    }

    void plainTextHelper()
    {
        using brulion::planning::plainText;
        QCOMPARE(plainText(u"Zobacz [[Projekty/Dom#Plan|plan domu]] i [[Książki 2026]]"_s),
                 u"Zobacz plan domu i Książki 2026"_s);
        QCOMPARE(plainText(u"**Ważne** ==zaznaczone== `kod` [tekst](https://x.pl)"_s), u"Ważne zaznaczone kod tekst"_s);
        QCOMPARE(plainText(u"  wiele    spacji\n i linii "_s), u"wiele spacji i linii"_s);
    }
};

QTEST_MAIN(TestPlanningActions)
#include "tst_planning_actions.moc"
