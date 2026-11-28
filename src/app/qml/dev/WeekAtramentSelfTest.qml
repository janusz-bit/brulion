pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// WeekAtramentSelfTest — dev: the Calendar view's week drawn by Atrament (docs/ATRAMENT.md "The views
// in Atrament (M3)"; qml/planning/CalendarWeekAtramentGrid.qml over views/atrament/weekview.h) in the
// real app, headless (`brulion --demo --route '{"view":"calendar","mode":"week"}' --editor-script
// week-atrament-selftest --atrament-views`, ctest tst_views_qml_atrament_week; CalendarView loads it
// when its week is Atrament's). With real keys and clicks through the window: tomorrow selected by
// a click on its head, → moving the selection, PageDown / PageUp another week and back, a double
// click on its head opening its daily note.
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
    property string tomorrow: DatesHelper.addDays(DatesHelper.today, 1)
    property string firstDay
    function rowOf(date: string): int { return test.calendar.rowForDate(date) }

    function runSteps(): void {
        if (test.at())
            test.waitFor(test.rowOf(test.tomorrow) >= 0 && test.partRect(test.rowOf(test.tomorrow), "head").height > 0)
        if (test.at()) {
            test.check(QmlCall.object(test.calendarView, "atramentWeek") === test.list,
                       "the week is Atrament's (" + test.calendar.mode + ")")
            test.firstDay = test.calendar.rangeStart
            const r = test.partRect(test.rowOf(test.tomorrow), "head")
            events.mouseClick(test.list, r.x + r.width / 2, r.y + 10, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate === test.tomorrow)
        if (test.at()) {
            test.check(test.calendar.anchorDate === test.tomorrow, "a click on a day's head selects it: " + test.calendar.anchorDate)
            test.list.forceActiveFocus()
            events.keyClick(Qt.Key_Right, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate === DatesHelper.addDays(test.tomorrow, 1))
        if (test.at()) {
            test.check(test.calendar.anchorDate === DatesHelper.addDays(test.tomorrow, 1), "→ moves the selected day")
            events.keyClick(Qt.Key_PageDown, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.rangeStart !== test.firstDay)
        if (test.at()) {
            test.check(test.calendar.rangeStart === DatesHelper.addDays(test.firstDay, 7), "PageDown shows the next week: " + test.calendar.rangeStart)
            events.keyClick(Qt.Key_PageUp, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.rangeStart === test.firstDay && test.partRect(test.rowOf(test.tomorrow), "head").height > 0)
        if (test.at()) {
            test.check(test.calendar.rangeStart === test.firstDay, "PageUp comes back")
            const r = test.partRect(test.rowOf(test.tomorrow), "head")
            const x = 0 + r.x + r.width / 2
            const y = 0 + r.y + 10
            // (Two clicks at once: a double click.)
            events.mouseClick(test.list, x, y, Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseClick(test.list, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(AppController.currentView !== "calendar")
        if (test.at())
            test.check(AppController.currentView !== "calendar", "a double click on a day's head opens its daily note: " + AppController.currentView)
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
