pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// ContentJournalsSelfTest — dev: the journal stream's data safety in the real app, headless
// (`brulion --demo --route journals --editor-script journals-selftest`, ctest
// tst_content_journals_qml; JournalsView loads it next to its list). Every day of the stream is
// its own editor (PageController) inside a lazy ListView, so a day being edited is destroyed when
// it scrolls far enough away. Checked with real key and input-method events:
//   * text typed into today and scrolled away is saved and shown again when scrolled back;
//   * a day switched to a document keeps the outline days' left edge (unless the journal's
//     default mode is document);
//   * text typed into an older day and scrolled away at once (before the debounced save, its
//     delegate destroyed) is in the file, also the word the keyboard was still composing;
//   * a day scrolled away while its editor has deferred work queued: nothing runs on it then;
//   * a same-day link to a heading far up a long day scrolls the stream there (the day's own list
//     never scrolls) and selects the heading;
//   * midnight while a day is being edited (the list's today moves on): the new day is listed
//     first, the edited day keeps its editor and caret, is no longer "today", and what is typed
//     after midnight lands in its own file — nothing is written to the new day;
//   * the touch toolbar on a simulated keyboard (KeyboardInsets): docked on the keyboard, not at
//     the day's bottom, the day does not grow for it, it stays put while the list scrolls (also
//     by a finger), the caret of a day typed into near the keyboard stays above it without
//     runaway scrolling, one toolbar when another day is edited.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
Item {
    id: test

    /// The journal list (ListView over `days`) and its model.
    property ListView list
    property JournalListModel days

    property int failures: 0
    property int stepIndex: 0
    property int waited: 0
    property var steps: []

    property ContentJournalDay tracked: null
    property string trackedPath: ""
    property string trackedDate: ""
    property string secondPath: ""
    property real dayHeight: 0
    property real slotY: 0
    property real contentBefore: 0
    property int linesBefore: 0
    property var events: null
    property int dragStep: 0
    property int linkRow: -1

    function check(ok, what) {
        if (!ok)
            failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    /// The ContentJournalDay of a row (null when its delegate does not exist).
    function dayAt(row) {
        const item = list.itemAtIndex(row)
        return item ? item.day : null
    }
    function typeKeys(edit, s) {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(edit, 0, 0, s.charAt(i))
    }
    /// Start editing a new block at the end of `day`.
    function editNewBlock(day) {
        const c = day.controller
        const row = c.blocks.count > 0 ? c.insertBlockAfter(c.lastVisibleRow(), "") : c.appendEmptyBlock()
        day.editor.startEditing(row, -1)
    }
    function editing(day) {
        return day !== null && day.editor.activeEditor !== null && day.editor.activeEditor.activeFocus
               && day.controller.editingRow >= 0
    }
    function fileHas(path, text) {
        return Workspace.readFile(path).indexOf(text) >= 0
    }
    // ---- The touch toolbar, docked on the journal stream's frame (the list's parent) -----------
    function visibleSlots() {
        const frame = test.list.parent
        const out = []
        for (let i = 0; frame && i < frame.children.length; ++i) {
            const item = frame.children[i]
            if (item.objectName === "editToolbarSlot" && item.visible)
                out.push(item)
        }
        return out
    }
    function slotTop() {
        const slots = visibleSlots()
        return slots.length === 1 ? slots[0].mapToItem(null, 0, 0).y : -1
    }
    function slotBottom() {
        const slots = visibleSlots()
        return slots.length === 1 ? slots[0].mapToItem(null, 0, slots[0].height).y : -1
    }
    function windowHeight() {
        return test.Window.window ? test.Window.window.height : 0
    }
    function near(a, b) {
        return Math.abs(a - b) <= 1
    }
    function caretBottom(day) {
        const edit = day ? day.editor.activeEditor : null
        if (!edit || !edit.activeFocus)
            return 1e9
        const r = edit.cursorRectangle
        return edit.mapToItem(null, r.x, r.y + r.height).y
    }
    /// A finger dragging over the list (QtTest's TestEvent), one move per step.
    function dragSteps() {
        const x = test.list.width / 2
        const y0 = test.list.height * 0.45
        const out = [() => {
            test.dragStep = 0
            test.events.touchEvent(test.list).press(0, test.list, x, y0).commit()
        }]
        for (let i = 1; i <= 6; ++i)
            out.push(() => { test.events.touchEvent(test.list).move(0, test.list, x, y0 - 30 * i).commit() })
        out.push(() => { test.events.touchEvent(test.list).release(0, test.list, x, y0 - 180).commit() })
        return out
    }

    Timer {
        interval: 120
        repeat: true
        running: test.list !== null && test.days !== null
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
                return // a waiting step: again on the next tick (~10 s at most)
            if (!done)
                test.check(false, "step " + test.stepIndex + " timed out")
            test.waited = 0
            test.stepIndex += 1
        }
    }

    Component.onCompleted: {
        steps = [
            // ---- A notebook with two months of days ------------------------------------------
            () => Workspace.ready && test.days.count >= 2 && test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                for (let n = 2; n <= 60; ++n) {
                    const date = DatesHelper.addDays(DatesHelper.today, -n)
                    const path = Workspace.journalPath(date)
                    if (Workspace.exists(path))
                        continue
                    let md = ""
                    if (n === 45) {
                        // A long day: its heading far above a link to it.
                        md = "## Cel podróży\n\n"
                        for (let b = 0; b < 40; ++b)
                            md += "- Dzień " + n + ", punkt " + b + "\n"
                        md += "- Zobacz [[#Cel podróży]]\n"
                    }
                    for (let b = 0; n !== 45 && b < 10; ++b)
                        md += "- Dzień " + n + ", notatka " + b + ": zażółć gęślą jaźń\n"
                    Workspace.writeFile(path, md)
                }
            },
            () => test.days.count >= 55,
            // ---- Today: typed and scrolled away before the save --------------------------------------
            () => {
                test.list.positionViewAtBeginning()
                test.tracked = test.dayAt(0)
                test.check(test.tracked !== null && test.tracked.isToday, "today is the first day")
                test.trackedPath = test.tracked.path
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => {
                test.typeKeys(test.tracked.editor.activeEditor, "Kupić mleko")
                test.check(test.tracked.controller.dirty, "today has unsaved text")
                test.list.positionViewAtEnd()
            },
            () => test.tracked === null, // the delegate (and its editor) is gone
            () => test.check(test.fileHas(test.trackedPath, "Kupić mleko"), "today's text is saved when it scrolls away"),
            () => test.list.positionViewAtBeginning(),
            () => test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                const c = test.dayAt(0).controller
                let found = false
                for (let i = 0; i < c.blocks.count; ++i)
                    found = found || String(c.blocks.get(i).plainText).indexOf("Kupić mleko") >= 0
                test.check(found, "scrolled back, today shows the text")
                c.stopEditing()
            },
            // ---- Note modes: a day switched to a document keeps the outline days' left edge --------
            () => test.check(test.dayAt(0).controller.setMode("document"), "today switches to a document"),
            () => test.dayAt(0) !== null && test.dayAt(0).controller.documentMode,
            () => {
                const ed = test.dayAt(0).editor
                test.check(ed.documentMode && ed.gutter === ed.outlineGutter && ed.gutter > 0,
                           "a document day among outline days keeps their left edge: " + ed.gutter)
                Workspace.setConfig({ journalMode: "document" })
            },
            () => Workspace.config.journalMode === "document",
            () => {
                const ed = test.dayAt(0).editor
                test.check(ed.documentMode && ed.gutter === 0, "with documents by default, days use the full width")
                Workspace.setConfig({ journalMode: "outline" })
                test.dayAt(0).controller.setMode("outline")
            },
            () => Workspace.config.journalMode === "outline" && !test.dayAt(0).controller.documentMode,
            // ---- The touch toolbar on a keyboard (simulated) -----------------------------------------
            () => {
                test.list.positionViewAtBeginning()
                test.tracked = test.dayAt(0)
                test.dayHeight = test.tracked.height
                test.tracked.editor.startEditing(test.tracked.controller.lastVisibleRow(), -1)
                KeyboardInsets.simulated = 300
            },
            () => test.editing(test.tracked) && test.visibleSlots().length === 1
                  && test.near(test.slotBottom(), test.windowHeight() - 300),
            () => {
                test.check(Math.abs(test.tracked.height - test.dayHeight) < 1,
                           "the day does not grow for the toolbar: " + test.tracked.height + " = " + test.dayHeight)
                test.check(test.near(test.slotBottom(), test.windowHeight() - 300),
                           "today's toolbar sits on the keyboard, not at the day's bottom: " + test.slotBottom())
                test.slotY = test.slotTop()
                test.list.contentY += 200
            },
            () => {
                test.check(test.visibleSlots().length === 1 && test.near(test.slotTop(), test.slotY),
                           "scrolling the journal by 200 px leaves the toolbar on the keyboard: " + test.slotTop() + " = " + test.slotY)
                try {
                    test.events = Qt.createQmlObject('import QtTest; TestEvent {}', test, "journalEvents")
                } catch (e) {
                    test.events = null
                }
                test.list.positionViewAtBeginning()
            },
            ...(() => {
                // A finger flick over the list (without QtTest's TestEvent: skipped).
                const steps = test.dragSteps()
                return steps.map(step => () => { if (test.events) step() })
            })(),
            () => !test.list.moving,
            () => {
                if (test.events)
                    test.check(test.list.contentY > 50 && test.visibleSlots().length === 1 && test.near(test.slotTop(), test.slotY),
                               "a finger flicking the journal leaves the toolbar on the keyboard: " + test.slotTop() + " (list at "
                               + test.list.contentY + ")")
                test.tracked.controller.stopEditing()
                test.list.positionViewAtIndex(6, ListView.End)
            },
            () => test.dayAt(6) !== null && test.dayAt(6).controller.loaded,
            () => {
                test.tracked = test.dayAt(6)
                test.tracked.editor.startEditing(test.tracked.controller.lastVisibleRow(), -1)
            },
            () => test.editing(test.tracked) && test.caretBottom(test.tracked) <= test.slotTop(),
            () => {
                test.check(test.visibleSlots().length === 1, "another day edited: one toolbar")
                test.contentBefore = test.list.contentY
                test.linesBefore = test.tracked.editor.activeEditor.lineCount
                test.typeKeys(test.tracked.editor.activeEditor, " — zażółć gęślą jaźń, źdźbło i łódź")
            },
            () => test.caretBottom(test.tracked) <= test.slotTop(),
            () => {
                const lines = test.tracked.editor.activeEditor.lineCount - test.linesBefore
                const moved = test.list.contentY - test.contentBefore
                test.check(moved >= -1 && moved <= lines * test.tracked.editor.lineHeight + 1,
                           "typing near the keyboard scrolls at most a line per new line: " + moved + " px, " + lines + " line(s)")
                test.check(test.caretBottom(test.tracked) <= test.slotTop(), "the caret stays above the toolbar")
                test.tracked.controller.stopEditing()
                KeyboardInsets.simulated = -1
            },
            () => test.visibleSlots().length === 0,
            // ---- An older day: typed, composing, scrolled away before the save --------------------
            () => test.list.positionViewAtIndex(20, ListView.Beginning),
            () => test.dayAt(20) !== null && test.dayAt(20).controller.loaded,
            () => {
                test.tracked = test.dayAt(20)
                test.secondPath = test.tracked.path
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => {
                // One step, well inside the editor's save delay (400 ms).
                const edit = test.tracked.editor.activeEditor
                test.typeKeys(edit, "Dopisane później ąę ")
                EditorInput.sendInputMethod(edit, "zażółć")
                test.check(edit.preeditText === "zażółć", "a word is being composed")
                test.check(test.tracked.controller.dirty, "the day has unsaved text")
                test.list.positionViewAtBeginning()
            },
            () => test.tracked === null, // the delegate (and its editor) is gone
            () => {
                const content = Workspace.readFile(test.secondPath)
                test.check(content.indexOf("Dopisane później ąę") >= 0, "an older day's edit is saved when it scrolls away")
                test.check(content.indexOf("zażółć") >= 0, "the word being composed is saved too")
            },
            // ---- A day scrolled away with deferred work queued (no TypeError, ctest) ----------------
            () => test.list.positionViewAtIndex(30, ListView.Beginning),
            () => test.dayAt(30) !== null && test.dayAt(30).controller.loaded,
            () => {
                const ed = test.dayAt(30).editor
                ed.revealRow(0)
                ed.ensureRowVisible(0)
                ed.editorFocusLost()
                ed.handleCommandResult(0, { action: "complete" })
                test.list.positionViewAtBeginning()
            },
            () => test.dayAt(30) === null,
            // ---- A same-day heading link: the stream scrolls up to the heading ------------------------
            () => {
                test.linkRow = test.days.rowOfDate(DatesHelper.addDays(DatesHelper.today, -45))
                test.check(test.linkRow > 0, "the long day is listed")
                test.list.positionViewAtIndex(test.linkRow, ListView.End)
            },
            () => test.dayAt(test.linkRow) !== null && test.dayAt(test.linkRow).controller.loaded,
            () => {
                // Its end in view once it has its height.
                test.list.positionViewAtIndex(test.linkRow, ListView.End)
                test.tracked = test.dayAt(test.linkRow)
            },
            () => {
                const day = test.tracked
                const head = day ? day.editor.listView.itemAtIndex(0) : null
                test.check(head !== null && day.controller.isHeading(0) && head.mapToItem(test.list, 0, head.height).y < 0,
                           "the day's heading is above the visible part")
                if (day)
                    day.editor.activateLink(day.controller.lastVisibleRow(), "page:#Cel%20podr%C3%B3%C5%BCy")
            },
            () => test.tracked !== null && test.tracked.controller.selectedRow === 0,
            () => {
                const head = test.tracked.editor.listView.itemAtIndex(0)
                const top = head ? head.mapToItem(test.list, 0, 0).y : -1
                test.check(top >= 0 && top + head.height <= test.list.height,
                           "following a same-day heading link scrolls the journal to the heading: " + top)
                test.tracked.controller.selectedRow = -1
                test.list.positionViewAtBeginning()
            },
            // ---- Midnight while editing --------------------------------------------------------------
            () => test.dayAt(0) !== null && test.dayAt(0).controller.loaded,
            () => {
                test.tracked = test.dayAt(0)
                test.trackedPath = test.tracked.path
                test.trackedDate = test.tracked.date
                test.editNewBlock(test.tracked)
            },
            () => test.editing(test.tracked),
            () => test.typeKeys(test.tracked.editor.activeEditor, "Przed północą"),
            // The list's clock moves on (the app's DatesHelper.today does that at 00:00).
            () => { test.days.today = DatesHelper.addDays(test.trackedDate, 1) },
            () => test.days.count > 0 && test.days.get(0).date === DatesHelper.addDays(test.trackedDate, 1),
            () => {
                test.check(test.tracked !== null, "the edited day keeps its editor over midnight")
                if (!test.tracked)
                    return
                test.check(test.tracked.date === test.trackedDate && !test.tracked.isToday,
                           "the edited day is no longer today: " + test.tracked.date + " " + test.tracked.isToday)
                const edit = test.tracked.editor.activeEditor
                test.check(test.editing(test.tracked), "the caret stays in the edited day (editingRow "
                           + test.tracked.controller.editingRow + ", editor " + (edit ? edit.activeFocus : "none")
                           + ", focus on " + test.Window.activeFocusItem + ")")
                const first = test.dayAt(0)
                test.check(first !== null && first.isToday && first.date === test.days.today, "the new day comes first")
            },
            () => { if (test.editing(test.tracked)) test.typeKeys(test.tracked.editor.activeEditor, " i po północy") },
            () => { if (test.tracked) test.tracked.controller.save() },
            () => {
                test.check(test.fileHas(test.trackedPath, "Przed północą i po północy"),
                           "text typed over midnight is in the day's own file")
                test.check(!Workspace.exists(Workspace.journalPath(test.days.today)), "nothing was written to the new day")
                let todays = 0
                for (let i = 0; i < 3; ++i) {
                    const d = test.dayAt(i)
                    if (d && d.isToday)
                        todays += 1
                }
                test.check(todays === 1, "one day says today: " + todays)
            },
            // ---- The next midnight: the unwritten day goes, the edited one keeps the caret ---------
            () => {
                test.check(test.editing(test.tracked), "still editing the day before")
                test.days.today = DatesHelper.addDays(test.trackedDate, 2)
            },
            () => test.days.count > 0 && test.days.get(0).date === DatesHelper.addDays(test.trackedDate, 2),
            () => {
                test.check(test.days.exactRowOfDate(DatesHelper.addDays(test.trackedDate, 1)) < 0,
                           "the day nobody wrote in is no longer listed")
                test.check(test.tracked !== null && test.editing(test.tracked), "rows removed above keep the caret in the edited day")
            },
            () => { if (test.editing(test.tracked)) test.typeKeys(test.tracked.editor.activeEditor, " — dalej") },
            () => { if (test.tracked) test.tracked.controller.save() },
            () => test.check(test.fileHas(test.trackedPath, "po północy — dalej"), "typing goes on in the edited day"),
        ]
    }
}
