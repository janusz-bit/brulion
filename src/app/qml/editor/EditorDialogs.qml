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
//   pickRecurrence(row)             Powtarzaj (🔁): quick choices or a rule typed in Polish/English
//   pickJournalDate(row, cursor)    Data…: a [[journal day]] link at the caret
//   pickTemplate(row)               Szablon…: insert a template from the templates folder
//   pickImage(row, cursor)          Obraz…: pictures from the system's file picker (desktop,
//                                   Android) or the browser's, stored as attachments, `![[…]]`
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

    /// The dialogs, null until first used (see the file comment).
    property BReminderDialog reminderDialog: null
    // (BDialogs with members of their own: `picker`, `ruleField`, `removable`, `templates`.)
    property var dateDialog: null
    property var recurrenceDialog: null
    property var templateDialog: null
    property FileDialog imageDialog: null
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
    property string mode: ""   // date dialog: "token" | "journal"

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
        from = f
        to = t
        data.context = editor.autoTextSafe(data.context)
        reminderPopup().edit(data)
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
        const current = controller.blockDates(r).recurrence
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
        id: reminderComponent
        BReminderDialog {
            parent: T.Overlay.overlay
            onReminderAccepted: (r) => {
                if (root.blockRow() >= 0)
                    root.controller.setReminder(root.blockRow(), r.at, r.alarm, r.recurrence, root.from, root.to)
                root.refocus()
            }
            onDeleteRequested: {
                if (root.from >= 0 && root.blockRow() >= 0)
                    root.controller.removeToken(root.blockRow(), root.from, root.to)
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
                        if (root.blockRow() >= 0)
                            root.controller.setDateToken(root.blockRow(), root.field, "")
                        dateDialog.close()
                        root.refocus()
                    }
                }
            ]
            onAcceptRequested: picker.commitPending()
            onAccepted: {
                const r = root.blockRow()
                if (r >= 0 && root.mode === "journal")
                    root.controller.insertToken(r, root.cursor, root.controller.journalLink(picker.selectedDate))
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
                        if (root.blockRow() >= 0)
                            root.controller.setRecurrence(root.blockRow(), "")
                        recurrenceDialog.close()
                        root.refocus()
                    }
                }
            ]
            onAccepted: {
                if (root.blockRow() >= 0)
                    root.controller.setRecurrence(root.blockRow(), recurrenceDialog.parsed.canonical)
                root.refocus()
            }
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
