pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Dialogs
import Brulion

// EditorDialogs — the dialogs the block editor opens (all asynchronous, never exec()):
//   createReminder(row, alarm)      BReminderDialog → ⏰ / ⏰! token (+ 🔁) on the block
//   editReminder(row, from, to)     a clicked reminder chip: edit / delete that token
//   pickDate(row, field)            Termin (📅 due) / Zaplanuj (⏳ scheduled): BDatePicker
//   pickDateAt(row, from, to, kind, at)  a date chip's "Zmień datę…" (DateChipMenu): that token
//                                   (kind "day": a [[day]] link, a date without a time)
//   pickRecurrence(row)             Powtarzaj (🔁): quick choices or a rule typed in Polish/English
//   pickRecurrenceAt(row, from, to, rule)  a 🔁 chip's "Zmień regułę…": that rule
//   pickDateWith / editReminderWith / pickRecurrenceWith(apply, …)  the same dialogs for a token
//                                   elsewhere (a query result, an embed, a table cell, …): the
//                                   choice goes to apply(action, a, b) (DateChipMenu's actions)
//   pickJournalDate(row, cursor)    Data…: a [[journal day]] link at the caret
//   pickTemplate(row)               Szablon…: insert a template from the templates folder
//   pickImage(row, cursor)          Obraz…: pictures from the system's file picker (desktop,
//                                   Android) or the browser's, stored as attachments, `![[…]]`
//   pickWebImage(row, cursor)       Obraz z internetu…: a picture's address → `![](https://…)`,
//                                   with "Zapisz w notatniku" stored as an attachment right away
// The page may change while a dialog is open (sync, Syncthing): a dialog tracks its block
// (PageController.trackBlock) and acts on wherever that block is then — never on whatever block
// moved into its row (and on nothing when the block itself changed or is gone).
// Each dialog is created when it is first needed: every page and every day of the journal stream
// has its own editor, and creating all of them with it made each day slow to build while the
// journal scrolled (docs/UI.md "Scrolling performance").
Item {
    id: root

    property BlockEditor editor
    property PageController controller
    readonly property bool anyOpen: (reminderDialog !== null && reminderDialog.opened)
                                    || (dateDialog !== null && dateDialog.opened)
                                    || (recurrenceDialog !== null && recurrenceDialog.opened)
                                    || (templateDialog !== null && templateDialog.opened)
                                    || (imageDialog !== null && imageDialog.visible)
                                    || (webImageDialog !== null && webImageDialog.opened)

    /// The dialogs, null until first used (see the file comment).
    property BReminderDialog reminderDialog: null
    // (BDialogs with members of their own: `picker`, `ruleField`, `removable`, `templates`.)
    property var dateDialog: null
    property var recurrenceDialog: null
    property var templateDialog: null
    property FileDialog imageDialog: null
    property var webImageDialog: null
    function reminderPopup(): BReminderDialog {
        if (!reminderDialog)
            reminderDialog = reminderComponent.createObject(root) as BReminderDialog
        return reminderDialog
    }
    function datePopup() {
        if (!dateDialog)
            dateDialog = dateComponent.createObject(root)
        return dateDialog
    }
    function recurrencePopup() {
        if (!recurrenceDialog)
            recurrenceDialog = recurrenceComponent.createObject(root)
        return recurrenceDialog
    }
    function templatePopup() {
        if (!templateDialog)
            templateDialog = templateComponent.createObject(root)
        return templateDialog
    }
    function imagePicker(): FileDialog {
        if (!imageDialog)
            imageDialog = imageComponent.createObject(root) as FileDialog
        return imageDialog
    }

    property int row: -1
    /// PageController.trackBlock() handle of the dialog's block (see blockRow()).
    property int handle: 0
    property int from: -1
    property int to: -1
    property string field: "due"
    property int cursor: -1
    property string mode: ""   // date dialog: "token" | "range" (from, to) | "journal" | "apply"
    /// "apply" dialogs: function(action, a, b) that makes the edit (pick…With()).
    property var apply: null
    /// The recurrence dialog: "" (the block's rule) | "range" (the rule at from, to) | "apply".
    property string recurrenceMode: ""
    /// The action the recurrence dialog's choice is given to in "apply" mode.
    property string recurrenceAction: "recurrence"
    /// The reminder dialog edits through `apply` (editReminderWith()).
    property bool reminderApply: false

    function remember(r) {
        row = r
        if (!controller)
            return
        if (handle > 0)
            controller.untrack(handle)
        handle = controller.trackBlock(r)
    }
    /// Current row of the dialog's block, -1 when it is gone.
    function blockRow() {
        return controller && handle > 0 ? controller.trackedRow(handle) : -1
    }

    function refocus() {
        if (controller && controller.editingRow >= 0 && editor)
            editor.later(() => {
                if (root && root.controller && root.controller.editingRow >= 0)
                    root.editor.startEditing(root.controller.editingRow, root.controller.cursorPosition)
            })
    }

    function createReminder(r, alarm) {
        remember(r)
        reminderApply = false
        from = -1
        to = -1
        const reminder = reminderPopup()
        reminder.create(editor.autoTextSafe(controller.blockContext(r)))
        const rule = controller.blockDates(r).recurrence
        if (rule !== "")
            reminder.setRecurrence(rule)
        reminder.alarm = alarm
    }
    function editReminder(r, f, t) {
        const data = controller.reminderAt(r, f, t)
        if (!data.at)
            return
        remember(r)
        reminderApply = false
        from = f
        to = t
        data.context = editor.autoTextSafe(data.context)
        reminderPopup().edit(data)
    }
    /// The reminder dialog for `chip` (a reminder elsewhere: its at, alarm, rule, context); the
    /// result goes to apply("reminder", at, { alarm, recurrence }) — "Usuń" to apply("remove").
    function editReminderWith(fn, chip) {
        apply = fn
        reminderApply = true
        reminderPopup().edit({
            at: chip.dialogAt || chip.at,
            alarm: chip.alarm === true,
            recurrence: chip.rule || "",
            context: editor.autoTextSafe(chip.context || "")
        })
    }
    function pickDate(r, which) {
        remember(r)
        field = which
        mode = "token"
        const current = controller.blockDates(r)[which] || ""
        const parts = DatesHelper.splitSpec(current)
        const dateDialog = datePopup()
        const picker = dateDialog.picker
        picker.clearTyped()
        dateDialog.title = which === "scheduled" ? qsTr("Scheduled") : which === "start" ? qsTr("Start date") : qsTr("Due date")
        dateDialog.iconName = which === "scheduled" ? "hourglass" : "calendar"
        picker.selectedDate = parts.ok ? parts.date : DatesHelper.addDays(DatesHelper.today, 1)
        picker.time = parts.ok ? parts.time : ""
        picker.withTime = true
        picker.timeRequired = false
        picker.showMonthOf(picker.selectedDate)
        dateDialog.removable = current !== ""
        dateDialog.open()
    }
    function pickDateAt(r, f, t, kind, at) {
        remember(r)
        from = f
        to = t
        mode = "range"
        openDateDialog(kind, at)
    }
    /// The date dialog for a date token (or day link) elsewhere: apply("set", value) / ("remove").
    function pickDateWith(fn, kind, at) {
        apply = fn
        mode = "apply"
        openDateDialog(kind, at)
    }
    function openDateDialog(kind, at) {
        const day = kind === "day"
        const parts = DatesHelper.splitSpec(at)
        const dateDialog = datePopup()
        const picker = dateDialog.picker
        picker.clearTyped()
        dateDialog.title = day ? qsTr("Link to a day") : kind === "scheduled" ? qsTr("Scheduled")
                         : kind === "start" ? qsTr("Start date") : kind === "created" ? qsTr("Created")
                         : kind === "done" ? qsTr("Done date") : kind === "cancelled" ? qsTr("Cancelled date")
                         : kind === "reminder" ? qsTr("Reminder") : qsTr("Due date")
        dateDialog.iconName = day ? "calendar-days" : kind === "scheduled" ? "hourglass" : kind === "done" ? "calendar-check"
                            : kind === "reminder" ? "calendar-clock" : "calendar"
        picker.selectedDate = parts.ok ? parts.date : DatesHelper.today
        picker.time = parts.ok && !day ? parts.time : ""
        picker.withTime = !day
        picker.timeRequired = kind === "reminder"
        picker.showMonthOf(picker.selectedDate)
        dateDialog.removable = !day
        dateDialog.open()
    }
    function pickJournalDate(r, c) {
        remember(r)
        cursor = c
        mode = "journal"
        const dateDialog = datePopup()
        const picker = dateDialog.picker
        picker.clearTyped()
        dateDialog.title = qsTr("Link to a day")
        dateDialog.iconName = "calendar-days"
        picker.selectedDate = DatesHelper.today
        picker.time = ""
        picker.withTime = false
        picker.showMonthOf(picker.selectedDate)
        dateDialog.removable = false
        dateDialog.open()
    }
    function pickRecurrence(r) {
        remember(r)
        recurrenceMode = ""
        openRecurrenceDialog(controller.blockDates(r).recurrence)
    }
    function pickRecurrenceAt(r, f, t, rule) {
        remember(r)
        from = f
        to = t
        recurrenceMode = "range"
        openRecurrenceDialog(rule || "")
    }
    /// The recurrence dialog for a rule elsewhere: apply(action, rule) ("" = "Nie powtarzaj").
    function pickRecurrenceWith(fn, rule, action) {
        apply = fn
        recurrenceMode = "apply"
        recurrenceAction = action || "recurrence"
        openRecurrenceDialog(rule || "")
    }
    /// The rule chosen (or "" for none) where the recurrence dialog was opened for.
    function recurrenceChosen(rule) {
        if (recurrenceMode === "apply") {
            if (apply)
                apply(recurrenceAction, rule)
        } else if (blockRow() >= 0 && recurrenceMode === "range") {
            controller.applyTokenAction(blockRow(), from, to, "recurrence", rule, 0)
        } else if (blockRow() >= 0) {
            controller.setRecurrence(blockRow(), rule)
        }
        refocus()
    }
    function openRecurrenceDialog(current) {
        const recurrenceDialog = recurrencePopup()
        recurrenceDialog.ruleField.text = current !== "" ? DatesHelper.ruleLabel(current) : ""
        recurrenceDialog.removable = current !== ""
        recurrenceDialog.open()
    }
    function pickTemplate(r) {
        remember(r)
        const templateDialog = templatePopup()
        templateDialog.templates.filter = ""
        templateDialog.open()
    }
    function pickWebImage(r, c) {
        if (!controller)
            return
        remember(r)
        cursor = c
        if (!webImageDialog)
            webImageDialog = webImageComponent.createObject(root)
        webImageDialog.urlField.text = ""
        webImageDialog.saveBox.checked = true // (each time: the dialog is reused)
        webImageDialog.open()
    }
    function pickImage(r, c) {
        if (!controller)
            return
        remember(r)
        cursor = c
        // The browser has its own picker (asynchronous, inserted by the controller).
        if (controller.pickImageInBrowser(r, c, c))
            return
        imagePicker().open()
    }

    Component {
        id: imageComponent
        FileDialog {
            title: qsTr("Insert a picture")
            fileMode: FileDialog.OpenFiles
            nameFilters: root.controller ? root.controller.imageNameFilters : []
            onAccepted: {
                const r = root.blockRow()
                const c = r >= 0 ? root.cursor : -1
                if (r >= 0 && root.controller.blocks.get(r).view === "text")
                    root.controller.insertFiles(r, c, c, selectedFiles)
                else
                    root.controller.insertFilesAfter(r, selectedFiles)
                root.refocus()
            }
            onRejected: root.refocus()
        }
    }

    Component {
        id: webImageComponent
        BDialog {
            id: webImageDialog
            readonly property alias urlField: urlField
            readonly property alias saveBox: saveBox
            readonly property bool valid: root.controller !== null && root.controller.isWebPictureUrl(urlField.typedText)
            parent: T.Overlay.overlay
            title: qsTr("Picture from the internet")
            iconName: "image"
            preferredWidth: Theme.dialogWidth
            acceptText: qsTr("Insert")
            acceptEnabled: valid
            initialFocusItem: urlField
            onAccepted: {
                const r = root.blockRow()
                const c = r >= 0 ? root.cursor : -1
                root.controller.insertWebPicture(r, c, c, urlField.typedText, saveBox.checked)
                root.refocus()
            }
            onRejected: root.refocus()

            BTextField {
                id: urlField
                objectName: "webImageUrl"
                Layout.fillWidth: true
                placeholderText: "https://…"
                accessibleName: qsTr("Picture address")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                errorText: typedText.trim() !== "" && !webImageDialog.valid ? qsTr("Enter an address starting with http:// or https://") : ""
                onAccepted: if (webImageDialog.valid) webImageDialog.accept()
            }
            BCheckBox {
                id: saveBox
                objectName: "webImageSave"
                Layout.fillWidth: true
                checked: true
                text: qsTr("Save to notebook")
                description: qsTr("A copy in the attachments folder: shown offline and synced with the notes. Without it the picture is loaded from the site when it is shown.")
            }
        }
    }

    Component {
        id: reminderComponent
        BReminderDialog {
            parent: T.Overlay.overlay
            onReminderAccepted: (r) => {
                if (root.reminderApply) {
                    if (root.apply)
                        root.apply("reminder", r.at, { alarm: r.alarm, recurrence: r.recurrence })
                } else if (root.blockRow() >= 0) {
                    root.controller.setReminder(root.blockRow(), r.at, r.alarm, r.recurrence, root.from, root.to)
                }
                root.refocus()
            }
            onDeleteRequested: {
                if (root.reminderApply) {
                    if (root.apply)
                        root.apply("remove", 0, 0)
                } else if (root.from >= 0 && root.blockRow() >= 0) {
                    root.controller.removeToken(root.blockRow(), root.from, root.to)
                }
                root.refocus()
            }
            onRejected: root.refocus()
        }
    }

    Component {
        id: dateComponent
        BDialog {
            id: dateDialog
            property bool removable: false
            readonly property alias picker: picker
            parent: T.Overlay.overlay
            preferredWidth: Theme.dialogWidthSm + 40
            acceptText: root.mode === "journal" ? qsTr("Insert") : qsTr("Save")
            // A recognised typed date counts before it is confirmed (commitPending()).
            acceptEnabled: (picker.selectedDate !== "" || picker.natural.ok) && !picker.timeInvalid
            footerLeading: [
                BButton {
                    visible: dateDialog.removable
                    text: qsTr("Remove")
                    iconName: "trash-2"
                    variant: "ghost"
                    textColor: Theme.dangerText
                    onClicked: {
                        if (root.mode === "apply") {
                            if (root.apply)
                                root.apply("remove", 0, 0)
                        } else if (root.blockRow() >= 0 && root.mode === "range") {
                            root.controller.removeToken(root.blockRow(), root.from, root.to)
                        } else if (root.blockRow() >= 0) {
                            root.controller.setDateToken(root.blockRow(), root.field, "")
                        }
                        dateDialog.close()
                        root.refocus()
                    }
                }
            ]
            onAcceptRequested: picker.commitPending()
            onAccepted: {
                const r = root.blockRow()
                if (root.mode === "apply") {
                    if (root.apply)
                        root.apply("set", picker.value, 0)
                } else if (r >= 0 && root.mode === "journal")
                    root.controller.insertToken(r, root.cursor, root.controller.journalLink(picker.selectedDate))
                else if (r >= 0 && root.mode === "range")
                    root.controller.applyTokenAction(r, root.from, root.to, "set", picker.value, 0)
                else if (r >= 0)
                    root.controller.setDateToken(r, root.field, picker.value)
                root.refocus()
            }
            onRejected: root.refocus()

            BDatePicker {
                id: picker
                Layout.fillWidth: true
                applyTypedOnFocusLoss: true
                onPicked: (value) => {
                    const s = DatesHelper.splitSpec(value)
                    if (s.ok) {
                        selectedDate = s.date
                        time = s.time
                    }
                }
            }
        }
    }

    Component {
        id: recurrenceComponent
        BDialog {
            id: recurrenceDialog
            property bool removable: false
            readonly property alias ruleField: ruleField
            readonly property var parsed: ruleField.text.trim() !== "" ? DatesHelper.parseRule(ruleField.text) : ({ ok: false })
            parent: T.Overlay.overlay
            title: qsTr("Repeat")
            iconName: "repeat"
            iconColor: Theme.chipRecurrenceFg
            preferredWidth: Theme.dialogWidth
            acceptText: qsTr("Save")
            acceptEnabled: parsed.ok
            initialFocusItem: ruleField
            footerLeading: [
                BButton {
                    visible: recurrenceDialog.removable
                    text: qsTr("Don't repeat")
                    iconName: "trash-2"
                    variant: "ghost"
                    textColor: Theme.dangerText
                    onClicked: {
                        recurrenceDialog.close()
                        root.recurrenceChosen("")
                    }
                }
            ]
            onAccepted: root.recurrenceChosen(recurrenceDialog.parsed.canonical)
            onRejected: root.refocus()

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        { rule: "every day", label: qsTr("Daily") },
                        { rule: "every weekday", label: qsTr("Weekdays") },
                        { rule: "every week", label: qsTr("Weekly") },
                        { rule: "every 2 weeks", label: qsTr("Every 2 weeks") },
                        { rule: "every month", label: qsTr("Monthly") },
                        { rule: "every year", label: qsTr("Yearly") }
                    ]
                    delegate: BChip {
                        required property var modelData
                        interactive: true
                        showIcon: false
                        text: modelData.label
                        selected: recurrenceDialog.parsed.ok && recurrenceDialog.parsed.canonical === modelData.rule
                        onClicked: ruleField.text = DatesHelper.ruleLabel(modelData.rule)
                    }
                }
            }
            BTextField {
                id: ruleField
                Layout.fillWidth: true
                leadingIcon: "repeat"
                placeholderText: qsTr("e.g. co tydzień w poniedziałek, every month on the 15th")
                // No sentence: no automatic capital letter from the keyboard ("Co tydzień").
                inputMethodHints: Qt.ImhNoAutoUppercase
                errorText: text.trim() !== "" && !recurrenceDialog.parsed.ok ? qsTr("Not a recurrence rule") : ""
                onAccepted: recurrenceDialog.tryAccept()
            }
            RowLayout {
                Layout.fillWidth: true
                visible: recurrenceDialog.parsed.ok
                spacing: Theme.space2
                BChip {
                    variant: "recurrence"
                    text: recurrenceDialog.parsed.ok ? recurrenceDialog.parsed.label : ""
                }
                Text {
                    Layout.fillWidth: true
                    visible: DatesHelper.locale !== "en"
                    text: qsTr("Written in the note as: %1").arg(recurrenceDialog.parsed.canonical || "")
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.textXs
                    color: Theme.fgFaint
                    wrapMode: Text.Wrap
                }
            }
        }
    }

    Component {
        id: templateComponent
        BDialog {
            id: templateDialog
            readonly property alias templates: templateModel
            parent: T.Overlay.overlay
            title: qsTr("Insert template")
            iconName: "sparkles"
            preferredWidth: Theme.dialogWidth
            showFooter: true
            acceptText: qsTr("Close")
            showReject: false
            onClosed: root.refocus()

            SuggestionModel {
                id: templateModel
                host: root.controller ? root.controller.host : null
                mode: "templates"
                limit: 50
            }
            BTextField {
                Layout.fillWidth: true
                leadingIcon: "search"
                placeholderText: qsTr("Search templates…")
                onTextChanged: templateModel.filter = text
            }
            BEmptyState {
                Layout.fillWidth: true
                visible: templateModel.count === 0
                compact: true
                iconName: "sparkles"
                title: qsTr("No templates")
                description: qsTr("Put Markdown files into the templates folder of the vault.")
            }
            Repeater {
                model: templateModel
                delegate: BListItem {
                    required property string title
                    required property string path
                    Layout.fillWidth: true
                    iconName: "file-text"
                    text: title
                    subtitle: path
                    onClicked: {
                        if (root.blockRow() >= 0)
                            root.controller.insertTemplate(root.blockRow(), path)
                        templateDialog.close()
                    }
                }
            }
        }
    }
}
