pragma ComponentBehavior: Bound

import QtQuick
import Brulion

// TasksAtramentSelfTest — dev: the Tasks view's list drawn by Atrament (docs/ATRAMENT.md "The views
// in Atrament (M3)"; qml/planning/TasksAtramentList.qml over views/atrament/tasksview.h) in the real
// app, headless (`brulion --demo --route tasks --editor-script tasks-atrament-selftest
// --atrament-views`, ctest tst_views_qml_atrament; TasksView loads it when its rows are Atrament's).
// With real clicks and keys through the window: the rows drawn by Atrament (the ListView keeping only
// its header), a task's box ticking the task off in its file and again opening it, a right-click
// opening the task's status menu, a section's header folding it, ↓ making the first task current,
// Space ticking it off (the task at its place current then), Enter opening its page.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Registered only where Atrament is built (it names AtramentView).
Item {
    id: test

    /// TasksView's parts: the view itself (its functions and menus), its model, the list drawn by
    /// Atrament.
    property Item tasksView
    property TaskListModel model
    property AtramentView list

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true

    property int taskRow: -1
    property string taskPath: ""
    property int taskLine: -1
    property string taskText: ""
    property string section: ""
    property rect _rect: Qt.rect(0, 0, 0, 0)
    property list<string> fileLines: []

    DevEvents { id: events }
    MapHolder { id: rowInfo }

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
    function rowType(row: int): string {
        rowInfo.map = test.model.get(row)
        return AppController.routeText(rowInfo.map, "rowType")
    }
    function rowText(row: int, key: string): string {
        rowInfo.map = test.model.get(row)
        return AppController.routeText(rowInfo.map, key)
    }
    /// The first open task's row (−1: none).
    function firstOpenTask(): int {
        for (let r = 0; r < test.model.rowCount(); ++r)
            if (test.rowType(r) === "task" && test.rowText(r, "status") === "todo")
                return r
        return -1
    }
    /// Shows `row` and says whether its part is drawn.
    function shown(row: int, part: string): bool {
        if (test.partRect(row, part).width > 0)
            return true
        test.list.viewRevealRow(row)
        return false
    }
    function partRect(row: int, part: string): rect {
        test._rect = test.list.viewPartRect(row, part)
        return test._rect
    }
    function click(row: int, part: string, button: int): void {
        const r = test.partRect(row, part)
        events.mouseClick(test.list, r.x + Math.min(r.width / 2, 40), r.y + r.height / 2, button, Qt.NoModifier, -1)
    }
    /// The task's line in its file is ticked off (`done`) or open.
    function lineIs(done: bool): bool {
        test.fileLines = Js.split(Workspace.readFile(test.taskPath), "\n")
        return test.taskLine >= 0 && test.taskLine < test.fileLines.length
               && Js.indexOf(test.fileLines[test.taskLine], done ? "[x]" : "[ ]") >= 0
    }
    function menuOpen(): bool {
        const menu = QmlCall.object(test.tasksView, "atramentMenu")
        return menu !== null && QmlCall.isTrue(QmlCall.get(menu, "opened"))
    }

    /// The section is folded in the model.
    function collapsed(section: string): bool {
        const sections = test.model.collapsedSections
        for (let i = 0; i < sections.length; ++i)
            if (sections[i] === section)
                return true
        return false
    }

    function runSteps(): void {
        if (test.at())
            test.waitFor(test.list !== null && test.model.rowCount() > 0 && test.partRect(0, "row").height > 0)
        if (test.at()) {
            const listView = QmlCall.object(test.tasksView, "listView")
            test.check(listView === null || QmlCall.integer(QmlCall.get(listView, "count")) === 0,
                       "the ListView keeps only its header: its rows are Atrament's")
            test.taskRow = test.firstOpenTask()
            test.check(test.taskRow > 0, "the demo notebook has an open task, row " + test.taskRow)
            test.taskPath = test.rowText(test.taskRow, "path")
            rowInfo.map = test.model.get(test.taskRow)
            test.taskLine = AppController.routeInt(rowInfo.map, "line", -1)
            test.taskText = AppController.routeText(rowInfo.map, "text")
            test.check(test.partRect(test.taskRow, "box").width > 0, "its box is drawn: " + test._rect.width + " px")
        }
        if (test.at())
            test.click(test.taskRow, "box", Qt.LeftButton)
        if (test.at())
            test.waitFor(test.lineIs(true))
        if (test.at()) {
            test.check(test.lineIs(true), "a click on the box ticks the task off in its file")
            // (Done is folded by default: unfolded, the task shows there.)
            if (test.collapsed("done"))
                test.model.toggleSection("done")
        }
        if (test.at())
            test.waitFor(test.shown(test.model.rowOfKey(test.taskPath + ":" + test.taskLine), "box"))
        if (test.at())
            test.click(test.model.rowOfKey(test.taskPath + ":" + test.taskLine), "box", Qt.LeftButton)
        if (test.at())
            test.waitFor(test.lineIs(false))
        if (test.at()) {
            test.check(test.lineIs(false), "a click on the ticked box opens the task again")
            if (!test.collapsed("done"))
                test.model.toggleSection("done")
        }
        // ---- The task's menu --------------------------------------------------------------------
        if (test.at()) {
            test.taskRow = test.model.rowOfKey(test.taskPath + ":" + test.taskLine)
            test.waitFor(test.shown(test.taskRow, "row"))
        }
        if (test.at())
            test.click(test.taskRow, "row", Qt.RightButton)
        if (test.at())
            test.waitFor(test.menuOpen())
        if (test.at()) {
            test.check(test.menuOpen(), "a right-click on the task opens its status menu")
            QmlCall.call(QmlCall.object(test.tasksView, "atramentMenu"), "close")
        }
        if (test.at())
            test.waitFor(!test.menuOpen())
        // ---- A section folded -------------------------------------------------------------------
        if (test.at())
            test.waitFor(test.shown(0, "row"))
        if (test.at()) {
            test.section = test.rowText(0, "section")
            test.click(0, "row", Qt.LeftButton)
        }
        if (test.at())
            test.waitFor(test.collapsed(test.section))
        if (test.at()) {
            test.check(test.collapsed(test.section), "a click on a header folds its section: " + test.section)
            test.click(0, "row", Qt.LeftButton)
        }
        if (test.at())
            test.waitFor(!test.collapsed(test.section))
        // ---- Keys ---------------------------------------------------------------------------------
        if (test.at()) {
            test.list.forceActiveFocus()
            events.keyClick(Qt.Key_Down, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.list.viewCurrentRow() >= 0)
        if (test.at()) {
            test.taskRow = test.list.viewCurrentRow()
            test.check(test.rowType(test.taskRow) === "task", "↓ makes the first task current: row " + test.taskRow)
            rowInfo.map = test.model.get(test.taskRow)
            test.taskPath = AppController.routeText(rowInfo.map, "path")
            test.taskLine = AppController.routeInt(rowInfo.map, "line", -1)
            events.keyClick(Qt.Key_Space, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.lineIs(true))
        if (test.at()) {
            test.check(test.lineIs(true), "Space ticks the current task off in its file")
            // (It went into Done, folded: the task now at its place is current.)
            test.check(test.list.viewCurrentRow() >= 0 && test.rowType(test.list.viewCurrentRow()) === "task",
                       "the task now at its place is current: row " + test.list.viewCurrentRow())
            test.list.forceActiveFocus()
            events.keyClick(Qt.Key_Return, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(AppController.currentView !== "tasks")
        if (test.at())
            test.check(AppController.currentView !== "tasks", "Enter opens the current task's page: " + AppController.currentView)
    }

    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.list !== null && test.model !== null
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
