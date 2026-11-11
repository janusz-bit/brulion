pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// DateChipSelfTest — the menu of date chips with real input, in the editor playground (`brulion
// --editor --editor-script datechips`; ctest tst_editor_qml_datechips, desktop, and
// tst_editor_qml_datechips_touch, a phone with --touch): a right-click (mouse) or a finger held
// on a 📅 chip and on a ⏰ chip opens DateChipMenu — never the block menu, no block selected —;
// its items tapped or clicked move the date (a day later, today), open the date dialog on the
// chip's own date, remove the token; on a reminder an hour later and the alarm switch, "Change
// date and time…" the reminder dialog; each change is one undo step and the rest of the line
// stays as written; a plain click or tap on the chip still edits the text. The same gesture on the
// ⏫ priority, the 🔁 rule (its quick rules), the reminder's "Priority" submenu, a [[day]] link (a
// day later) opens their menus (removing a date or a reminder red, the 🔁's and the priority's
// none value a neutral choice), and on a #tag and a [[link]] the link menu (LinkMenu: "Copy"
// copies the tag, the link) — never the block menu. While the block is edited its ⏰ stays a chip:
// a click or tap on it opens the reminder dialog on that reminder, the caret stays, the time chosen
// is written (one undo step). An open task without a date has the "Add date" button at the end of
// its first line — on hover (desktop) and while it is edited, never on a dated or a done task —:
// its menu (QuickDateMenu) writes Today / Tomorrow / Next week as the 📅 due date at once and
// "Pick a date…" opens the date dialog on tomorrow; the block stays edited, one undo step each.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    property int row: -1
    property point at: Qt.point(0, 0)
    readonly property string gesture: Theme.touchUi ? "a long press" : "a right-click"
    readonly property string due: "Kupić płytki 📅 2026-10-16 ⏫ #zakupy"
    readonly property string reminder: "Zadzwonić do hydraulika ⏰ 2026-10-15 18:00 #telefon"

    function check(ok: bool, what: string): void { window.check(ok, what) }
    function info(r: int): DevBlock { return window.info(r) }
    function line(prefix: string): string { return window.fileLineOf(window.pageText(), prefix) }
    function text(): Item { return window.renderedIn(window.delegateAt(row)) }
    function menu(): DateChipMenu { return editor.dateMenu }
    function menuOpen(): bool { return menu() !== null && menu().opened }
    function blockMenu(): BlockMenu { return editor.blockMenu }
    function blockMenuOpen(): bool { return blockMenu() !== null && blockMenu().opened }
    function list(): ListView { return editor.listView }
    function dialogs(): EditorDialogs { return editor.dialogs as EditorDialogs }
    function item(name: string): T.MenuItem { return window.menuItemNamed(menu(), name) }
    /// The menu's "remove" item looks as it should: with `danger` red with the trash (a date, a
    /// reminder), else a choice like the ones above it (🔁's "Don't repeat", a priority's "No
    /// priority": no red, no trash, checkable, unchecked).
    function removeItemLooks(danger: bool): bool {
        const remove = item("dateChipRemove") as BMenuItem
        return remove !== null && remove.visible && remove.danger === danger && (remove.iconName === "trash-2") === danger
               && remove.checkable === !danger && !remove.checked
    }
    function linkMenu(): LinkMenu { return editor.linkMenu }
    function linkMenuOpen(): bool { return linkMenu() !== null && linkMenu().opened }
    function linkItem(name: string): T.MenuItem { return window.menuItemNamed(linkMenu(), name) }
    function dateDialog(): BDialog { return dialogs().dateDialog }
    function datePicker(): BDatePicker { return DevTest.objectProperty(dateDialog(), "picker") as BDatePicker }
    function reminderDialog(): BReminderDialog { return dialogs().reminderDialog }
    /// The edited block's reminder chip (InlineEditor), null when there is none.
    function chipArea(): Item { return DevTest.findShown(window.delegateAt(row), "reminderChipArea") }
    function delegateAt(r: int): BlockDelegate { return window.delegateAt(r) }
    /// The "Add date" button of block `r` (BlockDelegate), null when it is not shown.
    function dateButton(r: int): Item { return DevTest.findShown(delegateAt(r), "taskDateButton") }
    function quickMenu(): QuickDateMenu { return editor.quickDateMenu }
    function quickMenuOpen(): bool { return quickMenu() !== null && quickMenu().opened }
    function quickItem(name: string): BMenuItem { return window.menuItemNamed(quickMenu(), name) as BMenuItem }
    /// The date the quick menu's item `name` shows ("" when there is no such item).
    function quickHint(name: string): string {
        const it = quickItem(name)
        return it !== null ? it.hint : ""
    }
    function datedAt(r: int): bool { return info(r).taskDated }
    /// The mouse moved to the middle of block `r` (its hover).
    function hoverBlock(r: int): void {
        const d = delegateAt(r)
        const x = d.width / 2
        const y = d.height / 2
        window.taskEvents.mouseMove(d, x, y, -1, Qt.NoButton, Qt.NoModifier)
    }
    /// The item of the date menu that opens its submenu `name`.
    function subMenuItem(name: string): T.MenuItem {
        const m = menu()
        for (let i = 0; m && i < m.count; ++i) {
            const it = m.itemAt(i) as T.MenuItem
            if (it && it.subMenu && it.subMenu.objectName === name)
                return it
        }
        return null
    }
    function subMenuOf(name: string): T.Menu {
        const it = subMenuItem(name)
        return it ? it.subMenu : null
    }

    /// A step: the block starting with `prefix` in the middle of the view.
    function show(prefix: string): void {
        row = window.rowOf(prefix)
        check(row >= 0, "the block “" + prefix + "” is there")
        list().positionViewAtIndex(row, ListView.Center)
    }
    /// The rendered text with the chip whose link starts with `scheme` (`at`: a point on it), or null.
    function find(scheme: string): Item {
        const t = text()
        const found = t !== null && window.linkPointIn(t, scheme)
        check(found, "the " + scheme + " chip is drawn")
        if (found)
            at = Qt.point(window.linkX, window.linkY)
        return found ? t : null
    }
    /// Steps: the context gesture on the chip whose link starts with `scheme`: a right-click, or a
    /// finger held down until the menu (the link menu if `link`) shows, then lifted.
    function chipGesture(scheme: string, link: bool): void {
        if (!window.touchRun) {
            if (window.at()) {
                const t = find(scheme)
                if (t)
                    window.taskEvents.mouseClick(t, at.x, at.y, Qt.RightButton, Qt.NoModifier, -1)
            }
            return
        }
        if (window.at()) {
            const t = find(scheme)
            if (t)
                window.taskEvents.touchPress(t, at.x, at.y)
        }
        if (window.at())
            window.waiting(link ? linkMenuOpen() : menuOpen())
        if (window.at()) {
            const t = text()
            if (t)
                window.taskEvents.touchRelease(t, at.x, at.y)
        }
    }
    /// Steps: choose the menu's item `name` (tapped / clicked) and wait until the menu is gone.
    function choose(name: string): void {
        if (window.at()) {
            check(item(name) !== null && item(name).visible, "the menu has " + name)
            window.tapItem(item(name))
        }
        if (window.at())
            window.waitStep(!menuOpen(), "choosing " + name + " closes the menu")
    }
    /// A step: a plain click (a tap) on the date chip.
    function clickChip(): void {
        const t = text()
        if (!t || !window.linkPointIn(t, "date:"))
            return
        if (Theme.touchUi) {
            window.taskEvents.touchPress(t, window.linkX, window.linkY)
            window.taskEvents.touchRelease(t, window.linkX, window.linkY)
        } else {
            window.taskEvents.mouseClick(t, window.linkX, window.linkY, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }

    function steps(): void {
        if (window.at()) show("Kupić płytki")
        if (window.at()) check(Js.indexOf(line("Kupić płytki"), due) > 0, "the sample task: " + line("Kupić płytki"))
        // ---- 📅: a day later, undo -------------------------------------------------------------
        chipGesture("date:", false)
        if (window.at()) window.waitStep(menuOpen(), gesture + " on a 📅 chip opens its menu")
        if (window.at()) {
            check(!blockMenuOpen(), "no block menu opens with it")
            check(page.selectionCount === 0 && !editor.selectionMode, "no block is selected")
            check(info(row).taskStatus === "todo", "the task is not toggled")
            check(menu().kind === "due", "the menu is the due date's")
            check(item("dateChipToday").visible && !item("dateChipHourLater").visible && !item("dateChipAlarm").visible,
                  "a date's items, no reminder's")
        }
        choose("dateChipDayLater")
        if (window.at()) {
            check(Js.indexOf(line("Kupić płytki"), "Kupić płytki 📅 2026-10-17 ⏫ #zakupy") > 0,
                  "“A day later” moves the due date: " + line("Kupić płytki"))
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Kupić płytki"), due) > 0, "one undo step puts it back")
        // ---- 📅: today ---------------------------------------------------------------------------
        chipGesture("date:", false)
        if (window.at()) window.waitStep(menuOpen(), "the menu opens again")
        choose("dateChipToday")
        if (window.at()) {
            check(Js.indexOf(line("Kupić płytki"), "Kupić płytki 📅 2026-10-15 ⏫") > 0, "“Today”: " + line("Kupić płytki"))
            page.undo()
        }
        // ---- 📅: the date dialog on the chip's date ---------------------------------------------
        chipGesture("date:", false)
        if (window.at()) window.waitStep(menuOpen(), "the menu opens for the dialog")
        choose("dateChipChange")
        if (window.at()) window.waitStep(dateDialog() !== null && dateDialog().opened, "“Change date…” opens the date dialog")
        if (window.at()) {
            const dialog = dateDialog()
            check(datePicker().selectedDate === "2026-10-16" && DevTest.property(dialog, "removable") === true,
                  "the dialog shows the chip's date: " + datePicker().selectedDate)
            datePicker().selectedDate = "2026-10-20"
            datePicker().time = "08:15"
            dialog.accept()
        }
        if (window.at()) window.waitStep(!dateDialog().opened, "the dialog closes")
        if (window.at()) {
            check(Js.indexOf(line("Kupić płytki"), "Kupić płytki 📅 2026-10-20 08:15 ⏫ #zakupy") > 0,
                  "the chosen date and time are written: " + line("Kupić płytki"))
            page.undo()
        }
        // ---- 📅: removed ---------------------------------------------------------------------------
        chipGesture("date:", false)
        if (window.at()) window.waitStep(menuOpen(), "the menu opens to remove the date")
        if (window.at()) check(removeItemLooks(true), "“Remove date” is red, with the trash")
        choose("dateChipRemove")
        if (window.at()) {
            check(Js.indexOf(line("Kupić płytki"), "Kupić płytki ⏫ #zakupy") > 0, "“Remove date”: " + line("Kupić płytki"))
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Kupić płytki"), due) > 0, "undo brings the date back")
        // ---- A plain click / tap on the chip edits the text --------------------------------------
        if (window.at()) clickChip()
        if (window.at()) window.waitStep(page.editingRow === row, (Theme.touchUi ? "a tap" : "a click") + " on the chip edits the block")
        if (window.at()) {
            check(!menuOpen(), "without a menu")
            page.stopEditing()
        }
        // ---- ⏰: an hour later, the alarm, undo ----------------------------------------------------
        if (window.at()) show("Zadzwonić do hydraulika")
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), reminder) > 0, "the sample reminder")
        chipGesture("reminder:", false)
        if (window.at()) window.waitStep(menuOpen(), gesture + " on a ⏰ chip opens its menu")
        if (window.at()) {
            check(!blockMenuOpen() && menu().kind === "reminder" && !menu().alarm, "the menu is the reminder's")
            check(item("dateChipHourLater").visible && item("dateChipAlarm").visible && !item("dateChipAlarm").checked
                  && !item("dateChipToday").visible, "a reminder's items, the alarm off")
            check(removeItemLooks(true), "“Delete reminder” is red, with the trash")
        }
        choose("dateChipHourLater")
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), "Zadzwonić do hydraulika ⏰ 2026-10-15 19:00 #telefon") > 0,
                    "“An hour later”: " + line("Zadzwonić do hydraulika"))
        chipGesture("reminder:", false)
        if (window.at()) window.waitStep(menuOpen(), "the menu opens for the alarm")
        choose("dateChipAlarm")
        if (window.at()) {
            check(Js.indexOf(line("Zadzwonić do hydraulika"), "Zadzwonić do hydraulika ⏰! 2026-10-15 19:00 #telefon") > 0,
                  "the alarm switched on: " + line("Zadzwonić do hydraulika"))
            page.undo()
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), reminder) > 0, "two undo steps put the reminder back")
        // ---- ⏰: "Change date and time…" opens the reminder dialog ------------------------------------
        chipGesture("reminder:", false)
        if (window.at()) window.waitStep(menuOpen(), "the menu opens for the reminder dialog")
        choose("dateChipChange")
        if (window.at()) window.waitStep(reminderDialog() !== null && reminderDialog().opened, "“Change date and time…” opens the reminder dialog")
        if (window.at()) reminderDialog().reject()
        if (window.at()) window.waitStep(!reminderDialog().opened, "the dialog closes")
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), reminder) > 0, "a cancelled dialog changes nothing")
        // ---- ⏰ in the edited text: still a chip, a click / tap opens the reminder dialog ---------------
        if (window.at()) editor.startEditing(row, 3)
        if (window.at()) window.waitStep(page.editingRow === row && chipArea() !== null, "the edited block's ⏰ is still a chip")
        if (window.at()) {
            // (One per line: on a phone the token wraps.)
            const areas = DevTest.countNamed(window.delegateAt(row), "reminderChipArea")
            check(areas === (Theme.touchUi ? 2 : 1), "one chip for its one reminder, a part per line: " + areas)
            const area = chipArea()
            window.tapAt(area, area.width / 2, area.height / 2)
        }
        if (window.at()) window.waitStep(reminderDialog() !== null && reminderDialog().opened,
                                         (Theme.touchUi ? "a tap" : "a click") + " on it opens the reminder dialog")
        if (window.at()) {
            check(reminderDialog().at === "2026-10-15 18:00", "on the reminder's date and time: " + reminderDialog().at)
            check(page.cursorPosition === 3, "the caret stays where it was: " + page.cursorPosition)
            reminderDialog().at = "2026-10-15 20:30"
            reminderDialog().accept()
        }
        if (window.at()) window.waitStep(!reminderDialog().opened, "the dialog closes")
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), "Zadzwonić do hydraulika ⏰ 2026-10-15 20:30 #telefon") > 0,
                               "the time chosen is written: " + line("Zadzwonić do hydraulika"))
        if (window.at()) window.waitStep(page.editingRow === row && chipArea() !== null, "the block is edited again, its reminder a chip")
        if (window.at()) {
            page.stopEditing()
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Zadzwonić do hydraulika"), reminder) > 0, "one undo step puts the time back")
        // ---- ⏫: the priority's menu ----------------------------------------------------------------
        if (window.at()) show("Kupić płytki")
        chipGesture("prio:", false)
        if (window.at()) window.waitStep(menuOpen(), gesture + " on a priority opens its menu")
        if (window.at()) {
            check(!blockMenuOpen() && menu().kind === "priority" && menu().priority === "high", "the menu is the priority's")
            check(item("dateChipPriorityHigh").checked && item("dateChipPriorityLowest").visible
                  && !item("dateChipToday").visible, "the current priority checked, five levels, no dates")
            check(removeItemLooks(false), "“No priority” is a choice like the levels: no red, no trash")
        }
        choose("dateChipPriorityLow")
        if (window.at()) {
            check(Js.indexOf(line("Kupić płytki"), "Kupić płytki 📅 2026-10-16 🔽 #zakupy") > 0, "“Low”: " + line("Kupić płytki"))
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Kupić płytki"), due) > 0, "one undo step puts the priority back")
        // ---- #tag and [[link]]: the link menu -------------------------------------------------------
        chipGesture("tag:", true)
        if (window.at()) window.waitStep(linkMenuOpen(), gesture + " on a #tag opens the link menu")
        if (window.at()) {
            check(!blockMenuOpen() && !menuOpen() && linkMenu().kind === "tag", "the tag's menu, no other")
            check(linkItem("linkMenuOverview").visible && linkItem("linkMenuTasks").visible, "with the tag view and Tasks")
            check(page.selectionCount === 0 && !editor.selectionMode, "no block is selected")
        }
        if (window.at()) window.tapItem(linkItem("linkMenuCopy"))
        if (window.at() && !window.waiting(!linkMenuOpen() && page.clipboardText() === "#zakupy"))
            window.checkWait("“Kopiuj tag” copies #zakupy: " + page.clipboardText())
        if (window.at()) show("Sklep:")
        chipGesture("page:Castorama", true)
        if (window.at()) window.waitStep(linkMenuOpen(), gesture + " on a [[link]] opens the link menu")
        if (window.at()) check(!blockMenuOpen() && linkMenu().kind === "page" && linkMenu().unresolved,
                    "the page link's menu (a page not written yet: “Utwórz stronę”)")
        if (window.at()) window.tapItem(linkItem("linkMenuCopy"))
        if (window.at() && !window.waiting(!linkMenuOpen() && page.clipboardText() === "[[Castorama]]"))
            window.checkWait("“Kopiuj link” copies the link: " + page.clipboardText())
        // ---- 🔁: a quick rule; the reminder's Priority submenu ---------------------------------------
        if (window.at()) show("Podlać kwiaty")
        chipGesture("recur:", false)
        if (window.at()) window.waitStep(menuOpen(), gesture + " on a 🔁 rule opens its menu")
        if (window.at()) {
            check(!blockMenuOpen() && menu().kind === "recurrence" && menu().rule === "every week on Friday",
                  "the menu is the rule's: " + menu().rule)
            check(item("dateChipRuleDaily").visible && !item("dateChipRuleDaily").checked && !item("dateChipToday").visible,
                  "its quick rules, no dates")
            check(removeItemLooks(false), "“Don't repeat” is a choice like the rules: no red, no trash")
        }
        choose("dateChipRuleDaily")
        if (window.at()) {
            check(Js.indexOf(line("Podlać kwiaty"), "Podlać kwiaty u sąsiadów ⏰! 2026-10-16 07:30 🔁 every day") > 0,
                  "“Daily” rewrites the rule: " + line("Podlać kwiaty"))
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Podlać kwiaty"), "🔁 every week on Friday") > 0, "undo puts the rule back")
        chipGesture("reminder:", false)
        if (window.at()) window.waitStep(menuOpen(), "the alarm's menu opens")
        if (window.at()) {
            check(menu().kind === "reminder" && menu().alarm && menu().rule === "every week on Friday" && menu().isTask,
                  "an alarm of a recurring task")
            check(subMenuItem("dateChipRepeatMenu") !== null && subMenuItem("dateChipRepeatMenu").visible
                  && subMenuItem("dateChipPriorityMenu") !== null && subMenuItem("dateChipPriorityMenu").visible,
                  "with the Repeat and Priority submenus")
            window.tapItem(subMenuItem("dateChipPriorityMenu"))
        }
        if (window.at()) window.waitStep(subMenuOf("dateChipPriorityMenu") !== null && subMenuOf("dateChipPriorityMenu").opened, "the Priority submenu opens")
        if (window.at()) window.tapItem(window.menuItemNamed(subMenuOf("dateChipPriorityMenu"), "dateChipTaskPriorityHigh"))
        if (window.at()) window.waitStep(!menuOpen(), "choosing a priority closes the menu")
        if (window.at()) {
            check(Js.indexOf(line("Podlać kwiaty"), "Podlać kwiaty u sąsiadów ⏰! 2026-10-16 07:30 🔁 every week on Friday ⏫") > 0,
                  "the task gets the priority: " + line("Podlać kwiaty"))
            page.undo()
        }
        // ---- [[2026-10-16]]: a day later ----------------------------------------------------------------
        if (window.at()) show("Klucz w skrzynce")
        chipGesture("day:", false)
        if (window.at()) window.waitStep(menuOpen(), gesture + " on a [[day]] link opens the date menu")
        if (window.at()) {
            check(!blockMenuOpen() && menu().kind === "day", "the menu is the day's")
            check(item("dateChipOpenDay").visible && item("dateChipCopyLink").visible && item("dateChipDayLater").visible
                  && !item("dateChipRemove").visible, "open, copy and move, nothing to remove")
        }
        choose("dateChipDayLater")
        if (window.at()) {
            check(Js.indexOf(line("Klucz w skrzynce"), "Klucz w skrzynce od [[2026-10-17]], kod `4711`") > 0,
                  "“A day later” moves the link: " + line("Klucz w skrzynce"))
            page.undo()
        }
        if (window.at()) check(Js.indexOf(line("Klucz w skrzynce"), "[[2026-10-16]]") > 0, "one undo step puts it back")

        addDateHoverSteps()
        addDateQuickSteps()
        addDatePickSteps()
        addDateNoneSteps()
    }

    /// taskDated and no button on the undated task before it is hovered; the mouse moves onto it.
    function checkDatedRows(): void {
        const r = row
        check(dateButton(r) === null, "an undated task shows no “Add date” button until hovered or edited")
        check(!datedAt(r), "the task is not dated (taskDated)")
        // The mouse over the block shows it (desktop); over a dated or done task it shows none.
        if (!Theme.touchUi)
            hoverBlock(r)
    }
    function addDateHoverSteps(): void {
        // ---- "Add date" on a task without a date ------------------------------------------------------
        if (window.at()) show("Położyć hydroizolację")
        if (window.at()) checkDatedRows()
        if (window.at() && !Theme.touchUi) window.waitStep(dateButton(row) !== null, "hovering the task shows the button")
        if (window.at() && !Theme.touchUi) hoverBlock(window.rowOf("Kupić płytki"))
        if (window.at() && !Theme.touchUi) window.waitStep(dateButton(row) === null, "it goes when the mouse leaves")
        if (window.at() && !Theme.touchUi) check(dateButton(window.rowOf("Kupić płytki")) === null, "a dated task has none")
    }
    function addDateQuickSteps(): void {
        if (window.at()) editor.startEditing(row, 3)
        if (window.at()) window.waitStep(page.editingRow === row && dateButton(row) !== null, "the edited task shows the button")
        if (window.at()) {
            const b = dateButton(row)
            const d = delegateAt(row)
            const centre = b.parent.x + b.x + b.width / 2 // (the button's Loader is the delegate's child)
            check(d.dateButtonRoom > 0 && centre > d.contentRight - d.dateButtonRoom && centre < d.contentRight,
                  "it sits at the end of the line, in the room the text leaves for it")
            window.tapAt(b, b.width / 2, b.height / 2)
        }
        if (window.at()) window.waitStep(quickMenuOpen(), (Theme.touchUi ? "a tap" : "a click") + " on it opens the quick date menu")
        if (window.at()) {
            const todayHint = quickHint("quickDateToday")
            const tomorrowHint = quickHint("quickDateTomorrow")
            const weekHint = quickHint("quickDateNextWeek")
            check(todayHint === DatesHelper.formatShort("2026-10-15") && tomorrowHint === DatesHelper.formatShort("2026-10-16")
                  && weekHint === DatesHelper.formatShort("2026-10-19"),
                  "Today, Tomorrow and Next week with their dates: " + weekHint)
            window.tapItem(quickItem("quickDateTomorrow"))
        }
        if (window.at()) window.waitStep(!quickMenuOpen(), "choosing Tomorrow closes the menu")
        if (window.at()) window.waitStep(Js.indexOf(line("Położyć hydroizolację"), "Położyć hydroizolację 📅 2026-10-16") > 0,
                                         "“Tomorrow” writes the due date: " + line("Położyć hydroizolację"))
        if (window.at()) window.waitStep(info(row).taskDated && page.editingRow === row && dateButton(row) === null,
                                         "the block stays edited, the dated task has no button any more")
        if (window.at()) {
            page.stopEditing()
            page.undo()
        }
        if (window.at()) check(line("Położyć hydroizolację") === "\t- [ ] Położyć hydroizolację",
                               "one undo step takes the date away: " + line("Położyć hydroizolację"))
    }
    function addDatePickSteps(): void {
        // "Pick a date…": the date dialog on tomorrow, nothing to remove.
        if (window.at()) editor.startEditing(row, 3)
        if (window.at()) window.waitStep(page.editingRow === row && dateButton(row) !== null, "the button is back")
        if (window.at()) {
            const b = dateButton(row)
            window.tapAt(b, b.width / 2, b.height / 2)
        }
        if (window.at()) window.waitStep(quickMenuOpen(), "the menu opens again")
        if (window.at()) window.tapItem(quickItem("quickDatePick"))
        if (window.at()) window.waitStep(!quickMenuOpen() && dateDialog() !== null && dateDialog().opened,
                                         "“Pick a date…” opens the date dialog")
        if (window.at()) {
            check(datePicker().selectedDate === "2026-10-16" && DevTest.property(dateDialog(), "removable") === false,
                  "on tomorrow, nothing to remove: " + datePicker().selectedDate)
            datePicker().selectedDate = "2026-10-21"
            dateDialog().accept()
        }
        if (window.at()) window.waitStep(!dateDialog().opened, "the dialog closes")
        if (window.at()) window.waitStep(Js.indexOf(line("Położyć hydroizolację"), "Położyć hydroizolację 📅 2026-10-21") > 0,
                                         "the picked date is written: " + line("Położyć hydroizolację"))
        if (window.at()) {
            page.stopEditing()
            page.undo()
        }
        if (window.at()) check(line("Położyć hydroizolację") === "\t- [ ] Położyć hydroizolację", "undo takes it away")
    }
    function addDateNoneSteps(): void {
        // A done task and a dated one have no button, also while edited.
        if (window.at()) editor.startEditing(window.rowOf("Zmierzyć ściany"), 2)
        if (window.at()) window.waitStep(page.editingRow === window.rowOf("Zmierzyć ściany"), "the done task is edited")
        if (window.at()) check(dateButton(window.rowOf("Zmierzyć ściany")) === null, "a done task has no “Add date”")
        if (window.at()) editor.startEditing(window.rowOf("Kupić płytki"), 2)
        if (window.at()) window.waitStep(page.editingRow === window.rowOf("Kupić płytki"), "the dated task is edited")
        if (window.at()) {
            check(dateButton(window.rowOf("Kupić płytki")) === null, "a dated task has no “Add date”")
            page.stopEditing()
        }
    }

    Component.onCompleted: window.runSteps("datechips")
}
