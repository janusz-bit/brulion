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
//     nothing (only copying works); Shift+↓ past a day's end into the next day and Shift+↑ back;
//     Esc clears every day; a mouse drag from one day into the next; a plain click edits and ends
//     the span;
//   * phone: a long press (the selection mode), a tap in the next day extends the selection
//     there; one docked toolbar with the stream's count; its Wytnij changes nothing, Kopiuj copies;
//     a tap two days further; Back clears every day;
//   * both: the anchor day scrolled far away (destroyed) and back gets its part again; copying
//     meanwhile reads it from its file.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
Item {
    id: test

    property ListView list
    property JournalListModel days
    property StreamSelection stream

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property var steps: []
    property var events: null
    /// Rows of the three days written for the test (newest first: A above B above C).
    property int rowA: -1
    property int rowB: -1
    property int rowC: -1
    property string fileA: ""

    readonly property bool touch: Theme.touchUi

    function check(ok, what) {
        if (!ok)
            failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    function dayAt(row) {
        const item = row >= 0 ? list.itemAtIndex(row) : null
        for (let i = 0; item && i < item.children.length; ++i) {
            const day = item.children[i] as ContentJournalDay
            if (day)
                return day
        }
        return null
    }
    function loaded(row) {
        const d = dayAt(row)
        return d !== null && d.controller.loaded && blockText(d, 0) !== null
    }
    function blockText(day, row) {
        const item = day ? day.editor.listView.itemAtIndex(row) : null
        return item ? richTextIn(item) : null
    }
    function richTextIn(item) {
        if (!item || !item.visible)
            return null
        if (item.textFormat === Text.RichText && typeof item.linkAt === "function")
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = richTextIn(item.children[i])
            if (found)
                return found
        }
        return null
    }
    function findShown(item, name) {
        if (!item || !item.visible)
            return null
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findShown(item.children[i], name)
            if (found)
                return found
        }
        return null
    }
    /// Items named `name` shown on the stream's frame (the docked toolbar, the floating tools).
    function shownOnFrame(name) {
        const frame = test.list.parent
        const out = []
        for (let i = 0; frame && i < frame.children.length; ++i) {
            const found = findShown(frame.children[i], name)
            if (found)
                out.push(found)
        }
        return out
    }
    function rows(row) {
        const d = dayAt(row)
        return d ? JSON.stringify(d.controller.selectedRows()) : "gone"
    }
    function idle(n) {
        const out = []
        for (let i = 0; i < n; ++i)
            out.push(() => {})
        return out
    }
    function click(item, modifiers) {
        if (test.touch) {
            test.events.touchEvent(item).press(0, item, 20, item.height / 2).commit()
            test.events.touchEvent(item).release(0, item, 20, item.height / 2).commit()
        } else {
            test.events.mouseClick(item, 20, item.height / 2, Qt.LeftButton, modifiers, -1)
        }
    }
    /// A key to what has the keyboard (the day the selection's end is in has it).
    function key(k, modifiers) {
        EditorInput.sendKey(test.Window.activeFocusItem, k, modifiers, "")
    }
    function clipboard() {
        const d = dayAt(test.rowA) || dayAt(test.rowB) || dayAt(test.rowC)
        return d ? d.controller.clipboardText() : ""
    }
    /// Nothing selected anywhere in the stream.
    function noneSelected() {
        for (const r of [test.rowA, test.rowB, test.rowC]) {
            const d = dayAt(r)
            if (d && d.controller.selectionCount > 0)
                return false
        }
        return !test.stream.active
    }

    Timer {
        interval: 120
        repeat: true
        running: test.list !== null && test.days !== null && test.stream !== null
        onTriggered: {
            if (test.stepIndex >= test.steps.length) {
                stop()
                console.log("SELFTEST DONE failures=" + test.failures)
                Qt.exit(test.failures === 0 ? 0 : 1)
                return
            }
            let done = true
            try {
                done = test.steps[test.stepIndex]() !== false
            } catch (e) {
                test.check(false, "step " + test.stepIndex + " threw: " + e)
            }
            if (!done && ++test.waited < 80)
                return
            if (!done)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }

    Component.onCompleted: {
        const common = [
            () => Workspace.ready && test.days.count >= 1 && test.dayAt(0) !== null,
            () => {
                test.events = Qt.createQmlObject('import QtTest; TestEvent {}', test, "spanEvents")
                // Three consecutive days, long ago (nothing of the sample notebook between them).
                for (let n = 300; n <= 302; ++n) {
                    let md = ""
                    for (let b = 0; b < 4; ++b)
                        md += "- Dzień " + n + ", blok " + b + ": zażółć gęślą jaźń\n"
                    Workspace.writeFile(Workspace.journalPath(DatesHelper.addDays(DatesHelper.today, -n)), md)
                }
                test.fileA = Workspace.journalPath(DatesHelper.addDays(DatesHelper.today, -300))
                // Older days below them: enough to scroll them out of the list's cache.
                for (let n = 303; n < 333; ++n) {
                    let md = ""
                    for (let b = 0; b < 10; ++b)
                        md += "- Wcześniej " + n + ", notatka " + b + "\n"
                    Workspace.writeFile(Workspace.journalPath(DatesHelper.addDays(DatesHelper.today, -n)), md)
                }
            },
            () => {
                test.rowA = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -300))
                test.rowB = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -301))
                test.rowC = test.days.exactRowOfDate(DatesHelper.addDays(DatesHelper.today, -302))
                return test.rowA > 0 && test.rowB === test.rowA + 1 && test.rowC === test.rowB + 1 && test.days.count > test.rowC + 29
            },
            () => test.list.positionViewAtIndex(test.rowA, ListView.Beginning),
            () => test.loaded(test.rowA) && test.loaded(test.rowB),
        ]
        const desktop = [
            // ---- Shift+click into the next day ---------------------------------------------------
            () => {
                test.dayAt(test.rowA).editor.selectBlock(1)
                test.click(test.blockText(test.dayAt(test.rowB), 2), Qt.ShiftModifier)
            },
            () => {
                test.check(test.stream.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                           "Shift+click in the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
                test.check(test.stream.count === 6 && test.stream.pageCount === 2, "the stream counts 6 blocks of 2 days: " + test.stream.count)
                const bars = test.shownOnFrame("floatingSelectionTools")
                test.check(bars.length === 1 && test.stream.toolsController === test.dayAt(test.rowB).controller,
                           "one floating tool bar, the clicked day's: " + bars.length)
                test.key(Qt.Key_C, Qt.ControlModifier)
            },
            () => {
                const text = test.clipboard()
                test.check(text.indexOf("[[") === 0 && text.indexOf("]]\n\n- Dzień 300, blok 1") > 0
                           && text.indexOf("]]\n\n- Dzień 301, blok 0") > 0 && text.endsWith("- Dzień 301, blok 2: zażółć gęślą jaźń"),
                           "Ctrl+C copies both days' blocks after their links: " + text.slice(0, 60))
                test.key(Qt.Key_Delete, 0)
                test.key(Qt.Key_Tab, 0)
            },
            () => test.dayAt(test.rowA).controller.save(),
            () => {
                test.check(test.stream.active && test.dayAt(test.rowA).controller.blocks.count === 4
                           && Workspace.readFile(test.fileA).indexOf("- Dzień 300, blok 3") >= 0
                           && !test.dayAt(test.rowA).controller.canUndo && !test.dayAt(test.rowB).controller.canUndo,
                           "Delete and Tab change nothing over several days")
                // Shift+↓ to the day's end, then past it into the third day.
                test.key(Qt.Key_Down, Qt.ShiftModifier)
                test.key(Qt.Key_Down, Qt.ShiftModifier)
            },
            () => test.loaded(test.rowC),
            () => {
                test.check(test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0]"
                           && test.stream.focusPath === test.dayAt(test.rowC).path,
                           "Shift+↓ past a day's end goes on in the next day: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
                test.key(Qt.Key_Up, Qt.ShiftModifier)
                test.key(Qt.Key_Up, Qt.ShiftModifier)
            },
            () => {
                test.check(test.rows(test.rowC) === "[]" && test.rows(test.rowB) === "[0,1,2]",
                           "Shift+↑ comes back: " + test.rows(test.rowB) + " " + test.rows(test.rowC))
                test.key(Qt.Key_Escape, 0)
            },
            () => test.check(test.noneSelected(), "Esc clears every day"),
            // ---- A mouse drag from one day into the next -----------------------------------------
            () => {
                const a0 = test.blockText(test.dayAt(test.rowA), 0)
                test.events.mousePress(a0, 30, a0.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
                test.events.mouseMove(a0, 50, a0.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const a2 = test.blockText(test.dayAt(test.rowA), 2)
                test.events.mouseMove(a2, 50, a2.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const b1 = test.blockText(test.dayAt(test.rowB), 1)
                test.events.mouseMove(b1, 50, b1.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
            },
            () => {
                const b1 = test.blockText(test.dayAt(test.rowB), 1)
                test.events.mouseRelease(b1, 50, b1.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            },
            () => {
                test.check(test.stream.active && test.rows(test.rowA) === "[0,1,2,3]" && test.rows(test.rowB) === "[0,1]",
                           "a mouse drag into the next day selects across both: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
                // A plain click edits a block and ends the selection.
                test.click(test.blockText(test.dayAt(test.rowC), 1), Qt.NoModifier)
            },
            () => test.dayAt(test.rowC).controller.editingRow === 1,
            () => {
                test.check(test.noneSelected(), "a click to edit ends the selection over several days")
                test.dayAt(test.rowC).controller.stopEditing()
                // Again from A for the scrolling part.
                test.dayAt(test.rowA).editor.selectBlock(2)
                test.click(test.blockText(test.dayAt(test.rowB), 1), Qt.ShiftModifier)
            },
        ]
        const phone = [
            // ---- A long press, then a tap in the next day ----------------------------------------------
            () => {
                const a1 = test.blockText(test.dayAt(test.rowA), 1)
                test.events.touchEvent(a1).press(0, a1, a1.width / 2, a1.height / 2).commit()
            },
            ...test.idle(9),
            () => {
                const a1 = test.blockText(test.dayAt(test.rowA), 1)
                test.events.touchEvent(a1).release(0, a1, a1.width / 2, a1.height / 2).commit()
            },
            () => test.dayAt(test.rowA).editor.selectionMode,
            () => test.click(test.blockText(test.dayAt(test.rowB), 2), Qt.NoModifier),
            () => {
                test.check(test.stream.active && test.rows(test.rowA) === "[1,2,3]" && test.rows(test.rowB) === "[0,1,2]",
                           "a tap in the next day extends the selection there: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
                const tools = test.shownOnFrame("toolbarSelectionTools")
                test.check(tools.length === 1 && test.dayAt(test.rowB).editor.toolbarVisible
                           && !test.dayAt(test.rowA).editor.toolbarVisible, "one docked toolbar: " + tools.length)
                const count = tools.length === 1 ? test.findShown(tools[0], "selectionCount") : null
                test.check(count !== null && count.text.indexOf("6") >= 0, "it counts the blocks of both days: " + (count ? count.text : ""))
                const cut = tools.length === 1 ? test.findShown(tools[0], "selectionCut") : null
                if (cut)
                    test.click(cut, Qt.NoModifier)
            },
            () => {
                test.check(test.stream.active && test.dayAt(test.rowA).controller.blocks.count === 4
                           && !test.dayAt(test.rowA).controller.canUndo, "Wytnij changes nothing over several days")
                const tools = test.shownOnFrame("toolbarSelectionTools")
                const copy = tools.length === 1 ? test.findShown(tools[0], "selectionCopy") : null
                if (copy)
                    test.click(copy, Qt.NoModifier)
            },
            () => {
                const text = test.clipboard()
                test.check(text.indexOf("[[") === 0 && text.indexOf("]]\n\n- Dzień 300, blok 1") > 0
                           && text.endsWith("- Dzień 301, blok 2: zażółć gęślą jaźń"),
                           "Kopiuj copies both days' blocks after their links: " + text.slice(0, 60))
                test.list.positionViewAtIndex(test.rowC, ListView.Beginning)
            },
            () => test.loaded(test.rowC),
            () => test.click(test.blockText(test.dayAt(test.rowC), 1), Qt.NoModifier),
            () => test.check(test.stream.pageCount === 3 && test.rows(test.rowB) === "[0,1,2,3]" && test.rows(test.rowC) === "[0,1]",
                             "a tap two days further takes the day between whole: " + test.rows(test.rowB) + " " + test.rows(test.rowC)),
        ]
        const scrolling = [
            // ---- The anchor day scrolled away and back ----------------------------------------------
            () => {
                test.check(test.stream.active, "a selection over several days to scroll with")
                test.list.positionViewAtEnd()
            },
            () => test.dayAt(test.rowA) === null && test.dayAt(test.rowB) === null,
            () => {
                test.check(test.stream.active, "the days scrolled away keep the selection")
                const copied = test.stream.copy(false)
                test.check(copied.ok && test.stream.markdown().indexOf("- Dzień 300, blok") > 0,
                           "copied from the files meanwhile: " + copied.count)
                test.list.positionViewAtIndex(test.rowA, ListView.Beginning)
            },
            () => test.loaded(test.rowA) && test.loaded(test.rowB),
            () => {
                test.check(test.dayAt(test.rowA).controller.selectionCount > 0 && test.dayAt(test.rowB).controller.selectionCount > 0,
                           "scrolled back, the days show their parts again: " + test.rows(test.rowA) + " " + test.rows(test.rowB))
                test.key(test.touch ? Qt.Key_Back : Qt.Key_Escape, 0)
            },
            () => test.check(test.noneSelected(), test.touch ? "Back clears every day" : "Esc clears every day again"),
        ]
        steps = common.concat(test.touch ? phone : desktop, scrolling)
    }
}
