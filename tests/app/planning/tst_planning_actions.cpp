// PlanningActions and the pure helpers behind it: toggling tasks through an EditorHost (✅ date,
// recurring tasks, a line that moved, a task that is gone, undo only while the file is
// unchanged), every status of the status menu (no status included), appending blocks to a page
// (template bullets, CRLF, continuation lines), dates added from Tasks' quick date menu, the lines written by the
// calendar ("Dodaj zadanie" / "Dodaj przypomnienie") and natural-language quick add (Polish and English).
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

    // The timer (one at a time, across files), "Add dependency…" (🆔 written to the other task)
    // and the status history setting of the host.
    void clockDependencyAndHistory()
    {
        FakeEditorHost host;
        host.setFile(u"A.md"_s, u"- [ ] Pierwsze\n- [ ] Drugie\n"_s);
        host.setFile(u"B.md"_s, u"- [ ] W innym pliku\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setVaultIndex(const_cast<brulion::VaultIndex *>(host.taskIndex()));
        actions.setNow(u"2026-10-15 10:00"_s);
        QVERIFY(actions.toggleClock(u"B.md"_s, 0, u"W innym pliku"_s));
        QCOMPARE(host.file(u"B.md"_s),
                 u"- [ ] W innym pliku\n  :LOGBOOK:\n  CLOCK: [2026-10-15 Thu 10:00:00]\n  :END:\n"_s);
        actions.setVaultIndex(const_cast<brulion::VaultIndex *>(host.taskIndex())); // rebuilt after the write
        actions.setNow(u"2026-10-15 10:30"_s);
        QVERIFY(actions.toggleClock(u"A.md"_s, 1, u"Drugie"_s)); // the other one stops
        QVERIFY(host.file(u"B.md"_s).contains(u"--[2026-10-15 Thu 10:30:00] =>  00:30:00"_s));
        QVERIFY(host.file(u"A.md"_s).contains(u"- [ ] Drugie\n  :LOGBOOK:\n  CLOCK: [2026-10-15 Thu 10:30:00]\n"_s));
        QVERIFY(actions.undo()); // the task's own file
        QCOMPARE(host.file(u"A.md"_s), u"- [ ] Pierwsze\n- [ ] Drugie\n"_s);

        // A dependency on a task of another file: it gets an id.
        actions.setVaultIndex(const_cast<brulion::VaultIndex *>(host.taskIndex()));
        const QVariantList candidates = actions.dependencyCandidates(u"A.md"_s, 0, u"inny"_s);
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first().toMap().value(u"path"_s).toString(), u"B.md"_s);
        QVERIFY(actions.addDependency(u"A.md"_s, 0, u"Pierwsze"_s, u"B.md"_s, 0));
        const QString b = host.file(u"B.md"_s);
        const QString id = b.section(u"🆔 "_s, 1).left(6);
        QCOMPARE(id.size(), 6);
        QCOMPARE(host.file(u"A.md"_s), u"- [ ] Pierwsze ⛔ "_s + id + u"\n- [ ] Drugie\n"_s);
        // In the same file.
        actions.setVaultIndex(const_cast<brulion::VaultIndex *>(host.taskIndex()));
        QVERIFY(actions.addDependency(u"A.md"_s, 1, u"Drugie"_s, u"A.md"_s, 0));
        QVERIFY(host.file(u"A.md"_s).contains(u"- [ ] Drugie ⛔ "_s));
        QVERIFY(host.file(u"A.md"_s).startsWith(u"- [ ] Pierwsze 🆔 "_s));
        // Not on itself.
        QVERIFY(!actions.addDependency(u"A.md"_s, 1, u"Drugie"_s, u"A.md"_s, 1));

        // The status history: logged, "In progress" starts the clock.
        host.setFile(u"C.md"_s, u"- [ ] Historia\n"_s);
        host.setTaskStatusHistory(true);
        actions.setNow(u"2026-10-15 11:00"_s);
        QVERIFY(actions.setTaskStatus(u"C.md"_s, 0, u"doing"_s, u"Historia"_s));
        QCOMPARE(host.file(u"C.md"_s),
                 u"- [/] Historia\n  :LOGBOOK:\n  CLOCK: [2026-10-15 Thu 11:00:00]\n"
                 "  State \"DOING\"      from \"TODO\"       [2026-10-15 Thu 11:00]\n  :END:\n"_s);
        actions.setNow(u"2026-10-15 11:20"_s);
        QVERIFY(actions.toggleTask(u"C.md"_s, 0, u"Historia"_s));
        QVERIFY(host.file(u"C.md"_s).contains(
                u"CLOCK: [2026-10-15 Thu 11:00:00]--[2026-10-15 Thu 11:20:00] =>  00:20:00"_s));
        QVERIFY(host.file(u"C.md"_s).contains(u"State \"DONE\"       from \"DOING\"      [2026-10-15 Thu 11:20]"_s));
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

    // "Add date" (the quick date menu of Tasks): the token added to the task's first line (before
    // its block id, CRLF kept), found again when the line moved, replaced, removed, undone.
    void setTaskDate()
    {
        FakeEditorHost host;
        const QString page = u"- nowy wpis\r\n- [ ] Kupić żółty ser ^ser01\r\n  notatka\r\n"_s;
        host.setFile(u"Zakupy.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-11-10"_s);
        // The view still has the task on line 0.
        QVERIFY(actions.setTaskDate(u"Zakupy.md"_s, 0, u"Kupić żółty ser"_s, u"due"_s, u"2026-11-11"_s));
        QCOMPARE(host.file(u"Zakupy.md"_s),
                 u"- nowy wpis\r\n- [ ] Kupić żółty ser 📅 2026-11-11 ^ser01\r\n  notatka\r\n"_s);
        QVERIFY(actions.canUndo());
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Zakupy.md"_s), page);

        QVERIFY(actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić żółty ser"_s, u"scheduled"_s, u"2026-11-16 09:30"_s));
        QCOMPARE(host.file(u"Zakupy.md"_s),
                 u"- nowy wpis\r\n- [ ] Kupić żółty ser ⏳ 2026-11-16 09:30 ^ser01\r\n  notatka\r\n"_s);
        QVERIFY(actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić żółty ser"_s, u"scheduled"_s, u"2026-11-17"_s));
        QCOMPARE(host.file(u"Zakupy.md"_s),
                 u"- nowy wpis\r\n- [ ] Kupić żółty ser ⏳ 2026-11-17 ^ser01\r\n  notatka\r\n"_s);
        QVERIFY(actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić żółty ser"_s, u"scheduled"_s, QString()));
        QCOMPARE(host.file(u"Zakupy.md"_s), page);

        // Malformed requests write nothing; a task that is gone fails.
        QVERIFY(!actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić żółty ser"_s, u"reminder"_s, u"2026-11-11"_s));
        QVERIFY(!actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić żółty ser"_s, u"due"_s, u"jutro"_s));
        QSignalSpy failed(&actions, &PlanningActions::failed);
        QVERIFY(!actions.setTaskDate(u"Zakupy.md"_s, 1, u"Kupić mleko"_s, u"due"_s, u"2026-11-11"_s));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(host.file(u"Zakupy.md"_s), page);
    }

    // "Add date" → "Pick a date…" switched to "Reminder" / "Alarm": a reminder appended to the
    // task's first line (before its block id, CRLF kept), next to one it has; undone.
    void addTaskReminder()
    {
        FakeEditorHost host;
        const QString page = u"- nowy wpis\r\n- [ ] Odebrać paczkę ⏰ 2026-11-11 08:00 ^pak1\r\n  notatka\r\n"_s;
        host.setFile(u"Poczta.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-11-10"_s);
        QVERIFY(actions.addTaskReminder(u"Poczta.md"_s, 0, u"Odebrać paczkę"_s, u"2026-11-12 17:30"_s, true));
        QCOMPARE(host.file(u"Poczta.md"_s),
                 u"- nowy wpis\r\n- [ ] Odebrać paczkę ⏰ 2026-11-11 08:00 ⏰! 2026-11-12 17:30 ^pak1\r\n  notatka\r\n"_s);
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Poczta.md"_s), page);
        QVERIFY(actions.addTaskReminder(u"Poczta.md"_s, 1, u"Odebrać paczkę"_s, u"2026-11-12 17:30"_s, false));
        QCOMPARE(host.file(u"Poczta.md"_s),
                 u"- nowy wpis\r\n- [ ] Odebrać paczkę ⏰ 2026-11-11 08:00 ⏰ 2026-11-12 17:30 ^pak1\r\n  notatka\r\n"_s);
        QVERIFY(actions.undo());

        // No date, a task that is gone: nothing written.
        QVERIFY(!actions.addTaskReminder(u"Poczta.md"_s, 1, u"Odebrać paczkę"_s, u"jutro"_s, false));
        QSignalSpy failed(&actions, &PlanningActions::failed);
        QVERIFY(!actions.addTaskReminder(u"Poczta.md"_s, 1, u"Wysłać list"_s, u"2026-11-12 17:30"_s, false));
        QCOMPARE(failed.count(), 1);
        QCOMPARE(host.file(u"Poczta.md"_s), page);
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
        QVERIFY(actions.snoozeReminder(u"Dom.md"_s, 0, QString(), u"2026-10-10 08:00"_s, u"2026-10-15 09:00"_s));
        QCOMPARE(host.file(u"Dom.md"_s), u"- Leki ⏰ 2026-10-10 08:00 🔁 every day\n\t- ⏰ 2026-10-15 09:00\n"_s);
        QVERIFY(actions.undo());
        QCOMPARE(host.file(u"Dom.md"_s), page);
        QVERIFY(!actions.snoozeReminder(u"Dom.md"_s, 0, QString(), u"2026-10-10 08:00"_s, u"jutro"_s));
    }

    void snoozeFindsTheReminderAgain_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("expected"); // "" = nothing written
        const QString lek = u"- [ ] Lek 🔁 every day ⏰ 2026-10-27 09:00\n"_s;
        const QString faktura = u"- [ ] Faktura ⏰ 2026-10-28 15:00\n"_s;
        const QString child = u"\t- ⏰ 2026-10-29 10:00\n"_s;
        QTest::newRow("on its line") << QString(u"# Dom\n\n- x\n"_s + lek) << QString(u"# Dom\n\n- x\n"_s + lek + child);
        // A line was added above since the menu opened: line 3 is another task's one-off reminder.
        QTest::newRow("moved down, another reminder on the line")
                << QString(u"# Dom\n\n- x\n"_s + faktura + lek)
                << QString(u"# Dom\n\n- x\n"_s + faktura + lek + child);
        QTest::newRow("on a continuation line")
                << QString(u"# Dom\n\n- x\n- [ ] Lek 🔁 every day\n  ⏰ 2026-10-27 09:00\n"_s)
                << QString(u"# Dom\n\n- x\n- [ ] Lek 🔁 every day\n  ⏰ 2026-10-27 09:00\n"_s + child);
        QTest::newRow("gone: the one-off of another task stays") << QString(u"# Dom\n\n- x\n"_s + faktura) << QString();
        QTest::newRow("its time changed meanwhile")
                << QString(u"# Dom\n\n- x\n- [ ] Lek 🔁 every day ⏰ 2026-10-27 08:30\n"_s) << QString();
    }

    void snoozeFindsTheReminderAgain()
    {
        // The calendar's menu on another date of "Lek" keeps line 3; whatever the file holds by the
        // time "An hour later" is chosen, only that reminder is snoozed.
        QFETCH(QString, page);
        QFETCH(QString, expected);
        FakeEditorHost host;
        host.setFile(u"Dom.md"_s, page);
        PlanningActions actions;
        actions.setHost(&host);
        actions.setToday(u"2026-10-29"_s);
        QSignalSpy failed(&actions, &PlanningActions::failed);
        const bool ok = actions.snoozeReminder(u"Dom.md"_s, 3, u"Lek"_s, u"2026-10-27 09:00"_s, u"2026-10-29 10:00"_s);
        QCOMPARE(ok, !expected.isEmpty());
        QCOMPARE(host.file(u"Dom.md"_s), expected.isEmpty() ? page : expected);
        QCOMPARE(failed.size(), expected.isEmpty() ? 1 : 0);
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

    void missingLinks_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("missing");
        QTest::newRow("existing page") << u"page:Dom"_s << false;
        QTest::newRow("existing page, heading") << u"page:Dom#Plan"_s << false;
        QTest::newRow("existing page, Polish name") << u"page:Za%C5%BC%C3%B3%C5%82%C4%87"_s << false;
        QTest::newRow("no such page") << u"page:Notatki%20z%20wyk%C5%82adu%205"_s << true;
        QTest::newRow("no such page, heading") << u"page:Nowa%20strona#Plan"_s << true;
        QTest::newRow("tag") << u"tag:wa%C5%BCne"_s << false;
        QTest::newRow("block") << u"block:abc"_s << false;
        QTest::newRow("address") << u"ext:https://example.org"_s << false;
        QTest::newRow("unknown scheme") << u"date:1-2"_s << false;
    }

    void missingLinks()
    {
        QFETCH(QString, url);
        QFETCH(bool, missing);
        FakeEditorHost host;
        host.setFile(u"Projekty/Dom.md"_s, u"- x\n"_s);
        host.setFile(u"Zażółć.md"_s, u"- y\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        QCOMPARE(actions.linkMissing(url, u"Zadania.md"_s), missing);
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

    // A path part of a link in Tasks and the calendar (part:, docs/SYNTAX.md §2 "Path parts").
    void linkParts_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<QString>("target"); // linkPartTarget()
        QTest::addColumn<bool>("missing");
        QTest::addColumn<QString>("text"); // linkText()
        QTest::newRow("a page") << u"part:1:Firma%2FAdam"_s << u"page"_s << false << u"[[Firma]]"_s;
        QTest::newRow("a folder") << u"part:1:Projekty%2FZa%C5%BC%C3%B3%C5%82%C4%87"_s << u"folder"_s << false
                                  << u"Projekty/"_s;
        QTest::newRow("a new page") << u"part:1:Nowa%2FStrona"_s << u"new"_s << true << u"[[Nowa]]"_s;
        QTest::newRow("malformed") << u"part:2:Firma%2FAdam"_s << QString() << false << QString();
        QTest::newRow("a page link") << u"page:Firma"_s << QString() << false << u"[[Firma]]"_s;
    }
    void linkParts()
    {
        QFETCH(QString, url);
        QFETCH(QString, target);
        QFETCH(bool, missing);
        QFETCH(QString, text);
        FakeEditorHost host;
        host.setFile(u"Firma.md"_s, u"- firma\n"_s);
        host.setFile(u"Firma/Adam.md"_s, u"- Adam\n"_s);
        host.setFile(u"Projekty/Zażółć/Notatki.md"_s, u"- notatki\n"_s);
        PlanningActions actions;
        actions.setHost(&host);
        QCOMPARE(actions.linkPartTarget(url, u"journals/2026-10-14.md"_s), target);
        QCOMPARE(actions.linkMissing(url, u"journals/2026-10-14.md"_s), missing);
        QCOMPARE(actions.linkText(url, u"journals/2026-10-14.md"_s), text);
    }
    void openLinkParts()
    {
        FakeEditorHost host;
        PlanningActions actions;
        actions.setHost(&host);
        QSignalSpy navigated(&host, &FakeEditorHost::navigated);
        QVERIFY(actions.openLink(u"part:1:Firma%2FAdam%20co%C5%9Btam"_s, u"journals/2026-10-14.md"_s, true));
        QCOMPARE(navigated.size(), 1);
        QCOMPARE(navigated.first().at(1).toBool(), true);
        QCOMPARE(host.lastRoute().value(u"target"_s).toString(), u"Firma"_s);
        QCOMPARE(host.lastRoute().value(u"linkTarget"_s).toString(), u"Firma/Adam cośtam"_s);
        QCOMPARE(host.lastRoute().value(u"linkPart"_s).toInt(), 1);
        QCOMPARE(host.lastRoute().value(u"from"_s).toString(), u"journals/2026-10-14.md"_s);
        QVERIFY(!actions.openLink(u"part:0:Firma%2FAdam"_s));
        QVERIFY(!actions.openLink(u"part:1:Firma"_s));
        QCOMPARE(navigated.size(), 1);
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
