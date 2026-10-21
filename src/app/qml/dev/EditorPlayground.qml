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
// table, calc, query, embeds, title, empty, conflict, markup; the document mode on the sample
// trip plan (FakeEditorHost.documentPath): document (reading), document-paragraph,
// document-list, document-heading, document-task (editing one), document-outline (the same page
// switched to an outline); `selftest` runs keyboard/input-method checks (outline and document
// mode) and exits; `clock` checks the round clock and its buttons (clockTest()) and exits. The
// in-memory vault runs ```query blocks on the real index (FakeEditorHost).
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
    property var crumbsProbe: null
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
    /// The Text under `item` that shows `text`, or null.
    function textShowing(item, text) {
        if (!item)
            return null
        if (item.text === text && item.textFormat !== undefined)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = textShowing(item.children[i], text)
            if (found)
                return found
        }
        return null
    }
    /// The Text under `item` whose text contains `needle`, or null.
    function textContaining(item, needle) {
        if (!item || !item.visible)
            return null
        if (item.textFormat !== undefined && String(item.text).indexOf(needle) >= 0)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = textContaining(item.children[i], needle)
            if (found)
                return found
        }
        return null
    }
    /// `label` is as wide as `text` written out in plain text (markup not interpreted).
    function showsAsWritten(label, text) {
        if (!label)
            return false
        const plain = Qt.createQmlObject('import QtQuick; Text { textFormat: Text.PlainText }', window.contentItem, "plainText")
        plain.font = label.font
        plain.text = text
        const same = Math.abs(plain.implicitWidth - label.implicitWidth) < 1
        plain.destroy()
        return same
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
            () => check(page.loaded && editor.alive, "the page's own editor still works"),
            // ---- Names in menus and breadcrumbs are shown as written -------------------------------
            () => {
                const markup = "<font size=1>Żółw " + "ż".repeat(24) + "</font>.png"
                const item = Qt.createQmlObject('import QtQuick; import Brulion; BMenuItem {}', window.contentItem, "menuItemProbe")
                item.text = markup
                check(showsAsWritten(textShowing(item, markup), markup), "a menu item shows a name with markup as written")
                item.destroy()
                const crumbs = Qt.createQmlObject('import QtQuick; import Brulion; BBreadcrumb { width: 900 }', window.contentItem, "crumbsProbe")
                crumbs.model = [{ title: "Projekty" }, { title: markup }]
                window.crumbsProbe = crumbs
            },
            () => {
                check(showsAsWritten(textShowing(window.crumbsProbe, "<font size=1>Żółw " + "ż".repeat(24) + "</font>.png"),
                                     "<font size=1>Żółw " + "ż".repeat(24) + "</font>.png"),
                      "a breadcrumb shows a name with markup as written")
                window.crumbsProbe.destroy()
                page.save()
                page.path = host.samplePath
            },
            () => {
                // A block zoomed into: its text in the breadcrumb above the blocks, as written.
                window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "Tekst z <b>HTML</b> & <i>kursywą</i>")
                page.stopEditing()
                page.zoomInto(window.realRow)
            },
            () => {
                const label = textContaining(editor, "<b>HTML</b>")
                check(label !== null && showsAsWritten(label, label.text), "the zoom breadcrumb shows a block's markup as written: "
                      + (label ? label.text : "none"))
                page.zoomOut()
                page.undo()
            }
        ]
        stepIndex = 0
        stepTimer.start()
    }

    // ---- Self test of the round clock (`--editor-script clock`, tst_editor_qml_clock*) -------
    // BTimePicker on its own: the value of every position, taps and drags with a finger (touch
    // UI) or the mouse — also drags that start on a number or the chosen value's bubble —, keys
    // and the focus ring; BTimePickerDialog: open, set, cancel, the whole dial in a short window;
    // the clock buttons of the reminder's date picker and of the calendar's add dialog (a typed
    // time first; the clock created only when asked for). Real input through QtTest's TestEvent.
    property var clockEvents: null
    property BTimePicker clock: null
    property BTimePickerDialog clockDialog: null
    property var clockPicks: []
    property var dialogPicks: []
    property var datePicker: null
    property var addDialog: null
    property var reminderDialog: null

    /// A point of the face (its coordinates) at `radius` from the centre, `deg` from 12 o'clock.
    function clockPoint(c, radius, deg) {
        const a = (deg - 90) * Math.PI / 180
        return { x: c.center + Math.cos(a) * radius, y: c.center + Math.sin(a) * radius }
    }
    /// A tap (a finger on touch UIs, else a click) at (x, y) of `item`.
    function tapAt(item, x, y) {
        if (Theme.touchUi) {
            clockEvents.touchEvent(item).press(0, item, x, y).commit()
            clockEvents.touchEvent(item).release(0, item, x, y).commit()
        } else {
            clockEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function clockTap(c, radius, deg) {
        const p = clockPoint(c, radius, deg)
        tapAt(c.focusItem, p.x, p.y)
    }
    /// A drag along the circle of `radius` from `fromDeg` to `toDeg`, in twelve moves.
    function clockDrag(c, radius, fromDeg, toDeg) {
        const face = c.focusItem
        const touch = Theme.touchUi
        let p = clockPoint(c, radius, fromDeg)
        if (touch)
            clockEvents.touchEvent(face).press(0, face, p.x, p.y).commit()
        else
            clockEvents.mousePress(face, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
        for (let i = 1; i <= 12; ++i) {
            p = clockPoint(c, radius, fromDeg + (toDeg - fromDeg) * i / 12)
            if (touch)
                clockEvents.touchEvent(face).move(0, face, p.x, p.y).commit()
            else
                clockEvents.mouseMove(face, p.x, p.y, -1, Qt.LeftButton, Qt.NoModifier)
        }
        if (touch)
            clockEvents.touchEvent(face).release(0, face, p.x, p.y).commit()
        else
            clockEvents.mouseRelease(face, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
    }
    function clockRing(c) {
        return findItem(c, "clockFocusRing")
    }
    /// Scroll a dialog's body so that `item` shows, then tap it.
    function tapInDialog(dialog, item) {
        const body = dialog.contentItem
        const at = item.mapToItem(body.contentItem, 0, 0)
        body.contentY = Math.max(0, Math.min(at.y - (body.height - item.height) / 2, body.contentHeight - body.height))
        tapAt(item, item.width / 2, item.height / 2)
    }
    /// The clock dialogs `object` holds (created ones: BDatePicker creates its clock on demand).
    function clockDialogsIn(object) {
        const found = []
        for (const list of [object.resources, object.data]) {
            for (let i = 0; list && i < list.length; ++i) {
                const o = list[i]
                if (o && o.openFor !== undefined && o.picked !== undefined && found.indexOf(o) < 0)
                    found.push(o)
            }
        }
        return found.length
    }
    /// Repeat the current step until `ready` (an asynchronous popup), at most 6 s.
    function waitUntil(ready) {
        if (ready || window.waits++ >= 50) {
            window.waits = 0
            return true
        }
        window.stepIndex -= 1
        return false
    }

    function clockTest() {
        clockEvents = Qt.createQmlObject('import QtTest; TestEvent {}', window, "clockEvents")
        clock = Qt.createQmlObject('import QtQuick; import Brulion; BTimePicker { x: 40; y: 40; z: 100000; time: "18:30" }',
                                   window.contentItem, "clock") as BTimePicker
        clock.picked.connect((v) => window.clockPicks.push(v))
        const c = clock
        steps = [
            () => check(c.focusItem.width === c.dialSize && c.focusItem.height === c.dialSize,
                        "the face is the dial's size: " + c.focusItem.width + "×" + c.focusItem.height + ", dial " + c.dialSize),
            // ---- The value of every position --------------------------------------------------
            () => {
                c.mode = "hour"
                const bad = []
                for (let h = 0; h < 24; ++h) {
                    c.time = "12:34"
                    const p = clockPoint(c, h < 12 ? c.outerRadius : c.innerRadius, (h % 12) * 30)
                    c.pickAt(p.x, p.y)
                    if (c.time !== c.pad(h) + ":34")
                        bad.push(h + "→" + c.time)
                }
                check(bad.length === 0, "every hour's number gives that hour (00–11 outside, 12–23 inside) " + bad)
            },
            () => {
                c.mode = "hour"
                let outerFrom = -1
                for (let r = 13; r < c.center && outerFrom < 0; ++r) {
                    c.time = "06:00"
                    const p = clockPoint(c, r, 90)
                    c.pickAt(p.x, p.y)
                    if (c.selectedHour === 3)
                        outerFrom = r
                }
                check(outerFrom > c.innerRadius && outerFrom < c.outerRadius,
                      "the outer ring starts between the rings: r " + outerFrom + " (inner " + c.innerRadius + ", outer " + c.outerRadius + ")")
            },
            () => {
                c.mode = "minute"
                const bad = []
                for (let m = 0; m < 60; ++m) {
                    for (const r of [20, c.innerRadius, c.outerRadius, c.center - 2]) {
                        c.time = "10:" + (m === 7 ? "08" : "07")
                        const p = clockPoint(c, r, m * 6)
                        c.pickAt(p.x, p.y)
                        if (c.selectedMinute !== m || c.selectedHour !== 10)
                            bad.push(m + "@" + Math.round(r) + "→" + c.time)
                    }
                }
                const at = (deg) => {
                    c.time = "10:30"
                    const p = clockPoint(c, c.outerRadius, deg)
                    c.pickAt(p.x, p.y)
                    return c.time
                }
                check(bad.length === 0 && at(359.9) === "10:00" && at(2.9) === "10:00" && at(3.1) === "10:01",
                      "every minute comes from its angle, at any distance from the centre " + bad)
            },
            () => {
                c.mode = "hour"
                c.time = "18:30"
                window.clockPicks = []
                const p = clockPoint(c, 5, 0)
                c.pickAt(p.x, p.y)
                check(c.time === "18:30" && window.clockPicks.length === 0, "the hub is no value")
            },
            // ---- Taps and drags (a finger on touch UIs, the mouse elsewhere) ------------------
            () => { c.time = "18:30"; c.mode = "hour"; clockTap(c, c.outerRadius, 7 * 30) },
            () => check(c.time === "07:30" && c.mode === "minute", "a tap on 07 (outer ring) picks it and moves on to the minutes: " + c.time + " " + c.mode),
            () => { c.mode = "hour"; clockTap(c, c.innerRadius, 9 * 30) },
            () => check(c.time === "21:30" && c.mode === "minute", "a tap on 21 (inner ring): " + c.time + " " + c.mode),
            () => clockTap(c, c.outerRadius, 37 * 6),
            () => check(c.time === "21:37", "a tap between the minute numbers gives that minute: " + c.time),
            () => clockTap(c, c.outerRadius - 4, 45 * 6 + 3),
            () => check(c.time === "21:45", "a tap a little off the number 45 gives 45: " + c.time),
            () => { c.time = "18:30"; c.mode = "hour"; clockTap(c, 4, 0) },
            () => check(c.time === "18:30" && c.mode === "hour", "a tap on the hub changes nothing: " + c.time + " " + c.mode),
            () => { c.time = "18:30"; c.mode = "minute"; window.clockPicks = []; clockDrag(c, c.outerRadius, 180, 270) },
            () => check(c.time === "18:45" && window.clockPicks.length > 1,
                        "dragging the minute hand by its bubble (on the number 30) to 45: " + c.time + " via " + window.clockPicks),
            () => { c.time = "18:30"; c.mode = "hour"; clockDrag(c, c.innerRadius, 180, 270) },
            () => check(c.time === "21:30" && c.mode === "minute",
                        "dragging the hour bubble (on 18) to 21, then on to the minutes: " + c.time + " " + c.mode),
            () => { c.time = "11:30"; c.mode = "hour"; clockDrag(c, c.outerRadius, 330, 390) },
            () => check(c.time === "01:30", "a drag from the number 11 across 12 o'clock to 01: " + c.time),
            () => { c.time = "18:30"; c.mode = "minute"; clockDrag(c, 60, 180, 270) },
            () => check(c.time === "18:45", "a drag inside the ring, off the numbers: " + c.time),
            () => { c.time = "18:32"; c.mode = "minute"; clockDrag(c, c.outerRadius, 32 * 6, 44 * 6) },
            () => check(c.time === "18:44", "a drag from between the numbers (32) to 44: " + c.time),
            // ---- Keys, and the focus ring for the keyboard only --------------------------------
            () => { c.mode = "hour"; c.time = "23:59"; c.focusItem.forceActiveFocus(Qt.MouseFocusReason) },
            () => check(c.focusItem.activeFocus && !clockRing(c).visible, "the face has the keyboard, without a focus ring after a tap"),
            () => EditorInput.sendKey(c.focusItem, Qt.Key_Up),
            () => check(c.time === "00:59" && clockRing(c).visible, "Up from 23 wraps to 00, and keys show the focus ring: " + c.time),
            () => EditorInput.sendKey(c.focusItem, Qt.Key_Down),
            () => check(c.time === "23:59", "Down from 00 wraps to 23: " + c.time),
            () => EditorInput.sendKey(c.focusItem, Qt.Key_PageUp),
            () => check(c.time === "04:59", "PageUp: five hours on: " + c.time),
            () => { EditorInput.sendKey(c.focusItem, Qt.Key_Right); EditorInput.sendKey(c.focusItem, Qt.Key_Up) },
            () => check(c.mode === "minute" && c.time === "04:00", "Right, then Up: minute 59 wraps to 00: " + c.time + " " + c.mode),
            () => EditorInput.sendKey(c.focusItem, Qt.Key_PageDown),
            () => check(c.time === "04:55", "PageDown: five minutes back: " + c.time),
            () => EditorInput.sendKey(c.focusItem, Qt.Key_End),
            () => check(c.time === "04:59", "End: the last minute: " + c.time),
            () => { EditorInput.sendKey(c.focusItem, Qt.Key_Left); EditorInput.sendKey(c.focusItem, Qt.Key_Home) },
            () => check(c.mode === "hour" && c.time === "00:59", "Left, then Home: hour 00: " + c.time + " " + c.mode),
            () => clockTap(c, c.outerRadius, 90),
            () => check(c.focusItem.activeFocus && !clockRing(c).visible && c.time === "03:59", "a tap hides the focus ring again: " + c.time),
            // ---- The dialog -----------------------------------------------------------------------
            () => {
                c.visible = false
                clockDialog = Qt.createQmlObject('import QtQuick; import Brulion; BTimePickerDialog {}', window.contentItem,
                                                 "clockDialog") as BTimePickerDialog
                clockDialog.picked.connect((v) => window.dialogPicks.push(v))
                clockDialog.openFor("7")
            },
            () => {
                if (!waitUntil(clockDialog.opened))
                    return
                const p = clockDialog.picker
                check(p.time === "07:00" && p.mode === "hour", "the dialog opens on 07:00 for \"7\", on the hours: " + p.time)
                check(p.focusItem.activeFocus && !clockRing(p).visible, "the dial has the keyboard when the dialog opens, without a focus ring")
                const body = clockDialog.contentItem
                check(body.contentHeight <= body.height + 1,
                      "the whole dial shows in the dialog in a " + window.width + "×" + window.height + " window: body " + body.height
                      + ", clock " + body.contentHeight + ", dial " + p.dialSize + (p.horizontal ? ", the fields beside it" : ""))
                check(p.dialSize >= 180 && 2 * Math.PI * p.innerRadius / 12 >= Theme.textSm * 1.6,
                      "the inner ring's numbers have room: dial " + p.dialSize + ", " + (2 * Math.PI * p.innerRadius / 12).toFixed(1)
                      + " px apart")
            },
            () => clockTap(clockDialog.picker, clockDialog.picker.innerRadius, 90),
            () => clockTap(clockDialog.picker, clockDialog.picker.outerRadius, 45 * 6),
            () => check(clockDialog.picker.time === "15:45" && window.dialogPicks.length === 0, "picked on the dial, not set yet: " + clockDialog.picker.time),
            () => {
                // A drag on the dial moves the hand and never scrolls the dialog.
                const body = clockDialog.contentItem
                const before = body.contentY
                clockDrag(clockDialog.picker, 60, 0, 170)
                check(clockDialog.picker.time === "15:28" && body.contentY === before,
                      "a drag down the dial in the dialog moves the hand, not the dialog: " + clockDialog.picker.time + ", scrolled " + (body.contentY - before))
                clockDialog.picker.time = "15:45"
            },
            () => clockDialog.tryAccept(),
            () => check(window.dialogPicks.join() === "15:45", "Set gives the time: " + window.dialogPicks),
            () => {
                if (waitUntil(!clockDialog.visible))
                    clockDialog.openFor("")
            },
            () => {
                if (!waitUntil(clockDialog.opened))
                    return
                check(clockDialog.picker.time === "10:00", "nothing given: the dialog opens on the current time: " + clockDialog.picker.time)
                clockTap(clockDialog.picker, clockDialog.picker.outerRadius, 30)
            },
            () => clockDialog.reject(),
            () => check(window.dialogPicks.join() === "15:45", "Cancel gives nothing: " + window.dialogPicks),
            () => {
                if (waitUntil(!clockDialog.visible))
                    clockDialog.openFor("25:00")
            },
            () => {
                if (!waitUntil(clockDialog.opened))
                    return
                check(clockDialog.picker.time === "10:00", "no time: the dialog opens on the current time: " + clockDialog.picker.time)
                clockDialog.reject()
            },
            // ---- The date picker's clock: created when asked for, after the typed time ---------
            () => {
                if (!waitUntil(!clockDialog.visible))
                    return
                const plain = Qt.createQmlObject('import QtQuick; import Brulion; BDatePicker { withTime: false }', window.contentItem, "plainDatePicker")
                check(clockDialogsIn(plain) === 0, "a date picker without a time creates no clock")
                plain.destroy()
                page.stopEditing()
                editor.dialogs.createReminder(rowOf("Zadzwonić do hydraulika"), false)
            },
            () => {
                reminderDialog = editor.dialogs.reminderDialog
                if (!waitUntil(reminderDialog && reminderDialog.opened))
                    return
                window.timeField = findItem(reminderDialog.contentItem, "datePickerTime") as BTextField
                window.dateField = findItem(reminderDialog.contentItem, "datePickerNatural") as BTextField
                datePicker = window.timeField
                while (datePicker && datePicker.applyTime === undefined)
                    datePicker = datePicker.parent
                check(datePicker !== null && clockDialogsIn(datePicker) === 0, "the reminder's date picker has no clock until it is asked for")
                window.timeField.forceActiveFocus()
                window.timeField.selectAll()
            },
            () => typeKeys("2346"),
            () => tapInDialog(reminderDialog, findItem(reminderDialog.contentItem, "datePickerClock")),
            () => {
                const clk = datePicker.clockDialog
                if (!waitUntil(clk && clk.opened))
                    return
                check(reminderDialog.at === "2026-10-16 23:46" && clk.picker.time === "23:46",
                      "the Clock button applies the typed time first and opens the clock on it: " + reminderDialog.at)
                window.firstClock = clk
            },
            () => clockTap(datePicker.clockDialog.picker, datePicker.clockDialog.picker.outerRadius, 5 * 30),
            () => clockTap(datePicker.clockDialog.picker, datePicker.clockDialog.picker.outerRadius, 15 * 6),
            () => datePicker.clockDialog.tryAccept(),
            () => check(reminderDialog.at === "2026-10-16 05:15", "Set on the clock changes the reminder's time: " + reminderDialog.at),
            () => {
                if (!waitUntil(!datePicker.clockDialog.visible))
                    return
                check(window.timeField.text === "05:15", "the time field shows the clock's time: " + window.timeField.text)
                window.dateField.forceActiveFocus()
            },
            () => check(reminderDialog.at === "2026-10-16 05:15", "leaving the time field keeps it: " + reminderDialog.at),
            () => { window.timeField.forceActiveFocus(); window.timeField.selectAll() },
            () => typeKeys("25:99"),
            () => tapInDialog(reminderDialog, findItem(reminderDialog.contentItem, "datePickerClock")),
            () => {
                const clk = datePicker.clockDialog
                if (!waitUntil(clk.opened))
                    return
                check(datePicker.timeInvalid && clk.picker.time === "05:15",
                      "after an invalid typed time the clock opens on the last good one: " + clk.picker.time)
                check(datePicker.clockDialog === window.firstClock, "the date picker keeps its one clock")
                clockTap(clk.picker, clk.picker.innerRadius, 7 * 30)
            },
            () => clockTap(datePicker.clockDialog.picker, datePicker.clockDialog.picker.outerRadius, 0),
            () => datePicker.clockDialog.tryAccept(),
            () => {
                if (!waitUntil(!datePicker.clockDialog.visible))
                    return
                check(reminderDialog.at === "2026-10-16 19:00" && !datePicker.timeInvalid && window.timeField.text === "19:00"
                      && reminderDialog.acceptEnabled, "a time from the clock replaces an invalid typed one: " + reminderDialog.at)
                reminderDialog.reject()
            },
            // ---- The calendar's add dialog ------------------------------------------------------
            () => {
                if (!waitUntil(!reminderDialog.visible))
                    return
                addDialog = Qt.createQmlObject('import QtQuick; import Brulion; CalendarAddDialog {}', window.contentItem, "addDialog")
                check(addDialog.clockDialog === null, "the calendar's add dialog creates no clock until it is asked for")
                addDialog.openFor("reminder", "2026-10-16")
            },
            () => {
                if (!waitUntil(addDialog.opened))
                    return
                findItem(addDialog.contentItem, "calendarAddTime").forceActiveFocus()
            },
            () => typeKeys("2346"),
            () => tapInDialog(addDialog, findItem(addDialog.contentItem, "calendarAddClock")),
            () => {
                const clk = addDialog.clockDialog
                if (!waitUntil(clk && clk.opened))
                    return
                check(addDialog.time === "23:46" && clk.picker.time === "23:46", "the add dialog's clock opens on the typed time: " + clk.picker.time)
                clockTap(clk.picker, clk.picker.outerRadius, 30)
            },
            () => clockTap(addDialog.clockDialog.picker, addDialog.clockDialog.picker.outerRadius, 10 * 6),
            () => addDialog.clockDialog.tryAccept(),
            () => {
                if (!waitUntil(!addDialog.clockDialog.visible))
                    return
                check(addDialog.time === "01:10" && findItem(addDialog.contentItem, "calendarAddTime").text === "",
                      "a time picked on the clock replaces the typed one: " + addDialog.time)
                addDialog.reject()
            }
        ]
        stepIndex = 0
        stepTimer.start()
    }
    property var firstClock: null

    function runScript(name) {
        switch (name) {
        case "selftest":
            selfTest()
            break
        case "clock":
            clockTest()
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
