pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalsSpanAtramentSelfTest — dev: selecting blocks across several days of the journal
// stream drawn by Atrament as one list (docs/ATRAMENT.md "The journal stream as one list";
// qml/editor/EditorAtramentJournal.qml) in the real app, headless (`brulion --demo --route journals
// --editor-script journals-span-selftest --atrament-editor`, ctest tst_content_journals_span_atrament_qml
// on a desktop and tst_content_journals_span_atrament_qml_touch on a phone; JournalsView loads it in
// place of ContentJournalsSpanSelfTest when its stream is Atrament's). ContentJournalsSpanSelfTest's
// steps against the one list, with real mouse, touch and key events through the window (DevEvents,
// EditorInput) at the places the stream says its days' blocks are (AtramentView.dayRowTop):
//   * desktop: Shift+click from a block of one day into the next day; the floating tools on that
//     day only; Ctrl+C copies both days' blocks after their [[date]] links; Delete and Tab change
//     nothing (only copying works), and so does the block menu's Move up, shown unavailable (its
//     Copy counts the whole span), as is the floating Cut's tooltip; Shift+↓ past a day's end into
//     the next day and Shift+↑ back; Esc clears every day; a mouse drag from one day into the next,
//     then held at the stream's bottom edge: the stream scrolls and the selection goes on into the
//     days coming in; a plain click edits and ends the span;
//   * phone: a long press (the selection mode), a tap in the next day extends the selection there;
//     one docked toolbar with the stream's count; its Cut changes nothing, Copy copies; a tap two
//     days further; Back clears every day; with every day of a span scrolled away a tap edits and
//     ends it;
//   * both: the anchor day scrolled far away (released — the day written in stays made: the
//     stream's editor acts on it) and back gets its part again; copying meanwhile reads it from
//     its file.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Registered only where Atrament is built (it names AtramentView).
Item {
    id: test

    /// JournalsView's parts: the stream (the AtramentView), its editor, the days, the selection over
    /// several days.
    property AtramentView stream
    property BlockEditor editor
    property JournalListModel days
    property StreamSelection selection

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property int stepTarget: 0
    property int stepCursor: 0
    property bool stepDone: true
    /// Rows of the three days written for the test (newest first: A above B above C), and one far below.
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property int rowFar: -1
    property string fileA: ""
    property real scrolledFrom: 0
    property point _at: Qt.point(0, 0)
    property Item _frame: null
    property Item _found: null
    property var _copied: ({})

    readonly property bool touch: Theme.touchUi

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
    function idle(n: int): void {
        test.stepCursor += n
    }
    /// Day `row`'s controller while the stream has it made (null: far from the view).
    function day(row: int): PageController {
        return test.stream.dayController(row) as PageController
    }
    /// The day is made, loaded and its first block drawn.
    function loaded(row: int): bool {
        const c = test.day(row)
        return c !== null && c.loaded && test.stream.dayRowHeight(row, 0) > 0
    }
    /// Where block `block` of day `row` is in the stream: a little into its text, its first line.
    function blockX(): real {
        return test.stream.contentX + test.stream.gutter + 30
    }
    function blockY(row: int, block: int): real {
        return test.stream.dayRowTop(row, block) + Math.min(test.stream.dayRowHeight(row, block) / 2, 12)
    }
    function click(row: int, block: int, modifiers: int): void {
        const x = test.blockX()
        const y = test.blockY(row, block)
        if (test.touch) {
            events.touchPress(test.stream, x, y)
            events.touchRelease(test.stream, x, y)
        } else {
            events.mouseClick(test.stream, x, y, Qt.LeftButton, modifiers, -1)
        }
    }
    function rows(row: int): string {
        const c = test.day(row)
        return c !== null ? Js.stringify(c.selectedRows()) : "gone"
    }
    /// A real key through the window (the stream keeps the keyboard while blocks are selected).
    function key(k: int, modifiers: int): void {
        const code = k
        const mods = modifiers
        EditorInput.sendKey(test.stream, code, mods, "")
    }
    function clipboard(): string {
        let c = test.day(test.rowA)
        if (c === null)
            c = test.day(test.rowB)
        if (c === null)
            c = test.day(test.rowC)
        return c !== null ? c.clipboardText() : ""
    }
    /// How many items named `name` show on the editor's dock (the floating tools, the docked
    /// toolbar), and the first of them (_found).
    function shownOnDock(name: string): int {
        test._frame = test.editor.dockHost
        test._found = null
        let count = 0
        const n = DevTest.childCount(test._frame)
        for (let i = 0; i < n; ++i) {
            const item = DevTest.findShown(DevTest.childAt(test._frame, i), name)
            if (item !== null) {
                if (count === 0)
                    test._found = item
                count += 1
            }
        }
        return count
    }
    function blockMenu(): BlockMenu {
        return DevTest.objectProperty(test.editor, "blockMenu") as BlockMenu
    }
    function menuItem(menu: BlockMenu, name: string): BMenuItem {
        for (let i = 0; menu !== null && i < menu.count; ++i) {
            const item = menu.itemAt(i) as BMenuItem
            if (item !== null && item.objectName === name)
                return item
        }
        return null
    }
    function copiedFlag(key: string): bool {
        return AppController.routeFlag(test._copied, key)
    }
    function copiedText(key: string): string {
        return AppController.routeText(test._copied, key)
    }
    function selected(row: int): bool {
        const c = test.day(row)
        return c !== null && c.selectionCount > 0
    }
    function noneSelected(): bool {
        return !test.selected(test.rowA) && !test.selected(test.rowB) && !test.selected(test.rowC) && !test.selection.active
    }
    /// The day the span's focus end is in (−1: none).
    function focusDay(): int {
        for (let d = 0; d < test.days.count; ++d) {
            if (AppController.routeText(test.days.get(d), "path") === test.selection.focusPath)
                return d
        }
        return -1
    }
    /// A finger held on day A's second block (the selection mode on a phone).
    function longPressSteps(): void {
        if (test.at())
            events.touchPress(test.stream, test.blockX(), test.blockY(test.rowA, 1))
        test.idle(9)
        if (test.at())
            events.touchRelease(test.stream, test.blockX(), test.blockY(test.rowA, 1))
        if (test.at())
            test.waitFor(test.editor.selectionMode && test.day(test.rowA).selectionCount > 0)
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
        // Older days below them: enough to scroll them far from the view.
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
            test.waitFor(Workspace.ready && test.days.count >= 1 && test.day(0) !== null)
        if (test.at())
            test.writeDays()
        if (test.at()) {
            test.rowA = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -300))
            test.rowB = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -301))
            test.rowC = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -302))
            test.waitFor(test.rowA > 0 && test.rowB === test.rowA + 1 && test.rowC === test.rowB + 1
                         && test.days.count > test.rowC + 29 && test.stream.dayCount() === test.days.count)
        }
        if (test.at())
            test.stream.scrollToDay(test.rowA, false)
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
            test.stream.activateDay(test.rowA)
            test.editor.selectBlock(1)
        }
        if (test.at())
            test.click(test.rowB, 2, Qt.ShiftModifier)
        if (test.at())
            test.waitFor(test.selection.active)
        if (test.at()) {
            test.check(test.selection.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                       "Shift+click in the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            test.check(test.selection.count === 6 && test.selection.pageCount === 2, "the stream counts 6 blocks of 2 days: " + test.selection.count)
            const bars = test.shownOnDock("floatingSelectionTools")
            test.check(bars === 1 && test.selection.toolsController === test.day(test.rowB),
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
            const cut = test.shownOnDock("floatingSelectionTools") === 1
                      ? DevTest.findShown(test._found, "selectionCut") as BIconButton : null
            const tip = cut !== null ? cut.toolTipText : ""
            test.check(cut !== null && tip === test.editor.spanReason, "the floating Wytnij says why in its tooltip: " + tip)
            // The block menu of a block in the span.
            events.mouseClick(test.stream, test.blockX(), test.blockY(test.rowA, 2), Qt.RightButton, Qt.NoModifier, -1)
        }
        if (test.at())
            test.waitFor(test.blockMenu() !== null && test.blockMenu().opened)
        if (test.at()) {
            const menu = test.blockMenu()
            const up = test.menuItem(menu, "blockMenuMoveUp")
            const cut = test.menuItem(menu, "blockMenuCut")
            const copy = test.menuItem(menu, "blockMenuCopy")
            const reason = "" + test.editor.spanReason
            test.check(test.selection.active && menu.inSelection && up !== null && up.unavailable === reason
                       && cut !== null && cut.unavailable === reason,
                       "the block menu in the span shows moving and cutting unavailable: " + (up !== null ? up.unavailable : "no item"))
            const copyText = copy !== null ? copy.text : ""
            test.check(copy !== null && Js.indexOf(copyText, "6") >= 0, "its Kopiuj counts the whole span: " + copyText)
            if (up !== null)
                up.triggered()
            menu.close()
        }
        if (test.at())
            test.waitFor(!test.blockMenu().opened)
        if (test.at())
            test.day(test.rowA).save()
        if (test.at()) {
            test.check(test.selection.active && test.day(test.rowA).blocks.count === 4
                       && Js.indexOf(Workspace.readFile(test.fileA), "- Dzień 300, blok 3") >= 0
                       && !test.day(test.rowA).canUndo && !test.day(test.rowB).canUndo,
                       "Delete, Tab and the block menu's Przenieś w górę change nothing over several days")
            // Shift+↓ to the day's end, then past it into the third day.
            test.key(Qt.Key_Down, Qt.ShiftModifier)
            test.key(Qt.Key_Down, Qt.ShiftModifier)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowC) && test.rows(test.rowC) === "[0]")
        if (test.at()) {
            test.check(test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0]"
                       && test.selection.focusPath === test.day(test.rowC).path,
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
        test.desktopDragSteps()
    }

    // (desktopSteps' second half: one function took qmlcachegen 47 s under load, over its 30 s.)
    function desktopDragSteps(): void {
        // ---- A mouse drag from one day into the next, then held at the stream's bottom edge ----------
        if (test.at())
            test.stream.scrollToDay(test.rowA, false)
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB))
        if (test.at()) {
            events.mousePress(test.stream, test.blockX(), test.blockY(test.rowA, 0), Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseMove(test.stream, test.blockX() + 20, test.blockY(test.rowA, 0) + 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at())
            events.mouseMove(test.stream, test.blockX() + 20, test.blockY(test.rowA, 2), -1, Qt.LeftButton, Qt.NoModifier)
        if (test.at())
            events.mouseMove(test.stream, test.blockX() + 20, test.blockY(test.rowB, 1), -1, Qt.LeftButton, Qt.NoModifier)
        if (test.at()) {
            test.check(test.selection.active && test.rows(test.rowA) === "[0,1,2,3]" && test.rows(test.rowB) === "[0,1]",
                       "a mouse drag into the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            test.scrolledFrom = test.stream.dayTop(test.rowB)
            events.mouseMove(test.stream, test.blockX() + 20, test.stream.height - 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at())
            test.waitFor(test.focusDay() > test.rowC && test.scrolledPast())
        if (test.at()) {
            test.check(test.focusDay() > test.rowC && test.scrolledPast(),
                       "held at the bottom edge the stream scrolls, the selection going on into the days coming in: day "
                       + test.focusDay())
            events.mouseRelease(test.stream, test.blockX() + 20, test.stream.height - 2, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at()) {
            test.check(test.selection.active && test.selection.pageCount > 3 && test.day(test.rowA) !== null
                       && test.day(test.rowA).editingRow === -1, "let go, the span stays: " + test.selection.pageCount + " days")
            test.key(Qt.Key_Escape, 0)
            test.stream.scrollToDay(test.rowA, false)
        }
        if (test.at())
            test.waitFor(test.noneSelected() && test.loaded(test.rowA) && test.loaded(test.rowC))
        if (test.at()) {
            test.dragSelect()
        }
        if (test.at())
            test.click(test.rowC, 1, Qt.NoModifier)
        if (test.at())
            test.waitFor(test.day(test.rowC).editingRow === 1)
        if (test.at()) {
            test.check(test.noneSelected(), "a click to edit ends the selection over several days")
            test.day(test.rowC).stopEditing()
            // Again from A for the scrolling part.
            test.stream.activateDay(test.rowA)
            test.editor.selectBlock(2)
        }
        if (test.at())
            test.click(test.rowB, 1, Qt.ShiftModifier)
        if (test.at())
            test.waitFor(test.selection.active)
    }
    /// Day B went up by 200 px or more since the drag began (or out of the view: not drawn).
    function scrolledPast(): bool {
        const top = test.stream.dayTop(test.rowB)
        return top === -1 || top < test.scrolledFrom - 200
    }
    /// A short mouse drag from day A into day B (a span to end with a click).
    function dragSelect(): void {
        events.mousePress(test.stream, test.blockX(), test.blockY(test.rowA, 0), Qt.LeftButton, Qt.NoModifier, -1)
        events.mouseMove(test.stream, test.blockX() + 20, test.blockY(test.rowA, 2), -1, Qt.LeftButton, Qt.NoModifier)
        events.mouseMove(test.stream, test.blockX() + 20, test.blockY(test.rowB, 1), -1, Qt.LeftButton, Qt.NoModifier)
        events.mouseRelease(test.stream, test.blockX() + 20, test.blockY(test.rowB, 1), Qt.LeftButton, Qt.NoModifier, -1)
    }

    function phoneSteps(): void {
        // ---- A long press, then a tap in the next day ----------------------------------------------
        test.longPressSteps()
        if (test.at())
            test.click(test.rowB, 2, Qt.NoModifier)
        if (test.at())
            test.waitFor(test.selection.active)
        if (test.at()) {
            test.check(test.selection.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                       "a tap in the next day extends the selection there: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            const tools = test.shownOnDock("toolbarSelectionTools")
            test.check(tools === 1 && test.editor.toolbarVisible, "one docked toolbar: " + tools)
            const count = tools === 1 ? DevTest.findShown(test._found, "selectionCount") as Text : null
            const countText = count !== null ? count.text : ""
            test.check(count !== null && Js.indexOf(countText, "6") >= 0, "it counts the blocks of both days: " + countText)
            const cut = tools === 1 ? DevTest.findShown(test._found, "selectionCut") as Item : null
            if (cut !== null) {
                test._at = cut.mapToItem(test.stream, cut.width / 2, cut.height / 2)
                events.touchPress(test.stream, test._at.x, test._at.y)
                events.touchRelease(test.stream, test._at.x, test._at.y)
            }
        }
        if (test.at()) {
            test.check(test.selection.active && test.day(test.rowA).blocks.count === 4
                       && !test.day(test.rowA).canUndo, "Wytnij changes nothing over several days")
            const copy = test.shownOnDock("toolbarSelectionTools") === 1
                       ? DevTest.findShown(test._found, "selectionCopy") as Item : null
            if (copy !== null) {
                test._at = copy.mapToItem(test.stream, copy.width / 2, copy.height / 2)
                events.touchPress(test.stream, test._at.x, test._at.y)
                events.touchRelease(test.stream, test._at.x, test._at.y)
            }
        }
        if (test.at()) {
            const text = test.clipboard()
            const head = "" + Js.slice(text, 0, 60)
            test.check(Js.indexOf(text, "[[") === 0 && Js.indexOf(text, "]]\n\n- Dzień 300, blok 1") > 0
                       && Js.endsWith(text, "- Dzień 301, blok 2: zażółć gęślą jaźń"),
                       "Kopiuj copies both days' blocks after their links: " + head)
            test.stream.scrollToDay(test.rowC, false)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowC))
        if (test.at())
            test.click(test.rowC, 1, Qt.NoModifier)
        if (test.at())
            test.waitFor(test.selection.pageCount === 3)
        if (test.at())
            test.check(test.selection.pageCount === 3 && test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0,1]",
                       "a tap two days further takes the day between whole: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
    }

    function scrollingSteps(): void {
        // ---- The anchor day scrolled away and back ----------------------------------------------
        if (test.at()) {
            test.check(test.selection.active, "a selection over several days to scroll with")
            test.stream.scrollToDay(test.days.count - 1, false)
        }
        // (The day written in stays made: the stream's editor acts on it.)
        if (test.at())
            test.waitFor(test.day(test.rowA) === null
                         && (test.day(test.rowB) === null || test.stream.activeDay === test.rowB))
        if (test.at()) {
            test.check(test.selection.active, "the days scrolled away keep the selection")
            test._copied = test.selection.copy(false)
            const copied = test.copiedFlag("ok") && Js.indexOf(test.selection.markdown(), "- Dzień 300, blok") > 0
            test.check(copied, "copied from the files meanwhile: " + test.copiedText("count"))
            test.stream.scrollToDay(test.rowA, false)
        }
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB) && test.selected(test.rowA))
        if (test.at()) {
            test.check(test.day(test.rowA).selectionCount > 0 && test.day(test.rowB).selectionCount > 0,
                       "scrolled back, the days show their parts again: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
            test.key(test.touch ? Qt.Key_Back : Qt.Key_Escape, 0)
        }
        if (test.at())
            test.waitFor(test.noneSelected())
        if (test.at())
            test.check(test.noneSelected(), test.touch ? "Back clears every day" : "Esc clears every day again")
    }

    function phoneAwaySteps(): void {
        // ---- Every day of a span scrolled away: a tap edits --------------------------------------
        if (test.at())
            test.stream.scrollToDay(test.rowA, false)
        if (test.at())
            test.waitFor(test.loaded(test.rowA) && test.loaded(test.rowB))
        test.longPressSteps()
        if (test.at())
            test.click(test.rowB, 2, Qt.NoModifier)
        if (test.at())
            test.waitFor(test.selection.active)
        if (test.at()) {
            test.rowFar = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -330))
            test.stream.scrollToDay(test.rowFar, false)
        }
        if (test.at())
            test.waitFor(test.day(test.rowA) === null && test.loaded(test.rowFar)
                         && (test.day(test.rowB) === null || test.stream.activeDay === test.rowB))
        if (test.at()) {
            test.check(test.selection.active && test.selection.toolsController === null, "no day of the span on screen: no tools")
            test.click(test.rowFar, 1, Qt.NoModifier)
        }
        if (test.at())
            test.waitFor(test.day(test.rowFar).editingRow === 1)
        if (test.at()) {
            test.check(!test.selection.active && test.day(test.rowFar).selectionCount === 0,
                       "a tap there edits and ends the span: " + test.selection.pageCount)
            test.day(test.rowFar).stopEditing()
        }
    }

    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.stream !== null && test.days !== null && test.selection !== null && test.editor !== null
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
