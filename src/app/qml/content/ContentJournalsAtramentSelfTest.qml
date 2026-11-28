pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalsAtramentSelfTest — dev: the journal stream drawn by Atrament as one list
// (docs/ATRAMENT.md "The journal stream as one list"; qml/editor/EditorAtramentJournal.qml) in the
// real app, headless (`brulion --demo --route journals --editor-script journals-selftest
// --atrament-editor`, ctest tst_content_journals_atrament_qml; JournalsView loads it in place of
// ContentJournalsSelfTest when its stream is Atrament's). The steps of the QML stream's self-test
// that matter for one list, with real keys, typed letters and taps through the window:
//   * today's prompt begins today (from the journal template), what is typed is saved, also once
//     another day is written in and today scrolled far away has gone, and shown when scrolled back;
//   * ↓ on a day's last line edits the next day's first block, ↑ goes back; Enter at a day's end
//     makes a block in that day, Backspace at the next day's start merges nothing;
//   * Esc and Shift+↓ past a day's end select blocks of two days (StreamSelection);
//   * the find bar's query reaches the stream (every day tints it), its matches counted in days;
//   * a zoom into a block of a day: its breadcrumb above the day's blocks (the page, the parent, the
//     block), a crumb clicked zooms out to it, Back (JournalsView.stepBack) zooms out;
//   * the view's state (the day at the top and the offset) saved and restored, "Go to day";
//   * only the days near the view are made (a stream scrolled to its end keeps a few);
//   * the touch toolbar on a simulated keyboard: docked on the stream's frame (one), staying put
//     while the stream scrolls, the caret of a block typed into near it kept above it.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Registered only where Atrament is built (it names AtramentView).
Item {
    id: test

    /// JournalsView's parts: the stream (the AtramentView), its editor, the days, the selection over
    /// several days, the find over them and JournalsView itself (viewState, stepBack, scrollToDate).
    property AtramentView stream
    property BlockEditor editor
    property JournalListModel days
    property StreamSelection selection
    property StreamFind streamFind
    property Item journalsView

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true

    property string todayPath: ""
    property int lastRow: -1
    property int blocksFirst: 0
    property int blocksSecond: 0
    property var savedState: ({})
    property int savedOffset: 0
    property string savedDate: ""
    property string expectedDate: ""
    property point _at: Qt.point(0, 0)
    property rect _rect: Qt.rect(0, 0, 0, 0)
    property Item _slot: null
    property Item _child: null
    property Item _frame: null
    property real slotY: 0
    property string crumbTitle: ""
    property int parentRow: -1
    property int childRow: -1

    DevEvents { id: events }
    DevBlock { id: block }
    MapHolder { id: stateInfo }

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
    /// Day `row`'s controller while the stream has it made (null: far from the view).
    function day(row: int): PageController {
        return test.stream.dayController(row) as PageController
    }
    function dayLoaded(row: int): bool {
        const c = test.day(row)
        return c !== null && c.loaded
    }
    function fileHas(path: string, text: string): bool {
        return Js.indexOf(Workspace.readFile(path), text) >= 0
    }
    function dayShows(row: int, needle: string): bool {
        const c = test.day(row)
        if (c === null)
            return false
        for (let i = 0; i < c.blocks.count; ++i) {
            block.read(c.blocks.get(i))
            if (Js.indexOf(block.plainText, needle) >= 0)
                return true
        }
        return false
    }
    /// A real key through the window (the stream has the keyboard).
    function key(k: int, modifiers: int): void {
        const code = k
        const mods = modifiers
        EditorInput.sendKey(test.stream, code, mods, "")
    }
    function typeKeys(s: string): void {
        const text = s
        for (let i = 0; i < text.length; ++i)
            EditorInput.sendKey(test.stream, 0, 0, text[i])
    }
    /// A tap (touch UIs) or a click at (x, y) of the stream.
    function tap(x: real, y: real): void {
        if (Theme.touchUi) {
            events.touchPress(test.stream, x, y)
            events.touchRelease(test.stream, x, y)
        } else {
            events.mouseClick(test.stream, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    /// A tap on block `row` of day `d`, a little into its text.
    function tapBlock(d: int, row: int, intoX: real): void {
        const top = test.stream.dayRowTop(d, row)
        const height = test.stream.dayRowHeight(d, row)
        const x = test.stream.contentX + test.stream.gutter + intoX
        test.tap(x, top + Math.min(height / 2, 12))
    }
    /// The breadcrumb over a day zoomed into a block (EditorAtramentJournal's dayHeader), or null.
    function crumbs(): Item {
        return DevTest.findShown(test.stream, "dayZoomBreadcrumb")
    }
    function editing(d: int, row: int): bool {
        const c = test.day(d)
        return c !== null && c.editingRow === row && test.stream.activeDay === d && test.stream.editRow === row
    }
    function writeDays(): void {
        for (let n = 2; n <= 60; ++n) {
            const path = "" + Workspace.journalPath("" + DatesHelper.addDays(DatesHelper.today, -n))
            if (Workspace.exists(path))
                continue
            let md = ""
            for (let b = 0; b < 6; ++b)
                md += "- Dzień " + n + ", notatka " + b + ": zażółć gęślą jaźń, mleko\n"
            Workspace.writeFile(path, md)
        }
    }

    // ---- The touch toolbar, docked on the stream's frame (the editor's dockHost) ---------------
    function visibleSlots(): int {
        test._slot = null
        test._frame = test.editor.dockHost
        let count = 0
        const n = DevTest.childCount(test._frame)
        for (let i = 0; i < n; ++i) {
            test._child = DevTest.childAt(test._frame, i)
            if (test._child.objectName === "editToolbarSlot" && test._child.visible) {
                if (count === 0)
                    test._slot = test._child
                count += 1
            }
        }
        return count
    }
    function slotTop(): real {
        if (test.visibleSlots() !== 1)
            return -1
        test._at = test._slot.mapToItem(null, 0, 0)
        return test._at.y
    }
    function slotBottom(): real {
        if (test.visibleSlots() !== 1)
            return -1
        test._at = test._slot.mapToItem(null, 0, test._slot.height)
        return test._at.y
    }
    function windowHeight(): real {
        return test.Window.window ? test.Window.window.height : 0
    }
    function near(a: real, b: real): bool {
        return Math.abs(a - b) <= 1
    }
    /// The bottom of the edited block's caret in the window (1e9: none).
    function caretBottom(): real {
        if (test.stream.editRow < 0)
            return 1e9
        test._rect = test.stream.editCaretRect
        test._at = test.stream.mapToItem(null, test._rect.x, test._rect.y + test._rect.height)
        return test._at.y
    }

    function runSteps(): void {
        runStepsToday()
        runStepsAcross()
        runStepsSelection()
        runStepsViews()
        runStepsToolbar()
    }
    function runStepsToday(): void {
        // ---- A notebook with two months of days ------------------------------------------------
        if (test.at())
            test.waitFor(Workspace.ready && test.days.count >= 2 && test.dayLoaded(0))
        if (test.at())
            test.writeDays()
        if (test.at())
            test.waitFor(test.days.count >= 55 && test.stream.dayCount() >= 55)
        // ---- Today: begun from its prompt, typed, saved ------------------------------------------
        if (test.at()) {
            test.stream.scrollToDay(0, false)
            const today = test.day(0)
            test.check(today !== null && AppController.routeFlag(test.days.get(0), "isToday"), "today is the first day")
            test.todayPath = AppController.routeText(test.days.get(0), "path")
        }
        if (test.at()) {
            const empty = test.day(0) !== null && test.day(0).blocks.visibleCount === 0
            if (empty) {
                test._at = test.stream.dayPoint(0, "prompt")
                test.check(test._at.x >= 0, "today without a note shows its prompt")
                test.tap(test._at.x, test._at.y)
            } else {
                test.editor.startEditing(test.day(0).lastVisibleRow(), -1)
            }
        }
        if (test.at())
            test.waitFor(test.day(0) !== null && test.day(0).editingRow >= 0 && test.stream.editRow >= 0
                         && test.stream.activeDay === 0)
        if (test.at()) {
            test.check(test.editor.controller === test.day(0), "the stream's editor acts on today")
            test.typeKeys("Kupić mleko")
            test.check(Js.indexOf(test.stream.editText, "Kupić mleko") >= 0, "typed into today: " + test.stream.editText)
        }
        if (test.at())
            test.waitFor(test.fileHas(test.todayPath, "Kupić mleko")) // the editor's debounced save
        if (test.at())
            test.check(test.fileHas(test.todayPath, "Kupić mleko"), "today's file is made by its first edit")
    }
    function runStepsAcross(): void {
        // ---- ↓ / ↑ across days -------------------------------------------------------------------
        if (test.at()) {
            test.day(0).stopEditing()
            test.stream.scrollToDay(1, false)
        }
        if (test.at())
            test.waitFor(test.dayLoaded(1) && test.dayLoaded(2) && test.stream.dayTop(1) >= 0)
        if (test.at()) {
            test.lastRow = test.day(1).lastVisibleRow()
            test.tapBlock(1, test.lastRow, 20)
        }
        if (test.at())
            test.waitFor(test.editing(1, test.lastRow))
        if (test.at())
            test.key(Qt.Key_Down, 0)
        if (test.at())
            test.waitFor(test.editing(2, 0))
        if (test.at()) {
            test.check(test.day(1).editingRow === -1, "↓ on a day's last line edits the next day's first block")
            test.check(test.editor.controller === test.day(2), "the next day is the one written in")
            test.key(Qt.Key_Up, 0)
        }
        if (test.at())
            test.waitFor(test.editing(1, test.lastRow))
        if (test.at()) {
            test.check(test.day(2).editingRow === -1, "↑ on its first line goes back to the day above")
            // ---- Enter and Backspace at a day's edges ------------------------------------------
            test.blocksFirst = test.day(1).blocks.count
            test.blocksSecond = test.day(2).blocks.count
            test.key(Qt.Key_End, 0)
            test.key(Qt.Key_Return, 0)
        }
        if (test.at())
            test.waitFor(test.day(1).blocks.count === test.blocksFirst + 1)
        if (test.at()) {
            test.check(test.day(2).blocks.count === test.blocksSecond, "Enter at a day's end makes a block in that day")
            test.check(test.day(1).editingRow === test.lastRow + 1, "the new block is edited")
            test.typeKeys("nowy")
            test.key(Qt.Key_Down, 0)
        }
        if (test.at())
            test.waitFor(test.editing(2, 0))
        if (test.at()) {
            test.key(Qt.Key_Home, 0)
            test.key(Qt.Key_Backspace, 0)
        }
        if (test.at()) {
            test.check(test.day(1).blocks.count === test.blocksFirst + 1 && test.day(2).blocks.count === test.blocksSecond,
                       "Backspace at a day's start merges nothing into the day above")
            test.check(test.day(2).editingRow === 0, "the day's first block stays edited")
        }
    }
    function runStepsSelection(): void {
        // ---- A selection over two days -------------------------------------------------------------
        if (test.at()) {
            test.key(Qt.Key_Up, 0)
        }
        if (test.at())
            test.waitFor(test.editing(1, test.lastRow + 1))
        if (test.at())
            test.key(Qt.Key_Escape, 0)
        if (test.at())
            test.waitFor(test.day(1).selectedRow === test.lastRow + 1 && test.day(1).editingRow === -1)
        if (test.at())
            test.key(Qt.Key_Down, Qt.ShiftModifier)
        if (test.at())
            test.waitFor(test.selection.active)
        if (test.at()) {
            test.check(test.selection.count >= 2 && test.day(2).selectionCount >= 1 && test.day(1).selectionCount >= 1,
                       "Shift+↓ past a day's end selects into the next day: " + test.selection.count)
            test.key(Qt.Key_Escape, 0)
        }
        if (test.at())
            test.waitFor(!test.selection.active && test.day(1).selectionCount === 0 && test.day(2).selectionCount === 0)
        // ---- The find bar over the days -------------------------------------------------------------
        if (test.at())
            QmlCall.call1(test.journalsView, "openFind", false)
        if (test.at())
            test.waitFor(test.editor.find !== null)
        if (test.at())
            test.editor.find.query = "mleko"
        if (test.at())
            test.waitFor(test.stream.findQuery === "mleko" && test.streamFind.count >= 2)
        if (test.at()) {
            test.check(test.streamFind.dayCount >= 2, "the find bar counts matches in several days: " + test.streamFind.dayCount)
            QmlCall.call(test.journalsView, "closeFind")
        }
        if (test.at())
            test.waitFor(test.stream.findQuery === "")
    }
    function runStepsViews(): void {
        // ---- A day zoomed into a block: its breadcrumb, a crumb clicked, Back ------------------------
        if (test.at()) {
            // (Two short blocks at the day's end, the second a child of the first: two levels to zoom
            // into, a path that fits a phone's width.)
            test.stream.scrollToDay(1, false)
            test.parentRow = test.day(1).insertBlockAfter(test.day(1).lastVisibleRow(), "Rodzic")
            test.childRow = test.day(1).insertBlockAfter(test.parentRow, "Dziecko")
            test.day(1).selectRange(test.childRow, test.childRow)
            test.day(1).indentSelection()
            test.day(1).clearSelection()
        }
        if (test.at()) {
            test.check(AppController.routeInt(test.day(1).blocks.get(test.childRow), "depth", -1) === 1, "a child to zoom into")
            test.check(test.crumbs() === null, "no breadcrumb over a day not zoomed into")
            test.day(1).zoomInto(test.childRow)
        }
        if (test.at())
            test.waitFor(test.day(1).zoomBlock === test.childRow && test.crumbs() !== null
                         && test.stream.dayRowTop(1, test.childRow) >= 0)
        if (test.at()) {
            test._child = test.crumbs()
            test._at = test._child.mapToItem(test.stream, 0, test._child.height)
            test.check(test._at.y <= test.stream.dayRowTop(1, test.childRow) + 0.5 && test._at.y >= test.stream.dayTop(1),
                       "the zoomed day's breadcrumb is above its blocks: " + test._at.y + " ≤ "
                       + test.stream.dayRowTop(1, test.childRow))
            // Its path: the page, the parent, the block; the parent clicked zooms out to it.
            test.crumbTitle = AppController.routeText(test.day(1).breadcrumb[0], "title")
            test._frame = DevTest.findWhere(test._child, "text", test.crumbTitle, true)
            test.check(test.day(1).breadcrumb.length === 2 && test._frame !== null,
                       "the breadcrumb names the zoomed block's parent: " + test.crumbTitle)
            if (test._frame !== null) {
                test._at = test._frame.mapToItem(test.stream, test._frame.width / 2, test._frame.height / 2)
                test.tap(test._at.x, test._at.y)
            }
        }
        if (test.at())
            test.waitFor(test.day(1).zoomBlock === test.parentRow)
        if (test.at()) {
            test.check(test.day(1).zoomBlock === test.parentRow && test.crumbs() !== null, "a crumb clicked zooms out to it")
            test.check(QmlCall.isTrue(QmlCall.get(test.journalsView, "canStepBack")), "Back can zoom out")
            QmlCall.call(test.journalsView, "stepBack")
        }
        if (test.at())
            test.waitFor(test.day(1).zoomBlock < 0 && test.crumbs() === null)
        if (test.at())
            test.check(test.stream.dayRowTop(1, 0) >= test.stream.dayTop(1), "Back zooms out of the day, its breadcrumb gone")
        // ---- The view's state, "Go to day" ---------------------------------------------------------
        if (test.at()) {
            test.stream.scrollToDay(20, false)
        }
        if (test.at())
            test.waitFor(test.stream.dayTop(20) >= -1 && test.stream.dayTop(20) <= 1)
        if (test.at()) {
            test.stream.scrollBy(30)
        }
        if (test.at()) {
            test.savedState = QmlCall.call(test.journalsView, "viewState")
            stateInfo.map = test.savedState
            test.savedOffset = AppController.routeInt(stateInfo.map, "offset", -1)
            test.savedDate = AppController.routeText(stateInfo.map, "date")
            test.expectedDate = AppController.routeText(test.days.get(20), "date")
            test.check(test.savedDate === test.expectedDate && Math.abs(test.savedOffset - 30) <= 1,
                       "the view's state: the day at the top and the offset into it: " + test.savedOffset)
            test.stream.scrollToDay(0, false)
        }
        if (test.at())
            test.waitFor(test.stream.dayTop(0) >= 0)
        if (test.at())
            QmlCall.call1(test.journalsView, "restoreViewState", test.savedState)
        if (test.at())
            test.waitFor(Math.abs(test.stream.dayTop(20) + test.savedOffset) <= 1)
        if (test.at()) {
            test.check(Math.abs(test.stream.dayTop(20) + test.savedOffset) <= 1, "the view's state restored")
            QmlCall.call1(test.journalsView, "scrollToDate", AppController.routeText(test.days.get(10), "date"))
        }
        if (test.at())
            test.waitFor(Math.abs(test.stream.dayTop(10)) <= 1)
        // ---- Only the days near the view; today saved and shown again --------------------------------
        if (test.at())
            test.stream.scrollToDay(test.days.count - 1, false)
        if (test.at())
            test.waitFor(test.day(0) === null && test.dayLoaded(test.days.count - 1))
        if (test.at()) {
            test.check(test.stream.madeDayCount() <= 20, "only the days near the view are made: " + test.stream.madeDayCount())
            test.check(test.fileHas(test.todayPath, "Kupić mleko"), "today's text is in its file after it went")
            test.stream.scrollToDay(0, false)
        }
        if (test.at())
            test.waitFor(test.dayLoaded(0))
        if (test.at())
            test.check(test.dayShows(0, "Kupić mleko"), "scrolled back, today shows the text")
    }

    function runStepsToolbar(): void {
        // ---- The touch toolbar on a keyboard (simulated) -------------------------------------------
        if (test.at()) {
            test.stream.scrollToDay(6, false)
            KeyboardInsets.simulated = 300
        }
        if (test.at())
            test.waitFor(test.dayLoaded(6) && test.dayLoaded(7))
        if (test.at()) {
            test.stream.activateDay(7)
            test.editor.startEditing(test.day(7).lastVisibleRow(), -1)
        }
        if (test.at())
            test.waitFor(!Theme.touchUi || (test.stream.editRow >= 0 && test.visibleSlots() === 1
                                             && test.near(test.slotBottom(), test.windowHeight() - 300)))
        if (test.at()) {
            if (Theme.touchUi) {
                test.check(test.visibleSlots() === 1 && test.near(test.slotBottom(), test.windowHeight() - 300),
                           "the touch toolbar sits on the keyboard, docked on the stream's frame: " + test.slotBottom())
                test.check(test.caretBottom() <= test.slotTop() + 0.5,
                           "the edited block's caret is above the toolbar: " + test.caretBottom() + " ≤ " + test.slotTop())
                test.slotY = test.slotTop()
                test.typeKeys(" — zażółć gęślą jaźń, źdźbło i łódź, zażółć gęślą jaźń, źdźbło i łódź")
            }
        }
        if (test.at())
            test.waitFor(!Theme.touchUi || test.caretBottom() <= test.slotTop() + 0.5)
        if (test.at()) {
            if (Theme.touchUi) {
                test.check(test.caretBottom() <= test.slotTop() + 0.5, "typing near the keyboard keeps the caret above the toolbar")
                test.stream.scrollBy(-120)
            }
        }
        if (test.at()) {
            if (Theme.touchUi)
                test.check(test.visibleSlots() === 1 && test.near(test.slotTop(), test.slotY),
                           "scrolling the stream leaves the toolbar on the keyboard: " + test.slotTop() + " = " + test.slotY)
            test.day(7).stopEditing()
            KeyboardInsets.simulated = -1
        }
        if (test.at())
            test.waitFor(test.visibleSlots() === 0)
    }

    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.stream !== null && test.days !== null
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
