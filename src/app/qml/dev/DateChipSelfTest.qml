pragma ComponentBehavior: Bound
import QtQuick
import Brulion

// DateChipSelfTest — the menu of date chips with real input, in the editor playground (`brulion
// --editor --editor-script datechips`; ctest tst_editor_qml_datechips, desktop, and
// tst_editor_qml_datechips_touch, a phone with --touch): a right-click (mouse) or a finger held
// on a 📅 chip and on a ⏰ chip opens DateChipMenu — never the block menu, no block selected —;
// its items tapped or clicked move the date (a day later, today), open the date dialog on the
// chip's own date, remove the token; on a reminder an hour later and the alarm switch, "Zmień
// datę i godzinę…" the reminder dialog; each change is one undo step and the rest of the line
// stays as written; a plain click or tap on the chip still edits the text.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property var window
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property var events: null
    property int row: -1
    property point at: Qt.point(0, 0)
    readonly property string gesture: Theme.touchUi ? "a long press" : "a right-click"

    function check(ok, what) { window.check(ok, what) }
    function wait(ready, what) { return window.waitStep(ready, what) }
    function line(prefix) { return window.fileLineOf(window.pageText(), prefix) }
    function text() { return window.renderedIn(window.delegateAt(row)) }
    function menu() { return editor.dateMenu }
    function menuOpen() { return editor.dateMenu !== null && editor.dateMenu.opened }
    function blockMenuOpen() { return editor.blockMenu !== null && editor.blockMenu.opened }
    function item(name) { return window.menuItemNamed(editor.dateMenu, name) }

    /// The block starting with `prefix` in the middle of the view.
    function show(prefix) {
        return () => {
            row = window.rowOf(prefix)
            check(row >= 0, "the block “" + prefix + "” is there")
            editor.listView.positionViewAtIndex(row, ListView.Center)
        }
    }
    /// The context gesture on the chip whose link starts with `scheme`: a right-click, or a finger
    /// held down until the menu shows, then lifted.
    function chipGesture(scheme) {
        const find = () => {
            const t = text()
            const p = t ? window.linkPointIn(t, scheme) : null
            check(p !== null, "the " + scheme + " chip is drawn")
            if (p)
                at = p
            return p ? t : null
        }
        if (!Theme.touchUi) {
            return [() => {
                const t = find()
                if (t)
                    events.mouseClick(t, at.x, at.y, Qt.RightButton, Qt.NoModifier, -1)
            }]
        }
        return [
            () => {
                const t = find()
                if (t)
                    events.touchEvent(t).press(0, t, at.x, at.y).commit()
            },
            window.holdStep(menuOpen),
            () => {
                const t = text()
                if (t)
                    events.touchEvent(t).release(0, t, at.x, at.y).commit()
            }
        ]
    }
    /// Choose the menu's item `name` (tapped / clicked) and wait until the menu is gone.
    function choose(name) {
        return [
            () => {
                check(item(name) !== null && item(name).visible, "the menu has " + name)
                window.tapItem(item(name))
            },
            wait(() => !menuOpen(), "choosing " + name + " closes the menu")
        ]
    }

    function steps() {
        events = Qt.createQmlObject('import QtQuick; import QtTest; TestEvent {}', test, "dateChipEvents")
        window.taskEvents = events // (window.tapItem)
        const due = "Kupić płytki 📅 2026-10-16 ⏫ #zakupy"
        const reminder = "Zadzwonić do hydraulika ⏰ 2026-10-15 18:00 #telefon"
        return [
            show("Kupić płytki"),
            () => check(line("Kupić płytki").indexOf(due) > 0, "the sample task: " + line("Kupić płytki")),
            // ---- 📅: a day later, undo -------------------------------------------------------------
            ...chipGesture("date:"),
            wait(menuOpen, gesture + " on a 📅 chip opens its menu"),
            () => {
                check(!blockMenuOpen(), "no block menu opens with it")
                check(page.selectionCount === 0 && !editor.selectionMode, "no block is selected")
                check(page.blocks.get(row).taskStatus === "todo", "the task is not toggled")
                check(menu().kind === "due", "the menu is the due date's")
                check(item("dateChipToday").visible && !item("dateChipHourLater").visible && !item("dateChipAlarm").visible,
                      "a date's items, no reminder's")
            },
            ...choose("dateChipDayLater"),
            () => {
                check(line("Kupić płytki").indexOf("Kupić płytki 📅 2026-10-17 ⏫ #zakupy") > 0,
                      "“A day later” moves the due date: " + line("Kupić płytki"))
                page.undo()
            },
            () => check(line("Kupić płytki").indexOf(due) > 0, "one undo step puts it back"),
            // ---- 📅: today ---------------------------------------------------------------------------
            ...chipGesture("date:"),
            wait(menuOpen, "the menu opens again"),
            ...choose("dateChipToday"),
            () => {
                check(line("Kupić płytki").indexOf("Kupić płytki 📅 2026-10-15 ⏫") > 0, "“Today”: " + line("Kupić płytki"))
                page.undo()
            },
            // ---- 📅: the date dialog on the chip's date ---------------------------------------------
            ...chipGesture("date:"),
            wait(menuOpen, "the menu opens for the dialog"),
            ...choose("dateChipChange"),
            wait(() => editor.dialogs.dateDialog !== null && editor.dialogs.dateDialog.opened,
                 "“Change date…” opens the date dialog"),
            () => {
                const dialog = editor.dialogs.dateDialog
                check(dialog.picker.selectedDate === "2026-10-16" && dialog.removable,
                      "the dialog shows the chip's date: " + dialog.picker.selectedDate)
                dialog.picker.selectedDate = "2026-10-20"
                dialog.picker.time = "08:15"
                dialog.accept()
            },
            wait(() => !editor.dialogs.dateDialog.opened, "the dialog closes"),
            () => {
                check(line("Kupić płytki").indexOf("Kupić płytki 📅 2026-10-20 08:15 ⏫ #zakupy") > 0,
                      "the chosen date and time are written: " + line("Kupić płytki"))
                page.undo()
            },
            // ---- 📅: removed ---------------------------------------------------------------------------
            ...chipGesture("date:"),
            wait(menuOpen, "the menu opens to remove the date"),
            ...choose("dateChipRemove"),
            () => {
                check(line("Kupić płytki").indexOf("Kupić płytki ⏫ #zakupy") > 0, "“Remove date”: " + line("Kupić płytki"))
                page.undo()
            },
            () => check(line("Kupić płytki").indexOf(due) > 0, "undo brings the date back"),
            // ---- A plain click / tap on the chip edits the text --------------------------------------
            () => {
                const t = text()
                const p = t ? window.linkPointIn(t, "date:") : null
                if (Theme.touchUi) {
                    events.touchEvent(t).press(0, t, p.x, p.y).commit()
                    events.touchEvent(t).release(0, t, p.x, p.y).commit()
                } else {
                    events.mouseClick(t, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
                }
            },
            wait(() => page.editingRow === row, (Theme.touchUi ? "a tap" : "a click") + " on the chip edits the block"),
            () => {
                check(!menuOpen(), "without a menu")
                page.stopEditing()
            },
            // ---- ⏰: an hour later, the alarm, undo ----------------------------------------------------
            show("Zadzwonić do hydraulika"),
            () => check(line("Zadzwonić do hydraulika").indexOf(reminder) > 0, "the sample reminder"),
            ...chipGesture("reminder:"),
            wait(menuOpen, gesture + " on a ⏰ chip opens its menu"),
            () => {
                check(!blockMenuOpen() && menu().kind === "reminder" && !menu().alarm, "the menu is the reminder's")
                check(item("dateChipHourLater").visible && item("dateChipAlarm").visible && !item("dateChipAlarm").checked
                      && !item("dateChipToday").visible, "a reminder's items, the alarm off")
            },
            ...choose("dateChipHourLater"),
            () => check(line("Zadzwonić do hydraulika").indexOf("Zadzwonić do hydraulika ⏰ 2026-10-15 19:00 #telefon") > 0,
                        "“An hour later”: " + line("Zadzwonić do hydraulika")),
            ...chipGesture("reminder:"),
            wait(menuOpen, "the menu opens for the alarm"),
            ...choose("dateChipAlarm"),
            () => {
                check(line("Zadzwonić do hydraulika").indexOf("Zadzwonić do hydraulika ⏰! 2026-10-15 19:00 #telefon") > 0,
                      "the alarm switched on: " + line("Zadzwonić do hydraulika"))
                page.undo()
                page.undo()
            },
            () => check(line("Zadzwonić do hydraulika").indexOf(reminder) > 0, "two undo steps put the reminder back"),
            // ---- ⏰: "Change date and time…" opens the reminder dialog ------------------------------------
            ...chipGesture("reminder:"),
            wait(menuOpen, "the menu opens for the reminder dialog"),
            ...choose("dateChipChange"),
            wait(() => editor.dialogs.reminderDialog !== null && editor.dialogs.reminderDialog.opened,
                 "“Change date and time…” opens the reminder dialog"),
            () => editor.dialogs.reminderDialog.reject(),
            wait(() => !editor.dialogs.reminderDialog.opened, "the dialog closes"),
            () => check(line("Zadzwonić do hydraulika").indexOf(reminder) > 0, "a cancelled dialog changes nothing")
        ]
    }

    Component.onCompleted: window.runSteps(steps())
}
