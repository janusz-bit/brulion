pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// AgendaAtramentSelfTest — dev: the Calendar view's agenda drawn by Atrament (docs/ATRAMENT.md "The
// views in Atrament (M3)"; qml/planning/CalendarAgendaAtramentList.qml over views/atrament/
// agendaview.h) in the real app, headless (`brulion --demo --route '{"view":"calendar","mode":
// "agenda"}' --editor-script agenda-atrament-selftest --atrament-views`, ctest
// tst_views_qml_atrament_agenda; CalendarView loads it when its agenda is Atrament's). With real
// keys and clicks through the window: tomorrow's task ticked off in its file by its box, a day
// picked by its header (the selection follows), ↓ and Enter picking the next day, a day's
// "Journal" button opening its daily note.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Registered only where Atrament is built (it names AtramentView).
Item {
    id: test

    /// CalendarView's parts: the view itself, its model, the agenda drawn by Atrament.
    final property Item calendarView
    final property CalendarModel calendar
    final property AtramentView list

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true
    property rect _rect: Qt.rect(0, 0, 0, 0)

    /// Tomorrow's task (the demo's "scheduled for tomorrow"): its day's row, its entry, its file.
    property string tomorrow: DatesHelper.addDays(DatesHelper.today, 1)
    property int taskRow: -1
    property int taskEntry: -1
    property string taskPath
    property int taskLine: -1
    property list<string> fileLines: []
    property list<var> dayList: []

    DevEvents { id: events }

    function check(ok: bool, what: string): void {
        if (!ok)
            test.failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    function at(): bool {
        return test.stepCursor++ === test.stepTarget
    }
    function waitFor(ready: bool): void {
        test.stepDone = ready
    }
    function partRect(row: int, part: string): rect {
        test._rect = test.list.viewPartRect(row, part)
        return test._rect
    }
    function click(row: int, part: string): void {
        const r = test.partRect(row, part)
        events.mouseClick(test.list, r.x + Math.min(r.width / 2, 12), r.y + r.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
    }
    /// Shown: the part drawn (else asked into view).
    function shown(row: int, part: string): bool {
        if (test.partRect(row, part).height > 0)
            return true
        test.list.viewRevealRow(row)
        return false
    }
    /// Tomorrow's first task: its row, its entry's index, its file and line.
    MapHolder { id: entryInfo }
    function findTask(): bool {
        test.taskRow = test.calendar.rowForDate(test.tomorrow)
        test.dayList = test.calendar.dayItems(test.tomorrow)
        for (let i = 0; i < test.dayList.length; ++i) {
            entryInfo.map = test.dayList[i]
            if (AppController.routeFlag(entryInfo.map, "isTask")) {
                test.taskEntry = i
                test.taskPath = "" + AppController.routeText(entryInfo.map, "path")
                test.taskLine = 0 + AppController.routeInt(entryInfo.map, "line", -1)
                return test.taskRow >= 0
            }
        }
        return false
    }
    /// The task's line in its file is ticked off (`done`) or open.
    function lineIs(done: bool): bool {
        test.fileLines = Js.split(Workspace.readFile(test.taskPath), "\n")
        return test.taskLine >= 0 && test.taskLine < test.fileLines.length
               && Js.indexOf(test.fileLines[test.taskLine], done ? "[x]" : "[ ]") >= 0
    }

    /// The first day with a daily note (−1: none).
    function journalRow(): int {
        const count = test.calendar.rowCount()
        for (let r = 0; r < count; ++r)
            if (test.calendar.data(test.calendar.index(r, 0), CalendarModel.HasJournalRole) === true)
                return r
        return -1
    }

    function runSteps(): void {
        if (test.at())
            test.waitFor(test.findTask() && test.shown(test.taskRow, "box " + test.taskEntry))
        if (test.at()) {
            test.check(QmlCall.object(test.calendarView, "atramentAgenda") === test.list,
                       "the agenda is Atrament's (" + test.calendar.mode + ")")
            test.check(test.lineIs(false), "tomorrow's task is open in its file: " + test.taskPath + ":" + test.taskLine)
            test.click(test.taskRow, "box " + test.taskEntry)
        }
        if (test.at())
            test.waitFor(test.lineIs(true))
        if (test.at()) {
            test.check(test.lineIs(true), "a click on the box ticks tomorrow's task off in its file")
            test.click(test.taskRow, "header")
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate === test.tomorrow)
        if (test.at()) {
            test.check(test.calendar.anchorDate === test.tomorrow, "a click on a day's header picks it: " + test.calendar.anchorDate)
            test.list.forceActiveFocus()
            events.keyClick(Qt.Key_Down, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.list.viewCurrentRow() === test.taskRow + 1)
        if (test.at()) {
            test.check(test.list.viewCurrentRow() === test.taskRow + 1, "↓ makes the next day current")
            events.keyClick(Qt.Key_Return, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate !== test.tomorrow)
        if (test.at()) {
            test.check(test.calendar.rowForDate(test.calendar.anchorDate) === test.taskRow + 1,
                       "Enter picks the current day: " + test.calendar.anchorDate)
        }
        if (test.at())
            test.waitFor(test.journalRow() >= 0 && test.shown(test.journalRow(), "journal"))
        if (test.at())
            test.click(test.journalRow(), "journal")
        if (test.at())
            test.waitFor(AppController.currentView !== "calendar")
        if (test.at())
            test.check(AppController.currentView !== "calendar", "the Journal button opens the daily note: " + AppController.currentView)
    }

    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.list !== null && test.calendar !== null
        onTriggered: {
            if (test.inStep)
                return
            test.inStep = true
            test.stepTarget = test.stepIndex
            test.stepCursor = 0
            test.stepDone = true
            const error = DevTest.callCatching(test, "runSteps")
            test.inStep = false
            if (error !== "")
                test.check(false, "step " + test.stepIndex + " threw: " + error)
            if (test.stepCursor <= test.stepTarget) {
                runner.stop()
                console.log("SELFTEST DONE failures=" + test.failures)
                Qt.exit(test.failures === 0 ? 0 : 1)
                return
            }
            if (!test.stepDone && ++test.waited < 80)
                return
            if (!test.stepDone)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }
}
