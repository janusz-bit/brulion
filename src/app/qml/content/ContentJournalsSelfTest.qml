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
//     runaway scrolling, one toolbar when another day is edited;
//   * selecting blocks in a day (docs/UI.md "Selecting and copying"): a finger's long press (the
//     selection mode; the stream holds still while the finger extends it, scrolls at the frame's
//     edge and the selection goes on into the next day — StreamSelection —, the selection tools
//     dock on the frame, Kopiuj with the days' links), another day's selection ending it, Back; a
//     mouse drag from a block into the next ones, the stream scrolled at its edge;
//   * Enter at the end of a zoomed paragraph at the bottom of a long day: the zoom ends, the stream
//     keeps the new block at the grown day's bottom in view; Ctrl+Z zooms back in, still in view.
// Prints "SELFTEST PASS|FAIL <what>" lines and quits with the number of failures as exit code.
// Compiled to C++ like the rest of the app's QML (docs/UI.md "Compiled QML"): the steps are one
// typed function (runSteps(): each step an `if (test.at())` block, in order), events through
// DevEvents (QtTest's TestEvent made when first used), a step's exception caught by
// DevTest.callCatching().
Item {
    id: test

    /// The journal list (ListView over `days`) and its model.
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

    property ContentJournalDay tracked: null
    property string trackedPath: ""
    property string trackedDate: ""
    property string secondPath: ""
    property real dayHeight: 0
    property real slotY: 0
    property real contentBefore: 0
    property int linesBefore: 0
    property int dragStep: 0
    property int linkRow: -1
    /// The zoomed paragraph of the long day and the day's text before it was added.
    property int zoomRow: -1
    property string dayText: ""
    /// Items and points being looked at (a call's item or point is read through a property,
    /// compiled).
    property Item _item: null
    property Item _row: null
    property Item _slot: null
    property Item _child: null
    property point _at: Qt.point(0, 0)
    property rect _rect: Qt.rect(0, 0, 0, 0)

    DevEvents { id: events }
    /// A block's fields (BlockListModel.get()).
    DevBlock { id: block }

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
    /// The ContentJournalDay of a row (null when its delegate does not exist).
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
    function dayLoaded(row: int): bool {
        const d = test.dayAt(row)
        return d !== null && d.controller.loaded
    }
    function typeKeys(edit: Item, s: string): void {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(edit, 0, 0, Js.charAt(s, i))
    }
    /// Start editing a new block at the end of `day`.
    function editNewBlock(day: ContentJournalDay): void {
        const c = day.controller
        const row = c.blocks.count > 0 ? c.insertBlockAfter(c.lastVisibleRow(), "") : c.appendEmptyBlock()
        day.editor.startEditing(row, -1)
    }
    function editing(day: ContentJournalDay): bool {
        return day !== null && day.editor.activeEditor !== null && day.editor.activeEditor.activeFocus
               && day.controller.editingRow >= 0
    }
    function fileHas(path: string, text: string): bool {
        return Js.indexOf(Workspace.readFile(path), text) >= 0
    }
    /// The rendered text of `row` in `day` (null when its block is not shown).
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
    /// Steps that do nothing (a finger held down meanwhile).
    function idle(n: int): void {
        for (let i = 0; i < n; ++i) {
            if (test.at()) {
                // (nothing)
            }
        }
    }
    // ---- The touch toolbar, docked on the journal stream's frame (the list's parent) -----------
    /// How many of the frame's toolbar slots are shown, and the first of them (_slot).
    function visibleSlots(): int {
        const frame = test.list.parent
        let count = 0
        test._slot = null
        const n = DevTest.childCount(frame)
        for (let i = 0; i < n; ++i) {
            test._child = DevTest.childAt(frame, i)
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
    function caretBottom(day: ContentJournalDay): real {
        const edit = day !== null ? day.editor.activeEditor : null
        if (edit === null || !edit.activeFocus)
            return 1e9
        test._rect = edit.cursorRectangle
        test._at = edit.mapToItem(null, test._rect.x, test._rect.y + test._rect.height)
        return test._at.y
    }
    /// Where `item` is in the list (its top, or its bottom).
    function topInList(item: Item): real {
        test._at = item.mapToItem(test.list, 0, 0)
        return test._at.y
    }
    function bottomInList(item: Item): real {
        test._at = item.mapToItem(test.list, 0, item.height)
        return test._at.y
    }
    /// A finger dragging over the list (QtTest's TestEvent), one move per step (none without it).
    function dragSteps(): void {
        const x = test.list.width / 2
        const y0 = test.list.height * 0.45
        if (test.at() && events.available()) {
            test.dragStep = 0
            events.touchPress(test.list, x, y0)
        }
        for (let i = 1; i <= 6; ++i) {
            if (test.at() && events.available())
                events.touchMove(test.list, x, y0 - 30 * i)
        }
        if (test.at() && events.available())
            events.touchRelease(test.list, x, y0 - 180)
    }
    /// A day's text holds `needle` (its blocks' plain text).
    function dayShows(day: ContentJournalDay, needle: string): bool {
        const c = day.controller
        for (let i = 0; i < c.blocks.count; ++i) {
            block.read(c.blocks.get(i))
            if (Js.indexOf(block.plainText, needle) >= 0)
                return true
        }
        return false
    }
    function writeDays(): void {
        for (let n = 2; n <= 60; ++n) {
            const path = "" + Workspace.journalPath("" + DatesHelper.addDays(DatesHelper.today, -n))
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
    }
    /// The date of the list's first row.
    function firstDate(): string {
        return AppController.routeText(test.days.get(0), "date")
    }

    function runSteps(): void {
        // ---- A notebook with two months of days ------------------------------------------
        if (test.at())
            test.waitFor(Workspace.ready && test.days.count >= 2 && test.dayLoaded(0))
        if (test.at())
            test.writeDays()
        if (test.at())
            test.waitFor(test.days.count >= 55)
        // ---- Today: typed and scrolled away before the save --------------------------------------
        if (test.at()) {
            test.list.positionViewAtBeginning()
            test.tracked = test.dayAt(0)
            test.check(test.tracked !== null && test.tracked.isToday, "today is the first day")
            test.trackedPath = test.tracked.path
            test.editNewBlock(test.tracked)
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked))
        if (test.at()) {
            test.typeKeys(test.tracked.editor.activeEditor, "Kupić mleko")
            test.check(test.tracked.controller.dirty, "today has unsaved text")
            test.list.positionViewAtEnd()
        }
        if (test.at())
            test.waitFor(test.tracked === null) // the delegate (and its editor) is gone
        if (test.at())
            test.check(test.fileHas(test.trackedPath, "Kupić mleko"), "today's text is saved when it scrolls away")
        if (test.at())
            test.list.positionViewAtBeginning()
        if (test.at())
            test.waitFor(test.dayLoaded(0))
        if (test.at()) {
            const day = test.dayAt(0)
            test.check(test.dayShows(day, "Kupić mleko"), "scrolled back, today shows the text")
            day.controller.stopEditing()
        }
        // ---- Note modes: a day switched to a document keeps the outline days' left edge --------
        if (test.at())
            test.check(test.dayAt(0).controller.setMode("document"), "today switches to a document")
        if (test.at())
            test.waitFor(test.dayAt(0) !== null && test.dayAt(0).controller.documentMode)
        if (test.at()) {
            const ed = test.dayAt(0).editor
            const gutter = 0 + ed.gutter
            const outlineGutter = 0 + ed.outlineGutter
            test.check(ed.documentMode && gutter === outlineGutter && gutter > 0,
                       "a document day among outline days keeps their left edge: " + gutter)
            Workspace.setConfig({ journalMode: "document" })
        }
        if (test.at())
            test.waitFor(Workspace.config.journalMode === "document")
        if (test.at()) {
            const ed = test.dayAt(0).editor
            test.check(ed.documentMode && 0 + ed.gutter === 0, "with documents by default, days use the full width")
            Workspace.setConfig({ journalMode: "outline" })
            test.dayAt(0).controller.setMode("outline")
        }
        if (test.at())
            test.waitFor(Workspace.config.journalMode === "outline" && !test.dayAt(0).controller.documentMode)
        // ---- The touch toolbar on a keyboard (simulated) -----------------------------------------
        if (test.at()) {
            test.list.positionViewAtBeginning()
            test.tracked = test.dayAt(0)
            test.dayHeight = test.tracked.height
            test.tracked.editor.startEditing(test.tracked.controller.lastVisibleRow(), -1)
            KeyboardInsets.simulated = 300
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked) && test.visibleSlots() === 1
                         && test.near(test.slotBottom(), test.windowHeight() - 300))
        if (test.at()) {
            test.check(Math.abs(test.tracked.height - test.dayHeight) < 1,
                       "the day does not grow for the toolbar: " + test.tracked.height + " = " + test.dayHeight)
            test.check(test.near(test.slotBottom(), test.windowHeight() - 300),
                       "today's toolbar sits on the keyboard, not at the day's bottom: " + test.slotBottom())
            test.slotY = test.slotTop()
            test.list.contentY += 200
        }
        if (test.at()) {
            test.check(test.visibleSlots() === 1 && test.near(test.slotTop(), test.slotY),
                       "scrolling the journal by 200 px leaves the toolbar on the keyboard: " + test.slotTop() + " = " + test.slotY)
            test.list.positionViewAtBeginning()
        }
        // A finger flick over the list (without QtTest's TestEvent: skipped).
        test.dragSteps()
        if (test.at())
            test.waitFor(!test.list.moving)
        if (test.at()) {
            if (events.available())
                test.check(test.list.contentY > 50 && test.visibleSlots() === 1 && test.near(test.slotTop(), test.slotY),
                           "a finger flicking the journal leaves the toolbar on the keyboard: " + test.slotTop() + " (list at "
                           + test.list.contentY + ")")
            test.tracked.controller.stopEditing()
            test.list.positionViewAtIndex(6, ListView.End)
        }
        if (test.at())
            test.waitFor(test.dayLoaded(6))
        if (test.at()) {
            test.tracked = test.dayAt(6)
            test.tracked.editor.startEditing(test.tracked.controller.lastVisibleRow(), -1)
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked) && test.caretBottom(test.tracked) <= test.slotTop())
        if (test.at()) {
            test.check(test.visibleSlots() === 1, "another day edited: one toolbar")
            test.contentBefore = test.list.contentY
            test.linesBefore = test.tracked.editor.activeEditor.lineCount
            test.typeKeys(test.tracked.editor.activeEditor, " — zażółć gęślą jaźń, źdźbło i łódź")
        }
        if (test.at())
            test.waitFor(test.caretBottom(test.tracked) <= test.slotTop())
        if (test.at()) {
            const lines = test.tracked.editor.activeEditor.lineCount - test.linesBefore
            const moved = test.list.contentY - test.contentBefore
            test.check(moved >= -1 && moved <= lines * test.tracked.editor.lineHeight + 1,
                       "typing near the keyboard scrolls at most a line per new line: " + moved + " px, " + lines + " line(s)")
            test.check(test.caretBottom(test.tracked) <= test.slotTop(), "the caret stays above the toolbar")
            test.tracked.controller.stopEditing()
            KeyboardInsets.simulated = -1
        }
        if (test.at())
            test.waitFor(test.visibleSlots() === 0)
        // ---- An older day: typed, composing, scrolled away before the save --------------------
        if (test.at())
            test.list.positionViewAtIndex(20, ListView.Beginning)
        if (test.at())
            test.waitFor(test.dayLoaded(20))
        if (test.at()) {
            test.tracked = test.dayAt(20)
            test.secondPath = test.tracked.path
            test.editNewBlock(test.tracked)
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked))
        if (test.at()) {
            // One step, well inside the editor's save delay (400 ms).
            const edit = test.tracked.editor.activeEditor
            test.typeKeys(edit, "Dopisane później ąę ")
            EditorInput.sendInputMethod(edit, "zażółć", "")
            test.check(edit.preeditText === "zażółć", "a word is being composed")
            test.check(test.tracked.controller.dirty, "the day has unsaved text")
            test.list.positionViewAtBeginning()
        }
        if (test.at())
            test.waitFor(test.tracked === null) // the delegate (and its editor) is gone
        if (test.at()) {
            const content = "" + Workspace.readFile(test.secondPath)
            test.check(Js.indexOf(content, "Dopisane później ąę") >= 0, "an older day's edit is saved when it scrolls away")
            test.check(Js.indexOf(content, "zażółć") >= 0, "the word being composed is saved too")
        }
        // ---- A day scrolled away with deferred work queued (no TypeError, ctest) ----------------
        if (test.at())
            test.list.positionViewAtIndex(30, ListView.Beginning)
        if (test.at())
            test.waitFor(test.dayLoaded(30))
        if (test.at()) {
            const ed = test.dayAt(30).editor
            ed.revealRow(0)
            ed.ensureRowVisible(0)
            ed.editorFocusLost()
            ed.handleCommandResult(0, { action: "complete" })
            test.list.positionViewAtBeginning()
        }
        if (test.at())
            test.waitFor(test.dayAt(30) === null)
        // ---- A same-day heading link: the stream scrolls up to the heading ------------------------
        if (test.at()) {
            test.linkRow = test.days.rowOfDate(DatesHelper.addDays(DatesHelper.today, -45))
            test.check(test.linkRow > 0, "the long day is listed")
            test.list.positionViewAtIndex(test.linkRow, ListView.End)
        }
        if (test.at())
            test.waitFor(test.dayLoaded(test.linkRow))
        if (test.at()) {
            // Its end in view once it has its height.
            test.list.positionViewAtIndex(test.linkRow, ListView.End)
            test.tracked = test.dayAt(test.linkRow)
        }
        if (test.at()) {
            const day = test.tracked
            const head = day !== null ? day.editor.listView.itemAtIndex(0) as Item : null
            test.check(head !== null && day.controller.isHeading(0) && test.bottomInList(head) < 0,
                       "the day's heading is above the visible part")
            if (day !== null)
                day.editor.activateLink(day.controller.lastVisibleRow(), "page:#Cel%20podr%C3%B3%C5%BCy")
        }
        if (test.at())
            test.waitFor(test.tracked !== null && test.tracked.controller.selectedRow === 0)
        if (test.at()) {
            const head = test.tracked.editor.listView.itemAtIndex(0) as Item
            const top = head !== null ? test.topInList(head) : -1
            test.check(top >= 0 && top + head.height <= test.list.height,
                       "following a same-day heading link scrolls the journal to the heading: " + top)
            test.tracked.controller.selectedRow = -1
            test.list.positionViewAtBeginning()
        }
        // ---- Selecting blocks in a day with a finger: the selection mode --------------------------
        if (test.at()) {
            // The long day (taller than the frame).
            test.list.positionViewAtIndex(test.linkRow, ListView.Beginning)
        }
        if (test.at())
            test.waitFor(test.dayLoaded(test.linkRow) && test.blockText(test.dayAt(test.linkRow), 1) !== null)
        if (test.at()) {
            test.tracked = test.dayAt(test.linkRow)
            test.contentBefore = test.list.contentY
            const text = test.blockText(test.tracked, 1)
            events.touchPress(text, text.width / 2, text.height / 2)
        }
        test.idle(9)
        if (test.at()) {
            const c = test.tracked.controller
            test.check(test.tracked.editor.selectionMode && c.selectedRow === 1 && c.editingRow < 0,
                       "a long press in a day selects its block (the selection mode)")
            test.check(!test.list.interactive, "the stream holds still while the finger selects")
            const text = test.blockText(test.tracked, 3)
            events.touchMove(text, text.width / 2, text.height / 2)
        }
        if (test.at())
            test.check(test.tracked.controller.selectionAnchorRow === 1 && test.tracked.controller.selectedRow === 3,
                       "moving the finger extends the selection: " + test.tracked.controller.selectedRow)
        if (test.at())
            events.touchMove(test.list, test.list.width / 2, test.list.height - 8)
        test.idle(8)
        if (test.at()) {
            const c = test.tracked.controller
            test.check(test.list.contentY > test.contentBefore + 50,
                       "at the frame's bottom edge the stream scrolls: " + test.list.contentY + " > " + test.contentBefore)
            test.check(c.selectionAnchorRow === 1 && c.selectedRow === c.lastVisibleRow(),
                       "the selection reaches the day's last block: " + c.selectedRow)
            test.check(test.stream.active && test.stream.anchorPath === test.tracked.path && test.stream.pageCount >= 2
                       && test.stream.focusPath !== AppController.routeText(test.days.get(test.linkRow - 1), "path"),
                       "and goes on into the days below: " + test.stream.focusPath)
            events.touchRelease(test.list, test.list.width / 2, test.list.height - 8)
        }
        if (test.at()) {
            test.check(test.list.interactive && test.tracked.editor.selectionMode, "lifted: the stream scrolls again, the selection stays")
            const slots = test.visibleSlots()
            test.check(slots === 1 && DevTest.findShown(test._slot, "toolbarSelectionTools") !== null,
                       "the selection tools are docked on the stream's frame")
            const copy = slots === 1 ? DevTest.findShown(test._slot, "selectionCopy") as Item : null
            if (copy !== null) {
                events.touchPress(copy, copy.width / 2, copy.height / 2)
                events.touchRelease(copy, copy.width / 2, copy.height / 2)
            }
        }
        if (test.at()) {
            const text = "" + test.tracked.controller.clipboardText()
            const head = "" + Js.slice(text, 0, 50)
            test.check(Js.indexOf(text, "[[") === 0 && Js.indexOf(text, "]]\n\n- Dzień 45, punkt 0\n- Dzień 45, punkt 1\n") > 0
                       && Js.indexOf(text, "- Zobacz [[#Cel podróży]]\n\n[[") > 0,
                       "Kopiuj copies the selected blocks of both days after their links: " + head)
            // Another day's own selection ends this one (one selection in the stream).
            const other = test.dayAt(test.linkRow + 1)
            test.check(other !== null, "the next day is there")
            if (other !== null)
                other.controller.selectRange(0, 1)
        }
        if (test.at()) {
            test.check(test.tracked.controller.selectionCount === 0 && !test.tracked.editor.selectionMode,
                       "selecting in another day ends this day's selection")
            const other = test.dayAt(test.linkRow + 1)
            if (other !== null) {
                other.controller.clearSelection()
                test.tracked.editor.selectBlock(2)
            }
        }
        if (test.at()) {
            test.check(test.tracked.editor.selectionMode, "the selection mode again")
            EditorInput.sendKey(test.tracked.editor.listView, Qt.Key_Back, 0, "")
        }
        if (test.at())
            test.check(!test.tracked.editor.selectionMode && test.tracked.controller.selectionCount === 0,
                       "Back leaves the selection mode")
        // ---- A mouse drag in a day selects its blocks; at the frame's edge the stream scrolls ------
        // (This run is a phone's: the mouse's own scrolling of the stream is off on desktop UIs —
        // `acceptedButtons`, as the page's list, checked by tst_editor_qml_selection.)
        if (test.at())
            test.list.positionViewAtIndex(test.linkRow, ListView.Beginning)
        if (test.at())
            test.waitFor(test.dayAt(test.linkRow) !== null && test.blockText(test.dayAt(test.linkRow), 1) !== null)
        if (test.at()) {
            test.tracked = test.dayAt(test.linkRow)
            test.contentBefore = test.list.contentY
            const text = test.blockText(test.tracked, 0)
            events.mousePress(text, 30, text.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            events.mouseMove(text, 50, text.height / 2 + 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at()) {
            const text = test.blockText(test.tracked, 2)
            events.mouseMove(text, 50, text.height / 2, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (test.at()) {
            const c = test.tracked.controller
            test.check(c.selectionAnchorRow === 0 && c.selectedRow === 2 && Math.abs(test.list.contentY - test.contentBefore) < 1,
                       "a mouse drag over a day's blocks selects them, the stream stays")
            events.mouseMove(test.list, test.list.width / 2, test.list.height - 6, -1, Qt.LeftButton, Qt.NoModifier)
        }
        test.idle(8)
        if (test.at()) {
            test.check(test.list.contentY > test.contentBefore + 50, "at the edge the stream scrolls: " + test.list.contentY)
            events.mouseRelease(test.list, test.list.width / 2, test.list.height - 6, Qt.LeftButton, Qt.NoModifier, -1)
        }
        if (test.at()) {
            const c = test.tracked.controller
            test.check(c.selectionAnchorRow === 0 && c.selectedRow === c.lastVisibleRow() && c.editingRow < 0,
                       "the drag ends with the day's blocks selected, nothing edited")
            c.clearSelection()
        }
        // ---- Enter at the end of a zoomed paragraph at the bottom of a long day -------------------
        // A paragraph has no children: the zoom ends, the day grows back to all its blocks, and
        // the stream keeps the new block below the paragraph (at the day's bottom) in view; Ctrl+Z
        // zooms back in, the paragraph still in view (BlockEditor's onZoomChanged, `scroller`).
        if (test.at())
            test.list.positionViewAtIndex(test.linkRow, ListView.Beginning)
        if (test.at())
            test.waitFor(test.dayLoaded(test.linkRow))
        if (test.at()) {
            test.tracked = test.dayAt(test.linkRow)
            const c = test.tracked.controller
            test.dayText = c.sourceText
            test.zoomRow = c.insertMarkdown(c.lastVisibleRow(), "Akapit na końcu dnia ąę")
            block.read(c.blocks.get(test.zoomRow))
            test.check(test.zoomRow > 40 && block.view === "text" && block.plainText === "Akapit na końcu dnia ąę",
                       "a paragraph at the long day's bottom: " + test.zoomRow)
            c.zoomInto(test.zoomRow)
            test.tracked.editor.startEditing(test.zoomRow, -1)
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked) && test.tracked.controller.zoomBlock === test.zoomRow)
        if (test.at()) {
            const ed = test.tracked.editor
            ed.handleEditorKeyCode(Qt.Key_Return, Qt.NoModifier, ed.activeEditor)
        }
        test.idle(3)
        if (test.at()) {
            const c = test.tracked.controller
            const item = test.tracked.editor.listView.itemAtIndex(c.editingRow) as Item
            const top = item !== null ? test.topInList(item) : -1e9
            test.check(c.zoomBlock < 0 && c.editingRow === test.zoomRow + 1 && test.editing(test.tracked),
                       "Enter at the end of a zoomed paragraph in a day ends the zoom and edits the new block: zoom "
                       + c.zoomBlock + ", row " + c.editingRow)
            test.check(item !== null && top >= 0 && top + item.height <= test.list.height,
                       "the stream keeps the new block at the grown day's bottom in view: " + top + " of " + test.list.height)
            const ed = test.tracked.editor
            ed.handleEditorKeyCode(Qt.Key_Z, Qt.ControlModifier, ed.activeEditor)
        }
        test.idle(3)
        if (test.at()) {
            const c = test.tracked.controller
            const item = test.tracked.editor.listView.itemAtIndex(test.zoomRow) as Item
            const top = item !== null ? test.topInList(item) : -1e9
            test.check(c.zoomBlock === test.zoomRow && c.editingRow === test.zoomRow && test.editing(test.tracked)
                       && item !== null && top >= 0 && top + item.height <= test.list.height,
                       "Ctrl+Z zooms back into the paragraph, in the stream's view: " + top + " (zoom " + c.zoomBlock + ")")
            c.undo() // the paragraph added
            c.zoomTo(-1)
            c.stopEditing()
            test.check(c.sourceText === test.dayText, "undo leaves the long day as it was")
        }
        if (test.at())
            test.list.positionViewAtBeginning()
        // ---- Midnight while editing --------------------------------------------------------------
        if (test.at())
            test.waitFor(test.dayLoaded(0))
        if (test.at()) {
            test.tracked = test.dayAt(0)
            test.trackedPath = test.tracked.path
            test.trackedDate = test.tracked.date
            test.editNewBlock(test.tracked)
        }
        if (test.at())
            test.waitFor(test.editing(test.tracked))
        if (test.at())
            test.typeKeys(test.tracked.editor.activeEditor, "Przed północą")
        // The list's clock moves on (the app's DatesHelper.today does that at 00:00).
        if (test.at())
            test.days.today = DatesHelper.addDays(test.trackedDate, 1)
        if (test.at())
            test.waitFor(test.days.count > 0 && test.firstDate() === DatesHelper.addDays(test.trackedDate, 1))
        if (test.at()) {
            test.check(test.tracked !== null, "the edited day keeps its editor over midnight")
            if (test.tracked !== null)
                test.afterMidnight()
        }
        if (test.at()) {
            if (test.editing(test.tracked))
                test.typeKeys(test.tracked.editor.activeEditor, " i po północy")
        }
        if (test.at()) {
            if (test.tracked !== null)
                test.tracked.controller.save()
        }
        if (test.at()) {
            test.check(test.fileHas(test.trackedPath, "Przed północą i po północy"),
                       "text typed over midnight is in the day's own file")
            test.check(!Workspace.exists(Workspace.journalPath(test.days.today)), "nothing was written to the new day")
            let todays = 0
            for (let i = 0; i < 3; ++i) {
                const d = test.dayAt(i)
                if (d !== null && d.isToday)
                    todays += 1
            }
            test.check(todays === 1, "one day says today: " + todays)
        }
        test.nextMidnightSteps()
    }
    /// The edited day over midnight (a step).
    function afterMidnight(): void {
        test.check(test.tracked.date === test.trackedDate && !test.tracked.isToday,
                   "the edited day is no longer today: " + test.tracked.date + " " + test.tracked.isToday)
        const edit = test.tracked.editor.activeEditor
        const focus = test.Window.activeFocusItem
        test.check(test.editing(test.tracked), "the caret stays in the edited day (editingRow "
                   + test.tracked.controller.editingRow + ", editor " + (edit !== null ? "" + edit.activeFocus : "none")
                   + ", focus on " + (focus !== null ? focus.objectName : "null") + ")")
        const first = test.dayAt(0)
        test.check(first !== null && first.isToday && first.date === test.days.today, "the new day comes first")
    }
    function nextMidnightSteps(): void {
        // ---- The next midnight: the unwritten day goes, the edited one keeps the caret ---------
        if (test.at()) {
            test.check(test.editing(test.tracked), "still editing the day before")
            test.days.today = DatesHelper.addDays(test.trackedDate, 2)
        }
        if (test.at())
            test.waitFor(test.days.count > 0 && test.firstDate() === DatesHelper.addDays(test.trackedDate, 2))
        if (test.at()) {
            test.check(test.days.exactRowOfDate(DatesHelper.addDays(test.trackedDate, 1)) < 0,
                       "the day nobody wrote in is no longer listed")
            test.check(test.tracked !== null && test.editing(test.tracked), "rows removed above keep the caret in the edited day")
        }
        if (test.at()) {
            if (test.editing(test.tracked))
                test.typeKeys(test.tracked.editor.activeEditor, " — dalej")
        }
        if (test.at()) {
            if (test.tracked !== null)
                test.tracked.controller.save()
        }
        if (test.at())
            test.check(test.fileHas(test.trackedPath, "po północy — dalej"), "typing goes on in the edited day")
    }

    /// A step is running. QtTest's events process events before they return (a touch sequence's
    /// commit(), a mouse click), so a step that outlasts the interval lets the timer fire from
    /// inside it: that tick is skipped — it would run the same step again and then skip the next.
    property bool inStep: false
    Timer {
        id: runner
        interval: 120
        repeat: true
        running: test.list !== null && test.days !== null
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
