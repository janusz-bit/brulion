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
// table, calc, query, embeds, title, empty, conflict, markup, task-menu (a checkbox's status
// menu), task-menu-query (a query result's), task-slash (the status commands); the document
// mode on the sample trip plan (FakeEditorHost.documentPath): document (reading),
// document-paragraph, document-list, document-heading, document-task (editing one),
// document-outline (the same page switched to an outline); `selftest` runs keyboard/input-method checks (outline and document
// mode) and exits; `taskstatus` checks the task status menu and commands (taskStatusTest()) and
// exits. The in-memory vault runs ```query blocks on the real index (FakeEditorHost).
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

    // Self test: an editor that is destroyed while its deferred work is queued.
    property Item probe: null
    readonly property FakeEditorHost probeHost: host
    Component {
        id: probeComponent
        Item {
            readonly property BlockEditor editor: probeEditor
            width: 320
            height: 240
            PageController {
                id: probePage
                host: window.probeHost
                path: window.probeHost.samplePath
            }
            BlockEditor {
                id: probeEditor
                anchors.fill: parent
                controller: probePage
            }
        }
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
                  : DevTools.editorScript === "title" ? host.recipePath
                  : DevTools.editorScript.startsWith("document") ? host.documentPath : host.samplePath
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
                // The note mode (docs/UI.md "Note modes"): the icon shows the current one.
                iconName: page.documentMode ? "file-text" : "list"
                label: page.documentMode ? qsTr("Document mode") : qsTr("Outline mode")
                focusPolicy: Qt.NoFocus
                onClicked: page.toggleMode()
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

    /// Runs `fn` once, 400 ms later (a scripted state that waits for the list's layout).
    Timer {
        id: scriptLater
        property var fn: null
        interval: 400
        function run(f) {
            fn = f
            start()
        }
        onTriggered: if (fn) fn()
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
    property real headerBefore: 0
    property real tableBefore: 0
    /// The date picker's fields in the reminder / date dialog (typed dates and times).
    property BTextField dateField: null
    property BTextField timeField: null
    /// Repeats of a step waiting for something asynchronous (a picture loading).
    property int waits: 0
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
    /// The item named `name` under `item` (depth first), or null.
    function findItem(item, name) {
        if (!item)
            return null
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findItem(item.children[i], name)
            if (found)
                return found
        }
        return null
    }
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
            () => {
                check(editor.suggestions.visible && editor.suggestions.model.count > 0, "[[ opens page suggestions")
                // Rows built ahead asynchronously and a keystroke resetting the model: Qt warned
                // "DelegateModel::cancel: index out range" (tst_editor_suggestions).
                const list = findItem(editor.suggestions.contentItem, "suggestionList") as ListView
                check(list !== null && list.cacheBuffer === 0, "the suggestion list builds no rows ahead of its viewport")
            },
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
            // Zooming into a block and out again: the breadcrumb's room above the blocks goes
            // with it (an inactive Loader kept its height: a blank band above the first block).
            () => {
                window.headerBefore = editor.listView.headerItem ? editor.listView.headerItem.height : -1
                page.zoomInto(rowOf("Przygotowanie"))
            },
            () => check(page.zoomBlock >= 0 && editor.listView.headerItem
                        && editor.listView.headerItem.height > window.headerBefore,
                        "zooming in shows the breadcrumb above the blocks: " + window.headerBefore + " → "
                        + (editor.listView.headerItem ? editor.listView.headerItem.height : -1)),
            () => page.zoomOut(),
            () => check(page.zoomBlock < 0 && editor.listView.headerItem
                        && editor.listView.headerItem.height === window.headerBefore,
                        "zooming out leaves no room for the breadcrumb: "
                        + (editor.listView.headerItem ? editor.listView.headerItem.height : -1) + " (was " + window.headerBefore + ")"),
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
            // With a mouse, a table takes no room below its grid until it is selected or edited
            // (the "+ Row / + Column" buttons): its height never changes under a resting pointer.
            () => {
                page.stopEditing()
                page.selectedRow = -1
                window.textBefore = page.sourceText
                const row = viewRow("table")
                editor.listView.positionViewAtIndex(row, ListView.Center)
                const item = editor.listView.itemAtIndex(row) as BlockDelegate
                const table = item ? item.contentItem as TableBlockView : null
                window.tableBefore = table ? table.height : -1
                check(table !== null && table.height === (table.rows.length + 1) * table.rowHeight,
                      "a table not being edited has no room below its grid: " + window.tableBefore)
                if (item)
                    item.focusContent()
            },
            () => {
                const item = editor.listView.itemAtIndex(viewRow("table")) as BlockDelegate
                const table = item ? item.contentItem as TableBlockView : null
                check(table !== null && table.height > window.tableBefore + 20,
                      "editing a cell shows the table's buttons below it: " + (table ? table.height : -1))
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
            // ---- Dates and times typed in the dialogs count without their own Enter ------------
            // (Android's keyboard showed Next, a Tab, as the action key of these fields, so Enter
            // never came; a time typed but not confirmed was dropped on Save.)
            () => {
                window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "Odebrać paczkę z paczkomatu")
                page.stopEditing()
                editor.dialogs.createReminder(window.realRow, false)
                window.waits = 0
            },
            () => {
                const d = editor.dialogs.reminderDialog
                // Opening takes its transition (it then focuses the dialog): wait for it.
                if (!d.opened && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                window.dateField = findItem(d.contentItem, "datePickerNatural") as BTextField
                window.timeField = findItem(d.contentItem, "datePickerTime") as BTextField
                check(d.opened && window.dateField !== null && window.timeField !== null && d.at === "2026-10-16 09:00",
                      "the reminder dialog has its date and time fields: " + d.at)
                check(window.dateField !== null && window.dateField.EnterKey.type === Qt.EnterKeyDone
                      && window.timeField !== null && window.timeField.EnterKey.type === Qt.EnterKeyDone,
                      "the fields' keyboard action key is Done (Enter), not Next")
                window.dateField.forceActiveFocus()
            },
            () => typeKeys("za 2 minuty"),
            () => check(window.dateField.text === "za 2 minuty" && editor.dialogs.reminderDialog.at === "2026-10-16 09:00",
                        "a typed date waits for Enter: " + editor.dialogs.reminderDialog.at),
            () => realKey(Qt.Key_Return),
            () => check(editor.dialogs.reminderDialog.at === "2026-10-15 10:02" && window.dateField.text === "",
                        "Enter (the keyboard's Done) applies the typed date: " + editor.dialogs.reminderDialog.at),
            () => typeKeys("jutro 18:30"),
            () => window.timeField.forceActiveFocus(),
            () => check(editor.dialogs.reminderDialog.at === "2026-10-16 18:30" && window.dateField.text === "",
                        "leaving the field applies the typed date: " + editor.dialogs.reminderDialog.at),
            () => { window.timeField.selectAll(); typeKeys("2346") },
            () => check(window.timeField.activeFocus && editor.dialogs.reminderDialog.at === "2026-10-16 18:30",
                        "a typed time waits while its field has the focus"),
            // Save with the focus still in the field (Ctrl+Enter; a touch keyboard's Save tap).
            () => editor.dialogs.reminderDialog.tryAccept(),
            () => {
                const line = page.sourceText.split("\n").find(l => l.indexOf("Odebrać paczkę z paczkomatu") >= 0) || ""
                check(!editor.dialogs.reminderDialog.opened && line.indexOf("⏰ 2026-10-16 23:46") > 0,
                      "Save applies the typed time: " + line)
            },
            () => { editor.dialogs.createReminder(window.realRow, false); window.waits = 0 },
            () => {
                if (!editor.dialogs.reminderDialog.opened && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                window.timeField.forceActiveFocus()
                window.timeField.selectAll()
                typeKeys("25:99")
            },
            () => editor.dialogs.reminderDialog.tryAccept(),
            () => check(editor.dialogs.reminderDialog.opened && !editor.dialogs.reminderDialog.acceptEnabled
                        && window.timeField.text === "25:99", "a typed time that is none keeps the dialog open"),
            () => window.dateField.forceActiveFocus(),
            () => check(window.timeField.text === "25:99" && !editor.dialogs.reminderDialog.acceptEnabled,
                        "it stays in its field with the hint after leaving it"),
            () => { window.timeField.forceActiveFocus(); window.timeField.selectAll(); typeKeys("7") },
            () => realKey(Qt.Key_Return),
            () => check(editor.dialogs.reminderDialog.at === "2026-10-16 07:00" && editor.dialogs.reminderDialog.acceptEnabled,
                        "Enter in the time field applies a valid time: " + editor.dialogs.reminderDialog.at),
            () => editor.dialogs.reminderDialog.close(),
            // The due-date dialog: a typed date counts on Save.
            () => { editor.dialogs.pickDate(window.realRow, "due"); window.waits = 0 },
            () => {
                if (!editor.dialogs.dateDialog.opened && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                window.dateField = findItem(editor.dialogs.dateDialog.contentItem, "datePickerNatural") as BTextField
                if (window.dateField)
                    window.dateField.forceActiveFocus()
            },
            () => typeKeys("w sobotę"),
            () => editor.dialogs.dateDialog.tryAccept(),
            () => {
                const line = page.sourceText.split("\n").find(l => l.indexOf("Odebrać paczkę z paczkomatu") >= 0) || ""
                check(!editor.dialogs.dateDialog.opened && line.indexOf("📅 2026-10-17") > 0,
                      "the date dialog's Save applies the typed date: " + line)
            },
            () => editor.dialogs.pickRecurrence(window.realRow),
            () => {
                const field = editor.dialogs.recurrenceDialog.ruleField as BTextField
                check(field !== null && field.EnterKey.type === Qt.EnterKeyDone, "the recurrence rule's action key is Done")
                editor.dialogs.recurrenceDialog.close()
            },
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
            },
            // ---- Document mode (docs/UI.md "Note modes"): the trip plan reads as a document --------
            () => { page.save(); page.path = host.documentPath },
            () => {
                check(page.loaded && page.mode === "document" && editor.documentMode, "the document sample opens as a document")
                const para = rowOf("Pierwszego dnia")
                const item = editor.listView.itemAtIndex(para) as BlockDelegate
                check(item !== null && Math.abs(item.contentLeft - editor.contentX) < 0.5 && editor.gutter === 0,
                      "paragraphs start at the text column, no outline gutter")
                const li = rowOf("Sobota")
                const nested = rowOf("Wejście od Palenicy")
                const liItem = editor.listView.itemAtIndex(li) as BlockDelegate
                const nestedItem = editor.listView.itemAtIndex(nested) as BlockDelegate
                check(liItem !== null && nestedItem !== null && liItem.contentLeft > editor.contentX
                      && nestedItem.contentLeft > liItem.contentLeft, "list items are indented per level")
                check(liItem !== null && liItem.topPad < item.topPad, "list items sit closer together than paragraphs")
                const h = rowOf("Plan")
                const hItem = editor.listView.itemAtIndex(h) as BlockDelegate
                check(hItem !== null && hItem.topPad > item.topPad && editor.headingSize(2) > editor.fontSize,
                      "headings are larger with air above them")
                check(info(rowOf("Pociąg")).listMarker === "1.", "numbered items keep their numbers")
            },
            () => editor.startEditing(rowOf("Niedziela"), -1),
            () => realKey(Qt.Key_Return),
            () => check(page.editingRow === rowOf("Niedziela") + 1 && info(page.editingRow).kind === "listItem",
                        "Enter in a list item adds a list item"),
            () => typeKeys("Powrót ąę"),
            () => realKey(Qt.Key_Return),
            () => { window.realRow = page.editingRow },
            () => realKey(Qt.Key_Return),
            () => check(page.editingRow === window.realRow && info(window.realRow).kind === "paragraph",
                        "Enter on an empty list item leaves the list: " + info(window.realRow).kind),
            () => typeKeys("Akapit po liście"),
            () => realKey(Qt.Key_Tab),
            () => check(info(window.realRow).kind === "paragraph" && info(window.realRow).depth === 0
                        && editor.activeEditor && editor.activeEditor.activeFocus, "Tab in a paragraph does nothing"),
            () => editor.startEditing(rowOf("Powrót ąę"), 0),
            () => realKey(Qt.Key_Backspace),
            () => check(info(rowOf("Powrót ąę")).kind === "paragraph" && page.editingRow === rowOf("Powrót ąę"),
                        "Backspace at the start of a list item removes its marker"),
            () => realKey(Qt.Key_Z, Qt.ControlModifier),
            () => check(info(rowOf("Powrót ąę")).kind === "listItem", "Ctrl+Z brings the list item back"),
            () => editor.startEditing(rowOf("Z powrotem bryczką"), 0),
            () => realKey(Qt.Key_Tab),
            () => check(info(rowOf("Z powrotem bryczką")).depth === 2, "Tab indents a list item"),
            () => realKey(Qt.Key_Backtab),
            () => check(info(rowOf("Z powrotem bryczką")).depth === 1, "Shift+Tab outdents it"),
            () => editor.startEditing(rowOf("Pierwszego dnia"), 0),
            () => realKey(Qt.Key_Up),
            () => check(page.editingRow === rowOf("Plan"), "Up goes to the previous block like in a text"),
            () => realKey(Qt.Key_Down),
            () => check(page.editingRow === rowOf("Pierwszego dnia"), "Down comes back"),
            () => { page.stopEditing(); page.toggleMode() },
            () => check(page.mode === "outline" && editor.gutter > 0 && host.file(page.path).indexOf("brulion: outline") > 0,
                        "the header's toggle switches to an outline and writes only the key"),
            () => page.toggleMode(),
            () => check(page.mode === "document" && host.file(page.path).indexOf("brulion: document") > 0
                        && host.file(page.path).indexOf("Akapit po liście") > 0, "and back to a document"),
            // A paragraph of nothing but a picture shows just the picture (no empty text line above
            // it); an SVG keeps its aspect ratio (it was stretched to a square); a click beside the
            // picture edits the line.
            () => {
                host.setTextAsset("assets/trasa.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"100\""
                                  + " viewBox=\"0 0 400 100\"><rect width=\"400\" height=\"100\" fill=\"#9fc3e6\"/></svg>")
                window.realRow = page.insertBlockAfter(rowOf("Akapit po liście"), "![[trasa.svg]]")
                page.stopEditing()
                window.waits = 0
            },
            () => editor.listView.positionViewAtIndex(window.realRow, ListView.Center),
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const w = item ? item.embedView.width : 0
                const h = item ? item.embedView.height : 0
                // The picture loads asynchronously: this step repeats until it has (or 6 s passed).
                if ((!item || Math.abs(h - w / 4) > 2) && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                check(item !== null && Math.abs(h - w / 4) <= 2, "an SVG picture keeps its aspect ratio: " + w + "×" + h)
                check(item !== null && item.onlyPictures && item.contentHeight === 0 && item.embedsY === item.topPad,
                      "a picture-only paragraph has no empty text line above the picture")
                if (item)
                    item.embedView.backgroundClicked()
            },
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                check(page.editingRow === window.realRow && item !== null && !item.onlyPictures && item.contentHeight > 0
                      && editor.activeEditor && editor.activeEditor.text === "![[trasa.svg]]",
                      "a click beside the picture edits its line")
                page.stopEditing()
            },
            // A picture replaced in its file by one of another shape (the same URL: Qt's pixmap
            // cache may still hold the old picture) gets a frame of the shape it is drawn with —
            // the file's header (the frame's size before loading) must not stretch it.
            () => {
                host.overwriteTextAsset("assets/trasa.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\""
                                        + " height=\"400\" viewBox=\"0 0 100 400\"><!-- wysoka --><rect width=\"100\""
                                        + " height=\"400\" fill=\"#e6c39f\"/></svg>")
                window.waits = 0
            },
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const frame = item ? window.findItem(item.embedView, "embedPicture") : null
                const img = item ? window.findItem(item.embedView, "embedImage") as Image : null
                const ready = frame !== null && img !== null && img.status === Image.Ready && frame.height > 0
                              && img.implicitHeight > 0
                if (!ready && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                const shown = ready ? img.implicitWidth / img.implicitHeight : -1
                const box = ready ? frame.width / frame.height : -2
                check(ready && Math.abs(box / shown - 1) < 0.03,
                      "a replaced picture is not stretched: frame " + (frame ? frame.width + "×" + frame.height : "-")
                      + ", picture " + (img ? img.implicitWidth + "×" + img.implicitHeight : "-"))
            },
            // The journal stream caps document headings at the size of a day's date: smaller, in
            // the same order.
            () => {
                const plain = [1, 2, 3, 4].map(l => editor.headingSize(l))
                editor.headingCap = editor.fontSize + 4
                const capped = [1, 2, 3, 4].map(l => editor.headingSize(l))
                editor.headingCap = 0
                check(plain[0] === Math.round(editor.fontSize * 1.75) && capped[0] === editor.fontSize + 4
                      && capped[1] < capped[0] && capped[2] <= capped[1] && capped[3] <= capped[2]
                      && capped[3] >= editor.fontSize && capped.every((s, i) => s <= plain[i]),
                      "a heading cap shrinks the document's headings, keeping their order: " + plain + " → " + capped)
            },
            // A frame right under a heading gets a little more air than text there (its edge sat
            // a few pixels under the heading's descenders).
            () => editor.listView.positionViewAtIndex(rowOf("Pierwszego dnia"), ListView.Center),
            () => {
                const para = editor.listView.itemAtIndex(rowOf("Pierwszego dnia")) as BlockDelegate
                check(para !== null && para.topPad === editor.headingGapBelow, "a paragraph sits close under its heading")
                window.countBefore = page.blocks.count
                window.realRow = page.insertMarkdown(rowOf("Plan"), "```\nmapa-trasy.gpx\n```")
                page.stopEditing()
            },
            () => editor.listView.positionViewAtIndex(window.realRow, ListView.Center),
            () => {
                const code = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const para = editor.listView.itemAtIndex(rowOf("Pierwszego dnia")) as BlockDelegate
                check(code !== null && code.view === "code" && code.prevKind === "heading"
                      && code.topPad === editor.headingGapBelow + Theme.space2,
                      "a code block under a heading gets more air than text: " + (code ? code.topPad : -1))
                check(para !== null && para.topPad === editor.paragraphGap, "the paragraph after the frame keeps a paragraph gap")
                page.undo()
            },
            () => check(page.blocks.count === window.countBefore && info(rowOf("Pierwszego dnia") - 1).view === "heading",
                        "undo takes the code block out again"),
            // The touch toolbar of a document leads with the writing tools (list, task, [[, #, /),
            // indentation after them (off in a paragraph); an outline keeps indentation first.
            () => { Theme.forceTouch = true; editor.startEditing(rowOf("Pierwszego dnia"), 0); window.waits = 0 },
            () => {
                const list = window.findItem(editor, "toolbarList")
                const writing = window.findItem(editor, "toolbarWritingDocument")
                const structure = window.findItem(editor, "toolbarStructure")
                const ordered = editor.toolbarVisible && list !== null && writing !== null && structure !== null
                      && list.visible && writing.visible && list.x < writing.x && writing.x < structure.x
                // The toolbar is laid out asynchronously (slow under sanitizers): repeat this step
                // until it is (or 6 s passed).
                if (!ordered && window.waits++ < 50) {
                    window.stepIndex -= 1
                    return
                }
                check(ordered, "document toolbar: list and writing tools first, indentation after them")
                check(structure !== null && !structure.children[0].enabled && !structure.children[1].enabled,
                      "document toolbar: no indentation for a paragraph")
            },
            () => editor.startEditing(rowOf("Sobota"), 0),
            () => {
                const structure = window.findItem(editor, "toolbarStructure")
                check(structure !== null && structure.children[0].enabled && structure.children[1].enabled,
                      "document toolbar: list items indent")
                page.stopEditing()
                page.setMode("outline")
            },
            () => editor.startEditing(rowOf("Pierwszego dnia"), 0),
            () => {
                const list = window.findItem(editor, "toolbarList")
                const writing = window.findItem(editor, "toolbarWriting")
                const structure = window.findItem(editor, "toolbarStructure")
                check(page.mode === "outline" && list !== null && !list.visible && writing !== null && structure !== null
                      && writing.visible && structure.x < writing.x && structure.children[0].enabled,
                      "outline toolbar: indentation first, no list button")
                page.stopEditing()
                page.setMode("document")
                Theme.forceTouch = false
            },
            // ---- An editor destroyed before its deferred work runs (scrolling to a row, focus
            // loss, completions, scrolling to the top after loading): nothing touches it then.
            // A TypeError in the output fails the ctest (tests/app/editor/CMakeLists.txt). -------
            () => { window.probe = probeComponent.createObject(window) },
            () => {
                const ed = window.probe ? window.probe.editor : null
                check(ed !== null && ed.controller !== null && ed.controller.loaded, "the probe editor is loaded")
                if (!ed)
                    return
                window.probe.destroy() // gone when the event loop runs, before the work below
                ed.revealRow(1)
                ed.ensureRowVisible(1)
                ed.editorFocusLost()
                ed.handleCommandResult(1, { action: "complete" })
                ed.controller.load()
            },
            () => check(window.probe === null, "the probe editor is gone"),
            () => check(page.loaded && editor.alive, "the page's own editor still works")
        ]
        stepIndex = 0
        stepTimer.start()
    }

    // ---- Self test of task statuses (`--editor-script taskstatus`, tst_editor_qml_taskstatus*) ---
    // The status menu with real input — a right-click (mouse) or a long press (finger, --touch) —
    // on the checkboxes of the editor, a query result and an embedded section, a menu item clicked
    // or tapped; a plain click / tap still toggles; Ctrl+Shift+Enter while editing and on a
    // selected block (menu keys, Escape, the caret kept); the "/" commands typed with real keys
    // (list item, paragraph, Logseq keyword, undo); the touch toolbar's task button held down.
    // QtTest's TestEvent sends the pointer events through the window like a real device.
    property var taskEvents: null
    property int taskRow: -1
    property string taskText: ""
    property TaskStatusMenu ownMenu: null
    /// A row's TaskBox (the editor's, objectName "taskBox"), or null.
    function blockTaskBox(row) {
        const item = editor.listView.itemAtIndex(row)
        return item ? findItem(item, "taskBox") : null
    }
    /// Every item named `name` under `item`.
    function findAll(item, name, out) {
        out = out || []
        if (!item)
            return out
        if (item.objectName === name)
            out.push(item)
        for (let i = 0; i < item.children.length; ++i)
            findAll(item.children[i], name, out)
        return out
    }
    /// A query result's TaskBox for the task whose text starts with `prefix`.
    function resultTaskBox(prefix) {
        const item = editor.listView.itemAtIndex(viewRow("query"))
        const boxes = findAll(item, "queryTaskBox")
        for (const box of boxes) {
            const resultRow = box.parent ? box.parent.parent : null
            if (resultRow && String(resultRow.text).indexOf(prefix) === 0)
                return box
        }
        return null
    }
    function menuIndexOf(menu, name) {
        for (let i = 0; menu && i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.objectName === name)
                return i
        }
        return -1
    }
    function menuItemNamed(menu, name) {
        for (let i = 0; menu && i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.objectName === name)
                return item
        }
        return null
    }
    /// A tap with a finger on touch UIs, else a left click, in the middle of `item`.
    function tapItem(item) {
        if (!item)
            return
        const x = item.width / 2
        const y = item.height / 2
        if (Theme.touchUi) {
            taskEvents.touchEvent(item).press(0, item, x, y).commit()
            taskEvents.touchEvent(item).release(0, item, x, y).commit()
        } else {
            taskEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    /// The steps of the context-menu gesture on `target()`: a right-click, or on touch UIs a finger
    /// held down for a second (TaskBox's press-and-hold) and lifted.
    function menuGesture(target) {
        if (!Theme.touchUi) {
            return [() => {
                const item = target()
                check(item !== null, "the checkbox is there for the right-click")
                if (item)
                    taskEvents.mouseClick(item, item.width / 2, item.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            }]
        }
        const steps = [() => {
            const item = target()
            check(item !== null, "the checkbox is there for the long press")
            if (item)
                taskEvents.touchEvent(item).press(0, item, item.width / 2, item.height / 2).commit()
        }]
        for (let i = 0; i < 9; ++i)
            steps.push(() => {})
        steps.push(() => {
            const item = target()
            if (item)
                taskEvents.touchEvent(item).release(0, item, item.width / 2, item.height / 2).commit()
        })
        return steps
    }
    /// Steps that wait until `ready()` (at most ~3 s), then go on. `what`: the check's text, or a
    /// function giving it (read when the step runs).
    function waitUntil(ready, what) {
        return () => {
            if (!ready() && window.waits++ < 25) {
                window.stepIndex -= 1
                return
            }
            window.waits = 0
            check(ready(), typeof what === "function" ? what() : what)
        }
    }
    /// The page's text as saved.
    function pageText() {
        page.save()
        return host.file(page.path)
    }
    /// The shown TaskBox of the embedded section under the block "Budżet na październik".
    function embedTaskBox() {
        const boxes = findAll(editor.listView.itemAtIndex(rowOf("Budżet na październik") + 1), "embedTaskBox")
        for (const box of boxes) {
            if (box.visible)
                return box
        }
        return null
    }
    function fileLine(path, prefix) {
        const lines = host.file(path).split("\n")
        for (const line of lines) {
            if (line.indexOf(prefix) >= 0)
                return line
        }
        return ""
    }

    function taskStatusTest() {
        taskEvents = Qt.createQmlObject('import QtTest; TestEvent {}', window, "taskEvents")
        const gesture = Theme.touchUi ? "a long press" : "a right-click"
        const tap = Theme.touchUi ? "a tap" : "a click"
        steps = [
            () => {
                window.taskRow = rowOf("Kupić płytki")
                editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
            },
            () => check(info(window.taskRow).taskStatus === "todo" && blockTaskBox(window.taskRow) !== null,
                        "the block shows its checkbox"),
            // ---- The editor's checkbox: the menu, a status, undo, a plain click, no status ---------
            ...menuGesture(() => blockTaskBox(window.taskRow)),
            waitUntil(() => editor.taskMenu !== null && editor.taskMenu.opened, gesture + " on a checkbox opens the status menu"),
            () => {
                const menu = editor.taskMenu
                check(info(window.taskRow).taskStatus === "todo", gesture + " does not toggle the task")
                const todo = menuItemNamed(menu, "taskStatus_todo")
                const doing = menuItemNamed(menu, "taskStatus_doing")
                check(todo !== null && todo.checked && doing !== null && !doing.checked
                      && menuItemNamed(menu, "taskStatus_none") !== null, "the current status is checked")
                tapItem(doing)
            },
            waitUntil(() => !editor.taskMenu.visible, "choosing a status closes the menu"),
            () => {
                check(info(window.taskRow).taskStatus === "doing" && pageText().indexOf("\t- [/] Kupić płytki 📅 2026-10-16") > 0,
                      "the chosen status is written: " + fileLineOf(pageText(), "Kupić płytki"))
                page.undo()
            },
            () => check(info(window.taskRow).taskStatus === "todo", "one undo step puts the status back"),
            () => tapItem(blockTaskBox(window.taskRow)),
            () => {
                check(info(window.taskRow).taskStatus === "done" && !editor.taskMenu.visible,
                      tap + " on the checkbox still toggles, without a menu")
                page.undo()
            },
            ...menuGesture(() => blockTaskBox(window.taskRow)),
            waitUntil(() => editor.taskMenu.opened, "the menu opens again"),
            () => {
                const doing = menuItemNamed(editor.taskMenu, "taskStatus_doing")
                check(!doing.checked && menuItemNamed(editor.taskMenu, "taskStatus_todo").checked,
                      "the check marks follow the status (not the last click)")
                tapItem(menuItemNamed(editor.taskMenu, "taskStatus_none"))
            },
            waitUntil(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "",
                      "“No status” removes the checkbox"),
            () => {
                check(info(window.taskRow).kind === "listItem" && info(window.taskRow).plainText.indexOf("Kupić płytki") === 0
                      && pageText().indexOf("\t- Kupić płytki 📅 2026-10-16") > 0, "the text stays a list item: "
                      + fileLineOf(pageText(), "Kupić płytki"))
                check(blockTaskBox(window.taskRow) === null, "the checkbox is gone")
                page.undo()
            },
            () => check(info(window.taskRow).taskStatus === "todo", "undo brings the checkbox back"),
            // ---- Ctrl+Shift+Enter while editing: the menu at the caret, keys, the caret kept -------
            () => {
                window.taskRow = rowOf("Wywieźć gruz")
                editor.startEditing(window.taskRow, 3)
            },
            () => {
                window.taskText = editor.activeEditor.plain()
                realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
            },
            waitUntil(() => editor.taskMenu.opened, "Ctrl+Shift+Enter opens the status menu of the edited block"),
            () => {
                check(info(window.taskRow).taskStatus === "todo", "Ctrl+Shift+Enter does not cycle the status")
                taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
            },
            waitUntil(() => !editor.taskMenu.visible && editor.activeEditor !== null && editor.activeEditor.activeFocus,
                      "Escape closes the menu and the block has the keyboard again"),
            () => {
                check(editor.activeEditor.row === window.taskRow && editor.activeEditor.cursorPosition === 3
                      && info(window.taskRow).taskStatus === "todo", "nothing changed, the caret is where it was")
                realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
                window.waits = 0
            },
            waitUntil(() => editor.taskMenu.opened, "the menu opens again"),
            () => {
                // Down until "Oczekujące" is highlighted, then Enter.
                const target = menuIndexOf(editor.taskMenu, "taskStatus_deferred")
                if (editor.taskMenu.currentIndex !== target && window.waits++ < 12) {
                    taskEvents.keyClick(Qt.Key_Down, Qt.NoModifier, -1)
                    window.stepIndex -= 1
                    return
                }
                window.waits = 0
                check(editor.taskMenu.currentIndex === target, "Down moves through the statuses: "
                      + editor.taskMenu.currentIndex + " (" + target + ")")
                taskEvents.keyClick(Qt.Key_Return, Qt.NoModifier, -1)
            },
            waitUntil(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "deferred",
                      "Enter on “Oczekujące” sets the status"),
            waitUntil(() => editor.activeEditor !== null && editor.activeEditor.activeFocus
                      && editor.activeEditor.row === window.taskRow, "the edited block gets the keyboard back"),
            () => {
                check(editor.activeEditor.cursorPosition === 3 && editor.activeEditor.plain() === window.taskText,
                      "the text and the caret are kept: " + editor.activeEditor.cursorPosition)
                check(pageText().indexOf("\t- [>] Wywieźć gruz ⏳ 2026-10-18") > 0, "written as [>]")
                realKey(Qt.Key_Return, Qt.ControlModifier)
            },
            () => check(info(window.taskRow).taskStatus !== "deferred" && !editor.taskMenu.visible,
                        "Ctrl+Enter still cycles, without the menu: " + info(window.taskRow).taskStatus),
            // A selected block (Esc): Ctrl+Shift+Enter opens its menu too, the selection keeps the keys.
            () => { realKey(Qt.Key_Escape) },
            () => {
                check(page.selectedRow === window.taskRow, "Esc selects the block")
                EditorInput.sendKey(editor.listView, Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
            },
            waitUntil(() => editor.taskMenu.opened, "Ctrl+Shift+Enter on a selected block opens its menu"),
            () => tapItem(menuItemNamed(editor.taskMenu, "taskStatus_cancelled")),
            waitUntil(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "cancelled",
                      "a status for the selected block"),
            waitUntil(() => editor.listView.activeFocus && page.selectedRow === window.taskRow,
                      "the selected block keeps the keyboard"),
            () => { page.selectedRow = -1 },
            // ---- "/" commands typed with real keys -----------------------------------------------
            () => { window.taskRow = page.insertBlockAfter(rowOf("Ekipa może"), "") },
            () => typeKeys("Zadzwonić do Żanety /czeka"),
            waitUntil(() => editor.suggestionsOpen && editor.suggestions.model.count > 0
                      && editor.suggestions.model.get(0).command === "taskWaiting",
                      "“/czeka” offers “Zadanie: oczekujące” first"),
            () => realKey(Qt.Key_Return),
            () => {
                check(info(window.taskRow).taskStatus === "deferred" && editor.activeEditor.plain() === "Zadzwonić do Żanety "
                      && editor.activeEditor.cursorPosition === 20 && !editor.suggestionsOpen,
                      "the command sets the status, its text goes, the caret stays: " + editor.activeEditor.plain())
                page.undo()
            },
            () => check(editor.activeEditor.plain() === "Zadzwonić do Żanety /czeka" && info(window.taskRow).taskStatus === "",
                        "one undo step brings the command text back: " + editor.activeEditor.plain()),
            () => {
                const e = editor.activeEditor
                e.cursorPosition = e.length
                realKey(Qt.Key_Backspace)
                realKey(Qt.Key_Backspace)
                realKey(Qt.Key_Backspace)
                realKey(Qt.Key_Backspace)
                realKey(Qt.Key_Backspace)
                typeKeys("zrob")
            },
            waitUntil(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "task",
                      "“/zrob” offers the statuses (to do first)"),
            () => {
                // Down to "Zadanie: zrobione", Enter.
                let done = -1
                for (let i = 0; i < editor.suggestions.model.count; ++i) {
                    if (editor.suggestions.model.get(i).command === "taskDone")
                        done = i
                }
                check(done > 0, "“Zadanie: zrobione” is listed")
                for (let i = 0; i < done; ++i)
                    realKey(Qt.Key_Down)
                realKey(Qt.Key_Return)
            },
            () => check(info(window.taskRow).taskStatus === "done" && pageText().indexOf("- [x] Zadzwonić do Żanety ✅ 2026-10-15") > 0,
                        "“Zadanie: zrobione” completes the task: " + fileLineOf(pageText(), "Żanety")),
            () => typeKeys(" /bez"),
            waitUntil(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "taskNone",
                      "“/bez” offers “Zadanie: bez statusu”"),
            () => realKey(Qt.Key_Return),
            () => check(info(window.taskRow).taskStatus === "" && fileLineOf(pageText(), "Żanety").indexOf("\t- Zadzwonić do Żanety") === 0,
                        "“Zadanie: bez statusu” removes the checkbox and its date: " + fileLineOf(pageText(), "Żanety")),
            // A paragraph becomes a task; a Logseq keyword keeps its workflow.
            () => {
                page.stopEditing()
                window.taskRow = page.insertMarkdown(page.lastVisibleRow(), "Akapit o płytkach")
            },
            () => editor.startEditing(rowOf("Akapit o płytkach"), -1),
            () => typeKeys(" /zadanie"),
            waitUntil(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "task",
                      "“/zadanie” offers “Zadanie: do zrobienia”"),
            () => realKey(Qt.Key_Return),
            () => {
                const r = rowOf("Akapit o płytkach")
                check(r >= 0 && info(r).taskStatus === "todo" && info(r).kind === "listItem"
                      && pageText().indexOf("- [ ] Akapit o płytkach") >= 0, "a paragraph becomes a task")
                page.stopEditing()
                window.taskRow = page.insertBlockAfter(rowOf("Ekipa może"), "TODO Odebrać kafelki")
            },
            () => check(info(window.taskRow).taskStatus === "todo", "a Logseq TODO is a task"),
            () => typeKeys(" /toku"),
            waitUntil(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "taskDoing",
                      "“/toku” offers “Zadanie: w toku”"),
            () => realKey(Qt.Key_Return),
            () => check(info(window.taskRow).taskStatus === "doing" && pageText().indexOf("- DOING Odebrać kafelki") > 0,
                        "the Logseq keyword follows: " + fileLineOf(pageText(), "kafelki")),
            () => page.stopEditing(),
            // ---- A query result's checkbox: its own menu, the task in its own file ----------------
            () => editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning),
            waitUntil(() => resultTaskBox("Mleko") !== null && resultTaskBox("Śrubki") !== null,
                      "the query block lists the shopping tasks"),
            ...menuGesture(() => resultTaskBox("Mleko")),
            waitUntil(() => resultTaskBox("Mleko") !== null && resultTaskBox("Mleko").statusMenu !== null
                      && resultTaskBox("Mleko").statusMenu.opened, gesture + " on a query result's checkbox opens its menu"),
            () => {
                window.ownMenu = resultTaskBox("Mleko").statusMenu
                check(host.file("Zakupy.md").indexOf("- [ ] Mleko") === 0, gesture + " does not toggle the result")
                check(menuItemNamed(window.ownMenu, "taskStatus_todo").checked, "its status is checked")
                tapItem(menuItemNamed(window.ownMenu, "taskStatus_deferred"))
            },
            waitUntil(() => host.file("Zakupy.md").indexOf("- [>] Mleko #zakupy") === 0,
                      () => "a result's status is written in its own file: " + host.file("Zakupy.md").split("\n")[0]),
            ...menuGesture(() => resultTaskBox("Śrubki")),
            waitUntil(() => resultTaskBox("Śrubki") !== null && resultTaskBox("Śrubki").statusMenu !== null
                      && resultTaskBox("Śrubki").statusMenu.opened, "the next result's menu opens"),
            () => tapItem(menuItemNamed(resultTaskBox("Śrubki").statusMenu, "taskStatus_none")),
            waitUntil(() => fileLine("Zakupy.md", "Śrubki") === "- Śrubki do półki #zakupy",
                      () => "“No status” on a result removes the task's checkbox in its file: " + fileLine("Zakupy.md", "Śrubki")),
            waitUntil(() => resultTaskBox("Śrubki") === null, "the result is no task any more"),
            // ---- An embedded section's task (Budżet domowy#Październik) --------------------------
            () => editor.listView.positionViewAtIndex(rowOf("Budżet na październik") + 1, ListView.Center),
            waitUntil(() => embedTaskBox() !== null, "the embedded section shows its task's checkbox"),
            ...menuGesture(() => embedTaskBox()),
            waitUntil(() => embedTaskBox() !== null && embedTaskBox().statusMenu !== null && embedTaskBox().statusMenu.opened,
                      gesture + " on an embedded task opens its menu"),
            () => tapItem(menuItemNamed(embedTaskBox().statusMenu, "taskStatus_done")),
            waitUntil(() => fileLine("Budżet domowy.md", "czynsz") === "- [x] Opłacić czynsz 📅 2026-10-20 #finanse ✅ 2026-10-15",
                      () => "an embedded task's status is written in its own file: " + fileLine("Budżet domowy.md", "czynsz")),
            // ---- The touch toolbar: the task button held down opens the menu above it --------------
            () => {
                Theme.forceTouch = true
                window.taskRow = rowOf("Położyć hydroizolację")
                editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
                editor.startEditing(window.taskRow, -1)
            },
            waitUntil(() => {
                const writing = findItem(editor, "toolbarWriting")
                const button = writing ? findItem(writing, "toolbarTask") : null
                return editor.toolbarVisible && button !== null && button.width > 0
            }, "the touch toolbar shows its task button"),
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                taskEvents.touchEvent(button).press(0, button, button.width / 2, button.height / 2).commit()
            },
            () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {}, () => {},
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                taskEvents.touchEvent(button).release(0, button, button.width / 2, button.height / 2).commit()
            },
            waitUntil(() => editor.taskMenu.opened, "holding the toolbar's task button opens the status menu"),
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                const menuBottom = editor.taskMenu.contentItem.mapToItem(null, 0, editor.taskMenu.contentItem.height).y
                const buttonTop = button.mapToItem(null, 0, 0).y
                check(menuBottom <= buttonTop + 1, "the menu is above the toolbar: " + menuBottom + " ≤ " + buttonTop)
                check(info(window.taskRow).taskStatus === "todo", "holding the button does not cycle the status")
                tapItem(menuItemNamed(editor.taskMenu, "taskStatus_done"))
            },
            waitUntil(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "done",
                      "a status from the toolbar's menu"),
            () => tapItem(findItem(findItem(editor, "toolbarWriting"), "toolbarTask")),
            () => check(info(window.taskRow).taskStatus === "", "a tap on the task button cycles (done → none)"),
            () => {
                page.stopEditing()
                Theme.forceTouch = DevTools.touch
            }
        ]
        stepIndex = 0
        stepTimer.start()
    }
    function fileLineOf(text, needle) {
        const lines = text.split("\n")
        for (const line of lines) {
            if (line.indexOf(needle) >= 0)
                return line
        }
        return ""
    }

    function runScript(name) {
        switch (name) {
        case "selftest":
            selfTest()
            break
        case "taskstatus":
            taskStatusTest()
            break
        case "task-menu": {
            // The status menu of a block's checkbox (screenshots).
            const row = rowOf("Kupić płytki")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            scriptLater.run(() => {
                const box = blockTaskBox(row)
                if (box)
                    editor.openTaskStatusMenu(row, box, 0, box.height)
            })
            break
        }
        case "task-menu-query":
            // The status menu of a query result's checkbox (screenshots).
            editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning)
            scriptLater.run(() => {
                const box = resultTaskBox("Mleko")
                if (box)
                    box.openMenu()
            })
            break
        case "task-slash": {
            const row = page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), "")
            typeInto(row, "Odebrać płytki /zad")
            break
        }
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
        case "document":
            break
        case "document-paragraph":
            editor.startEditing(rowOf("Pierwszego dnia"), 43)
            break
        case "document-list":
            editor.startEditing(rowOf("Wejście od Palenicy"), -1)
            break
        case "document-heading":
            editor.startEditing(rowOf("Do spakowania"), -1)
            break
        case "document-task":
            editor.startEditing(rowOf("Kurtka"), -1)
            break
        case "document-outline":
            page.setMode("outline")
            break
        case "conflict":
            page.saveDelay = 100000
            page.setEditText(rowOf("Prace"), "Prace (lokalnie)", 16)
            host.externalWrite(page.path, host.file(page.path).replace("- Prace", "- Prace na telefonie"))
            break
        }
    }
}
