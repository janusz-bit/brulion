pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import Brulion

// ReadingSelfTest — reading mode (docs/UI.md "Reading mode") with real input, in the editor
// playground (`brulion --editor --editor-script readingtest`; ctest tst_editor_qml_reading,
// desktop, and tst_editor_qml_reading_touch, a phone with --touch): with PageController.readOnly
// on, a click / tap on a paragraph's text edits nothing and the toast says why, its "Edit" leaving
// the mode; a tap on a task's checkbox changes nothing; a link still opens; the block menu (a
// bullet's right-click / long press) offers copying, not Cut, Move or Delete; Enter and Delete on
// a selected block change nothing; off again, a tap edits, and turning it on while editing ends the
// editing with the text kept. The file stays byte for byte as written throughout.
// Steps run on the playground's step runner (EditorPlayground.runSteps).
QtObject {
    id: test

    required property ApplicationWindow appWindow
    readonly property EditorPlayground window: test.appWindow as EditorPlayground
    required property BlockEditor editor
    required property PageController page
    required property FakeEditorHost host

    readonly property string path: "Do czytania.md"
    readonly property string content: "- [ ] Zadanie źdźbło\n"
        + "- Zwykły akapit do czytania, żółw i łódź\n"
        + "- Link do [[Inna strona]] tutaj\n"
        + "- Ostatni blok\n"
    property int taskRow: -1
    property int textRow: -1
    property int linkRow: -1
    property int navigationsBefore: 0
    property int navigations: 0
    /// The toast's "Edit" kept from the first refusal (pressed later).
    property EditorToastAction leave: null

    property Connections navigationWatch: Connections {
        target: test.host
        function onNavigated(route: var, newPane: bool) { test.navigations += 1 }
    }
    // The playground's host part: "Edit" in the toast leaves reading mode (the app: AppSettings).
    property Connections leaveWatch: Connections {
        target: test.editor
        function onReadOnlyLeaveRequested() { test.page.readOnly = false }
    }

    function check(ok: bool, what: string): void { window.check(ok, what) }
    function info(r: int): DevBlock { return window.info(r) }
    function rowWith(needle: string): int {
        for (let r = 0; r < page.blocks.count; ++r) {
            if (Js.indexOf(info(r).editText, needle) >= 0)
                return r
        }
        return -1
    }
    /// The rendered text of row `r`, or null.
    function textOf(r: int): Item { return window.renderedIn(window.delegateAt(r)) }
    /// A tap / click on the rendered text of row `r`, 6 px into its first line (no link there).
    function tapText(r: int): void {
        const t = textOf(r)
        check(t !== null, "row " + r + " is drawn")
        if (t)
            window.tapPoint(t, 6, Math.min(t.height / 2, 10))
    }
    function blockMenuItemShown(name: string): bool {
        const item = window.menuItemNamed(editor.blockMenu, name) as Item
        return item !== null && item.height > 0
    }
    function fileText(): string { return host.file(test.path) }

    function steps(): void {
        if (window.at()) {
            host.setFile("Inna strona.md", "- tam\n")
            host.setFile(test.path, test.content)
            page.path = test.path
        }
        if (window.at()) window.waitStep(page.loaded && rowWith("Zwykły") >= 0 && textOf(rowWith("Zwykły")) !== null, "the page is shown")
        if (window.at()) {
            taskRow = rowWith("Zadanie")
            textRow = rowWith("Zwykły")
            linkRow = rowWith("Link do")
            page.readOnly = true
            window.lastNotice = ""
            check(editor.readOnly, "the editor reads the controller's reading mode")
        }
        // ---- 1. a tap on text: nothing edited, the toast with "Edit" --------------------------
        if (window.at()) tapText(textRow)
        if (window.at()) window.waitStep(window.lastNotice === editor.readOnlyReason, "a tap on text says reading mode is on: " + window.lastNotice)
        if (window.at()) {
            check(page.editingRow < 0 && editor.activeEditor === null, "and no block is edited")
            test.leave = window.lastNoticeAction
            check(test.leave !== null && test.leave.kind === "leaveReadOnly" && test.leave.text !== "",
                  "the toast's button leaves reading mode")
        }
        // ---- 2. a task's checkbox: nothing changes ----------------------------------------------
        if (window.at()) window.tapItem(window.blockTaskBox(taskRow))
        if (window.at()) window.waitStep(true, "the checkbox tapped")
        if (window.at()) {
            check(info(taskRow).taskStatus === "todo", "the task stays open: " + info(taskRow).taskStatus)
            check(fileText() === test.content, "nothing is written")
        }
        // ---- 3. a link still opens ------------------------------------------------------------
        if (window.at()) {
            navigationsBefore = navigations
            const t = textOf(linkRow)
            const found = window.linkPointIn(t, "page:")
            check(found, "the link is drawn")
            if (found)
                window.tapPoint(t, window.linkX, window.linkY)
        }
        if (window.at() && !window.waiting(navigations > navigationsBefore))
            window.checkWait("a tap on the link opens its page: " + Js.stringify(host.lastRoute))
        if (window.at()) check(page.editingRow < 0, "and edits nothing")
        // ---- 4. the block menu: copying, no Cut, Move or Delete ---------------------------------
        window.bulletMenuGesture(textRow)
        if (window.at()) window.waitStep(window.blockMenuOpen(), "the bullet's menu opens")
        if (window.at()) {
            check(blockMenuItemShown("blockMenuCopy"), "it has Copy")
            check(!blockMenuItemShown("blockMenuCut"), "no Cut")
            check(!blockMenuItemShown("blockMenuMoveUp"), "no Move up")
            check(!blockMenuItemShown("blockMenuDelete"), "no Delete")
            check(!blockMenuItemShown("blockMenuDrawing"), "no Insert drawing")
            const menu = editor.blockMenu as T.Menu
            if (menu)
                menu.close()
        }
        if (window.at()) window.waitStep(window.blockMenuGone(), "the menu closes")
        // ---- 5. keys on a selected block: Delete and Enter change nothing ------------------------
        if (window.at()) {
            page.selectedRow = textRow
            editor.listView.forceActiveFocus()
        }
        if (window.at()) window.realKey(Qt.Key_Delete, Qt.NoModifier, "")
        if (window.at()) window.realKey(Qt.Key_Return, Qt.NoModifier, "")
        if (window.at()) window.waitStep(true, "Delete and Enter pressed")
        if (window.at()) {
            check(page.blocks.count === 4 && fileText() === test.content, "Delete removed nothing")
            check(page.editingRow < 0, "Enter edits nothing")
            page.clearSelection()
        }
        // ---- 6. "Edit" leaves reading mode; a tap edits again ---------------------------------
        if (window.at()) {
            if (test.leave)
                test.leave.run()
            check(!page.readOnly && !editor.readOnly, "“Edit” in the toast leaves reading mode")
        }
        if (window.at()) tapText(textRow)
        if (window.at()) window.waitStep(page.editingRow === textRow && editor.activeEditor !== null, "a tap edits the block again")
        // ---- 7. on again while editing: the editing ends, the text kept --------------------------
        if (window.at()) {
            window.lastNotice = ""
            page.readOnly = true
            check(page.editingRow < 0, "reading mode ends the editing")
        }
        if (window.at()) window.waitStep(editor.activeEditor === null || !editor.activeEditor.active, "the inline editor goes")
        if (window.at()) {
            check(window.lastNotice === "", "and no toast says an edit was refused: " + window.lastNotice)
            check(window.pageText() === test.content, "the file is still as written")
            page.readOnly = false
        }
    }

    Component.onCompleted: window.runSteps("readingtest")
}
