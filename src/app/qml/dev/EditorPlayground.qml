pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic
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
// (clockTest()) and exits; `taskclock` the task menu's timer and dependency picker;
// `taskstatus` checks the task status menu and commands
// (taskStatusTest()) and exits; `keyboard` checks the touch toolbar on a simulated on-screen
// keyboard (keyboardTest(): KeyboardInsets.simulated) and exits; `blocklinks` checks copying links
// to blocks and headings and following them (blockLinksTest()) and exits; `refreshtest` checks
// that views follow what they depend on (RefreshSelfTest.qml) and exits; `datechips` checks the
// menu of date chips with real input (DateChipSelfTest.qml) and exits; `tabletest` checks a
// table's rows and columns with real input (TableSelfTest.qml) and exits; `linkpartstest` checks the
// path parts of [[Folder/Page]] links with real input (LinkPartsSelfTest.qml) and exits, `linkparts`
// / `linkparts-menu` show them (a part hovered / a folder part's menu); `propstest` checks the
// page properties panel with real input (PropertiesSelfTest.qml) and exits; `readingtest` checks
// reading mode with real input (ReadingSelfTest.qml) and exits; `spelltest` checks spell
// checking's marks and menu with real input on word lists (SpellingSelfTest.qml) and exits; `properties` /
// `properties-phone` show the panel expanded on a page with every type; `copylink` /
// `copylink-document` show the block menu's "Copy link" (outline / document),
// `copylink-toolbar` the menu from the touch toolbar's "More" (--touch), `copylink-table` a
// table's menu from its cell toolbar's "More" (--touch), `link-flash` a
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
        onNavigated: toasts.showToast(qsTr("Open: %1").arg(window.routeText()), "info", "", -1)
        onUrlOpened: (url) => toasts.showToast(qsTr("Open in browser: %1").arg("" + url), "info", "", -1)
    }

    /// What a followed link opens, for its toast (the host's last route).
    function routeText(): string {
        if (host.lastRoute.view === "tag")
            return "#" + host.lastRoute.tag
        if (host.lastRoute.path !== undefined && host.lastRoute.path !== "")
            return "" + host.lastRoute.path
        if (host.lastRoute.target !== undefined)
            return host.lastRoute.target + " " + qsTr("(new page)")
        return host.lastRoute.blockId !== undefined && host.lastRoute.blockId !== null ? "" + host.lastRoute.blockId : ""
    }
    /// The editor's message as a toast; its button runs the editor's action (EditorToastAction).
    function showMessage(text: string, kind: string, action: EditorToastAction): void {
        const id = toasts.showToast(text, kind, action ? action.text : "", -1)
        if (action)
            toastActions.value = Js.withKey(toastActions.value, "" + id, action)
    }
    /// The toasts' actions by toast id.
    DevRecord { id: toastActions }
    Connections {
        target: toasts
        function onActionTriggered(toastId: int) {
            const action = toastActions.objectAt("" + toastId) as EditorToastAction
            toastActions.value = Js.withoutKey(toastActions.value, "" + toastId)
            if (action)
                action.run()
        }
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
                objectName: "probeEditor"
                anchors.fill: parent
                controller: probePage
            }
        }
    }

    PageController {
        id: page
        onSaveFailed: toasts.showToast(qsTr("Could not save the page"), "error", "", -1)
        onExternallyChanged: (merged) => toasts.showToast(merged ? qsTr("Merged changes made on another device")
                                                                 : qsTr("The page changed outside the editor"), "info", "", -1)
    }

    Component.onCompleted: {
        host.populateSample()
        page.host = host
        page.path = DevTools.editorScript === "empty" ? "Nowa strona.md"
                  : DevTools.editorScript === "title" ? host.recipePath
                  : Js.startsWith(DevTools.editorScript, "document") || DevTools.editorScript === "copylink-document"
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
        onMessage: (text, kind, action) => window.showMessage(text, kind, action)
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
                        text: Js.indexOf(page.path, "/") > 0 ? Js.slice(page.path, 0, Js.lastIndexOf(page.path, "/")) : qsTr("Vault")
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
        // (Along the overlay's bottom edge, as anchors to it did.)
        x: 0
        y: parent ? parent.height - height : 0
        width: parent ? parent.width : 0
        height: KeyboardInsets.overlap
        sourceComponent: BKeyboardPlaceholder {}
    }

    // ---- Selecting and copying blocks (`--editor-script selectiontest`) -----------------------------
    // The self-test lives in SelectionSelfTest.qml (qml/dev): it uses the step runner below.
    Loader {
        id: selectionTestLoader
        active: false
        sourceComponent: SelectionSelfTest {
            appWindow: window
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
            appWindow: window
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
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- Spell checking (`--editor-script spelltest`) ------------------------------------------------
    Loader {
        id: spellingTestLoader
        active: false
        sourceComponent: SpellingSelfTest {
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- A table's rows and columns (`--editor-script tabletest`) --------------------------------
    Loader {
        id: tableTestLoader
        active: false
        sourceComponent: TableSelfTest {
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- Path parts of a link (`--editor-script linkpartstest`) ----------------------------------
    Loader {
        id: linkPartsTestLoader
        active: false
        sourceComponent: LinkPartsSelfTest {
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- Reading mode (`--editor-script readingtest`) --------------------------------------------
    Loader {
        id: readingTestLoader
        active: false
        sourceComponent: ReadingSelfTest {
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- The page properties panel (`--editor-script propstest`) --------------------------------
    Loader {
        id: propertiesTestLoader
        active: false
        sourceComponent: PropertiesSelfTest {
            appWindow: window
            editor: editor
            page: page
            host: host
        }
    }
    // ---- Scripted states for screenshots -----------------------------------------------------
    /// The first row whose text starts with `prefix`, or -1.
    function rowOf(prefix: string): int {
        for (let i = 0; i < page.blocks.count; ++i) {
            if (Js.indexOf(window.info(i).plainText, prefix) === 0)
                return i
        }
        return -1
    }
    /// The first row of the view `view` ("table", "query", …), or -1.
    function viewRow(view: string): int {
        for (let i = 0; i < page.blocks.count; ++i) {
            if (window.info(i).view === view)
                return i
        }
        return -1
    }
    /// Start editing `row` and type `text` into it through the inline editor (like a user).
    function typeInto(row: int, text: string): void {
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

    /// Runs runScriptLater() for `name` once, 400 ms later (a scripted state that waits for the
    /// list's layout).
    Timer {
        id: scriptLater
        property string name
        interval: 400
        function run(script: string): void {
            name = script
            start()
        }
        onTriggered: window.runScriptLater(name)
    }

    Timer {
        id: scriptTimer
        interval: 250
        onTriggered: window.runScript(DevTools.editorScript)
    }

    // ---- The self-tests' step runner ----------------------------------------------------------
    // A self-test is a list of steps run one per tick of stepTimer, then the app exits with the
    // failures as its exit code. A test's steps function (selfTestSteps(), SpellingSelfTest's
    // steps(), …) writes each step as `if (window.at()) …`: every tick runs the function once and
    // at() is true for one step only, the stepIndex-th. A step repeats by lowering stepIndex
    // (waiting(), waitStep()); steps that depend on a fixed condition (Theme.touchUi) are in an
    // `if` around them. Helpers that stand for several steps (SpellingSelfTest's editBlock(), …)
    // call at() for each of theirs.
    /// The test whose steps run: runSuite() calls its steps function.
    property string suite: ""
    property int stepIndex: 0
    /// The step at() is looking for in this tick, and how many steps it counted so far.
    property int stepTarget: 0
    property int stepCursor: 0
    property int failures: 0
    /// The step to run now has come (counts the steps; see the runner above).
    function at(): bool {
        return window.stepCursor++ === window.stepTarget
    }
    /// Theme.touchUi when the steps began: what decides which steps there are (a test that
    /// switches to touch sizes halfway keeps its steps).
    property bool touchRun: false
    /// Run the steps of `name` (a suite of runSuite()), then exit with the failures.
    function runSteps(name: string): void {
        touchRun = Theme.touchUi
        suite = name
        stepIndex = 0
        stepTimer.start()
    }
    /// One pass over the current suite's steps (stepTimer, through DevTest.callCatching).
    function runSuite(): void {
        switch (window.suite) {
        case "selftest":
            window.selfTestSteps()
            break
        case "clock":
            window.clockSteps()
            break
        case "taskstatus":
            window.taskStatusSteps()
            break
        case "taskclock":
            window.taskClockSteps()
            break
        case "keyboard":
            window.keyboardSteps()
            break
        case "blocklinks":
            window.blockLinksSteps()
            break
        case "selectiontest":
            (selectionTestLoader.item as SelectionSelfTest).steps()
            break
        case "refreshtest":
            (refreshTestLoader.item as RefreshSelfTest).steps()
            break
        case "datechips":
            (dateChipTestLoader.item as DateChipSelfTest).steps()
            break
        case "spelltest":
            (spellingTestLoader.item as SpellingSelfTest).steps()
            break
        case "tabletest":
            (tableTestLoader.item as TableSelfTest).steps()
            break
        case "propstest":
            (propertiesTestLoader.item as PropertiesSelfTest).steps()
            break
        case "linkpartstest":
            (linkPartsTestLoader.item as LinkPartsSelfTest).steps()
            break
        case "readingtest":
            (readingTestLoader.item as ReadingSelfTest).steps()
            break
        }
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
            window.inStep = true
            window.stepTarget = window.stepIndex
            window.stepCursor = 0
            const error = DevTest.callCatching(window, "runSuite")
            window.inStep = false
            if (error === "" && window.stepCursor <= window.stepTarget) {
                // No step left.
                stop()
                console.log("SELFTEST DONE failures=" + window.failures)
                Qt.exit(window.failures === 0 ? 0 : 1)
                return
            }
            if (error !== "")
                window.check(false, "step " + window.stepTarget + " threw: " + error)
            window.stepIndex += 1
        }
    }
    /// How many steps a wait for something asynchronous lasts at most: ~3 s, times
    /// BRULION_TEST_SLOWDOWN (ctest sets it, 25 in sanitizer builds: tests/CMakeLists.txt).
    readonly property int waitLimit: 25 * DevTools.testSlowdown
    /// Repeats of a step waiting for something asynchronous (a picture loading).
    property int waits: 0
    /// What waiting() was last given (the check after a wait reads it instead of the condition).
    property bool lastReady: false
    /// In a step that waits until `ready` (at most `limit` times): true while it is not — the
    /// step then ends at once and comes again on the next tick.
    function waitingUpTo(ready: bool, limit: int): bool {
        window.lastReady = ready
        if (!ready && window.waits++ < limit) {
            window.stepIndex -= 1
            return true
        }
        window.waits = 0
        return false
    }
    /// waitingUpTo() for waitLimit tries. A finger held down until a menu shows is a step of
    /// `if (window.at()) window.waiting(…)` (the step after it lifts the finger).
    function waiting(ready: bool): bool {
        return window.waitingUpTo(ready, window.waitLimit)
    }
    /// The check after a wait (waiting() false): what it waited for holds.
    function checkWait(what: string): void {
        window.check(window.lastReady, what)
    }
    /// A step that waits until `ready` (waiting()), then checks it. `what` is read on every try: a
    /// text that cannot be made before `ready` holds is a step of its own
    /// `if (window.at() && !window.waiting(ready)) window.check(ready, what)`.
    function waitStep(ready: bool, what: string): void {
        if (!window.waiting(ready))
            window.checkWait(what)
    }

    // ---- Self test: keyboard flows through the real delegates (`--editor-script selftest`) ----
    property int baseRow: -1
    property int pasteRow: -1
    property int countBefore: 0
    property int realRow: -1
    property int farRow: -1
    property string fileLinkBefore: ""
    /// The paragraph zoomed into for Enter at its end.
    property int zoomTextRow: -1
    property string textBefore: ""
    property real headerBefore: 0
    property real tableBefore: 0
    /// The date picker's fields in the reminder / date dialog (typed dates and times).
    property BTextField dateField: null
    property BTextField timeField: null
    property BBreadcrumb crumbsProbe: null
    /// Real mouse, touch and key events of the self-tests (QtTest's TestEvent; see DevEvents):
    /// zooming, pictures from the internet, the clock; taskEvents for the task status menu, the
    /// block links, the keyboard and every self-test of its own file (SpellingSelfTest, …).
    DevEvents { id: zoomEvents }
    DevEvents { id: webEvents }
    DevEvents { id: clockEvents }
    DevEvents { id: taskEventsObject }
    final readonly property DevEvents taskEvents: taskEventsObject
    /// A point mapped to another item (mapToItem()'s result, read through a typed property).
    property point mapped
    property point mapped2
    /// A picture's sourceSize (read through a typed property).
    property size pictureSize
    /// window.info(row): the fields of a block (one object, read again by every call).
    DevBlock { id: infoBlock }
    function check(ok: bool, what: string): void {
        if (!ok)
            failures += 1
        console.log((ok ? "SELFTEST PASS " : "SELFTEST FAIL ") + what)
    }
    function key(k: int, modifiers: int): bool {
        const edit = editor.activeEditor
        if (!edit || !edit.active)
            return false
        return editor.handleEditorKeyCode(k, modifiers, edit)
    }
    function info(row: int): DevBlock {
        infoBlock.read(page.blocks.get(row))
        return infoBlock
    }
    /// The delegate of `row` is laid out and wholly inside the list's view.
    function rowInView(row: int): bool {
        const list = editor.listView
        const item = list.itemAtIndex(row) as Item
        if (!item || !item.visible)
            return false
        window.mapped = item.mapToItem(list, 0, 0)
        return window.mapped.y >= -0.5 && window.mapped.y + item.height <= list.height + 0.5
    }
    // What an input method (Gboard) is told about the text around the caret.
    function surroundingText(edit: TextEdit): string {
        return edit ? "" + edit.inputMethodQuery(Qt.ImSurroundingText, null) : "<none>"
    }
    function imCursor(edit: TextEdit): int {
        return edit ? Js.toNumber("" + edit.inputMethodQuery(Qt.ImCursorPosition, null)) : -1
    }
    /// A picture from the internet's placeholder in `item` (BlockDelegate): inside its frame,
    /// the site named on its own line, the alt text (or the failure) at most 2 (3) lines, the
    /// buttons side by side when both fit.
    function checkWebPlaceholder(item: BlockDelegate, failed: bool): void {
        const view = window.embedViewOf(item)
        const frame = window.findItem(view, "embedPicture")
        const holder = window.findItem(view, "webPicturePlaceholder")
        const text = window.findItem(view, "webPictureText") as Text
        const site = window.findItem(view, "webPictureHost") as Text
        const show = window.findItem(view, "webPictureShow") as BButton
        const save = window.findItem(view, "webPictureSave")
        const siteName = window.embedHost(item, 0)
        const what = failed ? "a failed picture from the internet: " : "a picture from the internet waiting: "
        check(frame !== null && holder !== null && holder.visible && holder.y >= 0
              && holder.y + holder.height <= frame.height,
              what + "the placeholder fits its frame: " + (holder ? holder.y + "+" + holder.height : "-")
              + " in " + (frame ? frame.width + "×" + frame.height : "-"))
        check(site !== null && site.visible && siteName !== "" && Js.indexOf(site.text, siteName) >= 0 && site.lineCount === 1,
              what + "its site is named: " + (site ? site.text : "-"))
        check(window.webTextFits(text, failed),
              what + "the text above it has its lines: " + (text ? text.lineCount : "-"))
        check(show !== null && save !== null && holder !== null && show.visible
              && (show.text === (failed ? qsTranslate("EmbedView", "Try again") : qsTranslate("EmbedView", "Show")))
              && (show.width + save.width + Theme.space2 > holder.width || show.y === save.y),
              what + "its buttons are side by side: " + (show && save ? show.y + " / " + save.y : "-"))
    }
    /// The text above a picture from the internet's buttons: at most 2 lines (3 when it failed),
    /// cut when waiting.
    function webTextFits(text: Text, failed: bool): bool {
        if (text === null || !text.visible)
            return false
        if (failed)
            return text.lineCount <= 3
        return text.lineCount <= 2 && text.truncated
    }
    /// The host of the `index`-th embed of a delegate ("" without one).
    function embedHost(item: BlockDelegate, index: int): string {
        const name = item ? DevTest.valueAt(item.embeds, index + ".host") : undefined
        return name === undefined ? "" : "" + name
    }
    // Typed reads of the editor's parts: a property of another component's property is read
    // through a function of this file (docs/UI.md "Compiled QML").
    function suggestionModel(): SuggestionModel {
        return editor.suggestions.model
    }
    /// window.suggestionModel().get(row)[key] (undefined without it).
    function suggestionAt(row: int, key: string): var {
        return DevTest.valueAt(window.suggestionModel().get(row), key)
    }
    function listHeader(): Item {
        return editor.listView.headerItem
    }
    /// The item named `name` under `item` (depth first), or null.
    function findItem(item: Item, name: string): Item {
        return DevTest.findItem(item, name)
    }
    /// How many items named `name` are under `item`, and the `index`-th of them (DevTest).
    function countNamed(item: Item, name: string): int {
        return DevTest.countNamed(item, name)
    }
    function namedAt(item: Item, name: string, index: int): Item {
        return DevTest.namedAt(item, name, index)
    }
    /// item.children.length, item.children[index].
    function childCount(item: Item): int {
        return DevTest.childCount(item)
    }
    function childAt(item: Item, index: int): Item {
        return DevTest.childAt(item, index)
    }
    /// The Text under `item` that shows `text`, or null.
    function textShowing(item: Item, text: string): Text {
        return DevTest.findText(item, text) as Text
    }
    /// The visible Text under `item` whose text contains `needle`, or null.
    function textContaining(item: Item, needle: string): Item {
        return DevTest.findTextContaining(item, needle)
    }
    /// The first item under `item` (itself included) with a property `name` equal to `value`
    /// (visible if `visibleOnly`), or null.
    function findWhere(item: Item, name: string, value: string, visibleOnly: bool): Item {
        return DevTest.findWhere(item, name, value, visibleOnly)
    }
    /// The first item under `item` (itself included) that has every member of `names` (a property,
    /// a function), or null.
    function findWith(item: Item, names: list<string>): Item {
        return DevTest.findWithMembers(item, names)
    }
    Component {
        id: plainTextComponent
        Text { textFormat: Text.PlainText }
    }
    /// `label` is as wide as `text` written out in plain text (markup not interpreted).
    function showsAsWritten(label: Text, text: string): bool {
        if (!label)
            return false
        const plain = plainTextComponent.createObject(window.contentItem) as Text
        plain.font = label.font
        plain.text = text
        const same = Math.abs(plain.implicitWidth - label.implicitWidth) < 1
        plain.destroy()
        return same
    }
    /// A real key press (through the window, Keys handlers and TextEdit) to the focused item.
    function realKey(k: int, modifiers: int, text: string): void {
        EditorInput.sendKey(editor.activeEditor ? editor.activeEditor : editor, k, modifiers, text)
    }
    /// Type `s` with real key events, one character at a time.
    function typeKeys(s: string): void {
        for (let i = 0; i < s.length; ++i)
            EditorInput.sendKey(editor.activeEditor ? editor.activeEditor : editor, 0, 0, s[i])
    }
    function selfTest(): void {
        baseRow = rowOf("Wywieźć gruz")
        runSteps("selftest")
    }
    /// The steps of selfTestSteps (see the step runner).
    function selfTestSteps(): void {
        window.selfTestOutline()
        window.selfTestKeys()
        window.selfTestDialogs()
        window.selfTestQueries()
        window.selfTestDocument()
        window.selfTestProbes()
    }
    function selfTestOutline(): void {
        const r = window.baseRow
        if (window.at()) editor.startEditing(r, -1)
        if (window.at()) check(editor.activeEditor && editor.activeEditor.row === r && editor.activeEditor.activeFocus, "editing starts with focus")
        if (window.at()) check(key(Qt.Key_Return, Qt.NoModifier), "Enter handled")
        if (window.at()) {
            check(page.editingRow === r + 1, "Enter splits: caret in the new block")
            check(info(r + 1).taskStatus === "todo", "the new block continues the task")
            check(editor.activeEditor && editor.activeEditor.row === r + 1 && editor.activeEditor.activeFocus, "new block has focus")
        }
        // The keyboard capitalises a sentence start from the text around the caret: the
        // hidden task marker must not be part of it (an empty text, caret at 0).
        if (window.at()) check(surroundingText(editor.activeEditor) === "" && imCursor(editor.activeEditor) === 0,
                    "a new task block shows the keyboard an empty text: \"" + surroundingText(editor.activeEditor)
                    + "\" at " + imCursor(editor.activeEditor))
        if (window.at()) editor.activeEditor.insert(editor.activeEditor.cursorPosition, "Nowe zadanie ąę")
        if (window.at()) check(info(r + 1).plainText === "Nowe zadanie ąę", "typing reaches the model: " + info(r + 1).plainText)
        if (window.at()) check(surroundingText(editor.activeEditor) === "Nowe zadanie ąę",
                    "the keyboard sees the task's text without its marker: " + surroundingText(editor.activeEditor))
        if (window.at()) { key(Qt.Key_Tab, Qt.NoModifier) }
        if (window.at()) check(info(r + 1).depth === info(r).depth + 1, "Tab indents")
        if (window.at()) { key(Qt.Key_Backtab, Qt.NoModifier) }
        if (window.at()) check(info(r + 1).depth === info(r).depth, "Shift+Tab outdents")
        if (window.at()) { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Backspace, Qt.NoModifier) }
        if (window.at()) check(info(r + 1).taskStatus === "" && page.editingRow === r + 1, "Backspace at start removes the checkbox first")
        if (window.at()) { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Backspace, Qt.NoModifier) }
        if (window.at()) {
            check(page.editingRow === r, "Backspace again merges with the previous block")
            check(Js.indexOf(info(r).plainText, "Nowe zadanie ąę") > 0, "merged text: " + info(r).plainText)
        }
        if (window.at()) { key(Qt.Key_Z, Qt.ControlModifier) }
        if (window.at()) check(info(r + 1).plainText === "Nowe zadanie ąę", "Ctrl+Z undoes the merge")
        if (window.at()) { key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier) }
        if (window.at()) check(Js.indexOf(info(r).plainText, "Nowe zadanie ąę") > 0, "Ctrl+Shift+Z redoes")
        if (window.at()) { key(Qt.Key_Return, Qt.ControlModifier) }
        if (window.at()) check(info(r).taskStatus === "doing", "Ctrl+Enter cycles the task: " + info(r).taskStatus)
        if (window.at()) { editor.activeEditor.cursorPosition = 0; key(Qt.Key_Up, Qt.NoModifier) }
        if (window.at()) check(page.editingRow === page.previousVisibleRow(r), "Up on the first line goes to the previous block")
        if (window.at()) { key(Qt.Key_Down, Qt.NoModifier) }
        if (window.at()) check(page.editingRow === r, "Down on the last line comes back")
        if (window.at()) { key(Qt.Key_Up, Qt.AltModifier) }
        if (window.at()) check(page.editingRow === r - 1 && Js.indexOf(info(r - 1).plainText, "Wywieźć") === 0, "Alt+Up moves the block")
        if (window.at()) { key(Qt.Key_Down, Qt.AltModifier) }
        if (window.at()) check(page.editingRow === r, "Alt+Down moves it back")
        if (window.at()) {
            const e = editor.activeEditor
            e.cursorPosition = e.length
            e.insert(e.length, " [[Rem")
        }
        if (window.at()) {
            check(editor.suggestions.visible && window.suggestionModel().count > 0, "[[ opens page suggestions")
            // Rows built ahead asynchronously and a keystroke resetting the model: Qt warned
            // "DelegateModel::cancel: index out range" (tst_editor_suggestions).
            const list = findItem(editor.suggestions.contentItem, "suggestionList") as ListView
            check(list !== null && list.cacheBuffer === 0, "the suggestion list builds no rows ahead of its viewport")
        }
        if (window.at()) { key(Qt.Key_Return, Qt.NoModifier) }
        if (window.at()) check(Js.indexOf(info(r).plainText, "Remont kuchni") > 0 && !editor.suggestions.opened, "Enter accepts a suggestion: " + info(r).plainText)
        // "Create page" leads the list (always in view), but Enter takes the existing page
        // (above) and Up reaches "Create page".
        if (window.at()) {
            const e = editor.activeEditor
            e.cursorPosition = e.length
            e.insert(e.length, " [[Remo")
        }
        if (window.at()) {
            const popup = editor.suggestions
            const list = findItem(popup.contentItem, "suggestionList") as ListView
            check(popup.visible && window.suggestionAt(0, "kind") === "create" && window.suggestionAt(0, "title") === "Remo"
                  && window.suggestionModel().count > 1 && popup.currentIndex === 1 && window.suggestionAt(1, "kind") === "page"
                  && list.indexAt(1, list.contentY + 1) === 0,
                  "Create page first, the first page highlighted: " + popup.currentIndex + " "
                  + Js.stringify(window.suggestionModel().get(0)))
        }
        if (window.at()) { key(Qt.Key_Up, Qt.NoModifier) }
        if (window.at()) check(editor.suggestions.currentIndex === 0, "Up reaches Create page: " + editor.suggestions.currentIndex)
        if (window.at()) { key(Qt.Key_Return, Qt.NoModifier) }
        if (window.at()) check(Js.indexOf(editor.activeEditor.text, "[[Remont kuchni]] [[Remo]]") > 0 && !editor.suggestions.opened,
                    "Up + Enter links the new page: " + editor.activeEditor.text)
        // `#` offers pages too (a tag names a page, docs/SYNTAX.md §3) — also after a written
        // date on the line (here the merged "⏳ 2026-10-18") —; a name with a space is written
        // #[[…]].
        if (window.at()) {
            const e = editor.activeEditor
            e.forceActiveFocus()
            e.cursorPosition = e.length
            e.insert(e.length, " #remont")
        }
        if (window.at()) check(editor.suggestions.visible && DevTest.valueAt(editor.suggestions.trigger, "mode") === "tags"
                    && window.suggestionModel().count > 0 && window.suggestionAt(0, "icon") === "file-text",
                    "# offers pages next to the tags: " + Js.stringify(window.suggestionModel().get(0)))
        if (window.at()) { key(Qt.Key_Return, Qt.NoModifier) }
        if (window.at()) check(Js.indexOf(editor.activeEditor.text, " #[[Remont ") > 0 && !editor.suggestions.opened,
                    "a page accepted after # is written #[[…]]: " + editor.activeEditor.text)
        if (window.at()) { key(Qt.Key_Escape, Qt.NoModifier) }
        if (window.at()) check(page.editingRow === -1 && page.selectedRow === r, "Esc stops editing and selects the block")
        if (window.at()) { editor.handleSelectionKeyCode(Qt.Key_Down, Qt.NoModifier) }
        if (window.at()) check(page.selectedRow === page.nextVisibleRow(r), "Down moves the selection")
        if (window.at()) { editor.handleSelectionKeyCode(Qt.Key_Return, Qt.NoModifier) }
        if (window.at()) check(page.editingRow === page.nextVisibleRow(r) && editor.activeEditor.activeFocus, "Enter edits the selected block")
        if (window.at()) { key(Qt.Key_Period, Qt.ControlModifier) }
        if (window.at()) { page.stopEditing(); page.toggleFold(rowOf("Przygotowanie")) }
        if (window.at()) check(!info(r).visible, "folding hides the children")
        if (window.at()) { page.toggleFold(rowOf("Przygotowanie")); editor.toggleSourceMode() }
        if (window.at()) check(page.sourceMode && page.sourceText.length > 100, "source mode shows the file")
        if (window.at()) { editor.toggleSourceMode() }
        if (window.at()) check(!page.sourceMode && page.blocks.count > 10, "back to blocks")
        // Zooming into a block and out again: the breadcrumb's room above the blocks goes
        // with it (an inactive Loader kept its height: a blank band above the first block).
        if (window.at()) {
            window.headerBefore = window.listHeader() ? window.listHeader().height : -1
            page.zoomInto(rowOf("Przygotowanie"))
        }
        if (window.at()) check(page.zoomBlock >= 0 && window.listHeader()
                    && window.listHeader().height > window.headerBefore,
                    "zooming in shows the breadcrumb above the blocks: " + window.headerBefore + " → "
                    + (window.listHeader() ? window.listHeader().height : -1))
        if (window.at()) page.zoomOut()
        if (window.at()) check(page.zoomBlock < 0 && window.listHeader()
                    && window.listHeader().height === window.headerBefore,
                    "zooming out leaves no room for the breadcrumb: "
                    + (window.listHeader() ? window.listHeader().height : -1) + " (was " + window.headerBefore + ")")
        // Enter at the end of a zoomed paragraph: a paragraph has no children, so the zoom
        // ends and the new block below it is edited — never a caret in a hidden block. Ctrl+Z
        // zooms back in, Ctrl+Shift+Z ends the zoom again.
        // (The view follows a zoom's change once the event loop is back — BlockEditor's zoomLater:
        // these steps wait for it, waitStep(), rather than for the next tick.)
        if (window.at()) {
            window.textBefore = page.sourceText
            window.zoomTextRow = rowOf("Plan remontu")
            page.zoomInto(window.zoomTextRow)
            editor.startEditing(window.zoomTextRow, -1)
        }
        if (window.at()) check(window.zoomTextRow > 0 && page.zoomBlock === window.zoomTextRow
                    && page.editingRow === window.zoomTextRow && editor.activeEditor !== null
                    && editor.activeEditor.activeFocus, "a zoomed paragraph is edited: " + window.zoomTextRow)
        if (window.at()) key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && info(page.editingRow).visible
                    && editor.activeEditor !== null && editor.activeEditor.activeFocus
                    && editor.activeEditor.row === page.editingRow && rowInView(page.editingRow),
                    "Enter at the end of a zoomed paragraph ends the zoom and edits the new block below it: zoom "
                    + page.zoomBlock + ", row " + page.editingRow + " (paragraph " + window.zoomTextRow + ")")
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) window.waitStep(page.zoomBlock === window.zoomTextRow && page.editingRow === window.zoomTextRow
                    && page.sourceText === window.textBefore && editor.activeEditor !== null
                    && editor.activeEditor.activeFocus,
                    "Ctrl+Z zooms back into the paragraph: zoom " + page.zoomBlock + ", row " + page.editingRow)
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier)
        if (window.at()) window.waitStep(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && editor.activeEditor !== null
                    && editor.activeEditor.activeFocus && editor.activeEditor.row === window.zoomTextRow + 1,
                    "Ctrl+Shift+Z ends the zoom again: zoom " + page.zoomBlock + ", row " + page.editingRow)
        if (window.at()) typeKeys("Źdźbło")
        if (window.at()) check(info(window.zoomTextRow + 1).plainText === "Źdźbło" && page.zoomBlock < 0,
                    "typing goes into the new block: " + info(window.zoomTextRow + 1).plainText)
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier) // the typing
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier) // Enter
        if (window.at()) {
            check(page.sourceText === window.textBefore && page.zoomBlock === window.zoomTextRow,
                  "two Ctrl+Z undo the typing and Enter, back in the zoom: zoom " + page.zoomBlock)
            page.zoomTo(-1)
            page.stopEditing()
        }
        if (window.at()) { window.pasteRow = page.insertBlockAfter(rowOf("Ekipa może"), ""); window.countBefore = page.blocks.count }
        if (window.at()) { const e = editor.activeEditor; e.insert(e.cursorPosition, "- [ ] pierwsze\n- drugie") }
        if (window.at()) {
            check(page.blocks.count === window.countBefore + 1, "pasted list becomes blocks")
            check(info(window.pasteRow).taskStatus === "todo" && info(window.pasteRow + 1).plainText === "drugie"
                  && page.editingRow === window.pasteRow + 1, "pasted blocks keep tasks, caret in the last one")
        }
    }
    function selfTestKeys(): void {
        // ---- Real key events (window -> Keys -> TextEdit), Polish text --------------------
        if (window.at()) { window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "") }
        if (window.at()) typeKeys("Żółw ąę")
        if (window.at()) check(info(window.realRow).plainText === "Żółw ąę", "typed keys reach the block: " + info(window.realRow).plainText)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.editingRow === window.realRow + 1 && editor.activeEditor && editor.activeEditor.activeFocus
                    && editor.activeEditor.row === window.realRow + 1, "real Enter splits and focuses the new block")
        if (window.at()) typeKeys("x")
        if (window.at()) { realKey(Qt.Key_Backspace, 0, ""); realKey(Qt.Key_Backspace, 0, "") }
        if (window.at()) check(page.editingRow === window.realRow && info(window.realRow).plainText === "Żółw ąę"
                    && editor.activeEditor && editor.activeEditor.activeFocus, "real Backspace deletes, then merges")
        if (window.at()) realKey(Qt.Key_Tab, 0, "")
        if (window.at()) check(info(window.realRow).depth === info(window.realRow - 1).depth + 1 && editor.activeEditor
                    && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow, "real Tab indents, focus stays")
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier, "")
        if (window.at()) check(info(window.realRow).depth === info(window.realRow - 1).depth && editor.activeEditor
                    && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow,
                    "real Ctrl+Z undoes the indent, focus stays")
        if (window.at()) { realKey(Qt.Key_Up, Qt.AltModifier, "") }
        if (window.at()) check(editor.activeEditor && editor.activeEditor.activeFocus && editor.activeEditor.row === window.realRow - 1
                    && info(window.realRow - 1).plainText === "Żółw ąę", "real Alt+Up moves the block, focus follows")
        if (window.at()) { realKey(Qt.Key_Down, Qt.AltModifier, "") }
        // ---- No-break spaces survive editing (TextEdit.text would turn them into spaces) ----
        if (window.at()) { page.setBody(window.realRow, "w\u00a0domu i\u00a0w\u00a0pracy"); editor.startEditing(window.realRow, -1) }
        if (window.at()) typeKeys("!")
        if (window.at()) check(Js.indexOf(page.sourceText, "w\u00a0domu i\u00a0w\u00a0pracy!") >= 0, "no-break spaces are kept when typing")
        // ---- Input method composition (Android keyboards compose every word) ---------------
        if (window.at()) { window.realRow = page.insertBlockAfter(window.realRow, ""); window.countBefore = page.blocks.count }
        // A block is prose: the keyboard capitalises its first letter (unlike the date fields).
        if (window.at()) check(editor.activeEditor && (editor.activeEditor.inputMethodHints & Qt.ImhNoAutoUppercase) === 0
                    && surroundingText(editor.activeEditor) === "" && imCursor(editor.activeEditor) === 0,
                    "a new block keeps the keyboard's automatic capital letter: \""
                    + surroundingText(editor.activeEditor) + "\" at " + imCursor(editor.activeEditor))
        if (window.at()) EditorInput.sendInputMethod(editor.activeEditor, "kot")
        if (window.at()) check(editor.activeEditor.preeditText === "kot", "composing text is preedit")
        if (window.at()) realKey(Qt.Key_Backspace, 0, "")
        if (window.at()) check(page.blocks.count === window.countBefore && page.editingRow === window.realRow
                    && info(window.realRow).plainText === "ko", "Backspace while composing edits the word, never merges: " + info(window.realRow).plainText)
        if (window.at()) EditorInput.sendInputMethod(editor.activeEditor, "pies")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(info(window.realRow).plainText === "kopies" && page.editingRow === window.realRow + 1,
                    "Enter while composing keeps the word and splits after it: " + info(window.realRow).plainText)
        if (window.at()) EditorInput.sendInputMethod(editor.activeEditor, "", "[[")
        if (window.at()) EditorInput.sendInputMethod(editor.activeEditor, "Rem")
        if (window.at()) check(editor.suggestions.visible && window.suggestionModel().count > 0, "completions follow a composed word")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(Js.indexOf(info(window.realRow + 1).plainText, "Remont kuchni") >= 0 && editor.activeEditor.preeditText === "",
                    "accepting a completion commits the composed word: " + info(window.realRow + 1).plainText)
        // ---- Typing after the edited block scrolled far away ---------------------------------
        if (window.at()) {
            let md = ""
            for (let i = 0; i < 150; ++i)
                md += "- Wiersz " + i + "\n"
            page.insertMarkdown(page.lastVisibleRow(), md)
            editor.listView.positionViewAtBeginning()
        }
        // A page of fewer than wholePageRows rows is created whole: its last row exists without
        // the view going there, filled in the background.
        if (window.at()) check(editor.wholePage && editor.listView.cacheBuffer >= editor.wholePageCache,
                               "a page of " + page.blocks.count + " rows is created whole")
        if (window.at()) waitStep(editor.listView.itemAtIndex(page.blocks.count - 1) !== null,
                                  "the last row of a page created whole exists without scrolling to it")
        // The lazy list of a longer page: rows far from the view are released.
        if (window.at()) {
            editor.wholePageRows = 0
            window.farRow = rowOf("Kupić płytki")
            editor.startEditing(window.farRow, -1)
        }
        if (window.at()) check(!editor.wholePage && editor.listView.cacheBuffer <= 1601,
                               "a page past wholePageRows has a cache of about two screens")
        if (window.at()) editor.listView.positionViewAtEnd()
        if (window.at()) check(editor.listView.itemAtIndex(window.farRow) === null, "the edited block's delegate is gone")
        if (window.at()) realKey(Qt.Key_Control, Qt.ControlModifier, "")
        if (window.at()) check(editor.listView.itemAtIndex(window.farRow) === null, "a lone modifier key does not scroll back")
        if (window.at()) typeKeys("Q")
        if (window.at()) check(Js.indexOf(info(window.farRow).plainText, "Q") > 0 && page.editingRow === window.farRow
                    && editor.activeEditor && editor.activeEditor.activeFocus, "typing brings the edited block back: " + info(window.farRow).plainText)
        if (window.at()) editor.wholePageRows = 1000
        // Enter at the end of a zoomed heading far down the page: the rows above it come back
        // with the zoom's end, and the view follows the new block below it (it does not stay
        // at the page's top, where the zoom had shown the heading).
        if (window.at()) {
            window.zoomTextRow = page.insertMarkdown(page.lastVisibleRow(), "## Na dole strony ąę")
            page.zoomInto(window.zoomTextRow)
            editor.startEditing(window.zoomTextRow, -1)
        }
        if (window.at()) check(page.zoomBlock === window.zoomTextRow && info(window.zoomTextRow).view === "heading"
                    && page.editingRow === window.zoomTextRow && rowInView(window.zoomTextRow),
                    "a zoomed heading far down the page is edited: " + window.zoomTextRow)
        if (window.at()) key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) window.waitStep(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && editor.activeEditor !== null
                    && editor.activeEditor.activeFocus && rowInView(window.zoomTextRow + 1)
                    && rowInView(window.zoomTextRow),
                    "Enter at the end of a zoomed heading far down the page keeps it and the new block in view: zoom "
                    + page.zoomBlock + ", row " + page.editingRow + ", y "
                    + (editor.listView.itemAtIndex(window.zoomTextRow + 1)
                       ? window.mappedY(editor.listView.itemAtIndex(window.zoomTextRow + 1) as Item, editor.listView, 0, 0) : "-"))
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier)
        if (window.at()) window.waitStep(page.zoomBlock === window.zoomTextRow && page.editingRow === window.zoomTextRow
                    && rowInView(window.zoomTextRow), "Ctrl+Z zooms back into the heading, in view")
        if (window.at()) key(Qt.Key_Z, Qt.ControlModifier) // the heading added above
        if (window.at()) {
            check(page.zoomBlock < 0 && rowOf("Na dole strony") < 0, "and the heading is gone with the zoom: " + page.zoomBlock)
            page.zoomTo(-1)
        }
        // Zooming out (the breadcrumb) while a block far down the page is edited: the rows
        // above it come back, the edited block stays in view.
        if (window.at()) {
            window.zoomTextRow = rowOf("Wiersz 140")
            page.zoomInto(window.zoomTextRow)
            editor.startEditing(window.zoomTextRow, -1)
        }
        if (window.at()) check(page.zoomBlock === window.zoomTextRow && page.editingRow === window.zoomTextRow
                    && rowInView(window.zoomTextRow), "a zoomed block far down the page is edited: " + window.zoomTextRow)
        if (window.at()) page.zoomOut()
        if (window.at()) window.waitStep(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow && rowInView(window.zoomTextRow)
                    && editor.activeEditor !== null && editor.activeEditor.activeFocus,
                    "zooming out keeps the edited block in view: y "
                    + (editor.listView.itemAtIndex(window.zoomTextRow)
                       ? window.mappedY(editor.listView.itemAtIndex(window.zoomTextRow) as Item, editor.listView, 0, 0) : "-")
                    + ", zoom " + page.zoomBlock + ", row " + page.editingRow + ", editor row "
                    + (editor.activeEditor ? editor.activeEditor.row : -1)
                    + ", contentY " + editor.listView.contentY + " (origin " + editor.listView.originY + ")")
        // The breadcrumb's arrow clicked with the mouse while a child of a folded zoom root is
        // edited (Enter in the zoom made it; the zoom root's children are shown whatever its
        // fold): the zoom ends, the fold opens over the child, which stays edited, in view,
        // and gets the keys typed next.
        if (window.at()) {
            page.stopEditing()
            window.textBefore = page.sourceText
            window.zoomTextRow = rowOf("Wiersz 145")
            page.zoomInto(window.zoomTextRow)
            editor.startEditing(window.zoomTextRow, -1)
        }
        if (window.at()) key(Qt.Key_Return, Qt.NoModifier)
        if (window.at()) typeKeys("Dziecko ąę")
        if (window.at()) page.setFolded(window.zoomTextRow, true)
        if (window.at()) check(page.zoomBlock === window.zoomTextRow && info(window.zoomTextRow).collapsed
                    && page.editingRow === window.zoomTextRow + 1 && info(window.zoomTextRow + 1).visible
                    && info(window.zoomTextRow + 1).plainText === "Dziecko ąę",
                    "a child of a folded zoom root is edited: zoom " + page.zoomBlock + ", row " + page.editingRow)
        if (window.at()) {
            const arrow = findWhere(window.listHeader(), "iconName", "arrow-left", true)
            check(arrow !== null, "the breadcrumb's arrow is there")
            if (!arrow)
                return
            // A finger on touch UIs, else the mouse.
            const x = arrow.width / 2
            const y = arrow.height / 2
            if (Theme.touchUi) {
                zoomEvents.touchPress(arrow, x, y)
                zoomEvents.touchRelease(arrow, x, y)
            } else {
                zoomEvents.mouseClick(arrow, x, y, Qt.LeftButton, Qt.NoModifier, -1)
            }
        }
        if (window.at()) window.waitStep(page.zoomBlock < 0 && page.editingRow === window.zoomTextRow + 1 && info(page.editingRow).visible
                    && !info(window.zoomTextRow).collapsed && rowInView(page.editingRow)
                    && editor.activeEditor !== null && editor.activeEditor.row === page.editingRow,
                    "the breadcrumb's arrow clicked: the zoom ends, the edited child stays shown and in view: zoom "
                    + page.zoomBlock + ", row " + page.editingRow + ", shown " + info(window.zoomTextRow + 1).visible)
        if (window.at()) EditorInput.sendKey(window.activeFocusItem, 0, 0, "ż")
        if (window.at()) check(info(window.zoomTextRow + 1).plainText === "Dziecko ąęż" && editor.activeEditor !== null
                    && editor.activeEditor.activeFocus,
                    "a key typed after the click goes into the edited child: " + info(window.zoomTextRow + 1).plainText
                    + " (focus " + window.activeFocusItem + ")")
        if (window.at()) {
            for (let i = 0; i < 6 && page.sourceText !== window.textBefore; ++i)
                page.undo()
            check(page.sourceText === window.textBefore, "undo takes the child away again")
            page.zoomTo(-1)
            page.stopEditing()
        }
    }
    function selfTestDialogs(): void {
        // ---- Table cells: typed text is in the document before the cell is left -------------
        // With a mouse, a table takes no room below its grid until it is selected or edited
        // (the "+ Row / + Column" buttons): its height never changes under a resting pointer.
        if (window.at()) {
            page.stopEditing()
            page.selectedRow = -1
            window.textBefore = page.sourceText
            const row = viewRow("table")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            const item = editor.listView.itemAtIndex(row) as BlockDelegate
            const table = item ? item.contentItem as TableBlockView : null
            window.tableBefore = table ? table.height : -1
            check(table !== null && table.height === (window.rowsOf(table).length + 1) * table.rowHeight,
                  "a table not being edited has no room below its grid: " + window.tableBefore)
            if (item)
                item.focusContent()
        }
        if (window.at()) {
            const item = editor.listView.itemAtIndex(viewRow("table")) as BlockDelegate
            const table = item ? item.contentItem as TableBlockView : null
            check(table !== null && table.height > window.tableBefore + 20,
                  "editing a cell shows the table's buttons below it: " + (table ? table.height : -1))
        }
        if (window.at()) typeKeys("źó")
        if (window.at()) check(page.sourceText !== window.textBefore && Js.indexOf(page.sourceText, "źó") > 0 && page.dirty,
                    "typing in a table cell reaches the document at once")
        if (window.at()) realKey(Qt.Key_Escape, 0, "")
        if (window.at()) check(page.sourceText === window.textBefore && page.selectedRow === viewRow("table"),
                    "Esc cancels the cell's edit: " + (page.sourceText === window.textBefore) + " " + page.selectedRow + "/" + viewRow("table")
                    + " canUndo=" + page.canUndo)
        // ---- A dialog acts on its block even when the page changed meanwhile ---------------
        if (window.at()) {
            page.save()
            window.farRow = rowOf("Zadzwonić do hydraulika")
            window.dialogs().createReminder(window.farRow, false)
        }
        if (window.at()) host.externalWrite(page.path, Js.replace(host.file(page.path), "Plan remontu", "- Dopisane z telefonu\n\nPlan remontu"))
        if (window.at()) window.chooseReminder(window.reminderBox(), "2026-10-16 08:30", false, "")
        if (window.at()) {
            const row = rowOf("Zadzwonić do hydraulika")
            const line = window.fileLineOf(page.sourceText, "Zadzwonić do hydraulika")
            check(row === window.farRow + 1 && Js.indexOf(line, "⏰ 2026-10-16 08:30") > 0
                  && window.splitText(page.sourceText, "⏰ 2026-10-16 08:30").length === 2,
                  "the reminder lands on its block after a sync shifted the rows: " + line)
            window.reminderBox().close()
        }
        if (window.at()) { page.save() }
        if (window.at()) check(!page.dirty && Js.indexOf(host.file(page.path), "[[Remont kuchni]]") > 0, "saved to the host")
        // ---- Dates and times typed in the dialogs count without their own Enter ------------
        // (Android's keyboard showed Next, a Tab, as the action key of these fields, so Enter
        // never came; a time typed but not confirmed was dropped on Save.)
        if (window.at()) {
            window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "Odebrać paczkę z paczkomatu")
            page.stopEditing()
            window.dialogs().createReminder(window.realRow, false)
            window.waits = 0
        }
        if (window.at()) {
            const d = window.reminderBox()
            // Opening takes its transition (it then focuses the dialog): wait for it.
            if (window.waitingUpTo(d.opened, 2 * window.waitLimit))
                return
            window.dateField = findItem(d.contentItem, "datePickerNatural") as BTextField
            window.timeField = findItem(d.contentItem, "datePickerTime") as BTextField
            check(d.opened && window.dateField !== null && window.timeField !== null && d.at === "2026-10-16 09:00",
                  "the reminder dialog has its date and time fields: " + d.at)
            check(window.dateField !== null && window.dateField.EnterKey.type === Qt.EnterKeyDone
                  && window.timeField !== null && window.timeField.EnterKey.type === Qt.EnterKeyDone,
                  "the fields' keyboard action key is Done (Enter), not Next")
            // Gboard wrote "Za 2 minuty" (in 2 minutes): a date in words is no sentence, so the keyboard
            // must not capitalise its first letter (the block editor itself still does).
            check(window.dateField !== null && (window.dateField.inputMethodHints & Qt.ImhNoAutoUppercase) !== 0,
                  "the typed date gets no automatic capital letter")
            window.dateField.forceActiveFocus()
        }
        if (window.at()) typeKeys("za 2 minuty")
        if (window.at()) check(window.dateField.text === "za 2 minuty" && window.reminderBox().at === "2026-10-16 09:00",
                    "a typed date waits for Enter: " + window.reminderBox().at)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(window.reminderBox().at === "2026-10-15 10:02" && window.dateField.text === "",
                    "Enter (the keyboard's Done) applies the typed date: " + window.reminderBox().at)
        if (window.at()) typeKeys("jutro 18:30")
        if (window.at()) window.timeField.forceActiveFocus()
        if (window.at()) check(window.reminderBox().at === "2026-10-16 18:30" && window.dateField.text === "",
                    "leaving the field applies the typed date: " + window.reminderBox().at)
        if (window.at()) { window.timeField.selectAll(); typeKeys("2346") }
        if (window.at()) check(window.timeField.activeFocus && window.reminderBox().at === "2026-10-16 18:30",
                    "a typed time waits while its field has the focus")
        // Save with the focus still in the field (Ctrl+Enter; a touch keyboard's Save tap).
        if (window.at()) window.reminderBox().tryAccept()
        if (window.at()) {
            const line = window.fileLineOf(page.sourceText, "Odebrać paczkę z paczkomatu")
            check(!window.reminderBox().opened && Js.indexOf(line, "⏰ 2026-10-16 23:46") > 0,
                  "Save applies the typed time: " + line)
        }
        if (window.at()) { window.dialogs().createReminder(window.realRow, false); window.waits = 0 }
        if (window.at()) {
            if (window.waitingUpTo(window.reminderBox().opened, 2 * window.waitLimit))
                return
            window.timeField.forceActiveFocus()
            window.timeField.selectAll()
            typeKeys("25:99")
        }
        if (window.at()) window.reminderBox().tryAccept()
        if (window.at()) check(window.reminderBox().opened && !window.reminderBox().acceptEnabled
                    && window.timeField.text === "25:99", "a typed time that is none keeps the dialog open")
        if (window.at()) window.dateField.forceActiveFocus()
        if (window.at()) check(window.timeField.text === "25:99" && !window.reminderBox().acceptEnabled,
                    "it stays in its field with the hint after leaving it")
        if (window.at()) { window.timeField.forceActiveFocus(); window.timeField.selectAll(); typeKeys("7") }
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) {
            const box = window.reminderBox()
            check(box.at === "2026-10-16 07:00" && box.acceptEnabled, "Enter in the time field applies a valid time: " + box.at)
        }
        if (window.at()) window.reminderBox().close()
        // The due-date dialog: a typed date counts on Save.
        if (window.at()) { window.dialogs().pickDate(window.realRow, "due"); window.waits = 0 }
        if (window.at()) {
            if (window.waitingUpTo(window.dateBox().opened, 2 * window.waitLimit))
                return
            window.dateField = findItem(window.dateBox().contentItem, "datePickerNatural") as BTextField
            if (window.dateField)
                window.dateField.forceActiveFocus()
        }
        if (window.at()) typeKeys("w sobotę")
        if (window.at()) window.dateBox().tryAccept()
        if (window.at()) {
            const line = window.fileLineOf(page.sourceText, "Odebrać paczkę z paczkomatu")
            check(!window.dateBox().opened && Js.indexOf(line, "📅 2026-10-17") > 0,
                  "the date dialog's Save applies the typed date: " + line)
        }
        if (window.at()) window.dialogs().pickRecurrence(window.realRow)
        if (window.at()) {
            const field = DevTest.objectProperty(window.recurrenceBox(), "ruleField") as BTextField
            check(field !== null && field.EnterKey.type === Qt.EnterKeyDone, "the recurrence rule's action key is Done")
            check(field !== null && (field.inputMethodHints & Qt.ImhNoAutoUppercase) !== 0,
                  "the typed recurrence rule gets no automatic capital letter")
            window.recurrenceBox().close()
        }
    }
    function selfTestQueries(): void {
        // ---- Live query results: tasks from the vault, checkable in their own files ----------
        if (window.at()) {
            const row = viewRow("query")
            editor.listView.positionViewAtIndex(row, ListView.Beginning)
        }
        if (window.at()) {
            const results = window.queryResults()
            check(results !== null && results.available && results.ok && results.rowCount() >= 4,
                  "the query block lists its results: " + (results ? results.rowCount() : -1))
            check(window.completeResult("Mleko") && Js.indexOf(host.file("Zakupy.md"), "- [x] Mleko") === 0,
                  "a result's checkbox completes the task in its file: " + window.lines(host.file("Zakupy.md"))[0])
        }
        if (window.at()) {
            // Embedded section and block: frames with the source's blocks.
            const row = rowOf("Hydraulik z kontaktów")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            const item = editor.listView.itemAtIndex(row) as BlockDelegate
            check(item !== null && DevTest.lengthAt(item.transclusions, "") === 1 && DevTest.valueAt(item.transclusions, "0.found") === true
                  && (DevTest.objectProperty(item, "transclusionView") as Item).height > 20, "a block embed shows the block in a frame")
            const section = info(rowOf("Budżet na październik") + 1)
            check(section.view === "text", "the embedded section's block is a text block")
        }
        // ---- Undo of a block inserted at the very start of a page (row 0) ----------------------
        // The caret goes back to the block it was in and every row shows its own block.
        if (window.at()) {
            page.save()
            host.setFile("Na początek.md", "- Zażółć gęślą\n- jaźń\n")
            page.path = "Na początek.md"
        }
        if (window.at()) editor.startEditing(0, 0)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.blocks.count === 3 && info(0).plainText === "" && page.editingRow === 1
                    && editor.activeEditor && editor.activeEditor.activeFocus && editor.activeEditor.row === 1
                    && editor.activeEditor.plain() === "Zażółć gęślą",
                    "real Enter at the start of the first block puts an empty block above it")
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier, "")
        if (window.at()) {
            check(page.sourceText === "- Zażółć gęślą\n- jaźń\n" && page.blocks.count === 2,
                  "real Ctrl+Z takes the block at the start away: " + Js.stringify(page.sourceText))
            check(page.editingRow === 0 && editor.activeEditor && editor.activeEditor.activeFocus
                  && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "Zażółć gęślą"
                  && editor.activeEditor.cursorPosition === 0,
                  "after Ctrl+Z the caret is at the start of the first block: row "
                  + window.editorState())
            const second = editor.listView.itemAtIndex(1) as Item
            check(second !== null && textContaining(second, "jaźń") !== null && textContaining(second, "Zażółć") === null,
                  "after Ctrl+Z the second row shows its own block")
        }
        if (window.at()) typeKeys("ą")
        if (window.at()) check(page.sourceText === "- ąZażółć gęślą\n- jaźń\n", "typing after Ctrl+Z goes into the first block: "
                    + Js.stringify(page.sourceText))
        if (window.at()) { realKey(Qt.Key_Z, Qt.ControlModifier, ""); editor.startEditing(1, 2) }
        if (window.at()) realKey(Qt.Key_Up, Qt.AltModifier, "")
        if (window.at()) check(page.sourceText === "- jaźń\n- Zażółć gęślą\n" && page.editingRow === 0 && editor.activeEditor
                    && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "jaźń",
                    "real Alt+Up moves the second block to the start of the page, edited: "
                    + Js.stringify(page.sourceText))
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier, "")
        if (window.at()) {
            check(page.sourceText === "- Zażółć gęślą\n- jaźń\n" && page.editingRow === 1 && editor.activeEditor
                  && editor.activeEditor.activeFocus && editor.activeEditor.row === 1 && editor.activeEditor.plain() === "jaźń"
                  && editor.activeEditor.cursorPosition === 2,
                  "real Ctrl+Z of that move puts the caret back where it was: row "
                  + window.editorState())
            const first = editor.listView.itemAtIndex(0) as Item
            check(first !== null && textContaining(first, "Zażółć") !== null && textContaining(first, "jaźń") === null,
                  "after Ctrl+Z the first row shows its own block")
        }
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier, "")
        if (window.at()) check(page.sourceText === "- jaźń\n- Zażółć gęślą\n" && page.editingRow === 0 && editor.activeEditor
                    && editor.activeEditor.activeFocus && editor.activeEditor.row === 0 && editor.activeEditor.plain() === "jaźń"
                    && editor.activeEditor.cursorPosition === 2,
                    "real Ctrl+Shift+Z moves it to the start again, edited")
        // A done task: the hidden "[x] " is no text, so Enter at the start of its text puts an
        // empty task above it and the text stays done.
        if (window.at()) {
            page.save()
            host.setFile("Zadanie na początek.md", "- [x] Zrobione źdźbło\n")
            page.path = "Zadanie na początek.md"
        }
        if (window.at()) editor.startEditing(0, 0)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.sourceText === "- [ ] \n- [x] Zrobione źdźbło\n" && info(0).taskStatus === "todo"
                    && info(0).plainText === "" && info(1).taskStatus === "done"
                    && info(1).plainText === "Zrobione źdźbło" && page.editingRow === 1
                    && editor.activeEditor && editor.activeEditor.row === 1
                    && editor.activeEditor.plain() === "Zrobione źdźbło",
                    "real Enter at the start of a done task's text puts an empty task above it: "
                    + Js.stringify(page.sourceText))
        // An empty paragraph is only blank lines in the file: Ctrl+Shift+Z brings it back.
        if (window.at()) {
            page.save()
            host.setFile("Akapit na początek.md", "---\nbrulion: document\n---\nPierwszy akapit źdźbła\n\nDrugi\n")
            page.path = "Akapit na początek.md"
        }
        if (window.at()) editor.startEditing(1, 0)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.blocks.count === 4 && info(1).kind === "paragraph" && info(1).plainText === ""
                    && page.editingRow === 2, "real Enter at the start of a paragraph puts an empty one above it")
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier, "")
        if (window.at()) check(page.blocks.count === 3 && page.editingRow === 1, "real Ctrl+Z takes the empty paragraph away")
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier | Qt.ShiftModifier, "")
        if (window.at()) check(page.blocks.count === 4 && info(1).kind === "paragraph" && info(1).plainText === ""
                    && page.editingRow === 2 && editor.activeEditor && editor.activeEditor.row === 2
                    && editor.activeEditor.plain() === "Pierwszy akapit źdźbła",
                    "real Ctrl+Shift+Z brings the empty paragraph back, the caret in the paragraph after it: rows "
                    + page.blocks.count + ", editing " + page.editingRow)
    }
    /// The visible children of a delegate `d` named `name` (indent guides): how many, the
    /// `index`-th, their x joined by ", ".
    function guideCount(d: Item, name: string): int {
        let n = 0
        for (let i = 0; i < window.childCount(d); ++i) {
            const child = window.childAt(d, i)
            if (child.objectName === name && child.visible)
                ++n
        }
        return n
    }
    function guideAt(d: Item, name: string, index: int): Item {
        let n = 0
        for (let i = 0; i < window.childCount(d); ++i) {
            const child = window.childAt(d, i)
            if (child.objectName === name && child.visible && n++ === index)
                return child
        }
        return null
    }
    function guideXs(d: Item, name: string): string {
        let xs = ""
        for (let i = 0; i < window.childCount(d); ++i) {
            const child = window.childAt(d, i)
            if (child.objectName === name && child.visible)
                xs += (xs === "" ? "" : ", ") + child.x
        }
        return xs
    }
    /// The delegate shown for `row`, or null.
    function shownItem(row: int): BlockDelegate {
        return editor.listView.itemAtIndex(row) as BlockDelegate
    }
    /// The item named `name` inside the embeds of `item`, or null.
    function embedPart(item: BlockDelegate, name: string): Item {
        return item ? window.findItem(window.embedViewOf(item), name) : null
    }
    function selfTestDocument(): void {
        // ---- Document mode (docs/UI.md "Note modes"): the trip plan reads as a document --------
        if (window.at()) { page.save(); page.path = host.documentPath }
        if (window.at()) {
            check(page.loaded && page.mode === "document" && editor.documentMode, "the document sample opens as a document")
            const para = rowOf("Pierwszego dnia")
            const item = window.shownItem(para)
            check(item !== null && Math.abs(item.contentLeft - editor.contentX) < 0.5 && editor.gutter === 0,
                  "paragraphs start at the text column, no outline gutter")
            const li = rowOf("Sobota")
            const nested = rowOf("Wejście od Palenicy")
            const liItem = window.shownItem(li)
            const nestedItem = window.shownItem(nested)
            check(liItem !== null && nestedItem !== null && liItem.contentLeft > editor.contentX
                  && nestedItem.contentLeft > liItem.contentLeft, "list items are indented per level")
            check(liItem !== null && liItem.topPad < item.topPad, "list items sit closer together than paragraphs")
            const h = rowOf("Plan")
            const hItem = window.shownItem(h)
            check(hItem !== null && hItem.topPad > item.topPad && editor.headingSize(2) > editor.fontSize,
                  "headings are larger with air above them")
            check(info(rowOf("Pociąg")).listMarker === "1.", "numbered items keep their numbers")
            // Indent guides of nested lists, at each ancestor level's marker centre (as the
            // outline's): the nested item has one at its parent's marker, never at its own; the
            // parent one from under its marker down; paragraphs and headings none.
            const nestedLine = window.guideAt(nestedItem, "guideLine", 0)
            check(nestedItem !== null && window.guideCount(nestedItem, "guideLine") === 1
                  && Math.abs(nestedLine.x - Math.round(liItem.bulletX)) < 0.5
                  && Math.abs(nestedLine.x - nestedItem.bulletX) > editor.listIndent / 2
                  && nestedLine.y === 0 && nestedLine.height === nestedItem.height,
                  "a nested list item draws its parent level's guide at the parent's marker, over its full height: "
                  + window.guideXs(nestedItem, "guideLine") + " / " + (liItem ? liItem.bulletX : "-"))
            const down = window.guideAt(liItem, "parentGuide", 0)
            check(window.guideCount(liItem, "parentGuide") === 1 && Math.abs(down.x - Math.round(liItem.bulletX)) < 0.5
                  && down.y > liItem.firstLineCenter && down.y + down.height === liItem.height,
                  "a list item with children draws its guide from under its marker down")
            check(window.guideCount(liItem, "guideLine") === 0 && window.guideCount(item, "guideLine") === 0
                  && window.guideCount(hItem, "guideLine") === 0 && window.guideCount(item, "parentGuide") === 0,
                  "top-level items, paragraphs and headings draw no guides")
            const after = window.shownItem(nested + 1)
            check(after !== null && Math.abs(after.y - (nestedItem.y + nestedItem.height)) < 0.5,
                  "the next nested item starts where this one ends: the guide runs on unbroken")
        }
        if (window.at()) editor.startEditing(rowOf("Niedziela"), -1)
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.editingRow === rowOf("Niedziela") + 1 && info(page.editingRow).kind === "listItem",
                    "Enter in a list item adds a list item")
        if (window.at()) typeKeys("Powrót ąę")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) { window.realRow = page.editingRow }
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(page.editingRow === window.realRow && info(window.realRow).kind === "paragraph",
                    "Enter on an empty list item leaves the list: " + info(window.realRow).kind)
        if (window.at()) typeKeys("Akapit po liście")
        if (window.at()) realKey(Qt.Key_Tab, 0, "")
        if (window.at()) check(info(window.realRow).kind === "paragraph" && info(window.realRow).depth === 0
                    && editor.activeEditor && editor.activeEditor.activeFocus, "Tab in a paragraph does nothing")
        if (window.at()) editor.startEditing(rowOf("Powrót ąę"), 0)
        if (window.at()) realKey(Qt.Key_Backspace, 0, "")
        if (window.at()) check(info(rowOf("Powrót ąę")).kind === "paragraph" && page.editingRow === rowOf("Powrót ąę"),
                    "Backspace at the start of a list item removes its marker")
        if (window.at()) realKey(Qt.Key_Z, Qt.ControlModifier, "")
        if (window.at()) check(info(rowOf("Powrót ąę")).kind === "listItem", "Ctrl+Z brings the list item back")
        if (window.at()) editor.startEditing(rowOf("Z powrotem bryczką"), 0)
        if (window.at()) realKey(Qt.Key_Tab, 0, "")
        if (window.at()) check(info(rowOf("Z powrotem bryczką")).depth === 2, "Tab indents a list item")
        if (window.at()) realKey(Qt.Key_Backtab, 0, "")
        if (window.at()) check(info(rowOf("Z powrotem bryczką")).depth === 1, "Shift+Tab outdents it")
        if (window.at()) editor.startEditing(rowOf("Pierwszego dnia"), 0)
        if (window.at()) realKey(Qt.Key_Up, 0, "")
        if (window.at()) check(page.editingRow === rowOf("Plan"), "Up goes to the previous block like in a text")
        if (window.at()) realKey(Qt.Key_Down, 0, "")
        if (window.at()) check(page.editingRow === rowOf("Pierwszego dnia"), "Down comes back")
        if (window.at()) { page.stopEditing(); page.toggleMode() }
        if (window.at()) check(page.mode === "outline" && editor.gutter > 0 && Js.indexOf(host.file(page.path), "brulion: outline") > 0,
                    "the header's toggle switches to an outline and writes only the key")
        if (window.at()) page.toggleMode()
        if (window.at()) check(page.mode === "document" && Js.indexOf(host.file(page.path), "brulion: document") > 0
                    && Js.indexOf(host.file(page.path), "Akapit po liście") > 0, "and back to a document")
        // A paragraph of nothing but a picture shows just the picture (no empty text line above
        // it); an SVG keeps its aspect ratio (it was stretched to a square); a click beside the
        // picture edits the line.
        if (window.at()) {
            host.setTextAsset("assets/trasa.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"400\" height=\"100\""
                              + " viewBox=\"0 0 400 100\"><rect width=\"400\" height=\"100\" fill=\"#9fc3e6\"/></svg>")
            window.realRow = page.insertBlockAfter(rowOf("Akapit po liście"), "![[trasa.svg]]")
            page.stopEditing()
            window.waits = 0
        }
        if (window.at()) editor.listView.positionViewAtIndex(window.realRow, ListView.Center)
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            const w = window.embedWidth(item)
            const h = window.embedHeight(item)
            // The picture loads asynchronously: this step repeats until it has (at most
            // 2 × waitLimit steps).
            if (!window.waitUntil(item !== null && Math.abs(h - w / 4) <= 2))
                return
            check(item !== null && Math.abs(h - w / 4) <= 2, "an SVG picture keeps its aspect ratio: " + w + "×" + h)
            check(item !== null && item.onlyPictures && item.contentHeight === 0 && item.embedsY - item.topPad === 0,
                  "a picture-only paragraph has no empty text line above the picture")
            if (item)
                window.embedViewOf(item).backgroundClicked()
        }
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            check(page.editingRow === window.realRow && item !== null && !item.onlyPictures && item.contentHeight > 0
                  && editor.activeEditor && editor.activeEditor.text === "![[trasa.svg]]",
                  "a click beside the picture edits its line")
            page.stopEditing()
        }
        // A picture replaced in its file by one of another shape (the same URL: Qt's pixmap
        // cache may still hold the old picture) gets a frame of the shape it is drawn with —
        // the file's header (the frame's size before loading) must not stretch it.
        if (window.at()) {
            host.overwriteTextAsset("assets/trasa.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\""
                                    + " height=\"400\" viewBox=\"0 0 100 400\"><!-- wysoka --><rect width=\"100\""
                                    + " height=\"400\" fill=\"#e6c39f\"/></svg>")
            window.waits = 0
        }
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            const frame = window.embedPart(item, "embedPicture")
            const img = window.embedPart(item, "embedImage") as Image
            const ready = frame !== null && img !== null && img.status === Image.Ready && frame.height > 0
                          && img.implicitHeight > 0
            if (!window.waitUntil(ready))
                return
            const shown = ready ? img.implicitWidth / img.implicitHeight : -1
            const box = ready ? frame.width / frame.height : -2
            check(ready && Math.abs(box / shown - 1) < 0.03,
                  "a replaced picture is not stretched: frame " + (frame ? frame.width + "×" + frame.height : "-")
                  + ", picture " + (img ? img.implicitWidth + "×" + img.implicitHeight : "-"))
        }
        // A picture from the internet: nothing loads until "Show"; then it is shown from the
        // cache with its own shape (an SVG by its bytes, the URL has no .svg), a right-click
        // opens its menu — naming the site, and only that menu —, and "Save to notebook"
        // makes it an attachment of the notebook (one undo step).
        if (window.at()) {
            host.setWebPictureText("https://example.com/mapa",
                                   "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"300\" height=\"100\""
                                   + " viewBox=\"0 0 300 100\"><rect width=\"300\" height=\"100\" fill=\"#c3e69f\"/></svg>")
            window.realRow = page.insertBlockAfter(rowOf("Akapit po liście"), "![Mapa trasy](https://example.com/mapa)")
            page.stopEditing()
            window.waits = 0
        }
        if (window.at()) editor.listView.positionViewAtIndex(window.realRow, ListView.Center)
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            const holder = window.embedPart(item, "webPicturePlaceholder")
            const show = window.embedPart(item, "webPictureShow") as T.AbstractButton
            check(holder !== null && holder.visible && show !== null && show.visible,
                  "a picture from the internet waits for “Pokaż”")
            check(host.webFetchCount() === 0, "nothing is fetched before “Pokaż”")
            if (show)
                show.clicked()
        }
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            const frame = window.embedPart(item, "embedPicture")
            const img = window.embedPart(item, "embedImage") as Image
            const ready = window.pictureReady(frame, img)
            if (!window.waitUntil(ready))
                return
            check(window.cachedPicture(frame, img),
                  "the picture is shown from the cache with its shape: " + (frame ? frame.width + "×" + frame.height : "-"))
            window.pictureSize = img ? img.sourceSize : Qt.size(0, 0)
            check(img !== null && window.pictureSize.width === 1024 && window.pictureSize.height === 420,
                  "an SVG from the internet is drawn to fit, not at 2048 × 2048: "
                  + (img ? window.pictureSize.width + "×" + window.pictureSize.height : "-"))
            // Real clicks when QtTest is there (TestEvent), else the signals themselves.
            // A click opens the picture on its own (the viewer), not its menu.
            if (frame && webEvents.available())
                webEvents.mouseClick(frame, frame.width / 2, frame.height / 2, Qt.LeftButton, Qt.NoModifier, -1)
            else if (item && DevTest.lengthAt(item.embeds, "") > 0)
                window.embedViewOf(item).webOpen(DevTest.valueAt(item.embeds, "0"))
            window.waits = 0
        }
        if (window.at()) {
            const viewer = editor.webPictureViewer
            const img = viewer ? window.findItem(viewer.contentItem, "webPictureViewerImage") as Image : null
            const ready = window.viewerReady(viewer, img)
            if (!window.waitUntil(ready))
                return
            check(window.viewerReady(viewer, img) && window.fromCache(img),
                  "a click on the picture opens it in the viewer, from the cache: " + (img ? img.source : "-"))
            const name = viewer ? window.findItem(viewer.contentItem, "webPictureViewerName") as Text : null
            check(name !== null && Js.indexOf(name.text, "example.com") >= 0, "the viewer names the site: " + (name ? name.text : "-"))
            check(editor.webPictureMenu === null || !editor.webPictureMenu.visible, "and no menu opens")
            // An SVG fills the window (drawn to fit, its shape kept).
            check(img !== null && Math.abs(img.width / img.height - 3) < 0.1 && img.width > 300,
                  "the viewer draws the SVG fitted with its shape: " + (img ? img.width + "×" + img.height : "-"))
            const close = viewer ? window.findItem(viewer.contentItem, "webPictureViewerClose") as T.AbstractButton : null
            if (close)
                close.clicked()
            else if (viewer)
                viewer.close()
        }
        if (window.at()) {
            check(editor.webPictureViewer !== null && !editor.webPictureViewer.visible, "“Zamknij” closes the viewer")
            const item = window.shownItem(window.realRow)
            const frame = window.embedPart(item, "embedPicture")
            // A right-click: the picture's menu.
            if (frame && webEvents.available())
                webEvents.mouseClick(frame, frame.width / 2, frame.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            else if (item)
                window.embedViewOf(item).webMenu("https://example.com/mapa", "example.com", frame, 10, 10)
        }
        if (window.at()) {
            const menu = editor.webPictureMenu
            check(menu !== null && menu.visible, "a right-click on the picture opens its menu")
            check(editor.blockMenu === null || !editor.blockMenu.visible,
                  "and not the block menu as well")
            const first = menu ? menu.itemAt(0) as BMenuItem : null
            check(first !== null && first.objectName === "webPictureHost" && !first.enabled && first.height > 0
                  && Js.indexOf(first.text, "example.com") >= 0 && menu.itemAt(1) !== null
                  && (menu.itemAt(1) as Item).objectName === "webPictureHostLine",
                  "the menu's first line names the site: " + (first ? first.text : "-"))
            const view = menu ? menu.itemAt(2) as BMenuItem : null
            check(view !== null && view.objectName === "webPictureView" && view.visible && view.enabled,
                  "the menu offers the viewer too (“Powiększ”)")
            const save = editor.webPictureMenu ? window.findItem(editor.webPictureMenu.contentItem, "webPictureDownload") as T.MenuItem : null
            if (save)
                save.triggered()
            else if (editor.webPictureMenu)
                editor.webPictureMenu.chosen("download")
            if (editor.webPictureMenu)
                editor.webPictureMenu.close()
        }
        if (window.at()) {
            const item = window.shownItem(window.realRow)
            check(window.webUrls().length === 0 && window.embedTarget(item) === "assets/mapa.svg" && !window.embedIsWeb(item),
                  "“Zapisz w notatniku” points the picture at the attachment: " + window.embedTarget(item))
            page.undo()
        }
        if (window.at()) check(window.webUrls().indexOf("https://example.com/mapa") >= 0, "undo brings the address back")
        // Its placeholder: a long alt text takes at most two lines, the site's line is always
        // there, both buttons side by side when they fit, and all of it inside the frame —
        // also once the fetch failed.
        if (window.at()) {
            window.farRow = page.insertBlockAfter(window.realRow,
                "![Zdjęcie z wycieczki: Morskie Oko o świcie, żółta łąka, ślady niedźwiedzia przy schronisku"
                + " i mgła nad Czarnym Stawem pod Rysami, zrobione z ostatniego zakrętu drogi](https://zdjęcia.example.org/brak.jpg)")
            page.stopEditing()
            window.waits = 0
        }
        if (window.at()) editor.listView.positionViewAtIndex(window.farRow, ListView.Center)
        if (window.at()) {
            const item = window.shownItem(window.farRow)
            window.checkWebPlaceholder(item, false)
            const show = window.embedPart(item, "webPictureShow") as T.AbstractButton
            if (show)
                show.clicked()
        }
        if (window.at()) {
            const item = window.shownItem(window.farRow)
            if (!window.waitUntil(window.embedFailed(item)))
                return
            check(window.embedFailed(item), "a picture that cannot be fetched fails")
            window.checkWebPlaceholder(item, true)
            page.deleteBlock(window.farRow)
        }
        // "/File…": the picker of any files, several at once; cancelled, nothing changes.
        if (window.at()) {
            window.fileLinkBefore = window.info(window.realRow).editText
            window.dialogs().pickFileLink(window.realRow, -1)
        }
        if (window.at()) window.waitStep(window.fileLinkBox() !== null && window.fileLinkBox().visible, "“Plik…” opens the file picker")
        if (window.at()) {
            const d = window.fileLinkBox()
            check(d.fileMode === FilePicker.OpenFiles && d.nameFilters.length === 0,
                  "it takes several files of any type")
            d.reject()
        }
        if (window.at()) window.waitStep(!window.fileLinkBox().visible, "cancelling closes it")
        if (window.at()) check(window.info(window.realRow).editText === window.fileLinkBefore,
                    "cancelling leaves the block as it was")
        // "/Picture from the internet…": "Save to notebook" checked each time it opens (the dialog
        // is reused), an address without http:// or https:// refused with both named.
        if (window.at()) window.dialogs().pickWebImage(window.realRow, -1)
        if (window.at()) window.waitStep(window.webImageBox() !== null && window.webImageBox().opened, "“Obraz z internetu…” opens its dialog")
        if (window.at()) {
            check(window.webImageSave().checked, "it opens with “Zapisz w notatniku” checked")
            window.webImageSave().checked = false
            window.webImageUrl().text = "www.example.com/żółw.jpg"
        }
        if (window.at()) {
            const d = window.webImageBox()
            const error = window.webImageUrl().errorText
            check(DevTest.property(d, "valid") !== true && !d.acceptEnabled && Js.indexOf("" + error, "http://") >= 0
                  && Js.indexOf("" + error, "https://") >= 0, "an address without its scheme is refused: " + error)
            d.reject()
        }
        if (window.at()) window.waitStep(!window.webImageBox().visible, "Anuluj closes it")
        if (window.at()) window.dialogs().pickWebImage(window.realRow, -1)
        if (window.at()) window.waitStep(window.webImageBox().opened, "it opens again")
        if (window.at()) {
            check(window.webImageSave().checked && window.webImageUrl().text === "", "again with “Zapisz w notatniku” checked and no address")
            window.webImageBox().reject()
        }
        if (window.at()) window.waitStep(!window.webImageBox().visible, "and closes again")
        // The journal stream caps document headings at the size of a day's date: smaller, in
        // the same order.
        if (window.at()) {
            const p1 = editor.headingSize(1)
            const p2 = editor.headingSize(2)
            const p3 = editor.headingSize(3)
            const p4 = editor.headingSize(4)
            editor.headingCap = editor.fontSize + 4
            const c1 = editor.headingSize(1)
            const c2 = editor.headingSize(2)
            const c3 = editor.headingSize(3)
            const c4 = editor.headingSize(4)
            editor.headingCap = 0
            check(p1 === Math.round(editor.fontSize * 1.75) && c1 === editor.fontSize + 4 && c2 < c1 && c3 <= c2 && c4 <= c3
                  && c4 >= editor.fontSize && c1 <= p1 && c2 <= p2 && c3 <= p3 && c4 <= p4,
                  "a heading cap shrinks the document's headings, keeping their order: " + p1 + "," + p2 + "," + p3 + "," + p4
                  + " → " + c1 + "," + c2 + "," + c3 + "," + c4)
        }
        // A frame right under a heading gets a little more air than text there (its edge sat
        // a few pixels under the heading's descenders).
        if (window.at()) editor.listView.positionViewAtIndex(rowOf("Pierwszego dnia"), ListView.Center)
        if (window.at()) {
            const para = window.shownItem(rowOf("Pierwszego dnia"))
            check(para !== null && para.topPad - editor.headingGapBelow === 0, "a paragraph sits close under its heading")
            window.countBefore = page.blocks.count
            window.realRow = page.insertMarkdown(rowOf("Plan"), "```\nmapa-trasy.gpx\n```")
            page.stopEditing()
        }
        if (window.at()) editor.listView.positionViewAtIndex(window.realRow, ListView.Center)
        if (window.at()) {
            const code = window.shownItem(window.realRow)
            const para = window.shownItem(rowOf("Pierwszego dnia"))
            check(code !== null && code.view === "code" && code.prevKind === "heading"
                  && code.topPad - (editor.headingGapBelow + Theme.space2) === 0,
                  "a code block under a heading gets more air than text: " + (code ? code.topPad : -1))
            check(para !== null && para.topPad - editor.paragraphGap === 0, "the paragraph after the frame keeps a paragraph gap")
            page.undo()
        }
        if (window.at()) check(page.blocks.count === window.countBefore && info(rowOf("Pierwszego dnia") - 1).view === "heading",
                    "undo takes the code block out again")
        // The touch toolbar of a document leads with the writing tools (list, task, [[, #, /),
        // indentation after them (off in a paragraph); an outline keeps indentation first.
        if (window.at()) { Theme.forceTouch = true; editor.startEditing(rowOf("Pierwszego dnia"), 0); window.waits = 0 }
        if (window.at()) {
            const list = window.findItem(editor, "toolbarList")
            const writing = window.findItem(editor, "toolbarWritingDocument")
            const structure = window.findItem(editor, "toolbarStructure")
            const ordered = editor.toolbarVisible && list !== null && writing !== null && structure !== null
                  && list.visible && writing.visible && list.x < writing.x && writing.x < structure.x
            // The toolbar is laid out asynchronously (slow under sanitizers): repeat this step
            // until it is (at most 2 × waitLimit steps).
            if (!window.waitUntil(ordered))
                return
            check(ordered, "document toolbar: list and writing tools first, indentation after them")
            check(structure !== null && !window.childAt(structure, 0).enabled && !window.childAt(structure, 1).enabled,
                  "document toolbar: no indentation for a paragraph")
        }
        if (window.at()) editor.startEditing(rowOf("Sobota"), 0)
        if (window.at()) {
            const structure = window.findItem(editor, "toolbarStructure")
            check(structure !== null && window.childAt(structure, 0).enabled && window.childAt(structure, 1).enabled,
                  "document toolbar: list items indent")
            page.stopEditing()
            page.setMode("outline")
        }
        if (window.at()) editor.startEditing(rowOf("Pierwszego dnia"), 0)
        if (window.at()) {
            const list = window.findItem(editor, "toolbarList")
            const writing = window.findItem(editor, "toolbarWriting")
            const structure = window.findItem(editor, "toolbarStructure")
            check(page.mode === "outline" && list !== null && !list.visible && writing !== null && structure !== null
                  && writing.visible && structure.x < writing.x && window.childAt(structure, 0).enabled,
                  "outline toolbar: indentation first, no list button")
            page.stopEditing()
            page.setMode("document")
            Theme.forceTouch = false
        }
    }
    readonly property list<string> projectsCrumb: ["Projekty"]
    readonly property string probeMarkup: "<font size=1>Żółw " + Js.repeat("ż", 24) + "</font>.png"
    Component {
        id: menuItemProbeComponent
        BMenuItem {}
    }
    Component {
        id: crumbsProbeComponent
        BBreadcrumb { width: 900 }
    }
    Component {
        id: focusProbeComponent
        TextInput { width: 10; height: 10 }
    }
    function selfTestProbes(): void {
        // ---- An editor destroyed before its deferred work runs (scrolling to a row, focus
        // loss, completions, scrolling to the top after loading): nothing touches it then.
        // A TypeError in the output fails the ctest (tests/app/editor/CMakeLists.txt). -------
        if (window.at()) { window.probe = probeComponent.createObject(window) as Item }
        if (window.at()) {
            const ed = window.probe ? window.findItem(window.probe, "probeEditor") as BlockEditor : null
            check(ed !== null && ed.controller !== null && ed.controller.loaded, "the probe editor is loaded")
            if (!ed)
                return
            window.probe.destroy() // gone when the event loop runs, before the work below
            ed.revealRow(1)
            ed.ensureRowVisible(1)
            ed.editorFocusLost()
            ed.handleCommandResult(1, { action: "complete" })
            ed.controller.load()
        }
        if (window.at()) check(window.probe === null, "the probe editor is gone")
        if (window.at()) check(page.loaded && editor.alive, "the page's own editor still works")
        // ---- Names in menus and breadcrumbs are shown as written -------------------------------
        if (window.at()) {
            const markup = window.probeMarkup
            const item = menuItemProbeComponent.createObject(window.contentItem) as BMenuItem
            item.text = markup
            check(showsAsWritten(textShowing(item, markup), markup), "a menu item shows a name with markup as written")
            item.destroy()
            const crumbs = crumbsProbeComponent.createObject(window.contentItem) as BBreadcrumb
            crumbs.model = DevTest.rowsOf("title", Js.appendedString(window.projectsCrumb, markup))
            window.crumbsProbe = crumbs
        }
        if (window.at()) {
            check(showsAsWritten(textShowing(window.crumbsProbe, window.probeMarkup), window.probeMarkup),
                  "a breadcrumb shows a name with markup as written")
            window.crumbsProbe.destroy()
            page.save()
            page.path = host.samplePath
        }
        if (window.at()) {
            // A block zoomed into: its text in the breadcrumb above the blocks, as written.
            window.realRow = page.insertBlockAfter(rowOf("Ekipa może"), "Tekst z <b>HTML</b> & <i>kursywą</i>")
            page.stopEditing()
            page.zoomInto(window.realRow)
        }
        if (window.at()) {
            const label = textContaining(editor, "<b>HTML</b>") as Text
            check(label !== null && showsAsWritten(label, label.text), "the zoom breadcrumb shows a block's markup as written: "
                  + (label ? label.text : "none"))
            page.zoomOut()
            page.undo()
        }
    }

    // ---- Self test of the round clock (`--editor-script clock`, tst_editor_qml_clock*) -------
    // BTimePicker on its own: the value of every position, taps and drags with a finger (touch
    // UI) or the mouse — also drags that start on a number or the chosen value's bubble —, keys
    // and the focus ring; BTimePickerDialog: open, set, cancel, the whole dial in a short window;
    // the clock buttons of the reminder's date picker and of the calendar's add dialog (a typed
    // time first; the clock created only when asked for). Real input through QtTest's TestEvent.
    property BTimePicker clock: null
    property BTimePickerDialog clockDialog: null
    property list<string> clockPicks: []
    property list<string> dialogPicks: []
    property BDatePicker datePicker: null
    property CalendarAddDialog addDialog: null
    property BReminderDialog reminderDialog: null
    Connections {
        target: window.clock
        function onPicked(time: string) {
            window.clockPicks = Js.appendedString(window.clockPicks, time)
        }
    }
    Connections {
        target: window.clockDialog
        function onPicked(time: string) {
            window.dialogPicks = Js.appendedString(window.dialogPicks, time)
        }
    }
    /// The `at` of each reminder the calendar's add dialog added.
    property list<string> addedReminders: []
    Connections {
        target: window.addDialog
        function onReminderAdded(text: string, at: string, alarm: bool) {
            window.addedReminders = Js.appendedString(window.addedReminders, at)
        }
    }
    Component {
        id: clockComponent
        BTimePicker { x: 40; y: 40; z: 100000; time: "18:30" }
    }
    Component {
        id: clockDialogComponent
        BTimePickerDialog {}
    }
    Component {
        id: plainDatePickerComponent
        BDatePicker { withTime: false }
    }
    Component {
        id: addDialogComponent
        CalendarAddDialog {}
    }
    Component {
        id: focusableComponent
        Item { activeFocusOnTab: true }
    }

    /// The point of the face (its coordinates) at `radius` from the centre, `deg` from 12 o'clock.
    function clockX(c: BTimePicker, radius: real, deg: real): real {
        return c.center + Math.cos((deg - 90) * Math.PI / 180) * radius
    }
    function clockY(c: BTimePicker, radius: real, deg: real): real {
        return c.center + Math.sin((deg - 90) * Math.PI / 180) * radius
    }
    /// A pick on the face at `radius` and `deg` (no events: BTimePicker.pickAt()).
    function clockPick(c: BTimePicker, radius: real, deg: real): void {
        c.pickAt(window.clockX(c, radius, deg), window.clockY(c, radius, deg))
    }
    /// The clock's face (BTimePicker.focusItem).
    function clockFace(c: BTimePicker): Item {
        return c.focusItem
    }
    /// A tap (a finger on touch UIs, else a click) at (x, y) of `item`.
    function tapAt(item: Item, x: real, y: real): void {
        if (Theme.touchUi) {
            clockEvents.touchPress(item, x, y)
            clockEvents.touchRelease(item, x, y)
        } else {
            clockEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    /// A tap on the inner / the outer ring at `deg`.
    function clockTapInner(c: BTimePicker, deg: real): void {
        window.clockTap(c, c.innerRadius, deg)
    }
    function clockTapOuter(c: BTimePicker, deg: real): void {
        window.clockTap(c, c.outerRadius, deg)
    }
    function clockTap(c: BTimePicker, radius: real, deg: real): void {
        tapAt(window.clockFace(c), window.clockX(c, radius, deg), window.clockY(c, radius, deg))
    }
    /// A tap with a finger that shakes a little (two moves of a few pixels, well under a drag).
    function clockShakyTap(c: BTimePicker, radius: real, deg: real): void {
        const face = window.clockFace(c)
        const x = window.clockX(c, radius, deg)
        const y = window.clockY(c, radius, deg)
        window.pointerDown(face, x, y)
        window.pointerMove(face, x + 2, y + 1)
        window.pointerMove(face, x + 3, y - 1)
        window.pointerUp(face, x + 3, y - 1)
    }
    /// The pointer pressed, moved and released at (x, y) of `item`: a finger on touch UIs, else the mouse.
    function pointerDown(item: Item, x: real, y: real): void {
        if (Theme.touchUi)
            clockEvents.touchPress(item, x, y)
        else
            clockEvents.mousePress(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
    }
    function pointerMove(item: Item, x: real, y: real): void {
        if (Theme.touchUi)
            clockEvents.touchMove(item, x, y)
        else
            clockEvents.mouseMove(item, x, y, -1, Qt.LeftButton, Qt.NoModifier)
    }
    function pointerUp(item: Item, x: real, y: real): void {
        if (Theme.touchUi)
            clockEvents.touchRelease(item, x, y)
        else
            clockEvents.mouseRelease(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
    }
    /// A drag along the circle of `radius` from `fromDeg` to `toDeg`, in twelve moves.
    function clockDrag(c: BTimePicker, radius: real, fromDeg: real, toDeg: real): void {
        const face = window.clockFace(c)
        let x = window.clockX(c, radius, fromDeg)
        let y = window.clockY(c, radius, fromDeg)
        window.pointerDown(face, x, y)
        for (let i = 1; i <= 12; ++i) {
            const deg = fromDeg + (toDeg - fromDeg) * i / 12
            x = window.clockX(c, radius, deg)
            y = window.clockY(c, radius, deg)
            window.pointerMove(face, x, y)
        }
        window.pointerUp(face, x, y)
    }
    function clockRing(c: BTimePicker): Item {
        return window.findItem(c, "clockFocusRing")
    }
    /// The numbers on the face never overlap (nor does the chosen value's bubble reach over a
    /// neighbour of its number), each holds its text in a legible font — the outer ring's in the
    /// text size itself, the inner one's at least 12 px —, and a press near a number's edge,
    /// towards its neighbour, still gives that number (DevTest.clockNumbers()).
    function checkClockNumbers(c: BTimePicker, what: string): void {
        const report = DevTest.clockNumbers(c, Theme.textSm)
        const expected = c.mode === "hour" ? 24 : 12
        check(report === expected + " numbers", "the clock's numbers do not overlap and hold their text, " + what + ": " + report)
    }
    /// Scroll a dialog's body so that `item` shows, then tap it.
    function tapInDialog(dialog: T.Dialog, item: Item): void {
        const body = dialog.contentItem as Flickable
        window.mapped = item.mapToItem(body.contentItem, 0, 0)
        body.contentY = Math.max(0, Math.min(window.mapped.y - (body.height - item.height) / 2, body.contentHeight - body.height))
        tapAt(item, item.width / 2, item.height / 2)
    }
    readonly property list<string> clockDialogMembers: ["openFor", "picked"]
    /// The clock dialogs `object` holds (created ones: BDatePicker creates its clock on demand).
    function clockDialogsIn(object: QtObject): int {
        return DevTest.countWithMembers(object, window.clockDialogMembers)
    }
    /// Repeat the current step until `ready` (an asynchronous popup), at most 2 × waitLimit steps
    /// (~6 s, times BRULION_TEST_SLOWDOWN).
    function waitUntil(ready: bool): bool {
        if (ready || window.waits++ >= 2 * window.waitLimit) {
            window.waits = 0
            return true
        }
        window.stepIndex -= 1
        return false
    }
    /// The ring of hour `h` (00–11 outside, 12–23 inside).
    function hourRadius(c: BTimePicker, h: int): real {
        return h < 12 ? c.outerRadius : c.innerRadius
    }
    /// The distances from the centre minutes are picked at: near the hub, on the rings, at the edge.
    function minuteRadius(c: BTimePicker, k: int): real {
        return k === 0 ? 20 : k === 1 ? c.innerRadius : k === 2 ? c.outerRadius : c.center - 2
    }
    /// The time a pick on the outer ring at `deg` gives (from 10:30, on the minutes).
    function clockAt(c: BTimePicker, deg: real): string {
        c.time = "10:30"
        window.clockPick(c, c.outerRadius, deg)
        return c.time
    }

    function clockTest(): void {
        clock = clockComponent.createObject(window.contentItem) as BTimePicker
        runSteps("clock")
    }
    /// The steps of clockTest (see the step runner).
    function clockSteps(): void {
        window.clockFaceSteps()
        window.clockDialogSteps()
    }
    function clockFaceSteps(): void {
        const c = window.clock
        const face = window.clockFace(c)
        if (window.at()) check(face.width === c.dialSize && face.height === c.dialSize,
                    "the face is the dial's size: " + face.width + "×" + face.height + ", dial " + c.dialSize)
        // ---- The numbers never overlap, down to the smallest dial (the dialog's 180 px floor;
        //      196 px: a phone held sideways), with an inner hour or a minute chosen ----------
        if (window.at()) checkClockNumbers(c, c.dialSize + " px, hours")
        if (window.at()) { window.clockSizeBefore = c.dialSize; c.dialSize = 196; c.time = "13:25" }
        if (window.at()) checkClockNumbers(c, "196 px, hours, 13 chosen")
        if (window.at()) { c.dialSize = 180 }
        if (window.at()) checkClockNumbers(c, "180 px, hours, 13 chosen")
        if (window.at()) { c.mode = "minute" }
        if (window.at()) checkClockNumbers(c, "180 px, minutes")
        // Larger text (Settings → Text size → Larger) on the smallest dials: the
        // outer numbers keep the text size, the inner hours stay legible.
        if (window.at()) { window.clockFontScaleBefore = Theme.fontScale; Theme.fontScale = 1.3; c.mode = "hour" }
        if (window.at()) checkClockNumbers(c, "180 px, larger text, hours, 13 chosen")
        if (window.at()) { c.mode = "minute" }
        if (window.at()) checkClockNumbers(c, "180 px, larger text, minutes")
        if (window.at()) { c.dialSize = 196; c.mode = "hour" }
        if (window.at()) checkClockNumbers(c, "196 px, larger text, hours, 13 chosen")
        if (window.at()) {
            Theme.fontScale = window.clockFontScaleBefore
            c.dialSize = window.clockSizeBefore
            c.mode = "hour"
            c.time = "18:30"
        }
        // ---- The value of every position --------------------------------------------------
        if (window.at()) {
            c.mode = "hour"
            let bad = ""
            for (let h = 0; h < 24; ++h) {
                c.time = "12:34"
                const radius = window.hourRadius(c, h)
                window.clockPick(c, radius, (h % 12) * 30)
                if (c.time !== c.pad(h) + ":34")
                    bad += (bad === "" ? "" : ",") + h + "→" + c.time
            }
            check(bad === "", "every hour's number gives that hour (00–11 outside, 12–23 inside) " + bad)
        }
        if (window.at()) {
            c.mode = "hour"
            let outerFrom = -1
            for (let r = 13; r < c.center && outerFrom < 0; ++r) {
                c.time = "06:00"
                window.clockPick(c, r, 90)
                if (c.selectedHour === 3)
                    outerFrom = r
            }
            check(outerFrom > c.innerRadius && outerFrom < c.outerRadius,
                  "the outer ring starts between the rings: r " + outerFrom + " (inner " + c.innerRadius + ", outer " + c.outerRadius + ")")
        }
        if (window.at()) {
            c.mode = "minute"
            let bad = ""
            for (let m = 0; m < 60; ++m) {
                for (let k = 0; k < 4; ++k) {
                    const r = window.minuteRadius(c, k)
                    c.time = "10:" + (m === 7 ? "08" : "07")
                    window.clockPick(c, r, m * 6)
                    if (c.selectedMinute !== m || c.selectedHour !== 10)
                        bad += (bad === "" ? "" : ",") + m + "@" + Math.round(r) + "→" + c.time
                }
            }
            check(bad === "" && window.clockAt(c, 359.9) === "10:00" && window.clockAt(c, 2.9) === "10:00"
                  && window.clockAt(c, 3.1) === "10:01",
                  "every minute comes from its angle, at any distance from the centre " + bad)
        }
        if (window.at()) {
            c.mode = "hour"
            c.time = "18:30"
            window.clockPicks = []
            window.clockPick(c, 5, 0)
            check(c.time === "18:30" && window.clockPicks.length === 0, "the hub is no value")
        }
        // ---- Taps and drags (a finger on touch UIs, the mouse elsewhere) ------------------
        if (window.at()) { c.time = "18:30"; c.mode = "hour"; clockTap(c, c.outerRadius, 7 * 30) }
        if (window.at()) check(c.time === "07:30" && c.mode === "minute", "a tap on 07 (outer ring) picks it and moves on to the minutes: " + c.time + " " + c.mode)
        if (window.at()) { c.mode = "hour"; clockTap(c, c.innerRadius, 9 * 30) }
        if (window.at()) check(c.time === "21:30" && c.mode === "minute", "a tap on 21 (inner ring): " + c.time + " " + c.mode)
        if (window.at()) clockTap(c, c.outerRadius, 37 * 6)
        if (window.at()) check(c.time === "21:37", "a tap between the minute numbers gives that minute: " + c.time)
        if (window.at()) clockTap(c, c.outerRadius - 4, 45 * 6 + 3)
        if (window.at()) check(c.time === "21:45", "a tap a little off the number 45 gives 45: " + c.time)
        if (window.at()) { c.time = "21:30"; clockShakyTap(c, c.outerRadius - 3, 45 * 6 + 4) }
        if (window.at()) check(c.time === "21:45", "a shaky tap a little off the number 45 (the finger moves a few pixels) gives 45: " + c.time)
        if (window.at()) { c.time = "18:30"; c.mode = "hour"; clockTap(c, 4, 0) }
        if (window.at()) check(c.time === "18:30" && c.mode === "hour", "a tap on the hub changes nothing: " + c.time + " " + c.mode)
        if (window.at()) { c.time = "18:30"; c.mode = "minute"; window.clockPicks = []; clockDrag(c, c.outerRadius, 180, 270) }
        if (window.at()) check(c.time === "18:45" && window.clockPicks.length > 1,
                    "dragging the minute hand by its bubble (on the number 30) to 45: " + c.time + " via " + window.clockPicks.join(","))
        if (window.at()) { c.time = "18:30"; c.mode = "hour"; clockDrag(c, c.innerRadius, 180, 270) }
        if (window.at()) check(c.time === "21:30" && c.mode === "minute",
                    "dragging the hour bubble (on 18) to 21, then on to the minutes: " + c.time + " " + c.mode)
        if (window.at()) { c.time = "11:30"; c.mode = "hour"; clockDrag(c, c.outerRadius, 330, 390) }
        if (window.at()) check(c.time === "01:30", "a drag from the number 11 across 12 o'clock to 01: " + c.time)
        if (window.at()) { c.time = "18:30"; c.mode = "minute"; clockDrag(c, 60, 180, 270) }
        if (window.at()) check(c.time === "18:45", "a drag inside the ring, off the numbers: " + c.time)
        if (window.at()) { c.time = "18:32"; c.mode = "minute"; clockDrag(c, c.outerRadius, 32 * 6, 44 * 6) }
        if (window.at()) check(c.time === "18:44", "a drag from between the numbers (32) to 44: " + c.time)
        // ---- Keys, and the focus ring for the keyboard only --------------------------------
        if (window.at()) { c.mode = "hour"; c.time = "23:59"; face.forceActiveFocus(Qt.MouseFocusReason) }
        if (window.at()) check(face.activeFocus && !clockRing(c).visible, "the face has the keyboard, without a focus ring after a tap")
        if (window.at()) EditorInput.sendKey(face, Qt.Key_Up, 0, "")
        if (window.at()) check(c.time === "00:59" && clockRing(c).visible, "Up from 23 wraps to 00, and keys show the focus ring: " + c.time)
        if (window.at()) EditorInput.sendKey(face, Qt.Key_Down, 0, "")
        if (window.at()) check(c.time === "23:59", "Down from 00 wraps to 23: " + c.time)
        if (window.at()) EditorInput.sendKey(face, Qt.Key_PageUp, 0, "")
        if (window.at()) check(c.time === "04:59", "PageUp: five hours on: " + c.time)
        if (window.at()) { EditorInput.sendKey(face, Qt.Key_Right, 0, ""); EditorInput.sendKey(face, Qt.Key_Up, 0, "") }
        if (window.at()) check(c.mode === "minute" && c.time === "04:00", "Right, then Up: minute 59 wraps to 00: " + c.time + " " + c.mode)
        if (window.at()) EditorInput.sendKey(face, Qt.Key_PageDown, 0, "")
        if (window.at()) check(c.time === "04:55", "PageDown: five minutes back: " + c.time)
        if (window.at()) EditorInput.sendKey(face, Qt.Key_End, 0, "")
        if (window.at()) check(c.time === "04:59", "End: the last minute: " + c.time)
        if (window.at()) { EditorInput.sendKey(face, Qt.Key_Left, 0, ""); EditorInput.sendKey(face, Qt.Key_Home, 0, "") }
        if (window.at()) check(c.mode === "hour" && c.time === "00:59", "Left, then Home: hour 00: " + c.time + " " + c.mode)
        if (window.at()) clockTap(c, c.outerRadius, 90)
        if (window.at()) check(face.activeFocus && !clockRing(c).visible && c.time === "03:59", "a tap hides the focus ring again: " + c.time)
        if (window.at()) {
            window.clockOther = focusableComponent.createObject(window.contentItem) as Item
            window.clockOther.forceActiveFocus()
        }
        if (window.at()) face.forceActiveFocus(Qt.TabFocusReason)
        if (window.at()) check(face.activeFocus && clockRing(c).visible, "focus that comes with Tab shows the focus ring")
        if (window.at()) clockTap(c, c.outerRadius, 120)
        if (window.at()) {
            check(face.activeFocus && !clockRing(c).visible && c.time === "03:20",
                  "a tap after Tab hides the focus ring: " + c.time)
            window.clockOther.destroy()
        }
    }
    /// The picker of clockDialog.
    function dialogPicker(): BTimePicker {
        return window.clockDialog.picker
    }
    /// The clock of datePicker, its picker.
    function dateClock(): BTimePickerDialog {
        return window.datePicker.clockDialog
    }
    function dateClockPicker(): BTimePicker {
        return window.dateClock().picker
    }
    /// The clock of addDialog, its picker.
    function addClock(): BTimePickerDialog {
        return window.addDialog.clockDialog
    }
    function addClockPicker(): BTimePicker {
        return window.addClock().picker
    }
    /// A field of a dialog's body, by its objectName.
    function dialogField(dialog: T.Dialog, name: string): Item {
        return window.findItem(dialog.contentItem, name)
    }
    function clockDialogSteps(): void {
        const c = window.clock
        // ---- The dialog -----------------------------------------------------------------------
        if (window.at()) {
            c.visible = false
            window.clockDialog = clockDialogComponent.createObject(window.contentItem) as BTimePickerDialog
            window.clockDialog.openFor("7")
        }
        if (window.at()) {
            if (!waitUntil(window.clockDialog.opened))
                return
            const p = window.dialogPicker()
            check(p.time === "07:00" && p.mode === "hour", "the dialog opens on 07:00 for \"7\", on the hours: " + p.time)
            check(window.clockFace(p).activeFocus && !clockRing(p).visible, "the dial has the keyboard when the dialog opens, without a focus ring")
            const body = window.clockDialog.contentItem as Flickable
            check(body.contentHeight <= body.height + 1,
                  "the whole dial shows in the dialog in a " + window.width + "×" + window.height + " window: body " + body.height
                  + ", clock " + body.contentHeight + ", dial " + p.dialSize + (p.horizontal ? ", the fields beside it" : ""))
            check(p.dialSize >= 180 && 2 * Math.PI * p.innerRadius / 12 >= Theme.textSm * 1.6,
                  "the inner ring's numbers have room: dial " + p.dialSize + ", " + Js.toFixed(2 * Math.PI * p.innerRadius / 12, 1)
                  + " px apart")
            checkClockNumbers(p, "in the dialog (dial " + p.dialSize + " px)")
        }
        if (window.at()) clockTapInner(window.dialogPicker(), 90)
        if (window.at()) clockTapOuter(window.dialogPicker(), 45 * 6)
        if (window.at()) check(window.dialogPicker().time === "15:45" && window.dialogPicks.length === 0, "picked on the dial, not set yet: " + window.dialogPicker().time)
        if (window.at()) {
            // A drag on the dial moves the hand and never scrolls the dialog.
            const body = window.clockDialog.contentItem as Flickable
            window.scrollBefore = body.contentY
            clockDrag(window.dialogPicker(), 60, 0, 170)
            check(window.dialogPicker().time === "15:28" && body.contentY === window.scrollBefore,
                  "a drag down the dial in the dialog moves the hand, not the dialog: " + window.dialogPicker().time + ", scrolled " + (body.contentY - window.scrollBefore))
            window.dialogPicker().time = "15:45"
        }
        if (window.at()) window.clockDialog.tryAccept()
        if (window.at()) check(window.dialogPicks.join(",") === "15:45", "Set gives the time: " + window.dialogPicks.join(","))
        if (window.at()) {
            if (waitUntil(!window.clockDialog.visible))
                window.clockDialog.openFor("")
        }
        if (window.at()) {
            if (!waitUntil(window.clockDialog.opened))
                return
            check(window.dialogPicker().time === "10:00", "nothing given: the dialog opens on the current time: " + window.dialogPicker().time)
            clockTapOuter(window.dialogPicker(), 30)
        }
        if (window.at()) window.clockDialog.reject()
        if (window.at()) check(window.dialogPicks.join(",") === "15:45", "Cancel gives nothing: " + window.dialogPicks.join(","))
        if (window.at()) {
            if (waitUntil(!window.clockDialog.visible))
                window.clockDialog.openFor("25:00")
        }
        if (window.at()) {
            if (!waitUntil(window.clockDialog.opened))
                return
            check(window.dialogPicker().time === "10:00", "no time: the dialog opens on the current time: " + window.dialogPicker().time)
            window.clockDialog.reject()
        }
        // ---- The date picker's clock: created when asked for, after the typed time ---------
        if (window.at()) {
            if (!waitUntil(!window.clockDialog.visible))
                return
            const plain = plainDatePickerComponent.createObject(window.contentItem) as BDatePicker
            check(clockDialogsIn(plain) === 0, "a date picker without a time creates no clock")
            plain.destroy()
            page.stopEditing()
            window.dialogs().createReminder(rowOf("Zadzwonić do hydraulika"), false)
        }
        if (window.at()) {
            window.reminderDialog = window.reminderBox()
            if (!waitUntil(window.reminderDialog && window.reminderDialog.opened))
                return
            window.timeField = window.dialogField(window.reminderDialog, "datePickerTime") as BTextField
            window.dateField = window.dialogField(window.reminderDialog, "datePickerNatural") as BTextField
            let item = window.timeField as Item
            while (item && !DevTest.hasMember(item, "applyTime"))
                item = item.parent
            window.datePicker = item as BDatePicker
            check(window.datePicker !== null && clockDialogsIn(window.datePicker) === 0, "the reminder's date picker has no clock until it is asked for")
            window.timeField.forceActiveFocus()
            window.timeField.selectAll()
        }
        if (window.at()) typeKeys("2346")
        if (window.at()) tapInDialog(window.reminderDialog, window.dialogField(window.reminderDialog, "datePickerClock"))
        if (window.at()) {
            const clk = window.dateClock()
            if (!waitUntil(clk !== null && clk.opened))
                return
            check(window.reminderDialog.at === "2026-10-16 23:46" && window.dateClockPicker().time === "23:46",
                  "the Clock button applies the typed time first and opens the clock on it: " + window.reminderDialog.at)
            window.firstClock = clk
        }
        if (window.at()) clockTapOuter(window.dateClockPicker(), 5 * 30)
        if (window.at()) clockTapOuter(window.dateClockPicker(), 15 * 6)
        if (window.at()) window.dateClock().tryAccept()
        if (window.at()) check(window.reminderDialog.at === "2026-10-16 05:15", "Set on the clock changes the reminder's time: " + window.reminderDialog.at)
        if (window.at()) {
            if (!waitUntil(!window.dateClock().visible))
                return
            check(window.timeField.text === "05:15", "the time field shows the clock's time: " + window.timeField.text)
            window.dateField.forceActiveFocus()
        }
        if (window.at()) check(window.reminderDialog.at === "2026-10-16 05:15", "leaving the time field keeps it: " + window.reminderDialog.at)
        if (window.at()) { window.timeField.forceActiveFocus(); window.timeField.selectAll() }
        if (window.at()) typeKeys("25:99")
        if (window.at()) tapInDialog(window.reminderDialog, window.dialogField(window.reminderDialog, "datePickerClock"))
        if (window.at()) {
            const clk = window.dateClock()
            if (!waitUntil(clk.opened))
                return
            const picker = window.dateClockPicker()
            check(window.datePicker.timeInvalid && picker.time === "05:15",
                  "after an invalid typed time the clock opens on the last good one: " + picker.time)
            check(clk === window.firstClock, "the date picker keeps its one clock")
            clockTapInner(window.dateClockPicker(), 7 * 30)
        }
        if (window.at()) clockTapOuter(window.dateClockPicker(), 0)
        if (window.at()) window.dateClock().tryAccept()
        if (window.at()) {
            if (!waitUntil(!window.dateClock().visible))
                return
            check(window.reminderDialog.at === "2026-10-16 19:00" && !window.datePicker.timeInvalid && window.timeField.text === "19:00"
                  && window.reminderDialog.acceptEnabled, "a time from the clock replaces an invalid typed one: " + window.reminderDialog.at)
            window.reminderDialog.reject()
        }
        // ---- The calendar's add dialog ------------------------------------------------------
        if (window.at()) {
            if (!waitUntil(!window.reminderDialog.visible))
                return
            window.addDialog = addDialogComponent.createObject(window.contentItem) as CalendarAddDialog
            check(window.addClock() === null, "the calendar's add dialog creates no clock until it is asked for")
            window.addDialog.openFor("reminder", "2026-10-16")
        }
        if (window.at()) {
            if (!waitUntil(window.addDialog.opened))
                return
            window.addTimeField().forceActiveFocus()
        }
        if (window.at()) typeKeys("2346")
        if (window.at()) tapInDialog(window.addDialog, window.dialogField(window.addDialog, "calendarAddClock"))
        if (window.at()) {
            const clk = window.addClock()
            if (!waitUntil(clk !== null && clk.opened))
                return
            check(window.addDialog.time === "23:46" && window.addClockPicker().time === "23:46", "the add dialog's clock opens on the typed time: " + window.addClockPicker().time)
            clockTapOuter(window.addClockPicker(), 30)
        }
        if (window.at()) clockTapOuter(window.addClockPicker(), 10 * 6)
        if (window.at()) window.addClock().tryAccept()
        if (window.at()) {
            if (!waitUntil(!window.addClock().visible))
                return
            check(window.addDialog.time === "01:10" && window.addTimeField().text === "",
                  "a time picked on the clock replaces the typed one: " + window.addDialog.time)
            window.addDialog.reject()
        }
        // A typed time applies on Enter or when the field is left, never per keystroke; text that
        // is no time keeps Add disabled, its hint shown only once it is applied (no hint coming and
        // going while typing). Today (--now 10:00): the next full hour, the quick times already
        // past disabled, a past time said under the field.
        if (window.at()) {
            if (!waitUntil(!window.addDialog.visible))
                return
            window.addDialog.openFor("reminder", DatesHelper.today)
        }
        if (window.at()) {
            if (!waitUntil(window.addDialog.opened))
                return
            check(window.addDialog.time === "11:00", "today after the default time the add dialog opens on the next full hour: " + window.addDialog.time)
            check(!window.dialogField(window.addDialog, "calendarAddQuick_09:00").enabled
                  && window.dialogField(window.addDialog, "calendarAddQuick_12:00").enabled, "today the quick times already past are disabled")
            typeKeys("Zadzwonić do mamy")
        }
        if (window.at()) {
            check(window.addDialog.acceptEnabled, "Add is enabled once there is something to remind of")
            window.addTimeField().forceActiveFocus()
        }
        if (window.at()) typeKeys("25:00")
        if (window.at()) {
            check(window.addDialog.time === "11:00" && !window.addDialog.acceptEnabled && !window.addTimeHint().visible,
                  "a typed text that is no time changes nothing and keeps Add disabled, without a hint while typed: "
                  + window.addDialog.time + ", " + window.addTimeHint().text)
            realKey(Qt.Key_Return, 0, "")
        }
        if (window.at()) {
            const hint = "" + window.addTimeHint().text
            check(window.addDialog.opened && window.addDialog.time === "11:00" && window.addedReminders.length === 0
                  && window.addTimeHint().visible && hint === window.addDialog.badTimeHint,
                  "Enter on it applies and adds nothing, the hint under the field says why: " + hint)
            window.addTimeField().selectAll()
        }
        if (window.at()) typeKeys("0930")
        if (window.at()) {
            check(window.addDialog.time === "11:00" && window.addDialog.acceptEnabled && !window.addTimeHint().visible,
                  "keystrokes do not change the time, the first one takes the hint away: " + window.addDialog.time)
            window.dialogField(window.addDialog, "calendarAddText").forceActiveFocus()
        }
        if (window.at()) {
            check(window.addDialog.time === "09:30" && window.addTimeField().text === "09:30", "leaving the field applies the typed time: " + window.addDialog.time)
            const hint = "" + window.addTimeHint().text
            check(window.addDialog.past && window.addTimeHint().visible && hint === window.addDialog.pastHint,
                  "a time already past is said under the field: " + hint)
            window.addTimeField().forceActiveFocus()
            window.addTimeField().selectAll()
        }
        if (window.at()) typeKeys("14:")
        if (window.at()) {
            const hint = "" + window.addTimeHint().text
            check(window.addDialog.time === "09:30" && !window.addDialog.acceptEnabled && hint === window.addDialog.pastHint,
                  "a time half typed keeps Add disabled quietly, the hint still the applied time's: " + hint)
        }
        if (window.at()) typeKeys("30")
        if (window.at()) {
            check(window.addDialog.time === "09:30" && window.addDialog.acceptEnabled, "typing does not change the time: " + window.addDialog.time)
            realKey(Qt.Key_Return, 0, "")
        }
        if (window.at()) {
            if (!waitUntil(!window.addDialog.visible))
                return
            check(window.addedReminders.length === 1 && window.addedReminders[0] === DatesHelper.today + " 14:30",
                  "Enter applies the typed time and adds the reminder: " + window.addedReminders.join(", "))
        }
    }
    /// The add dialog's typed time and the hint under it.
    function addTimeField(): BTextField {
        return window.dialogField(window.addDialog, "calendarAddTime") as BTextField
    }
    function addTimeHint(): Text {
        return window.dialogField(window.addDialog, "calendarAddTimeHint") as Text
    }
    property BTimePickerDialog firstClock: null
    property real scrollBefore: 0
    property Item clockOther: null
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
    property int taskRow: -1
    property string taskText: ""
    property TaskStatusMenu ownMenu: null
    /// The page's text when taskClockTest() began.
    property string taskOriginal: ""
    /// A row's TaskBox (the editor's, objectName "taskBox"), or null.
    function blockTaskBox(row: int): TaskBox {
        return window.findItem(editor.listView.itemAtIndex(row) as Item, "taskBox") as TaskBox
    }
    /// A query result's TaskBox for the task whose text starts with `prefix`.
    function resultTaskBox(prefix: string): TaskBox {
        const view = editor.listView.itemAtIndex(viewRow("query")) as Item
        for (let i = 0; i < window.countNamed(view, "queryTaskBox"); ++i) {
            const box = window.namedAt(view, "queryTaskBox", i)
            const resultRow = box.parent ? box.parent.parent : null
            if (resultRow && Js.indexOf("" + DevTest.property(resultRow, "text"), prefix) === 0)
                return box as TaskBox
        }
        return null
    }
    /// The query block's live results (QueryResultModel), or null.
    function queryResults(): QueryResultModel {
        const item = window.shownItem(viewRow("query"))
        const view = item ? item.contentItem as QueryBlockView : null
        return view ? view.results : null
    }
    /// The query's results show what the index has now: each row's page, line and status as a run
    /// of the query now gives them. A status written from a result re-runs the query a moment later
    /// (debounced, after the index followed the file) and replaces the rows: a gesture on a row
    /// before that went to a checkbox about to go, and its menu went with it.
    function queryResultsSettled(): bool {
        const results = queryResults()
        if (!results || !results.ready)
            return false
        queryNow.value = host.runQuery(results.source, page.path)
        const shownCount = Math.min(queryNow.lengthAt("items"), results.maxItems)
        if (queryNow.at("ok") !== true || results.rowCount() !== shownCount)
            return false
        for (let i = 0; i < shownCount; ++i) {
            queryShown.value = results.get(i)
            const key = "items." + i
            const status = queryNow.at(key + ".task") ? queryNow.at(key + ".task.status") : ""
            if ("" + queryShown.at("path") !== "" + queryNow.at(key + ".path")
                    || "" + queryShown.at("line") !== "" + queryNow.at(key + ".line")
                    || "" + queryShown.at("status") !== "" + status)
                return false
        }
        return true
    }
    /// A query's run (host.runQuery()) and a result (QueryResultModel.get()), read by the above.
    DevRecord { id: queryNow }
    DevRecord { id: queryShown }
    /// The last of the query's results whose text starts with `prefix`, -1 for none.
    function resultIndex(prefix: string): int {
        const results = queryResults()
        let found = -1
        for (let i = 0; results && i < results.rowCount(); ++i) {
            if (Js.indexOf("" + DevTest.valueAt(results.get(i), "text"), prefix) === 0)
                found = i
        }
        return found
    }
    /// That result's checkbox toggled (QueryResultModel.toggleTask()); false without one.
    function completeResult(prefix: string): bool {
        const at = window.resultIndex(prefix)
        if (at < 0)
            return false
        return window.queryResults().toggleTask(at)
    }
    /// A run of the query block's query now lists a task whose text starts with `prefix`.
    function queryNowLists(prefix: string): bool {
        const results = queryResults()
        if (!results)
            return false
        queryNow.value = host.runQuery(results.source, page.path)
        for (let i = 0; i < queryNow.lengthAt("items"); ++i) {
            if (Js.indexOf("" + queryNow.at("items." + i + ".text"), prefix) === 0)
                return true
        }
        return false
    }
    /// The status menu of a TaskBox (null without one).
    function statusMenuOf(box: TaskBox): TaskStatusMenu {
        return box ? box.statusMenu : null
    }
    /// The status menu of the result whose text starts with `prefix` is open.
    function resultMenuOpen(prefix: string): bool {
        const menu = window.statusMenuOf(resultTaskBox(prefix))
        return menu !== null && menu.opened
    }
    function editorMenuOpen(): bool {
        return editor.taskMenu !== null && editor.taskMenu.opened
    }
    function embedMenuOpen(): bool {
        const menu = window.statusMenuOf(embedTaskBox())
        return menu !== null && menu.opened
    }
    function menuIndexOf(menu: T.Menu, name: string): int {
        for (let i = 0; menu && i < menu.count; ++i) {
            const item = menu.itemAt(i) as Item
            if (item && item.objectName === name)
                return i
        }
        return -1
    }
    function menuItemNamed(menu: T.Menu, name: string): T.MenuItem {
        const at = window.menuIndexOf(menu, name)
        if (at < 0)
            return null
        return menu.itemAt(at) as T.MenuItem
    }
    /// A tap with a finger on touch UIs, else a left click, in the middle of `item`.
    function tapItem(item: Item): void {
        if (!item)
            return
        const x = item.width / 2
        const y = item.height / 2
        if (Theme.touchUi) {
            taskEvents.touchPress(item, x, y)
            taskEvents.touchRelease(item, x, y)
        } else {
            taskEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    /// The steps of the context-menu gesture on `target`: a right-click, or on touch UIs a finger
    /// held down until `shown` (TaskBox's press-and-hold opened the menu) and lifted — as a user
    /// lifts it once the menu shows. (A fixed hold lifted it too early in a slow sanitizer build: a
    /// tap, which toggled the task.) Both are read on every step, each step takes what they are then.
    function menuGesture(target: Item, shown: bool): void {
        if (!window.touchRun) {
            if (window.at()) {
                check(target !== null, "the checkbox is there for the right-click")
                if (target)
                    taskEvents.mouseClick(target, target.width / 2, target.height / 2, Qt.RightButton, Qt.NoModifier, -1)
            }
            return
        }
        if (window.at()) {
            check(target !== null, "the checkbox is there for the long press")
            if (target)
                taskEvents.touchPress(target, target.width / 2, target.height / 2)
        }
        if (window.at())
            window.waiting(shown)
        if (window.at()) {
            if (target)
                taskEvents.touchRelease(target, target.width / 2, target.height / 2)
        }
    }
    /// The touch toolbar's task button.
    function toolbarTask(): Item {
        return window.findItem(window.findItem(editor, "toolbarWriting"), "toolbarTask")
    }
    /// The page's text as saved.
    function pageText(): string {
        page.save()
        return host.file(page.path)
    }
    /// The shown TaskBox of the embedded section under the block "Budżet na październik".
    function embedTaskBox(): TaskBox {
        const row = editor.listView.itemAtIndex(rowOf("Budżet na październik") + 1) as Item
        for (let i = 0; i < window.countNamed(row, "embedTaskBox"); ++i) {
            const box = window.namedAt(row, "embedTaskBox", i)
            if (box.visible)
                return box as TaskBox
        }
        return null
    }
    /// The TransclusionView of the embedded section (the one with embedTaskBox()), or null.
    function embedView(): TransclusionView {
        let item = embedTaskBox() as Item
        while (item && !DevTest.hasMember(item, "shown"))
            item = item.parent
        return item as TransclusionView
    }
    function fileLine(path: string, prefix: string): string {
        return window.fileLineOf(host.file(path), prefix)
    }

    function taskStatusTest(): void {
        runSteps("taskstatus")
    }
    /// The steps of taskStatusTest() (see the step runner).
    function taskStatusSteps(): void {
        const gesture = window.touchRun ? "a long press" : "a right-click"
        const tap = window.touchRun ? "a tap" : "a click"
        if (window.at()) {
            window.taskRow = rowOf("Kupić płytki")
            editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
        }
        if (window.at()) check(info(window.taskRow).taskStatus === "todo" && blockTaskBox(window.taskRow) !== null,
                    "the block shows its checkbox")
        // ---- The editor's checkbox: the menu, a status, undo, a plain click, no status ---------
        menuGesture(blockTaskBox(window.taskRow), editorMenuOpen())
        if (window.at()) window.waitStep(editorMenuOpen(), gesture + " on a checkbox opens the status menu")
        if (window.at()) {
            const menu = editor.taskMenu
            check(info(window.taskRow).taskStatus === "todo", gesture + " does not toggle the task")
            const todo = menuItemNamed(menu, "taskStatus_todo")
            const doing = menuItemNamed(menu, "taskStatus_doing")
            check(todo !== null && todo.checked && doing !== null && !doing.checked
                  && menuItemNamed(menu, "taskStatus_none") !== null, "the current status is checked")
            tapItem(doing)
        }
        if (window.at()) window.waitStep(!editor.taskMenu.visible, "choosing a status closes the menu")
        if (window.at()) {
            check(info(window.taskRow).taskStatus === "doing" && Js.indexOf(pageText(), "\t- [/] Kupić płytki 📅 2026-10-16") > 0,
                  "the chosen status is written: " + fileLineOf(pageText(), "Kupić płytki"))
            page.undo()
        }
        if (window.at()) check(info(window.taskRow).taskStatus === "todo", "one undo step puts the status back")
        if (window.at()) tapItem(blockTaskBox(window.taskRow))
        if (window.at()) {
            check(info(window.taskRow).taskStatus === "done" && !editor.taskMenu.visible,
                  tap + " on the checkbox still toggles, without a menu")
            page.undo()
        }
        menuGesture(blockTaskBox(window.taskRow), editorMenuOpen())
        if (window.at()) window.waitStep(editorMenuOpen(), "the menu opens again")
        if (window.at()) {
            const doing = menuItemNamed(editor.taskMenu, "taskStatus_doing")
            check(!doing.checked && menuItemNamed(editor.taskMenu, "taskStatus_todo").checked,
                  "the check marks follow the status (not the last click)")
            tapItem(menuItemNamed(editor.taskMenu, "taskStatus_none"))
        }
        if (window.at()) window.waitStep(!editor.taskMenu.visible && info(window.taskRow).taskStatus === "", "“No status” removes the checkbox")
        if (window.at()) {
            check(info(window.taskRow).kind === "listItem" && Js.indexOf(info(window.taskRow).plainText, "Kupić płytki") === 0
                  && Js.indexOf(pageText(), "\t- Kupić płytki 📅 2026-10-16") > 0, "the text stays a list item: "
                  + fileLineOf(pageText(), "Kupić płytki"))
            check(blockTaskBox(window.taskRow) === null, "the checkbox is gone")
            page.undo()
        }
        if (window.at()) check(info(window.taskRow).taskStatus === "todo", "undo brings the checkbox back")
        // ---- Ctrl+Shift+Enter while editing: the menu at the caret, keys, the caret kept -------
        if (window.at()) {
            window.taskRow = rowOf("Wywieźć gruz")
            editor.startEditing(window.taskRow, 3)
        }
        if (window.at()) {
            window.taskText = editor.activeEditor.plain()
            realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at()) window.waitStep(editor.taskMenu.opened, "Ctrl+Shift+Enter opens the status menu of the edited block")
        if (window.at()) {
            check(info(window.taskRow).taskStatus === "todo", "Ctrl+Shift+Enter does not cycle the status")
            taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
        }
        if (window.at()) window.waitStep(!editor.taskMenu.visible && editor.activeEditor !== null && editor.activeEditor.activeFocus, "Escape closes the menu and the block has the keyboard again")
        if (window.at()) {
            check(editor.activeEditor.row === window.taskRow && editor.activeEditor.cursorPosition === 3
                  && info(window.taskRow).taskStatus === "todo", "nothing changed, the caret is where it was")
            realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
            window.waits = 0
        }
        if (window.at()) window.waitStep(editor.taskMenu.opened, "the menu opens again")
        if (window.at()) {
            // Down until "Waiting" is highlighted, then Enter.
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
        }
        if (window.at()) window.waitStep(!editor.taskMenu.visible && info(window.taskRow).taskStatus === "deferred", "Enter on “Oczekujące” sets the status")
        if (window.at()) window.waitStep(editor.activeEditor !== null && editor.activeEditor.activeFocus
                  && editor.activeEditor.row === window.taskRow, "the edited block gets the keyboard back")
        if (window.at()) {
            check(editor.activeEditor.cursorPosition === 3 && editor.activeEditor.plain() === window.taskText,
                  "the text and the caret are kept: " + editor.activeEditor.cursorPosition)
            check(Js.indexOf(pageText(), "\t- [>] Wywieźć gruz ⏳ 2026-10-18") > 0, "written as [>]")
            realKey(Qt.Key_Return, Qt.ControlModifier, "")
        }
        if (window.at()) check(info(window.taskRow).taskStatus !== "deferred" && !editor.taskMenu.visible,
                    "Ctrl+Enter still cycles, without the menu: " + info(window.taskRow).taskStatus)
        // A selected block (Esc): Ctrl+Shift+Enter opens its menu too, the selection keeps the keys.
        if (window.at()) { realKey(Qt.Key_Escape, 0, "") }
        if (window.at()) {
            check(page.selectedRow === window.taskRow, "Esc selects the block")
            EditorInput.sendKey(editor.listView, Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at()) window.waitStep(editor.taskMenu.opened, "Ctrl+Shift+Enter on a selected block opens its menu")
        if (window.at()) tapItem(menuItemNamed(editor.taskMenu, "taskStatus_cancelled"))
        if (window.at()) window.waitStep(!editor.taskMenu.visible && info(window.taskRow).taskStatus === "cancelled", "a status for the selected block")
        if (window.at()) window.waitStep(editor.listView.activeFocus && page.selectedRow === window.taskRow, "the selected block keeps the keyboard")
        // A heading gets no status: Ctrl+Shift+Enter opens no menu there (selected or edited).
        if (window.at()) {
            window.taskRow = rowOf("Koszty")
            check(window.taskRow >= 0 && info(window.taskRow).kind === "heading", "the page has the heading “Koszty”")
            window.taskText = pageText()
            page.selectedRow = window.taskRow
            EditorInput.sendKey(editor.listView, Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at()) {
            check(!editor.taskMenu.visible && pageText() === window.taskText,
                  "Ctrl+Shift+Enter on a selected heading opens no status menu")
            editor.startEditing(window.taskRow, -1)
        }
        if (window.at()) window.waitStep(editor.activeEditor !== null && editor.activeEditor.activeFocus
                 && editor.activeEditor.row === window.taskRow, "the heading is edited")
        if (window.at()) realKey(Qt.Key_Return, Qt.ControlModifier | Qt.ShiftModifier, "")
        if (window.at()) {
            check(!editor.taskMenu.visible && pageText() === window.taskText && editor.activeEditor !== null
                  && editor.activeEditor.activeFocus, "Ctrl+Shift+Enter in a heading opens no status menu")
            page.stopEditing()
        }
        if (window.at()) { page.selectedRow = -1 }
        // ---- "/" commands typed with real keys -----------------------------------------------
        if (window.at()) { window.taskRow = page.insertBlockAfter(rowOf("Ekipa może"), "") }
        if (window.at()) typeKeys("Zadzwonić do Żanety /czeka")
        if (window.at()) window.waitStep(editor.suggestionsOpen && window.suggestionModel().count > 0
                  && window.suggestionAt(0, "command") === "taskWaiting", "“/czeka” offers “Zadanie: oczekujące” first")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        // (The list closes with a transition: a step later it may still be fading out.)
        if (window.at() && !window.waiting(info(window.taskRow).taskStatus === "deferred" && editor.activeEditor.plain() === "Zadzwonić do Żanety "
                  && editor.activeEditor.cursorPosition === 20 && !editor.suggestionsOpen))
            window.checkWait("the command sets the status, its text goes, the caret stays: " + editor.activeEditor.plain())
        if (window.at()) page.undo()
        if (window.at()) check(editor.activeEditor.plain() === "Zadzwonić do Żanety /czeka" && info(window.taskRow).taskStatus === "",
                    "one undo step brings the command text back: " + editor.activeEditor.plain())
        if (window.at()) {
            const e = editor.activeEditor
            e.cursorPosition = e.length
            realKey(Qt.Key_Backspace, 0, "")
            realKey(Qt.Key_Backspace, 0, "")
            realKey(Qt.Key_Backspace, 0, "")
            realKey(Qt.Key_Backspace, 0, "")
            realKey(Qt.Key_Backspace, 0, "")
            typeKeys("zrob")
        }
        if (window.at()) window.waitStep(editor.suggestionsOpen && window.suggestionAt(0, "command") === "task", "“/zrob” offers the statuses (to do first)")
        if (window.at()) {
            // Down to "Task: done", Enter.
            let done = -1
            for (let i = 0; i < window.suggestionModel().count; ++i) {
                if (window.suggestionAt(i, "command") === "taskDone")
                    done = i
            }
            check(done > 0, "“Zadanie: zrobione” is listed")
            for (let i = 0; i < done; ++i)
                realKey(Qt.Key_Down, 0, "")
            realKey(Qt.Key_Return, 0, "")
        }
        if (window.at()) check(info(window.taskRow).taskStatus === "done" && Js.indexOf(pageText(), "- [x] Zadzwonić do Żanety ✅ 2026-10-15") > 0,
                    "“Zadanie: zrobione” completes the task: " + fileLineOf(pageText(), "Żanety"))
        if (window.at()) typeKeys(" /bez")
        if (window.at()) window.waitStep(editor.suggestionsOpen && window.suggestionAt(0, "command") === "taskNone", "“/bez” offers “Zadanie: bez statusu”")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(info(window.taskRow).taskStatus === "" && Js.indexOf(fileLineOf(pageText(), "Żanety"), "\t- Zadzwonić do Żanety") === 0,
                    "“Zadanie: bez statusu” removes the checkbox and its date: " + fileLineOf(pageText(), "Żanety"))
        // A paragraph becomes a task; a Logseq keyword keeps its workflow.
        if (window.at()) {
            page.stopEditing()
            window.taskRow = page.insertMarkdown(page.lastVisibleRow(), "Akapit o płytkach")
        }
        if (window.at()) editor.startEditing(rowOf("Akapit o płytkach"), -1)
        if (window.at()) typeKeys(" /zadanie")
        if (window.at()) window.waitStep(editor.suggestionsOpen && window.suggestionAt(0, "command") === "task", "“/zadanie” offers “Zadanie: do zrobienia”")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) {
            const r = rowOf("Akapit o płytkach")
            check(r >= 0 && info(r).taskStatus === "todo" && info(r).kind === "listItem"
                  && Js.indexOf(pageText(), "- [ ] Akapit o płytkach") >= 0, "a paragraph becomes a task")
            page.stopEditing()
            window.taskRow = page.insertBlockAfter(rowOf("Ekipa może"), "TODO Odebrać kafelki")
        }
        if (window.at()) check(info(window.taskRow).taskStatus === "todo", "a Logseq TODO is a task")
        if (window.at()) typeKeys(" /toku")
        if (window.at()) window.waitStep(editor.suggestionsOpen && window.suggestionAt(0, "command") === "taskDoing", "“/toku” offers “Zadanie: w toku”")
        if (window.at()) realKey(Qt.Key_Return, 0, "")
        if (window.at()) check(info(window.taskRow).taskStatus === "doing" && Js.indexOf(pageText(), "- DOING Odebrać kafelki") > 0,
                    "the Logseq keyword follows: " + fileLineOf(pageText(), "kafelki"))
        if (window.at()) page.stopEditing()
        // ---- A query result's checkbox: its own menu, the task in its own file ----------------
        if (window.at()) {
            page.save()
            editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning)
        }
        // The page's edits above re-run the query too: its rows are replaced until it settled.
        if (window.at()) window.waitStep(resultTaskBox("Mleko") !== null && resultTaskBox("Śrubki") !== null && queryResultsSettled(), "the query block lists the shopping tasks")
        menuGesture(resultTaskBox("Mleko"), resultMenuOpen("Mleko"))
        if (window.at()) window.waitStep(resultMenuOpen("Mleko"), gesture + " on a query result's checkbox opens its menu")
        // Another device adds a task above it while the menu is open (a sync): the results
        // wait for the menu — new rows would take it away — and the status goes to the task
        // where it is now, not to its old line.
        if (window.at()) {
            window.ownMenu = resultTaskBox("Mleko").statusMenu
            check(queryResults().held, "the open menu holds the results")
            host.externalWrite("Zakupy.md", "- [ ] Jajka #zakupy\n" + host.file("Zakupy.md"))
        }
        if (window.at()) window.waitStep(window.queryNowLists("Jajka"), "the index has the new task")
        if (window.at()) {}
        if (window.at()) {}
        if (window.at()) {} // longer than the results' debounce
        if (window.at()) {
            check(resultMenuOpen("Mleko") && resultTaskBox("Mleko").statusMenu === window.ownMenu,
                  "the menu stays open while the results change elsewhere")
            check(resultTaskBox("Jajka") === null, "the results wait for the menu")
            check(fileLine("Zakupy.md", "Mleko") === "- [ ] Mleko #zakupy", gesture + " does not toggle the result")
            check(menuItemNamed(window.ownMenu, "taskStatus_todo").checked, "its status is checked")
            tapItem(menuItemNamed(window.ownMenu, "taskStatus_deferred"))
        }
        if (window.at() && !window.waiting(Js.indexOf(host.file("Zakupy.md"), "- [ ] Jajka #zakupy\n- [>] Mleko #zakupy\n") === 0))
            window.checkWait("a result's status is written in its own file, on the task's line now: "
                        + window.lines(host.file("Zakupy.md")).slice(0, 2).join(" | "))
        // The next gesture once the results followed: "status:open" lists no waiting task, and
        // the task added meanwhile came.
        if (window.at()) window.waitStep(queryResultsSettled() && resultTaskBox("Mleko") === null && resultTaskBox("Śrubki") !== null
                       && resultTaskBox("Jajka") !== null && !queryResults().held, "the results follow once the menu closed: the waiting task is no open task any more, the new one is listed")
        menuGesture(resultTaskBox("Śrubki"), resultMenuOpen("Śrubki"))
        if (window.at()) window.waitStep(resultMenuOpen("Śrubki"), "the next result's menu opens")
        if (window.at()) tapItem(menuItemNamed(resultTaskBox("Śrubki").statusMenu, "taskStatus_none"))
        if (window.at() && !window.waiting(fileLine("Zakupy.md", "Śrubki") === "- Śrubki do półki #zakupy"))
            window.checkWait("“No status” on a result removes the task's checkbox in its file: " + fileLine("Zakupy.md", "Śrubki"))
        if (window.at()) window.waitStep(resultTaskBox("Śrubki") === null && queryResultsSettled(), "the result is no task any more")
        // ---- An embedded section's task (Budżet domowy#Październik) --------------------------
        if (window.at()) editor.listView.positionViewAtIndex(rowOf("Budżet na październik") + 1, ListView.Center)
        if (window.at()) window.waitStep(embedTaskBox() !== null, "the embedded section shows its task's checkbox")
        menuGesture(embedTaskBox(), embedMenuOpen())
        if (window.at()) window.waitStep(embedMenuOpen(), gesture + " on an embedded task opens its menu")
        // The section changes elsewhere while the menu is open: the embed waits, the status
        // goes to the task's line now.
        if (window.at()) {
            window.ownMenu = embedTaskBox().statusMenu
            host.externalWrite("Budżet domowy.md", Js.replace(host.file("Budżet domowy.md"), "- [ ] Opłacić czynsz", "- Prąd: 250 zł\n- [ ] Opłacić czynsz"))
        }
        if (window.at()) window.waitStep(embedView() !== null && Js.indexOf(Js.stringify(embedView().transclusions), "Prąd") >= 0, "the block's embeds follow the file")
        if (window.at()) {}
        if (window.at()) {}
        if (window.at()) {
            check(embedMenuOpen() && embedTaskBox().statusMenu === window.ownMenu, "the embed's menu stays open")
            check(Js.indexOf(Js.stringify(embedView().shown), "Prąd") < 0, "the embed waits for the menu")
            tapItem(menuItemNamed(window.ownMenu, "taskStatus_done"))
        }
        if (window.at() && !window.waiting(fileLine("Budżet domowy.md", "czynsz") === "- [x] Opłacić czynsz 📅 2026-10-20 #finanse ✅ 2026-10-15"))
            window.checkWait("an embedded task's status is written in its own file: " + fileLine("Budżet domowy.md", "czynsz"))
        if (window.at()) check(fileLine("Budżet domowy.md", "Prąd") === "- Prąd: 250 zł", "the line above it is left alone")
        if (window.at()) window.waitStep(embedView() !== null && Js.indexOf(Js.stringify(embedView().shown), "Prąd") >= 0, "the embed follows once the menu closed")
        // ---- The touch toolbar: the task button held down opens the menu above it --------------
        if (window.at()) {
            Theme.forceTouch = true
            window.taskRow = rowOf("Położyć hydroizolację")
            editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
            editor.startEditing(window.taskRow, -1)
        }
        if (window.at())
            window.waitStep(window.toolbarTaskShown(), "the touch toolbar shows its task button")
        if (window.at()) {
            const button = window.toolbarTask()
            taskEvents.touchPress(button, button.width / 2, button.height / 2)
        }
        if (window.at()) window.waiting(editorMenuOpen())
        if (window.at()) {
            const button = window.toolbarTask()
            taskEvents.touchRelease(button, button.width / 2, button.height / 2)
        }
        if (window.at()) window.waitStep(editorMenuOpen(), "holding the toolbar's task button opens the status menu")
        if (window.at()) {
            const button = window.toolbarTask()
            const menuBottom = window.windowY(editor.taskMenu.contentItem, 0, editor.taskMenu.contentItem.height)
            const buttonTop = window.windowY(button, 0, 0)
            check(menuBottom <= buttonTop + 1, "the menu is above the toolbar: " + menuBottom + " ≤ " + buttonTop)
            check(info(window.taskRow).taskStatus === "todo", "holding the button does not cycle the status")
            tapItem(menuItemNamed(editor.taskMenu, "taskStatus_done"))
        }
        if (window.at()) window.waitStep(!editor.taskMenu.visible && info(window.taskRow).taskStatus === "done", "a status from the toolbar's menu")
        if (window.at()) tapItem(window.toolbarTask())
        if (window.at()) check(info(window.taskRow).taskStatus === "", "a tap on the task button cycles (done → none)")
        if (window.at()) {
            page.stopEditing()
            Theme.forceTouch = DevTools.touch
        }
    }
    function webImageBox(): BDialog {
        return window.dialogs().webImageDialog
    }
    /// The task dependency picker's tasks.
    function dependencyItems(): var {
        return editor.dependencyDialog.items
    }
    function fileLinkBox(): FilePicker {
        return window.dialogs().fileLinkDialog
    }
    /// The fields of the "Picture from the internet…" dialog.
    function webImageSave(): T.AbstractButton {
        return DevTest.objectProperty(window.dialogs().webImageDialog, "saveBox") as T.AbstractButton
    }
    function webImageUrl(): BTextField {
        return DevTest.objectProperty(window.dialogs().webImageDialog, "urlField") as BTextField
    }
    /// The page's pictures from the internet (PageController.webPictureUrls()).
    function webUrls(): list<string> {
        return page.webPictureUrls()
    }
    /// The target of a delegate's first embed ("-" without one).
    function embedTarget(item: BlockDelegate): string {
        if (!item || DevTest.lengthAt(item.embeds, "") === 0)
            return "-"
        return "" + DevTest.valueAt(item.embeds, "0.target")
    }
    /// The first embed of a delegate failed to load.
    function embedFailed(item: BlockDelegate): bool {
        return item !== null && DevTest.lengthAt(item.embeds, "") > 0 && DevTest.valueAt(item.embeds, "0.state") === "failed"
    }
    /// The first embed of a delegate is from the internet.
    function embedIsWeb(item: BlockDelegate): bool {
        return item !== null && DevTest.valueAt(item.embeds, "0.web") === true
    }
    /// The width and height of a delegate's embeds (0 without a delegate).
    function embedWidth(item: BlockDelegate): real {
        return item ? window.embedViewOf(item).width : 0
    }
    function embedHeight(item: BlockDelegate): real {
        return item ? window.embedViewOf(item).height : 0
    }
    /// An embedded picture has loaded into its frame.
    function pictureReady(frame: Item, img: Image): bool {
        if (frame === null || img === null || img.status !== Image.Ready)
            return false
        return frame.height > 0
    }
    /// ... from the cache, with its shape (3:1).
    function cachedPicture(frame: Item, img: Image): bool {
        if (!window.pictureReady(frame, img) || Math.abs(frame.width / frame.height - 3) >= 0.1)
            return false
        return window.fromCache(img)
    }
    function fromCache(img: Image): bool {
        return img !== null && Js.startsWith("" + img.source, "file:")
    }
    /// The picture viewer is open with its picture loaded.
    function viewerReady(viewer: T.Popup, img: Image): bool {
        if (viewer === null || !viewer.visible || img === null)
            return false
        return img.status === Image.Ready
    }
    /// The dependency picker closed and the dependency written (🆔 and ⛔).
    function dependencyWritten(): bool {
        if (editor.dependencyDialog.visible)
            return false
        return Js.indexOf(pageText(), "- [ ] Wywieźć gruz ⛔ ") >= 0 && Js.indexOf(pageText(), "- [ ] Skuć stare płytki 🆔 ") >= 0
    }
    /// The touched block menu stayed where it began (blocklinks).
    function menuStayed(): bool {
        if (!editor.blockMenu.touched)
            return false
        return Math.abs(window.menuTop(editor.blockMenu) - window.menuTopBefore) < 1
    }
    /// The task menu closed and the timer written into the task's LOGBOOK (taskclock).
    function timerWritten(): bool {
        if (editor.taskMenu.visible)
            return false
        return Js.indexOf(pageText(), "\t- [ ] Wywieźć gruz ⏳ 2026-10-18\n\t  :LOGBOOK:\n\t  CLOCK: [2026-10-15 Thu 10:00:00]\n\t  :END:\n") >= 0
    }
    /// `length` characters of `text` from the first `needle`.
    function textAround(text: string, needle: string, length: int): string {
        const at = Js.indexOf(text, needle)
        return Js.slice(text, at, at + length)
    }
    /// The edited table's raw text of cell (r, c).
    function cellText(r: int, c: int): string {
        return editor.editingTable.rawText(r, c)
    }
    /// The rendered text of `row` with its no-break spaces as spaces (chips use them).
    function rowHtml(row: int): string {
        return Js.replaceRegExp(info(row).html, "&nbsp;|\u00a0", "g", " ")
    }
    // ---- The task menu's timer and dependency (`--editor-script taskclock`, tst_editor_qml_taskclock*,
    // docs/SYNTAX.md §5): "Start timer" writes a running CLOCK into the task's :LOGBOOK:
    // (its chip "⏱ from 10:00"), "Stop timer" closes it, one undo step each; "Add dependency…"
    // opens the picker, a search finds the task of the page, choosing it writes 🆔 / ⛔ and the
    // ⛔ chip shows that task's text.
    function taskClockTest(): void {
        window.taskOriginal = host.file(page.path)
        runSteps("taskclock")
    }
    /// The steps of taskClockTest() (see the step runner).
    function taskClockSteps(): void {
        const original = window.taskOriginal
        if (window.at()) {
            window.taskRow = rowOf("Wywieźć gruz")
            editor.listView.positionViewAtIndex(window.taskRow, ListView.Center)
        }
        menuGesture(blockTaskBox(window.taskRow), editorMenuOpen())
        if (window.at()) window.waitStep(editorMenuOpen(), "the task's menu opens")
        if (window.at()) {
            const start = menuItemNamed(editor.taskMenu, "taskClockToggle")
            check(start !== null && start.visible && start.text === "Uruchom licznik czasu",
                  "the menu offers the timer: " + (start ? start.text : "none"))
            check(menuItemNamed(editor.taskMenu, "taskAddDependency") !== null, "and “Dodaj zależność…”")
            tapItem(start)
        }
        if (window.at() && !window.waiting(window.timerWritten()))
            window.checkWait("the timer is written to the task's LOGBOOK: " + Js.stringify(window.textAround(pageText(), "Wywieźć", 120)))
        if (window.at()) window.waitStep(Js.indexOf(rowHtml(window.taskRow), "⏱ od 10:00") >= 0, "the task shows its running timer")
        menuGesture(blockTaskBox(window.taskRow), editorMenuOpen())
        if (window.at()) window.waitStep(editorMenuOpen(), "the menu opens again")
        if (window.at()) {
            const stop = menuItemNamed(editor.taskMenu, "taskClockToggle")
            check(stop !== null && stop.text === "Zatrzymaj licznik czasu", "it offers to stop the timer")
            tapItem(stop)
        }
        if (window.at()) window.waitStep(Js.indexOf(pageText(), "CLOCK: [2026-10-15 Thu 10:00:00]--[2026-10-15 Thu 10:00:00] =>  00:00:00") >= 0, "stopping closes the CLOCK line")
        if (window.at()) page.undo()
        if (window.at()) page.undo()
        if (window.at()) check(pageText() === original, "two undo steps take both back")
        // ---- A dependency through the picker ------------------------------------------------
        menuGesture(blockTaskBox(window.taskRow), editorMenuOpen())
        if (window.at()) window.waitStep(editorMenuOpen(), "the menu opens for a dependency")
        if (window.at()) tapItem(menuItemNamed(editor.taskMenu, "taskAddDependency"))
        if (window.at()) window.waitStep(editor.dependencyDialog !== null && editor.dependencyDialog.opened, "the picker opens")
        if (window.at()) {
            const field = findItem(editor.dependencyDialog.contentItem, "taskDependencySearch") as TextInput
            check(field !== null, "the picker has its search field")
            field.text = "skuc"
        }
        if (window.at()) {
            check(DevTest.lengthAt(window.dependencyItems(), "") === 1
                  && DevTest.valueAt(window.dependencyItems(), "0.text") === "Skuć stare płytki",
                  "a search without diacritics finds the task: " + Js.stringify(window.dependencyItems()))
            editor.dependencyDialog.tryAccept()
        }
        if (window.at() && !window.waiting(window.dependencyWritten()))
            window.checkWait("choosing it writes 🆔 and ⛔: " + fileLineOf(pageText(), "Wywieźć gruz"))
        if (window.at()) window.waitStep(Js.indexOf(rowHtml(rowOf("Wywieźć gruz")), "⛔ Skuć stare płytki") >= 0, "the ⛔ chip shows the task it waits for")
        if (window.at()) {
            page.undo()
            host.setFile(page.path, original)
        }
    }
    // ---- Links to blocks and headings (`--editor-script blocklinks`, tst_editor_qml_blocklinks*) --
    // "Copy link" with real input: the block menu from a right-click on a heading's text, a
    // paragraph, a callout and a calc block (desktop) — a finger's long press on text selects the
    // block, no menu (--touch) —, from a bullet (right-click / long press) and from the touch
    // toolbar's "More" (the menu above the bar); the submenu's items put the link on the
    // clipboard (read back by pasting) and say so; Ctrl+Shift+C while editing, on a selected block
    // and with nothing selected, the palette's heading link; a copied link followed by a click or
    // tap scrolls to its block or heading, selects and flashes it — again after scrolling away —, a
    // missing heading is said; a table: "More" on the cell toolbar, Ctrl+Shift+C in a cell, the
    // palette after the cell lost the keyboard; (desktop) a right-click on a title drawn above the
    // page's properties.
    final property string lastNotice: ""
    /// The last toast's button, else null.
    final property EditorToastAction lastNoticeAction: null
    /// The playground's toasts (the self-tests press their buttons).
    final readonly property BToastHost toastHost: toasts
    property string copiedId: ""
    property string expectedLink: ""
    property Item focusProbe: null
    property int linkRow: -1
    property int targetRow: -1
    Connections {
        target: editor
        function onMessage(text: string, kind: string, action: EditorToastAction) {
            window.lastNotice = text
            window.lastNoticeAction = action
        }
    }
    Component {
        id: clipboardProbeComponent
        TextEdit { visible: false }
    }
    /// What the clipboard holds (pasted into a hidden text field).
    function clipboardText(): string {
        const edit = clipboardProbeComponent.createObject(window.contentItem) as TextEdit
        edit.paste()
        const text = edit.text
        edit.destroy()
        return text
    }
    function delegateAt(row: int): BlockDelegate {
        editor.listView.forceLayout()
        return editor.listView.itemAtIndex(row) as BlockDelegate
    }
    /// The rendered rich text under `item` (a visible Text or TextEdit in RichText — a text with
    /// chips is a TextEdit), or null.
    function renderedIn(item: Item): Item {
        return DevTest.findRendered(item)
    }
    /// linkAt(x, y) and the text of a rendered text (a Text or a TextEdit).
    function linkAt(text: Item, x: real, y: real): string {
        return DevTest.linkAt(text, x, y)
    }
    function textOfRendered(text: Item): string {
        return "" + DevTest.property(text, "text")
    }
    property real linkX: 0
    property real linkY: 0
    /// A point of the rendered `text` on its link whose URL contains `url` (linkX, linkY), or false.
    function linkPointIn(text: Item, url: string): bool {
        for (let y = 2; text && y < text.height; y += 3) {
            for (let x = 2; x < text.width; x += 4) {
                if (Js.indexOf(window.linkAt(text, x, y), url) >= 0) {
                    window.linkX = x + 2
                    window.linkY = y + 2
                    return true
                }
            }
        }
        return false
    }
    /// A tap with a finger on touch UIs, else a left click, at (x, y) of `item`.
    function tapPoint(item: Item, x: real, y: real): void {
        if (Theme.touchUi) {
            taskEvents.touchPress(item, x, y)
            taskEvents.touchRelease(item, x, y)
        } else {
            taskEvents.mouseClick(item, x, y, Qt.LeftButton, Qt.NoModifier, -1)
        }
    }
    function rightClick(item: Item, x: real, y: real): void {
        if (item)
            taskEvents.mouseClick(item, x, y, Qt.RightButton, Qt.NoModifier, -1)
    }
    /// Where the block menu began before the keyboard moved, and its gap above the toolbar's
    /// "More" then (blocklinks).
    property real menuTopBefore: 0
    property real menuGapBefore: 0
    /// The y of (x, y) of `item` in the window.
    function windowY(item: Item, x: real, y: real): real {
        window.mapped2 = item.mapToItem(null, x, y)
        return window.mapped2.y
    }
    /// The y of (x, y) of `item` in `target`.
    function mappedY(item: Item, target: Item, x: real, y: real): real {
        window.mapped2 = item.mapToItem(target, x, y)
        return window.mapped2.y
    }
    /// From the block menu's bottom to the top of the toolbar's "More" under it.
    function menuGap(): real {
        const background = editor.blockMenu.background
        return window.windowY(window.findItem(editor, "toolbarMore"), 0, 0) - window.windowY(background, 0, background.height)
    }
    function blockMenuGone(): bool {
        return editor.blockMenu === null || !editor.blockMenu.opened
    }
    function blockMenuOpen(): bool {
        return editor.blockMenu !== null && editor.blockMenu.opened
    }
    /// Where `menu` begins in the window.
    function menuTop(menu: T.Menu): real {
        return window.windowY(menu.background, 0, 0)
    }
    /// The block menu's entry of the "Copy link" submenu, and the submenu.
    function copyEntry(): T.MenuItem {
        const menu = editor.blockMenu
        for (let i = 0; menu && i < menu.count; ++i) {
            const item = menu.itemAt(i) as T.MenuItem
            if (item && item.subMenu && item.subMenu.objectName === "copyLinkMenu")
                return item
        }
        return null
    }
    function copySubmenu(): T.Menu {
        const entry = copyEntry()
        return entry ? entry.subMenu : null
    }
    /// Taps the link whose URL contains `url` in the rendered text of `row` (nothing without one).
    function tapLinkIn(row: int, url: string): void {
        const text = renderedOf(row)
        if (linkPointIn(text, url))
            tapPoint(text, window.linkX, window.linkY)
    }
    /// Scrolls the editing toolbar's tools to their end.
    function scrollToolbarToEnd(): void {
        const flick = findItem(editor, "editToolbarFlick") as Flickable
        flick.contentX = Math.max(0, flick.contentWidth - flick.width)
    }
    /// Steps: open "Copy link", choose `name` there; the clipboard then holds `expected`
    /// (read on each of these steps) and the toast says so.
    function copySteps(name: string, what: string, expected: string): void {
        if (window.at()) {
            check(copyEntry() !== null && copyEntry().text === copySubmenu().title, "the block menu has “Copy link”")
            tapItem(copyEntry())
        }
        if (window.at())
            window.waitStep(copySubmenu() !== null && copySubmenu().opened, "“Copy link” opens its submenu")
        if (window.at())
            check(menuTop(copySubmenu()) >= T.Overlay.overlay.SafeArea.margins.top,
                  "the submenu begins below the window's status bar: " + menuTop(copySubmenu()))
        if (window.at())
            tapItem(menuItemNamed(copySubmenu(), name))
        if (window.at() && !window.waiting(!blockMenuOpen() && clipboardText() === expected))
            window.checkWait(what + ": " + clipboardText() + " (" + expected + ")")
        if (window.at())
            check(Js.endsWith(window.lastNotice, " " + expected) && window.lastNotice.length > expected.length + 3,
                  "a toast says what was copied: " + window.lastNotice)
    }
    /// The touch toolbar shows its task button.
    function toolbarTaskShown(): bool {
        const writing = findItem(editor, "toolbarWriting")
        const button = writing ? findItem(writing, "toolbarTask") : null
        return editor.toolbarVisible && button !== null && button.width > 0
    }
    /// The block menu is right above the toolbar, every item shown (blocklinks).
    function menuFollowsToolbar(): bool {
        const menu = editor.blockMenu
        return menu.visible && menuGap() >= 0 && menuGap() < 24 && !(menu.contentItem as Flickable).interactive
    }
    /// Steps of the menu gesture on the bullet of `row`: a right-click, or a finger held down until
    /// the block menu opened (a bullet's long press) and lifted. (A fixed hold could be lifted too
    /// early in a slow sanitizer run: a tap, which zooms into the block.)
    function bulletMenuGesture(row: int): void {
        if (!window.touchRun) {
            if (window.at()) {
                const item = delegateAt(row)
                rightClick(item, item.bulletX, item.firstLineCenter)
            }
            return
        }
        if (window.at()) {
            const item = delegateAt(row)
            taskEvents.touchPress(item, item.bulletX, item.firstLineCenter)
        }
        if (window.at())
            window.waiting(blockMenuOpen())
        if (window.at()) {
            const item = delegateAt(row)
            taskEvents.touchRelease(item, item.bulletX, item.firstLineCenter)
        }
    }

    function blockLinksTest(): void {
        runSteps("blocklinks")
    }
    /// The steps of blockLinksTest() (see the step runner).
    function blockLinksSteps(): void {
        const touch = window.touchRun
        const gesture = touch ? "a long press" : "a right-click"
        const doc = "Wyjazd w Tatry"
        if (window.at()) page.path = host.documentPath
        if (window.at()) window.waitStep(page.loaded && page.documentMode && rowOf("Plan") > 0, "the document is open")

        if (!touch) {
            // ---- A heading's link from a right-click on its text --------------------------------
            if (window.at()) {
                window.targetRow = rowOf("Plan")
                window.textBefore = pageText()
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            }
            if (window.at()) {
                const text = renderedOf(window.targetRow)
                check(text !== null, "the heading shows its text")
                rightClick(text, text.width / 4, text.height / 2)
            }
            if (window.at()) window.waitStep(blockMenuOpen() && editor.blockMenu.heading, "a right-click on a heading's text opens the block menu")
            if (window.at()) check(DevTest.property(menuItemNamed(editor.blockMenu, "blockMenuZoom"), "shown") === false, "a document has no zoom in the menu")
            copySteps("copyHeadingLink", "“Link to heading” copies [[Page#Heading]]", "[[" + doc + "#Plan]]")
            if (window.at()) check(pageText() === window.textBefore, "a heading's link changes nothing in the page")
            // ---- A paragraph: its block reference, the id written once -------------------------
            if (window.at()) {
                window.targetRow = rowOf("Pierwszego dnia")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            }
            if (window.at()) {
                const text = renderedOf(window.targetRow)
                rightClick(text, text.width / 2, text.height / 2)
            }
            if (window.at()) window.waitStep(blockMenuOpen() && !editor.blockMenu.heading, "a right-click on a paragraph opens the block menu")
            if (window.at()) check(DevTest.property(menuItemNamed(copySubmenu(), "copyHeadingLink"), "shown") === false, "no heading link for a paragraph")
            copySteps("copyBlockRef", "“Block reference” copies ((id))", "((" + info(window.targetRow).blockId + "))")
            if (window.at()) {
                const id = info(window.targetRow).blockId
                check(id.length === 6 && Js.indexOf(pageText(), "tłoczno. ^" + id) > 0, "the paragraph got its id: " + id)
            }
            // ---- Frames: a callout and (later, in the outline) a calc block ---------------------
            if (window.at()) {
                window.targetRow = viewRow("callout")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            }
            if (window.at()) {
                const item = delegateAt(window.targetRow)
                rightClick(item, item.width / 2, item.height / 2)
            }
            if (window.at()) window.waitStep(blockMenuOpen(), "a right-click on a callout opens the block menu")
            if (window.at()) taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
            if (window.at()) window.waitStep(!blockMenuOpen(), "Escape closes it")
        } else {
            // ---- A finger's long press on text opens no menu (left for selecting text) ---------
            if (window.at()) {
                window.targetRow = rowOf("Pierwszego dnia")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            }
            if (window.at()) {
                const text = renderedOf(window.targetRow)
                taskEvents.touchPress(text, text.width / 2, text.height / 2)
            }
            // Held until the long press took effect: it selects the block (the selection mode).
            if (window.at()) window.waiting(editor.selectionMode && page.selectedRow === window.targetRow)
            if (window.at()) {
                const text = renderedOf(window.targetRow)
                if (text)
                    taskEvents.touchRelease(text, text.width / 2, text.height / 2)
            }
            if (window.at()) check(editor.selectionMode && page.selectedRow === window.targetRow
                        && (editor.blockMenu === null || !editor.blockMenu.visible),
                        "a finger's long press on text selects the block and opens no block menu")
            if (window.at()) editor.leaveSelectionMode()
        }
        // ---- Ctrl+Shift+C: editing, selected, nothing; the palette's heading link -----------------
        if (window.at()) {
            page.stopEditing()
            window.targetRow = rowOf("Piątek")
            editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            editor.startEditing(window.targetRow, -1)
        }
        if (window.at()) window.waitStep(editor.activeEditor !== null && editor.activeEditor.activeFocus, "a list item is edited")
        if (window.at()) realKey(Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
        if (window.at() && !window.waiting(info(window.targetRow).blockId !== "" && clipboardText() === "((" + info(window.targetRow).blockId + "))"))
            window.checkWait("Ctrl+Shift+C copies the edited block's reference: " + clipboardText())
        if (window.at()) {
            window.copiedId = info(window.targetRow).blockId
            check(Js.endsWith(window.lastNotice, " ((" + window.copiedId + "))"), "and says so: " + window.lastNotice)
            check(editor.activeEditor !== null && editor.activeEditor.activeFocus && page.editingRow === window.targetRow,
                  "the block is still being edited")
            editor.copyCurrentLink("heading") // the palette's "Copy link to heading"
        }
        if (window.at() && !window.waiting(clipboardText() === "[[" + doc + "#Plan]]"))
            window.checkWait("the palette's heading link names the heading of the caret's section: " + clipboardText())
        if (window.at()) realKey(Qt.Key_Escape, 0, "")
        if (window.at()) {
            check(page.selectedRow === window.targetRow, "Esc selects the block")
            EditorInput.sendKey(editor.listView, Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at()) window.waitStep(clipboardText() === "((" + window.copiedId + "))", "Ctrl+Shift+C on a selected block, the same id")
        if (window.at()) {
            page.selectedRow = -1
            window.lastNotice = ""
            window.textBefore = pageText()
            EditorInput.sendKey(editor.listView, Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at() && !window.waiting(window.lastNotice === editor.copyFailure("noRow")))
            window.checkWait("with nothing selected it says what to do: " + window.lastNotice)
        if (window.at()) check(pageText() === window.textBefore, "and changes nothing")
        // ---- Following copied links: scroll, select, flash — twice; a missing heading ------------
        if (window.at()) {
            window.linkRow = page.insertBlockAfter(page.lastVisibleRow(),
                "Zobacz [[" + doc + "#^" + window.copiedId + "]], [[" + doc + "#Plan]] i [[#Nieistniejący]]")
            page.stopEditing()
            editor.listView.positionViewAtEnd()
        }
        if (window.at()) {
            const text = renderedOf(window.linkRow)
            check(text !== null && Js.indexOf(window.textOfRendered(text), doc + " › Piątek") >= 0,
                  "[[Page#^id]] reads as the page and the block's text")
            const p = linkPointIn(text, "#^" + window.copiedId)
            check(p, "the link to the block is there")
            if (p)
                tapPoint(text, window.linkX, window.linkY)
        }
        if (window.at()) window.waitStep(page.selectedRow === window.targetRow && findShown(delegateAt(window.targetRow), "blockFlash") !== null, "following [[Page#^id]] selects the block and flashes it")
        if (window.at()) {
            const item = delegateAt(window.targetRow)
            const top = window.mappedY(item, editor.listView, 0, 0)
            check(top >= 0 && top + item.height <= editor.listView.height, "the block is scrolled into view: " + top)
            page.selectedRow = -1
            editor.listView.positionViewAtEnd()
        }
        if (window.at()) tapLinkIn(window.linkRow, "#^" + window.copiedId)
        if (window.at()) window.waitStep(page.selectedRow === window.targetRow, "the same link a second time, after scrolling away, goes there again")
        if (window.at()) window.waitStep(findShown(delegateAt(window.targetRow), "blockFlash") === null, "the flash fades away")
        if (window.at()) {
            check(delegateAt(window.targetRow).height > 0, "the flash changed no height")
            editor.listView.positionViewAtEnd()
        }
        if (window.at()) tapLinkIn(window.linkRow, "#Plan")
        if (window.at()) window.waitStep(page.selectedRow === rowOf("Plan"), "following [[Page#Heading]] selects the heading")
        if (window.at()) {
            editor.listView.positionViewAtEnd()
            window.lastNotice = ""
        }
        if (window.at()) tapLinkIn(window.linkRow, "#Nieistniej")
        if (window.at() && !window.waiting(Js.indexOf(window.lastNotice, "Nieistniejący") >= 0 && window.lastNotice !== ""))
            window.checkWait("a missing heading is said: " + window.lastNotice)
        // ---- The touch toolbar's "More": the block menu above the bar ------------------------------
        if (window.at()) {
            page.selectedRow = -1
            Theme.forceTouch = true
            window.targetRow = rowOf("Niedziela")
            editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            editor.startEditing(window.targetRow, -1)
        }
        if (window.at()) window.waitStep(editor.toolbarVisible && findItem(editor, "toolbarMore") !== null, "the touch toolbar has “More”")
        if (window.at()) {
            // A phone's keyboard and the status bar over an edge-to-edge window's top (Android
            // 11+): the menu, taller than the room above the toolbar, stays below the bar —
            // a finger's tap there reached the status bar, not the menu (the emulator).
            T.Overlay.overlay.SafeArea.additionalMargins.top = 48
            KeyboardInsets.simulated = 336
        }
        if (window.at()) scrollToolbarToEnd()
        if (window.at()) tapItem(findItem(editor, "toolbarMore"))
        if (window.at()) window.waitStep(blockMenuOpen(), "“More” on the toolbar opens the block menu")
        if (window.at()) {
            const button = findItem(editor, "toolbarMore")
            const menu = editor.blockMenu
            const menuBottom = window.windowY(menu.contentItem, 0, menu.contentItem.height)
            const buttonTop = window.windowY(button, 0, 0)
            check(menuBottom <= buttonTop + 1, "the menu is above the toolbar: " + menuBottom + " ≤ " + buttonTop)
            check(menuTop(menu) >= 48 + 8 - 1, "and below the status bar: " + menuTop(menu) + " ≥ 56")
            check(editor.blockMenu.listItem && !editor.blockMenu.heading, "it is the edited block's menu")
            window.menuGapBefore = menuGap()
            window.menuTopBefore = menuTop(menu)
            // A finger already on it: it stays where it is when the keyboard goes.
            const item = menuItemNamed(menu, "blockMenuCopyText")
            taskEvents.touchPress(item, item.width / 2, item.height / 2)
        }
        if (window.at()) KeyboardInsets.simulated = 0
        if (window.at()) {
            const item = menuItemNamed(editor.blockMenu, "blockMenuCopyText")
            check(window.menuStayed(),
                  "touched, the menu does not move when the keyboard goes: " + menuTop(editor.blockMenu) + " vs "
                  + window.menuTopBefore)
            // Lifted off the menu (at the window's top): nothing chosen.
            window.mapped2 = item.mapFromItem(null, 20, 20)
            taskEvents.touchMove(item, window.mapped2.x, window.mapped2.y)
            taskEvents.touchRelease(item, window.mapped2.x, window.mapped2.y)
        }
        if (window.at()) {
            check(editor.blockMenu.visible, "the menu stays open")
            editor.blockMenu.close()
            KeyboardInsets.simulated = 336
        }
        if (window.at()) window.waitStep(!editor.blockMenu.visible && editor.toolbarVisible, "closed, the toolbar back on the keyboard")
        if (window.at()) tapItem(findItem(editor, "toolbarMore"))
        if (window.at()) window.waitStep(blockMenuOpen() && !editor.blockMenu.touched, "“More” opens the block menu again")
        // The menu took the keys: the system hides the keyboard, the toolbar drops to the bottom.
        if (window.at()) KeyboardInsets.simulated = 0
        if (window.at() && !window.waiting(window.menuFollowsToolbar()))
            window.checkWait("the untouched menu follows the toolbar down when the keyboard goes, right above it, every "
                  + "item shown (none scrolled away): " + menuGap() + " px above it (" + window.menuGapBefore
                  + " before), scrolls " + (editor.blockMenu.contentItem as Flickable).interactive)
        if (window.at()) check(window.windowY(menuItemNamed(editor.blockMenu, "blockMenuDelete"), 0, 0)
                    + menuItemNamed(editor.blockMenu, "blockMenuDelete").height
                    <= window.windowY(findItem(editor, "toolbarMore"), 0, 0),
                    "“Usuń blok”, its last item, is on screen above the toolbar")
        copySteps("copyBlockLink", "“Link to block” copies [[Page#^id]]",
                     "[[" + doc + "#^" + info(window.targetRow).blockId + "]]")
        if (window.at()) {
            check(info(window.targetRow).blockId !== "" && page.editingRow === window.targetRow,
                  "the block got its id and is still being edited")
            page.stopEditing()
            T.Overlay.overlay.SafeArea.additionalMargins.top = 0
            KeyboardInsets.simulated = -1
            Theme.forceTouch = DevTools.touch
        }
        // ---- An outline: the bullet's menu; a calc block's (desktop) ------------------------------
        if (window.at()) page.path = host.samplePath
        if (window.at()) window.waitStep(page.loaded && !page.documentMode && rowOf("Kolejność") > 0, "the outline is open")
        if (window.at()) {
            // A list item without a checkbox (a task's box sits over its bullet on touch screens).
            window.targetRow = rowOf("Kolejność")
            editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
        }
        bulletMenuGesture(window.targetRow)
        if (window.at()) window.waitStep(blockMenuOpen(), gesture + " on a bullet opens the block menu")
        if (window.at()) check(DevTest.property(menuItemNamed(editor.blockMenu, "blockMenuZoom"), "shown") === true, "with zoom in an outline")
        if (window.at()) check(page.zoomBlock < 0, "and does not zoom")
        copySteps("copyEmbed", "“Embed” copies !((id))", "!((" + info(window.targetRow).blockId + "))")
        // ---- A table: "More" on the cell toolbar; the edited cell's table is current ---------------
        if (window.at()) {
            page.selectedRow = -1
            Theme.forceTouch = true
            window.targetRow = viewRow("table")
            editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
        }
        if (window.at()) {
            const item = delegateAt(window.targetRow)
            check(item !== null, "the table is on screen")
            if (item)
                item.focusContent()
        }
        if (window.at()) window.waitStep(editor.toolbarMode === "cell" && findShown(findItem(editor, "editToolbarSlot"), "toolbarCellMore") !== null, "a table cell's toolbar has “More”")
        if (window.at()) scrollToolbarToEnd()
        if (window.at()) tapItem(findShown(findItem(editor, "editToolbarSlot"), "toolbarCellMore"))
        if (window.at()) window.waitStep(blockMenuOpen(), "“More” on the cell toolbar opens the block menu")
        if (window.at()) {
            check(editor.blockMenu.row() === window.targetRow && !editor.blockMenu.listItem, "it is the table's menu")
            check(editor.editingTable === null && page.selectedRow === window.targetRow,
                  "the cell's editing ended, the table is selected")
        }
        copySteps("copyBlockRef", "“Block reference” of a table copies ((id))",
                     "((" + info(window.targetRow).blockId + "))")
        if (window.at()) {
            Theme.forceTouch = DevTools.touch
            page.selectedRow = -1
            const item = delegateAt(window.targetRow)
            if (item)
                item.focusContent()
        }
        if (window.at()) window.waitStep(editor.editingTable !== null && window.cellInput() !== null
                       && window.cellInput().activeFocus && page.selectedRow < 0, "a cell is edited (nothing selected)")
        if (window.at()) {
            window.lastNotice = ""
            EditorInput.sendKey(window.cellInput(), Qt.Key_C, Qt.ControlModifier | Qt.ShiftModifier, "")
        }
        if (window.at() && !window.waiting(Js.endsWith(window.lastNotice, " ((" + info(window.targetRow).blockId + "))")))
            window.checkWait("Ctrl+Shift+C in a cell copies its table's reference: " + window.lastNotice)
        if (window.at()) {
            // The keyboard goes elsewhere (the palette's field): the cell's editing ends and the
            // table stays the current block.
            window.focusProbe = focusProbeComponent.createObject(window.contentItem) as Item
            window.focusProbe.forceActiveFocus()
        }
        if (window.at() && !window.waiting(editor.editingTable === null && page.selectedRow === window.targetRow))
            window.checkWait("a cell left for another field leaves its table selected: " + page.selectedRow)
        if (window.at()) {
            window.expectedLink = page.linkText(window.targetRow, "blockLink").text
            check(Js.indexOf(window.expectedLink, "#^" + info(window.targetRow).blockId + "]]") > 0, "the table's link: " + window.expectedLink)
            editor.copyCurrentLink("blockLink") // the palette's "Copy link to block"
        }
        if (window.at() && !window.waiting(clipboardText() === window.expectedLink))
            window.checkWait("the palette then copies the table's link: " + clipboardText())
        if (window.at()) {
            window.focusProbe.destroy()
            window.focusProbe = null
            page.selectedRow = -1
            editor.listView.forceActiveFocus()
        }
        if (!touch) {
            if (window.at()) {
                window.targetRow = viewRow("calc")
                editor.listView.positionViewAtIndex(window.targetRow, ListView.Center)
            }
            if (window.at()) {
                const item = delegateAt(window.targetRow)
                rightClick(item, item.width / 2, item.height / 2)
            }
            if (window.at()) window.waitStep(blockMenuOpen() && page.editingRow < 0, "a right-click on a calc block opens the block menu (no editing)")
            if (window.at()) taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
            if (window.at()) window.waitStep(!blockMenuOpen(), "Escape closes it")
            // ---- The title drawn above the properties: the menu of the block drawn there ---------
            if (window.at()) page.path = host.recipePath
            if (window.at()) window.waitStep(page.loaded && page.titleRow > 0 && editor.titleFirst && renderedOf(page.titleRow) !== null, "the recipe (properties, then its title) is open")
            if (window.at()) editor.listView.positionViewAtBeginning()
            if (window.at()) {
                const text = renderedOf(page.titleRow)
                check(text !== null && window.windowY(text, 0, 0) < window.windowY(delegateAt(page.titleRow - 1), 0, 0),
                      "the title is drawn above the properties")
                if (text)
                    rightClick(text, text.width / 4, text.height / 2)
            }
            if (window.at()) window.waitStep(blockMenuOpen() && editor.blockMenu.heading && editor.blockMenu.row() === page.titleRow, "a right-click on that title opens the title's block menu")
            if (window.at()) taskEvents.keyClick(Qt.Key_Escape, Qt.NoModifier, -1)
            // Gone, its closing transition too: a menu the next click opened would be visible at once.
            if (window.at()) window.waitStep(!editor.blockMenu.visible, "Escape closes it")
            if (window.at()) {
                const properties = delegateAt(page.titleRow - 1)
                rightClick(properties, properties.width - 8, properties.height - 4)
            }
            if (window.at()) check(!editor.blockMenu.visible, "a right-click on the properties drawn below it opens no block menu")
        }
    }
    // The editor's dialogs (EditorDialogs), read through functions of this file.
    function dialogs(): EditorDialogs {
        return editor.dialogs as EditorDialogs
    }
    function reminderBox(): BReminderDialog {
        return window.dialogs().reminderDialog
    }
    function dateBox(): BDialog {
        return window.dialogs().dateDialog
    }
    function recurrenceBox(): BDialog {
        return window.dialogs().recurrenceDialog
    }
    function embedViewOf(item: BlockDelegate): EmbedView {
        return item ? DevTest.objectProperty(item, "embedView") as EmbedView : null
    }
    /// A table view's rows (a typed read).
    function rowsOf(table: TableBlockView): list<var> {
        return table.rows
    }
    /// A reminder chosen in `dialog` as its Save does (both of its signals).
    function chooseReminder(dialog: BReminderDialog, at: string, alarm: bool, recurrence: string): void {
        dialog.reminderAccepted(Js.withKey(Js.withKey(Js.withKey({}, "at", at), "alarm", alarm), "recurrence", recurrence))
        dialog.reminderChosen(at, alarm, recurrence)
    }
    /// The edited block's row and text, for the checks' messages ("none" when nothing is edited).
    function editorState(): string {
        const edit = editor.activeEditor
        return edit ? edit.row + " " + Js.stringify(edit.plain()) : "none"
    }
    function renderedOf(row: int): Item {
        return renderedIn(delegateAt(row))
    }

    // ---- The touch toolbar on a simulated keyboard (`--editor-script keyboard`) -----------------
    // Run with --touch --keyboard 0 (ctest tst_editor_qml_keyboard_touch): the toolbar is docked on
    // the keyboard's top for a block (outline and document), the source and a table cell, follows
    // a keyboard that slides in frame by frame without scrolling until it has settled, stays at the
    // bottom when the system hides the keyboard while editing goes on; the caret, the cell and the
    // autocomplete stay above it.
    function toolbarSlot(): Loader {
        return findItem(editor, "editToolbarSlot") as Loader
    }
    function slotTop(): real {
        const slot = toolbarSlot()
        return slot ? window.windowY(slot, 0, 0) : -1
    }
    function slotBottom(): real {
        const slot = toolbarSlot()
        return slot ? window.windowY(slot, 0, slot.height) : -1
    }
    function near(a: real, b: real): bool {
        return Math.abs(a - b) <= 1
    }
    /// The toolbar is shown with its bottom edge on a keyboard of `px` (0: the window's bottom).
    function dockedOn(px: real): bool {
        const slot = toolbarSlot()
        return editor.toolbarVisible && slot !== null && slot.visible && slot.item !== null
               && near(slotBottom(), window.height - px)
    }
    function caretBottom(): real {
        const edit = editor.activeEditor
        if (!edit || !edit.activeFocus)
            return 1e9
        window.cursorRect = edit.cursorRectangle
        return window.windowY(edit, window.cursorRect.x, window.cursorRect.y + window.cursorRect.height)
    }
    function sourceCaretBottom(): real {
        const edit = editor.sourceTextEdit
        if (!edit)
            return 1e9
        window.cursorRect = edit.cursorRectangle
        return window.windowY(edit, window.cursorRect.x, window.cursorRect.y + window.cursorRect.height)
    }
    /// The source's text as written (EditorInput.plainText()).
    function sourcePlain(): string {
        return EditorInput.plainText(editor.sourceTextEdit.textDocument)
    }
    /// A caret's rectangle (read through a typed property).
    property rect cursorRect
    function cellInput(): T.TextField {
        return editor.editingTable ? editor.editingTable.cellInput : null
    }
    function cellBottom(): real {
        const input = window.cellInput()
        return input ? window.windowY(input, 0, input.height) : 1e9
    }
    /// The shown item called `name` under `item` (hidden tool sets keep theirs), or null.
    function findShown(item: Item, name: string): Item {
        return DevTest.findShown(item, name)
    }
    function toolbarButton(name: string): Item {
        return findShown(toolbarSlot(), name)
    }
    /// Cell (r, c) is being edited, with the cell tools on the keyboard.
    function cellEdited(r: int, c: int): bool {
        const table = editor.editingTable
        return editor.toolbarMode === "cell" && table !== null && table.editRow === r && table.editColumn === c
               && window.cellInput() !== null && window.cellInput().activeFocus && dockedOn(320)
               && toolbarButton("toolbarCellTools") !== null
    }
    /// A finger's tap at (x, y) of `item`.
    function fingerTap(item: Item, x: real, y: real): void {
        taskEvents.touchPress(item, x, y)
        taskEvents.touchRelease(item, x, y)
    }
    /// The bottom of the suggestion popup in the window.
    function suggestionsBottom(): real {
        return window.windowY(editor, 0, editor.suggestions.y + editor.suggestions.height)
    }
    property real contentBefore: 0
    property Item toolsBefore: null
    property real toolsScroll: 0
    property int linesBefore: 0
    function keyboardTest(): void {
        runSteps("keyboard")
    }
    /// The steps of keyboardTest() (see the step runner).
    function keyboardSteps(): void {
        const lh = editor.lineHeight
        if (window.at()) {
            KeyboardInsets.simulated = 0
            // A text block at the end of the long page (the last rows may be a query or code).
            window.realRow = page.insertBlockAfter(page.lastVisibleRow(), "Ostatni punkt: zażółć gęślą jaźń")
            editor.startEditing(window.realRow, -1)
        }
        if (window.at() && !window.waiting(dockedOn(0) && editor.activeEditor !== null && editor.activeEditor.activeFocus))
            window.checkWait("no keyboard: the toolbar is docked on the editor's bottom edge: " + slotBottom() + " = " + window.height)
        // (a) The keyboard comes up: the toolbar sits on it, the caret above the toolbar.
        if (window.at()) KeyboardInsets.simulated = 320
        if (window.at() && !window.waiting(dockedOn(320)))
            window.checkWait("the toolbar sits on the keyboard: " + slotBottom() + " = " + (window.height - 320))
        if (window.at() && !window.waiting(caretBottom() <= slotTop() - lh / 2))
            window.checkWait("the caret of the last block is scrolled above the toolbar: " + caretBottom() + " ≤ " + slotTop())
        // (c) Typing with real keys: no runaway scrolling, the caret stays visible.
        if (window.at()) {
            window.contentBefore = editor.listView.contentY
            window.linesBefore = editor.activeEditor.lineCount
            typeKeys(" zażółć gęślą jaźń, Łódź i Kraków")
        }
        if (window.at() && !window.waiting(caretBottom() <= slotTop() && editor.listView.contentY - window.contentBefore
                       <= (editor.activeEditor.lineCount - window.linesBefore) * lh + 1))
            window.checkWait("typing keeps the caret above the toolbar, scrolling at most a line per new line: "
                       + (editor.listView.contentY - window.contentBefore) + " px for "
                       + (editor.activeEditor.lineCount - window.linesBefore) + " line(s)")
        if (window.at()) {
            window.contentBefore = editor.listView.contentY
            typeKeys("ąę")
        }
        if (window.at()) check(near(editor.listView.contentY, window.contentBefore) && caretBottom() <= slotTop(),
                    "typing on the same line does not scroll")
        if (window.at()) check(window.countNamed(editor, "editToolbarSlot") === 1, "one toolbar")
        // (d) The system hides the keyboard (Back) while the block is still being edited.
        if (window.at()) KeyboardInsets.simulated = 0
        if (window.at()) window.waitStep(dockedOn(0) && page.editingRow === window.realRow, "the keyboard hidden by the system: the toolbar stays docked at the bottom, editing goes on")
        // (e) The keyboard slides in frame by frame: the toolbar follows each frame, nothing
        // scrolls until it has settled. (The caret low on the screen first: the keyboard will
        // cover it.)
        if (window.at()) {
            const list = editor.listView
            list.contentY = Math.max(list.originY, list.contentY - ((window.height - 150) - caretBottom()))
        }
        if (window.at() && !window.waiting(caretBottom() > window.height - 320 && caretBottom() < window.height - editor.toolbarHeight))
            window.checkWait("the caret is low on the screen, where the keyboard will come: " + caretBottom())
        if (window.at()) {
            window.contentBefore = editor.listView.contentY
            KeyboardInsets.simulate(100, true)
        }
        if (window.at()) check(dockedOn(100) && near(editor.listView.contentY, window.contentBefore),
                    "100 px into the slide: the toolbar follows, the text does not move")
        if (window.at()) KeyboardInsets.simulate(200, true)
        if (window.at()) check(dockedOn(200) && near(editor.listView.contentY, window.contentBefore),
                    "200 px into the slide: the toolbar follows, the text does not move")
        if (window.at()) KeyboardInsets.simulate(320, true)
        if (window.at()) check(dockedOn(320) && near(editor.listView.contentY, window.contentBefore) && KeyboardInsets.animating,
                    "320 px, still sliding: the toolbar follows, the text does not move")
        if (window.at()) KeyboardInsets.simulate(320, false)
        if (window.at() && !window.waiting(caretBottom() <= slotTop() - lh / 2))
            window.checkWait("the keyboard settled: the caret is scrolled above the toolbar: " + caretBottom() + " ≤ " + slotTop())
        // (i) Autocomplete near the keyboard stays above the toolbar.
        if (window.at()) {
            const row = page.insertBlockAfter(page.lastVisibleRow(), "")
            editor.startEditing(row, -1)
        }
        if (window.at()) window.waitStep(editor.activeEditor !== null && editor.activeEditor.activeFocus && dockedOn(320), "a new block at the end is edited with the toolbar on the keyboard")
        if (window.at()) typeKeys("Zobacz [[Re")
        if (window.at()) window.waitStep(editor.suggestionsOpen && editor.suggestions.opened, "[[ near the keyboard opens the suggestions")
        if (window.at() && !window.waiting(window.suggestionsBottom() <= slotTop() + 1))
            window.checkWait("the suggestions are above the toolbar: "
                  + window.suggestionsBottom() + " ≤ " + slotTop())
        if (window.at()) {
            editor.closeSuggestions()
            page.undo()
            // The tools scrolled to their end before the editing stops.
            const flick = toolbarButton("editToolbarFlick") as Flickable
            flick.contentX = flick.contentWidth - flick.width
            window.toolsScroll = flick.contentX
            window.toolsBefore = toolbarButton("toolbarBlockTools")
            page.stopEditing()
        }
        if (window.at()) window.waitStep(!editor.toolbarVisible, "stopping the editing takes the toolbar away")
        // The next edit shows the same tools (not made again), where they were scrolled to.
        if (window.at()) editor.startEditing(window.realRow, -1)
        if (window.at()) window.waitStep(editor.toolbarMode === "block" && dockedOn(320) && toolbarButton("toolbarBlockTools") !== null, "editing again brings the toolbar back")
        if (window.at()) check(window.toolsScroll > 0 && toolbarButton("toolbarBlockTools") === window.toolsBefore
                    && near((toolbarButton("editToolbarFlick") as Flickable).contentX, window.toolsScroll),
                    "the block tools are kept between two edits, scrolled where they were: "
                    + (toolbarButton("editToolbarFlick") as Flickable).contentX + " = " + window.toolsScroll)
        if (window.at()) page.stopEditing()
        if (window.at()) window.waitStep(!editor.toolbarVisible, "the editing stops again")
        // (g) Source mode: the toolbar with the source tools on the keyboard.
        if (window.at()) editor.toggleSourceMode()
        if (window.at()) window.waitStep(page.sourceMode && editor.sourceTextEdit !== null && editor.sourceTextEdit.activeFocus, "source mode: the source has the keyboard")
        if (window.at()) window.waitStep(editor.toolbarMode === "source" && dockedOn(320) && toolbarButton("toolbarSourceTag") !== null, "source mode: the toolbar with the source tools sits on the keyboard")
        // A tap on the bar beside its buttons stays on the bar: it moved the caret to the
        // text under the bar.
        if (window.at()) editor.sourceTextEdit.cursorPosition = 0
        if (window.at()) window.waitStep(sourceCaretBottom() < slotTop() / 2, "source mode: the caret at the start of the file")
        if (window.at()) {
            const slot = toolbarSlot()
            fingerTap(slot, slot.width - 24, slot.height / 2)
        }
        if (window.at()) check(editor.toolbarMode === "source" && editor.sourceTextEdit.activeFocus
                    && editor.sourceTextEdit.cursorPosition === 0,
                    "source mode: a tap on the bar beside its buttons leaves the caret where it was: "
                    + editor.sourceTextEdit.cursorPosition)
        if (window.at()) {
            const edit = editor.sourceTextEdit
            edit.cursorPosition = edit.length
        }
        if (window.at() && !window.waiting(sourceCaretBottom() <= slotTop()))
            window.checkWait("source mode: the caret at the end of the file is above the toolbar: " + sourceCaretBottom() + " ≤ " + slotTop())
        if (window.at()) {
            window.textBefore = window.sourcePlain()
            tapItem(toolbarButton("toolbarSourceTag"))
        }
        if (window.at()) window.waitStep(window.sourcePlain().length === window.textBefore.length + 1
                       && Js.indexOf(page.sourceText, "#") >= 0, "the toolbar's # goes into the source")
        if (window.at()) tapItem(toolbarButton("toolbarSourceUndo"))
        if (window.at()) window.waitStep(window.sourcePlain() === window.textBefore, "the toolbar's undo takes it back")
        if (window.at()) tapItem(toolbarButton("toolbarHideKeyboard"))
        if (window.at()) window.waitStep(editor.toolbarMode === "" && !editor.sourceTextEdit.activeFocus, "hide keyboard: the source gives up the keyboard and the toolbar goes")
        if (window.at()) editor.toggleSourceMode()
        if (window.at()) window.waitStep(!page.sourceMode, "back from source mode")
        // (h) A table cell: the cell tools, the cell above the toolbar, the next cell.
        if (window.at()) {
            window.realRow = viewRow("table")
            editor.listView.positionViewAtIndex(window.realRow, ListView.End)
        }
        if (window.at()) {
            const item = editor.listView.itemAtIndex(window.realRow) as BlockDelegate
            check(item !== null, "the table is on screen")
            if (item)
                item.focusContent()
        }
        if (window.at()) window.waitStep(editor.toolbarMode === "cell" && editor.editingTable !== null && window.cellInput() !== null
                       && dockedOn(320) && toolbarButton("toolbarNextCell") !== null, "a table cell: the toolbar with the cell tools sits on the keyboard")
        if (window.at() && !window.waiting(cellBottom() <= slotTop()))
            window.checkWait("the cell being edited is above the toolbar: " + cellBottom() + " ≤ " + slotTop())
        if (window.at()) {
            window.baseRow = editor.editingTable.editRow
            window.pasteRow = editor.editingTable.editColumn
        }
        // A word the keyboard is still composing (Android) goes into the cell left with the
        // toolbar's next cell.
        if (window.at()) EditorInput.sendInputMethod(window.cellInput(), "Kminek")
        if (window.at()) check(window.cellInput().preeditText === "Kminek", "a word composed in the cell is preedit")
        if (window.at()) tapItem(toolbarButton("toolbarNextCell"))
        if (window.at()) window.waitStep(editor.editingTable !== null && window.cellInput() !== null
                       && window.cellInput().activeFocus
                       && (editor.editingTable.editRow !== window.baseRow || editor.editingTable.editColumn !== window.pasteRow), "the toolbar's next cell moves to the next cell")
        if (window.at()) {
            const left = window.cellText(window.baseRow, window.pasteRow)
            check(Js.endsWith(left, "Kminek"), "the word being composed is kept in the cell left: " + left)
        }
        // Undo and redo keep the cell edited (the toolbar and the keyboard stay).
        if (window.at()) {
            const table = editor.editingTable
            window.baseRow = table.editRow
            window.pasteRow = table.editColumn
            window.textBefore = table.rawText(table.editRow, table.editColumn)
            EditorInput.sendInputMethod(table.cellInput, "Szczaw")
        }
        if (window.at()) tapItem(toolbarButton("toolbarCellUndo"))
        if (window.at() && !window.waiting(cellEdited(window.baseRow, window.pasteRow) && window.cellInput().text === window.textBefore
                       && window.cellText(window.baseRow, window.pasteRow) === window.textBefore))
            window.checkWait("the toolbar's undo takes the cell's typing back (the composed word too), the cell stays edited: "
                       + (window.cellInput() ? window.cellInput().text : "(no cell)"))
        if (window.at()) tapItem(toolbarButton("toolbarCellRedo"))
        if (window.at() && !window.waiting(cellEdited(window.baseRow, window.pasteRow)
                       && window.cellInput().text === window.textBefore + "Szczaw"))
            window.checkWait("the toolbar's redo brings it back, the cell stays edited: "
                       + (window.cellInput() ? window.cellInput().text : "(no cell)"))
        if (window.at()) tapItem(toolbarButton("toolbarCellUndo"))
        if (window.at()) window.waitStep(cellEdited(window.baseRow, window.pasteRow) && window.cellInput().text === window.textBefore, "undo again, still in the cell")
        // Hide keyboard ends the cell's editing, with the word being composed.
        if (window.at()) EditorInput.sendInputMethod(window.cellInput(), "Rzodkiew")
        if (window.at()) tapItem(toolbarButton("toolbarHideKeyboard"))
        if (window.at()) window.waitStep(editor.editingTable === null && !editor.toolbarVisible, "hide keyboard ends the cell's editing and the toolbar")
        if (window.at()) check(Js.indexOf(page.sourceText, "Rzodkiew") >= 0, "the word being composed is kept in the cell")
        // (f) The document mode: the same docking.
        if (window.at()) page.path = host.documentPath
        if (window.at()) window.waitStep(page.path === host.documentPath && page.loaded && page.documentMode, "the trip plan opens as a document")
        if (window.at()) {
            window.realRow = page.insertBlockAfter(page.lastVisibleRow(), "Na koniec: źródło przy schronisku")
            editor.startEditing(window.realRow, -1)
        }
        if (window.at() && !window.waiting(dockedOn(320) && caretBottom() <= slotTop() - lh / 2))
            window.checkWait("document mode: the toolbar on the keyboard, the caret above it: " + caretBottom() + " ≤ " + slotTop())
        if (window.at()) {
            page.stopEditing()
            KeyboardInsets.simulated = 0
        }
        if (window.at()) window.waitStep(!editor.toolbarVisible && editor.height > window.height - 400, "the keyboard and the toolbar are gone")
    }

    function lines(text: string): list<string> {
        return window.splitText(text, "\n")
    }
    function splitText(text: string, separator: string): list<string> {
        return Js.split(text, separator)
    }
    /// The first line of `text` that contains `needle`, or "".
    function fileLineOf(text: string, needle: string): string {
        return DevTest.lineWith(text, needle)
    }

    /// What tells a block's rendered text (BlockText) from the items around it.
    readonly property list<string> blockTextMembers: ["shownHtml", "hoveredLink"]
    /// The row (and a text) a scripted state remembers for runScriptLater().
    property int scriptRow: -1
    property string scriptText: ""
    /// The second half of a scripted state (`name`), after the list's layout (scriptLater).
    function runScriptLater(name: string): void {
        switch (name) {
        case "linkparts":
        case "linkparts-menu": {
            const row = name === "linkparts" ? 0 : 1
            const text = renderedIn(delegateAt(row))
            if (!text || !linkPointIn(text, "part:1:"))
                return
            if (name === "linkparts") {
                const block = findWith(delegateAt(row), window.blockTextMembers) as BlockText
                if (block)
                    block.hoveredLink = window.linkAt(text, window.linkX, window.linkY)
            } else {
                editor.openBlockLinkMenu(row, window.linkAt(text, window.linkX, window.linkY), text, window.linkX, window.linkY)
            }
            break
        }
        case "properties":
        case "properties-phone":
            editor.listView.positionViewAtBeginning()
            break
        case "link-flash":
            editor.activateLink(window.scriptRow, "page:Wyjazd%20w%20Tatry#%5E" + window.scriptText)
            break
        case "copylink-toolbar": {
            const flick = window.findItem(editor, "editToolbarFlick") as Flickable
            const more = window.findItem(editor, "toolbarMore")
            if (!flick || !more)
                return
            flick.contentX = Math.max(0, flick.contentWidth - flick.width)
            editor.openBlockMenuAbove(window.scriptRow, more)
            break
        }
        case "copylink-table":
            if (editor.editingTable)
                editor.openTableBlockMenu(editor.editingTable)
            break
        case "copylink":
        case "copylink-document":
        case "selection-menu": {
            const item = editor.listView.itemAtIndex(window.scriptRow) as BlockDelegate
            if (!item)
                return
            editor.openBlockMenu(window.scriptRow, item, item.contentLeft + Theme.space8, item.firstLineCenter + editor.lineHeight / 2)
            if (name !== "selection-menu")
                copyEntryLater.restart()
            break
        }
        case "task-menu": {
            const box = blockTaskBox(window.scriptRow)
            if (box)
                editor.openTaskStatusMenu(window.scriptRow, box, 0, box.height)
            break
        }
        case "recurrence-menu":
        case "link-menu":
        case "date-menu":
        case "date-menu-reminder": {
            const text = renderedIn(delegateAt(window.scriptRow))
            const scheme = name === "link-menu" ? "tag:" : name === "recurrence-menu" ? "recur:"
                         : name === "date-menu" ? "date:" : "reminder:"
            if (!text || !linkPointIn(text, scheme))
                return
            if (name === "link-menu")
                editor.openBlockLinkMenu(window.scriptRow, window.linkAt(text, window.linkX, window.linkY), text, window.linkX, window.linkY)
            else
                editor.openDateMenu(window.scriptRow, window.linkAt(text, window.linkX, window.linkY), text, window.linkX, window.linkY)
            break
        }
        case "task-menu-query": {
            const box = resultTaskBox("Mleko")
            if (box)
                box.openMenu()
            break
        }
        case "text-menu": {
            const edit = editor.activeEditor
            if (!edit)
                return
            edit.select(0, 11)
            window.cursorRect = edit.cursorRectangle
            editor.openTextMenu(edit, window.cursorRect.x, window.cursorRect.y + window.cursorRect.height / 2)
            break
        }
        }
    }
    /// "copylink": the "Copy link" entry opened once the block menu is up (Qt.callLater).
    Later {
        id: copyEntryLater
        onTriggered: {
            const entry = window.copyEntry()
            if (entry)
                entry.triggered()
        }
    }
    /// Shows `row` in the middle and opens its menu `name` after the layout (scriptLater).
    function scriptMenuAt(row: int, name: string): void {
        editor.listView.positionViewAtIndex(row, ListView.Center)
        window.scriptRow = row
        scriptLater.run(name)
    }
    function runScript(name: string): void {
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
        case "taskclock":
            taskClockTest()
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
        case "spelltest":
            spellingTestLoader.active = true
            break
        case "tabletest":
            tableTestLoader.active = true
            break
        case "propstest":
            propertiesTestLoader.active = true
            break
        case "linkpartstest":
            linkPartsTestLoader.active = true
            break
        case "readingtest":
            readingTestLoader.active = true
            break
        case "linkparts":
        case "linkparts-menu": {
            // [[Firma/Adam cośtam]] as two links: the pointer resting on "Firma" (underlined) / the
            // menu of a folder's part (screenshots).
            host.setFile("Firma.md", "- Strona firmy\n")
            host.setFile("Firma/Adam cośtam.md", "- Adam\n")
            host.setFile("Projekty/Zażółć gęślą jaźń/Notatki.md", "- notatki\n")
            host.setFile("Części linków.md", "- Zapytać się [[Firma/Adam cośtam]] o to czego się pouczyć\n"
                + "- Notatki w [[Projekty/Zażółć gęślą jaźń/Notatki]] i [[Nowy dział/Plan]]\n"
                + "- Z tekstem: [[Firma/Adam cośtam|Adam]]\n")
            page.path = "Części linków.md"
            scriptLater.run(name)
            break
        }
        case "properties":
        case "properties-phone": {
            // The page properties panel expanded on a page with every type (screenshots; the phone
            // layout comes with --touch / a narrow --size).
            host.setFile("Projekty/Remont garażu.md", "---\nstatus: w toku\nkoszt: 1200\ngotowe: false\n"
                + "termin: 2026-10-28\nkiedy: 2026-10-30T09:30\ntags: [projekt, dom]\naliases: [Łazienka]\n"
                + "autorzy:\n  - \"[[Jan Kowalski]]\"\n  - Anna\nopis: \"Płytki z [[Castorama]] #pilne\"\n"
                + "pusta:\n---\n# Remont garażu\n\n- Kupić płytki\n- Zamówić hydraulika\n")
            editor.setPropertiesExpanded(true)
            page.path = "Projekty/Remont garażu.md"
            scriptLater.run(name)
            break
        }
        case "link-flash": {
            // A copied [[Page#^id]] (shown as the page and the block's text) followed: the block
            // selected and flashing (screenshots; --delay 1600 lands in the flash).
            const target = rowOf("Piątek")
            const id = page.ensureBlockRef(target)
            const after = page.insertBlockAfter(rowOf("Niedziela"), "")
            page.setBody(after, "Zobacz [[Wyjazd w Tatry#^" + id + "]] i [[Wyjazd w Tatry#Plan]].")
            page.stopEditing()
            window.scriptRow = after
            window.scriptText = id
            scriptLater.run(name)
            break
        }
        case "copylink-toolbar": {
            // Touch: the editing toolbar's "More" — the block menu above the bar (screenshots).
            const row = rowOf("Kolejność")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            editor.startEditing(row, -1)
            window.scriptRow = row
            scriptLater.run(name)
            break
        }
        case "copylink-table": {
            // Touch: a table cell's toolbar "More" — the cell left, the table's block menu at it
            // (screenshots).
            const row = viewRow("table")
            editor.listView.positionViewAtIndex(row, ListView.Center)
            const item = editor.listView.itemAtIndex(row) as BlockDelegate
            if (item)
                item.focusContent()
            scriptLater.run(name)
            break
        }
        case "copylink":
        case "copylink-document":
            // The block menu with "Copy link" open on a heading (screenshots).
            scriptMenuAt(rowOf(name === "copylink" ? "Koszty" : "Plan"), name)
            break
        case "task-menu":
            // The status menu of a block's checkbox (screenshots).
            scriptMenuAt(rowOf("Kupić płytki"), name)
            break
        case "recurrence-menu":
        case "link-menu":
            // The menu of a 🔁 rule / of a #tag (screenshots).
            scriptMenuAt(rowOf(name === "link-menu" ? "Kupić płytki" : "Podlać kwiaty"), name)
            break
        case "date-menu":
        case "date-menu-reminder":
            // The menu of a 📅 / ⏰ chip (screenshots).
            scriptMenuAt(rowOf(name === "date-menu" ? "Kupić płytki" : "Zadzwonić do hydraulika"), name)
            break
        case "task-menu-query":
            // The status menu of a query result's checkbox (screenshots).
            editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning)
            scriptLater.run(name)
            break
        case "task-slash":
            typeInto(page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), ""), "Odebrać płytki /zad")
            break
        case "editing":
            editor.startEditing(rowOf("Kupić płytki"), -1)
            break
        case "suggest":
            typeInto(page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), ""), "Porównać z [[Re")
            break
        case "tags":
            typeInto(page.insertBlockAfter(rowOf("Wywieźć gruz"), ""), "Zamówić kontener #za")
            break
        case "tag-pages":
            // `#` offers pages too (a tag names a page): pages no tag names yet, with their folder.
            typeInto(page.insertBlockAfter(rowOf("Wywieźć gruz"), ""), "Porównać z #rem")
            break
        case "slash":
            typeInto(page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), ""), "Zapytać o termin /")
            break
        case "date":
            typeInto(page.insertBlockAfter(rowOf("Zadzwonić do hydraulika"), ""), "Odebrać płytki ⏰ jutro o 9")
            break
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
            // floating tools; --touch: the selection mode with "One by one" on) — screenshots.
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
            // The block menu of a block in a selection: Copy 3 blocks… (screenshots).
            const from = rowOf("Zadzwonić do hydraulika")
            const to = rowOf("Prace")
            page.selectRange(from, to)
            window.scriptRow = from
            scriptLater.run(name)
            break
        }
        case "text-menu": {
            // The edited text's menu (desktop right-click): Cut, Copy, Paste, Select all.
            const row = rowOf("Kupić płytki")
            editor.startEditing(row, -1)
            scriptLater.run(name)
            break
        }
        case "source":
            editor.toggleSourceMode()
            break
        case "reminder":
            window.dialogs().createReminder(rowOf("Zadzwonić do hydraulika"), false)
            break
        case "recurrence":
            window.dialogs().pickRecurrence(rowOf("Podlać kwiaty"))
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
        case "query":
            editor.listView.positionViewAtIndex(viewRow("query"), ListView.Beginning)
            break
        case "dataview": {
            // ```dataview blocks (docs/SYNTAX.md §10 "Dataview queries"): a TABLE and a TASK query.
            const row = page.insertBlockAfter(viewRow("query"), "```dataview\nTABLE status, budżet AS \"Budżet\", "
                                              + "length(file.tasks) AS Zadania, file.etags AS Tagi\nFROM #projekt\n```")
            page.insertBlockAfter(row, "```dataview\nTASK WHERE !completed AND contains(tags, \"#zakupy\")\n```")
            page.stopEditing()
            editor.listView.positionViewAtIndex(row, ListView.Beginning)
            break
        }
        case "mermaid":
        case "mermaid-editing": {
            // ```mermaid blocks: a flowchart drawn, a type Brulion does not draw yet (its source and
            // a note); "mermaid-editing": the source being edited with the diagram under it.
            const row = page.insertBlockAfter(rowOf("Ekipa może"), "```mermaid\nflowchart LR\n"
                                              + "  A[Pomysł] --> B{Czy warto?}\n  B -->|Tak| C[Zrób to]\n"
                                              + "  B -->|Nie| D([Odłóż])\n  C -.-> E[(Notatki)]\n```")
            page.insertBlockAfter(row, "```mermaid\nclassDiagram\n  Notatka <|-- Dziennik\n```")
            page.stopEditing()
            editor.listView.positionViewAtIndex(row, ListView.Beginning)
            if (name === "mermaid-editing")
                editor.startEditing(row, -1)
            break
        }
        case "chart":
        case "chart-filter": {
            // A ```chart of the sample's table (its "Razem" — total — column) under it, and a pie of the calc
            // block; "chart-filter": the table's filter row with a filter typed.
            const table = viewRow("table")
            const row = page.insertBlockAfter(table, "```chart\ntyp: słupkowy\nwartości: Ilość, Cena\ntytuł: Materiały\n```")
            page.insertBlockAfter(row, "```chart\nobliczenia: powyżej\ntyp: kołowy\n```")
            page.stopEditing()
            if (name === "chart-filter") {
                page.setTableFilterOpen(table, true)
                page.setTableFilter(table, 2, "> 40")
            }
            editor.listView.positionViewAtIndex(name === "chart-filter" ? table : row, ListView.Beginning)
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
            host.externalWrite(page.path, Js.replace(host.file(page.path), "- Prace", "- Prace na telefonie"))
            break
        }
    }
}
