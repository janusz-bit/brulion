pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Templates as T
import Brulion

// EditorPlayground — the block editor on a rich Polish sample page in an in-memory vault
// (`brulion --editor`): FakeEditorHost + PageController + BlockEditor, with theme / language /
// touch switches, source mode, undo/redo and a button that changes the file "from outside".
//
// `--editor-script <name>` puts the editor into a state for screenshots (see runScript()):
// editing, suggest, tags, slash, date, zoom, folded, selected, source, reminder, recurrence,
// table, calc, query, embeds, title, empty, conflict, markup; `selftest` runs keyboard/input-method
// checks and exits. The in-memory vault runs ```query blocks on the real index (FakeEditorHost).
ApplicationWindow {
    id: window

    width: 1280
    height: 800
    visible: true
    title: "Brulion — edytor bloków"
    color: Theme.bg
    font.family: Theme.fontUi
    font.pixelSize: Theme.textBase

    Binding { target: Theme; property: "windowWidth"; value: window.width }
    Binding { target: Theme; property: "windowHeight"; value: window.height }
    Binding { target: DatesHelper; property: "locale"; value: DevTools.language }

    FakeEditorHost {
        id: host
        now: DatesHelper.fixedNow
        onNavigated: (route, newPane) => {
            const what = route.view === "tag" ? "#" + route.tag
                       : route.path !== undefined && route.path !== "" ? route.path
                       : route.target !== undefined ? route.target + " " + qsTr("(new page)") : (route.blockId || "")
            toasts.show(qsTr("Open: %1").arg(what), { kind: "info" })
        }
        onUrlOpened: (url) => toasts.show(qsTr("Open in browser: %1").arg(url.toString()), { kind: "info" })
    }

    PageController {
        id: page
        onSaveFailed: toasts.show(qsTr("Could not save the page"), { kind: "error" })
        onExternallyChanged: (merged) => toasts.show(merged ? qsTr("Merged changes made on another device")
                                                            : qsTr("The page changed outside the editor"), { kind: "info" })
    }

    Component.onCompleted: {
        host.populateSample()
        page.host = host
        page.path = DevTools.editorScript === "empty" ? "Nowa strona.md"
                  : DevTools.editorScript === "title" ? host.recipePath : host.samplePath
        if (DevTools.editorScript !== "")
            scriptTimer.start()
    }

    // ---- Header ------------------------------------------------------------------------
    header: Rectangle {
        implicitHeight: Theme.touchUi ? 56 : 48
        color: Theme.bgElevated

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.narrow ? Theme.space2 : Theme.space4
            anchors.rightMargin: Theme.narrow ? Theme.space2 : Theme.space4
            spacing: Theme.space2

            Image {
                visible: !Theme.compact
                source: "qrc:/images/logo.svg"
                sourceSize: Qt.size(24, 24)
                Layout.preferredWidth: 24
                Layout.preferredHeight: 24
            }
            Text {
                Layout.fillWidth: true
                text: Theme.compact ? page.title : "Edytor bloków · " + page.path
                textFormat: Text.PlainText
                font.family: Theme.fontUi
                font.pixelSize: Theme.textMd
                font.weight: Theme.weightSemiBold
                color: Theme.fg
                elide: Text.ElideMiddle
            }
            Text {
                visible: !Theme.compact
                text: page.conflict ? qsTr("Conflict") : page.dirty ? qsTr("Unsaved") : qsTr("Saved")
                font.family: Theme.fontUi
                font.pixelSize: Theme.textSm
                color: page.conflict ? Theme.dangerText : Theme.fgFaint
            }
            BIconButton {
                iconName: "undo-2"
                label: qsTr("Undo")
                shortcut: "Ctrl+Z"
                enabled: page.canUndo
                focusPolicy: Qt.NoFocus
                visible: !Theme.compact
                onClicked: page.undo()
            }
            BIconButton {
                iconName: "redo-2"
                label: qsTr("Redo")
                shortcut: "Ctrl+Shift+Z"
                enabled: page.canRedo
                focusPolicy: Qt.NoFocus
                visible: !Theme.compact
                onClicked: page.redo()
            }
            BIconButton {
                iconName: "code"
                label: qsTr("Source mode")
                shortcut: "Ctrl+E"
                checkable: true
                checked: page.sourceMode
                focusPolicy: Qt.NoFocus
                onClicked: editor.toggleSourceMode()
            }
            BIconButton {
                iconName: "refresh-cw"
                label: qsTr("Simulate a change from another device")
                focusPolicy: Qt.NoFocus
                visible: !Theme.compact
                onClicked: {
                    const text = host.file(page.path)
                    host.externalWrite(page.path, text + "- Dopisane na telefonie #sync\n")
                }
            }
            BSegmented {
                size: "small"
                iconOnly: true
                accessibleName: qsTr("Theme")
                model: [
                    { value: "light", text: qsTr("Light"), icon: "sun" },
                    { value: "dark", text: qsTr("Dark"), icon: "moon" }
                ]
                currentValue: Theme.dark ? "dark" : "light"
                onActivated: (value) => Theme.mode = value
            }
            BSegmented {
                visible: !Theme.compact
                size: "small"
                accessibleName: qsTr("Language")
                model: [{ value: "pl", text: "PL" }, { value: "en", text: "EN" }]
                currentValue: DevTools.language
                onActivated: (value) => DevTools.setLanguage(value)
            }
            BIconButton {
                visible: !Theme.compact
                iconName: "smartphone"
                label: qsTr("Touch sizes")
                checkable: true
                checked: Theme.forceTouch
                focusPolicy: Qt.NoFocus
                onClicked: Theme.forceTouch = !Theme.forceTouch
            }
        }
    }

    // ---- The editor --------------------------------------------------------------------------
    BlockEditor {
        id: editor
        anchors.fill: parent
        controller: page
        focus: true
        onNotice: (text, kind) => toasts.show(text, { kind: kind })
        header: Component {
            Item {
                width: editor.width
                implicitHeight: titleColumn.implicitHeight + Theme.space6 + (Theme.compact ? Theme.space2 : Theme.space8)
                Column {
                    id: titleColumn
                    x: editor.contentX + editor.gutter
                    y: Theme.compact ? Theme.space4 : Theme.space10
                    width: editor.contentWidth - editor.gutter
                    spacing: Theme.space1
                    Text {
                        // A page starting with "# Title" shows that heading as its title.
                        visible: page.titleRow < 0
                        width: parent.width
                        text: page.title
                        textFormat: Text.PlainText
                        font.family: Theme.fontEditor
                        font.pixelSize: Theme.text3xl
                        font.weight: Theme.weightSemiBold
                        color: Theme.fg
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }
                    Text {
                        width: parent.width
                        text: page.path.indexOf("/") > 0 ? page.path.slice(0, page.path.lastIndexOf("/")) : qsTr("Vault")
                        textFormat: Text.PlainText
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.textSm
                        color: Theme.fgFaint
                    }
                }
            }
        }
    }

    BToastHost {
        id: toasts
        parent: T.Overlay.overlay
        anchors.fill: parent
        z: 1000
    }

    // ---- Scripted states for screenshots -----------------------------------------------------
    function rowOf(prefix) {
        const blocks = page.blocks
        for (let i = 0; i < blocks.count; ++i) {
            if (blocks.get(i).plainText.indexOf(prefix) === 0)
                return i
        }
        return -1
    }
    function viewRow(view) {
        for (let i = 0; i < page.blocks.count; ++i) {
            if (page.blocks.get(i).view === view)
                return i
        }
        return -1
    }
    /// Start editing `row` and type `text` into it through the inline editor (like a user).
    function typeInto(row, text) {
        editor.startEditing(row, -1)
        typingTimer.pending = text
        typingTimer.start()
    }

    Timer {
        id: typingTimer
        property string pending
        interval: 150
        onTriggered: {
            const edit = editor.activeEditor
            if (edit)
                edit.insert(edit.cursorPosition, pending)
        }
    }

    Timer {
        id: scriptTimer
        interval: 250
        onTriggered: window.runScript(DevTools.editorScript)
    }

    // ---- Self test: keyboard flows through the real delegates (`--editor-script selftest`) ----
    property var steps: []
    property int stepIndex: 0
    property int failures: 0
    property int baseRow: -1
    property int pasteRow: -1
    property int countBefore: 0
    property int realRow: -1
    property int farRow: -1
    property string textBefore: ""
    function check(ok, what) {
        if (!ok)
            failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    function key(k, modifiers) {
        const edit = editor.activeEditor
        if (!edit || !edit.active)
            return false
        return editor.handleEditorKey({ key: k, modifiers: modifiers || Qt.NoModifier }, edit)
    }
    function info(row) { return page.blocks.get(row) }
    /// A real key press (through the window, Keys handlers and TextEdit) to the focused item.
    function realKey(k, modifiers, text) {
        EditorInput.sendKey(editor.activeEditor ? editor.activeEditor : editor, k, modifiers || 0, text || "")
    }
    /// Type `s` with real key events, one character at a time.
    function typeKeys(s) {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(editor.activeEditor ? editor.activeEditor : editor, 0, 0, s.charAt(i))
    }
    Timer {
        id: stepTimer
        interval: 120
        repeat: true
        onTriggered: {
            if (window.stepIndex >= window.steps.length) {
                stop()
                console.log("SELFTEST DONE failures=" + window.failures)
                Qt.exit(window.failures === 0 ? 0 : 1)
                return
            }
            try {
                window.steps[window.stepIndex]()
            } catch (e) {
                window.check(false, "step " + window.stepIndex + " threw: " + e)
            }
            window.stepIndex += 1
        }
    }
    function selfTest() {
        const r = rowOf("Wywieźć gruz")
        baseRow = r
        steps = [
            () => editor.startEditing(r, -1),
            () => check(editor.activeEditor && editor.activeEditor.row === r && editor.activeEditor.activeFocus, "editing starts with focus"),
            () => check(key(Qt.Key_Return), "Enter handled"),
            () => {
                check(page.editingRow === r + 1, "Enter splits: caret in the new block")
                check(info(r + 1).taskStatus === "todo", "the new block continues the task")
                check(editor.activeEditor && editor.activeEditor.row === r + 1 && editor.activeEditor.activeFocus, "new block has focus")
            },
            () => editor.activeEditor.insert(editor.activeEditor.cursorPosition, "Nowe zadanie ąę"),
            () => check(info(r + 1).plainText === "Nowe zadanie ąę", "typing reaches the model: " + info(r + 1).plainText),
            () => { key(Qt.Key_Tab) },
            () => check(info(r + 1).depth === info(r).depth + 1, "Tab indents"),
            () => { key(Qt.Key_Backtab) },
            () => check(info(r + 1).depth === info(r).depth, "Shift+Tab outdents"),
            () => { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Backspace) },
            () => check(info(r + 1).taskStatus === "" && page.editingRow === r + 1, "Backspace at start removes the checkbox first"),
            () => { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Backspace) },
            () => {
                check(page.editingRow === r, "Backspace again merges with the previous block")
                check(info(r).plainText.indexOf("Nowe zadanie ąę") > 0, "merged text: " + info(r).plainText)
            },
            () => { key(Qt.Key_Z, Qt.ControlModifier) },
            () => check(info(r + 1).plainText === "Nowe zadanie ąę", "Ctrl+Z undoes the merge"),
            () => { key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier) },
            () => check(info(r).plainText.indexOf("Nowe zadanie ąę") > 0, "Ctrl+Shift+Z redoes"),
            () => { key(Qt.Key_Return, Qt.ControlModifier) },
            () => check(info(r).taskStatus === "doing", "Ctrl+Enter cycles the task: " + info(r).taskStatus),
            () => { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Up) },
            () => check(page.editingRow === page.previousVisibleRow(r), "Up on the first line goes to the previous block"),
            () => { key(Qt.Key_Down) ; },
            () => check(page.editingRow === r, "Down on the last line comes back"),
            () => { key(Qt.Key_Up, Qt.AltModifier) },
            () => check(page.editingRow === r - 1 && info(r - 1).plainText.indexOf("Wywieźć") === 0, "Alt+Up moves the block"),
            () => { key(Qt.Key_Down, Qt.AltModifier) },
            () => check(page.editingRow === r, "Alt+Down moves it back"),
            () => {
                const e = editor.activeEditor
                e.cursorPosition = e.length
                e.insert(e.length, " [[Rem")
            },
            () => check(editor.suggestions.visible && editor.suggestions.model.count > 0, "[[ opens page suggestions"),
            () => { key(Qt.Key_Return) },
            () => check(info(r).plainText.indexOf("Remont kuchni") > 0 && !editor.suggestions.opened, "Enter accepts a suggestion: " + info(r).plainText),
            () => { key(Qt.Key_Escape) },
            () => check(page.editingRow === -1 && page.selectedRow === r, "Esc stops editing and selects the block"),
            () => { editor.handleSelectionKey({ key: Qt.Key_Down, modifiers: Qt.NoModifier }) },
            () => check(page.selectedRow === page.nextVisibleRow(r), "Down moves the selection"),
            () => { editor.handleSelectionKey({ key: Qt.Key_Return, modifiers: Qt.NoModifier }) },
            () => check(page.editingRow === page.nextVisibleRow(r) && editor.activeEditor.activeFocus, "Enter edits the selected block"),
            () => { key(Qt.Key_Period, Qt.ControlModifier) },
            () => { page.stopEditing(); page.toggleFold(rowOf("Przygotowanie")) },
            () => check(!info(r).visible, "folding hides the children"),
            () => { page.toggleFold(rowOf("Przygotowanie")); editor.toggleSourceMode() },
            () => check(page.sourceMode && page.sourceText.length > 100, "source mode shows the file"),
            () => { editor.toggleSourceMode() },
            () => check(!page.sourceMode && page.blocks.count > 10, "back to blocks"),
            () => { window.pasteRow = page.insertBlockAfter(rowOf("Ekipa może"), ""); window.countBefore = page.blocks.count },
            () => { const e = editor.activeEditor; e.insert(e.cursorPosition, "- [ ] pierwsze\n- drugie") },
            () => {
                check(page.blocks.count === window.countBefore + 1, "pasted list becomes blocks")
                check(info(window.pasteRow).taskStatus === "todo" && info(window.pasteRow + 1).plainText === "drugie"
                      && page.editingRow === window.pasteRow + 1, "pasted blocks keep tasks, caret in the last one")
            },
            // ---- Real key events (window -> Keys -> TextEdit), Polish text --------------------
            () => { window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "") },
            () => typeKeys("Żółw ąę"),
            () => check(info(window.realRow).plainText === "Żółw ąę", "typed keys reach the block: " + info(window.realRow).plainText),
            () => realKey(Qt.Key_Return),
            () => check(page.editingRow === window.realRow + 1 && editor.activeEditor && editor.activeEditor.activeFocus
                        && editor.activeEditor.row === window.realRow + 1, "real Enter splits and focuses the new block"),
            () => typeKeys("x"),
            () => { realKey(Qt.Key_Backspace); realKey(Qt.Key_Backspace) },
            () => check(page.editingRow === window.realRow && info(window.realRow).plainText === "Żółw ąę"
                        && editor.activeEditor && editor.activeEditor.activeFocus, "real Backspace deletes, then merges"),
            () => realKey(Qt.Key_Tab),
            () => check(info(window.realRow).depth === info(window.realRow - 1).depth + 1 && editor.activeEditor
                        && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow, "real Tab indents, focus stays"),
            () => realKey(Qt.Key_Z, Qt.ControlModifier),
            () => check(info(window.realRow).depth === info(window.realRow - 1).depth && editor.activeEditor
                        && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow,
                        "real Ctrl+Z undoes the indent, focus stays"),
            () => { realKey(Qt.Key_Up, Qt.AltModifier) },
            () => check(editor.activeEditor && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow - 1
                        && info(window.realRow - 1).plainText === "Żółw ąę", "real Alt+Up moves the block, focus follows"),
            () => { realKey(Qt.Key_Down, Qt.AltModifier) },
            // ---- No-break spaces survive editing (TextEdit.text would turn them into spaces) ----
            () => { page.setBody(window.realRow, "w\u00a0domu i\u00a0w\u00a0pracy"); editor.startEditing(window.realRow, -1) },
            () => typeKeys("!"),
            () => check(page.sourceText.indexOf("w\u00a0domu i\u00a0w\u00a0pracy!") >= 0, "no-break spaces are kept when typing"),
            // ---- Input method composition (Android keyboards compose every word) ---------------
            () => { window.realRow = page.insertBlockAfter(window.realRow, ""); window.countBefore = page.blocks.count },
            () => EditorInput.sendInputMethod(editor.activeEditor, "kot"),
            () => check(editor.activeEditor.preeditText === "kot", "composing text is preedit"),
            () => realKey(Qt.Key_Backspace),
            () => check(page.blocks.count === window.countBefore && page.editingRow === window.realRow
                        && info(window.realRow).plainText === "ko", "Backspace while composing edits the word, never merges: " + info(window.realRow).plainText),
            () => EditorInput.sendInputMethod(editor.activeEditor, "pies"),
            () => realKey(Qt.Key_Return),
            () => check(info(window.realRow).plainText === "kopies" && page.editingRow === window.realRow + 1,
                        "Enter while composing keeps the word and splits after it: " + info(window.realRow).plainText),
            () => EditorInput.sendInputMethod(editor.activeEditor, "", "[["),
            () => EditorInput.sendInputMethod(editor.activeEditor, "Rem"),
            () => check(editor.suggestions.visible && editor.suggestions.model.count > 0, "completions follow a composed word"),
            () => realKey(Qt.Key_Return),
            () => check(info(window.realRow + 1).plainText.indexOf("Remont kuchni") >= 0 && editor.activeEditor.preeditText === "",
                        "accepting a completion commits the composed word: " + info(window.realRow + 1).plainText),
            // ---- Typing after the edited block scrolled far away ---------------------------------
            () => {
                let md = ""
                for (let i = 0; i < 150; ++i)
                    md += "- Wiersz " + i + "\n"
                page.insertMarkdown(page.lastVisibleRow(), md)
                window.farRow = rowOf("Kupić płytki")
                editor.startEditing(window.farRow, -1)
            },
            () => editor.listView.positionViewAtEnd(),
            () => check(editor.listView.itemAtIndex(window.farRow) === null, "the edited block's delegate is gone"),
            () => realKey(Qt.Key_Control, Qt.ControlModifier),
            () => check(editor.listView.itemAtIndex(window.farRow) === null, "a lone modifier key does not scroll back"),
            () => typeKeys("Q"),
            () => check(info(window.farRow).plainText.indexOf("Q") > 0 && page.editingRow === window.farRow
                        && editor.activeEditor && editor.activeEditor.activeFocus, "typing brings the edited block back: " + info(window.farRow).plainText),
            // ---- Table cells: typed text is in the document before the cell is left -------------
            () => {
                page.stopEditing()
                window.textBefore = page.sourceText
                const row = viewRow("table")
                editor.listView.positionViewAtIndex(row, ListView.Center)
                const item = editor.listView.itemAtIndex(row) as BlockDelegate
                if (item)
                    item.focusContent()
            },
            () => typeKeys("źó"),
            () => check(page.sourceText !== window.textBefore && page.sourceText.indexOf("źó") > 0 && page.dirty,
                        "typing in a table cell reaches the document at once"),
            () => realKey(Qt.Key_Escape),
            () => check(page.sourceText === window.textBefore && page.selectedRow === viewRow("table"),
                        "Esc cancels the cell's edit: " + (page.sourceText === window.textBefore) + " " + page.selectedRow + "/" + viewRow("table")
                        + " canUndo=" + page.canUndo),
            // ---- A dialog acts on its block even when the page changed meanwhile ---------------
            () => {
                page.save()
                window.farRow = rowOf("Zadzwonić do hydraulika")
                editor.dialogs.createReminder(window.farRow, false)
            },
            () => host.externalWrite(page.path, host.file(page.path).replace("Plan remontu", "- Dopisane z telefonu\n\nPlan remontu")),
            () => editor.dialogs.reminderDialog.reminderAccepted({ at: "2026-10-16 08:30", alarm: false, recurrence: "" }),
            () => {
                const row = rowOf("Zadzwonić do hydraulika")
                const line = page.sourceText.split("\n").find(l => l.indexOf("Zadzwonić do hydraulika") >= 0) || ""
                check(row === window.farRow + 1 && line.indexOf("⏰ 2026-10-16 08:30") > 0
                      && page.sourceText.split("⏰ 2026-10-16 08:30").length === 2,
                      "the reminder lands on its block after a sync shifted the rows: " + line)
                editor.dialogs.reminderDialog.close()
            },
            () => { page.save() },
            () => check(!page.dirty && host.file(page.path).indexOf("[[Remont kuchni]]") > 0, "saved to the host"),
            // ---- Live query results: tasks from the vault, checkable in their own files ----------
            () => {
                const row = viewRow("query")
                editor.listView.positionViewAtIndex(row, ListView.Beginning)
            },
            () => {
                const item = editor.listView.itemAtIndex(viewRow("query")) as BlockDelegate
                const view = item ? item.contentItem as QueryBlockView : null
                const results = view ? view.results : null
                check(results !== null && results.available && results.ok && results.rowCount() >= 4,
                      "the query block lists its results: " + (results ? results.rowCount() : -1))
                let milk = -1
                for (let i = 0; results && i < results.rowCount(); ++i) {
                    if (results.get(i).text.indexOf("Mleko") === 0)
                        milk = i
                }
                check(milk >= 0 && results.toggleTask(milk) && host.file("Zakupy.md").indexOf("- [x] Mleko") === 0,
                      "a result's checkbox completes the task in its file: " + host.file("Zakupy.md").split("\n")[0])
            },
            () => {
                // Embedded section and block: frames with the source's blocks.
                const row = rowOf("Hydraulik z kontaktów")
                editor.listView.positionViewAtIndex(row, ListView.Center)
                const item = editor.listView.itemAtIndex(row) as BlockDelegate
                check(item !== null && item.transclusions.length === 1 && item.transclusions[0].found
                      && item.transclusionView.height > 20, "a block embed shows the block in a frame")
                const section = info(rowOf("Budżet na październik") + 1)
                check(section.view === "text", "the embedded section's block is a text block")
            }
        ]
        stepIndex = 0
        stepTimer.start()
    }

    function runScript(name) {
        switch (name) {
        case "selftest":
            selfTest()
            break
        case "editing": {
            const row = rowOf("Kupić płytki")
            editor.startEditing(row, -1)
            break
        }
        case "suggest": {
            const row = page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), "")
            typeInto(row, "Porównać z [[Re")
            break
        }
        case "tags": {
            const row = page.insertBlockAfter(rowOf("Wywieźć gruz"), "")
            typeInto(row, "Zamówić kontener #za")
            break
        }
        case "slash": {
            const row = page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), "")
            typeInto(row, "Zapytać o termin /")
            break
        }
        case "date": {
            const row = page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), "")
            typeInto(row, "Odebrać płytki ⏰ jutro o 9")
            break
        }
        case "zoom":
            page.zoomInto(rowOf("Przygotowanie"))
            break
        case "folded":
            page.setFolded(rowOf("Przygotowanie"), true)
            page.setFolded(rowOf("Prace"), true)
            break
        case "selected":
            page.selectedRow = rowOf("Położyć hydroizolację")
            break
        case "source":
            editor.toggleSourceMode()
            break
        case "reminder": {
            editor.dialogs.createReminder(rowOf("Zadzwonić do hydraulika"), false)
            break
        }
        case "recurrence":
            editor.dialogs.pickRecurrence(rowOf("Podlać kwiaty"))
            break
        case "table": {
            const row = viewRow("table")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            const item = editor.listView.itemAtIndex(row) as BlockDelegate
            if (item)
                item.focusContent()
            break
        }
        case "calc": {
            const row = viewRow("calc")
            editor.listView.positionViewAtIndex(row, ListView.Beginning)
            editor.startEditing(row, 0)
            break
        }
        case "query": {
            const row = viewRow("query")
            editor.listView.positionViewAtIndex(row, ListView.Beginning)
            break
        }
        case "embeds":
            editor.listView.positionViewAtIndex(rowOf("Powiązane"), ListView.Beginning)
            break
        case "title":
            break
        case "empty":
            page.appendEmptyBlock()
            break
        case "markup": {
            // Note text that looks like HTML is shown as written everywhere (block, breadcrumb,
            // query chips), never interpreted.
            const row = page.insertBlockAfter(rowOf("Ekipa może"), "Tekst z <b>HTML</b> & <img src=\"x.png\"> — to tylko tekst")
            const child = page.insertBlockAfter(row, "```query\ntag:<b>pilne</b> \"<i>płytki</i>\"\n```")
            page.indent(child)
            page.stopEditing()
            page.zoomInto(row)
            break
        }
        case "conflict":
            page.saveDelay = 100000
            page.setEditText(rowOf("Prace"), "Prace (lokalnie)", 16)
            host.externalWrite(page.path, host.file(page.path).replace("- Prace", "- Prace na telefonie"))
            break
        }
    }
}
