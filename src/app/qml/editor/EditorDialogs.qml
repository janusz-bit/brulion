pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Brulion

// EditorDialogs — the dialogs the block editor opens (all asynchronous, never exec()):
//   createReminder(row, alarm)      BReminderDialog → ⏰ / ⏰! token (+ 🔁) on the block
//   editReminder(row, from, to)     a clicked reminder chip: edit / delete that token
//   pickDate(row, field)            Due date… (📅) / Scheduled… (⏳): BDatePicker
//   pickCsvTable(row)               "/Table from CSV…": FilePicker → PageController.insertCsvFile
//   saveTableCsv(row)               "Save as CSV…": FilePicker (save) → saveTableCsv
//   pickDateAt(row, from, to, kind, at)  a date chip's "Change date…" (DateChipMenu): that token
//                                   (kind "day": a [[day]] link, a date without a time)
//   pickRecurrence(row)             Repeat… (🔁): quick choices or a rule typed in Polish/English
//   pickRecurrenceAt(row, from, to, rule)  a 🔁 chip's "Change rule…": that rule
//   pickDateWith / editReminderWith / pickRecurrenceWith(target, …)  the same dialogs for a token
//                                   elsewhere (a query result, an embed, a table cell, Tasks, the
//                                   calendar — there without an editor or controller): the choice
//                                   is the signal applied(target, action, a, b) (DateChipMenu's
//                                   actions); `target` is the caller's name for its request
//   pickPropertyDate(target, title, at, withTime)  a page property's date (PropertyRow): the same
//                                   dialog titled with the property's name, "Clear" to empty it
//   pickJournalDate(row, cursor)    Date…: a [[journal day]] link at the caret
//   pickTemplate(row)               Template…: insert a template from the templates folder
//   pickImage(row, cursor)          Image…: pictures from the system's file picker (desktop,
//                                   Android) or the browser's, stored as attachments, `![[…]]`
//   pickFileLink(row, cursor)       File…: any files from the system's file picker or the browser's,
//                                   kept in the notebook, `[[…]]` links at the caret
//   pickWebImage(row, cursor)       Picture from the internet…: a picture's address → `![](https://…)`,
//                                   with "Save to notebook" stored as an attachment right away
//   pickDrawingBackdrop(row, cursor)  Drawing on a picture or PDF…: a picture or a PDF (the system's
//                                   picker; the browser's: pictures only) → a PDF's pages chosen in
//                                   DrawingBackdropDialog → one drawing per page (a picture: one
//                                   drawing) with it as the backdrop, embedded at the caret and
//                                   after it; one drawing opens in the drawing editor. A copy of
//                                   a picked file stored for nothing (cancelled, a locked PDF, a
//                                   picture that cannot be shown) is removed again.
// The page may change while a dialog is open (sync, Syncthing): a dialog tracks its block
// (PageController.trackBlock) and acts on wherever that block is then — never on whatever block
// moved into its row (and on nothing when the block itself changed or is gone).
// Each dialog is created when it is first needed: every page and every day of the journal stream
// has its own editor, and creating all of them with it made each day slow to build while the
// journal scrolled (docs/UI.md "Scrolling performance").
Item {
    id: root

    /// The editor (an Item: BlockEditor passes itself, see blockItem in the block views).
    property Item editorItem
    final property BlockEditor editor: editorItem as BlockEditor
    property PageController controller
    readonly property bool anyOpen: (reminderDialog !== null && reminderDialog.opened)
                                    || (dateDialog !== null && dateDialog.opened)
                                    || (recurrenceDialog !== null && recurrenceDialog.opened)
                                    || (templateDialog !== null && templateDialog.opened)
                                    || (imageDialog !== null && imageDialog.visible)
                                    || (fileLinkDialog !== null && fileLinkDialog.visible)
                                    || (csvDialog !== null && csvDialog.visible)
                                    || (csvSaveDialog !== null && csvSaveDialog.visible)
                                    || (webImageDialog !== null && webImageDialog.opened)
                                    || (backdropPicker !== null && backdropPicker.visible)
                                    || (backdropDialog !== null && backdropDialog.opened)

    /// The dialogs, null until first used (see the file comment).
    property BReminderDialog reminderDialog: null
    // (BDialogs with members of their own — `picker`, `ruleField`, `removable`, `templates` —
    // set up from this item's properties as they open.)
    property BDialog dateDialog: null
    property BDialog recurrenceDialog: null
    property BDialog templateDialog: null
    property FilePicker imageDialog: null
    property FilePicker fileLinkDialog: null
    property FilePicker csvDialog: null
    property FilePicker csvSaveDialog: null
    property BDialog webImageDialog: null
    property FilePicker backdropPicker: null
    property DrawingBackdropDialog backdropDialog: null
    /// "Drawing on a picture or PDF…": the browser's pick and a picture being prepared (0 none).
    property int backdropPick: 0
    property int backdropJob: 0
    /// The picked file's vault path until a drawing uses it: abandoned, its copy goes again
    /// (PageController.discardBackdropSource).
    property string backdropImport: ""
    /// The page the drawings are for (the controller's path at the pick): another page loaded
    /// meanwhile gets none.
    property string backdropPage: ""
    function reminderPopup(): BReminderDialog {
        if (!reminderDialog)
            reminderDialog = reminderComponent.createObject(root) as BReminderDialog
        return reminderDialog
    }
    function datePopup(): BDialog {
        if (!dateDialog)
            dateDialog = dateComponent.createObject(root) as BDialog
        return dateDialog
    }
    function recurrencePopup(): BDialog {
        if (!recurrenceDialog)
            recurrenceDialog = recurrenceComponent.createObject(root) as BDialog
        return recurrenceDialog
    }
    function templatePopup(): BDialog {
        if (!templateDialog)
            templateDialog = templateComponent.createObject(root) as BDialog
        return templateDialog
    }
    function imagePicker(): FilePicker {
        if (!imageDialog)
            imageDialog = imageComponent.createObject(root) as FilePicker
        return imageDialog
    }

    /// The recurrence dialog's quick choices.
    readonly property list<var> quickRules: [
        { rule: "every day", label: qsTr("Daily") },
        { rule: "every weekday", label: qsTr("Weekdays") },
        { rule: "every week", label: qsTr("Weekly") },
        { rule: "every 2 weeks", label: qsTr("Every 2 weeks") },
        { rule: "every month", label: qsTr("Monthly") },
        { rule: "every year", label: qsTr("Yearly") }
    ]

    property int row: -1
    /// PageController.trackBlock() handle of the dialog's block (see blockRow()).
    property int handle: 0
    property int from: -1
    property int to: -1
    property string field: "due"
    property int cursor: -1
    property string mode: ""   // date dialog: "token" | "range" (from, to) | "journal" | "apply"
    /// "apply" dialogs: the caller's name for its request, given back by applied() (pick…With()).
    property string applyTarget: ""
    /// The choice of a dialog opened for `target` (pick…With(), pickPropertyDate()): the edit to
    /// make — ("set", value, 0), ("remove", 0, 0), ("reminder", at, { alarm, recurrence }) or
    /// (the recurrence action, rule, 0).
    signal applied(string target, string action, var a, var b)

    // What the date dialog shows as it opens (it takes them in its onAboutToShow).
    property string dateTitle: ""
    property string dateIcon: "calendar"
    property string dateSelected: ""
    property string dateTime: ""
    property bool dateWithTime: true
    property bool dateTimeRequired: false
    property bool dateRemovable: false
    property string dateRemoveText: ""
    // What the recurrence dialog shows as it opens: the rule's text, "Don't repeat".
    property string recurrenceText: ""
    property bool recurrenceRemovable: false
    /// The recurrence dialog: "" (the block's rule) | "range" (the rule at from, to) | "apply".
    property string recurrenceMode: ""
    /// The action the recurrence dialog's choice is given to in "apply" mode.
    property string recurrenceAction: "recurrence"
    /// The reminder dialog edits through `apply` (editReminderWith()).
    property bool reminderApply: false

    /// What a controller or DatesHelper call answered (a map).
    MapHolder { id: answer }
    MapHolder { id: parts }
    /// The reminder chosen in the reminder dialog ({ at, alarm, recurrence }).
    MapHolder { id: chosen }
    function answerText(key: string): string {
        return BlockViews.textAt(answer.map, key)
    }
    /// `text` safe for the reminder dialog's context line (BlockEditor.autoTextSafe).
    function safeText(text: string): string {
        return editor.autoTextSafe(text)
    }
    /// `text` escaped when it has markup.
    function escapedText(text: string): string {
        return Js.indexOf(text, "<") >= 0 ? TextHelper.escapeHtml(text) : text
    }
    function chosenText(key: string): string {
        return BlockViews.textAt(chosen.map, key)
    }

    function remember(r: int): void {
        row = r
        if (!controller)
            return
        if (handle > 0)
            controller.untrack(handle)
        handle = controller.trackBlock(r)
    }
    /// Current row of the dialog's block, -1 when it is gone.
    function blockRow(): int {
        return controller && handle > 0 ? controller.trackedRow(handle) : -1
    }

    function refocus(): void {
        if (controller && controller.editingRow >= 0 && editor)
            refocusTimer.restart()
    }
    Later {
        id: refocusTimer
        onTriggered: {
            if (root.editor && root.editor.alive && root.controller && root.controller.editingRow >= 0)
                root.editor.startEditing(root.controller.editingRow, root.controller.cursorPosition)
        }
    }

    function createReminder(r: int, alarm: bool): void {
        remember(r)
        reminderApply = false
        from = -1
        to = -1
        const reminder = reminderPopup()
        reminder.create(editor.autoTextSafe(controller.blockContext(r)))
        answer.map = controller.blockDates(r)
        const rule = BlockViews.textAt(answer.map, "recurrence")
        if (rule !== "")
            reminder.setRecurrence(rule)
        reminder.alarm = alarm
    }
    function editReminder(r: int, f: int, t: int): void {
        answer.map = controller.reminderAt(r, f, t)
        if (!answer.map.at)
            return
        remember(r)
        reminderApply = false
        from = f
        to = t
        const at = root.answerText("at")
        const recurrence = root.answerText("recurrence")
        const context = root.safeText(root.answerText("context"))
        reminderPopup().editWith(at, answer.map.alarm === true, recurrence, context)
    }
    /// The reminder dialog for a reminder elsewhere (a chip's dialogAt || at, alarm, rule,
    /// context); the result is applied(target, "reminder", at, { alarm, recurrence }) — "Delete":
    /// applied(target, "remove", 0, 0).
    function editReminderWith(target: string, at: string, alarm: bool, rule: string, context: string): void {
        applyTarget = target
        reminderApply = true
        const shown = root.escapedText(context)
        reminderPopup().editWith(at, alarm, rule, shown)
    }
    function pickDate(r: int, which: string): void {
        remember(r)
        field = which
        mode = "token"
        answer.map = controller.blockDates(r)
        const current = BlockViews.textAt(answer.map, which)
        parts.map = DatesHelper.splitSpec(current)
        const ok = parts.map.ok === true
        dateTitle = which === "scheduled" ? qsTr("Scheduled") : which === "start" ? qsTr("Start date") : qsTr("Due date")
        dateIcon = which === "scheduled" ? "hourglass" : "calendar"
        dateSelected = ok ? BlockViews.textAt(parts.map, "date") : DatesHelper.addDays(DatesHelper.today, 1)
        dateTime = ok ? BlockViews.textAt(parts.map, "time") : ""
        dateWithTime = true
        dateTimeRequired = false
        dateRemovable = current !== ""
        dateRemoveText = qsTr("Remove")
        datePopup().open()
    }
    function pickDateAt(r: int, f: int, t: int, kind: string, at: string): void {
        remember(r)
        from = f
        to = t
        mode = "range"
        openDateDialog(kind, at)
    }
    /// The date dialog for a page property (PropertyRow): the property's name as the title, a
    /// date or a date and time (`withTime`), "Clear" for an empty value — applied(target, "set",
    /// value) / (target, "remove"). `at`: the value now ("YYYY-MM-DD[ HH:mm]"; "" — empty: today in
    /// the calendar).
    function pickPropertyDate(target: string, title: string, at: string, withTime: bool): void {
        applyTarget = target
        mode = "apply"
        parts.map = DatesHelper.splitSpec(at)
        const ok = parts.map.ok === true
        dateTitle = title
        dateIcon = withTime ? "calendar-clock" : "calendar"
        dateSelected = ok ? BlockViews.textAt(parts.map, "date") : DatesHelper.today
        dateTime = ok && withTime ? BlockViews.textAt(parts.map, "time") : ""
        dateWithTime = withTime
        dateTimeRequired = false
        dateRemovable = at !== ""
        dateRemoveText = qsTr("Clear")
        datePopup().open()
    }
    /// The date dialog for a date token (or day link) elsewhere: applied(target, "set", value) /
    /// (target, "remove").
    function pickDateWith(target: string, kind: string, at: string): void {
        applyTarget = target
        mode = "apply"
        openDateDialog(kind, at)
    }
    function openDateDialog(kind: string, at: string): void {
        const day = kind === "day"
        parts.map = DatesHelper.splitSpec(at)
        const ok = parts.map.ok === true
        dateTitle = day ? qsTr("Link to a day") : kind === "scheduled" ? qsTr("Scheduled")
                  : kind === "start" ? qsTr("Start date") : kind === "created" ? qsTr("Created")
                  : kind === "done" ? qsTr("Done date") : kind === "cancelled" ? qsTr("Cancelled date")
                  : kind === "reminder" ? qsTr("Reminder") : qsTr("Due date")
        dateIcon = day ? "calendar-days" : kind === "scheduled" ? "hourglass" : kind === "done" ? "calendar-check"
                 : kind === "reminder" ? "calendar-clock" : "calendar"
        dateSelected = ok ? BlockViews.textAt(parts.map, "date") : DatesHelper.today
        dateTime = ok && !day ? BlockViews.textAt(parts.map, "time") : ""
        dateWithTime = !day
        dateTimeRequired = kind === "reminder"
        dateRemovable = !day
        dateRemoveText = qsTr("Remove")
        datePopup().open()
    }
    function pickJournalDate(r: int, c: int): void {
        remember(r)
        cursor = c
        mode = "journal"
        dateTitle = qsTr("Link to a day")
        dateIcon = "calendar-days"
        dateSelected = DatesHelper.today
        dateTime = ""
        dateWithTime = false
        // (The journal link keeps whatever "time required" was: it has no time.)
        dateRemovable = false
        datePopup().open()
    }
    function pickRecurrence(r: int): void {
        remember(r)
        recurrenceMode = ""
        answer.map = controller.blockDates(r)
        openRecurrenceDialog(BlockViews.textAt(answer.map, "recurrence"))
    }
    function pickRecurrenceAt(r: int, f: int, t: int, rule: string): void {
        remember(r)
        from = f
        to = t
        recurrenceMode = "range"
        openRecurrenceDialog(rule)
    }
    /// The recurrence dialog for a rule elsewhere: applied(target, action, rule, 0) ("" = "Don't
    /// repeat").
    function pickRecurrenceWith(target: string, rule: string, action: string): void {
        applyTarget = target
        recurrenceMode = "apply"
        recurrenceAction = action !== "" ? action : "recurrence"
        openRecurrenceDialog(rule)
    }
    /// The rule chosen (or "" for none) where the recurrence dialog was opened for.
    function recurrenceChosen(rule: string): void {
        if (recurrenceMode === "apply") {
            root.applied(applyTarget, recurrenceAction, rule, 0)
        } else if (blockRow() >= 0 && recurrenceMode === "range") {
            controller.applyTokenAction(blockRow(), from, to, "recurrence", rule, 0)
        } else if (blockRow() >= 0) {
            controller.setRecurrence(blockRow(), rule)
        }
        refocus()
    }
    function openRecurrenceDialog(current: string): void {
        recurrenceText = current !== "" ? DatesHelper.ruleLabel(current) : ""
        recurrenceRemovable = current !== ""
        recurrencePopup().open()
    }
    function pickTemplate(r: int): void {
        remember(r)
        templatePopup().open()
    }
    function pickWebImage(r: int, c: int): void {
        if (!controller)
            return
        remember(r)
        cursor = c
        if (!webImageDialog)
            webImageDialog = webImageComponent.createObject(root) as BDialog
        webImageDialog.open()
    }
    function pickDrawingBackdrop(r: int, c: int): void {
        if (!controller)
            return
        remember(r)
        cursor = c
        backdropPage = controller.path
        // The browser has its own picker (pictures only; picked() below).
        const pick = DrawingBackdrops.pickInBrowser()
        if (pick > 0) {
            backdropPick = pick
            return
        }
        if (!backdropPicker)
            backdropPicker = backdropPickerComponent.createObject(root) as FilePicker
        backdropPicker.open()
    }
    /// The picked file (a vault path): a PDF's pages are chosen first, a picture is prepared now
    /// (with its drawing: off the GUI thread).
    function useBackdropSource(path: string): void {
        backdropImport = path
        if (path === "") {
            refocus()
            return
        }
        if (DrawingBackdrops.isPdf(path)) {
            if (!backdropDialog)
                backdropDialog = backdropDialogComponent.createObject(root) as DrawingBackdropDialog
            backdropDialog.openFor(path, "insert", 1)
            return
        }
        backdropJob = DrawingBackdrops.prepare(path, root.noPages(), true)
    }
    function noPages(): list<int> {
        return []
    }
    /// No drawing was made of the picked file: the copy stored for it goes (never a file of the
    /// notebook or one reused).
    function abandonBackdropSource(): void {
        if (backdropImport !== "" && controller)
            controller.discardBackdropSource(backdropImport)
        backdropImport = ""
    }
    /// The prepared backdrops become drawings in the note: one opens in the drawing editor.
    function insertBackdrops(items: list<var>): void {
        root.prepared = items
        const goodCount = root.preparedOf(root.prepared).length
        if (goodCount < root.prepared.length)
            AppController.toast(DrawingBackdrops.errorText(BlockViews.firstItemError(root.prepared)))
        if (goodCount === 0 || !controller) {
            abandonBackdropSource()
            refocus()
            return
        }
        if (controller.path !== backdropPage) {
            // Another page is shown now: never at the end of that one.
            abandonBackdropSource()
            AppController.toast(qsTr("Another page was opened while the picture was prepared: no drawing was inserted."))
            return
        }
        // (The block gone meanwhile: at the end of the page.)
        const r = blockRow()
        const c = r >= 0 ? cursor : -1
        answer.map = controller.insertDrawingsOn(r, c, c, root.preparedOf(root.prepared))
        root.prepared = []
        const count = root.answerPaths().length
        if (count === 0)
            abandonBackdropSource()
        backdropImport = "" // (used by the drawings: kept)
        if (count === 1)
            controller.openDrawing(root.answerPaths()[0])
        else if (count > 1)
            AppController.toast(qsTr("Inserted %n drawing(s)", "", count))
        else
            refocus()
    }
    /// The prepared backdrops being inserted (insertBackdrops()).
    property list<var> prepared
    /// The prepared items without an error.
    function preparedOf(items: list<var>): list<var> {
        return BlockViews.preparedItems(items)
    }
    /// `paths` of the last answer (insertDrawingsOn()).
    function answerPaths(): list<string> {
        if (answer.map.paths === undefined)
            return []
        return answer.map.paths
    }
    /// The name of a picked file's URL (its last segment, decoded).
    function fileNameOf(url: string): string {
        return Js.decodeURIComponent(Js.sliceFrom(url, Js.lastIndexOf(url, "/") + 1))
    }

    Connections {
        target: DrawingBackdrops
        // (Every page and journal day has its editor: only the one waiting listens.)
        enabled: root.backdropPick > 0 || root.backdropJob > 0
        function onPicked(pick: int, path: string) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            root.useBackdropSource(path)
        }
        function onPickFailed(pick: int, name: string) {
            if (pick !== root.backdropPick)
                return
            root.backdropPick = 0
            AppController.toast(DrawingBackdrops.errorText("unreadable"))
            root.refocus()
        }
        function onFinished(job: int, items: list<var>, cancelled: bool) {
            if (job !== root.backdropJob)
                return
            // (The items first: an argument read after another write does not compile.)
            if (!cancelled)
                root.insertBackdrops(items)
            else
                root.abandonBackdropSource()
            root.backdropJob = 0
        }
    }

    Component {
        id: backdropPickerComponent
        FilePicker {
            title: qsTr("Drawing on a picture or PDF")
            fileMode: FilePicker.OpenFile
            dark: Theme.dark
            colors: Theme.colors
            nameFilters: DrawingBackdrops.nameFilters
            onAccepted: root.useBackdropSource(root.controller ? root.controller.importBackdropSource(selectedFile) : "")
            onRejected: root.refocus()
        }
    }
    Component {
        id: backdropDialogComponent
        DrawingBackdropDialog {
            onPrepared: (items) => root.insertBackdrops(items)
            onRejected: {
                root.abandonBackdropSource()
                root.refocus()
            }
        }
    }

    function pickImage(r: int, c: int): void {
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
        FilePicker {
            title: qsTr("Insert a picture")
            fileMode: FilePicker.OpenFiles
            dark: Theme.dark
            colors: Theme.colors
            nameFilters: root.controller ? root.controller.imageNameFilters : []
            onAccepted: {
                const r = root.blockRow()
                const c = r >= 0 ? root.cursor : -1
                answer.map = r >= 0 ? root.controller.blocks.get(r) : ({})
                if (r >= 0 && answer.map.view === "text")
                    root.controller.insertFiles(r, c, c, selectedFiles)
                else
                    root.controller.insertFilesAfter(r, selectedFiles)
                root.refocus()
            }
            onRejected: root.refocus()
        }
    }

    function pickFileLink(r: int, c: int): void {
        if (!controller)
            return
        remember(r)
        cursor = c
        // The browser has its own picker (asynchronous, inserted by the controller).
        if (controller.pickFileLinksInBrowser(r, c, c))
            return
        if (!fileLinkDialog)
            fileLinkDialog = fileLinkComponent.createObject(root) as FilePicker
        fileLinkDialog.open()
    }

    Component {
        id: fileLinkComponent
        FilePicker {
            title: qsTr("Link to a file")
            fileMode: FilePicker.OpenFiles
            dark: Theme.dark
            colors: Theme.colors
            onAccepted: {
                const r = root.blockRow()
                root.controller.insertFileLinks(r, r >= 0 ? root.cursor : -1, r >= 0 ? root.cursor : -1, selectedFiles)
                root.refocus()
            }
            onRejected: root.refocus()
        }
    }

    /// "/Table from CSV…": a CSV file as a table in place of the block (PageController.insertCsvFile).
    function pickCsvTable(r: int): void {
        if (!controller)
            return
        remember(r)
        // The browser has its own picker (asynchronous, inserted by the controller).
        if (controller.pickCsvInBrowser(r))
            return
        if (!csvDialog)
            csvDialog = csvComponent.createObject(root) as FilePicker
        csvDialog.open()
    }

    Component {
        id: csvComponent
        FilePicker {
            title: qsTr("Table from a CSV file")
            fileMode: FilePicker.OpenFile
            dark: Theme.dark
            colors: Theme.colors
            nameFilters: [qsTr("CSV files (*.csv *.tsv *.txt)"), qsTr("All files (*)")]
            onAccepted: {
                answer.map = root.controller.insertCsvFile(root.blockRow(), selectedFile)
                if (answer.map.row < 0)
                    root.editor.notice(qsTr("“%1” holds no table Brulion can read.")
                                      .arg(root.fileNameOf("" + selectedFile)), "error", null)
                else
                    root.editor.csvInserted(answer.map.truncated === true)
            }
            onRejected: root.refocus()
        }
    }

    /// The table menu's "Save as CSV…": the system's save picker (the browser: a download).
    function saveTableCsv(r: int): void {
        if (!controller)
            return
        if (controller.downloadTableCsv(r))
            return
        remember(r)
        if (!csvSaveDialog)
            csvSaveDialog = csvSaveComponent.createObject(root) as FilePicker
        csvSaveDialog.currentFolder = StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        csvSaveDialog.selectedFile = csvSaveDialog.currentFolder + "/" + controller.tableCsvFileName(r)
        csvSaveDialog.open()
    }

    Component {
        id: csvSaveComponent
        FilePicker {
            objectName: "tableCsvSaveDialog"
            title: qsTr("Save the table as CSV")
            fileMode: FilePicker.SaveFile
            dark: Theme.dark
            colors: Theme.colors
            nameFilters: [qsTr("CSV files (*.csv)")]
            defaultSuffix: "csv"
            onAccepted: {
                if (root.controller.saveTableCsv(root.blockRow(), selectedFile))
                    root.editor.notice(qsTr("The table was saved as CSV."), "success", null)
                else
                    root.editor.notice(qsTr("The file could not be written."), "error", null)
            }
        }
    }

    Component {
        id: webImageComponent
        BDialog {
            id: webImageDialog
            readonly property alias urlField: urlField
            readonly property alias saveBox: saveBox
            readonly property bool valid: root.controller !== null && root.controller.isWebPictureUrl(urlField.typedText)
            onAboutToShow: {
                urlField.text = ""
                saveBox.checked = true // (each time: the dialog is reused)
            }
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
                errorText: Js.trim(urlField.typedText) !== "" && !webImageDialog.valid ? qsTr("Enter an address starting with http:// or https://") : ""
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
                chosen.map = r
                const at = root.chosenText("at")
                const recurrence = root.chosenText("recurrence")
                const alarm = chosen.map.alarm === true
                if (root.reminderApply) {
                    root.applied(root.applyTarget, "reminder", at, { alarm: alarm, recurrence: recurrence })
                } else if (root.blockRow() >= 0) {
                    root.controller.setReminder(root.blockRow(), at, alarm, recurrence, root.from, root.to)
                }
                root.refocus()
            }
            onDeleteRequested: {
                if (root.reminderApply) {
                    root.applied(root.applyTarget, "remove", 0, 0)
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
            property string removeText: qsTr("Remove")
            readonly property alias picker: picker
            parent: T.Overlay.overlay
            preferredWidth: Theme.dialogWidthSm + 40
            // What the opening function asked for (pickDate() and the others).
            onAboutToShow: {
                picker.clearTyped()
                dateDialog.title = root.dateTitle
                dateDialog.iconName = root.dateIcon
                picker.selectedDate = root.dateSelected
                picker.time = root.dateTime
                picker.withTime = root.dateWithTime
                picker.timeRequired = root.dateTimeRequired
                picker.showMonthOf(picker.selectedDate)
                dateDialog.removable = root.dateRemovable
                dateDialog.removeText = root.dateRemoveText
            }
            acceptText: root.mode === "journal" ? qsTr("Insert") : qsTr("Save")
            // A recognised typed date counts before it is confirmed (commitPending()).
            acceptEnabled: (picker.selectedDate !== "" || natural.map.ok === true) && !picker.timeInvalid
            /// The picker's natural-language date ({ ok, … }).
            MapHolder { id: natural; map: picker.natural }
            footerLeading: [
                BButton {
                    objectName: "dateDialogRemove"
                    visible: dateDialog.removable
                    text: dateDialog.removeText
                    iconName: "trash-2"
                    variant: "ghost"
                    textColor: Theme.dangerText
                    onClicked: {
                        if (root.mode === "apply") {
                            root.applied(root.applyTarget, "remove", 0, 0)
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
                    root.applied(root.applyTarget, "set", picker.value, 0)
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
                    picked.map = DatesHelper.splitSpec(value)
                    if (picked.map.ok === true) {
                        picker.selectedDate = BlockViews.textAt(picked.map, "date")
                        picker.time = BlockViews.textAt(picked.map, "time")
                    }
                }
                MapHolder { id: picked }
            }
        }
    }

    Component {
        id: recurrenceComponent
        BDialog {
            id: recurrenceDialog
            property bool removable: false
            readonly property alias ruleField: ruleField
            /// The typed rule, parsed: { ok, canonical, label, … }.
            readonly property MapHolder parsed: MapHolder { map: recurrenceDialog.parse(ruleField.text) }
            readonly property bool parsedOk: parsed.map.ok === true
            readonly property string canonical: BlockViews.textAt(parsed.map, "canonical")
            function parse(text: string): var {
                // (Nothing typed: { ok: false }, as for a text that is no rule.)
                return DatesHelper.parseRule(Js.trim(text) !== "" ? text : "")
            }
            onAboutToShow: {
                ruleField.text = root.recurrenceText
                recurrenceDialog.removable = root.recurrenceRemovable
            }
            parent: T.Overlay.overlay
            title: qsTr("Repeat")
            iconName: "repeat"
            iconColor: Theme.chipRecurrenceFg
            preferredWidth: Theme.dialogWidth
            acceptText: qsTr("Save")
            acceptEnabled: recurrenceDialog.parsedOk
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
            onAccepted: root.recurrenceChosen(recurrenceDialog.canonical)
            onRejected: root.refocus()

            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: root.quickRules
                    delegate: BChip {
                        id: quickRule
                        required property string rule
                        required property string label
                        interactive: true
                        showIcon: false
                        text: quickRule.label
                        selected: recurrenceDialog.parsedOk && recurrenceDialog.canonical === quickRule.rule
                        onClicked: ruleField.text = DatesHelper.ruleLabel(quickRule.rule)
                    }
                }
            }
            BTextField {
                id: ruleField
                Layout.fillWidth: true
                leadingIcon: "repeat"
                placeholderText: qsTr("e.g. every week on Monday, every month on the 15th")
                // No sentence: no automatic capital letter from the keyboard ("Co tydzień": typed rules
                // are Polish or English).
                inputMethodHints: Qt.ImhNoAutoUppercase
                errorText: Js.trim(ruleField.text) !== "" && !recurrenceDialog.parsedOk ? qsTr("Not a recurrence rule") : ""
                onAccepted: recurrenceDialog.tryAccept()
            }
            RowLayout {
                Layout.fillWidth: true
                visible: recurrenceDialog.parsedOk
                spacing: Theme.space2
                BChip {
                    variant: "recurrence"
                    text: recurrenceDialog.parsedOk ? BlockViews.textAt(recurrenceDialog.parsed.map, "label") : ""
                }
                Text {
                    Layout.fillWidth: true
                    visible: DatesHelper.locale !== "en"
                    text: qsTr("Written in the note as: %1").arg(recurrenceDialog.canonical)
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
            onAboutToShow: templateModel.filter = ""
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
                id: templateSearch
                onTextChanged: templateModel.filter = templateSearch.text
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
                    id: templateItem
                    required property string title
                    required property string path
                    Layout.fillWidth: true
                    iconName: "file-text"
                    text: templateItem.title
                    subtitle: templateItem.path
                    onClicked: {
                        if (root.blockRow() >= 0)
                            root.controller.insertTemplate(root.blockRow(), templateItem.path)
                        templateDialog.close()
                    }
                }
            }
        }
    }
}
