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
// menu), task-menu-query (a query result's), task-slash (the status commands), date-menu /
// date-menu-reminder (a 📅 / ⏰ chip's menu), recurrence-menu (a 🔁 rule's), link-menu (a #tag's);
// the document
// mode on the sample trip plan (FakeEditorHost.documentPath): document (reading),
// document-paragraph, document-list, document-heading, document-task (editing one),
// document-outline (the same page switched to an outline); `selftest` runs keyboard/input-method
// checks (outline and document mode) and exits; `clock` checks the round clock and its buttons
// (clockTest()) and exits; `taskstatus` checks the task status menu and commands
// (taskStatusTest()) and exits; `keyboard` checks the touch toolbar on a simulated on-screen
// keyboard (keyboardTest(): KeyboardInsets.simulated) and exits; `blocklinks` checks copying links
// to blocks and headings and following them (blockLinksTest()) and exits; `refreshtest` checks
// that views follow what they depend on (RefreshSelfTest.qml) and exits; `datechips` checks the
// menu of date chips with real input (DateChipSelfTest.qml) and exits; `copylink` /
// `copylink-document` show the block menu's "Kopiuj odnośnik" (outline / document),
// `copylink-toolbar` the menu from the touch toolbar's "Więcej" (--touch), `copylink-table` a
// table's menu from its cell toolbar's "Więcej" (--touch), `link-flash` a
// copied block link followed (the block selected and flashing). The in-memory vault runs
// ```query blocks on the real index (FakeEditorHost). Like the app's shell, the editor ends at
// the on-screen keyboard (KeyboardInsets.overlap); a simulated one is drawn grey.
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
                  : DevTools.editorScript.startsWith("document") || DevTools.editorScript === "copylink-document"
                    || DevTools.editorScript === "link-flash" || DevTools.editorScript === "selection-document"
                    ? host.documentPath : host.samplePath
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
        // Ends at the on-screen keyboard (as AppShell does for the app's views).
        anchors.bottomMargin: KeyboardInsets.overlap
        controller: page
        focus: true
        onMessage: (text, kind, action) => toasts.show(text, action ? { kind: kind, actionText: action.text, onAction: action.run }
                                                           : { kind: kind })
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
        bottomMargin: Theme.space4 + KeyboardInsets.overlap + (editor.toolbarVisible ? editor.toolbarHeight : 0)
    }
    // A simulated on-screen keyboard (--keyboard <px>, keyboardTest()).
    Loader {
        parent: T.Overlay.overlay
        z: 2000
        active: KeyboardInsets.source === "simulated" && KeyboardInsets.overlap > 0
        anchors.left: parent ? parent.left : undefined
        anchors.right: parent ? parent.right : undefined
        anchors.bottom: parent ? parent.bottom : undefined
        height: KeyboardInsets.overlap
        sourceComponent: BKeyboardPlaceholder {}
    }

    // ---- Selecting and copying blocks (`--editor-script selectiontest`) -----------------------------
    // The self-test lives in SelectionSelfTest.qml (qml/dev): it uses the step runner below.
    Loader {
        id: selectionTestLoader
        active: false
        sourceComponent: SelectionSelfTest {
            window: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- Views following what they depend on (`--editor-script refreshtest`) -----------------------
    // The self-test lives in RefreshSelfTest.qml (qml/dev): it uses the step runner below.
    Loader {
        id: refreshTestLoader
        active: false
        sourceComponent: RefreshSelfTest {
            window: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- The menu of date chips (`--editor-script datechips`) ------------------------------------
    Loader {
        id: dateChipTestLoader
        active: false
        sourceComponent: DateChipSelfTest {
            window: window
            editor: editor
            page: page
            host: host
        }
    }
    /// Run `s` (functions) one after another on the step timer, then exit with the failures.
    function runSteps(s) {
        steps = s
        stepIndex = 0
        stepTimer.start()
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
    /// The paragraph zoomed into for Enter at its end.
    property int zoomTextRow: -1
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
    /// The delegate of `row` is laid out and wholly inside the list's view.
    function rowInView(row) {
        const list = editor.listView
        const item = list.itemAtIndex(row)
        if (!item || !item.visible)
            return false
        const p = item.mapToItem(list, 0, 0)
        return p.y >= -0.5 && p.y + item.height <= list.height + 0.5
    }
    // What an input method (Gboard) is told about the text around the caret.
    function surroundingText(edit) { return edit ? String(edit.inputMethodQuery(Qt.ImSurroundingText, null)) : "<none>" }
    function imCursor(edit) { return edit ? Number(edit.inputMethodQuery(Qt.ImCursorPosition, null)) : -1 }
    /// The item named `name` under `item` (depth first), or null.
    /// TestEvent for the pictures from the internet (null without QtTest).
    property var webEvents: null
    /// A picture from the internet's placeholder in `item` (BlockDelegate): inside its frame,
    /// the site named on its own line, the alt text (or the failure) at most 2 (3) lines, the
    /// buttons side by side when both fit.
    function checkWebPlaceholder(item, failed) {
        const frame = item ? findItem(item.embedView, "embedPicture") : null
        const holder = item ? findItem(item.embedView, "webPicturePlaceholder") : null
        const text = item ? findItem(item.embedView, "webPictureText") as Text : null
        const site = item ? findItem(item.embedView, "webPictureHost") as Text : null
        const show = item ? findItem(item.embedView, "webPictureShow") as BButton : null
        const save = item ? findItem(item.embedView, "webPictureSave") : null
        const embed = item && item.embeds.length > 0 ? item.embeds[0] : null
        const what = failed ? "a failed picture from the internet: " : "a picture from the internet waiting: "
        check(frame !== null && holder !== null && holder.visible && holder.y >= 0
              && holder.y + holder.height <= frame.height,
              what + "the placeholder fits its frame: " + (holder ? holder.y + "+" + holder.height : "-")
              + " in " + (frame ? frame.width + "×" + frame.height : "-"))
        check(site !== null && site.visible && embed !== null && embed.host !== ""
              && site.text.indexOf(embed.host) >= 0 && site.lineCount === 1,
              what + "its site is named: " + (site ? site.text : "-"))
        check(text !== null && text.visible && text.lineCount <= (failed ? 3 : 2) && (failed || text.truncated),
              what + "the text above it has its lines: " + (text ? text.lineCount : "-"))
        check(show !== null && save !== null && show.visible
              && (show.text === (failed ? qsTranslate("EmbedView", "Try again") : qsTranslate("EmbedView", "Show")))
              && (show.width + save.width + Theme.space2 > holder.width || show.y === save.y),
              what + "its buttons are side by side: " + (show && save ? show.y + " / " + save.y : "-"))
    }
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
    /// A step is running. A step that processes events (a touch sequence's commit() does) lets the
    /// timer fire from inside it when the step outlasts the interval (a slow sanitizer build): that
    /// tick is skipped — it ran the same step again and then skipped the next one.
    property bool inStep: false
    Timer {
        id: stepTimer
        interval: 120
        repeat: true
        onTriggered: {
            if (window.inStep)
                return
            if (window.stepIndex >= window.steps.length) {
                stop()
                console.log("SELFTEST DONE failures=" + window.failures)
                Qt.exit(window.failures === 0 ? 0 : 1)
                return
            }
            window.inStep = true
            try {
                window.steps[window.stepIndex]()
            } catch (e) {
                window.check(false, "step " + window.stepIndex + " threw: " + e)
            }
            window.inStep = false
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
            // The keyboard capitalises a sentence start from the text around the caret: the
            // hidden task marker must not be part of it (an empty text, caret at 0).
            () => check(surroundingText(editor.activeEditor) === "" && imCursor(editor.activeEditor) === 0,
                        "a new task block shows the keyboard an empty text: \"" + surroundingText(editor.activeEditor)
                        + "\" at " + imCursor(editor.activeEditor)),
            () => editor.activeEditor.insert(editor.activeEditor.cursorPosition, "Nowe zadanie ąę"),
            () => check(info(r + 1).plainText === "Nowe zadanie ąę", "typing reaches the model: " + info(r + 1).plainText),
            () => check(surroundingText(editor.activeEditor) === "Nowe zadanie ąę",
                        "the keyboard sees the task's text without its marker: " + surroundingText(editor.activeEditor)),
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
            // "Create page" leads the list (always in view), but Enter takes the existing page
            // (above) and Up reaches "Create page".
            () => {
                const e = editor.activeEditor
                e.cursorPosition = e.length
                e.insert(e.length, " [[Remo")
            },
            () => {
                const popup = editor.suggestions
                const list = findItem(popup.contentItem, "suggestionList") as ListView
                check(popup.visible && popup.model.get(0).kind === "create" && popup.model.get(0).title === "Remo"
                      && popup.model.count > 1 && popup.currentIndex === 1 && popup.model.get(1).kind === "page"
                      && list.indexAt(1, list.contentY + 1) === 0,
                      "Create page first, the first page highlighted: " + popup.currentIndex + " "
                      + JSON.stringify(popup.model.get(0)))
            },
            () => { key(Qt.Key_Up) },
            () => check(editor.suggestions.currentIndex === 0, "Up reaches Create page: " + editor.suggestions.currentIndex),
            () => { key(Qt.Key_Return) },
            () => check(editor.activeEditor.text.indexOf("[[Remont kuchni]] [[Remo]]") > 0 && !editor.suggestions.opened,
                        "Up + Enter links the new page: " + editor.activeEditor.text),
            // `#` offers pages too (a tag names a page, docs/SYNTAX.md §3) — also after a written
            // date on the line (here the merged "⏳ 2026-10-18") —; a name with a space is written
            // #[[…]].
            () => {
                const e = editor.activeEditor
                e.forceActiveFocus()
                e.cursorPosition = e.length
                e.insert(e.length, " #remont")
            },
            () => check(editor.suggestions.visible && editor.suggestions.trigger.mode === "tags"
                        && editor.suggestions.model.count > 0 && editor.suggestions.model.get(0).icon === "file-text",
                        "# offers pages next to the tags: " + JSON.stringify(editor.suggestions.model.get(0))),
            () => { key(Qt.Key_Return) },
            () => check(editor.activeEditor.text.indexOf(" #[[Remont ") > 0 && !editor.suggestions.opened,
                        "a page accepted after # is written #[[…]]: " + editor.activeEditor.text),
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
            // Enter at the end of a zoomed paragraph: a paragraph has no children, so the zoom
            // ends and the new block below it is edited — never a caret in a hidden block. Ctrl+Z
            // zooms back in, Ctrl+Shift+Z ends the zoom again.
            () => {
                window.textBefore = page.sourceText
                window.zoomTextRow = rowOf("Plan remontu")
                page.zoomInto(window.zoomTextRow)
                editor.startEditing(window.zoomTextRow, -1)
            },
            () => check(window.zoomTextRow > 0 && page.zoomBlock === window.zoomTextRow
                        && page.editingRow === window.zoomTextRow && editor.activeEditor !== null
                        && editor.activeEditor.activeFocus, "a zoomed paragraph is edited: " + window.zoomTextRow),
            () => key(Qt.Key_Return),
            () => check(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && info(page.editingRow).visible
                        && editor.activeEditor !== null && editor.activeEditor.activeFocus
                        && editor.activeEditor.row === page.editingRow && rowInView(page.editingRow),
                        "Enter at the end of a zoomed paragraph ends the zoom and edits the new block below it: zoom "
                        + page.zoomBlock + ", row " + page.editingRow + " (paragraph " + window.zoomTextRow + ")"),
            () => key(Qt.Key_Z, Qt.ControlModifier),
            () => check(page.zoomBlock === window.zoomTextRow && page.editingRow === window.zoomTextRow
                        && page.sourceText === window.textBefore && editor.activeEditor !== null
                        && editor.activeEditor.activeFocus,
                        "Ctrl+Z zooms back into the paragraph: zoom " + page.zoomBlock + ", row " + page.editingRow),
            () => key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier),
            () => check(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && editor.activeEditor !== null
                        && editor.activeEditor.activeFocus && editor.activeEditor.row === window.zoomTextRow + 1,
                        "Ctrl+Shift+Z ends the zoom again: zoom " + page.zoomBlock + ", row " + page.editingRow),
            () => typeKeys("Źdźbło"),
            () => check(info(window.zoomTextRow + 1).plainText === "Źdźbło" && page.zoomBlock < 0,
                        "typing goes into the new block: " + info(window.zoomTextRow + 1).plainText),
            () => key(Qt.Key_Z, Qt.ControlModifier), // the typing
            () => key(Qt.Key_Z, Qt.ControlModifier), // Enter
            () => {
                check(page.sourceText === window.textBefore && page.zoomBlock === window.zoomTextRow,
                      "two Ctrl+Z undo the typing and Enter, back in the zoom: zoom " + page.zoomBlock)
                page.zoomTo(-1)
                page.stopEditing()
            },
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
            // A block is prose: the keyboard capitalises its first letter (unlike the date fields).
            () => check(editor.activeEditor && (editor.activeEditor.inputMethodHints & Qt.ImhNoAutoUppercase) === 0
                        && surroundingText(editor.activeEditor) === "" && imCursor(editor.activeEditor) === 0,
                        "a new block keeps the keyboard's automatic capital letter: \""
                        + surroundingText(editor.activeEditor) + "\" at " + imCursor(editor.activeEditor)),
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
            // Enter at the end of a zoomed heading far down the page: the rows above it come back
            // with the zoom's end, and the view follows the new block below it (it does not stay
            // at the page's top, where the zoom had shown the heading).
            () => {
                window.zoomTextRow = page.insertMarkdown(page.lastVisibleRow(), "## Na dole strony ąę")
                page.zoomInto(window.zoomTextRow)
                editor.startEditing(window.zoomTextRow, -1)
            },
            () => check(page.zoomBlock === window.zoomTextRow && info(window.zoomTextRow).view === "heading"
                        && page.editingRow === window.zoomTextRow && rowInView(window.zoomTextRow),
                        "a zoomed heading far down the page is edited: " + window.zoomTextRow),
            () => key(Qt.Key_Return),
            () => check(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && editor.activeEditor !== null
                        && editor.activeEditor.activeFocus && rowInView(window.zoomTextRow + 1)
                        && rowInView(window.zoomTextRow),
                        "Enter at the end of a zoomed heading far down the page keeps it and the new block in view: zoom "
                        + page.zoomBlock + ", row " + page.editingRow + ", y "
                        + (editor.listView.itemAtIndex(window.zoomTextRow + 1)
                           ? editor.listView.itemAtIndex(window.zoomTextRow + 1).mapToItem(editor.listView, 0, 0).y : "-")),
            () => key(Qt.Key_Z, Qt.ControlModifier),
            () => check(page.zoomBlock === window.zoomTextRow && page.editingRow === window.zoomTextRow
                        && rowInView(window.zoomTextRow), "Ctrl+Z zooms back into the heading, in view"),
            () => key(Qt.Key_Z, Qt.ControlModifier), // the heading added above
            () => {
                check(page.zoomBlock < 0 && rowOf("Na dole strony") < 0, "and the heading is gone with the zoom: " + page.zoomBlock)
                page.zoomTo(-1)
            },
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
                if (!d.opened && window.waits++ < 2 * window.waitLimit) {
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
                // Gboard wrote "Za 2 minuty": a date in words is no sentence, so the keyboard
                // must not capitalise its first letter (the block editor itself still does).
                check(window.dateField !== null && (window.dateField.inputMethodHints & Qt.ImhNoAutoUppercase) !== 0,
                      "the typed date gets no automatic capital letter")
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
                if (!editor.dialogs.reminderDialog.opened && window.waits++ < 2 * window.waitLimit) {
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
                if (!editor.dialogs.dateDialog.opened && window.waits++ < 2 * window.waitLimit) {
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
                check(field !== null && (field.inputMethodHints & Qt.ImhNoAutoUppercase) !== 0,
                      "the typed recurrence rule gets no automatic capital letter")
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
            // ---- Undo of a block inserted at the very start of a page (row 0) ----------------------
            // The caret goes back to the block it was in and every row shows its own block.
            () => {
                page.save()
                host.setFile("Na początek.md", "- Zażółć gęślą\n- jaźń\n")
                page.path = "Na początek.md"
            },
            () => editor.startEditing(0, 0),
            () => realKey(Qt.Key_Return),
            () => check(page.blocks.count === 3 && info(0).plainText === "" && page.editingRow === 1
                        && editor.activeEditor && editor.activeEditor.activeFocus && editor.activeEditor.row === 1
                        && editor.activeEditor.plain() === "Zażółć gęślą",
                        "real Enter at the start of the first block puts an empty block above it"),
            () => realKey(Qt.Key_Z, Qt.ControlModifier),
            () => {
                check(page.sourceText === "- Zażółć gęślą\n- jaźń\n" && page.blocks.count === 2,
                      "real Ctrl+Z takes the block at the start away: " + JSON.stringify(page.sourceText))
                check(page.editingRow === 0 && editor.activeEditor && editor.activeEditor.activeFocus
                      && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "Zażółć gęślą"
                      && editor.activeEditor.cursorPosition === 0,
                      "after Ctrl+Z the caret is at the start of the first block: row "
                      + (editor.activeEditor ? editor.activeEditor.row + " " + JSON.stringify(editor.activeEditor.plain()) : "none"))
                const second = editor.listView.itemAtIndex(1)
                check(second !== null && textContaining(second, "jaźń") !== null && textContaining(second, "Zażółć") === null,
                      "after Ctrl+Z the second row shows its own block")
            },
            () => typeKeys("ą"),
            () => check(page.sourceText === "- ąZażółć gęślą\n- jaźń\n", "typing after Ctrl+Z goes into the first block: "
                        + JSON.stringify(page.sourceText)),
            () => { realKey(Qt.Key_Z, Qt.ControlModifier); editor.startEditing(1, 2) },
            () => realKey(Qt.Key_Up, Qt.AltModifier),
            () => check(page.sourceText === "- jaźń\n- Zażółć gęślą\n" && page.editingRow === 0 && editor.activeEditor
                        && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "jaźń",
                        "real Alt+Up moves the second block to the start of the page, edited: "
                        + JSON.stringify(page.sourceText)),
            () => realKey(Qt.Key_Z, Qt.ControlModifier),
            () => {
                check(page.sourceText === "- Zażółć gęślą\n- jaźń\n" && page.editingRow === 1 && editor.activeEditor
                      && editor.activeEditor.activeFocus && editor.activeEditor.row === 1 && editor.activeEditor.plain() === "jaźń"
                      && editor.activeEditor.cursorPosition === 2,
                      "real Ctrl+Z of that move puts the caret back where it was: row "
                      + (editor.activeEditor ? editor.activeEditor.row + " " + JSON.stringify(editor.activeEditor.plain()) : "none"))
                const first = editor.listView.itemAtIndex(0)
                check(first !== null && textContaining(first, "Zażółć") !== null && textContaining(first, "jaźń") === null,
                      "after Ctrl+Z the first row shows its own block")
            },
            () => realKey(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier),
            () => check(page.sourceText === "- jaźń\n- Zażółć gęślą\n" && page.editingRow === 0 && editor.activeEditor
                        && editor.activeEditor.activeFocus && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "jaźń"
                        && editor.activeEditor.cursorPosition === 2,
                        "real Ctrl+Shift+Z moves it to the start again, edited"),
            // A done task: the hidden "[x] " is no text, so Enter at the start of its text puts an
            // empty task above it and the text stays done.
            () => {
                page.save()
                host.setFile("Zadanie na początek.md", "- [x] Zrobione źdźbło\n")
                page.path = "Zadanie na początek.md"
            },
            () => editor.startEditing(0, 0),
            () => realKey(Qt.Key_Return),
            () => check(page.sourceText === "- [ ] \n- [x] Zrobione źdźbło\n" && info(0).taskStatus === "todo"
                        && info(0).plainText === "" && info(1).taskStatus === "done"
                        && info(1).plainText === "Zrobione źdźbło" && page.editingRow === 1
                        && editor.activeEditor && editor.activeEditor.row === 1
                        && editor.activeEditor.plain() === "Zrobione źdźbło",
                        "real Enter at the start of a done task's text puts an empty task above it: "
                        + JSON.stringify(page.sourceText)),
            // An empty paragraph is only blank lines in the file: Ctrl+Shift+Z brings it back.
            () => {
                page.save()
                host.setFile("Akapit na początek.md", "---\nbrulion: document\n---\nPierwszy akapit źdźbła\n\nDrugi\n")
                page.path = "Akapit na początek.md"
            },
            () => editor.startEditing(1, 0),
            () => realKey(Qt.Key_Return),
            () => check(page.blocks.count === 4 && info(1).kind === "paragraph" && info(1).plainText === ""
                        && page.editingRow === 2, "real Enter at the start of a paragraph puts an empty one above it"),
            () => realKey(Qt.Key_Z, Qt.ControlModifier),
            () => check(page.blocks.count === 3 && page.editingRow === 1, "real Ctrl+Z takes the empty paragraph away"),
            () => realKey(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier),
            () => check(page.blocks.count === 4 && info(1).kind === "paragraph" && info(1).plainText === ""
                        && page.editingRow === 2 && editor.activeEditor && editor.activeEditor.row === 2
                        && editor.activeEditor.plain() === "Pierwszy akapit źdźbła",
                        "real Ctrl+Shift+Z brings the empty paragraph back, the caret in the paragraph after it: rows "
                        + page.blocks.count + ", editing " + page.editingRow),
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
                // The picture loads asynchronously: this step repeats until it has (at most
                // 2 × waitLimit steps).
                if ((!item || Math.abs(h - w / 4) > 2) && window.waits++ < 2 * window.waitLimit) {
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
                if (!ready && window.waits++ < 2 * window.waitLimit) {
                    window.stepIndex -= 1
                    return
                }
                const shown = ready ? img.implicitWidth / img.implicitHeight : -1
                const box = ready ? frame.width / frame.height : -2
                check(ready && Math.abs(box / shown - 1) < 0.03,
                      "a replaced picture is not stretched: frame " + (frame ? frame.width + "×" + frame.height : "-")
                      + ", picture " + (img ? img.implicitWidth + "×" + img.implicitHeight : "-"))
            },
            // A picture from the internet: nothing loads until "Pokaż"; then it is shown from the
            // cache with its own shape (an SVG by its bytes, the URL has no .svg), a right-click
            // opens its menu — naming the site, and only that menu —, and "Zapisz w notatniku"
            // makes it an attachment of the notebook (one undo step).
            () => {
                host.setWebPictureText("https://example.com/mapa",
                                       "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"300\" height=\"100\""
                                       + " viewBox=\"0 0 300 100\"><rect width=\"300\" height=\"100\" fill=\"#c3e69f\"/></svg>")
                window.realRow = page.insertBlockAfter(rowOf("Akapit po liście"), "![Mapa trasy](https://example.com/mapa)")
                page.stopEditing()
                window.waits = 0
            },
            () => editor.listView.positionViewAtIndex(window.realRow, ListView.Center),
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const holder = item ? window.findItem(item.embedView, "webPicturePlaceholder") : null
                const show = item ? window.findItem(item.embedView, "webPictureShow") : null
                check(holder !== null && holder.visible && show !== null && show.visible,
                      "a picture from the internet waits for “Pokaż”")
                check(host.webFetchCount() === 0, "nothing is fetched before “Pokaż”")
                if (show)
                    show.clicked()
            },
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const frame = item ? window.findItem(item.embedView, "embedPicture") : null
                const img = item ? window.findItem(item.embedView, "embedImage") as Image : null
                const ready = frame !== null && img !== null && img.status === Image.Ready && frame.height > 0
                if (!ready && window.waits++ < 2 * window.waitLimit) {
                    window.stepIndex -= 1
                    return
                }
                check(ready && Math.abs(frame.width / frame.height - 3) < 0.1 && img.source.toString().startsWith("file:"),
                      "the picture is shown from the cache with its shape: " + (frame ? frame.width + "×" + frame.height : "-"))
                check(img !== null && img.sourceSize.width === 1024 && img.sourceSize.height === 420,
                      "an SVG from the internet is drawn to fit, not at 2048 × 2048: "
                      + (img ? img.sourceSize.width + "×" + img.sourceSize.height : "-"))
                // Real clicks when QtTest is there (TestEvent), else the signals themselves.
                if (!window.webEvents) {
                    try {
                        window.webEvents = Qt.createQmlObject('import QtTest; TestEvent {}', window, "webEvents")
                    } catch (e) {
                        window.webEvents = null
                    }
                }
                // A click opens the picture on its own (the viewer), not its menu.
                if (frame && window.webEvents)
                    window.webEvents.mouseClick(frame, frame.width / 2, frame.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
                else if (item && item.embeds.length > 0)
                    item.embedView.webOpen(item.embeds[0])
                window.waits = 0
            },
            () => {
                const viewer = editor.webPictureViewer
                const img = viewer ? window.findItem(viewer.contentItem, "webPictureViewerImage") as Image : null
                const ready = viewer !== null && viewer.visible && img !== null && img.status === Image.Ready
                if (!ready && window.waits++ < 2 * window.waitLimit) {
                    window.stepIndex -= 1
                    return
                }
                check(ready && img.source.toString().startsWith("file:"),
                      "a click on the picture opens it in the viewer, from the cache: " + (img ? img.source : "-"))
                const name = viewer ? window.findItem(viewer.contentItem, "webPictureViewerName") as Text : null
                check(name !== null && name.text.indexOf("example.com") >= 0, "the viewer names the site: " + (name ? name.text : "-"))
                check(editor.webPictureMenu === null || !editor.webPictureMenu.visible, "and no menu opens")
                // An SVG fills the window (drawn to fit, its shape kept).
                check(img !== null && Math.abs(img.width / img.height - 3) < 0.1 && img.width > 300,
                      "the viewer draws the SVG fitted with its shape: " + (img ? img.width + "×" + img.height : "-"))
                const close = viewer ? window.findItem(viewer.contentItem, "webPictureViewerClose") : null
                if (close)
                    close.clicked()
                else if (viewer)
                    viewer.close()
            },
            () => {
                check(editor.webPictureViewer !== null && !editor.webPictureViewer.visible, "“Zamknij” closes the viewer")
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const frame = item ? window.findItem(item.embedView, "embedPicture") : null
                // A right-click: the picture's menu.
                if (frame && window.webEvents)
                    window.webEvents.mouseClick(frame, frame.width / 2, frame.height / 2, Qt.RightButton, Qt.NoModifier, -1)
                else if (item)
                    item.embedView.webMenu("https://example.com/mapa", "example.com", frame, 10, 10)
            },
            () => {
                const menu = editor.webPictureMenu
                check(menu !== null && menu.visible, "a right-click on the picture opens its menu")
                check(editor.blockMenu === null || !editor.blockMenu.visible,
                      "and not the block menu as well")
                const first = menu ? menu.itemAt(0) as BMenuItem : null
                check(first !== null && first.objectName === "webPictureHost" && !first.enabled && first.height > 0
                      && first.text.indexOf("example.com") >= 0 && menu.itemAt(1) !== null
                      && menu.itemAt(1).objectName === "webPictureHostLine",
                      "the menu's first line names the site: " + (first ? first.text : "-"))
                const view = menu ? menu.itemAt(2) as BMenuItem : null
                check(view !== null && view.objectName === "webPictureView" && view.visible && view.enabled,
                      "the menu offers the viewer too (“Powiększ”)")
                const save = editor.webPictureMenu ? window.findItem(editor.webPictureMenu.contentItem, "webPictureDownload") : null
                if (save)
                    save.triggered()
                else if (editor.webPictureMenu)
                    editor.webPictureMenu.chosen("download")
                if (editor.webPictureMenu)
                    editor.webPictureMenu.close()
            },
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                const embed = item && item.embeds.length > 0 ? item.embeds[0] : null
                check(page.webPictureUrls().length === 0 && embed !== null && !embed.web && embed.target === "assets/mapa.svg",
                      "“Zapisz w notatniku” points the picture at the attachment: " + (embed ? embed.target : "-"))
                page.undo()
            },
            () => check(page.webPictureUrls().indexOf("https://example.com/mapa") >= 0, "undo brings the address back"),
            // Its placeholder: a long alt text takes at most two lines, the site's line is always
            // there, both buttons side by side when they fit, and all of it inside the frame —
            // also once the fetch failed.
            () => {
                window.farRow = page.insertBlockAfter(window.realRow,
                    "![Zdjęcie z wycieczki: Morskie Oko o świcie, żółta łąka, ślady niedźwiedzia przy schronisku"
                    + " i mgła nad Czarnym Stawem pod Rysami, zrobione z ostatniego zakrętu drogi](https://zdjęcia.example.org/brak.jpg)")
                page.stopEditing()
                window.waits = 0
            },
            () => editor.listView.positionViewAtIndex(window.farRow, ListView.Center),
            () => {
                const item = editor.listView.itemAtIndex(window.farRow) as BlockDelegate
                window.checkWebPlaceholder(item, false)
                const show = item ? window.findItem(item.embedView, "webPictureShow") : null
                if (show)
                    show.clicked()
            },
            () => {
                const item = editor.listView.itemAtIndex(window.farRow) as BlockDelegate
                const embed = item && item.embeds.length > 0 ? item.embeds[0] : null
                if ((!embed || embed.state !== "failed") && window.waits++ < 2 * window.waitLimit) {
                    window.stepIndex -= 1
                    return
                }
                window.waits = 0
                check(embed !== null && embed.state === "failed", "a picture that cannot be fetched fails")
                window.checkWebPlaceholder(item, true)
                page.deleteBlock(window.farRow)
            },
            // "/Obraz z internetu…": "Zapisz w notatniku" checked each time it opens (the dialog
            // is reused), an address without http:// or https:// refused with both named.
            () => editor.dialogs.pickWebImage(window.realRow, -1),
            waitStep(() => editor.dialogs.webImageDialog !== null && editor.dialogs.webImageDialog.opened,
                     "“Obraz z internetu…” opens its dialog"),
            () => {
                const d = editor.dialogs.webImageDialog
                check(d.saveBox.checked, "it opens with “Zapisz w notatniku” checked")
                d.saveBox.checked = false
                d.urlField.text = "www.example.com/żółw.jpg"
            },
            () => {
                const d = editor.dialogs.webImageDialog
                const error = d.urlField.errorText
                check(!d.valid && !d.acceptEnabled && error.indexOf("http://") >= 0 && error.indexOf("https://") >= 0,
                      "an address without its scheme is refused: " + error)
                d.reject()
            },
            waitStep(() => !editor.dialogs.webImageDialog.visible, "Anuluj closes it"),
            () => editor.dialogs.pickWebImage(window.realRow, -1),
            waitStep(() => editor.dialogs.webImageDialog.opened, "it opens again"),
            () => {
                const d = editor.dialogs.webImageDialog
                check(d.saveBox.checked && d.urlField.text === "", "again with “Zapisz w notatniku” checked and no address")
                d.reject()
            },
            waitStep(() => !editor.dialogs.webImageDialog.visible, "and closes again"),
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
                // until it is (at most 2 × waitLimit steps).
                if (!ordered && window.waits++ < 2 * window.waitLimit) {
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
    /// A tap with a finger that shakes a little (two moves of a few pixels, well under a drag).
    function clockShakyTap(c, radius, deg) {
        const face = c.focusItem
        const p = clockPoint(c, radius, deg)
        if (Theme.touchUi) {
            clockEvents.touchEvent(face).press(0, face, p.x, p.y).commit()
            clockEvents.touchEvent(face).move(0, face, p.x + 2, p.y + 1).commit()
            clockEvents.touchEvent(face).move(0, face, p.x + 3, p.y - 1).commit()
            clockEvents.touchEvent(face).release(0, face, p.x + 3, p.y - 1).commit()
        } else {
            clockEvents.mousePress(face, p.x, p.y, Qt.LeftButton, Qt.NoModifier, -1)
            clockEvents.mouseMove(face, p.x + 2, p.y + 1, -1, Qt.LeftButton, Qt.NoModifier)
            clockEvents.mouseMove(face, p.x + 3, p.y - 1, -1, Qt.LeftButton, Qt.NoModifier)
            clockEvents.mouseRelease(face, p.x + 3, p.y - 1, Qt.LeftButton, Qt.NoModifier, -1)
        }
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
    /// The number circles the face shows (hours or minutes) and the chosen value's bubble, as
    /// { label, bubble, outer (on the outer ring), x, y, r, textWidth, font } in the face's
    /// coordinates.
    function clockCircles(c) {
        const out = []
        const face = c.focusItem
        for (let i = 0; i < face.children.length; ++i) {
            const item = face.children[i]
            if (!item.visible || item.width <= 0)
                continue
            const hour = item.selected !== undefined && item.angle !== undefined
            const minute = !hour && item.value !== undefined && item.angle !== undefined
            // The bubble: a round Rectangle holding only its text (the face's disc holds nothing).
            const bubble = !hour && !minute && item.children.length === 1 && item.children[0].text !== undefined
                           && Math.abs(item.radius - item.width / 2) < 0.01
            if (!hour && !minute && !bubble)
                continue
            let text = null
            for (let j = 0; j < item.children.length; ++j) {
                if (item.children[j].text !== undefined)
                    text = item.children[j]
            }
            const x = item.x + item.width / 2
            const y = item.y + item.height / 2
            out.push({ label: bubble ? "bubble " + text.text : text ? text.text : "?", bubble: bubble,
                       outer: Math.abs(Math.hypot(x - c.center, y - c.center) - c.outerRadius) < 0.5,
                       x: x, y: y, r: item.width / 2,
                       textWidth: text ? text.contentWidth : 0, font: text ? text.font.pixelSize : 0 })
        }
        return out
    }
    /// The numbers on the face never overlap (nor does the chosen value's bubble reach over a
    /// neighbour of its number), each holds its text in a legible font — the outer ring's in the
    /// text size itself, the inner one's at least 12 px —, and a press near a number's edge,
    /// towards its neighbour, still gives that number.
    function checkClockNumbers(c, what) {
        const circles = clockCircles(c)
        const numbers = circles.filter(n => !n.bubble)
        const bubble = circles.filter(n => n.bubble)[0]
        const overlaps = []
        const cramped = []
        const fits = (n, minFont) => n.textWidth <= 2 * n.r - 2 && n.font >= minFont
        for (let i = 0; i < numbers.length; ++i) {
            const a = numbers[i]
            if (!fits(a, a.outer ? Theme.textSm : Math.min(Theme.textSm, 12)))
                cramped.push(a.label + " (" + a.textWidth.toFixed(1) + " px text in " + 2 * a.r + ", font " + a.font + ")")
            for (let j = i + 1; j < numbers.length; ++j) {
                const b = numbers[j]
                const d = Math.hypot(a.x - b.x, a.y - b.y)
                if (d < a.r + b.r - 0.01)
                    overlaps.push(a.label + "/" + b.label + " " + d.toFixed(1) + " < " + (a.r + b.r))
            }
            const toBubble = bubble ? Math.hypot(a.x - bubble.x, a.y - bubble.y) : 0
            if (bubble && toBubble > 0.5 && toBubble < a.r + bubble.r - 0.01)
                overlaps.push(bubble.label + "/" + a.label + " " + toBubble.toFixed(1) + " < " + (a.r + bubble.r))
        }
        if (bubble && !fits(bubble, 10))
            cramped.push(bubble.label + " (" + bubble.textWidth.toFixed(1) + " px text in " + 2 * bubble.r + ")")
        const wrong = []
        if (c.mode === "hour") {
            for (let h = 0; h < 24; ++h) {
                const n = numbers.filter(m => m.label === c.pad(h))[0]
                for (const side of [-1, 1]) {
                    const other = (h % 12 + 12 + side) % 12 + (h >= 12 ? 12 : 0)
                    const next = numbers.filter(m => m.label === c.pad(other))[0]
                    if (!n || !next)
                        continue
                    const d = Math.hypot(next.x - n.x, next.y - n.y)
                    const x = n.x + (next.x - n.x) / d * (n.r - 1)
                    const y = n.y + (next.y - n.y) / d * (n.r - 1)
                    if (c.labelAt(x, y) !== h)
                        wrong.push(n.label + "→" + c.labelAt(x, y))
                }
            }
        }
        const expected = c.mode === "hour" ? 24 : 12
        check(numbers.length === expected && overlaps.length === 0 && cramped.length === 0 && wrong.length === 0,
              "the clock's numbers do not overlap and hold their text, " + what + ": " + numbers.length + " numbers"
              + (overlaps.length ? ", overlapping " + overlaps : "") + (cramped.length ? ", cramped " + cramped : "")
              + (wrong.length ? ", a press near a number's edge gives another one " + wrong : ""))
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
    /// Repeat the current step until `ready` (an asynchronous popup), at most 2 × waitLimit steps
    /// (~6 s, times BRULION_TEST_SLOWDOWN).
    function waitUntil(ready) {
        if (ready || window.waits++ >= 2 * window.waitLimit) {
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
            // ---- The numbers never overlap, down to the smallest dial (the dialog's 180 px floor;
            //      196 px: a phone held sideways), with an inner hour or a minute chosen ----------
            () => checkClockNumbers(c, c.dialSize + " px, hours"),
            () => { window.clockSizeBefore = c.dialSize; c.dialSize = 196; c.time = "13:25" },
            () => checkClockNumbers(c, "196 px, hours, 13 chosen"),
            () => { c.dialSize = 180 },
            () => checkClockNumbers(c, "180 px, hours, 13 chosen"),
            () => { c.mode = "minute" },
            () => checkClockNumbers(c, "180 px, minutes"),
            // Larger text (Ustawienia → Rozmiar tekstu → Większy) on the smallest dials: the
            // outer numbers keep the text size, the inner hours stay legible.
            () => { window.clockFontScaleBefore = Theme.fontScale; Theme.fontScale = 1.3; c.mode = "hour" },
            () => checkClockNumbers(c, "180 px, larger text, hours, 13 chosen"),
            () => { c.mode = "minute" },
            () => checkClockNumbers(c, "180 px, larger text, minutes"),
            () => { c.dialSize = 196; c.mode = "hour" },
            () => checkClockNumbers(c, "196 px, larger text, hours, 13 chosen"),
            () => {
                Theme.fontScale = window.clockFontScaleBefore
                c.dialSize = window.clockSizeBefore
                c.mode = "hour"
                c.time = "18:30"
            },
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
            () => { c.time = "21:30"; clockShakyTap(c, c.outerRadius - 3, 45 * 6 + 4) },
            () => check(c.time === "21:45", "a shaky tap a little off the number 45 (the finger moves a few pixels) gives 45: " + c.time),
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
            () => {
                window.clockOther = Qt.createQmlObject('import QtQuick; Item { activeFocusOnTab: true }', window.contentItem, "clockOther")
                window.clockOther.forceActiveFocus()
            },
            () => c.focusItem.forceActiveFocus(Qt.TabFocusReason),
            () => check(c.focusItem.activeFocus && clockRing(c).visible, "focus that comes with Tab shows the focus ring"),
            () => clockTap(c, c.outerRadius, 120),
            () => {
                check(c.focusItem.activeFocus && !clockRing(c).visible && c.time === "03:20",
                      "a tap after Tab hides the focus ring: " + c.time)
                window.clockOther.destroy()
            },
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
                checkClockNumbers(p, "in the dialog (dial " + p.dialSize + " px)")
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
    property var clockOther: null
    property int clockSizeBefore: 0
    property real clockFontScaleBefore: 1

    // ---- Self test of task statuses (`--editor-script taskstatus`, tst_editor_qml_taskstatus*) ---
    // The status menu with real input — a right-click (mouse) or a long press (finger, --touch) —
    // on the checkboxes of the editor, a query result and an embedded section, a menu item clicked
    // or tapped (the result's and the embed's menu stay open while their file changes elsewhere:
    // the rows wait, the status goes to the task's line now); a plain click / tap still toggles;
    // Ctrl+Shift+Enter while editing and on a
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
    /// The query block's live results (QueryResultModel), or null.
    function queryResults() {
        const item = editor.listView.itemAtIndex(viewRow("query")) as BlockDelegate
        const view = item ? item.contentItem as QueryBlockView : null
        return view ? view.results : null
    }
    /// The query's results show what the index has now: each row's page, line and status as a run
    /// of the query now gives them. A status written from a result re-runs the query a moment later
    /// (debounced, after the index followed the file) and replaces the rows: a gesture on a row
    /// before that went to a checkbox about to go, and its menu went with it.
    function queryResultsSettled() {
        const results = queryResults()
        if (!results || !results.ready)
            return false
        const now = host.runQuery(results.source, page.path)
        const items = now.items || []
        const shownCount = Math.min(items.length, results.maxItems)
        if (!now.ok || results.rowCount() !== shownCount)
            return false
        for (let i = 0; i < shownCount; ++i) {
            const shown = results.get(i)
            if (shown.path !== items[i].path || shown.line !== items[i].line
                    || shown.status !== (items[i].task ? items[i].task.status : ""))
                return false
        }
        return true
    }
    /// The status menu of the result whose text starts with `prefix` is open.
    function resultMenuOpen(prefix) {
        const box = resultTaskBox(prefix)
        return box !== null && box.statusMenu !== null && box.statusMenu.opened
    }
    function editorMenuOpen() { return editor.taskMenu !== null && editor.taskMenu.opened }
    function embedMenuOpen() {
        const box = embedTaskBox()
        return box !== null && box.statusMenu !== null && box.statusMenu.opened
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
    /// held down until `shown()` (TaskBox's press-and-hold opened the menu) and lifted — as a user
    /// lifts it once the menu shows. (A fixed hold lifted it too early in a slow sanitizer build: a
    /// tap, which toggled the task.)
    function menuGesture(target, shown) {
        if (!Theme.touchUi) {
            return [() => {
                const item = target()
                check(item !== null, "the checkbox is there for the right-click")
                if (item)
                    taskEvents.mouseClick(item, item.width / 2, item.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            }]
        }
        return [
            () => {
                const item = target()
                check(item !== null, "the checkbox is there for the long press")
                if (item)
                    taskEvents.touchEvent(item).press(0, item, item.width / 2, item.height / 2).commit()
            },
            holdStep(shown),
            () => {
                const item = target()
                if (item)
                    taskEvents.touchEvent(item).release(0, item, item.width / 2, item.height / 2).commit()
            }
        ]
    }
    /// How many steps a wait for something asynchronous lasts at most: ~3 s, times
    /// BRULION_TEST_SLOWDOWN (ctest sets it, 25 in sanitizer builds: tests/CMakeLists.txt).
    readonly property int waitLimit: 25 * DevTools.testSlowdown
    /// A step that repeats until `ready()` (at most waitLimit times) and checks nothing: a finger
    /// held down until the menu shows (the step after it lifts the finger, the next checks).
    function holdStep(ready) {
        return () => {
            if (!ready() && window.waits++ < window.waitLimit) {
                window.stepIndex -= 1
                return
            }
            window.waits = 0
        }
    }
    /// Steps that wait until `ready()` (at most waitLimit steps), then go on. `what`: the check's
    /// text, or a function giving it (read when the step runs).
    function waitStep(ready, what) {
        return () => {
            if (!ready() && window.waits++ < window.waitLimit) {
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
    /// The TransclusionView of the embedded section (the one with embedTaskBox()), or null.
    function embedView() {
        let item = embedTaskBox()
        while (item && item.shown === undefined)
            item = item.parent
        return item
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
            ...menuGesture(() => blockTaskBox(window.taskRow), editorMenuOpen),
            waitStep(editorMenuOpen, gesture + " on a checkbox opens the status menu"),
            () => {
                const menu = editor.taskMenu
                check(info(window.taskRow).taskStatus === "todo", gesture + " does not toggle the task")
                const todo = menuItemNamed(menu, "taskStatus_todo")
                const doing = menuItemNamed(menu, "taskStatus_doing")
                check(todo !== null && todo.checked && doing !== null && !doing.checked
                      && menuItemNamed(menu, "taskStatus_none") !== null, "the current status is checked")
                tapItem(doing)
            },
            waitStep(() => !editor.taskMenu.visible, "choosing a status closes the menu"),
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
            ...menuGesture(() => blockTaskBox(window.taskRow), editorMenuOpen),
            waitStep(editorMenuOpen, "the menu opens again"),
            () => {
                const doing = menuItemNamed(editor.taskMenu, "taskStatus_doing")
                check(!doing.checked && menuItemNamed(editor.taskMenu, "taskStatus_todo").checked,
                      "the check marks follow the status (not the last click)")
                tapItem(menuItemNamed(editor.taskMenu, "taskStatus_none"))
            },
            waitStep(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "",
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
            waitStep(() => editor.taskMenu.opened, "Ctrl+Shift+Enter opens the status menu of the edited block"),
            () => {
                check(info(window.taskRow).taskStatus === "todo", "Ctrl+Shift+Enter does not cycle the status")
                taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
            },
            waitStep(() => !editor.taskMenu.visible && editor.activeEditor !== null && editor.activeEditor.activeFocus,
                      "Escape closes the menu and the block has the keyboard again"),
            () => {
                check(editor.activeEditor.row === window.taskRow && editor.activeEditor.cursorPosition === 3
                      && info(window.taskRow).taskStatus === "todo", "nothing changed, the caret is where it was")
                realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier)
                window.waits = 0
            },
            waitStep(() => editor.taskMenu.opened, "the menu opens again"),
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
            waitStep(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "deferred",
                      "Enter on “Oczekujące” sets the status"),
            waitStep(() => editor.activeEditor !== null && editor.activeEditor.activeFocus
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
            waitStep(() => editor.taskMenu.opened, "Ctrl+Shift+Enter on a selected block opens its menu"),
            () => tapItem(menuItemNamed(editor.taskMenu, "taskStatus_cancelled")),
            waitStep(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "cancelled",
                      "a status for the selected block"),
            waitStep(() => editor.listView.activeFocus && page.selectedRow === window.taskRow,
                      "the selected block keeps the keyboard"),
            // A heading gets no status: Ctrl+Shift+Enter opens no menu there (selected or edited).
            () => {
                window.taskRow = rowOf("Koszty")
                check(window.taskRow >= 0 && info(window.taskRow).kind === "heading", "the page has the heading “Koszty”")
                window.taskText = pageText()
                page.selectedRow = window.taskRow
                EditorInput.sendKey(editor.listView, Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
            },
            () => {
                check(!editor.taskMenu.visible && pageText() === window.taskText,
                      "Ctrl+Shift+Enter on a selected heading opens no status menu")
                editor.startEditing(window.taskRow, -1)
            },
            waitStep(() => editor.activeEditor !== null && editor.activeEditor.activeFocus
                     && editor.activeEditor.row === window.taskRow, "the heading is edited"),
            () => realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier),
            () => {
                check(!editor.taskMenu.visible && pageText() === window.taskText && editor.activeEditor !== null
                      && editor.activeEditor.activeFocus, "Ctrl+Shift+Enter in a heading opens no status menu")
                page.stopEditing()
            },
            () => { page.selectedRow = -1 },
            // ---- "/" commands typed with real keys -----------------------------------------------
            () => { window.taskRow = page.insertBlockAfter(rowOf("Ekipa może"), "") },
            () => typeKeys("Zadzwonić do Żanety /czeka"),
            waitStep(() => editor.suggestionsOpen && editor.suggestions.model.count > 0
                      && editor.suggestions.model.get(0).command === "taskWaiting",
                      "“/czeka” offers “Zadanie: oczekujące” first"),
            () => realKey(Qt.Key_Return),
            // (The list closes with a transition: a step later it may still be fading out.)
            waitStep(() => info(window.taskRow).taskStatus === "deferred" && editor.activeEditor.plain() === "Zadzwonić do Żanety "
                      && editor.activeEditor.cursorPosition === 20 && !editor.suggestionsOpen,
                      () => "the command sets the status, its text goes, the caret stays: " + editor.activeEditor.plain()),
            () => page.undo(),
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
            waitStep(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "task",
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
            waitStep(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "taskNone",
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
            waitStep(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "task",
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
            waitStep(() => editor.suggestionsOpen && editor.suggestions.model.get(0).command === "taskDoing",
                      "“/toku” offers “Zadanie: w toku”"),
            () => realKey(Qt.Key_Return),
            () => check(info(window.taskRow).taskStatus === "doing" && pageText().indexOf("- DOING Odebrać kafelki") > 0,
                        "the Logseq keyword follows: " + fileLineOf(pageText(), "kafelki")),
            () => page.stopEditing(),
            // ---- A query result's checkbox: its own menu, the task in its own file ----------------
            () => {
                page.save()
                editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning)
            },
            // The page's edits above re-run the query too: its rows are replaced until it settled.
            waitStep(() => resultTaskBox("Mleko") !== null && resultTaskBox("Śrubki") !== null && queryResultsSettled(),
                      "the query block lists the shopping tasks"),
            ...menuGesture(() => resultTaskBox("Mleko"), () => resultMenuOpen("Mleko")),
            waitStep(() => resultMenuOpen("Mleko"), gesture + " on a query result's checkbox opens its menu"),
            // Another device adds a task above it while the menu is open (a sync): the results
            // wait for the menu — new rows would take it away — and the status goes to the task
            // where it is now, not to its old line.
            () => {
                window.ownMenu = resultTaskBox("Mleko").statusMenu
                check(queryResults().held, "the open menu holds the results")
                host.externalWrite("Zakupy.md", "- [ ] Jajka #zakupy\n" + host.file("Zakupy.md"))
            },
            waitStep(() => (host.runQuery(queryResults().source, page.path).items || []).some(i => i.text.indexOf("Jajka") === 0),
                     "the index has the new task"),
            () => {}, () => {}, () => {}, // longer than the results' debounce
            () => {
                check(resultMenuOpen("Mleko") && resultTaskBox("Mleko").statusMenu === window.ownMenu,
                      "the menu stays open while the results change elsewhere")
                check(resultTaskBox("Jajka") === null, "the results wait for the menu")
                check(fileLine("Zakupy.md", "Mleko") === "- [ ] Mleko #zakupy", gesture + " does not toggle the result")
                check(menuItemNamed(window.ownMenu, "taskStatus_todo").checked, "its status is checked")
                tapItem(menuItemNamed(window.ownMenu, "taskStatus_deferred"))
            },
            waitStep(() => host.file("Zakupy.md").indexOf("- [ ] Jajka #zakupy\n- [>] Mleko #zakupy\n") === 0,
                      () => "a result's status is written in its own file, on the task's line now: "
                            + host.file("Zakupy.md").split("\n").slice(0, 2).join(" | ")),
            // The next gesture once the results followed: "status:open" lists no waiting task, and
            // the task added meanwhile came.
            waitStep(() => queryResultsSettled() && resultTaskBox("Mleko") === null && resultTaskBox("Śrubki") !== null
                           && resultTaskBox("Jajka") !== null && !queryResults().held,
                      "the results follow once the menu closed: the waiting task is no open task any more, the new one is listed"),
            ...menuGesture(() => resultTaskBox("Śrubki"), () => resultMenuOpen("Śrubki")),
            waitStep(() => resultMenuOpen("Śrubki"), "the next result's menu opens"),
            () => tapItem(menuItemNamed(resultTaskBox("Śrubki").statusMenu, "taskStatus_none")),
            waitStep(() => fileLine("Zakupy.md", "Śrubki") === "- Śrubki do półki #zakupy",
                      () => "“No status” on a result removes the task's checkbox in its file: " + fileLine("Zakupy.md", "Śrubki")),
            waitStep(() => resultTaskBox("Śrubki") === null && queryResultsSettled(), "the result is no task any more"),
            // ---- An embedded section's task (Budżet domowy#Październik) --------------------------
            () => editor.listView.positionViewAtIndex(rowOf("Budżet na październik") + 1, ListView.Center),
            waitStep(() => embedTaskBox() !== null, "the embedded section shows its task's checkbox"),
            ...menuGesture(() => embedTaskBox(), embedMenuOpen),
            waitStep(embedMenuOpen, gesture + " on an embedded task opens its menu"),
            // The section changes elsewhere while the menu is open: the embed waits, the status
            // goes to the task's line now.
            () => {
                window.ownMenu = embedTaskBox().statusMenu
                host.externalWrite("Budżet domowy.md", host.file("Budżet domowy.md").replace("- [ ] Opłacić czynsz", "- Prąd: 250 zł\n- [ ] Opłacić czynsz"))
            },
            waitStep(() => embedView() !== null && JSON.stringify(embedView().transclusions).indexOf("Prąd") >= 0,
                     "the block's embeds follow the file"),
            () => {}, () => {},
            () => {
                check(embedMenuOpen() && embedTaskBox().statusMenu === window.ownMenu, "the embed's menu stays open")
                check(JSON.stringify(embedView().shown).indexOf("Prąd") < 0, "the embed waits for the menu")
                tapItem(menuItemNamed(window.ownMenu, "taskStatus_done"))
            },
            waitStep(() => fileLine("Budżet domowy.md", "czynsz") === "- [x] Opłacić czynsz 📅 2026-10-20 #finanse ✅ 2026-10-15",
                      () => "an embedded task's status is written in its own file: " + fileLine("Budżet domowy.md", "czynsz")),
            () => check(fileLine("Budżet domowy.md", "Prąd") === "- Prąd: 250 zł", "the line above it is left alone"),
            waitStep(() => embedView() !== null && JSON.stringify(embedView().shown).indexOf("Prąd") >= 0,
                     "the embed follows once the menu closed"),
            // ---- The touch toolbar: the task button held down opens the menu above it --------------
            () => {
                Theme.forceTouch = true
                window.taskRow = rowOf("Położyć hydroizolację")
                editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
                editor.startEditing(window.taskRow, -1)
            },
            waitStep(() => {
                const writing = findItem(editor, "toolbarWriting")
                const button = writing ? findItem(writing, "toolbarTask") : null
                return editor.toolbarVisible && button !== null && button.width > 0
            }, "the touch toolbar shows its task button"),
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                taskEvents.touchEvent(button).press(0, button, button.width / 2, button.height / 2).commit()
            },
            holdStep(editorMenuOpen),
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                taskEvents.touchEvent(button).release(0, button, button.width / 2, button.height / 2).commit()
            },
            waitStep(editorMenuOpen, "holding the toolbar's task button opens the status menu"),
            () => {
                const button = findItem(findItem(editor, "toolbarWriting"), "toolbarTask")
                const menuBottom = editor.taskMenu.contentItem.mapToItem(null, 0, editor.taskMenu.contentItem.height).y
                const buttonTop = button.mapToItem(null, 0, 0).y
                check(menuBottom <= buttonTop + 1, "the menu is above the toolbar: " + menuBottom + " ≤ " + buttonTop)
                check(info(window.taskRow).taskStatus === "todo", "holding the button does not cycle the status")
                tapItem(menuItemNamed(editor.taskMenu, "taskStatus_done"))
            },
            waitStep(() => !editor.taskMenu.visible && info(window.taskRow).taskStatus === "done",
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
    // ---- Links to blocks and headings (`--editor-script blocklinks`, tst_editor_qml_blocklinks*) --
    // "Kopiuj odnośnik" with real input: the block menu from a right-click on a heading's text, a
    // paragraph, a callout and a calc block (desktop) — a finger's long press on text selects the
    // block, no menu (--touch) —, from a bullet (right-click / long press) and from the touch
    // toolbar's "Więcej" (the menu above the bar); the submenu's items put the link on the
    // clipboard (read back by pasting) and say so; Ctrl+Shift+C while editing, on a selected block
    // and with nothing selected, the palette's heading link; a copied link followed by a click or
    // tap scrolls to its block or heading, selects and flashes it — again after scrolling away —, a
    // missing heading is said; a table: "Więcej" on the cell toolbar, Ctrl+Shift+C in a cell, the
    // palette after the cell lost the keyboard; (desktop) a right-click on a title drawn above the
    // page's properties.
    property string lastNotice: ""
    /// The last toast's button ({ text, run }), else null.
    property var lastNoticeAction: null
    /// The playground's toasts (the self-tests press their buttons).
    readonly property BToastHost toastHost: toasts
    property string copiedId: ""
    property string expectedLink: ""
    property Item focusProbe: null
    property int linkRow: -1
    property int targetRow: -1
    Connections {
        target: editor
        function onMessage(text, kind, action) {
            window.lastNotice = text
            window.lastNoticeAction = action
        }
    }
    /// What the clipboard holds (pasted into a hidden text field).
    function clipboardText() {
        const edit = Qt.createQmlObject('import QtQuick; TextEdit { visible: false }', window, "clipboardProbe")
        edit.paste()
        const text = edit.text
        edit.destroy()
        return text
    }
    function delegateAt(row) {
        editor.listView.forceLayout()
        return editor.listView.itemAtIndex(row)
    }
    /// The rendered rich text under `item`, or null.
    function renderedIn(item) {
        if (!item || !item.visible)
            return null
        if (item.textFormat === Text.RichText && typeof item.linkAt === "function")
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = renderedIn(item.children[i])
            if (found)
                return found
        }
        return null
    }
    /// A point of the rendered `text` on its link whose URL contains `url`, or null.
    function linkPointIn(text, url) {
        for (let y = 2; text && y < text.height; y += 3) {
            for (let x = 2; x < text.width; x += 4) {
                if (text.linkAt(x, y).indexOf(url) >= 0)
                    return Qt.point(x + 2, y + 2)
            }
        }
        return null
    }
    /// A tap with a finger on touch UIs, else a left click, at (x, y) of `item`.
    function tapPoint(item, x, y) {
        if (Theme.touchUi) {
            taskEvents.touchEvent(item).press(0, item, x, y).commit()
            taskEvents.touchEvent(item).release(0, item, x, y).commit()
        } else {
            taskEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function rightClick(item, x, y) {
        if (item)
            taskEvents.mouseClick(item, x, y, Qt.RightButton, Qt.NoModifier, -1)
    }
    /// Where the block menu began before the keyboard moved, and its gap above the toolbar's
    /// "Więcej" then (blocklinks).
    property real menuTopBefore: 0
    property real menuGapBefore: 0
    /// From the block menu's bottom to the top of the toolbar's "Więcej" under it.
    function menuGap() {
        const menu = editor.blockMenu
        return findItem(editor, "toolbarMore").mapToItem(null, 0, 0).y
               - menu.background.mapToItem(null, 0, menu.background.height).y
    }
    function blockMenuOpen() {
        return editor.blockMenu !== null && editor.blockMenu.opened
    }
    /// Where `menu` begins in the window.
    function menuTop(menu) {
        return menu.background.mapToItem(null, 0, 0).y
    }
    /// The block menu's entry of the "Kopiuj odnośnik" submenu, and the submenu.
    function copyEntry() {
        const menu = editor.blockMenu
        for (let i = 0; menu && i < menu.count; ++i) {
            const item = menu.itemAt(i)
            if (item && item.subMenu && item.subMenu.objectName === "copyLinkMenu")
                return item
        }
        return null
    }
    function copySubmenu() {
        const entry = copyEntry()
        return entry ? entry.subMenu : null
    }
    /// Steps: open "Kopiuj odnośnik", choose `name` there; the clipboard then holds `expected()`
    /// and the toast says so.
    function copySteps(name, what, expected) {
        return [
            () => {
                check(copyEntry() !== null && copyEntry().text === copySubmenu().title, "the block menu has “Copy link”")
                tapItem(copyEntry())
            },
            waitStep(() => copySubmenu() !== null && copySubmenu().opened, "“Copy link” opens its submenu"),
            () => check(menuTop(copySubmenu()) >= T.Overlay.overlay.SafeArea.margins.top,
                        "the submenu begins below the window's status bar: " + menuTop(copySubmenu())),
            () => tapItem(menuItemNamed(copySubmenu(), name)),
            waitStep(() => !blockMenuOpen() && clipboardText() === expected(),
                     () => what + ": " + clipboardText() + " (" + expected() + ")"),
            () => check(window.lastNotice.endsWith(" " + expected()) && window.lastNotice.length > expected().length + 3,
                        "a toast says what was copied: " + window.lastNotice)
        ]
    }
    /// Steps of the menu gesture on point (x, y) of `target()`: a right-click, or a finger held
    /// down until `shown()` (a bullet's long press opened the menu) and lifted. (A fixed hold could
    /// be lifted too early in a slow sanitizer run: a tap, which zooms into the block.)
    function holdOrRightClick(target, x, y, shown) {
        if (!Theme.touchUi)
            return [() => rightClick(target(), x(), y())]
        return [
            () => taskEvents.touchEvent(target()).press(0, target(), x(), y()).commit(),
            holdStep(shown),
            () => taskEvents.touchEvent(target()).release(0, target(), x(), y()).commit()
        ]
    }

    function blockLinksTest() {
        taskEvents = Qt.createQmlObject('import QtTest; TestEvent {}', window, "linkEvents")
        const touch = Theme.touchUi
        const gesture = touch ? "a long press" : "a right-click"
        const doc = "Wyjazd w Tatry"
        let s = [
            () => { page.path = host.documentPath },
            waitStep(() => page.loaded && page.documentMode && rowOf("Plan") > 0, "the document is open"),
        ]
        if (!touch) {
            s = s.concat([
                // ---- A heading's link from a right-click on its text --------------------------------
                () => {
                    window.targetRow = rowOf("Plan")
                    window.textBefore = pageText()
                    editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                },
                () => {
                    const text = renderedOf(window.targetRow)
                    check(text !== null, "the heading shows its text")
                    rightClick(text, text.width / 4, text.height / 2)
                },
                waitStep(() => blockMenuOpen() && editor.blockMenu.heading, "a right-click on a heading's text opens the block menu"),
                () => check(menuItemNamed(editor.blockMenu, "blockMenuZoom").shown === false, "a document has no zoom in the menu"),
                ...copySteps("copyHeadingLink", "“Link to heading” copies [[Page#Heading]]", () => "[[" + doc + "#Plan]]"),
                () => check(pageText() === window.textBefore, "a heading's link changes nothing in the page"),
                // ---- A paragraph: its block reference, the id written once -------------------------
                () => {
                    window.targetRow = rowOf("Pierwszego dnia")
                    editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                },
                () => {
                    const text = renderedOf(window.targetRow)
                    rightClick(text, text.width / 2, text.height / 2)
                },
                waitStep(() => blockMenuOpen() && !editor.blockMenu.heading, "a right-click on a paragraph opens the block menu"),
                () => check(menuItemNamed(copySubmenu(), "copyHeadingLink").shown === false, "no heading link for a paragraph"),
                ...copySteps("copyBlockRef", "“Block reference” copies ((id))", () => "((" + info(window.targetRow).blockId + "))"),
                () => {
                    const id = info(window.targetRow).blockId
                    check(id.length === 6 && pageText().indexOf("tłoczno. ^" + id) > 0, "the paragraph got its id: " + id)
                },
                // ---- Frames: a callout and (later, in the outline) a calc block ---------------------
                () => {
                    window.targetRow = viewRow("callout")
                    editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                },
                () => {
                    const item = delegateAt(window.targetRow)
                    rightClick(item, item.width / 2, item.height / 2)
                },
                waitStep(() => blockMenuOpen(), "a right-click on a callout opens the block menu"),
                () => taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1),
                waitStep(() => !blockMenuOpen(), "Escape closes it"),
            ])
        } else {
            s = s.concat([
                // ---- A finger's long press on text opens no menu (left for selecting text) ---------
                () => {
                    window.targetRow = rowOf("Pierwszego dnia")
                    editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                },
                () => {
                    const text = renderedOf(window.targetRow)
                    taskEvents.touchEvent(text).press(0, text, text.width / 2, text.height / 2).commit()
                },
                // Held until the long press took effect: it selects the block (the selection mode).
                holdStep(() => editor.selectionMode && page.selectedRow === window.targetRow),
                () => {
                    const text = renderedOf(window.targetRow)
                    if (text)
                        taskEvents.touchEvent(text).release(0, text, text.width / 2, text.height / 2).commit()
                },
                () => check(editor.selectionMode && page.selectedRow === window.targetRow
                            && (editor.blockMenu === null || !editor.blockMenu.visible),
                            "a finger's long press on text selects the block and opens no block menu"),
                () => editor.leaveSelectionMode(),
            ])
        }
        s = s.concat([
            // ---- Ctrl+Shift+C: editing, selected, nothing; the palette's heading link -----------------
            () => {
                page.stopEditing()
                window.targetRow = rowOf("Piątek")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                editor.startEditing(window.targetRow, -1)
            },
            waitStep(() => editor.activeEditor !== null && editor.activeEditor.activeFocus, "a list item is edited"),
            () => realKey(Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier),
            waitStep(() => info(window.targetRow).blockId !== "" && clipboardText() === "((" + info(window.targetRow).blockId + "))",
                     () => "Ctrl+Shift+C copies the edited block's reference: " + clipboardText()),
            () => {
                window.copiedId = info(window.targetRow).blockId
                check(window.lastNotice.endsWith(" ((" + window.copiedId + "))"), "and says so: " + window.lastNotice)
                check(editor.activeEditor !== null && editor.activeEditor.activeFocus && page.editingRow === window.targetRow,
                      "the block is still being edited")
                editor.copyCurrentLink("heading") // the palette's "Kopiuj link do nagłówka"
            },
            waitStep(() => clipboardText() === "[[" + doc + "#Plan]]",
                     () => "the palette's heading link names the heading of the caret's section: " + clipboardText()),
            () => realKey(Qt.Key_Escape),
            () => {
                check(page.selectedRow === window.targetRow, "Esc selects the block")
                EditorInput.sendKey(editor.listView, Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
            },
            waitStep(() => clipboardText() === "((" + window.copiedId + "))", "Ctrl+Shift+C on a selected block, the same id"),
            () => {
                page.selectedRow = -1
                window.lastNotice = ""
                window.textBefore = pageText()
                EditorInput.sendKey(editor.listView, Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
            },
            waitStep(() => window.lastNotice === editor.copyFailure("noRow"),
                     () => "with nothing selected it says what to do: " + window.lastNotice),
            () => check(pageText() === window.textBefore, "and changes nothing"),
            // ---- Following copied links: scroll, select, flash — twice; a missing heading ------------
            () => {
                window.linkRow = page.insertBlockAfter(page.lastVisibleRow(),
                    "Zobacz [[" + doc + "#^" + window.copiedId + "]], [[" + doc + "#Plan]] i [[#Nieistniejący]]")
                page.stopEditing()
                editor.listView.positionViewAtEnd()
            },
            () => {
                const text = renderedOf(window.linkRow)
                check(text !== null && String(text.text).indexOf(doc + " › Piątek") >= 0,
                      "[[Page#^id]] reads as the page and the block's text")
                const p = linkPointIn(text, "#^" + window.copiedId)
                check(p !== null, "the link to the block is there")
                if (p)
                    tapPoint(text, p.x, p.y)
            },
            waitStep(() => page.selectedRow === window.targetRow && findShown(delegateAt(window.targetRow), "blockFlash") !== null,
                     "following [[Page#^id]] selects the block and flashes it"),
            () => {
                const item = delegateAt(window.targetRow)
                const top = item.mapToItem(editor.listView, 0, 0).y
                check(top >= 0 && top + item.height <= editor.listView.height, "the block is scrolled into view: " + top)
                page.selectedRow = -1
                editor.listView.positionViewAtEnd()
            },
            () => {
                const text = renderedOf(window.linkRow)
                const p = linkPointIn(text, "#^" + window.copiedId)
                if (p)
                    tapPoint(text, p.x, p.y)
            },
            waitStep(() => page.selectedRow === window.targetRow, "the same link a second time, after scrolling away, goes there again"),
            waitStep(() => findShown(delegateAt(window.targetRow), "blockFlash") === null, "the flash fades away"),
            () => {
                check(delegateAt(window.targetRow).height > 0, "the flash changed no height")
                editor.listView.positionViewAtEnd()
            },
            () => {
                const text = renderedOf(window.linkRow)
                const p = linkPointIn(text, "#Plan")
                if (p)
                    tapPoint(text, p.x, p.y)
            },
            waitStep(() => page.selectedRow === rowOf("Plan"), "following [[Page#Heading]] selects the heading"),
            () => {
                editor.listView.positionViewAtEnd()
                window.lastNotice = ""
            },
            () => {
                const text = renderedOf(window.linkRow)
                const p = linkPointIn(text, "#Nieistniej")
                if (p)
                    tapPoint(text, p.x, p.y)
            },
            waitStep(() => window.lastNotice.indexOf("Nieistniejący") >= 0 && window.lastNotice !== "",
                     () => "a missing heading is said: " + window.lastNotice),
            // ---- The touch toolbar's "Więcej": the block menu above the bar ----------------------------
            () => {
                page.selectedRow = -1
                Theme.forceTouch = true
                window.targetRow = rowOf("Niedziela")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                editor.startEditing(window.targetRow, -1)
            },
            waitStep(() => editor.toolbarVisible && findItem(editor, "toolbarMore") !== null, "the touch toolbar has “More”"),
            () => {
                // A phone's keyboard and the status bar over an edge-to-edge window's top (Android
                // 11+): the menu, taller than the room above the toolbar, stays below the bar —
                // a finger's tap there reached the status bar, not the menu (the emulator).
                T.Overlay.overlay.SafeArea.additionalMargins.top = 48
                KeyboardInsets.simulated = 336
            },
            () => {
                const flick = findItem(editor, "editToolbarFlick")
                flick.contentX = Math.max(0, flick.contentWidth - flick.width)
            },
            () => tapItem(findItem(editor, "toolbarMore")),
            waitStep(() => blockMenuOpen(), "“More” on the toolbar opens the block menu"),
            () => {
                const button = findItem(editor, "toolbarMore")
                const menu = editor.blockMenu
                const menuBottom = menu.contentItem.mapToItem(null, 0, menu.contentItem.height).y
                const buttonTop = button.mapToItem(null, 0, 0).y
                check(menuBottom <= buttonTop + 1, "the menu is above the toolbar: " + menuBottom + " ≤ " + buttonTop)
                check(menuTop(menu) >= 48 + 8 - 1, "and below the status bar: " + menuTop(menu) + " ≥ 56")
                check(editor.blockMenu.listItem && !editor.blockMenu.heading, "it is the edited block's menu")
                window.menuGapBefore = menuGap()
                window.menuTopBefore = menuTop(menu)
                // A finger already on it: it stays where it is when the keyboard goes.
                const item = menuItemNamed(menu, "blockMenuCopyText")
                taskEvents.touchEvent(item).press(0, item, item.width / 2, item.height / 2).commit()
            },
            () => { KeyboardInsets.simulated = 0 },
            () => {},
            () => {
                const item = menuItemNamed(editor.blockMenu, "blockMenuCopyText")
                check(editor.blockMenu.touched && Math.abs(menuTop(editor.blockMenu) - window.menuTopBefore) < 1,
                      "touched, the menu does not move when the keyboard goes: " + menuTop(editor.blockMenu) + " vs "
                      + window.menuTopBefore)
                // Lifted off the menu (at the window's top): nothing chosen.
                const away = item.mapFromItem(null, 20, 20)
                taskEvents.touchEvent(item).move(0, item, away.x, away.y).commit()
                taskEvents.touchEvent(item).release(0, item, away.x, away.y).commit()
            },
            () => {
                check(editor.blockMenu.visible, "the menu stays open")
                editor.blockMenu.close()
                KeyboardInsets.simulated = 336
            },
            waitStep(() => !editor.blockMenu.visible && editor.toolbarVisible, "closed, the toolbar back on the keyboard"),
            () => tapItem(findItem(editor, "toolbarMore")),
            waitStep(() => blockMenuOpen() && !editor.blockMenu.touched, "“More” opens the block menu again"),
            // The menu took the keys: the system hides the keyboard, the toolbar drops to the bottom.
            () => { KeyboardInsets.simulated = 0 },
            waitStep(() => {
                const menu = editor.blockMenu
                return menu.visible && menuGap() >= 0 && menuGap() < 24 && !menu.contentItem.interactive
            }, () => "the untouched menu follows the toolbar down when the keyboard goes, right above it, every "
                     + "item shown (none scrolled away): " + menuGap() + " px above it (" + window.menuGapBefore
                     + " before), scrolls " + editor.blockMenu.contentItem.interactive),
            () => check(menuItemNamed(editor.blockMenu, "blockMenuDelete").mapToItem(null, 0, 0).y
                        + menuItemNamed(editor.blockMenu, "blockMenuDelete").height
                        <= findItem(editor, "toolbarMore").mapToItem(null, 0, 0).y,
                        "“Usuń blok”, its last item, is on screen above the toolbar"),
            ...copySteps("copyBlockLink", "“Link to block” copies [[Page#^id]]",
                         () => "[[" + doc + "#^" + info(window.targetRow).blockId + "]]"),
            () => {
                check(info(window.targetRow).blockId !== "" && page.editingRow === window.targetRow,
                      "the block got its id and is still being edited")
                page.stopEditing()
                T.Overlay.overlay.SafeArea.additionalMargins.top = 0
                KeyboardInsets.simulated = -1
                Theme.forceTouch = DevTools.touch
            },
            // ---- An outline: the bullet's menu; a calc block's (desktop) ------------------------------
            () => { page.path = host.samplePath },
            waitStep(() => page.loaded && !page.documentMode && rowOf("Kolejność") > 0, "the outline is open"),
            () => {
                // A list item without a checkbox (a task's box sits over its bullet on touch screens).
                window.targetRow = rowOf("Kolejność")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            },
            ...holdOrRightClick(() => delegateAt(window.targetRow), () => delegateAt(window.targetRow).bulletX,
                                () => delegateAt(window.targetRow).firstLineCenter, blockMenuOpen),
            waitStep(() => blockMenuOpen(), gesture + " on a bullet opens the block menu"),
            () => check(menuItemNamed(editor.blockMenu, "blockMenuZoom").shown, "with zoom in an outline"),
            () => check(page.zoomBlock < 0, "and does not zoom"),
            ...copySteps("copyEmbed", "“Embed” copies !((id))", () => "!((" + info(window.targetRow).blockId + "))"),
            // ---- A table: "Więcej" on the cell toolbar; the edited cell's table is current -------------
            () => {
                page.selectedRow = -1
                Theme.forceTouch = true
                window.targetRow = viewRow("table")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            },
            () => {
                const item = delegateAt(window.targetRow)
                check(item !== null, "the table is on screen")
                if (item)
                    item.focusContent()
            },
            waitStep(() => editor.toolbarMode === "cell" && findShown(findItem(editor, "editToolbarSlot"), "toolbarCellMore") !== null,
                     "a table cell's toolbar has “More”"),
            () => {
                const flick = findItem(editor, "editToolbarFlick")
                flick.contentX = Math.max(0, flick.contentWidth - flick.width)
            },
            () => tapItem(findShown(findItem(editor, "editToolbarSlot"), "toolbarCellMore")),
            waitStep(() => blockMenuOpen(), "“More” on the cell toolbar opens the block menu"),
            () => {
                check(editor.blockMenu.row() === window.targetRow && !editor.blockMenu.listItem, "it is the table's menu")
                check(editor.editingTable === null && page.selectedRow === window.targetRow,
                      "the cell's editing ended, the table is selected")
            },
            ...copySteps("copyBlockRef", "“Block reference” of a table copies ((id))",
                         () => "((" + info(window.targetRow).blockId + "))"),
            () => {
                Theme.forceTouch = DevTools.touch
                page.selectedRow = -1
                const item = delegateAt(window.targetRow)
                if (item)
                    item.focusContent()
            },
            waitStep(() => editor.editingTable !== null && editor.editingTable.cellInput !== null
                           && editor.editingTable.cellInput.activeFocus && page.selectedRow < 0,
                     "a cell is edited (nothing selected)"),
            () => {
                window.lastNotice = ""
                EditorInput.sendKey(editor.editingTable.cellInput, Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
            },
            waitStep(() => window.lastNotice.endsWith(" ((" + info(window.targetRow).blockId + "))"),
                     () => "Ctrl+Shift+C in a cell copies its table's reference: " + window.lastNotice),
            () => {
                // The keyboard goes elsewhere (the palette's field): the cell's editing ends and the
                // table stays the current block.
                window.focusProbe = Qt.createQmlObject('import QtQuick; TextInput { width: 10; height: 10 }',
                                                       window.contentItem, "focusProbe")
                window.focusProbe.forceActiveFocus()
            },
            waitStep(() => editor.editingTable === null && page.selectedRow === window.targetRow,
                     () => "a cell left for another field leaves its table selected: " + page.selectedRow),
            () => {
                window.expectedLink = page.linkText(window.targetRow, "blockLink").text
                check(window.expectedLink.indexOf("#^" + info(window.targetRow).blockId + "]]") > 0, "the table's link: " + window.expectedLink)
                editor.copyCurrentLink("blockLink") // the palette's "Kopiuj link do bloku"
            },
            waitStep(() => clipboardText() === window.expectedLink,
                     () => "the palette then copies the table's link: " + clipboardText()),
            () => {
                window.focusProbe.destroy()
                window.focusProbe = null
                page.selectedRow = -1
                editor.listView.forceActiveFocus()
            },
        ])
        if (!touch) {
            s = s.concat([
                () => {
                    window.targetRow = viewRow("calc")
                    editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
                },
                () => {
                    const item = delegateAt(window.targetRow)
                    rightClick(item, item.width / 2, item.height / 2)
                },
                waitStep(() => blockMenuOpen() && page.editingRow < 0, "a right-click on a calc block opens the block menu (no editing)"),
                () => taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1),
                waitStep(() => !blockMenuOpen(), "Escape closes it"),
                // ---- The title drawn above the properties: the menu of the block drawn there ---------
                () => { page.path = host.recipePath },
                waitStep(() => page.loaded && page.titleRow > 0 && editor.titleFirst && renderedOf(page.titleRow) !== null,
                         "the recipe (properties, then its title) is open"),
                () => editor.listView.positionViewAtBeginning(),
                () => {
                    const text = renderedOf(page.titleRow)
                    check(text !== null && text.mapToItem(null, 0, 0).y < delegateAt(page.titleRow - 1).mapToItem(null, 0, 0).y,
                          "the title is drawn above the properties")
                    if (text)
                        rightClick(text, text.width / 4, text.height / 2)
                },
                waitStep(() => blockMenuOpen() && editor.blockMenu.heading && editor.blockMenu.row() === page.titleRow,
                         "a right-click on that title opens the title's block menu"),
                () => taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1),
                // Gone, its closing transition too: a menu the next click opened would be visible at once.
                waitStep(() => !editor.blockMenu.visible, "Escape closes it"),
                () => {
                    const properties = delegateAt(page.titleRow - 1)
                    rightClick(properties, properties.width - 8, properties.height - 4)
                },
                () => check(!editor.blockMenu.visible, "a right-click on the properties drawn below it opens no block menu"),
            ])
        }
        steps = s
        stepIndex = 0
        stepTimer.start()
    }
    function renderedOf(row) {
        return renderedIn(delegateAt(row))
    }

    // ---- The touch toolbar on a simulated keyboard (`--editor-script keyboard`) -----------------
    // Run with --touch --keyboard 0 (ctest tst_editor_qml_keyboard_touch): the toolbar is docked on
    // the keyboard's top for a block (outline and document), the source and a table cell, follows
    // a keyboard that slides in frame by frame without scrolling until it has settled, stays at the
    // bottom when the system hides the keyboard while editing goes on; the caret, the cell and the
    // autocomplete stay above it.
    function toolbarSlot() {
        return findItem(editor, "editToolbarSlot")
    }
    function slotTop() {
        const slot = toolbarSlot()
        return slot ? slot.mapToItem(null, 0, 0).y : -1
    }
    function slotBottom() {
        const slot = toolbarSlot()
        return slot ? slot.mapToItem(null, 0, slot.height).y : -1
    }
    function near(a, b) {
        return Math.abs(a - b) <= 1
    }
    /// The toolbar is shown with its bottom edge on a keyboard of `px` (0: the window's bottom).
    function dockedOn(px) {
        const slot = toolbarSlot()
        return editor.toolbarVisible && slot !== null && slot.visible && slot.item !== null
               && near(slotBottom(), window.height - px)
    }
    function caretBottom() {
        const edit = editor.activeEditor
        if (!edit || !edit.activeFocus)
            return 1e9
        const r = edit.cursorRectangle
        return edit.mapToItem(null, r.x, r.y + r.height).y
    }
    function sourceCaretBottom() {
        const edit = editor.sourceTextEdit
        if (!edit)
            return 1e9
        const r = edit.cursorRectangle
        return edit.mapToItem(null, r.x, r.y + r.height).y
    }
    function cellBottom() {
        const input = editor.editingTable ? editor.editingTable.cellInput : null
        return input ? input.mapToItem(null, 0, input.height).y : 1e9
    }
    /// The shown item called `name` under `item` (hidden tool sets keep theirs), or null.
    function findShown(item, name) {
        if (!item || !item.visible)
            return null
        if (item.objectName === name)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findShown(item.children[i], name)
            if (found)
                return found
        }
        return null
    }
    function toolbarButton(name) {
        return findShown(toolbarSlot(), name)
    }
    /// Cell (r, c) is being edited, with the cell tools on the keyboard.
    function cellEdited(r, c) {
        const table = editor.editingTable
        return editor.toolbarMode === "cell" && table !== null && table.editRow === r && table.editColumn === c
               && table.cellInput !== null && table.cellInput.activeFocus && dockedOn(320)
               && toolbarButton("toolbarCellTools") !== null
    }
    /// A finger's tap at (x, y) of `item`.
    function fingerTap(item, x, y) {
        taskEvents.touchEvent(item).press(0, item, x, y).commit()
        taskEvents.touchEvent(item).release(0, item, x, y).commit()
    }
    property real contentBefore: 0
    property var toolsBefore: null
    property real toolsScroll: 0
    property int linesBefore: 0
    function keyboardTest() {
        taskEvents = Qt.createQmlObject('import QtTest; TestEvent {}', window, "keyboardEvents")
        const lh = editor.lineHeight
        steps = [
            () => {
                KeyboardInsets.simulated = 0
                // A text block at the end of the long page (the last rows may be a query or code).
                window.realRow = page.insertBlockAfter(page.lastVisibleRow(), "Ostatni punkt: zażółć gęślą jaźń")
                editor.startEditing(window.realRow, -1)
            },
            waitStep(() => dockedOn(0) && editor.activeEditor !== null && editor.activeEditor.activeFocus,
                     () => "no keyboard: the toolbar is docked on the editor's bottom edge: " + slotBottom() + " = " + window.height),
            // (a) The keyboard comes up: the toolbar sits on it, the caret above the toolbar.
            () => KeyboardInsets.simulated = 320,
            waitStep(() => dockedOn(320), () => "the toolbar sits on the keyboard: " + slotBottom() + " = " + (window.height - 320)),
            waitStep(() => caretBottom() <= slotTop() - lh / 2,
                     () => "the caret of the last block is scrolled above the toolbar: " + caretBottom() + " ≤ " + slotTop()),
            // (c) Typing with real keys: no runaway scrolling, the caret stays visible.
            () => {
                window.contentBefore = editor.listView.contentY
                window.linesBefore = editor.activeEditor.lineCount
                typeKeys(" zażółć gęślą jaźń, Łódź i Kraków")
            },
            waitStep(() => caretBottom() <= slotTop() && editor.listView.contentY - window.contentBefore
                           <= (editor.activeEditor.lineCount - window.linesBefore) * lh + 1,
                     () => "typing keeps the caret above the toolbar, scrolling at most a line per new line: "
                           + (editor.listView.contentY - window.contentBefore) + " px for "
                           + (editor.activeEditor.lineCount - window.linesBefore) + " line(s)"),
            () => {
                window.contentBefore = editor.listView.contentY
                typeKeys("ąę")
            },
            () => check(near(editor.listView.contentY, window.contentBefore) && caretBottom() <= slotTop(),
                        "typing on the same line does not scroll"),
            () => check(findAll(editor, "editToolbarSlot").length === 1, "one toolbar"),
            // (d) The system hides the keyboard (Back) while the block is still being edited.
            () => KeyboardInsets.simulated = 0,
            waitStep(() => dockedOn(0) && page.editingRow === window.realRow,
                     "the keyboard hidden by the system: the toolbar stays docked at the bottom, editing goes on"),
            // (e) The keyboard slides in frame by frame: the toolbar follows each frame, nothing
            // scrolls until it has settled. (The caret low on the screen first: the keyboard will
            // cover it.)
            () => {
                const list = editor.listView
                list.contentY = Math.max(list.originY, list.contentY - ((window.height - 150) - caretBottom()))
            },
            waitStep(() => caretBottom() > window.height - 320 && caretBottom() < window.height - editor.toolbarHeight,
                     () => "the caret is low on the screen, where the keyboard will come: " + caretBottom()),
            () => {
                window.contentBefore = editor.listView.contentY
                KeyboardInsets.simulate(100, true)
            },
            () => check(dockedOn(100) && near(editor.listView.contentY, window.contentBefore),
                        "100 px into the slide: the toolbar follows, the text does not move"),
            () => KeyboardInsets.simulate(200, true),
            () => check(dockedOn(200) && near(editor.listView.contentY, window.contentBefore),
                        "200 px into the slide: the toolbar follows, the text does not move"),
            () => KeyboardInsets.simulate(320, true),
            () => check(dockedOn(320) && near(editor.listView.contentY, window.contentBefore) && KeyboardInsets.animating,
                        "320 px, still sliding: the toolbar follows, the text does not move"),
            () => KeyboardInsets.simulate(320, false),
            waitStep(() => caretBottom() <= slotTop() - lh / 2,
                     () => "the keyboard settled: the caret is scrolled above the toolbar: " + caretBottom() + " ≤ " + slotTop()),
            // (i) Autocomplete near the keyboard stays above the toolbar.
            () => {
                const row = page.insertBlockAfter(page.lastVisibleRow(), "")
                editor.startEditing(row, -1)
            },
            waitStep(() => editor.activeEditor !== null && editor.activeEditor.activeFocus && dockedOn(320),
                     "a new block at the end is edited with the toolbar on the keyboard"),
            () => typeKeys("Zobacz [[Re"),
            waitStep(() => editor.suggestionsOpen && editor.suggestions.opened, "[[ near the keyboard opens the suggestions"),
            waitStep(() => {
                const popup = editor.suggestions
                return editor.mapToItem(null, 0, popup.y + popup.height).y <= slotTop() + 1
            }, () => "the suggestions are above the toolbar: "
                     + editor.mapToItem(null, 0, editor.suggestions.y + editor.suggestions.height).y + " ≤ " + slotTop()),
            () => {
                editor.closeSuggestions()
                page.undo()
                // The tools scrolled to their end before the editing stops.
                const flick = toolbarButton("editToolbarFlick")
                flick.contentX = flick.contentWidth - flick.width
                window.toolsScroll = flick.contentX
                window.toolsBefore = toolbarButton("toolbarBlockTools")
                page.stopEditing()
            },
            waitStep(() => !editor.toolbarVisible, "stopping the editing takes the toolbar away"),
            // The next edit shows the same tools (not made again), where they were scrolled to.
            () => editor.startEditing(window.realRow, -1),
            waitStep(() => editor.toolbarMode === "block" && dockedOn(320) && toolbarButton("toolbarBlockTools") !== null,
                     "editing again brings the toolbar back"),
            () => check(window.toolsScroll > 0 && toolbarButton("toolbarBlockTools") === window.toolsBefore
                        && near(toolbarButton("editToolbarFlick").contentX, window.toolsScroll),
                        "the block tools are kept between two edits, scrolled where they were: "
                        + toolbarButton("editToolbarFlick").contentX + " = " + window.toolsScroll),
            () => page.stopEditing(),
            waitStep(() => !editor.toolbarVisible, "the editing stops again"),
            // (g) Source mode: the toolbar with the source tools on the keyboard.
            () => editor.toggleSourceMode(),
            waitStep(() => page.sourceMode && editor.sourceTextEdit !== null && editor.sourceTextEdit.activeFocus,
                     "source mode: the source has the keyboard"),
            waitStep(() => editor.toolbarMode === "source" && dockedOn(320) && toolbarButton("toolbarSourceTag") !== null,
                     "source mode: the toolbar with the source tools sits on the keyboard"),
            // A tap on the bar beside its buttons stays on the bar: it moved the caret to the
            // text under the bar.
            () => editor.sourceTextEdit.cursorPosition = 0,
            waitStep(() => sourceCaretBottom() < slotTop() / 2, "source mode: the caret at the start of the file"),
            () => {
                const slot = toolbarSlot()
                fingerTap(slot, slot.width - 24, slot.height / 2)
            },
            () => check(editor.toolbarMode === "source" && editor.sourceTextEdit.activeFocus
                        && editor.sourceTextEdit.cursorPosition === 0,
                        "source mode: a tap on the bar beside its buttons leaves the caret where it was: "
                        + editor.sourceTextEdit.cursorPosition),
            () => {
                const edit = editor.sourceTextEdit
                edit.cursorPosition = edit.length
            },
            waitStep(() => sourceCaretBottom() <= slotTop(),
                     () => "source mode: the caret at the end of the file is above the toolbar: " + sourceCaretBottom() + " ≤ " + slotTop()),
            () => {
                window.textBefore = EditorInput.plainText(editor.sourceTextEdit.textDocument)
                tapItem(toolbarButton("toolbarSourceTag"))
            },
            waitStep(() => EditorInput.plainText(editor.sourceTextEdit.textDocument).length === window.textBefore.length + 1
                           && page.sourceText.indexOf("#") >= 0,
                     "the toolbar's # goes into the source"),
            () => tapItem(toolbarButton("toolbarSourceUndo")),
            waitStep(() => EditorInput.plainText(editor.sourceTextEdit.textDocument) === window.textBefore,
                     "the toolbar's undo takes it back"),
            () => tapItem(toolbarButton("toolbarHideKeyboard")),
            waitStep(() => editor.toolbarMode === "" && !editor.sourceTextEdit.activeFocus,
                     "hide keyboard: the source gives up the keyboard and the toolbar goes"),
            () => editor.toggleSourceMode(),
            waitStep(() => !page.sourceMode, "back from source mode"),
            // (h) A table cell: the cell tools, the cell above the toolbar, the next cell.
            () => {
                window.realRow = viewRow("table")
                editor.listView.positionViewAtIndex(window.realRow, ListView.End)
            },
            () => {
                const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
                check(item !== null, "the table is on screen")
                if (item)
                    item.focusContent()
            },
            waitStep(() => editor.toolbarMode === "cell" && editor.editingTable !== null && editor.editingTable.cellInput !== null
                           && dockedOn(320) && toolbarButton("toolbarNextCell") !== null,
                     "a table cell: the toolbar with the cell tools sits on the keyboard"),
            waitStep(() => cellBottom() <= slotTop(), () => "the cell being edited is above the toolbar: " + cellBottom() + " ≤ " + slotTop()),
            () => {
                window.baseRow = editor.editingTable.editRow
                window.pasteRow = editor.editingTable.editColumn
            },
            // A word the keyboard is still composing (Android) goes into the cell left with the
            // toolbar's next cell.
            () => EditorInput.sendInputMethod(editor.editingTable.cellInput, "Kminek"),
            () => check(editor.editingTable.cellInput.preeditText === "Kminek", "a word composed in the cell is preedit"),
            () => tapItem(toolbarButton("toolbarNextCell")),
            waitStep(() => editor.editingTable !== null && editor.editingTable.cellInput !== null
                           && editor.editingTable.cellInput.activeFocus
                           && (editor.editingTable.editRow !== window.baseRow || editor.editingTable.editColumn !== window.pasteRow),
                     "the toolbar's next cell moves to the next cell"),
            () => check(editor.editingTable.rawText(window.baseRow, window.pasteRow).endsWith("Kminek"),
                        "the word being composed is kept in the cell left: " + editor.editingTable.rawText(window.baseRow, window.pasteRow)),
            // Undo and redo keep the cell edited (the toolbar and the keyboard stay).
            () => {
                const table = editor.editingTable
                window.baseRow = table.editRow
                window.pasteRow = table.editColumn
                window.textBefore = table.rawText(table.editRow, table.editColumn)
                EditorInput.sendInputMethod(table.cellInput, "Szczaw")
            },
            () => tapItem(toolbarButton("toolbarCellUndo")),
            waitStep(() => cellEdited(window.baseRow, window.pasteRow) && editor.editingTable.cellInput.text === window.textBefore
                           && editor.editingTable.rawText(window.baseRow, window.pasteRow) === window.textBefore,
                     () => "the toolbar's undo takes the cell's typing back (the composed word too), the cell stays edited: "
                           + (editor.editingTable && editor.editingTable.cellInput ? editor.editingTable.cellInput.text : "(no cell)")),
            () => tapItem(toolbarButton("toolbarCellRedo")),
            waitStep(() => cellEdited(window.baseRow, window.pasteRow)
                           && editor.editingTable.cellInput.text === window.textBefore + "Szczaw",
                     () => "the toolbar's redo brings it back, the cell stays edited: "
                           + (editor.editingTable && editor.editingTable.cellInput ? editor.editingTable.cellInput.text : "(no cell)")),
            () => tapItem(toolbarButton("toolbarCellUndo")),
            waitStep(() => cellEdited(window.baseRow, window.pasteRow) && editor.editingTable.cellInput.text === window.textBefore,
                     "undo again, still in the cell"),
            // Hide keyboard ends the cell's editing, with the word being composed.
            () => EditorInput.sendInputMethod(editor.editingTable.cellInput, "Rzodkiew"),
            () => tapItem(toolbarButton("toolbarHideKeyboard")),
            waitStep(() => editor.editingTable === null && !editor.toolbarVisible, "hide keyboard ends the cell's editing and the toolbar"),
            () => check(page.sourceText.indexOf("Rzodkiew") >= 0, "the word being composed is kept in the cell"),
            // (f) The document mode: the same docking.
            () => page.path = host.documentPath,
            waitStep(() => page.path === host.documentPath && page.loaded && page.documentMode, "the trip plan opens as a document"),
            () => {
                window.realRow = page.insertBlockAfter(page.lastVisibleRow(), "Na koniec: źródło przy schronisku")
                editor.startEditing(window.realRow, -1)
            },
            waitStep(() => dockedOn(320) && caretBottom() <= slotTop() - lh / 2,
                     () => "document mode: the toolbar on the keyboard, the caret above it: " + caretBottom() + " ≤ " + slotTop()),
            () => {
                page.stopEditing()
                KeyboardInsets.simulated = 0
            },
            waitStep(() => !editor.toolbarVisible && editor.height > window.height - 400, "the keyboard and the toolbar are gone"),
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
        case "clock":
            clockTest()
            break
        case "taskstatus":
            taskStatusTest()
            break
        case "keyboard":
            keyboardTest()
            break
        case "blocklinks":
            blockLinksTest()
            break
        case "selectiontest":
            selectionTestLoader.active = true
            break
        case "refreshtest":
            refreshTestLoader.active = true
            break
        case "datechips":
            dateChipTestLoader.active = true
            break
        case "link-flash": {
            // A copied [[Page#^id]] (shown as the page and the block's text) followed: the block
            // selected and flashing (screenshots; --delay 1600 lands in the flash).
            const target = rowOf("Piątek")
            const id = page.ensureBlockRef(target)
            const after = page.insertBlockAfter(rowOf("Niedziela"), "")
            page.setBody(after, "Zobacz [[Wyjazd w Tatry#^" + id + "]] i [[Wyjazd w Tatry#Plan]].")
            page.stopEditing()
            scriptLater.run(() => editor.activateLink(after, "page:Wyjazd%20w%20Tatry#%5E" + id))
            break
        }
        case "copylink-toolbar": {
            // Touch: the editing toolbar's "Więcej" — the block menu above the bar (screenshots).
            const row = rowOf("Kolejność")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            editor.startEditing(row, -1)
            scriptLater.run(() => {
                const flick = window.findItem(editor, "editToolbarFlick")
                const more = window.findItem(editor, "toolbarMore")
                if (!flick || !more)
                    return
                flick.contentX = Math.max(0, flick.contentWidth - flick.width)
                editor.openBlockMenuAbove(row, more)
            })
            break
        }
        case "copylink-table": {
            // Touch: a table cell's toolbar "Więcej" — the cell left, the table's block menu at it
            // (screenshots).
            const row = viewRow("table")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            const item = editor.listView.itemAtIndex(row) as BlockDelegate
            if (item)
                item.focusContent()
            scriptLater.run(() => {
                if (editor.editingTable)
                    editor.openTableBlockMenu(editor.editingTable)
            })
            break
        }
        case "copylink":
        case "copylink-document": {
            // The block menu with "Kopiuj odnośnik" open on a heading (screenshots).
            const row = rowOf(name === "copylink" ? "Koszty" : "Plan")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            scriptLater.run(() => {
                const item = editor.listView.itemAtIndex(row)
                if (!item)
                    return
                editor.openBlockMenu(row, item, item.contentLeft + Theme.space8, item.firstLineCenter + editor.lineHeight / 2)
                Qt.callLater(() => {
                    const entry = window.copyEntry()
                    if (entry)
                        entry.triggered()
                })
            })
            break
        }
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
        case "recurrence-menu":
        case "link-menu": {
            // The menu of a 🔁 rule / of a #tag (screenshots).
            const row = rowOf(name === "link-menu" ? "Kupić płytki" : "Podlać kwiaty")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            scriptLater.run(() => {
                const text = renderedIn(delegateAt(row))
                const p = text ? linkPointIn(text, name === "link-menu" ? "tag:" : "recur:") : null
                if (p && name === "link-menu")
                    editor.openBlockLinkMenu(row, text.linkAt(p.x, p.y), text, p.x, p.y)
                else if (p)
                    editor.openDateMenu(row, text.linkAt(p.x, p.y), text, p.x, p.y)
            })
            break
        }
        case "date-menu":
        case "date-menu-reminder": {
            // The menu of a 📅 / ⏰ chip (screenshots).
            const row = rowOf(name === "date-menu" ? "Kupić płytki" : "Zadzwonić do hydraulika")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            scriptLater.run(() => {
                const text = renderedIn(delegateAt(row))
                const p = text ? linkPointIn(text, name === "date-menu" ? "date:" : "reminder:") : null
                if (p)
                    editor.openDateMenu(row, text.linkAt(p.x, p.y), text, p.x, p.y)
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
        case "tag-pages": {
            // `#` offers pages too (a tag names a page): pages no tag names yet, with their folder.
            const row = page.insertBlockAfter(rowOf("Wywieźć gruz"), "")
            typeInto(row, "Porównać z #rem")
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
        case "selection": {
            // A block selection rising through levels (desktop: the floating tools; --touch: the
            // selection mode with its docked tools and grips) — screenshots.
            const from = rowOf("Zadzwonić do hydraulika")
            editor.listView.positionViewAtIndex(from, ListView.Center)
            if (Theme.touchUi)
                editor.selectBlock(from)
            page.selectRange(from, rowOf("Położyć hydroizolację"))
            editor.listView.forceActiveFocus()
            break
        }
        case "selection-picked": {
            // Blocks picked one by one with gaps between them (desktop: Ctrl+clicks and the
            // floating tools; --touch: the selection mode with "Pojedynczo" on) — screenshots.
            const first = rowOf("Zmierzyć ściany")
            editor.listView.positionViewAtIndex(rowOf("Zadzwonić do hydraulika"), ListView.Center)
            if (Theme.touchUi) {
                editor.selectBlock(first)
                editor.pickMode = true
            } else {
                page.selectedRow = first
            }
            page.toggleSelected(rowOf("Zadzwonić do hydraulika"))
            page.toggleSelected(rowOf("Skuć stare płytki"))
            editor.listView.forceActiveFocus()
            break
        }
        case "selection-document": {
            // Paragraphs, a heading and list items of a document selected (screenshots).
            const from = rowOf("Pierwszego dnia")
            if (Theme.touchUi)
                editor.selectBlock(from)
            page.selectRange(from, rowOf("Sobota"))
            editor.listView.forceActiveFocus()
            break
        }
        case "selection-menu": {
            // The block menu of a block in a selection: Kopiuj 3 bloki… (screenshots).
            const from = rowOf("Zadzwonić do hydraulika")
            const to = rowOf("Prace")
            page.selectRange(from, to)
            scriptLater.run(() => {
                const item = editor.listView.itemAtIndex(from)
                if (item)
                    editor.openBlockMenu(from, item, item.contentLeft + Theme.space8, item.firstLineCenter + editor.lineHeight / 2)
            })
            break
        }
        case "text-menu": {
            // The edited text's menu (desktop right-click): Wytnij, Kopiuj, Wklej, Zaznacz wszystko.
            const row = rowOf("Kupić płytki")
            editor.startEditing(row, -1)
            scriptLater.run(() => {
                const edit = editor.activeEditor
                if (!edit)
                    return
                edit.select(0, 11)
                editor.openTextMenu(edit, edit.cursorRectangle.x, edit.cursorRectangle.y + edit.cursorRectangle.height / 2)
            })
            break
        }
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
