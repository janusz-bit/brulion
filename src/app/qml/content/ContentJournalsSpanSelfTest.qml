pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalsSpanSelfTest — dev: selecting blocks across several days of the journal stream
// in the real app, headless (`brulion --demo --route journals --editor-script
// journals-span-selftest`, ctest tst_content_journals_span_qml on a desktop and
// tst_content_journals_span_qml_touch on a phone; docs/UI.md "Selecting and copying"). With real
// mouse, touch and key events (QtTest's TestEvent, EditorInput):
//   * desktop: Shift+click from a block of one day into the next day; the floating tools on that
//     day only; Ctrl+C copies both days' blocks after their [[date]] links; Delete and Tab change
//     nothing (only copying works), and so does the block menu's Przenieś w górę, shown unavailable
//     (its Kopiuj counts the whole span), as are the floating Wytnij's tooltip;
//     Shift+↓ past a day's end into the next day and Shift+↑ back;
//     Esc clears every day; a mouse drag from one day into the next; a plain click edits and ends
//     the span;
//   * phone: a long press (the selection mode), a tap in the next day extends the selection
//     there; one docked toolbar with the stream's count; its Wytnij changes nothing, Kopiuj copies;
//     a tap two days further; Back clears every day; with every day of a span scrolled away a tap
//     edits and ends it;
//   * both: the anchor day scrolled far away (destroyed) and back gets its part again; copying
//     meanwhile reads it from its file.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Compiled to C++ like the rest of the app's QML (docs/UI.md "Compiled QML"): the steps are one
// typed function (runSteps(): each step an `if (test.at())` block, in order), events through
// DevEvents (QtTest's TestEvent made when first used), a step's exception caught by
// DevTest.callCatching().
Item {
    id: test

    property ListView list
    property JournalListModel days
    property StreamSelection stream

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    /// The step runner: the step to run on this tick (stepTarget), the steps counted so far in
    /// runSteps() (stepCursor) and whether a waiting step's condition held (stepDone).
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true
    /// Rows of the three days written for the test (newest first: A above B above C).
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property string fileA: ""
    /// A day far below them (-330).
    property int rowFar: -1
    /// Items being looked at (a call's item is read through a property, compiled).
    property Item _item: null
    property Item _row: null
    property Item _frameItem: null
    /// StreamSelection.copy()'s result.
    property var _copied: ({})

    readonly property bool touch: Theme.touchUi

    DevEvents { id: events }

    function check(ok: bool, what: string): void {
        if (!ok)
            test.failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    /// The step being run now: true for one `if (test.at())` of runSteps() per tick.
    function at(): bool {
        return test.stepCursor++ === test.stepTarget
    }
    /// A waiting step: done once `ready` holds (run again on the next tick until then).
    function waitFor(ready: bool): void {
        test.stepDone = ready
    }
    /// Steps that do nothing (a finger held down meanwhile).
    function idle(n: int): void {
        for (let i = 0; i < n; ++i) {
            if (test.at()) {
                // (nothing)
            }
        }
    }
    function dayAt(row: int): ContentJournalDay {
        test._item = row >= 0 ? test.list.itemAtIndex(row) : null
        if (test._item === null)
            return null
        const n = DevTest.childCount(test._item)
        for (let i = 0; i < n; ++i) {
            const day = DevTest.childAt(test._item, i) as ContentJournalDay
            if (day !== null)
                return day
        }
        return null
    }
    function loaded(row: int): bool {
        const d = test.dayAt(row)
        return d !== null && d.controller.loaded && test.blockText(d, 0) !== null
    }
    function blockText(day: ContentJournalDay, row: int): Item {
        test._row = day !== null ? day.editor.listView.itemAtIndex(row) : null
        return test._row !== null ? test.richTextIn(test._row) : null
    }
    /// The rendered rich text under `item` (a Text or TextEdit showing rich text), visible only.
    function richTextIn(item: Item): Item {
        if (item === null || !item.visible)
            return null
        const text = item as Text
        if (text !== null && text.textFormat === Text.RichText)
            return text
        const edit = item as TextEdit
        if (edit !== null && edit.textFormat === TextEdit.RichText)
            return edit
        const n = DevTest.childCount(item)
        for (let i = 0; i < n; ++i) {
            const found = test.richTextIn(DevTest.childAt(item, i))
            if (found !== null)
                return found
        }
        return null
    }
    /// How many items named `name` are shown on the stream's frame (the docked toolbar, the
    /// floating tools), and the first of them.
    function shownOnFrameCount(name: string): int {
        const frame = test.list.parent
        let count = 0
        const n = DevTest.childCount(frame)
        for (let i = 0; i < n; ++i) {
            if (DevTest.findShown(DevTest.childAt(frame, i), name) !== null)
                count += 1
        }
        return count
    }
    function shownOnFrame(name: string): Item {
        const frame = test.list.parent
        const n = DevTest.childCount(frame)
        for (let i = 0; i < n; ++i) {
            test._frameItem = DevTest.findShown(DevTest.childAt(frame, i), name)
            if (test._frameItem !== null)
                return test._frameItem
        }
        return null
    }
    function rows(row: int): string {
        const d = test.dayAt(row)
        return d !== null ? Js.stringify(d.controller.selectedRows()) : "gone"
    }
    function click(item: Item, modifiers: int): void {
        if (test.touch) {
            events.touchPress(item, 20, item.height / 2)
            events.touchRelease(item, 20, item.height / 2)
        } else {
            events.mouseClick(item, 20, item.height / 2, Qt.LeftButton, modifiers, -1)
        }
    }
    /// A key to what has the keyboard (the day the selection's end is in has it).
    function key(k: int, modifiers: int): void {
        EditorInput.sendKey(test.Window.activeFocusItem, k, modifiers, "")
    }
    function clipboard(): string {
        let d = test.dayAt(test.rowA)
        if (d === null)
            d = test.dayAt(test.rowB)
        if (d === null)
            d = test.dayAt(test.rowC)
        return d !== null ? d.controller.clipboardText() : ""
    }
    /// The block menu of a day's editor (BlockEditor.blockMenu, made when first opened).
    function blockMenuOf(day: ContentJournalDay): BlockMenu {
        return DevTest.objectProperty(day.editor, "blockMenu") as BlockMenu
    }
    /// The item of `menu` named `name`.
    function menuItem(menu: BlockMenu, name: string): BMenuItem {
        for (let i = 0; menu !== null && i < menu.count; ++i) {
            const item = menu.itemAt(i) as BMenuItem
            if (item !== null && item.objectName === name)
                return item
        }
        return null
    }
    /// StreamSelection.copy()'s result (_copied) and its Markdown.
    function copiedFlag(key: string): bool {
        return AppController.routeFlag(test._copied, key)
    }
    function copiedText(key: string): string {
        return AppController.routeText(test._copied, key)
    }
    function streamMarkdownHas(needle: string): bool {
        return Js.indexOf(test.stream.markdown(), needle) > 0
    }
    function selected(row: int): bool {
        const d = test.dayAt(row)
        return d !== null && d.controller.selectionCount > 0
    }
    /// Nothing selected anywhere in the stream.
    function noneSelected(): bool {
        return !test.selected(test.rowA) && !test.selected(test.rowB) && !test.selected(test.rowC) && !test.stream.active
    }
    /// A long press on the second block of day A (the selection mode on a phone).
    function longPressSteps(): void {
        if (test.at()) {
            const a1 = test.blockText(test.dayAt(test.rowA), 1)
            events.touchPress(a1, a1.width / 2, a1.height / 2)
        }
        test.idle(9)
        if (test.at()) {
            const a1 = test.blockText(test.dayAt(test.rowA), 1)
            events.touchRelease(a1, a1.width / 2, a1.height / 2)
        }
        if (test.at())
            test.waitFor(test.dayAt(test.rowA).editor.selectionMode)
    }
    function writeDays(): void {
        // Three consecutive days, long ago (nothing of the sample notebook between them).
        for (let n = 300; n <= 302; ++n) {
            let md = ""
            for (let b = 0; b < 4; ++b)
                md += "- Dzień " + n + ", blok " + b + ": zażółć gęślą jaźń\n"
            Workspace.writeFile("" + Workspace.journalPath("" + DatesHelper.addDays(DatesHelper.today, -n)), md)
        }
        test.fileA = Workspace.journalPath(DatesHelper.addDays(DatesHelper.today, -300))
        // Older days below them: enough to scroll them out of the list's cache.
        for (let n = 303; n < 333; ++n) {
            let md = ""
            for (let b = 0; b < 10; ++b)
                md += "- Wcześniej " + n + ", notatka " + b + "\n"
            Workspace.writeFile("" + Workspace.journalPath("" + DatesHelper.addDays(DatesHelper.today, -n)), md)
        }
    }

    function runSteps(): void {
        // ---- Common -----------------------------------------------------------------------------
        if (test.at())
            test.waitFor(Workspace.ready && test.days.count >= 1 && test.dayAt(0) !== null)
        if (test.at())
            test.writeDays()
        if (test.at()) {
            test.rowA = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -300))
            test.rowB = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -301))
            test.rowC = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -302))
            test.waitFor(test.rowA > 0 && test.rowB === test.rowA + 1 && test.rowC === test.rowB + 1
                         && test.days.count > test.rowC + 29)
        }
        if (test.at())
            test.list.positionViewAtIndex(test.rowA, ListView.Beginning)
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB))
        if (!test.touch)
            test.desktopSteps()
        else
            test.phoneSteps()
        test.scrollingSteps()
        if (test.touch)
            test.phoneAwaySteps()
    }

    function desktopSteps(): void {
        // ---- Shift+click into the next day ---------------------------------------------------
        if (test.at()) {
            test.dayAt(test.rowA).editor.selectBlock(1)
            test.click(test.blockText(test.dayAt(test.rowB), 2), Qt.ShiftModifier)
        }
        if (test.at()) {
            test.check(test.stream.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                       "Shift+click in the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            test.check(test.stream.count === 6 && test.stream.pageCount === 2, "the stream counts 6 blocks of 2 days: " + test.stream.count)
            const bars = test.shownOnFrameCount("floatingSelectionTools")
            test.check(bars === 1 && test.stream.toolsController === test.dayAt(test.rowB).controller,
                       "one floating tool bar, the clicked day's: " + bars)
            test.key(Qt.Key_C, Qt.ControlModifier)
        }
        if (test.at()) {
            const text = test.clipboard()
            const head = "" + Js.slice(text, 0, 60)
            test.check(Js.indexOf(text, "[[") === 0 && Js.indexOf(text, "]]\n\n- Dzień 300, blok 1") > 0
                       && Js.indexOf(text, "]]\n\n- Dzień 301, blok 0") > 0 && Js.endsWith(text, "- Dzień 301, blok 2: zażółć gęślą jaźń"),
                       "Ctrl+C copies both days' blocks after their links: " + head)
            test.key(Qt.Key_Delete, 0)
            test.key(Qt.Key_Tab, 0)
            const bar = test.shownOnFrameCount("floatingSelectionTools") === 1 && test.shownOnFrame("floatingSelectionTools") !== null
            const cut = bar ? DevTest.findShown(test._frameItem, "selectionCut") as BIconButton : null
            const tip = cut !== null ? cut.toolTipText : ""
            const reason = "" + test.dayAt(test.rowB).editor.spanReason
            test.check(cut !== null && tip === reason,
                       "the floating Wytnij says why in its tooltip: " + tip)
            // The block menu of a block in the span.
            const a2 = test.blockText(test.dayAt(test.rowA), 2)
            events.mouseClick(a2, 20, a2.height / 2, Qt.RightButton, Qt.NoModifier, -1)
        }
        if (test.at()) {
            const menu = test.blockMenuOf(test.dayAt(test.rowA))
            test.waitFor(menu !== null && menu.opened)
        }
        if (test.at()) {
            const editor = test.dayAt(test.rowA).editor
            const menu = test.blockMenuOf(test.dayAt(test.rowA))
            const up = test.menuItem(menu, "blockMenuMoveUp")
            const cut = test.menuItem(menu, "blockMenuCut")
            const copy = test.menuItem(menu, "blockMenuCopy")
            const reason = "" + editor.spanReason
            test.check(test.stream.active && menu.inSelection && up !== null && up.unavailable === reason
                       && cut !== null && cut.unavailable === reason,
                       "the block menu in the span shows moving and cutting unavailable: " + (up !== null ? up.unavailable : "no item"))
            const copyText = copy !== null ? copy.text : ""
            test.check(copy !== null && Js.indexOf(copyText, "6") >= 0, "its Kopiuj counts the whole span: " + copyText)
            if (up !== null)
                up.triggered()
            menu.close()
        }
        if (test.at())
            test.waitFor(!test.blockMenuOf(test.dayAt(test.rowA)).opened)
        if (test.at())
            test.dayAt(test.rowA).controller.save()
        if (test.at()) {
            test.check(test.stream.active && test.dayAt(test.rowA).controller.blocks.count === 4
                       && Js.indexOf(Workspace.readFile(test.fileA), "- Dzień 300, blok 3") >= 0
                       && !test.dayAt(test.rowA).controller.canUndo && !test.dayAt(test.rowB).controller.canUndo,
                       "Delete, Tab and the block menu's Przenieś w górę change nothing over several days")
            // Shift+↓ to the day's end, then past it into the third day.
            test.key(Qt.Key_Down, Qt.ShiftModifier)
            test.key(Qt.Key_Down, Qt.ShiftModifier)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowC))
        if (test.at()) {
            test.check(test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0]"
                       && test.stream.focusPath === test.dayAt(test.rowC).path,
                       "Shift+↓ past a day's end goes on in the next day: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
            test.key(Qt.Key_Up, Qt.ShiftModifier)
            test.key(Qt.Key_Up, Qt.ShiftModifier)
        }
        if (test.at()) {
            test.check(test.rows(test.rowC) === "[]" && test.rows(test.rowB) === "[0,1,2]",
                       "Shift+↑ comes back: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
            test.key(Qt.Key_Escape, 0)
        }
        if (test.at())
            test.check(test.noneSelected(), "Esc clears every day")
        // ---- A mouse drag from one day into the next -----------------------------------------
        if (test.at()) {
            const a0 = test.blockText(test.dayAt(test.rowA), 0)
            events.mousePress(a0, 30, a0.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseMove(a0, 50, a0.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at()) {
            const a2 = test.blockText(test.dayAt(test.rowA), 2)
            events.mouseMove(a2, 50, a2.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at()) {
            const b1 = test.blockText(test.dayAt(test.rowB), 1)
            events.mouseMove(b1, 50, b1.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at()) {
            const b1 = test.blockText(test.dayAt(test.rowB), 1)
            events.mouseRelease(b1, 50, b1.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at()) {
            test.check(test.stream.active && test.rows(test.rowA) === "[0,1,2,3]" && test.rows(test.rowB) === "[0,1]",
                       "a mouse drag into the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            // A plain click edits a block and ends the selection.
            test.click(test.blockText(test.dayAt(test.rowC), 1), Qt.NoModifier)
        }
        if (test.at())
            test.waitFor(test.dayAt(test.rowC).controller.editingRow === 1)
        if (test.at()) {
            test.check(test.noneSelected(), "a click to edit ends the selection over several days")
            test.dayAt(test.rowC).controller.stopEditing()
            // Again from A for the scrolling part.
            test.dayAt(test.rowA).editor.selectBlock(2)
            test.click(test.blockText(test.dayAt(test.rowB), 1), Qt.ShiftModifier)
        }
    }

    function phoneSteps(): void {
        // ---- A long press, then a tap in the next day ----------------------------------------------
        test.longPressSteps()
        if (test.at())
            test.click(test.blockText(test.dayAt(test.rowB), 2), Qt.NoModifier)
        if (test.at()) {
            test.check(test.stream.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                       "a tap in the next day extends the selection there: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            const tools = test.shownOnFrameCount("toolbarSelectionTools")
            test.check(tools === 1 && test.dayAt(test.rowB).editor.toolbarVisible
                       && !test.dayAt(test.rowA).editor.toolbarVisible, "one docked toolbar: " + tools)
            const bar = tools === 1 && test.shownOnFrame("toolbarSelectionTools") !== null
            const count = bar ? DevTest.findShown(test._frameItem, "selectionCount") as Text : null
            const countText = count !== null ? count.text : ""
            test.check(count !== null && Js.indexOf(countText, "6") >= 0, "it counts the blocks of both days: " + countText)
            const cut = bar ? DevTest.findShown(test._frameItem, "selectionCut") as Item : null
            if (cut !== null)
                test.click(cut, Qt.NoModifier)
        }
        if (test.at()) {
            test.check(test.stream.active && test.dayAt(test.rowA).controller.blocks.count === 4
                       && !test.dayAt(test.rowA).controller.canUndo, "Wytnij changes nothing over several days")
            const bar = test.shownOnFrameCount("toolbarSelectionTools") === 1 && test.shownOnFrame("toolbarSelectionTools") !== null
            const copy = bar ? DevTest.findShown(test._frameItem, "selectionCopy") as Item : null
            if (copy !== null)
                test.click(copy, Qt.NoModifier)
        }
        if (test.at()) {
            const text = test.clipboard()
            const head = "" + Js.slice(text, 0, 60)
            test.check(Js.indexOf(text, "[[") === 0 && Js.indexOf(text, "]]\n\n- Dzień 300, blok 1") > 0
                       && Js.endsWith(text, "- Dzień 301, blok 2: zażółć gęślą jaźń"),
                       "Kopiuj copies both days' blocks after their links: " + head)
            test.list.positionViewAtIndex(test.rowC, ListView.Beginning)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowC))
        if (test.at())
            test.click(test.blockText(test.dayAt(test.rowC), 1), Qt.NoModifier)
        if (test.at())
            test.check(test.stream.pageCount === 3 && test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0,1]",
                       "a tap two days further takes the day between whole: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
    }

    function scrollingSteps(): void {
        // ---- The anchor day scrolled away and back ----------------------------------------------
        if (test.at()) {
            test.check(test.stream.active, "a selection over several days to scroll with")
            test.list.positionViewAtEnd()
        }
        if (test.at())
            test.waitFor(test.dayAt(test.rowA) === null && test.dayAt(test.rowB) === null)
        if (test.at()) {
            test.check(test.stream.active, "the days scrolled away keep the selection")
            test._copied = test.stream.copy(false)
            const copied = test.copiedFlag("ok") && test.streamMarkdownHas("- Dzień 300, blok")
            test.check(copied, "copied from the files meanwhile: " + test.copiedText("count"))
            test.list.positionViewAtIndex(test.rowA, ListView.Beginning)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB))
        if (test.at()) {
            test.check(test.dayAt(test.rowA).controller.selectionCount > 0 && test.dayAt(test.rowB).controller.selectionCount > 0,
                       "scrolled back, the days show their parts again: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            test.key(test.touch ? Qt.Key_Back : Qt.Key_Escape, 0)
        }
        if (test.at())
            test.check(test.noneSelected(), test.touch ? "Back clears every day" : "Esc clears every day again")
    }

    function phoneAwaySteps(): void {
        // ---- Every day of a span scrolled away: a tap edits --------------------------------------
        if (test.at())
            test.list.positionViewAtIndex(test.rowA, ListView.Beginning)
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB))
        test.longPressSteps()
        if (test.at())
            test.click(test.blockText(test.dayAt(test.rowB), 2), Qt.NoModifier)
        if (test.at()) {
            test.check(test.stream.active, "a selection over two days again")
            test.rowFar = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -330))
            test.list.positionViewAtIndex(test.rowFar, ListView.Beginning)
        }
        if (test.at())
            test.waitFor(test.dayAt(test.rowA) === null && test.dayAt(test.rowB) === null && test.loaded(test.rowFar))
        if (test.at()) {
            test.check(test.stream.active && test.stream.toolsController === null, "no day of the span on screen: no tools")
            test.click(test.blockText(test.dayAt(test.rowFar), 1), Qt.NoModifier)
        }
        if (test.at())
            test.waitFor(test.dayAt(test.rowFar).controller.editingRow === 1)
        if (test.at()) {
            test.check(!test.stream.active && test.dayAt(test.rowFar).controller.selectionCount === 0,
                       "a tap there edits and ends the span: " + test.stream.pageCount)
            test.dayAt(test.rowFar).controller.stopEditing()
        }
    }

    /// A step is running. QtTest's events process events before they return (a touch sequence's
    /// commit(), a mouse click), so a step that outlasts the interval lets the timer fire from
    /// inside it: that tick is skipped — it would run the same step again and then skip the next.
    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.list !== null && test.days !== null && test.stream !== null
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
                // (No step left.)
                runner.stop()
                console.log("SELFTEST DONE failures=" + test.failures)
                Qt.exit(test.failures === 0 ? 0 : 1)
                return
            }
            if (!test.stepDone && ++test.waited < 80)
                return // a waiting step: again on the next tick (~10 s at most)
            if (!test.stepDone)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }
}
