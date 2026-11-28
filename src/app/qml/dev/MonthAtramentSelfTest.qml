pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// MonthAtramentSelfTest — dev: the Calendar view's month drawn by Atrament (docs/ATRAMENT.md "The
// views in Atrament (M3)"; qml/planning/CalendarMonthAtramentGrid.qml over views/atrament/
// monthview.h) in the real app, headless (`brulion --demo --route '{"view":"calendar","mode":
// "month"}' --editor-script month-atrament-selftest --atrament-views`, ctest
// tst_views_qml_atrament_month; CalendarView loads it when its month is Atrament's). With real keys
// and clicks through the window: a day selected by a click, → moving it, PageDown / PageUp another
// month and back, a pill of tomorrow's opening its entry.
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
    /// The rows of today and tomorrow, the month shown first.
    property string tomorrow: DatesHelper.addDays(DatesHelper.today, 1)
    /// The day panel drawn by Atrament (CalendarView's side column).
    property AtramentView panel: null
    property rect _panelRect: Qt.rect(0, 0, 0, 0)
    function panelPart(part: string): rect {
        if (test.panel === null)
            return test._rect
        test._panelRect = test.panel.viewPartRect(0, part)
        return test._panelRect
    }
    property int firstMonth: 0
    function rowOf(date: string): int { return test.calendar.rowForDate(date) }

    function runSteps(): void {
        if (test.at())
            test.waitFor(test.rowOf(test.tomorrow) >= 0 && test.partRect(test.rowOf(test.tomorrow), "cell").height > 0)
        if (test.at()) {
            test.check(QmlCall.object(test.calendarView, "atramentMonth") === test.list,
                       "the month is Atrament's (" + test.calendar.mode + ")")
            test.firstMonth = test.calendar.displayMonth
            const r = test.partRect(test.rowOf(test.tomorrow), "cell")
            events.mouseClick(test.list, r.x + r.width / 2, r.y + r.height - 6, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate === test.tomorrow)
        if (test.at()) {
            test.check(test.calendar.anchorDate === test.tomorrow, "a click selects the day: " + test.calendar.anchorDate)
            test.panel = QmlCall.object(test.calendarView, "atramentPanel") as AtramentView
            test.check(test.panel !== null && QmlCall.integer(QmlCall.get(test.panel, "width")) > 0,
                       "the side column is the day panel drawn by Atrament")
            test.list.forceActiveFocus()
            events.keyClick(Qt.Key_Right, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.anchorDate === DatesHelper.addDays(test.tomorrow, 1)
                         && test.panelPart("plan").height > 0 && test.panelPart("entry 0").height > 0)
        if (test.at()) {
            test.check(test.calendar.anchorDate === DatesHelper.addDays(test.tomorrow, 1), "→ moves the selected day")
            test.check(test.panelPart("entry 0").height > 0, "the panel shows the selected day's plan")
            events.keyClick(Qt.Key_PageDown, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.displayMonth !== test.firstMonth)
        if (test.at()) {
            test.check(test.calendar.displayMonth === test.firstMonth % 12 + 1, "PageDown shows the next month")
            events.keyClick(Qt.Key_PageUp, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.calendar.displayMonth === test.firstMonth && test.partRect(test.rowOf(test.tomorrow), "pill 0").height > 0)
        if (test.at()) {
            test.check(test.calendar.displayMonth === test.firstMonth, "PageUp comes back")
            const p = test.partRect(test.rowOf(test.tomorrow), "pill 0")
            events.mouseClick(test.list, p.x + p.width / 2, p.y + p.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(AppController.currentView !== "calendar")
        if (test.at())
            test.check(AppController.currentView !== "calendar", "a pill opens its entry: " + AppController.currentView)
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
